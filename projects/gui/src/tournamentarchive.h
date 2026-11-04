/*
    This file is part of Cute Chess.

    Cute Chess is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#ifndef TOURNAMENTARCHIVE_H
#define TOURNAMENTARCHIVE_H

#include <QString>
#include <QFileInfoList>

class QWidget;

/*!
 * The "Previous Tournaments" folder: one HTML crosstable per tournament
 * (finished or not). The folder never grows past maxBytes(); when a save
 * would exceed it, the oldest records are deleted first.
 *
 * The folder is "Previous Tournaments" next to the Cute Chess program
 * (the main Cute Chess folder). If that location is read-only (e.g. an
 * installation under Program Files, or an AppImage), the same folder name
 * inside the user's application-data folder is used instead.
 */
class TournamentArchive
{
	public:
		/*! Size limit of the whole folder, in bytes. */
		static qint64 maxBytes();

		/*! Full path of the archive folder, created if needed.
		    Empty if no writable location exists. */
		static QString directory();

		/*!
		 * Writes \a html as a record. If \a recordFile is empty a new
		 * record is started (its path is returned in \a recordFile),
		 * otherwise that record is overwritten. Afterwards the size
		 * limit is enforced. Returns false if nothing could be saved.
		 */
		static bool save(const QString& name, const QString& html,
				 QString* recordFile);

		/*! Records, newest first. */
		static QFileInfoList records();

		/*! Deletes oldest records until the folder fits in maxBytes().
		    \a keepFile is deleted only as a last resort. */
		static void enforceLimit(const QString& keepFile = QString());

		/*! Human readable title ("2026-11-03 21:25  Name") of a record. */
		static QString displayTitle(const QFileInfo& record);

		/*! Shows a record in a window of its own. */
		static void showRecord(QWidget* parent, const QString& file);
};

#endif // TOURNAMENTARCHIVE_H
