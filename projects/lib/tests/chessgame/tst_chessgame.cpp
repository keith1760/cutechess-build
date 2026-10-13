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
	public slots:
		void onRewound(int plies) { rewound << plies; }
		void onFinished() { finished++; }
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

		QString m_uci;
		QString m_xboard;
};

void tst_ChessGame::initTestCase()
{
	qRegisterMetaType<Chess::Result>("Chess::Result");
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

QTEST_MAIN(tst_ChessGame)
#include "tst_chessgame.moc"
