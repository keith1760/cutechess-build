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

#include "gameviewer.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QToolButton>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QAbstractSlider>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QFontInfo>
#include <pgngame.h>
#include <chessgame.h>
#include <chessplayer.h>
#include "boardview/boardscene.h"
#include "boardview/boardview.h"
#include "chessclock.h"
#include "evalbar.h"
#include "cutechessapp.h"

GameViewer::GameViewer(Qt::Orientation orientation,
                       QWidget* parent,
                       bool addChessClock)
	: QWidget(parent),
	  m_moveNumberSlider(new QSlider(Qt::Horizontal)),
	  m_scoreLabel(nullptr),
	  m_navBar(addChessClock ? new QWidget(this) : nullptr),
	  m_externalNav(addChessClock),
	  m_viewFirstMoveBtn(new QToolButton),
	  m_viewPreviousMoveBtn(new QToolButton),
	  m_viewNextMoveBtn(new QToolButton),
	  m_viewLastMoveBtn(new QToolButton),
	  m_moveIndex(0),
	  m_humanGame(false),
	  m_commitTimer(new QTimer(this)),
	  m_sliderPausedGame(false),
	  m_branchBar(new QWidget(this)),
	  m_branchBtn(new QToolButton),
	  m_rebranchBtn(new QToolButton),
	  m_sidelineLabel(new QLabel),
	  m_branchMode(false),
	  m_branchPausedGame(false),
	  m_matchWasRunning(false)
{
	#ifdef Q_OS_MAC
	setStyleSheet("QToolButton:!hover { border: none; }");
	#endif

	m_boardScene = new BoardScene(this);
	m_boardView = new BoardView(m_boardScene, this);
	m_boardView->setEnabled(false);

	// Slim evaluation bar, flush against the numbers down the board's
	// left edge. It is intentionally only driven by
	// ChessGame::scoreChanged() (a decided-move signal), not by any
	// per-iteration "engine is thinking" signal -- see evalbar.h for
	// details.
	//
	// It is NOT placed in a layout next to m_boardView: BoardView keeps
	// the board's own aspect ratio, which usually leaves leftover
	// space (painted in the view's background colour) on one or both
	// sides of the actual board inside the view -- a plain layout
	// neighbour would sit at the *view's* edge, not the board's,
	// which is exactly the "far left of screen" bug this was fixed
	// to avoid. Instead it's a free-floating child positioned
	// explicitly against BoardView::boardFrameRect(), which is
	// recomputed by BoardView every time it re-fits the scene.
	//
	// Parented to the view's viewport (not the view itself), since
	// boardFrameRect() is expressed in viewport coordinates -- the
	// view widget's own coordinates would be offset from that by the
	// view's frame width.
	m_evalBar = new EvalBar(m_boardView->viewport());
	// Stays hidden until updateEvalBarGeometry() gives it a real
	// position, so it doesn't flash at its default QWidget geometry
	// for a frame before the board's first fitInView() happens.
	m_evalBar->hide();
	connect(m_boardView, SIGNAL(boardFrameChanged(QRect)),
		this, SLOT(updateEvalBarGeometry(QRect)));

	// Keep the eval bar's orientation in sync with the board: when
	// the "black at bottom" preference is on and the board is
	// currently flipped (black playing from the bottom of the
	// screen), the bar should show black's colour at the bottom
	// too, instead of always defaulting to white. See evalbar.h.
	m_evalBar->setBlackAtBottomPreference(
		CuteChessApplication::instance()->evalBarBlackAtBottom());
	connect(CuteChessApplication::instance(),
		SIGNAL(evalBarBlackAtBottomChanged(bool)),
		m_evalBar, SLOT(setBlackAtBottomPreference(bool)));
	connect(m_boardScene, SIGNAL(flipped(bool)),
		m_evalBar, SLOT(setBoardFlipped(bool)));

	// Displays the currently-known opening (from ECO/PGN tags) in
	// bold. Updated in setGame(const PgnGame*) below. Hidden when
	// there's no opening info yet, rather than showing an empty bar.
	m_openingLabel = new QLabel(this);
	// Matched by the "QLabel#openingLabel" ID selector in
	// CuteChessApplication::applyCustomAppearance(), which is what
	// actually controls this label's font size -- see the comment
	// there. Don't rely on setFont()/setStyleSheet() here, since a
	// plain per-widget font size is silently overridden by the
	// app-wide "QLabel { font-size: ... }" rule.
	m_openingLabel->setObjectName("openingLabel");
	m_openingLabel->setVisible(false);

	m_viewFirstMoveBtn->setEnabled(false);
	m_viewFirstMoveBtn->setAutoRaise(true);
	m_viewFirstMoveBtn->setMinimumSize(32, 32);
	m_viewFirstMoveBtn->setToolTip(tr("Skip to the beginning"));
	m_viewFirstMoveBtn->setIcon(QIcon(":/icons/toolbutton/first_16x16"));
	connect(m_viewFirstMoveBtn, SIGNAL(clicked()),
		this, SLOT(viewFirstMoveClicked()));

	m_viewPreviousMoveBtn->setEnabled(false);
	m_viewPreviousMoveBtn->setAutoRaise(true);
	m_viewPreviousMoveBtn->setMinimumSize(32, 32);
	m_viewPreviousMoveBtn->setToolTip(tr("Previous move"));
	m_viewPreviousMoveBtn->setIcon(QIcon(":/icons/toolbutton/previous_16x16"));
	connect(m_viewPreviousMoveBtn, SIGNAL(clicked()),
		this, SLOT(viewPreviousMoveClicked()));

	m_viewNextMoveBtn->setEnabled(false);
	m_viewNextMoveBtn->setAutoRaise(true);
	m_viewNextMoveBtn->setMinimumSize(32, 32);
	m_viewNextMoveBtn->setToolTip(tr("Next move"));
	m_viewNextMoveBtn->setIcon(QIcon(":/icons/toolbutton/next_16x16"));
	connect(m_viewNextMoveBtn, SIGNAL(clicked()),
		this, SLOT(viewNextMoveClicked()));

	m_viewLastMoveBtn->setEnabled(false);
	m_viewLastMoveBtn->setAutoRaise(true);
	m_viewLastMoveBtn->setMinimumSize(32, 32);
	m_viewLastMoveBtn->setToolTip(tr("Skip to the end"));
	m_viewLastMoveBtn->setIcon(QIcon(":/icons/toolbutton/last_16x16"));
	connect(m_viewLastMoveBtn, SIGNAL(clicked()),
		this, SLOT(viewLastMoveClicked()));

	m_moveNumberSlider->setEnabled(false);
	if (m_externalNav)
	{
		// No slider on screen in the main window: it stays as the
		// hidden model of "which move is shown" (the code below sets
		// its position and listens to it), and the Back / Forward
		// buttons in the top right-hand corner do its job. Parented
		// to this viewer so that it is never a stray top-level window.
		m_moveNumberSlider->setParent(this);
		m_moveNumberSlider->hide();
	}
	#ifdef Q_OS_WIN
	m_moveNumberSlider->setMinimumHeight(18);
	#endif
	connect(m_moveNumberSlider, SIGNAL(valueChanged(int)),
		this, SLOT(viewPositionClicked(int)));

	m_commitTimer->setSingleShot(true);
	connect(m_commitTimer, SIGNAL(timeout()),
		this, SLOT(commitSliderRewind()));
	connect(m_moveNumberSlider, SIGNAL(sliderPressed()),
		this, SLOT(onSliderPressed()));
	connect(m_moveNumberSlider, SIGNAL(actionTriggered(int)),
		this, SLOT(onSliderAction(int)));
	connect(m_moveNumberSlider, SIGNAL(sliderReleased()),
		this, SLOT(onSliderReleased()));

	// The three branch buttons live in one bar that the main window
	// puts next to Pause/Resume (see MainWindow::createMenus()).
	QHBoxLayout* branchLayout = new QHBoxLayout(m_branchBar);
	branchLayout->setContentsMargins(0, 0, 0, 0);
	branchLayout->setSpacing(4);
	branchLayout->addWidget(m_branchBtn);
	branchLayout->addWidget(m_rebranchBtn);
	m_branchBar->hide();

	m_branchBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
	m_rebranchBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
	m_branchBtn->setVisible(false);
	m_branchBtn->setFocusPolicy(Qt::NoFocus);
	connect(m_branchBtn, SIGNAL(clicked()), this, SLOT(onBranchClicked()));
	m_rebranchBtn->setVisible(false);
	m_rebranchBtn->setFocusPolicy(Qt::NoFocus);
	m_rebranchBtn->setText(tr("Branch again"));
	m_rebranchBtn->setToolTip(tr("Pause the sideline and try a different "
		"move from an earlier position of it"));
	connect(m_rebranchBtn, SIGNAL(clicked()), this, SLOT(onRebranchClicked()));
	connect(m_boardScene, SIGNAL(humanMove(Chess::GenericMove, Chess::Side)),
		this, SLOT(onBoardUserMove(Chess::GenericMove)));
	m_sidelineLabel->setVisible(false);
	m_sidelineLabel->setWordWrap(true);
	m_sidelineLabel->setAlignment(Qt::AlignCenter);

	QHBoxLayout* controls = new QHBoxLayout();
	controls->setContentsMargins(0, 0, 0, 0);
	controls->setSpacing(0);
	if (!m_externalNav)
		controls->addWidget(m_viewFirstMoveBtn);
	if (m_externalNav)
	{
		// Back / Forward go to the top right-hand corner (the main
		// window takes navBar()), as text buttons like Pause/Resume.
		// They are the same buttons as before, so their enabled
		// state keeps following the position being shown.
		QHBoxLayout* navLayout = new QHBoxLayout(m_navBar);
		navLayout->setContentsMargins(0, 0, 0, 0);
		navLayout->setSpacing(4);
		navLayout->addWidget(m_viewFirstMoveBtn);
		navLayout->addWidget(m_viewPreviousMoveBtn);
		navLayout->addWidget(m_viewNextMoveBtn);
		navLayout->addWidget(m_viewLastMoveBtn);
		m_navBar->hide();

		const QList<QToolButton*> navBtns = { m_viewFirstMoveBtn,
			m_viewPreviousMoveBtn, m_viewNextMoveBtn, m_viewLastMoveBtn };
		for (QToolButton* b : navBtns)
		{
			b->setToolButtonStyle(Qt::ToolButtonTextOnly);
			b->setAutoRaise(false);
			b->setMinimumSize(0, 0);
			b->setFocusPolicy(Qt::NoFocus);
		}
		m_viewFirstMoveBtn->setText(tr("|<"));
		m_viewFirstMoveBtn->setToolTip(tr("Pause & Start: pause the game and go to the "
			"first position. The game is taken back there, as with the "
			"move slider."));
		m_viewLastMoveBtn->setText(tr(">|"));
		m_viewLastMoveBtn->setToolTip(tr("Pause & End: go to the last move; a game "
			"paused by these buttons carries on"));
		m_viewPreviousMoveBtn->setText(tr("<"));
		m_viewPreviousMoveBtn->setToolTip(tr("Pause & Back: pause the game and go back "
			"one move. The game is taken back to the position shown, "
			"as with the move slider."));
		m_viewNextMoveBtn->setText(tr(">"));
		m_viewNextMoveBtn->setToolTip(tr("Pause & Forward: pause the game and go forward "
			"one move. At the last move the game carries on."));
	}
	else
	{
		controls->addWidget(m_viewPreviousMoveBtn);
		controls->addWidget(m_viewNextMoveBtn);
	}
	if (!m_externalNav)
		controls->addWidget(m_viewLastMoveBtn);

	QVBoxLayout* layout = new QVBoxLayout();
	layout->setContentsMargins(0, 0, 0, 0);

	layout->addWidget(m_openingLabel);

	if (addChessClock)
	{
		m_chessClock[0] = new ChessClock();

		// Live tournament score / Elo readout. Sits between the two
		// clocks -- a much more prominent spot than the bottom-right
		// corner of the status bar it used to live in, and the
		// stretches on either side keep it centered regardless of
		// how wide the player-name labels in the clocks end up being.
		// Hidden until MainWindow has something to show (a two-player
		// tournament in progress).
		m_scoreLabel = new QLabel();
		m_scoreLabel->setAlignment(Qt::AlignCenter);
		// See the matching comment on m_openingLabel above: the
		// font size is controlled entirely by the "QLabel#scoreLabel"
		// ID selector in CuteChessApplication::applyCustomAppearance().
		m_scoreLabel->setObjectName("scoreLabel");
		m_scoreLabel->hide();

		m_chessClock[1] = new ChessClock();

		QHBoxLayout* clockLayout = new QHBoxLayout();
		clockLayout->addWidget(m_chessClock[0]);
		clockLayout->addStretch();
		clockLayout->addWidget(m_scoreLabel);
		clockLayout->addStretch();
		clockLayout->addWidget(m_chessClock[1]);
		layout->addLayout(clockLayout);
	}
	else
	{
		m_chessClock[0] = nullptr;
		m_chessClock[1] = nullptr;
	}

	// The eval bar is a floating child of m_boardView, not a layout
	// item here -- see the comment where it's constructed above.
	layout->addWidget(m_boardView);

	if (m_externalNav)
	{
		// No slider and no buttons under the board: all four
		// navigation buttons are in the top right-hand corner.
	}
	else if (orientation == Qt::Horizontal)
	{
		controls->addSpacing(6);
		controls->addWidget(m_moveNumberSlider);
	}
	else
	{
		layout->addWidget(m_moveNumberSlider);

		controls->insertStretch(0);
		controls->addStretch();
	}

	layout->addWidget(m_sidelineLabel);
	if (!m_externalNav)
		layout->addLayout(controls);
	setLayout(layout);
}

