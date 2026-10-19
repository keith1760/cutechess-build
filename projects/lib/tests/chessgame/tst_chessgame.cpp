/*
    This file is part of Cute Chess.

    Cute Chess is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Cute Chess is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Cute Chess.  If not, see <http://www.gnu.org/licenses/>.
*/

// Tests ChessGame::rewindTo() (the GUI's move slider takes a running
// engine-vs-engine game back to an earlier position and carries on).
//
// The tests need real engines. They look for "stockfish" (UCI) and
// "gnuchess" (XBoard) in PATH and /usr/games, or in the environment
// variables CUTECHESS_TEST_UCI_ENGINE / CUTECHESS_TEST_XBOARD_ENGINE
// (the latter is started with the argument --xboard), and skip the
// protocol they cannot find.

#include <QtTest/QtTest>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>

#include <chessgame.h>
#include <chessplayer.h>
#include <gamemanager.h>
#include <enginebuilder.h>
#include <engineconfiguration.h>
#include <pgngame.h>
#include <timecontrol.h>
#include <board/board.h>
#include <board/boardfactory.h>
#include <board/result.h>
#include <board/genericmove.h>

namespace {

QMutex s_logMutex;
QStringList s_warnings;
QtMessageHandler s_oldHandler = nullptr;

void messageHandler(QtMsgType type, const QMessageLogContext& ctx,
		    const QString& msg)
{
	if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
	{
		QMutexLocker locker(&s_logMutex);
		s_warnings << msg;
	}
	if (s_oldHandler)
		s_oldHandler(type, ctx, msg);
}

QString findEngine(const char* env, const QString& name)
{
	QString path = qEnvironmentVariable(env);
	if (!path.isEmpty())
		return path;
	path = QStandardPaths::findExecutable(name);
	if (path.isEmpty())
		path = QStandardPaths::findExecutable(name, { "/usr/games" });
	return path;
}

// Receives rewound() in the test's own thread.
class Receiver : public QObject
{
	Q_OBJECT
	public:
		QList<int> rewound;
		int finished = 0;
		QList<bool> sidelineChanges;
		QList<int> branchedPlies;
		QStringList notes;
	public slots:
		void onRewound(int plies) { rewound << plies; }
		void onFinished() { finished++; }
		void onSidelineChanged(bool active) { sidelineChanges << active; }
		void onBranched(int plies, const Chess::GenericMove&) { branchedPlies << plies; }
		void onNote(const QString& note) { notes << note; }
};

} // anonymous namespace

class tst_ChessGame : public QObject
{
	Q_OBJECT

	private slots:
		void initTestCase();
		void cleanupTestCase();
		void init();

		void rewindUci_data();
		void rewindUci();
		void rewindXboard();
		void rewindWhileFinishedIsIgnored();

		void branchUci_data();
		void branchUci();
		void branchStartsPaused();
		void branchXboard();
		void branchSidelineEnds();
		void branchStopDuringSideline();
		void branchAtLastPositionAndAtStart();
		void branchReturnResumesAndRetry();
		void branchStress_data();
		void branchStress();

	private:
		struct Setup
		{
			ChessGame* game;
			EngineBuilder* white;
			EngineBuilder* black;
			GameManager* manager;
			Receiver* receiver;
		};

		Setup startGame(const QString& command, const QStringList& args,
				const QString& protocol, bool ponder,
				const TimeControl& tc, int maxMoves);
		void stopGame(Setup& s);
		void runRewindCycles(Setup& s, int cycles);
		void checkConsistent(ChessGame* game, int expectedPlies);

		struct Record
		{
			QVector<PgnGame::MoveData> pgnMoves;
			QMap<int,int> scores;
			QPair<int,int> clocks;
			int plies;
		};
		Record takeRecord(ChessGame* game);
		void compareRecord(ChessGame* game, const Record& rec);
		Chess::GenericMove alternativeMove(const Record& rec, int ply);
		void pauseAndSettle(Setup& s, int minPlies);
		void branch(Setup& s, int ply, const Chess::GenericMove& m, int ms);

		bool m_lastCommentMayGrow = false;
		QString m_uci;
		QString m_xboard;
};

void tst_ChessGame::initTestCase()
{
	qRegisterMetaType<Chess::Result>("Chess::Result");
	qRegisterMetaType<Chess::GenericMove>("Chess::GenericMove");
	s_oldHandler = qInstallMessageHandler(messageHandler);
	m_uci = findEngine("CUTECHESS_TEST_UCI_ENGINE", "stockfish");
	m_xboard = findEngine("CUTECHESS_TEST_XBOARD_ENGINE", "gnuchess");
}

void tst_ChessGame::cleanupTestCase()
{
	qInstallMessageHandler(s_oldHandler);
}

void tst_ChessGame::init()
{
	QMutexLocker locker(&s_logMutex);
	s_warnings.clear();
}

