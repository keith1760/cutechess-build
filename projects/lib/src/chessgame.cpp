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

#include "chessgame.h"
#include <QThread>
#include <QTimer>
#include "board/board.h"
#include "chessplayer.h"
#include "chessengine.h"
#include "openingbook.h"
#include "timecontrol.h"

namespace {

QString evalString(const MoveEvaluation& eval)
{
	if (eval.isBookEval())
		return "book";
	if (eval.isEmpty())
		return QString();

	QString str = eval.scoreText();
	if (eval.depth() > 0)
		str += "/" + QString::number(eval.depth()) + " ";

	int t = eval.time();
	if (t == 0)
		return str + "0s";

	int precision = 0;
	if (t < 100)
		precision = 3;
	else if (t < 1000)
		precision = 2;
	else if (t < 10000)
		precision = 1;
	str += QString::number(double(t / 1000.0), 'f', precision) + 's';

	return str;
}

} // anonymous namespace

ChessGame::ChessGame(Chess::Board* board, PgnGame* pgn, QObject* parent)
	: QObject(parent),
	  m_board(board),
	  m_startDelay(0),
	  m_bookMoveDelay(0),
	  m_useBookSeed(false),
	  m_bookSeed(0),
	  m_openingIndex(-1),
	  m_finished(false),
	  m_gameInProgress(false),
	  m_paused(false),
	  m_moveHeld(false),
	  m_restartPending(false),
	  m_heldMoveSender(nullptr),
	  m_pgnInitialized(false),
	  m_bookOwnership(false),
	  m_boardShouldBeFlipped(false),
	  m_pgn(pgn)
{
	Q_ASSERT(pgn != nullptr);

	for (int i = 0; i < 2; i++)
	{
		m_player[i] = nullptr;
		m_book[i] = nullptr;
		m_bookDepth[i] = 0;
	}
}

ChessGame::~ChessGame()
{
	delete m_board;
	if (m_bookOwnership)
	{
		bool same = (m_book[0] == m_book[1]);
		delete m_book[0];
		if (!same)
			delete m_book[1];
	}
}

QString ChessGame::errorString() const
{
	return m_error;
}

ChessPlayer* ChessGame::player(Chess::Side side) const
{
	Q_ASSERT(!side.isNull());
	return m_player[side];
}

bool ChessGame::isFinished() const
{
	return m_finished;
}

bool ChessGame::isPaused() const
{
	return m_paused;
}

bool ChessGame::boardShouldBeFlipped() const
{
	return m_boardShouldBeFlipped;
}

void ChessGame::setBoardShouldBeFlipped(bool flip)
{
	m_boardShouldBeFlipped = flip;
}

PgnGame* ChessGame::pgn() const
{
	return m_pgn;
}

Chess::Board* ChessGame::board() const
{
	return m_board;
}

QString ChessGame::startingFen() const
{
	return m_startingFen;
}

const QVector<Chess::Move>& ChessGame::moves() const
{
	return m_moves;
}

const QMap<int,int>& ChessGame::scores() const
{
	return m_scores;
}

Chess::Result ChessGame::result() const
{
	return m_result;
}

ChessPlayer* ChessGame::playerToMove() const
{
	if (m_board->sideToMove().isNull())
		return nullptr;
	return m_player[m_board->sideToMove()];
}

ChessPlayer* ChessGame::playerToWait() const
{
	if (m_board->sideToMove().isNull())
		return nullptr;
	return m_player[m_board->sideToMove().opposite()];
}

void ChessGame::stop(bool emitMoveChanged)
{
	if (m_finished)
		return;

	m_finished = true;
	m_moveHeld = false;
	m_restartPending = false;
	m_heldMoveSender = nullptr;
	emit humanEnabled(false);
	if (!m_gameInProgress)
	{
		m_result = Chess::Result();
		finish();
		return;
	}
	
	QDateTime gameEndTime = QDateTime::currentDateTime();

	initializePgn();
	m_gameInProgress = false;
	const QVector<PgnGame::MoveData>& moves(m_pgn->moves());
	int plies = moves.size();

	m_pgn->setTag("PlyCount", QString::number(plies));

	m_pgn->setGameEndTime(gameEndTime);

	m_pgn->setResult(m_result);
	m_pgn->setResultDescription(m_result.description());

	if (emitMoveChanged && plies > 1)
	{
		const PgnGame::MoveData& md(moves.at(plies - 1));
		emit moveChanged(plies - 1, md.move, md.moveString, md.comment);
	}

	m_player[Chess::Side::White]->endGame(m_result);
	m_player[Chess::Side::Black]->endGame(m_result);

	connect(this, SIGNAL(playersReady()), this, SLOT(finish()), Qt::QueuedConnection);
	syncPlayers();
}

