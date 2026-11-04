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

#include "swisstournament.h"
#include <QStringList>
#include <algorithm>

namespace {

// Upper limit for the backtracking search of one pairing attempt. The
// search is exhaustive only when pairings are very constrained (late
// rounds with many players); the limit makes sure it always ends.
const int MaxSearchNodes = 200000;

inline QPair<int, int> key(int a, int b)
{
	return a < b ? qMakePair(a, b) : qMakePair(b, a);
}

struct Search
{
	const QVector<int>& points;
	const QMap< QPair<int, int>, int >& played;
	bool allowRematch;
	int nodes;
	QList< QPair<int, int> > out;

	bool rematch(int a, int b) const
	{
		return played.value(key(a, b)) > 0;
	}

	// Candidate opponents of the first player of 'rem' (which is
	// sorted by rank), best first. Within the player's own score
	// group the order is the Dutch one: the player meets the first
	// player of the lower half of the group, then the next ones, and
	// only then the players of the upper half. After the group come
	// the lower score groups, nearest first.
	QVector<int> candidates(const QVector<int>& rem) const
	{
		const int p = rem.first();
		int g = 1;
		while (g < rem.size() && points[rem[g]] == points[p])
			g++;

		QVector<int> order;
		const int h = g / 2;
		for (int i = qMax(h, 1); i < g; i++)
			order << rem[i];
		for (int i = h - 1; i >= 1; i--)
			order << rem[i];
		for (int i = g; i < rem.size(); i++)
			order << rem[i];

		QVector<int> fresh;
		QVector<int> repeat;
		for (int c : std::as_const(order))
		{
			if (rematch(p, c))
			{
				if (allowRematch)
					repeat << c;
			}
			else
				fresh << c;
		}
		return fresh + repeat;
	}

	bool run(const QVector<int>& rem)
	{
		if (rem.isEmpty())
			return true;
		if (++nodes > MaxSearchNodes)
			return false;

		const int p = rem.first();
		const QVector<int> cand = candidates(rem);
		for (int c : cand)
		{
			QVector<int> next;
			next.reserve(rem.size() - 2);
			for (int i = 1; i < rem.size(); i++)
			{
				if (rem[i] != c)
					next << rem[i];
			}

			out.append(qMakePair(p, c));
			if (run(next))
				return true;
			out.removeLast();
			if (nodes > MaxSearchNodes)
				return false;
		}
		return false;
	}
};

// Tries to pair all players of 'rem'. Returns false if it can't be done.
bool pairAll(const QVector<int>& rem,
	     const QVector<int>& points,
	     const QMap< QPair<int, int>, int >& played,
	     bool allowRematch,
	     QList< QPair<int, int> >& pairs)
{
	Search s{ points, played, allowRematch, 0, {} };
	if (!s.run(rem))
		return false;
	pairs = s.out;
	return true;
}

} // anonymous namespace

SwissTournament::SwissTournament(GameManager* gameManager,
				 QObject *parent)
	: Tournament(gameManager, parent),
	  m_encounterIndex(0)
{
}

QString SwissTournament::type() const
{
	return "swiss";
}

int SwissTournament::byePoints(int player) const
{
	return m_byePoints.value(player, 0);
}

QList< QPair<int, int> > SwissTournament::computePairings(
	const QVector<int>& points,
	const QMap< QPair<int, int>, int >& played,
	const QVector<bool>& hadBye,
	int* bye)
{
	const int n = points.size();
	if (bye)
		*bye = -1;

	// Rank: more points first, then the lower seed number
	QVector<int> order;
	for (int i = 0; i < n; i++)
		order << i;
	std::stable_sort(order.begin(), order.end(), [&](int a, int b)
	{
		return points[a] > points[b];
	});

	QList< QPair<int, int> > pairs;

	// With an odd number of players: choose who sits out, trying the
	// lowest ranked players who haven't had a bye first.
	QVector<int> byeOrder;
	if (n % 2 != 0)
	{
		for (int i = n - 1; i >= 0; i--)
		{
			if (!hadBye.value(order[i]))
				byeOrder << order[i];
		}
		for (int i = n - 1; i >= 0; i--)
		{
			if (hadBye.value(order[i]))
				byeOrder << order[i];
		}
	}
	else
		byeOrder << -1;

	auto without = [&](int player)
	{
		QVector<int> rem;
		for (int p : std::as_const(order))
		{
			if (p != player)
				rem << p;
		}
		return rem;
	};

	// First choice: nobody meets twice. Then allow rematches, but
	// only where there's no other way.
	for (int allowRematch = 0; allowRematch < 2; allowRematch++)
	{
		for (int b : std::as_const(byeOrder))
		{
			if (pairAll(without(b), points, played,
				    allowRematch, pairs))
			{
				if (bye)
					*bye = b;
				return pairs;
			}
		}
	}

	// Last resort (the searches above ran out of time): pair in rank
	// order.
	const int b = byeOrder.first();
	const QVector<int> rem = without(b);
	pairs.clear();
	for (int i = 0; i + 1 < rem.size(); i += 2)
		pairs.append(qMakePair(rem[i], rem[i + 1]));
	if (bye)
		*bye = b;
	return pairs;
}