ChessClock* GameViewer::chessClock(Chess::Side side)
{
	return m_chessClock[side];
}

void GameViewer::autoFlip()
{
	if (QSettings().value("ui/auto_flip_board_for_human_games",false).toBool())
		m_boardScene->flip();
}

void GameViewer::updateEvalBarGeometry(const QRect& boardFrameRect)
{
	// Right edge of the bar touches the left edge of the board frame,
	// i.e. right where the rank numbers start. Height matches the
	// board frame (squares plus the coordinate-label margin) rather
	// than the full view, so it doesn't run past a letterboxed board.
	int barWidth = m_evalBar->sizeHint().width();
	int x = boardFrameRect.left() - barWidth;

	// If the view happens to leave no room to the left of the board
	// (e.g. it's width-constrained rather than height-constrained, so
	// any letterboxing landed above/below instead of left/right),
	// there's nowhere to put the bar without covering the board
	// itself. Clamp rather than draw off-screen to the left.
	if (x < 0)
		x = 0;

	m_evalBar->setGeometry(x, boardFrameRect.top(),
			       barWidth, boardFrameRect.height());
	m_evalBar->show();
	m_evalBar->raise();
}

void GameViewer::setGame(ChessGame* game)
{
	Q_ASSERT(game != nullptr);

	setGame(game->pgn());
	m_game = game;

	connect(m_game, SIGNAL(fenChanged(QString)),
		this, SLOT(onFenChanged(QString)));
	connect(m_game, SIGNAL(rewound(int)),
		this, SLOT(onRewound(int)));
	connect(m_game, SIGNAL(moveMade(Chess::GenericMove, QString, QString)),
		this, SLOT(onMoveMade(Chess::GenericMove)));
	connect(m_game, SIGNAL(branched(int, Chess::GenericMove)),
		this, SLOT(onBranched(int, Chess::GenericMove)));
	connect(m_game, SIGNAL(sidelineChanged(bool)),
		this, SLOT(onSidelineChanged(bool)));
	connect(m_game, SIGNAL(sidelineNote(QString)),
		this, SLOT(onSidelineNote(QString)));
	connect(m_game, SIGNAL(finished(ChessGame*, Chess::Result)),
		this, SLOT(onGameFinished()));
	connect(m_game, SIGNAL(pausedChanged(bool)),
		this, SLOT(onPausedChanged(bool)));
	connect(m_game, SIGNAL(humanEnabled(bool)),
		m_boardView, SLOT(setEnabled(bool)));
	// NOTE: deliberately scoreChanged (emitted once per finalized
	// move), not any live/per-iteration search-info signal. Do not
	// change this to a "thinking" signal -- see evalbar.h.
	connect(m_game, SIGNAL(scoreChanged(int,int)),
		m_evalBar, SLOT(onScoreChanged(int,int)));
	m_evalBar->clear();

	// WORKAROUND for the eval bar colours occasionally not matching
	// the board in engine-vs-engine games -- see the doc comment on
	// EvalBar::setEngineGameBottomColor() for the full story. Record,
	// once per game, which side is moving up the board (i.e. sitting
	// at the bottom, advancing toward the opponent) and pin the
	// bar's colours to that directly, rather than trusting the live
	// flip-tracking path, which is only really exercised by a human
	// interactively flipping the board and has not proven reliable
	// when nobody is doing that.
	{
		ChessPlayer* whitePlayer = m_game->player(Chess::Side::White);
		ChessPlayer* blackPlayer = m_game->player(Chess::Side::Black);
		bool engineGame = whitePlayer && blackPlayer
				&& !whitePlayer->isHuman()
				&& !blackPlayer->isHuman();
		if (engineGame)
		{
			bool whiteMovingUp = !m_game->boardShouldBeFlipped();
			m_evalBar->setEngineGameBottomColor(whiteMovingUp);
		}
		else
			m_evalBar->clearEngineGameBottomColor();
	}

	for (int i = 0; i < 2; i++)
	{
		ChessPlayer* player(m_game->player(Chess::Side::Type(i)));
		if (player->isHuman())
			connect(m_boardScene, SIGNAL(humanMove(Chess::GenericMove, Chess::Side)),
				player, SLOT(onHumanMove(Chess::GenericMove, Chess::Side)));
	}

	connect(m_game, SIGNAL(finished(ChessGame*, Chess::Result)),
		m_boardScene, SLOT(onGameFinished(ChessGame*, Chess::Result)));
	m_boardView->setEnabled(!m_game->isFinished() &&
				m_game->playerToMove()->isHuman());
	m_humanGame = !m_game.isNull()
		    && m_game->playerToMove()->isHuman()
		    && m_game->playerToWait()->isHuman();

	if (m_humanGame && m_game->playerToMove() != m_game->player(Chess::Side::White))
		autoFlip();

	updateBranchUi();
}

