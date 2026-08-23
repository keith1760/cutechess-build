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

#ifndef EVALBAR_H
#define EVALBAR_H

#include <QWidget>

/*!
 * \brief A slim vertical evaluation bar shown next to the chess board.
 *
 * EvalBar shows which side is ahead, from White's point of view, as a
 * proportion of white/black fill. Unlike EvalWidget (which streams the
 * engine's live "thinking" info line-by-line while it searches), EvalBar
 * intentionally only updates once a move has actually been decided/played
 * -- i.e. only in response to ChessGame::scoreChanged(ply, score), which
 * CuteChess already emits exactly once per finished move (see
 * ChessGame::onMoveMade() / emitLastMove() in chessgame.cpp). It must
 * NOT be wired to the engine's per-iteration "info score" signal
 * (that's what EvalWidget/EvalHistory are for), or it will flicker while
 * the engine is still thinking about a move instead of only moving once
 * the move is finalized.
 *
 * Width is fixed at ~3mm, computed from the screen's logical DPI at
 * construction time (recomputed on screen change) so it stays visually
 * consistent across displays. The bar is meant to sit flush against the
 * left edge of the board in GameViewer.
 *
 * KNOWN LIMITATION / TODO (left as a log for a future session, per user
 * request): mate scores are not specially handled -- MoveEvaluation
 * already encodes forced mates as very large centipawn-like values, and
 * this widget just clamps + squashes whatever score it is given via a
 * logistic curve. A future pass should detect mate scores explicitly
 * (see MoveEvaluation / the MATE reference in moveevaluation.h) and
 * render a solid full bar + "M<n>" label instead of relying on clamping.
 */
class EvalBar : public QWidget
{
	Q_OBJECT

	public:
		explicit EvalBar(QWidget* parent = nullptr);

		QSize sizeHint() const override;

	public slots:
		//! Called once a move has been finalized. \a ply is 0-based
		//! (ply 0 = White's first move), \a score is in centipawns
		//! from the perspective of the side that just moved.
		void onScoreChanged(int ply, int score);

		//! Resets the bar to the neutral (50/50) state, e.g. for a
		//! new game.
		void clear();

	protected:
		void paintEvent(QPaintEvent* event) override;

	private:
		int m_widthPx;
		int m_score;    // last known score, White's perspective, cp
		bool m_hasScore;

		void updateWidthFromDpi();
};

#endif // EVALBAR_H
