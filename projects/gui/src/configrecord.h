/*
    This file is part of Cute Chess (fork).

    Cute Chess is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#ifndef CONFIGRECORD_H
#define CONFIGRECORD_H

#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QSettings;
class EngineManager;

/*!
 * A saved record of the program's configuration, kept in a single
 * human-readable JSON file.
 *
 * The record holds
 *  - every user setting stored in QSettings (General / Games / Tournaments
 *    settings, the New Tournament form's remembered values, board colours,
 *    default file names and so on), and
 *  - the list of configured engines (the contents of engines.json).
 *
 *  - the main window's position, size and dock layout (settings key group
 *    "ui/mainwindow/"; binary values are stored as base64).
 *
 * Deliberately NOT part of the record: the folder last used in the
 * save/restore file dialogs.
 *
 * Note that the window layout in QSettings is only written when the program
 * quits, so after apply() the caller must also apply the layout to the open
 * windows (MainWindow::applySavedGeometry()) for it to take effect and stick.
 *
 * This class has no GUI code, so it can be used and tested without a
 * display.
 */
class ConfigRecord
{
	public:
		/*! File format version written by save(). */
		static const int FormatVersion;

		/*! What a loaded record contains. */
		struct Contents
		{
			QVariantMap settings;	//!< key -> value
			QVariantList engines;	//!< engine configurations (as QVariant)
			bool hasEngines = false;
			bool hasWindowLayout = false;	//!< record has ui/mainwindow/ keys
			QString savedAt;	//!< ISO date/time stored in the file
			int version = 0;
		};

		/*! Returns true if \a key must not be saved or restored. */
		static bool isExcluded(const QString& key);

		/*! Returns true if \a key belongs to the window layout. */
		static bool isWindowLayout(const QString& key);

		/*!
		 * Suggested file name for a new record, e.g.
		 * "CutechessConfig-2026-10-08.json".
		 */
		static QString suggestedFileName();

		/*!
		 * Writes the current settings in \a settings and the engines in
		 * \a engines to \a fileName.
		 * Returns false and sets \a error on failure; the existing file
		 * (if any) is then left untouched.
		 *
		 * The window layout in QSettings is only updated when the program
		 * quits, so it is stale while the program runs. If
		 * \a windowLayout is not empty it is used as the window layout
		 * ("ui/mainwindow/..." keys -> values) instead of whatever
		 * QSettings holds for those keys.
		 */
		static bool save(const QString& fileName,
				 QSettings& settings,
				 EngineManager* engines,
				 QString* error,
				 const QVariantMap& windowLayout = QVariantMap());

		/*!
		 * Reads and validates \a fileName without changing anything.
		 * Returns false and sets \a error if the file is unreadable or
		 * is not a configuration record this version understands.
		 */
		static bool load(const QString& fileName,
				 Contents* contents,
				 QString* error);

		/*!
		 * Makes the program's configuration equal to \a contents:
		 * settings not in the record go back to their defaults, the
		 * engine list (if the record has one) is replaced and written to
		 * \a enginesFile. The stored window layout is only touched if the
		 * record contains one (Contents::hasWindowLayout); the caller
		 * then has to apply it to the open windows.
		 */
		static bool apply(const Contents& contents,
				  QSettings& settings,
				  EngineManager* engines,
				  const QString& enginesFile,
				  QString* error);
};

#endif // CONFIGRECORD_H