void ChessGame::finish()
{
	disconnect(this, SIGNAL(playersReady()), this, SLOT(finish()));
	for (int i = 0; i < 2; i++)
	{
		if (m_player[i] != nullptr)
			m_player[i]->disconnect(this);
	}

	emit finished(this, m_result);
}

void ChessGame::kill()
{
	for (int i = 0; i < 2; i++)
	{
		if (m_player[i] != nullptr)
			m_player[i]->kill();
	}

	stop();
}

void ChessGame::addPgnMove(const Chess::Move& move, const QString& comment)
{
	PgnGame::MoveData md;
	md.key = m_board->key();
	md.move = m_board->genericMove(move);
	md.moveString = m_board->moveString(move, Chess::Board::StandardAlgebraic);
	md.comment = comment;

	m_pgn->addMove(md);
}

void ChessGame::emitLastMove()
{
	int ply = m_moves.size() - 1;
	if (m_scores.contains(ply))
	{
		int score = m_scores[ply];
		if (score != MoveEvaluation::NULL_SCORE)
			emit scoreChanged(ply, score);
	}

	const auto& md = m_pgn->moves().last();
	emit moveMade(md.move, md.moveString, md.comment);
}

void ChessGame::onMoveMade(const Chess::Move& move)
{
	ChessPlayer* sender = qobject_cast<ChessPlayer*>(QObject::sender());
	Q_ASSERT(sender != nullptr);

	Q_ASSERT(m_gameInProgress);

	// A move may arrive while the game is paused (the engine was
	// already searching when Pause was pressed). Do not apply it, and
	// do not throw it away either: the engine already regards it as
	// played. Hold it, and resume() applies it exactly as if it had
	// just arrived. The players' clocks were already stopped when the
	// move arrived, so paused time is not charged to anyone.
	if (m_paused)
	{
		if (sender == playerToMove() && !m_moveHeld)
		{
			m_moveHeld = true;
			m_heldMoveSender = sender;
			m_heldMove = move;
		}
		return;
	}

	Q_ASSERT(m_board->isLegalMove(move));
	if (sender != playerToMove())
	{
		qWarning("%s tried to make a move on the opponent's turn",
			 qUtf8Printable(sender->name()));
		return;
	}

	applyMove(sender, move);
}

void ChessGame::onSearchInterrupted()
{
	// An engine's search was cut short by Pause and its reply was
	// dropped (see UciEngine::parseLine()), so no move is coming. Start
	// the same search again, from the same position, as soon as the game
	// is running -- immediately if Resume was already pressed, otherwise
	// from resume().
	ChessPlayer* sender = qobject_cast<ChessPlayer*>(QObject::sender());
	if (m_finished || !m_gameInProgress || sender == nullptr
	||  sender != playerToMove())
		return;

	if (m_paused)
	{
		m_restartPending = true;
		return;
	}

	sender->go();
}

void ChessGame::applyMove(ChessPlayer* sender, const Chess::Move& move)
{
	m_scores[m_moves.size()] = sender->evaluation().score();
	m_moves.append(move);
	addPgnMove(move, evalString(sender->evaluation()));

	// Get the result before sending the move to the opponent
	m_board->makeMove(move);
	m_result = m_board->result();
	if (m_result.isNone())
	{
		if (m_board->reversibleMoveCount() == 0)
			m_adjudicator.resetDrawMoveCount();

		m_adjudicator.addEval(m_board, sender->evaluation());
		m_result = m_adjudicator.result();
	}
	m_board->undoMove();

	ChessPlayer* player = playerToWait();
	if (player->timeControl()->isHourglass()
	&&  sender->timeControl()->isHourglass())
		player->addTime(sender->timeControl()->lastMoveTime());

	player->makeMove(move);
	m_board->makeMove(move);

	if (m_result.isNone())
	{
		emitLastMove();
		startTurn();
	}
	else
	{
		stop(false);
		emitLastMove();
	}
}