void GameViewer::setGame(const PgnGame* pgn)
{
	Q_ASSERT(pgn != nullptr);

	disconnectGame();

	auto board = pgn->createBoard();
	if (board)
	{
		m_boardScene->setBoard(board);
		m_boardScene->populate();
	}
	else
	{
		QMessageBox::critical(
			this, tr("Cannot show game"),
			tr("This game is incompatible with Cute Chess and cannot be shown."));
	}
	m_moveIndex = 0;
	m_evalBar->clear();
	updateOpeningLabel(pgn);

	m_moves.clear();
	for (const PgnGame::MoveData& md : pgn->moves())
		m_moves.append(md.move);

	m_viewFirstMoveBtn->setEnabled(false);
	m_viewPreviousMoveBtn->setEnabled(false);
	m_viewNextMoveBtn->setEnabled(!m_moves.isEmpty());
	m_viewLastMoveBtn->setEnabled(!m_moves.isEmpty());

	m_moveNumberSlider->setEnabled(!m_moves.isEmpty());
	m_moveNumberSlider->setMaximum(m_moves.count());
	m_moveNumberSlider->setValue(0);

	viewLastMove();
}

void GameViewer::disconnectGame()
{
	m_commitTimer->stop();
	m_sliderPausedGame = false;
	m_branchMode = false;
	m_branchPausedGame = false;
	m_sidelineNote.clear();
	m_boardView->setEnabled(false);
	if (m_game.isNull())
	{
		updateBranchUi();
		return;
	}

	disconnect(m_game, nullptr, m_boardScene, nullptr);
	disconnect(m_game, nullptr, m_boardView, nullptr);
	disconnect(m_game, nullptr, this, nullptr);

	for (int i = 0; i < 2; i++)
	{
		ChessPlayer* player(m_game->player(Chess::Side::Type(i)));
		if (player != nullptr)
			disconnect(m_boardScene, nullptr, player, nullptr);
	}

	m_game = nullptr;
	updateBranchUi();
}

