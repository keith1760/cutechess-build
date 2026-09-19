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

#include "openingbook.h"
#include <QString>
#include <QFile>
#include <QDataStream>
#include <QtDebug>
#include <QVector>
#include <QtGlobal>
#include "pgngame.h"
#include "pgnstream.h"
#include "mersenne.h"


QDataStream& operator>>(QDataStream& in, OpeningBook* book)
{
	while (in.status() == QDataStream::Ok)
	{
		quint64 key;
		OpeningBook::Entry entry = book->readEntry(in, &key);
		book->addEntry(entry, key);
	}

	return in;
}

QDataStream& operator<<(QDataStream& out, const OpeningBook* book)
{
	OpeningBook::Map::const_iterator it;
	for (it = book->m_map.constBegin(); it != book->m_map.constEnd(); ++it)
		book->writeEntry(it, out);

	return out;
}

OpeningBook::OpeningBook(AccessMode mode)
	: m_mode(mode),
	  m_randomness(0)
{
}

OpeningBook::~OpeningBook()
{
}

bool OpeningBook::read(const QString& filename)
{
	m_filename = filename;
	QFile file(filename);
	if (!file.open(QIODevice::ReadOnly))
		return false;

	if ((file.size() % entrySize()) != 0)
	{
		qWarning("Invalid size for opening book %s",
			 qUtf8Printable(filename));
		return false;
	}

	if (m_mode == Disk)
		return true;

	m_map.clear();
	QDataStream in(&file);
	in >> this;

	return !m_map.isEmpty();
}

bool OpeningBook::write(const QString& filename) const
{
	QFile file(filename);
	if (!file.open(QIODevice::WriteOnly))
		return false;

	QDataStream out(&file);
	out << this;

	return true;
}

void OpeningBook::addEntry(const Entry& entry, quint64 key)
{
	Map::iterator it = m_map.find(key);
	while (it != m_map.end() && it.key() == key)
	{
		Entry& tmp = it.value();
		if (tmp.move == entry.move)
		{
			tmp.weight += entry.weight;
			return;
		}
		++it;
	}
	
	m_map.insert(key, entry);
}

int OpeningBook::import(const PgnGame& pgn, int maxMoves)
{
	Q_ASSERT(maxMoves > 0);

	Chess::Side winner(pgn.result().winner());
	int loserMod = -1;
	quint16 weight = 1;
	maxMoves = qMin(maxMoves, pgn.moves().size());
	int ret = maxMoves;

	if (!winner.isNull())
	{
		loserMod = int(pgn.startingSide() == winner);
		weight = 2;
		ret = (ret - loserMod) / 2 + loserMod;
	}

	const QVector<PgnGame::MoveData>& moves = pgn.moves();
	for (int i = 0; i < maxMoves; i++)
	{
		// Skip the loser's moves
		if ((i % 2) != loserMod)
		{
			Entry entry = { moves.at(i).move, weight };
			addEntry(entry, moves.at(i).key);
		}
	}

	return ret;
}

int OpeningBook::import(PgnStream& in, int maxMoves)
{
	Q_ASSERT(maxMoves > 0);

	if (!in.isOpen())
		return 0;

	int moveCount = 0;
	while (in.status() == PgnStream::Ok)
	{
		PgnGame game;
		game.read(in, maxMoves);
		if (game.moves().isEmpty())
			break;

		moveCount += import(game, maxMoves);
	}

	return moveCount;
}

QList<OpeningBook::Entry> OpeningBook::entriesFromDisk(quint64 key) const
{
	QList<Entry> entries;
	QFile file(m_filename);
	if (!file.open(QIODevice::ReadOnly))
	{
		qWarning("Could not open book file %s",
			 qUtf8Printable(m_filename));
		return entries;
	}
	QDataStream in(&file);

	quint64 entryKey = 0;
	qint64 step = entrySize();
	qint64 n = file.size() / step;
	qint64 first = 0;
	qint64 last = n - 1;
	qint64 middle = (first + last) / 2;

	// Binary search
	while (first <= last)
	{
		qint64 pos = middle * step;
		file.seek(pos);
		Entry entry = readEntry(in, &entryKey);
		if (entryKey < key)
			first = middle + 1;
		else if (entryKey == key)
		{
			entries << entry;
			for (qint64 i = pos - step; i >= 0; i -= step)
			{
				file.seek(i);
				entry = readEntry(in, &entryKey);
				if (entryKey != key)
					break;
				entries << entry;
			}
			qint64 maxPos = (n - 1) * step;
			for (qint64 i = pos + step; i <= maxPos; i += step)
			{
				file.seek(i);
				entry = readEntry(in, &entryKey);
				if (entryKey != key)
					break;
				entries << entry;
			}
			return entries;
		}
		else
			last = middle - 1;
		middle = (first + last) / 2;
	}

	return entries;
}

QList<OpeningBook::Entry> OpeningBook::entries(quint64 key) const
{
	if (m_mode == Ram)
		return m_map.values(key);
	return entriesFromDisk(key);
}

int OpeningBook::randomness() const
{
	return m_randomness;
}

void OpeningBook::setRandomness(int percent)
{
	m_randomness = qBound(0, percent, 100);
}

Chess::GenericMove OpeningBook::move(quint64 key) const
{
	Chess::GenericMove move;
	
	// There can be multiple entries/moves with the same key.
	// We need to find them all to choose the best one
	const auto entries = this->entries(key);
	if (entries.isEmpty())
		return move;

	// Calculate the total weight of all available moves
	int totalWeight = 0;
	for (const Entry& entry : entries)
		totalWeight += entry.weight;
	if (totalWeight <= 0)
		return move;

	// randomness() is a percentage (0-100) that blends each move's
	// popularity-based selection probability with an equal share
	// among all matching moves. At 0% (the default), this reduces
	// to the original behavior: pure popularity-weighted selection.
	// At 100%, popularity/weight has no influence at all, and every
	// matching move is equally likely to be picked.
	int n = entries.size();
	double r = qBound(0, m_randomness, 100) / 100.0;

	if (r <= 0.0)
	{
		// Fast path: identical to the original, purely
		// weighted selection.
		int pick = Mersenne::random() % totalWeight;
		int currentWeight = 0;
		for (const Entry& entry : entries)
		{
			currentWeight += entry.weight;
			if (currentWeight > pick)
				return entry.move;
		}
		return move;
	}

	// Blend each move's popularity share with a uniform share.
	QVector<double> effectiveWeights;
	effectiveWeights.reserve(n);
	double totalEffective = 0.0;
	double uniformShare = 1.0 / double(n);
	for (const Entry& entry : entries)
	{
		double popularityShare = double(entry.weight) / double(totalWeight);
		double blended = (1.0 - r) * popularityShare + r * uniformShare;
		effectiveWeights.append(blended);
		totalEffective += blended;
	}
	if (totalEffective <= 0.0)
		return move;

	// Pick a move using the blended weights. Mersenne::random() is
	// used as the source of randomness for consistency with the
	// rest of the engine, scaled down to a double in [0, 1).
	double pick = (double(Mersenne::random()) / 4294967296.0) * totalEffective;
	double currentWeight = 0.0;
	for (int i = 0; i < n; i++)
	{
		currentWeight += effectiveWeights.at(i);
		if (currentWeight > pick)
			return entries.at(i).move;
	}

	// Guard against floating-point rounding leaving a tiny
	// remainder unassigned; fall back to the last move.
	return entries.last().move;
}