void ChessGame::startTurn()
{
	if (m_paused)
		return;

	Chess::Side side(m_board->sideToMove());
	Q_ASSERT(!side.isNull());

	// Guards against asking the same player to move twice in a row
	// with no move actually applied in between. That can't happen from
	// the normal move->applyMove()->startTurn() chain (each call there
	// corresponds to a newly applied move), but resume() restarts play
	// with a queued call of its own (see resume()), and a stale queued
	// restart landing after the game already resumed some other way
	// (e.g. a human's wokeUp()) would otherwise call go() a second time
	// on a player that's already thinking.
	if (m_player[side]->state() == ChessPlayer::Thinking)
		return;

	emit humanEnabled(m_player[side]->isHuman());

	Chess::Move move(bookMove(side));
	if (move.isNull())
	{
		// Pause may be requested by the GUI between the initial
		// m_paused check above and this point.
		if (m_paused)
			return;

		m_player[side]->go();
		m_player[side.opposite()]->startPondering();
	}
	else
	{
		m_player[side.opposite()]->clearPonderState();

		// In engine-engine games, book moves are played back-to-back
		// without ever waiting for a "thinking" reply, so a whole
		// opening line can fly past on screen in a single event-loop
		// tick. If a display delay is set, pace it out one move at a
		// time instead of playing it out immediately.
		if (m_bookMoveDelay > 0
		&&  !m_player[side]->isHuman()
		&&  !m_player[side.opposite()]->isHuman())
		{
			QTimer::singleShot(m_bookMoveDelay, this, [this, side, move]()
			{
				if (m_finished || m_paused)
					return;
				if (m_board->sideToMove() != side)
					return;

				m_player[side]->makeBookMove(move);
			});
		}
		else
		{
			// A pause request can arrive after bookMove() but before the
			// book move is actually sent to the player.
			if (m_paused)
				return;
			m_player[side]->makeBookMove(move);
		}
	}
}

void ChessGame::onAdjudication(const Chess::Result& result)
{
	if (m_finished || result.type() != Chess::Result::Adjudication)
		return;

	m_result = result;

	stop();
}

void ChessGame::onResignation(const Chess::Result& result)
{
	if (m_finished || result.type() != Chess::Result::Resignation)
		return;

	m_result = result;

	stop();
}

void ChessGame::onResultClaim(const Chess::Result& result)
{
	if (m_finished)
		return;

	ChessPlayer* sender = qobject_cast<ChessPlayer*>(QObject::sender());
	Q_ASSERT(sender != nullptr);

	if (result.type() == Chess::Result::Disconnection)
	{
		// The engine may not be properly started so we have to
		// figure out the player's side this way
		Chess::Side side(Chess::Side::White);
		if (m_player[side] != sender)
			side = Chess::Side::Black;
		m_result = Chess::Result(result.type(), side.opposite());
	}
	else if (!m_gameInProgress && result.winner().isNull())
	{
		qWarning("Unexpected result claim from %s: %s",
			 qUtf8Printable(sender->name()),
			 qUtf8Printable(result.toVerboseString()));
	}
	else if (sender->areClaimsValidated() && result.loser() != sender->side())
	{
		qWarning("%s forfeits by invalid result claim: %s",
			 qUtf8Printable(sender->name()),
			 qUtf8Printable(result.toVerboseString()));
		m_result = Chess::Result(Chess::Result::Adjudication,
					 sender->side().opposite(),
					 "Invalid result claim");
	}
	else
		m_result = result;

	stop();
}

