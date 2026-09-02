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

#include "evalbar.h"

#include <QPainter>
#include <QScreen>
#include <QGuiApplication>
#include <cmath>

namespace {
	// How the raw centipawn score is squashed into a 0..1 fill
	// fraction. This is the same rough shape lichess/chess.com use
	// for their eval bars. k controls how quickly the bar saturates;
	// smaller k = more sensitive around 0.
	double scoreToFraction(int scoreCp)
	{
		const double k = 0.004;
		double x = 1.0 / (1.0 + std::exp(-k * scoreCp));
		return x;
	}
}

EvalBar::EvalBar(QWidget* parent)
	: QWidget(parent),
	  m_widthPx(12),
	  m_score(0),
	  m_hasScore(false)
{
	updateWidthFromDpi();
	setMinimumHeight(40);
}

void EvalBar::updateWidthFromDpi()
{
	qreal dpi = 96.0;
	if (QScreen* screen = QGuiApplication::primaryScreen())
		dpi = screen->logicalDotsPerInchX();

	// 3mm in inches = 3 / 25.4
	int px = qRound((3.0 / 25.4) * dpi);
	if (px < 6)
		px = 6; // don't let it collapse to nothing on odd DPI setups
	m_widthPx = px;

	setFixedWidth(m_widthPx);
}

QSize EvalBar::sizeHint() const
{
	return QSize(m_widthPx, 200);
}

void EvalBar::onScoreChanged(int ply, int score)
{
	// scoreChanged() gives the score from the perspective of whoever
	// just moved. Convert to White's perspective: even ply (0, 2, 4,
	// ...) means White just moved, odd ply means Black just moved.
	int whiteScore = (ply % 2 == 0) ? score : -score;

	m_score = whiteScore;
	m_hasScore = true;
	update();
}

void EvalBar::clear()
{
	m_score = 0;
	m_hasScore = false;
	update();
}

void EvalBar::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, false);

	double fraction = m_hasScore ? scoreToFraction(m_score) : 0.5;
	fraction = qBound(0.0, fraction, 1.0);

	int h = height();
	int whiteHeightPx = qRound(h * fraction);

	// White portion at the bottom, black portion at the top, matching
	// the usual eval-bar convention (bar "fills up" for White).
	QRect blackRect(0, 0, width(), h - whiteHeightPx);
	QRect whiteRect(0, h - whiteHeightPx, width(), whiteHeightPx);

	painter.fillRect(blackRect, QColor(40, 40, 40));
	painter.fillRect(whiteRect, QColor(235, 235, 235));

	// Thin midline marker at the 50/50 point for reference.
	painter.setPen(QColor(150, 150, 150));
	painter.drawLine(0, h / 2, width(), h / 2);
}