void SwissTournament::initializePairing()
{
	const int n = playerCount();

	m_encounters.clear();
	m_encounterIndex = 0;
	m_played.clear();
	m_hadBye = QVector<bool>(n, false);
	m_byePoints = QVector<int>(n, 0);
	m_colorBalance = QVector<int>(n, 0);
	m_lastColor = QVector<int>(n, 0);

	setCurrentRound(1);
	generateRound();
}

int SwissTournament::gamesPerCycle() const
{
	// A cycle is one Swiss round; every player but a possible bye plays
	return playerCount() / 2;
}

void SwissTournament::generateRound()
{
	const int n = playerCount();

	QVector<int> points(n);
	for (int i = 0; i < n; i++)
		points[i] = playerAt(i).score() + m_byePoints[i];

	int bye = -1;
	const auto pairs = computePairings(points, m_played, m_hadBye, &bye);

	m_encounters.clear();
	m_encounterIndex = 0;

	QStringList log;
	int board = 0;
	for (const auto& pr : pairs)
	{
		const int a = pr.first;
		const int b = pr.second;

		// The player with fewer whites so far gets white. If equal,
		// the one who had black last time. If still equal, colours
		// alternate by board and round.
		int white;
		if (m_colorBalance[a] != m_colorBalance[b])
			white = m_colorBalance[a] < m_colorBalance[b] ? a : b;
		else if (m_lastColor[a] != m_lastColor[b])
			white = m_lastColor[a] < m_lastColor[b] ? a : b;
		else
			white = ((board + currentRound()) % 2 == 1) ? a : b;
		const int black = (white == a) ? b : a;
		board++;

		m_encounters.append(Encounter{ white, black, 0 });
		m_played[qMakePair(qMin(a, b), qMax(a, b))]++;

		// Colour balance only means something when a pairing has an
		// odd number of games; otherwise both sides get equal colours
		if (gamesPerEncounter() % 2 != 0)
		{
			m_colorBalance[white]++;
			m_colorBalance[black]--;
			m_lastColor[white] = 1;
			m_lastColor[black] = -1;
		}

		log << QString("%1-%2").arg(playerAt(white).name(),
					    playerAt(black).name());
	}

	if (bye != -1)
	{
		m_hadBye[bye] = true;
		m_byePoints[bye] += 2 * gamesPerEncounter();
		log << QString("(bye: %1)").arg(playerAt(bye).name());
	}

	qInfo("Swiss round %d pairings: %s", currentRound(),
	      qUtf8Printable(log.join(", ")));
}

TournamentPair* SwissTournament::nextPair(int gameNumber)
{
	Q_UNUSED(gameNumber);

	if (m_encounterIndex >= m_encounters.size())
	{
		// The round is over only when its last game has finished,
		// because the next pairings depend on the results.
		if (gamesInProgress() > 0)
			return nullptr;
		if (currentRound() >= roundMultiplier())
			return nullptr;

		setCurrentRound(currentRound() + 1);
		generateRound();
		if (m_encounters.isEmpty())
			return nullptr;
	}

	Encounter& e = m_encounters[m_encounterIndex];
	TournamentPair* pr = pair(e.white, e.black);

	// A pair object can be reused from an earlier round (a rematch),
	// so make sure the planned player has white in the first game.
	if (e.started == 0 && pr->firstPlayer() != e.white)
		pr->swapPlayers();

	if (++e.started >= gamesPerEncounter())
		m_encounterIndex++;

	return pr;
}