tst_ChessGame::Setup tst_ChessGame::startGame(const QString& command,
					      const QStringList& args,
					      const QString& protocol,
					      bool ponder,
					      const TimeControl& tc,
					      int maxMoves)
{
	Setup s;
	EngineConfiguration config;
	config.setName("Engine");
	config.setCommand(command);
	config.setArguments(args);
	config.setProtocol(protocol);
	config.setPondering(ponder);

	s.white = new EngineBuilder(config);
	s.black = new EngineBuilder(config);
	s.manager = new GameManager(this);
	s.receiver = new Receiver;

	Chess::Board* board = Chess::BoardFactory::create("standard");
	s.game = new ChessGame(board, new PgnGame());
	s.game->setTimeControl(tc);
	GameAdjudicator adjudicator;
	adjudicator.setMaximumGameLength(maxMoves);
	s.game->setAdjudicator(adjudicator);

	connect(s.game, SIGNAL(rewound(int)), s.receiver, SLOT(onRewound(int)),
		Qt::QueuedConnection);
	connect(s.game, SIGNAL(finished(ChessGame*, Chess::Result)),
		s.receiver, SLOT(onFinished()), Qt::QueuedConnection);
	connect(s.game, SIGNAL(sidelineChanged(bool)),
		s.receiver, SLOT(onSidelineChanged(bool)), Qt::QueuedConnection);
	connect(s.game, SIGNAL(branched(int, Chess::GenericMove)),
		s.receiver, SLOT(onBranched(int, Chess::GenericMove)), Qt::QueuedConnection);
	connect(s.game, SIGNAL(sidelineNote(QString)),
		s.receiver, SLOT(onNote(QString)), Qt::QueuedConnection);

	s.manager->newGame(s.game, s.white, s.black,
			   GameManager::StartImmediately,
			   GameManager::DeletePlayers);
	return s;
}

void tst_ChessGame::stopGame(Setup& s)
{
	if (!s.game->isFinished())
	{
		QMetaObject::invokeMethod(s.game, "stop", Qt::QueuedConnection);
		QTRY_VERIFY_WITH_TIMEOUT(s.receiver->finished > 0, 20000);
	}
	// Delete the game (this also deletes the players and builders),
	// then the idle thread it ran in.
	s.game->deleteLater();
	QTest::qWait(500);
	s.manager->cleanupIdleThreads();
	QTest::qWait(200);
	delete s.manager;
	delete s.receiver;
}

// Looks at the game from the test's thread. The game thread is stopped
// for the duration (lockThread) so the data is stable.
void tst_ChessGame::checkConsistent(ChessGame* game, int expectedPlies)
{
	game->lockThread();
	const int moves = game->moves().size();
	const int pgnMoves = game->pgn()->moves().size();
	const int boardPlies = game->board()->plyCount();
	const QList<int> scoreKeys = game->scores().keys();
	game->unlockThread();

	if (expectedPlies >= 0)
	{
		QCOMPARE(moves, expectedPlies);
		QCOMPARE(pgnMoves, expectedPlies);
		QCOMPARE(boardPlies, expectedPlies);
		for (int key : scoreKeys)
			QVERIFY(key < expectedPlies);
	}
	else
	{
		QCOMPARE(pgnMoves, moves);
		QCOMPARE(boardPlies, moves);
	}
}

void tst_ChessGame::runRewindCycles(Setup& s, int cycles)
{
	for (int cycle = 0; cycle < cycles; cycle++)
	{
		// Let the game get a few moves in.
		int target = 6 + cycle * 2;
		QTRY_VERIFY_WITH_TIMEOUT(
			s.game->isFinished() || s.game->pgn()->moves().size() >= target,
			60000);
		if (s.game->isFinished())
			return;

		s.game->lockThread();
		const int played = s.game->pgn()->moves().size();
		const QVector<PgnGame::MoveData> before = s.game->pgn()->moves();
		s.game->unlockThread();

		// Rewind at a random moment, to a random earlier ply,
		// pausing first the way the slider does.
		QTest::qWait(QRandomGenerator::global()->bounded(0, 150));
		const int ply = QRandomGenerator::global()->bounded(1, played);

		s.game->lockThread();
		const QPair<int,int> times = s.game->clockTimesAt(ply);
		s.game->unlockThread();

		const int already = s.receiver->rewound.size();
		s.game->pause();
		QMetaObject::invokeMethod(s.game, "rewindTo", Qt::QueuedConnection,
					  Q_ARG(int, ply), Q_ARG(bool, true));
		if (s.game->isFinished())
			return;
		QTRY_VERIFY_WITH_TIMEOUT(s.receiver->rewound.size() > already
					 || s.game->isFinished(), 15000);
		if (s.game->isFinished())
			return;
		QCOMPARE(s.receiver->rewound.last(), ply);

		// The game was cut back, with the clocks as they were. By
		// the time we look, the game may already have moved on, so
		// only the kept moves and the consistency are checked.
		s.game->lockThread();
		QVERIFY(s.game->pgn()->moves().size() >= ply);
		for (int i = 0; i < ply; i++)
			QCOMPARE(s.game->pgn()->moves().at(i).moveString,
				 before.at(i).moveString);
		QVERIFY(s.game->moves().size() == s.game->pgn()->moves().size());
		s.game->unlockThread();
		QVERIFY(times.first > 0 || times.second > 0
			|| s.game->player(Chess::Side::White)->timeControl()->isInfinite());

		// ...and play must carry on from there.
		const int sizeNow = s.game->pgn()->moves().size();
		QTRY_VERIFY_WITH_TIMEOUT(s.game->isFinished()
			|| s.game->pgn()->moves().size() >= sizeNow + 2, 30000);
	}
}