Chess::Board* GameViewer::board() const
{
	return m_boardScene->board();
}

BoardScene* GameViewer::boardScene() const
{
	return m_boardScene;
}

void GameViewer::viewFirstMoveClicked()
{
	// Same as the move slider's keyboard steps: pause a running
	// engine game, show the move, and (after a short wait) take the
	// game back to the position on the board.
	userNavigated();
	viewFirstMove();
	emit moveSelected(m_moveIndex - 1);
}

void GameViewer::viewFirstMove()
{
	while (m_moveIndex > 0)
		viewPreviousMove();
}

void GameViewer::viewPreviousMoveClicked()
{
	// Same as the move slider's keyboard steps: pause a running
	// engine game, show the move, and (after a short wait) take the
	// game back to the position on the board.
	userNavigated();
	viewPreviousMove();
	emit moveSelected(m_moveIndex - 1);
}

void GameViewer::viewPreviousMove()
{
	m_moveIndex--;

	if (m_moveIndex == 0)
	{
		m_viewPreviousMoveBtn->setEnabled(false);
		m_viewFirstMoveBtn->setEnabled(false);
	}

	m_boardScene->undoMove();

	m_viewNextMoveBtn->setEnabled(true);
	m_viewLastMoveBtn->setEnabled(true);
	m_boardView->setEnabled(m_branchMode);

	m_moveNumberSlider->setSliderPosition(m_moveIndex);
}