Chess::Move ChessGame::bookMove(Chess::Side side)
{
	Q_ASSERT(!side.isNull());

	if (m_book[side] == nullptr
	||  m_moves.size() >= m_bookDepth[side] * 2)
		return Chess::Move();

	Chess::GenericMove bookMove = m_useBookSeed
		? m_book[side]->move(m_board->key(), bookDraw())
		: m_book[side]->move(m_board->key());
	Chess::Move move = m_board->moveFromGenericMove(bookMove);
	if (move.isNull())
		return Chess::Move();

	if (!m_board->isLegalMove(move))
	{
		qWarning("Illegal opening book move for %s: %s",
			 qUtf8Printable(side.toString()),
			 qUtf8Printable(m_board->moveString(move, Chess::Board::LongAlgebraic)));
		return Chess::Move();
	}

	if (m_board->isRepetition(move))
		return Chess::Move();

	return move;
}

void ChessGame::setError(const QString& message)
{
	m_error = message;
}

void ChessGame::setPlayer(Chess::Side side, ChessPlayer* player)
{
	Q_ASSERT(!side.isNull());
	Q_ASSERT(player != nullptr);
	m_player[side] = player;
}

void ChessGame::setStartingFen(const QString& fen)
{
	Q_ASSERT(!m_gameInProgress);
	m_startingFen = fen;
}

void ChessGame::setTimeControl(const TimeControl& timeControl, Chess::Side side)
{
	if (side != Chess::Side::White)
		m_timeControl[Chess::Side::Black] = timeControl;
	if (side != Chess::Side::Black)
		m_timeControl[Chess::Side::White] = timeControl;
}

void ChessGame::setMoves(const QVector<Chess::Move>& moves)
{
	Q_ASSERT(!m_gameInProgress);
	m_scores.clear();
	m_moves = moves;
}

bool ChessGame::setMoves(const PgnGame& pgn)
{
	setStartingFen(pgn.startingFenString());
	if (!resetBoard())
		return false;
	m_scores.clear();
	m_moves.clear();

	for (const PgnGame::MoveData& md : pgn.moves())
	{
		Chess::Move move(m_board->moveFromGenericMove(md.move));
		if (!m_board->isLegalMove(move))
			return false;

		m_board->makeMove(move);
		if (!m_board->result().isNone())
			return true;

		m_moves.append(move);
	}

	return true;
}

void ChessGame::setOpeningBook(const OpeningBook* book,
			       Chess::Side side,
			       int depth)
{
	Q_ASSERT(!m_gameInProgress);

	if (side.isNull())
	{
		setOpeningBook(book, Chess::Side::White, depth);
		setOpeningBook(book, Chess::Side::Black, depth);
	}
	else
	{
		m_book[side] = book;
		m_bookDepth[side] = depth;
	}
}

void ChessGame::setAdjudicator(const GameAdjudicator& adjudicator)
{
	m_adjudicator = adjudicator;
}

void ChessGame::setBookSeed(quint32 seed)
{
	m_useBookSeed = true;
	m_bookSeed = seed;
}

quint32 ChessGame::bookDraw() const
{
	// splitmix64 of (seed, number of moves played so far)
	quint64 z = (quint64(m_bookSeed) << 32) + quint64(m_moves.size()) * 0x9E3779B97F4A7C15ULL
		  + 0x9E3779B97F4A7C15ULL;
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
	z ^= z >> 31;
	return quint32(z >> 32);
}

void ChessGame::generateOpening()
{
	if (m_book[Chess::Side::White] == nullptr || m_book[Chess::Side::Black] == nullptr)
		return;
	if (!resetBoard())
		return;

	// First play moves that are already in the opening
	for (const Chess::Move& move : std::as_const(m_moves))
	{
		Q_ASSERT(m_board->isLegalMove(move));

		m_board->makeMove(move);
		if (!m_board->result().isNone())
			return;
	}

	// Then play the opening book moves
	for (;;)
	{
		Chess::Move move = bookMove(m_board->sideToMove());
		if (move.isNull())
			break;

		m_board->makeMove(move);
		if (!m_board->result().isNone())
			break;

		m_moves.append(move);
	}
}

void ChessGame::emitStartFailed()
{
	emit startFailed(this);
}

void ChessGame::setStartDelay(int time)
{
	Q_ASSERT(time >= 0);
	m_startDelay = time;
}