void tst_ChessGame::rewindUci_data()
{
	QTest::addColumn<bool>("ponder");
	QTest::newRow("no ponder") << false;
	QTest::newRow("ponder") << true;
}

void tst_ChessGame::rewindUci()
{
	if (m_uci.isEmpty())
		QSKIP("No UCI engine (stockfish) found");
	QFETCH(bool, ponder);

	TimeControl tc("60+1");
	tc.setNodeLimit(20000);
	Setup s = startGame(m_uci, {}, "uci", ponder, tc, 70);

	runRewindCycles(s, 12);

	// Let the game finish by itself (maximum game length) and check
	// that the record is consistent.
	QTRY_VERIFY_WITH_TIMEOUT(s.receiver->finished > 0, 120000);
	checkConsistent(s.game, -1);
	stopGame(s);

	QMutexLocker locker(&s_logMutex);
	QVERIFY2(s_warnings.isEmpty(), qPrintable(s_warnings.join("\n")));
}

void tst_ChessGame::rewindXboard()
{
	if (m_xboard.isEmpty())
		QSKIP("No XBoard engine (gnuchess) found");

	TimeControl tc("60+1");
	tc.setPlyLimit(3);
	Setup s = startGame(m_xboard, { "--xboard" }, "xboard", false, tc, 40);

	runRewindCycles(s, 8);

	QTRY_VERIFY_WITH_TIMEOUT(s.receiver->finished > 0, 120000);
	checkConsistent(s.game, -1);
	stopGame(s);

	QMutexLocker locker(&s_logMutex);
	QVERIFY2(s_warnings.isEmpty(), qPrintable(s_warnings.join("\n")));
}

void tst_ChessGame::rewindWhileFinishedIsIgnored()
{
	if (m_uci.isEmpty())
		QSKIP("No UCI engine (stockfish) found");

	TimeControl tc("60+1");
	tc.setNodeLimit(2000);
	Setup s = startGame(m_uci, {}, "uci", false, tc, 6);
	QTRY_VERIFY_WITH_TIMEOUT(s.receiver->finished > 0, 60000);

	s.game->lockThread();
	const int plies = s.game->pgn()->moves().size();
	s.game->unlockThread();

	QMetaObject::invokeMethod(s.game, "rewindTo", Qt::QueuedConnection,
				  Q_ARG(int, 2), Q_ARG(bool, true));
	QTest::qWait(300);
	QVERIFY(s.receiver->rewound.isEmpty());
	checkConsistent(s.game, plies);
	stopGame(s);
}

tst_ChessGame::Record tst_ChessGame::takeRecord(ChessGame* game)
{
	Record rec;
	game->lockThread();
	rec.pgnMoves = game->pgn()->moves();
	rec.scores = game->scores();
	rec.plies = game->moves().size();
	rec.clocks = game->clockTimesAt(rec.plies);
	game->unlockThread();
	return rec;
}

void tst_ChessGame::compareRecord(ChessGame* game, const Record& rec)
{
	game->lockThread();
	const QVector<PgnGame::MoveData> moves = game->pgn()->moves();
	const QMap<int,int> scores = game->scores();
	const int plies = game->moves().size();
	const int boardPlies = game->board()->plyCount();
	const QPair<int,int> clocks = game->clockTimesAt(plies);
	game->unlockThread();

	QCOMPARE(plies, rec.plies);
	QCOMPARE(moves.size(), rec.pgnMoves.size());
	QCOMPARE(boardPlies, rec.plies);
	for (int i = 0; i < moves.size(); i++)
	{
		QCOMPARE(moves.at(i).moveString, rec.pgnMoves.at(i).moveString);
		if (m_lastCommentMayGrow && i == moves.size() - 1)
			QVERIFY(moves.at(i).comment.startsWith(rec.pgnMoves.at(i).comment));
		else
			QCOMPARE(moves.at(i).comment, rec.pgnMoves.at(i).comment);
		QCOMPARE(moves.at(i).key, rec.pgnMoves.at(i).key);
	}
	QCOMPARE(scores, rec.scores);
	QCOMPARE(clocks, rec.clocks);
}

