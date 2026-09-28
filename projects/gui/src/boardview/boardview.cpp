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

#include "boardview.h"
#include <QPainter>
#include <QResizeEvent>
#include <QTimer>
#include <QColor>
#include "../cutechessapp.h"


BoardView::BoardView(QGraphicsScene* scene, QWidget* parent)
	: QGraphicsView(scene, parent),
	  m_initialized(false),
	  m_resizeTimer(new QTimer(this))
{
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setRenderHint(QPainter::Antialiasing);
	setMouseTracking(true);

	// QGraphicsView's default MinimalViewportUpdate mode redraws only
	// the union of the rects Qt was told changed since the last paint.
	// GraphicsPiece uses DeviceCoordinateCache (see graphicspiece.cpp),
	// and during very fast games pieces can be moved (and animations
	// started/stopped -- see BoardScene::stopAnimation(), which jumps
	// a running animation straight to its end value rather than
	// letting it play) several times before the viewport gets a
	// chance to actually paint. When that happens, the square a piece
	// just vacated isn't reliably included in that union, so a
	// fragment of the cached piece pixmap is left behind until
	// something else forces a repaint of that square. FullViewportUpdate
	// simply repaints the whole (small) board view every time, which
	// costs nothing noticeable for a widget this size and removes the
	// missed-region case entirely.
	setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

	// "Surround to the board": the area of this view outside the
	// 8x8 squares themselves -- i.e. the coordinate-label margin
	// (a-h / 1-8) plus any leftover space around the scene -- is
	// this QGraphicsView's own background, not something
	// GraphicsBoard::paint() draws. (GraphicsBoard::m_wallColor is
	// a different, unrelated thing: it only colors special "wall"
	// squares used by a couple of chess variants.) Recolor it to
	// the same mid-cream used elsewhere, per user request, instead
	// of the Qt default white. The colour is user configurable via
	// the RGB sliders in the Settings dialog's General tab, so it's
	// read from CuteChessApplication rather than hard-coded, and
	// kept in sync live if the user changes it.
	setBackgroundBrush(CuteChessApplication::instance()->boardBackgroundColor());

	connect(CuteChessApplication::instance(),
		&CuteChessApplication::boardBackgroundColorChanged,
		this, [=](const QColor& color)
	{
		setBackgroundBrush(color);
		viewport()->update();
	});

	QSizePolicy sp(sizePolicy());
	sp.setHeightForWidth(true);
	setSizePolicy(sp);

	m_resizeTimer->setSingleShot(true);
	m_resizeTimer->setInterval(300);

	connect(m_resizeTimer, SIGNAL(timeout()),
		this, SLOT(fitToRect()));
	connect(scene, SIGNAL(sceneRectChanged(QRectF)),
		this, SLOT(onSceneRectChanged()));
}

QSize BoardView::sizeHint() const
{
	QSize size(sceneRect().size().toSize());
	if (!size.isEmpty())
		return size;

	return QSize(200, 200);
}

int BoardView::heightForWidth(int width) const
{
	QSizeF size(sceneRect().size());
	if (!size.isEmpty())
	{
		qreal ar = size.width() / size.height();
		return width / ar;
	}

	return width;
}

void BoardView::paintEvent(QPaintEvent* event)
{
	if (!m_resizePixmap.isNull())
	{
		QRect rect(viewport()->rect());
		qreal srcAr = qreal(m_resizePixmap.width()) / m_resizePixmap.height();
		qreal trgAr = qreal(rect.width()) / rect.height();

		if (srcAr > trgAr)
			rect.setHeight(rect.width() / srcAr);
		else if (srcAr < trgAr)
			rect.setWidth(rect.height() * srcAr);
		rect.moveCenter(viewport()->rect().center());

		QPainter painter(viewport());
		painter.drawPixmap(rect, m_resizePixmap);
	}
	else
		QGraphicsView::paintEvent(event);
}

void BoardView::resizeEvent(QResizeEvent* event)
{
	QGraphicsView::resizeEvent(event);
	if (!m_initialized)
		return;

	if (m_resizePixmap.isNull())
	{
		m_resizePixmap = QPixmap(sceneRect().toRect().size());
		m_resizePixmap.fill(Qt::transparent);
		QPainter painter(&m_resizePixmap);
		scene()->render(&painter);
	}

	m_resizeTimer->start();
}

void BoardView::fitToRect()
{
	m_initialized = true;
	m_resizePixmap = QPixmap();
	fitInView(sceneRect(), Qt::KeepAspectRatio);
	emit boardFrameChanged(boardFrameRect());
}

QRect BoardView::boardFrameRect() const
{
	// mapFromScene() takes the current fitInView() transform into
	// account, so this tracks wherever the board was actually last
	// drawn -- including any letterboxing this view added to
	// preserve the board's aspect ratio.
	return mapFromScene(sceneRect()).boundingRect();
}

void BoardView::onSceneRectChanged()
{
	updateGeometry();
	fitToRect();
}
