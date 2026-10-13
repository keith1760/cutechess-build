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

#ifndef CHESSGAME_H
#define CHESSGAME_H

#include <QObject>
#include <QVector>
#include <QStringList>
#include <QMap>
#include <QPair>
#include <QSemaphore>
#include <atomic>
#include "pgngame.h"
#include "board/result.h"
#include "board/move.h"
#include "timecontrol.h"
#include "gameadjudicator.h"

namespace Chess { class Board; }
class ChessPlayer;
class OpeningBook;
class MoveEvaluation;


class LIB_EXPORT ChessGame : public QObject
{
	Q_OBJECT

	public:
		ChessGame(Chess::Board* board, PgnGame* pgn, QObject* parent = nullptr);
		virtual ~ChessGame();
		
		QString errorString() const;
		ChessPlayer* player(Chess::Side side) const;
		ChessPlayer* playerToMove() const;
		ChessPlayer* playerToWait() const;
		bool isFinished() const;
		bool boardShouldBeFlipped() const;
		void setBoardShouldBeFlipped(bool flip);
		/*!
		 * Makes live book moves (used when only one side has an
		 * opening book) depend only on \a seed and the move number,
		 * instead of on the shared generator. This keeps the run
		 * reproducible with concurrent games, and a game replayed
		 * with the same seed picks the same book move in the same
		 * position.
		 */
		void setBookSeed(quint32 seed);

		/*!
		 * Returns true if the game is currently paused, i.e. pause()
		 * has been called and resume() hasn't (yet) been called to
		 * match it.
		 *
		 * \sa pause(), resume(), pausedChanged()
		 */
		bool isPaused() const;

		PgnGame* pgn() const;
		Chess::Board* board() const;
		QString startingFen() const;
		const QVector<Chess::Move>& moves() const;
		const QMap<int,int>& scores() const;
		Chess::Result result() const;

		/*!
		 * Returns the time left (in milliseconds) on White's and
		 * Black's clocks after \a ply plies had been played, as
		 * recorded when that position was reached; (-1, -1) if
		 * there is no such record.
		 *
		 * Not thread-safe by itself: from another thread call it
		 * between lockThread() and unlockThread().
		 */
		QPair<int,int> clockTimesAt(int ply) const;

		void setError(const QString& message);
		void setPlayer(Chess::Side side, ChessPlayer* player);
		void setStartingFen(const QString& fen);
		void setTimeControl(const TimeControl& timeControl,
				    Chess::Side side = Chess::Side());
		void setMoves(const QVector<Chess::Move>& moves);
		bool setMoves(const PgnGame& pgn);
		void setOpeningBook(const OpeningBook* book,
				    Chess::Side side = Chess::Side(),
				    int depth = 1000);
		void setAdjudicator(const GameAdjudicator& adjudicator);
		void setStartDelay(int time);
		void setBookOwnership(bool enabled);
		void setBookMoveDelay(int time);

		void generateOpening();

		void lockThread();
		void unlockThread();
		void stopThinkingForPause(int attempt);
		void stopPonderingForPause();

	public slots:
		void start();
		/*!
		 * Requests that the game pause at the next safe point.
		 *
		 * Safe to call from any thread, including directly from the
		 * GUI thread while this ChessGame runs on its own thread (see
		 * MainWindow::togglePauseResume()): the only state pause()
		 * itself touches is the atomic m_paused flag, so the request
		 * is visible to the game's own thread the instant this call
		 * returns, regardless of what that thread's event queue
		 * already holds (an engine's just-arrived move, in
		 * particular). It's that immediate visibility -- not FIFO
		 * order with other queued events -- that lets the game's own
		 * thread stop at the next safe point instead of one move late.
		 *
		 * The engine that is on move is asked to stop searching and
		 * hand in its move right away, so its clock stops when Pause
		 * is pressed whatever the time control. That move is held
		 * (not applied, not discarded) until resume(). No further
		 * turn is started until resume().
		 *
		 * \sa isPaused(), resume(), pausedChanged()
		 */
		void pause();
		/*!
		 * Cancels a pause requested with pause(), and continues the
		 * game from exactly the position it was paused at.
		 *
		 * Also safe to call from any thread; see pause().
		 *
		 * \sa isPaused(), pause(), pausedChanged()
		 */
		void resume();
		/*!
		 * Takes the game back to the position after \a plies plies and
		 * carries on from there, with the clocks and the adjudicator
		 * as they were at that point.
		 *
		 * Used by the GUI's move slider. The game is paused (if it
		 * is not already), both players abandon whatever they were
		 * doing, and every player is told the shortened game afresh
		 * (newGame() followed by the kept moves), which works for
		 * every protocol and cannot leave an engine out of step with
		 * the board. The record of the game (moves, scores, PGN) is
		 * cut back and rewound() is emitted. If \a resumeAfter is
		 * true the game then resumes through resume(); otherwise it
		 * stays paused until resume() is called.
		 *
		 * Does nothing to a finished game, and nothing if \a plies is
		 * not less than the number of plies played (except that
		 * \a resumeAfter is honoured). Must run on the game's thread:
		 * call it with a queued connection from other threads.
		 *
		 * \sa rewound(), pause(), resume()
		 */
		void rewindTo(int plies, bool resumeAfter = true);
		void stop(bool emitMoveChanged = true);
		void kill();
		void emitStartFailed();
		void onMoveMade(const Chess::Move& move);
		void onSearchInterrupted();
		void onAdjudication(const Chess::Result& result);
		void onResignation(const Chess::Result& result);