void GameViewer::viewNextMoveClicked()
{
	// Same as the move slider's keyboard steps: pause a running
	// engine game, show the move, and (after a short wait) take the
	// game back to the position on the board.
	userNavigated();
	viewNextMove();
	emit moveSelected(m_moveIndex - 1);
}

void GameViewer::viewNextMove()
{
	m_boardScene->makeMove(m_moves.at(m_moveIndex++));

	m_viewPreviousMoveBtn->setEnabled(true);
	m_viewFirstMoveBtn->setEnabled(true);

	if (m_moveIndex >= m_moves.count())
	{
		m_viewNextMoveBtn->setEnabled(false);
		m_viewLastMoveBtn->setEnabled(false);

		if (!m_game.isNull()
		&&  !m_game->isFinished() && m_game->playerToMove()->isHuman())
			m_boardView->setEnabled(true);
	}
	if (m_branchMode)
		m_boardView->setEnabled(true);

	m_moveNumberSlider->setSliderPosition(m_moveIndex);
}

void GameViewer::viewLastMoveClicked()
{
	// Same as the move slider's keyboard steps: pause a running
	// engine game, show the move, and (after a short wait) take the
	// game back to the position on the board.
	userNavigated();
	viewLastMove();
	emit moveSelected(m_moveIndex - 1);
}

void GameViewer::viewLastMove()
{
	while (m_moveIndex < m_moves.count())
		viewNextMove();
}

void GameViewer::viewPositionClicked(int index)
{
	viewPosition(index);
	emit moveSelected(m_moveIndex - 1);

	// While the user works the slider the clocks follow the position
	// on the board.
	if (m_moveNumberSlider->isSliderDown() || m_commitTimer->isActive()
	||  m_sliderPausedGame)
		showClocksForPly(m_moveIndex);
}