Chess::GenericMove tst_ChessGame::alternativeMove(const Record& rec, int ply)
{
	Chess::Board* board = Chess::BoardFactory::create("standard");
	board->reset();
	for (int i = 0; i < ply; i++)
		board->makeMove(board->moveFromGenericMove(rec.pgnMoves.at(i).move));

	Chess::GenericMove result;
	const auto legal = board->legalMoves();
	for (const auto& m : legal)
	{
		Chess::GenericMove g = board->genericMove(m);
		if (ply < rec.pgnMoves.size() && g == rec.pgnMoves.at(ply).move)
			continue;
		result = g;
		break;
	}
	delete board;
	return result;
}

void tst_ChessGame::pauseAndSettle(Setup& s, int minPlies)
{
	QTRY_VERIFY_WITH_TIMEOUT(s.game->isFinished()
		|| s.game->pgn()->moves().size() >= minPlies, 150000);
	QVERIFY(!s.game->isFinished());
	s.game->pause();
	QTest::qWait(500);
}

void tst_ChessGame::branch(Setup& s, int ply, const Chess::GenericMove& m, int ms)
{
	// A branch now starts paused, like the Resume button in the GUI
	// leaves it to the user. Most tests want the engines to play on,
	// so this helper presses "Resume" once the branch has been set up
	// (unless the branch move already ended the game).
	const int before = s.receiver->branchedPlies.size();
	QMetaObject::invokeMethod(s.game, "branchTo", Qt::QueuedConnection,
				  Q_ARG(int, ply),
				  Q_ARG(Chess::GenericMove, m),
				  Q_ARG(int, ms));
	QTRY_VERIFY_WITH_TIMEOUT(s.receiver->branchedPlies.size() > before, 15000);
	QVERIFY(s.game->isPaused());
	QTest::qWait(100);
	if (s.game->board()->result().isNone())
		s.game->resume();
}

// A branch must not start playing by itself: it waits for Resume.
void tst_ChessGame::branchStartsPaused()
{
	if (m_uci.isEmpty())
		QSKIP("No UCI engine (stockfish) found");

	TimeControl tc("60+1");
	tc.setNodeLimit(1500);
	Setup s = startGame(m_uci, {}, "uci", false, tc, 40);
	pauseAndSettle(s, 12);
	const Record orig = takeRecord(s.game);

	const int ply = 6;
	const Chess::GenericMove alt = alternativeMove(orig, ply);
	QMetaObject::invokeMethod(s.game, "branchTo", Qt::QueuedConnection,
				  Q_ARG(int, ply),
				  Q_ARG(Chess::GenericMove, alt),
				  Q_ARG(int, 3000));
	QTRY_VERIFY_WITH_TIMEOUT(!s.receiver->branchedPlies.isEmpty(), 15000);
	QVERIFY(s.game->isSideline());
	QVERIFY(s.game->isPaused());

	// Nothing is played while paused.
	const int plies = s.game->pgn()->moves().size();
	QCOMPARE(plies, ply + 1);
	QTest::qWait(2500);
	QCOMPARE(s.game->pgn()->moves().size(), plies);
	QVERIFY(s.game->isPaused());

	// Resume lets the engines play on, at the chosen time per move.
	s.game->resume();
	QTRY_VERIFY_WITH_TIMEOUT(s.game->pgn()->moves().size() >= plies + 2, 30000);
	QVERIFY(!s.game->isPaused());
	QCOMPARE(s.game->player(Chess::Side::White)->timeControl()->timePerMove(), 3000);

	s.game->pause();
	QMetaObject::invokeMethod(s.game, "endSideline", Qt::QueuedConnection);
	QTRY_VERIFY_WITH_TIMEOUT(!s.game->isSideline(), 15000);
	QTest::qWait(300);
	compareRecord(s.game, orig);
}

// The main scenario: take a move back, play another, let the engines
// play the sideline, return, and make sure the original game is exactly
// as it was and goes on from there.
void tst_ChessGame::branchUci_data()
{
	QTest::addColumn<bool>("ponder");
	QTest::newRow("no ponder") << false;
	QTest::newRow("ponder") << true;
}

