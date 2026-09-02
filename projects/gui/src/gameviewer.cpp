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
#include <QSlider>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <pgngame.h>
#include <chessgame.h>
#include <chessplayer.h>
#include "boardview/boardscene.h"
#include "boardview/boardview.h"
#include "chessclock.h"
#include "evalbar.h"

GameViewer::GameViewer(Qt::Orientation orientation,
                       QWidget* parent,
                       bool addChessClock)
	: QWidget(parent),
	  m_moveNumberSlider(new QSlider(Qt::Horizontal)),
	  m_scoreLabel(nullptr),
	  m_viewFirstMoveBtn(new QToolButton),
	  m_viewPreviousMoveBtn(new QToolButton),
	  m_viewNextMoveBtn(new QToolButton),
	  m_viewLastMoveBtn(new QToolButton),
	  m_moveIndex(0),
	  m_humanGame(false)
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

	// Displays the currently-known opening (from ECO/PGN tags) in
	// bold. Updated in setGame(const PgnGame*) below. Hidden when
	// there's no opening info yet, rather than showing an empty bar.
	m_openingLabel = new QLabel(this);
	QFont openingFont = m_openingLabel->font();
	openingFont.setBold(true);
	m_openingLabel->setFont(openingFont);
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
	#ifdef Q_OS_WIN
	m_moveNumberSlider->setMinimumHeight(18);
	#endif
	connect(m_moveNumberSlider, SIGNAL(valueChanged(int)),
		this, SLOT(viewPositionClicked(int)));

	QHBoxLayout* controls = new QHBoxLayout();
	controls->setContentsMargins(0, 0, 0, 0);
	controls->setSpacing(0);
	controls->addWidget(m_viewFirstMoveBtn);
	controls->addWidget(m_viewPreviousMoveBtn);
	controls->addWidget(m_viewNextMoveBtn);
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
		QFont scoreFont = m_scoreLabel->font();
		scoreFont.setBold(true);
		m_scoreLabel->setFont(scoreFont);
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

	if (orientation == Qt::Horizontal)
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
	connect(m_game, SIGNAL(moveMade(Chess::GenericMove, QString, QString)),
		this, SLOT(onMoveMade(Chess::GenericMove)));
	connect(m_game, SIGNAL(humanEnabled(bool)),
		m_boardView, SLOT(setEnabled(bool)));
	// NOTE: deliberately scoreChanged (emitted once per finalized
	// move), not any live/per-iteration search-info signal. Do not
	// change this to a "thinking" signal -- see evalbar.h.
	connect(m_game, SIGNAL(scoreChanged(int,int)),
		m_evalBar, SLOT(onScoreChanged(int,int)));
	m_evalBar->clear();

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
	m_boardView->setEnabled(false);
	if (m_game.isNull())
		return;

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
	m_boardView->setEnabled(false);

	m_moveNumberSlider->setSliderPosition(m_moveIndex);
}

void GameViewer::viewNextMoveClicked()
{
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

	m_moveNumberSlider->setSliderPosition(m_moveIndex);
}

void GameViewer::viewLastMoveClicked()
{
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