void GameViewer::viewPosition(int index)
{
	if (m_moves.isEmpty())
		return;

	while (index < m_moveIndex)
		viewPreviousMove();
	while (index > m_moveIndex)
		viewNextMove();
}

void GameViewer::viewMove(int index, bool keyLeft)
{
	Q_ASSERT(index >= 0);
	Q_ASSERT(!m_moves.isEmpty());

	if (keyLeft && index == m_moveIndex - 2)
		viewPreviousMove();
	else if (index < m_moveIndex)
	{
		// We backtrack one move too far and then make one
		// move forward to highlight the correct move
		while (index < m_moveIndex)
			viewPreviousMove();
		viewNextMove();
	}
	else
	{
		while (index + 1 > m_moveIndex)
			viewNextMove();
	}
}

void GameViewer::onFenChanged(const QString& fen)
{
	m_moves.clear();
	m_moveIndex = 0;

	m_viewFirstMoveBtn->setEnabled(false);
	m_viewPreviousMoveBtn->setEnabled(false);
	m_viewNextMoveBtn->setEnabled(false);
	m_viewLastMoveBtn->setEnabled(false);
	m_moveNumberSlider->setEnabled(false);
	m_moveNumberSlider->setMaximum(0);

	m_boardScene->setFenString(fen);
}

void GameViewer::onMoveMade(const Chess::GenericMove& move)
{
	m_moves.append(move);

	m_moveNumberSlider->setEnabled(true);
	m_moveNumberSlider->setMaximum(m_moves.count());

	if (m_moveIndex == m_moves.count() - 1)
		viewNextMove();

	if (!m_game.isNull())
		updateOpeningLabel(m_game->pgn());

	if (m_humanGame)
		autoFlip();
}

void GameViewer::updateOpeningLabel(const PgnGame* pgn)
{
	if (pgn == nullptr)
	{
		m_openingLabel->clear();
		m_openingLabel->setVisible(false);
		return;
	}

	QString opening = pgn->tagValue("Opening");
	QString variation = pgn->tagValue("Variation");

	if (opening.isEmpty())
	{
		m_openingLabel->clear();
		m_openingLabel->setVisible(false);
		return;
	}

	QString text = opening;
	if (!variation.isEmpty())
		text += QLatin1String(", ") + variation;

	// Label text is bold by construction (font set in the ctor); the
	// task only asked for the opening name itself, not the "Opening:"
	// prefix, but we still stay honest and keep it recognizable.
	m_openingLabel->setText(text);
	m_openingLabel->setVisible(true);
}

bool GameViewer::canRewindGame() const
{
	if (m_game.isNull() || m_game->isFinished())
		return false;

	// Only engine-vs-engine games: with a human playing, the slider
	// just browses, as before.
	for (int i = 0; i < 2; i++)
	{
		ChessPlayer* player = m_game->player(Chess::Side::Type(i));
		if (player == nullptr || player->isHuman())
			return false;
	}
	return true;
}

void GameViewer::pauseForSlider()
{
	if (!canRewindGame() || m_sliderPausedGame || m_game->isPaused())
		return;

	m_sliderPausedGame = true;
	m_game->pause();
}

void GameViewer::userNavigated()
{
	// Exactly what onSliderAction() does for a keyboard/wheel step of
	// the slider: pause (if the user had not), and (re)start the wait
	// after which commitSliderRewind() cuts the game back to the shown
	// position and resumes it if we paused it. In branch mode, like the
	// slider, the buttons only browse.
	if (m_branchMode)
		return;
	pauseForSlider();
	m_commitTimer->start(500);
}

void GameViewer::onSliderPressed()
{
	if (m_branchMode)
		return;
	m_commitTimer->stop();
	pauseForSlider();
}

void GameViewer::onSliderAction(int action)
{
	// Qt also reports setSliderPosition() calls made by this class (the
	// slider follows the game) as SliderMove. Those are not the user:
	// a real SliderMove happens only while the handle is held down.
	if (action == QAbstractSlider::SliderMove
	&&  !m_moveNumberSlider->isSliderDown())
		return;

	// While choosing a move to try, the slider only browses.
	if (m_branchMode)
		return;

	// Keys, wheel, page steps and dragging all come through here. Wait
	// for the user to stop (a drag waits for the release instead).
	pauseForSlider();
	if (m_moveNumberSlider->isSliderDown())
		m_commitTimer->stop();
	else
		m_commitTimer->start(500);
}

void GameViewer::onSliderReleased()
{
	if (m_branchMode)
		return;
	if (m_sliderPausedGame || canRewindGame())
		m_commitTimer->start(0);
}

