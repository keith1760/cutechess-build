/*
    This file is part of Cute Chess.

    Cute Chess is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#ifndef TOURNAMENTRESUME_H
#define TOURNAMENTRESUME_H

#include <QString>
#include <QStringList>

/*!
 * Support for resuming a tournament from its PGN file.
 *
 * Every tournament started from the GUI saves its settings beside its PGN
 * file ("<pgn>.cutechess.ini": engines and their options, time controls,
 * tournament type and options, opening suite/book, adjudication, name and the
 * random seed that fixes the opening order). Resume restores them.
 *
 * For a PGN without that file, the tournament name, site, variant and
 * the players are reconstructed from the games in the PGN instead; the
 * other options then come from the last tournament set up.
 */
namespace TournamentResume
{

struct PgnSummary
{
	bool readable = false;
	int games = 0;            //!< games with a result
	bool unfinishedTail = false; //!< last game has no result
	QString event;            //!< tournament name
	QString site;
	QString variant;
	QStringList players;      //!< in order of first appearance
};

struct Info
{
	QString pgnFile;
	QString name;
	quint32 seed = 0;         //!< 0: unknown
	bool fromSidecar = false;
	QStringList players;      //!< players found in the PGN
};

QString sidecarPath(const QString& pgnFile);

/*! Reads the tags of the games in \a pgnFile. */
PgnSummary scanPgn(const QString& pgnFile);

/*!
 * Writes the settings sidecar for \a pgnFile from the current
 * application settings.
 */
bool saveSidecar(const QString& pgnFile, const QString& name, quint32 seed);

/*!
 * Restores the settings of the tournament stored in \a pgnFile into the
 * application settings (so that the New Tournament dialog shows them),
 * from the sidecar if there is one, otherwise from the PGN's own tags.
 * Returns false if the PGN can't be used.
 */
bool restore(const QString& pgnFile, Info* info, QString* error);

}

#endif // TOURNAMENTRESUME_H