void tst_ChessGame::branchUci()
{
	if (m_uci.isEmpty())
		QSKIP("No UCI engine (stockfish) found");
	QFETCH(bool, ponder);

	TimeControl tc("60+1");
	tc.setNodeLimit(3000);
	Setup s = startGame(m_uci, {}, "uci", ponder, tc, 30);
	pauseAndSettle(s, 14);
	const Record orig = takeRecord(s.game);
	QVERIFY(orig.plies >= 14);

	const int ply = 8;
	const Chess::GenericMove alt = alternativeMove(orig, ply);
	branch(s, ply, alt, 100);

	QTRY_VERIFY_WITH_TIMEOUT(!s.receiver->branchedPlies.isEmpty(), 15000);
	QCOMPARE(s.receiver->branchedPlies.last(), ply);
	QVERIFY(s.game->isSideline());
	QVERIFY(s.receiver->sidelineChanges.contains(true));

	// The sideline: kept moves, then the user's move, then the engines.
	QTRY_VERIFY_WITH_TIMEOUT(s.game->pgn()->moves().size() >= ply + 5, 30000);
	QVERIFY(!s.game->isFinished());
	{
		const Record r = takeRecord(s.game);
		QVERIFY(r.pgnMoves.size() >= ply + 1);
		for (int i = 0; i < ply; i++)
			QCOMPARE(r.pgnMoves.at(i).moveString,
				 orig.pgnMoves.at(i).moveString);
		QVERIFY(r.pgnMoves.at(ply).move == alt);
		QCOMPARE(r.pgnMoves.at(ply).comment, QString("sideline"));
		QVERIFY(r.pgnMoves.at(ply).moveString
			!= orig.pgnMoves.at(ply).moveString);
	}

	// Return to the match.
	QMetaObject::invokeMethod(s.game, "endSideline", Qt::QueuedConnection);
	QTRY_VERIFY_WITH_TIMEOUT(!s.receiver->sidelineChanges.isEmpty()
				 && !s.receiver->sidelineChanges.last(), 15000);
	QVERIFY(!s.game->isSideline());
	QVERIFY(!s.game->isFinished());
	compareRecord(s.game, orig);
	QTRY_VERIFY_WITH_TIMEOUT(!s.receiver->rewound.isEmpty(), 5000);
	QCOMPARE(s.receiver->rewound.last(), ply);

	// Nothing may move while the game is back and paused.
	QVERIFY(s.game->isPaused());
	QTest::qWait(800);
	compareRecord(s.game, orig);

	// Resume: the original game goes on from where it was.
	s.game->resume();
	QTRY_VERIFY_WITH_TIMEOUT(s.game->isFinished()
		|| s.game->pgn()->moves().size() >= orig.plies + 2, 30000);
	QTRY_VERIFY_WITH_TIMEOUT(s.receiver->finished > 0, 120000);
	QCOMPARE(s.receiver->finished, 1);
	{
		const Record r = takeRecord(s.game);
		for (int i = 0; i < orig.plies; i++)
			QCOMPARE(r.pgnMoves.at(i).moveString,
				 orig.pgnMoves.at(i).moveString);
		for (int i = 0; i < r.pgnMoves.size(); i++)
			QVERIFY(r.pgnMoves.at(i).comment != "sideline");
	}
	QVERIFY(!s.game->result().isNone());
	stopGame(s);

	QMutexLocker locker(&s_logMutex);
	QVERIFY2(s_warnings.isEmpty(), qPrintable(s_warnings.join("\n")));
}

void tst_ChessGame::branchXboard()
{
	if (m_xboard.isEmpty())
		QSKIP("No XBoard engine (gnuchess) found");
	// gnuchess is very slow / unreliable in some environments (the
	// rewindXboard test has the same problem there), so this one only
	// runs when asked for.
	if (qEnvironmentVariableIsEmpty("CUTECHESS_TEST_XBOARD_BRANCH"))
		QSKIP("Set CUTECHESS_TEST_XBOARD_BRANCH=1 to run this test");

	TimeControl tc("60+1");
	tc.setPlyLimit(2);
	Setup s = startGame(m_xboard, { "--xboard" }, "xboard", false, tc, 30);
	pauseAndSettle(s, 6);
	const Record orig = takeRecord(s.game);

	const int ply = 4;
	const Chess::GenericMove alt = alternativeMove(orig, ply);
	branch(s, ply, alt, 1000);
	QTRY_VERIFY_WITH_TIMEOUT(!s.receiver->branchedPlies.isEmpty(), 20000);
	QTRY_VERIFY_WITH_TIMEOUT(s.game->pgn()->moves().size() >= ply + 3, 90000);
	QVERIFY(!s.game->isFinished());

	QMetaObject::invokeMethod(s.game, "endSideline", Qt::QueuedConnection);
	QTRY_VERIFY_WITH_TIMEOUT(!s.receiver->sidelineChanges.isEmpty()
				 && !s.receiver->sidelineChanges.last(), 20000);
	compareRecord(s.game, orig);
	QVERIFY(s.game->isPaused());

	s.game->resume();
	QTRY_VERIFY_WITH_TIMEOUT(s.game->pgn()->moves().size() >= orig.plies + 2
				 || s.game->isFinished(), 60000);
	stopGame(s);

	QMutexLocker locker(&s_logMutex);
	QVERIFY2(s_warnings.isEmpty(), qPrintable(s_warnings.join("\n")));
}