void ChessGame::setBookOwnership(bool enabled)
{
	m_bookOwnership = enabled;
}

void ChessGame::setBookMoveDelay(int time)
{
	Q_ASSERT(time >= 0);
	m_bookMoveDelay = time;
}

void ChessGame::pauseThread()
{
	m_pauseSem.release();
	m_resumeSem.acquire();
}

void ChessGame::lockThread()
{
	if (QThread::currentThread() == thread())
		return;

	QMetaObject::invokeMethod(this, "pauseThread", Qt::QueuedConnection);
	m_pauseSem.acquire();
}

void ChessGame::unlockThread()
{
	if (QThread::currentThread() == thread())
		return;

	m_resumeSem.release();
}

bool ChessGame::resetBoard()
{
	QString fen(m_startingFen);
	if (fen.isEmpty())
	{
		fen = m_board->defaultFenString();
		if (m_board->isRandomVariant())
			m_startingFen = fen;
	}

	if (!m_board->setFenString(fen))
	{
		qWarning("Invalid FEN string: %s", qUtf8Printable(fen));
		m_board->reset();
		if (m_board->isRandomVariant())
			m_startingFen = m_board->fenString();
		else
			m_startingFen.clear();
		return false;
	}
	else if (!m_startingFen.isEmpty())
		m_startingFen = m_board->fenString();

	return true;
}

void ChessGame::onPlayerReady()
{
	ChessPlayer* sender = qobject_cast<ChessPlayer*>(QObject::sender());
	Q_ASSERT(sender != nullptr);

	disconnect(sender, SIGNAL(ready()),
		   this, SLOT(onPlayerReady()));
	disconnect(sender, SIGNAL(disconnected()),
		   this, SLOT(onPlayerReady()));

	for (int i = 0; i < 2; i++)
	{
		if (!m_player[i]->isReady()
		&&  m_player[i]->state() != ChessPlayer::Disconnected)
			return;
	}

	emit playersReady();
}

void ChessGame::syncPlayers()
{
	bool ready = true;

	for (int i = 0; i < 2; i++)
	{
		ChessPlayer* player = m_player[i];
		Q_ASSERT(player != nullptr);

		if (!player->isReady()
		&&  player->state() != ChessPlayer::Disconnected)
		{
			ready = false;
			connect(player, SIGNAL(ready()),
				this, SLOT(onPlayerReady()));
			connect(player, SIGNAL(disconnected()),
				this, SLOT(onPlayerReady()));
		}
	}
	if (ready)
		emit playersReady();
}

void ChessGame::start()
{
	if (m_startDelay > 0)
	{
		QTimer::singleShot(m_startDelay, this, SLOT(start()));
		m_startDelay = 0;
		return;
	}

	for (int i = 0; i < 2; i++)
	{
		connect(m_player[i], SIGNAL(resultClaim(Chess::Result)),
			this, SLOT(onResultClaim(Chess::Result)));
	}

	// Start the game in the correct thread
	connect(this, SIGNAL(playersReady()), this, SLOT(startGame()));
	QMetaObject::invokeMethod(this, "syncPlayers", Qt::QueuedConnection);


	m_result = Chess::Result();
	emit humanEnabled(false);
	resetBoard();
	initializePgn();
	emit initialized(this);
	emit fenChanged(m_board->startingFenString());
}

void ChessGame::pause()
{
	// pause() is thread-safe: it sets the atomic flag and queues a
	// request on the game's own thread. The engine that is currently
	// searching is asked to hand in its move at once (so its clock stops
	// immediately), but its reply is NEVER discarded: a stopped engine
	// has already booked its bestmove into its own move list, so
	// ignoring the reply would desynchronise it from the game.
	// onMoveMade() holds any move that arrives while paused and
	// resume() applies it. An engine that is only pondering is left
	// alone, and startTurn() refuses to start a new turn.
	if (m_paused.exchange(true))
		return;
	emit pausedChanged(true);

	// Ask the engine that is currently searching to hand in its move now,
	// so its clock stops at once instead of running until its search ends
	// on its own. That has to happen on the game's own thread. The reply is
	// not discarded: onMoveMade() holds it until resume().
	QMetaObject::invokeMethod(this, [this]()
	{
		stopThinkingForPause(0);
		stopPonderingForPause();
	}, Qt::QueuedConnection);
}