	signals:
		/*!
		 * Emitted whenever pause()/resume() actually change the
		 * game's paused state (a call that doesn't change the
		 * state, e.g. resume() while not paused, emits nothing).
		 * \a paused is the new state. Works the same way whether
		 * either side is a human or an engine, and for games that
		 * are part of a tournament/match or standalone.
		 */
		void pausedChanged(bool paused);
		/*!
		 * Emitted by rewindTo() once the game has been cut back to
		 * \a plies plies and the players have been brought up to
		 * date. moves(), scores(), pgn() and the players' clocks
		 * now describe the shortened game; every new move signal
		 * follows this one.
		 */
		void rewound(int plies);
		void humanEnabled(bool);
		void fenChanged(const QString& fenString);
		void moveMade(const Chess::GenericMove& move,
			      const QString& sanString,
			      const QString& comment);
		void moveChanged(int ply,
				 const Chess::GenericMove& move,
				 const QString& sanString,
				 const QString& comment);
		void scoreChanged(int ply, int score);
		void initialized(ChessGame* game = nullptr);
		void started(ChessGame* game = nullptr);
		void finished(ChessGame* game = nullptr,
			      Chess::Result result = Chess::Result());
		void startFailed(ChessGame* game = nullptr);
		void playersReady();

	private slots:
		void startGame();
		void startTurn();
		void finish();
		void onResultClaim(const Chess::Result& result);
		void onPlayerReady();
		void syncPlayers();
		void pauseThread();
		void onRewindPlayersReady();

	private:
		Chess::Move bookMove(Chess::Side side);
		quint32 bookDraw() const;
		bool resetBoard();
		void initializePgn();
		void addPgnMove(const Chess::Move& move, const QString& comment);
		void emitLastMove();
		bool playOpeningMove(int index);
		void scheduleOpeningMove(int index);
		void continueOpening(int index);
		void beginPlay();
		void connectPlayers();
		void applyMove(ChessPlayer* sender, const Chess::Move& move);
		void recordSnapshot();
		void finishRewind();
		
		Chess::Board* m_board;
		ChessPlayer* m_player[2];
		TimeControl m_timeControl[2];
		const OpeningBook* m_book[2];
		int m_bookDepth[2];
		int m_startDelay;
		int m_bookMoveDelay;
		int m_openingIndex;
		bool m_useBookSeed;
		quint32 m_bookSeed;
		bool m_finished;
		bool m_gameInProgress;
		// The single source of truth for whether the game is paused.
		// pause()/resume() may be called directly from any thread (see
		// their doc comments), so this is written with std::atomic's
		// exchange(), and every other piece of code that acts on it --
		// whether that's isPaused() being read cross-thread from the
		// GUI, or startTurn()/continueOpening()/the book-move-delay
		// lambda in startTurn() deciding, on the game's own thread,
		// whether it's allowed to start another turn -- just reads the
		// same flag. There's deliberately no separate "acknowledged" or
		// "settled" flag: the game's own thread is always the one that
		// decides whether a turn may start, and it always does so by
		// checking this flag at the moment it would otherwise start
		// one, so there's nothing a second flag would add.
		std::atomic<bool> m_paused;
		// A move that arrived while the game was paused. It is NOT
		// discarded and the engines are NOT told to stop: an engine
		// that has already answered (or is about to) has already
		// updated its own internal move list, so throwing its reply
		// away would leave it out of sync with the game (wrong side
		// to move, "illegal pv move", bogus scores, forced
		// resignation, killed processes). Instead the move is kept
		// here, untouched, and applied by resume(). Only ever
		// accessed on the game's own thread.
		bool m_moveHeld;
		// An engine's search was cut short by Pause and its reply
		// discarded (ChessPlayer::searchInterrupted()); resume() must
		// start that search again. Game thread only.
		bool m_restartPending;
		ChessPlayer* m_heldMoveSender;
		Chess::Move m_heldMove;
		bool m_pgnInitialized;
		// What rewindTo() needs to restore: the players' clocks and the
		// adjudicator as they were after each ply (index = number of
		// plies played, so there is always one more entry than there
		// are moves). Game thread only.
		struct PlySnapshot
		{
			TimeControl tc[2];
			GameAdjudicator adjudicator;
		};
		QVector<PlySnapshot> m_history;
		// rewindTo() is waiting for the players to settle. While it is,
		// no turn is started. Game thread only.
		bool m_rewinding;
		int m_rewindPlies;
		bool m_rewindResume;
		bool m_playersConnected;
		bool m_bookOwnership;
		bool m_boardShouldBeFlipped;
		QString m_error;
		QString m_startingFen;
		Chess::Result m_result;
		QVector<Chess::Move> m_moves;
		QMap<int,int> m_scores;
		PgnGame* m_pgn;
		QSemaphore m_pauseSem;
		QSemaphore m_resumeSem;
		GameAdjudicator m_adjudicator;
};

#endif // CHESSGAME_H