// A sideline that reaches the end of a game must not finish the game;
// the user can branch again from inside it and still return to the
// original afterwards.
void tst_ChessGame::branchSidelineEnds()
{
	if (m_uci.isEmpty())
		QSKIP("No UCI engine (stockfish) found");

	TimeControl tc("60+1");
	tc.setNodeLimit(4000);
	Setup s = startGame(m_uci, {}, "uci", false, tc, 20);	// 40 plies
	pauseAndSettle(s, 16);
	const Record orig = takeRecord(s.game);

	branch(s, 6, alternativeMove(orig, 6), 100);
	// The sideline is adjudicated at the maximum game length.
	QTRY_VERIFY_WITH_TIMEOUT(!s.receiver->notes.isEmpty(), 60000);
	QVERIFY(!s.game->isFinished());
	QCOMPARE(s.receiver->finished, 0);
	QVERIFY(s.game->isSideline());
	QVERIFY(s.game->isPaused());
	QVERIFY(s.game->result().isNone());

	// Branch again, from inside the sideline.
	const Record side = takeRecord(s.game);
	QVERIFY(side.plies >= 20);
	const int again = 12;
	const Chess::GenericMove alt2 = alternativeMove(side, again);
	branch(s, again, alt2, 100);
	QTRY_VERIFY_WITH_TIMEOUT(s.receiver->branchedPlies.size() >= 2, 15000);
	QCOMPARE(s.receiver->branchedPlies.last(), again);
	QTRY_VERIFY_WITH_TIMEOUT(s.game->pgn()->moves().size() >= again + 4
				 || s.receiver->notes.size() >= 2, 30000);
	QVERIFY(s.game->isSideline());
	{
		const Record r = takeRecord(s.game);
		for (int i = 0; i < again; i++)
			QCOMPARE(r.pgnMoves.at(i).moveString,
				 side.pgnMoves.at(i).moveString);
		QVERIFY(r.pgnMoves.at(again).move == alt2);
	}

	// Return: the game as it was when the FIRST branch was made.
	QMetaObject::invokeMethod(s.game, "endSideline", Qt::QueuedConnection);
	QTRY_VERIFY_WITH_TIMEOUT(!s.receiver->sidelineChanges.isEmpty()
				 && !s.receiver->sidelineChanges.last(), 15000);
	compareRecord(s.game, orig);
	QVERIFY(!s.game->isSideline());
	QVERIFY(s.game->isPaused());
	QCOMPARE(s.receiver->rewound.last(), 6);

	// Branch from the restored match once more, then return again.
	branch(s, 10, alternativeMove(orig, 10), 100);
	QTRY_VERIFY_WITH_TIMEOUT(s.game->isSideline()
		&& s.game->pgn()->moves().size() >= 13, 30000);
	QMetaObject::invokeMethod(s.game, "endSideline", Qt::QueuedConnection);
	QTRY_VERIFY_WITH_TIMEOUT(!s.game->isSideline(), 15000);
	QTest::qWait(300);
	compareRecord(s.game, orig);

	stopGame(s);
	QMutexLocker locker(&s_logMutex);
	QVERIFY2(s_warnings.isEmpty(), qPrintable(s_warnings.join("\n")));
}

// Stopping the game (tournament stopped, window closed) during a
// sideline records the original game, not the sideline.
void tst_ChessGame::branchStopDuringSideline()
{
	if (m_uci.isEmpty())
		QSKIP("No UCI engine (stockfish) found");

	TimeControl tc("60+1");
	tc.setNodeLimit(1500);
	Setup s = startGame(m_uci, {}, "uci", false, tc, 40);
	pauseAndSettle(s, 12);
	const Record orig = takeRecord(s.game);

	branch(s, 6, alternativeMove(orig, 6), 100);
	QTRY_VERIFY_WITH_TIMEOUT(s.game->isSideline()
		&& s.game->pgn()->moves().size() >= 10, 30000);

	QMetaObject::invokeMethod(s.game, "stop", Qt::QueuedConnection);
	QTRY_VERIFY_WITH_TIMEOUT(s.receiver->finished > 0, 20000);
	QCOMPARE(s.receiver->finished, 1);
	QVERIFY(!s.game->isSideline());
	m_lastCommentMayGrow = true;	// stop() appends the result text
	compareRecord(s.game, orig);
	QVERIFY(!s.receiver->sidelineChanges.last());
	m_lastCommentMayGrow = false;
	stopGame(s);

	QMutexLocker locker(&s_logMutex);
	QVERIFY2(s_warnings.isEmpty(), qPrintable(s_warnings.join("\n")));
}