void ChessGame::stopPonderingForPause()
{
	// The engine that is not on move may be pondering (thinking on the
	// opponent's time), which would keep the CPU at full load for the
	// whole pause. Stop it; its ponder result is discarded exactly as
	// after a ponder miss, and resume() carries on as after a miss.
	if (!m_paused || m_finished || !m_gameInProgress)
		return;

	ChessPlayer* player = playerToWait();
	if (player != nullptr && !player->isHuman())
		player->pausePondering();
}

void ChessGame::stopThinkingForPause(int attempt)
{
	if (!m_paused || m_finished || !m_gameInProgress || m_moveHeld)
		return;

	ChessPlayer* player = playerToMove();
	if (player == nullptr || player->isHuman()
	||  player->state() != ChessPlayer::Thinking)
		return;

	if (player->pauseThinking())
		return;

	// The engine is not ready to be stopped yet (still being pinged, or
	// "go" not sent yet). Try again shortly, for a couple of seconds.
	if (attempt < 40)
	{
		QTimer::singleShot(50, this, [this, attempt]()
		{
			stopThinkingForPause(attempt + 1);
		});
	}
}

void ChessGame::resume()
{
	if (!m_paused.exchange(false))
		return;
	emit pausedChanged(false);

	// Restarting play touches state (m_openingIndex, the board, the
	// players) that belongs to the game's own thread, so hop over there
	// with a queued call -- safe even when resume() is itself already
	// running on that thread (e.g. via the wokeUp()->resume() connection
	// in beginPlay()): it simply runs on the next pass of that thread's
	// event loop.
	QMetaObject::invokeMethod(this, [this]()
	{
		// Re-check rather than trust the moment this was queued: a
		// later pause()/resume() (rapid double-clicks, say) may have
		// already landed by the time this runs, and the game may
		// have finished in the meantime too.
		if (m_paused || m_finished)
			return;

		if (m_restartPending)
		{
			// The engine on move was stopped by Pause and its
			// cut-short reply discarded: let it think again about
			// the same position instead of playing that reply.
			m_restartPending = false;
			ChessPlayer* player = playerToMove();
			if (player != nullptr && player->state() != ChessPlayer::Thinking)
				player->go();
			return;
		}

		if (m_moveHeld)
		{
			// Apply the move that arrived during the pause.
			ChessPlayer* sender = m_heldMoveSender;
			Chess::Move move = m_heldMove;
			m_moveHeld = false;
			m_heldMoveSender = nullptr;
			m_heldMove = Chess::Move();

			if (sender == playerToMove() && m_board->isLegalMove(move))
				applyMove(sender, move);
			return;
		}

		if (m_openingIndex >= 0)
			// Paused in the middle of a paced opening line:
			// carry on with it instead of starting a turn.
			scheduleOpeningMove(m_openingIndex);
		else
			startTurn();
	}, Qt::QueuedConnection);
}

void ChessGame::initializePgn()
{
	if (m_pgnInitialized)
		return;
	m_pgnInitialized = true;

	m_pgn->setVariant(m_board->variant());
	m_pgn->setStartingFenString(m_board->startingSide(), m_startingFen);
	m_pgn->setDate(QDate::currentDate());
	m_pgn->setPlayerName(Chess::Side::White, m_player[Chess::Side::White]->name());
	m_pgn->setPlayerName(Chess::Side::Black, m_player[Chess::Side::Black]->name());
	m_pgn->setResult(m_result);

	if (m_timeControl[Chess::Side::White] == m_timeControl[Chess::Side::Black])
		m_pgn->setTag("TimeControl", m_timeControl[0].toString());
	else
	{
		m_pgn->setTag("WhiteTimeControl", m_timeControl[Chess::Side::White].toString());
		m_pgn->setTag("BlackTimeControl", m_timeControl[Chess::Side::Black].toString());
	}
}

