/*
    This file is part of Cute Chess (fork).

    Cute Chess is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#ifndef SCROLLHELPER_H
#define SCROLLHELPER_H

#include <QDialog>
#include <QGuiApplication>
#include <QLayout>
#include <QScreen>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

namespace ScrollHelper {

// Wraps a widget in a frameless, resizable QScrollArea.
inline QScrollArea* wrapInScrollArea(QWidget* content, QWidget* parent)
{
	auto area = new QScrollArea(parent);
	area->setWidgetResizable(true);
	area->setFrameShape(QFrame::NoFrame);
	area->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	area->setWidget(content);
	return area;
}

// Makes every page of a tab widget scrollable. Page widgets (and the
// pointers to them held by the generated ui class) stay valid.
inline void makeTabsScrollable(QTabWidget* tabs)
{
	const int current = tabs->currentIndex();
	const int count = tabs->count();
	for (int i = 0; i < count; i++)
	{
		QWidget* page = tabs->widget(i);
		if (qobject_cast<QScrollArea*>(page))
			continue;
		const QString title = tabs->tabText(i);
		const QString tip = tabs->tabToolTip(i);
		const QIcon icon = tabs->tabIcon(i);
		const bool enabled = tabs->isTabEnabled(i);

		tabs->removeTab(i);
		QScrollArea* area = wrapInScrollArea(page, tabs);
		tabs->insertTab(i, area, icon, title);
		tabs->setTabToolTip(i, tip);
		tabs->setTabEnabled(i, enabled);
		page->show();
	}
	tabs->setCurrentIndex(current);
}

// Shrinks a dialog's initial size so that it fits the screen.
inline void fitToScreen(QDialog* dialog)
{
	QScreen* screen = dialog->screen();
	if (!screen)
		screen = QGuiApplication::primaryScreen();
	if (!screen)
		return;
	const QSize avail = screen->availableGeometry().size();
	const QSize hint = dialog->sizeHint();
	dialog->resize(qMin(hint.width(), avail.width() * 9 / 10),
		       qMin(hint.height(), avail.height() * 8 / 10));
}

// Moves everything in the dialog's top-level layout, except the last item
// (the button box), into a scroll area. The buttons stay visible.
inline void makeDialogScrollable(QDialog* dialog, QVBoxLayout* layout)
{
	layout->setSizeConstraint(QLayout::SetDefaultConstraint);

	auto content = new QWidget();
	auto contentLayout = new QVBoxLayout(content);
	contentLayout->setContentsMargins(0, 0, 0, 0);

	while (layout->count() > 1)
	{
		QLayoutItem* item = layout->takeAt(0);
		if (item->widget())
			contentLayout->addWidget(item->widget());
		else if (item->layout())
			contentLayout->addLayout(item->layout());
		else
			contentLayout->addItem(item);
	}

	layout->insertWidget(0, wrapInScrollArea(content, dialog), 1);
}

}

#endif // SCROLLHELPER_H