// Branching from the very last position (replace the last move
// played) and from the very start.
void tst_ChessGame::branchAtLastPositionAndAtStart()
{
	if (m_uci.isEmpty())
		QSKIP("No UCI engine (stockfish) found");

	TimeControl tc("60+1");
	tc.setNodeLimit(1500);
	Setup s = startGame(m_uci, {}, "uci", false, tc, 40);
	pauseAndSettle(s, 10);
	const Record orig = takeRecord(s.game);

	// Take back the last move and substitute another.
	const int last = orig.plies - 1;
	branch(s, last, alternativeMove(orig, last), 100);
	QTRY_VERIFY_WITH_TIMEOUT(!s.receiver->branchedPlies.isEmpty(), 15000);
	QTRY_VERIFY_WITH_TIMEOUT(s.game->pgn()->moves().size() >= last + 4, 30000);
	QMetaObject::invokeMethod(s.game, "endSideline", Qt::QueuedConnection);
	QTRY_VERIFY_WITH_TIMEOUT(!s.game->isSideline(), 15000);
	QTest::qWait(300);
	compareRecord(s.game, orig);

	// A move at the position after all the moves played so far.
	branch(s, orig.plies, alternativeMove(orig, orig.plies), 100);
	QTRY_VERIFY_WITH_TIMEOUT(s.game->isSideline()
		&& s.game->pgn()->moves().size() >= orig.plies + 3, 30000);
	QMetaObject::invokeMethod(s.game, "endSideline", Qt::QueuedConnection);
	QTRY_VERIFY_WITH_TIMEOUT(!s.game->isSideline(), 15000);
	QTest::qWait(300);
	compareRecord(s.game, orig);

	// From the starting position.
	branch(s, 0, alternativeMove(orig, 0), 100);
	QTRY_VERIFY_WITH_TIMEOUT(s.game->isSideline()
		&& s.game->pgn()->moves().size() >= 4, 30000);
	QMetaObject::invokeMethod(s.game, "endSideline", Qt::QueuedConnection);
	QTRY_VERIFY_WITH_TIMEOUT(!s.game->isSideline(), 15000);
	QTest::qWait(300);
	compareRecord(s.game, orig);

	s.game->resume();
	QTRY_VERIFY_WITH_TIMEOUT(s.game->pgn()->moves().size() >= orig.plies + 2, 30000);
	stopGame(s);

	QMutexLocker locker(&s_logMutex);
	QVERIFY2(s_warnings.isEmpty(), qPrintable(s_warnings.join("\n")));
}


// endSideline(true) carries on with the match; a branch request that
// arrives while a rewind is still under way is not lost.
void tst_ChessGame::branchReturnResumesAndRetry()
{
	if (m_uci.isEmpty())
		QSKIP("No UCI engine (stockfish) found");

	TimeControl tc("60+1");
	tc.setNodeLimit(1500);
	Setup s = startGame(m_uci, {}, "uci", false, tc, 60);
	pauseAndSettle(s, 12);
	const Record orig = takeRecord(s.game);

	// Slider rewind and, straight behind it, a branch request.
	const Chess::GenericMove alt = alternativeMove(orig, 6);
	QMetaObject::invokeMethod(s.game, "rewindTo", Qt::QueuedConnection,
				  Q_ARG(int, 8), Q_ARG(bool, false));
	branch(s, 6, alt, 100);
	QTRY_VERIFY_WITH_TIMEOUT(!s.receiver->branchedPlies.isEmpty(), 15000);
	QCOMPARE(s.receiver->branchedPlies.last(), 6);
	QVERIFY(s.game->isSideline());
	{
		const Record r = takeRecord(s.game);
		QVERIFY(r.pgnMoves.at(6).move == alt);
	}

	QMetaObject::invokeMethod(s.game, "endSideline", Qt::QueuedConnection,
				  Q_ARG(bool, true));
	QTRY_VERIFY_WITH_TIMEOUT(!s.game->isSideline(), 15000);
	// The match is back and running again, with no click on Resume.
	QTRY_VERIFY_WITH_TIMEOUT(!s.game->isPaused(), 5000);
	// (the slider had cut the match back to 8 plies before the branch)
	QTRY_VERIFY_WITH_TIMEOUT(s.game->isFinished()
		|| s.game->pgn()->moves().size() >= 10, 30000);
	{
		const Record r = takeRecord(s.game);
		for (int i = 0; i < 8; i++)
			QCOMPARE(r.pgnMoves.at(i).moveString,
				 orig.pgnMoves.at(i).moveString);
	}
	stopGame(s);

	QMutexLocker locker(&s_logMutex);
	QVERIFY2(s_warnings.isEmpty(), qPrintable(s_warnings.join("\n")));
}

