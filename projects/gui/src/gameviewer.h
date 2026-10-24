/*
    This file is part of Cute Chess.
    Copyright (C) 2008-2018 Cute Chess authors

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

#ifndef GAMEVIEWER_H
#define GAMEVIEWER_H

#include <QWidget>
#include <QVector>
#include <QPointer>
#include <board/side.h>
#include <board/genericmove.h>
class QToolButton;
class QPushButton;
class QSlider;
class ChessGame;
class PgnGame;
class BoardScene;
class BoardView;
class ChessClock;
class EvalBar;
class QLabel;
class QTimer;
namespace Chess
{
	class Board;
}

class GameViewer : public QWidget
{
	Q_OBJECT

	public:
		explicit GameViewer(Qt::Orientation orientation = Qt::Horizontal,
		                    QWidget* parent = nullptr,
		                    bool addChessClock = false);

		void setGame(ChessGame* game);
		void setGame(const PgnGame* pgn);
		void disconnectGame();
		Chess::Board* board() const;
		BoardScene* boardScene() const;
		ChessClock* chessClock(Chess::Side side);
		EvalBar* evalBar() const { return m_evalBar; }

		// Label sandwiched between the two clocks (only created when
		// this GameViewer was built with addChessClock = true), used
		// by MainWindow to show the live tournament score/Elo readout
		// in a prominent spot instead of tucked in the status bar.
		// Returns nullptr if this GameViewer has no clocks.
		QLabel* scoreLabel() const { return m_scoreLabel; }

		/*!
		 * The Branch / Branch again / Return to match buttons, in one
		 * widget, for the main window to place next to Pause/Resume.
		 * The viewer keeps them up to date; they are hidden when they
		 * do not apply. The widget starts out hidden and unplaced:
		 * whoever takes it must add it to a layout and show() it.
		 */
		QWidget* branchBar() const { return m_branchBar; }

		/*!
		 * The "Pause & Start" / "Back" / "Forward" / "End" buttons, in one
		 * widget, for the main window to place in the top right-hand
		 * corner next to the other buttons. Only exists for a viewer
		 * made with addChessClock = true (the main window's), which
		 * has no move slider: these buttons do what the slider's
		 * keyboard steps do. Returns nullptr for any other viewer
		 * (its buttons stay beside its slider). Like branchBar() it
		 * starts out hidden: whoever takes it must show() it.
		 */
		QWidget* navBar() const { return m_navBar; }

	public slots:
		void viewMove(int index, bool keyLeft = false);

	signals:
		/*!
		 * Emitted whenever the text or visibility of the Branch
		 * buttons may have changed, so whoever hosts branchBar()
		 * can re-fit it (the menu-bar corner does not resize a
		 * corner widget on its own when its contents change).
		 */
		void branchBarChanged();
		void moveSelected(int moveNumber);

	private slots:
		void viewFirstMoveClicked();
		void viewPreviousMoveClicked();
		void viewNextMoveClicked();
		void viewLastMoveClicked();
		void viewPositionClicked(int index);

		// The move slider: grabbing or using it pauses a running
		// engine-vs-engine game; once the slider settles the game is
		// taken back to the position shown and resumed.
		void onSliderPressed();
		void onSliderAction(int action);
		void onSliderReleased();
		void commitSliderRewind();
		void onRewound(int plies);

		// "Branch": try a different move from an earlier position,
		// let the engines play on, then return to the real match.
		void onBranchClicked();
		void onRebranchClicked();
		void onBoardUserMove(const Chess::GenericMove& move);
		void onBranched(int plies, const Chess::GenericMove& move);
		void onSidelineChanged(bool active);
		void onSidelineNote(const QString& description);
		void onGameFinished();
		void onPausedChanged(bool paused);

		void onFenChanged(const QString& fen);
		void onMoveMade(const Chess::GenericMove& move);
		void updateEvalBarGeometry(const QRect& boardFrameRect);

	private:
		void viewFirstMove();
		void viewPreviousMove();
		void viewNextMove();
		void viewLastMove();
		void viewPosition(int index);
		void autoFlip();
		void updateOpeningLabel(const PgnGame* pgn);
		bool canRewindGame() const;
		void pauseForSlider();
		void userNavigated();
		void showClocksForPly(int ply);
		void enterBranchMode();
		void leaveBranchMode(bool cancelled);
		void updateBranchUi();
		void syncMovesFromGame();

		BoardScene* m_boardScene;
		BoardView* m_boardView;
		EvalBar* m_evalBar;
		QLabel* m_openingLabel;
		QSlider* m_moveNumberSlider;
		ChessClock* m_chessClock[2];
		QLabel* m_scoreLabel;

		// Holds Back / Forward in the main window (see navBar()).
		QWidget* m_navBar;
		// True when the move slider is not shown (main window): the
		// slider object still exists as the viewer's move-position
		// model but is never visible.
		bool m_externalNav;

		QToolButton* m_viewFirstMoveBtn;
		QToolButton* m_viewPreviousMoveBtn;
		QToolButton* m_viewNextMoveBtn;
		QToolButton* m_viewLastMoveBtn;

		QPointer<ChessGame> m_game;
		QVector<Chess::GenericMove> m_moves;
		int m_moveIndex;
		bool m_humanGame;
		QTimer* m_commitTimer;
		// True if the slider (not the user's Pause button) paused the
		// game, so the slider also resumes it.
		bool m_sliderPausedGame;

		QWidget* m_branchBar;
		QToolButton* m_branchBtn;
		// Shown during a sideline: take that line back too and try
		// another move, without first returning to the match.
		QToolButton* m_rebranchBtn;
		QLabel* m_sidelineLabel;
		// The user is choosing a position and a move to try.
		bool m_branchMode;
		// Branch mode paused the game (so cancelling resumes it).
		bool m_branchPausedGame;
		// The match was running when the first branch was started, so
		// Return to match carries on with it.
		bool m_matchWasRunning;
		// The last result the sideline reached, shown until it ends.
		QString m_sidelineNote;
};

#endif // GAMEVIEWER_H