void ChessGame::startGame()
{
	disconnect(this, SIGNAL(playersReady()), this, SLOT(startGame()));
	if (m_finished)
		return;

	m_gameInProgress = true;
	for (int i = 0; i < 2; i++)
	{
		ChessPlayer* player = m_player[i];
		Q_ASSERT(player != nullptr);
		Q_ASSERT(player->isReady());

		if (player->state() == ChessPlayer::Disconnected)
		{
			setError(tr("Could not initialize player %1: %2")
			         .arg(player->name(), player->errorString()));
			m_result = Chess::Result(Chess::Result::ResultError);
			stop();
			emitStartFailed();
			return;
		}
		if (!player->supportsVariant(m_board->variant()))
		{
			qWarning("%s doesn't support variant %s",
				 qUtf8Printable(player->name()),
				 qUtf8Printable(m_board->variant()));
			m_result = Chess::Result(Chess::Result::ResultError);
			stop();
			return;
		}
	}

	m_pgn->setPlayerName(Chess::Side::White, m_player[Chess::Side::White]->name());
	m_pgn->setPlayerName(Chess::Side::Black, m_player[Chess::Side::Black]->name());

	emit started(this);
	QDateTime gameStartTime = QDateTime::currentDateTime();
	m_pgn->setGameStartTime(gameStartTime);

	for (int i = 0; i < 2; i++)
	{
		Chess::Side side = Chess::Side::Type(i);

		Q_ASSERT(m_timeControl[side].isValid());
		m_player[side]->setTimeControl(m_timeControl[side]);
		m_player[side]->newGame(side, m_player[side.opposite()], m_board);
	}

	// Play the forced opening moves first. These are the moves of the
	// opening suite (PGN/EPD) and, when both sides use an opening book,
	// the moves generated from the book by generateOpening(). This is
	// where nearly all "book" moves in a tournament come from.
	//
	// Normally they are all played back-to-back, which makes the whole
	// line flash past on screen. In engine-engine games with a book move
	// delay set, they are paced out one at a time instead.
	if (m_bookMoveDelay > 0
	&&  !m_moves.isEmpty()
	&&  !m_player[Chess::Side::White]->isHuman()
	&&  !m_player[Chess::Side::Black]->isHuman())
	{
		m_openingIndex = 0;
		scheduleOpeningMove(0);
		return;
	}

	for (int i = 0; i < m_moves.size(); i++)
	{
		if (!playOpeningMove(i))
			return;
	}

	beginPlay();
}

bool ChessGame::playOpeningMove(int index)
{
	Chess::Move move(m_moves.at(index));
	Q_ASSERT(m_board->isLegalMove(move));

	addPgnMove(move, "book");

	playerToMove()->makeBookMove(move);
	playerToWait()->makeMove(move);
	m_board->makeMove(move);

	emitLastMove();

	if (!m_board->result().isNone())
	{
		qWarning("Every move was played from the book");
		m_result = m_board->result();
		stop();
		return false;
	}

	return true;
}

void ChessGame::scheduleOpeningMove(int index)
{
	QTimer::singleShot(m_bookMoveDelay, this, [this, index]()
	{
		continueOpening(index);
	});
}

void ChessGame::continueOpening(int index)
{
	// Stale timers (e.g. left over from a pause/resume) do nothing
	if (m_finished || index != m_openingIndex)
		return;

	// resume() reschedules the move
	if (m_paused)
		return;

	if (!playOpeningMove(index))
	{
		m_openingIndex = -1;
		return;
	}

	m_openingIndex = index + 1;
	if (m_openingIndex < m_moves.size())
	{
		scheduleOpeningMove(m_openingIndex);
		return;
	}

	m_openingIndex = -1;
	beginPlay();
}

void ChessGame::beginPlay()
{
	for (int i = 0; i < 2; i++)
	{
		connect(m_player[i], SIGNAL(moveMade(Chess::Move)),
			this, SLOT(onMoveMade(Chess::Move)));
		connect(m_player[i], SIGNAL(searchInterrupted()),
			this, SLOT(onSearchInterrupted()));
		if (m_player[i]->isHuman())
			connect(m_player[i], SIGNAL(wokeUp()),
				this, SLOT(resume()));
	}

	startTurn();
}
