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

#ifndef DIALOGGEOMETRY_H
#define DIALOGGEOMETRY_H

#include <QDialog>
#include <QSettings>

/*!
 * \brief Persists a dialog's window position/size across application
 * restarts, the same way MainWindow already does for itself (see
 * MainWindow::readSettings()/writeSettings()).
 *
 * Call restoreDialogGeometry() once, right after ui->setupUi(this), in
 * every dialog whose position the user might care about keeping. It
 * both restores any previously saved geometry immediately and installs
 * a QDialog::finished handler that saves the current geometry every
 * time the dialog closes (however it was closed: OK, Cancel, or the
 * window's own close button all emit finished()).
 *
 * \a key should be a short, unique-per-dialog-class identifier, e.g.
 * "newtournamentdialog". Geometry is stored under
 * ui/dialog_geometry/<key> in QSettings.
 */
inline void restoreDialogGeometry(QDialog* dialog, const QString& key)
{
	const QString settingsKey = QStringLiteral("ui/dialog_geometry/") + key;

	QByteArray geometry = QSettings().value(settingsKey).toByteArray();
	if (!geometry.isEmpty())
		dialog->restoreGeometry(geometry);

	QObject::connect(dialog, &QDialog::finished, dialog, [dialog, settingsKey](int)
	{
		QSettings().setValue(settingsKey, dialog->saveGeometry());
	});
}

#endif // DIALOGGEOMETRY_H