void GameViewer::commitSliderRewind()
{
	if (m_branchMode)
		return;
	if (m_game.isNull())
	{
		m_sliderPausedGame = false;
		return;
	}

	const bool resume = m_sliderPausedGame;
	m_sliderPausedGame = false;

	if (!canRewindGame())
	{
		// The game ended while the slider was in use: browse only.
		if (resume)
			m_game->resume();
		return;
	}

	// The slider is on a position that is already part of the game
	// (at the end): nothing to take back.
	if (m_moveIndex >= m_moves.count())
	{
		if (resume)
			m_game->resume();
		return;
	}

	// Take the game back to the position on the board, then (if the
	// slider paused it) resume. This runs on the game's own thread.
	QMetaObject::invokeMethod(m_game.data(), "rewindTo",
				  Qt::QueuedConnection,
				  Q_ARG(int, m_moveIndex),
				  Q_ARG(bool, resume));
}

void GameViewer::showClocksForPly(int ply)
{
	if (m_game.isNull() || m_chessClock[0] == nullptr)
		return;

	m_game->lockThread();
	QPair<int,int> times = m_game->clockTimesAt(ply);
	bool infinite = m_game->player(Chess::Side::White)->timeControl()->isInfinite();
	m_game->unlockThread();

	if (infinite || times.first < 0)
		return;

	m_chessClock[Chess::Side::White]->stop();
	m_chessClock[Chess::Side::White]->setTime(times.first);
	m_chessClock[Chess::Side::Black]->stop();
	m_chessClock[Chess::Side::Black]->setTime(times.second);
}

void GameViewer::onRewound(int plies)
{
	// The game has been cut back to 'plies' plies. If the board shows
	// more than that (the user moved on again meanwhile) step back.
	if (m_moveIndex > plies)
		viewPosition(plies);
	if (m_moves.count() > plies)
		m_moves.resize(plies);

	m_moveNumberSlider->setEnabled(!m_moves.isEmpty());
	m_moveNumberSlider->setMaximum(m_moves.count());
	m_moveNumberSlider->setSliderPosition(m_moveIndex);

	const bool atEnd = (m_moveIndex >= m_moves.count());
	m_viewNextMoveBtn->setEnabled(!atEnd);
	m_viewLastMoveBtn->setEnabled(!atEnd);
	m_viewPreviousMoveBtn->setEnabled(m_moveIndex > 0);
	m_viewFirstMoveBtn->setEnabled(m_moveIndex > 0);

	// Evaluation bar: the score of the last move that is left.
	m_evalBar->clear();
	if (!m_game.isNull() && plies > 0)
	{
		m_game->lockThread();
		const QMap<int,int> scores = m_game->scores();
		m_game->unlockThread();
		auto it = scores.constFind(plies - 1);
		if (it != scores.constEnd())
			m_evalBar->onScoreChanged(plies - 1, it.value());
	}

	if (!m_game.isNull())
		updateOpeningLabel(m_game->pgn());

	showClocksForPly(plies);
	updateBranchUi();
}

// ---------------------------------------------------------------------
// Branch: try a different move from an earlier position of a running
// engine-vs-engine game, watch the engines play on, then go back to the
// real match. The game keeps the original aside (ChessGame::branchTo()).
// ---------------------------------------------------------------------

void GameViewer::updateBranchUi()
{
	const bool sideline = !m_game.isNull() && m_game->isSideline();
	const bool can = canRewindGame();

	m_branchBtn->setVisible(sideline || can);
	m_branchBtn->setEnabled(true);
	m_rebranchBtn->setVisible(sideline && can && !m_branchMode);
	m_rebranchBtn->setEnabled(true);

	if (m_branchMode)
	{
		m_branchBtn->setText(tr("Cancel branch"));
		m_branchBtn->setToolTip(tr("Leave without trying a move"));
		m_sidelineLabel->setText(tr("Choose a position with the Back / "
			"Forward buttons or the move list, then move a piece to try "
			"that move. The time per move is set in Tools > Settings > "
			"Games. The branch starts paused: press Resume to let the "
			"engines play on."));
	}
	else if (sideline)
	{
		m_branchBtn->setText(tr("Return to match"));
		m_branchBtn->setToolTip(tr("Abandon this sideline and go back to "
			"the match exactly as it was"));
		m_sidelineLabel->setText(m_sidelineNote.isEmpty()
			? tr("Sideline: the engines play on from your move when you "
			     "press Resume. This does not count in the match.")
			: tr("Sideline ended (%1). It does not count in the match.")
				.arg(m_sidelineNote));
	}
	else
	{
		m_branchBtn->setText(tr("Branch"));
		m_branchBtn->setToolTip(tr("Pause the game and try a different "
			"move from an earlier position"));
	}

	m_sidelineLabel->setVisible(sideline || m_branchMode);

	emit branchBarChanged();
}