// Random sequences of branch / branch again / return / slider rewind /
// pause / resume at random moments. Afterwards the record must be
// consistent and returning must restore the original game.
void tst_ChessGame::branchStress_data()
{
	QTest::addColumn<bool>("ponder");
	QTest::addColumn<int>("seed");
	for (int seed = 1; seed <= 4; seed++)
	{
		QTest::newRow(qPrintable(QString("no ponder %1").arg(seed))) << false << seed;
		QTest::newRow(qPrintable(QString("ponder %1").arg(seed))) << true << seed;
	}
}

void tst_ChessGame::branchStress()
{
	if (m_uci.isEmpty())
		QSKIP("No UCI engine (stockfish) found");
	QFETCH(bool, ponder);
	QFETCH(int, seed);

	QRandomGenerator rng(seed * 7919 + (ponder ? 1 : 0));
	TimeControl tc("60+1");
	tc.setNodeLimit(1500);
	Setup s = startGame(m_uci, {}, "uci", ponder, tc, 60);
	pauseAndSettle(s, 12);
	s.game->resume();

	Record orig;
	bool haveOrig = false;
	int ops = 0;
	for (int i = 0; i < 40 && !s.game->isFinished(); i++)
	{
		const int op = rng.bounded(0, 10);
		if (op <= 3)
		{
			// Branch (from the match or from inside a sideline).
			s.game->pause();
			QTest::qWait(350);
			if (s.game->isFinished())
				break;
			const Record rec = takeRecord(s.game);
			if (rec.plies < 2)
				continue;
			const int ply = rng.bounded(0, rec.plies + 1);
			const Chess::GenericMove alt = alternativeMove(rec, ply);
			if (alt.isNull() && ply < rec.plies)
				continue;
			if (!s.game->isSideline())
			{
				orig = rec;
				haveOrig = true;
			}
			branch(s, ply, alt, 100);
			ops++;
		}
		else if (op == 4 || op == 5)
		{
			QMetaObject::invokeMethod(s.game, "endSideline", Qt::QueuedConnection);
		}
		else if (op == 6 && s.game->isSideline())
		{
			// The slider, inside a sideline.
			const Record rec = takeRecord(s.game);
			if (rec.plies > 1)
			{
				s.game->pause();
				QMetaObject::invokeMethod(s.game, "rewindTo", Qt::QueuedConnection,
					Q_ARG(int, int(rng.bounded(1, rec.plies))),
					Q_ARG(bool, rng.bounded(0, 2) == 0));
			}
		}
		else if (op == 7)
			s.game->pause();
		else if (op == 8 || op == 9)
			s.game->resume();
		QTest::qWait(rng.bounded(0, 400));
	}
	if (s.game->isFinished())
	{
		stopGame(s);
		return;
	}

	// Settle, then come back to the match.
	QTest::qWait(600);
	QMetaObject::invokeMethod(s.game, "endSideline", Qt::QueuedConnection);
	QTRY_VERIFY_WITH_TIMEOUT(!s.game->isSideline(), 15000);
	QTest::qWait(600);
	QVERIFY(!s.game->isFinished());

	// Consistent record whatever happened.
	{
		const Record r = takeRecord(s.game);
		Chess::Board* board = Chess::BoardFactory::create("standard");
		board->reset();
		for (const auto& md : r.pgnMoves)
		{
			Chess::Move m = board->moveFromGenericMove(md.move);
			QVERIFY(!m.isNull() && board->isLegalMove(m));
			board->makeMove(m);
		}
		delete board;
		s.game->lockThread();
		QCOMPARE(s.game->moves().size(), r.pgnMoves.size());
		QCOMPARE(s.game->board()->plyCount(), r.pgnMoves.size());
		s.game->unlockThread();
		for (const auto& md : r.pgnMoves)
			QVERIFY(md.comment != "sideline");
	}
	if (haveOrig && ops > 0)
	{
		// The slider may have cut the match itself back when the game
		// was not in a sideline, so only check what must hold: the
		// moves kept are a prefix of the original.
		const Record r = takeRecord(s.game);
		const int n = qMin(r.pgnMoves.size(), orig.pgnMoves.size());
		QVERIFY(n > 0);
		for (int i = 0; i < n; i++)
			QCOMPARE(r.pgnMoves.at(i).moveString,
				 orig.pgnMoves.at(i).moveString);
	}

	// And the game still plays on correctly.
	s.game->resume();
	const int sizeNow = s.game->pgn()->moves().size();
	QTRY_VERIFY_WITH_TIMEOUT(s.game->isFinished()
		|| s.game->pgn()->moves().size() >= sizeNow + 3, 30000);
	stopGame(s);

	QMutexLocker locker(&s_logMutex);
	QStringList warnings;
	for (const QString& w : std::as_const(s_warnings))
		if (!w.contains("Illegal move offered"))
			warnings << w;
	QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join("\n")));
}

QTEST_MAIN(tst_ChessGame)
#include "tst_chessgame.moc"
