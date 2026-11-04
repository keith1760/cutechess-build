/*
    This file is part of Cute Chess.

    Cute Chess is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include "tournamentresume.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>

namespace TournamentResume {

static const char* const kPrefixes[] = { "tournament/", "games/", "pgn/" };
static const char* const kNameKey = "resume/name";
static const char* const kSeedKey = "resume/seed";

QString sidecarPath(const QString& pgnFile)
{
	return pgnFile + QStringLiteral(".cutechess.ini");
}

static QString unescapeTag(QString v)
{
	v.replace(QStringLiteral("\\\""), QStringLiteral("\""));
	v.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
	return v;
}

PgnSummary scanPgn(const QString& pgnFile)
{
	PgnSummary s;
	QFile f(pgnFile);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return s;
	s.readable = true;

	static const QRegularExpression tagRe(
		QStringLiteral("^\\[(\\w+)\\s+\"((?:[^\"\\\\]|\\\\.)*)\"\\]"));

	bool inGame = false;
	QString white, black, result, event, site, variant;
	bool first = true;

	auto endGame = [&]()
	{
		if (!inGame)
			return;
		inGame = false;
		if (first)
		{
			s.event = event;
			s.site = site;
			s.variant = variant;
			first = false;
		}
		for (const QString& p : { white, black })
			if (!p.isEmpty() && !s.players.contains(p))
				s.players << p;
		if (result.isEmpty() || result == QLatin1String("*"))
			s.unfinishedTail = true;
		else
		{
			s.unfinishedTail = false;
			s.games++;
		}
		white.clear(); black.clear(); result.clear();
		event.clear(); site.clear(); variant.clear();
	};

	bool sawMoves = false;
	while (!f.atEnd())
	{
		const QString line = QString::fromUtf8(f.readLine()).trimmed();
		const auto m = tagRe.match(line);
		if (m.hasMatch())
		{
			// A tag after movetext starts the next game
			if (sawMoves)
			{
				endGame();
				sawMoves = false;
			}
			inGame = true;
			const QString k = m.captured(1);
			const QString v = unescapeTag(m.captured(2));
			if (k == QLatin1String("White")) white = v;
			else if (k == QLatin1String("Black")) black = v;
			else if (k == QLatin1String("Result")) result = v;
			else if (k == QLatin1String("Event")) event = v;
			else if (k == QLatin1String("Site")) site = v;
			else if (k == QLatin1String("Variant")) variant = v;
		}
		else if (inGame && !line.isEmpty())
			sawMoves = true;
	}
	endGame();
	return s;
}

bool saveSidecar(const QString& pgnFile, const QString& name, quint32 seed)
{
	if (pgnFile.isEmpty())
		return false;

	QSettings current;
	QSettings snapshot(sidecarPath(pgnFile), QSettings::IniFormat);
	snapshot.clear();
	for (const QString& key : current.allKeys())
	{
		for (const char* prefix : kPrefixes)
		{
			if (key.startsWith(QLatin1String(prefix)))
			{
				snapshot.setValue(key, current.value(key));
				break;
			}
		}
	}
	snapshot.setValue(kNameKey, name);
	snapshot.setValue(kSeedKey, seed);
	snapshot.sync();
	return snapshot.status() == QSettings::NoError;
}

bool restore(const QString& pgnFile, Info* info, QString* error)
{
	Q_ASSERT(info != nullptr);
	*info = Info();
	info->pgnFile = pgnFile;

	const PgnSummary sum = scanPgn(pgnFile);
	if (!sum.readable)
	{
		if (error)
			*error = QObject::tr("The file could not be read.");
		return false;
	}
	if (sum.games == 0)
	{
		if (error)
			*error = QObject::tr("The file contains no finished games, "
					     "so there is nothing to resume.");
		return false;
	}
	info->players = sum.players;
	info->name = sum.event;

	QSettings current;
	const QString sidecar = sidecarPath(pgnFile);
	if (QFileInfo::exists(sidecar))
	{
		QSettings saved(sidecar, QSettings::IniFormat);
		const QStringList keys = saved.allKeys();
		bool any = false;
		for (const QString& key : keys)
		{
			for (const char* prefix : kPrefixes)
				if (key.startsWith(QLatin1String(prefix)))
					any = true;
		}
		if (saved.status() == QSettings::NoError && any)
		{
			// Per-engine time controls of the current setup must not
			// leak into the restored one
			current.remove(QStringLiteral("tournament/last_engine_tc"));
			for (const QString& key : keys)
			{
				if (key == QLatin1String(kNameKey))
				{
					info->name = saved.value(key).toString();
					continue;
				}
				if (key == QLatin1String(kSeedKey))
				{
					info->seed = saved.value(key).toUInt();
					continue;
				}
				current.setValue(key, saved.value(key));
			}
			current.sync();
			info->fromSidecar = true;
			return true;
		}
	}

	// No usable sidecar: rebuild what the PGN itself tells us
	current.remove(QStringLiteral("tournament/last_engines"));
	current.remove(QStringLiteral("tournament/last_engine_tc"));
	current.setValue(QStringLiteral("tournament/last_engine_names"), sum.players);
	if (!sum.site.isEmpty() && sum.site != QLatin1String("?"))
		current.setValue(QStringLiteral("pgn/site"), sum.site);
	if (!sum.variant.isEmpty())
		current.setValue(QStringLiteral("games/variant"), sum.variant.toLower());
	current.sync();
	return true;
}

} // namespace TournamentResume