void GameViewer::enterBranchMode()
{
	if (!canRewindGame() || m_moves.isEmpty())
		return;

	// The user's own Pause stays theirs. If the slider paused the game,
	// or we do now, cancelling gives the game back.
	m_commitTimer->stop();
	if (!m_game->isSideline())
		m_matchWasRunning = m_sliderPausedGame || !m_game->isPaused();
	m_branchPausedGame = m_sliderPausedGame;
	m_sliderPausedGame = false;
	if (!m_game->isPaused())
	{
		m_game->pause();
		m_branchPausedGame = true;
	}

	m_branchMode = true;
	m_boardView->setEnabled(true);
	updateBranchUi();
}

void GameViewer::leaveBranchMode(bool cancelled)
{
	if (!m_branchMode)
		return;

	m_branchMode = false;
	m_boardView->setEnabled(false);

	if (cancelled)
	{
		viewLastMove();
		if (m_branchPausedGame && !m_game.isNull())
			m_game->resume();
	}
	m_branchPausedGame = false;
	updateBranchUi();
}

void GameViewer::onBranchClicked()
{
	if (m_game.isNull())
		return;

	// Choosing a move to try (in the match or inside a sideline):
	// the button is "Cancel branch".
	if (m_branchMode)
	{
		leaveBranchMode(true);
		return;
	}

	if (m_game->isSideline())
	{
		// Put the original match back. It carries on if it was
		// running when the branch was made (setting
		// ui/sideline_resume_on_return); otherwise it stays paused.
		const bool resume = m_matchWasRunning
			&& QSettings().value("ui/sideline_resume_on_return", true).toBool();
		m_branchBtn->setEnabled(false);
		m_rebranchBtn->setEnabled(false);
		QMetaObject::invokeMethod(m_game.data(), "endSideline",
					  Qt::QueuedConnection,
					  Q_ARG(bool, resume));
		return;
	}

	enterBranchMode();
}

void GameViewer::onRebranchClicked()
{
	if (m_game.isNull() || !m_game->isSideline() || m_branchMode)
		return;

	enterBranchMode();
}

void GameViewer::onBoardUserMove(const Chess::GenericMove& move)
{
	if (!m_branchMode || m_game.isNull())
		return;

	// The game is cut back to the position on the board and the move
	// is played there. This runs on the game's own thread.
	const int ply = m_moveIndex;
	// Time per move for the branch: Tools > Settings > Games.
	const int ms = QSettings().value("ui/sideline_move_ms", 5000).toInt();
	m_sidelineNote.clear();
	leaveBranchMode(false);
	QMetaObject::invokeMethod(m_game.data(), "branchTo",
				  Qt::QueuedConnection,
				  Q_ARG(int, ply),
				  Q_ARG(Chess::GenericMove, move),
				  Q_ARG(int, qBound(1000, ms, 60000)));
}

void GameViewer::onBranched(int plies, const Chess::GenericMove& move)
{
	// rewound(plies) has just cut the viewer's moves back; now add the
	// user's move (the viewer follows it if it was on the last move).
	Q_UNUSED(plies);
	onMoveMade(move);
	updateBranchUi();
}

void GameViewer::onSidelineChanged(bool active)
{
	m_sidelineNote.clear();
	if (!active)
		syncMovesFromGame();
	updateBranchUi();
}

void GameViewer::onSidelineNote(const QString& description)
{
	m_sidelineNote = description;
	updateBranchUi();
}

void GameViewer::onPausedChanged(bool paused)
{
	// While choosing a move to try, the game must stay where it is
	// (Resume was pressed, or another window resumed the game).
	if (!paused && m_branchMode && !m_game.isNull())
		m_game->pause();
}

void GameViewer::onGameFinished()
{
	m_sidelineNote.clear();
	if (m_branchMode)
	{
		m_branchMode = false;
		m_branchPausedGame = false;
		m_boardView->setEnabled(false);
	}
	updateBranchUi();
}

void GameViewer::syncMovesFromGame()
{
	// The original game is back. rewound() has already cut the viewer
	// back to the part it shares with the sideline; add the rest.
	if (m_game.isNull())
		return;

	m_game->lockThread();
	const QVector<PgnGame::MoveData> moves = m_game->pgn()->moves();
	const QMap<int,int> scores = m_game->scores();
	m_game->unlockThread();

	for (int i = m_moves.count(); i < moves.size(); i++)
		onMoveMade(moves.at(i).move);

	m_evalBar->clear();
	if (!moves.isEmpty())
	{
		const int ply = moves.size() - 1;
		auto it = scores.constFind(ply);
		if (it != scores.constEnd())
			m_evalBar->onScoreChanged(ply, it.value());
	}
	showClocksForPly(moves.size());
}
