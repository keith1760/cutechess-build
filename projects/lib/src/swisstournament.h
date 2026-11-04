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

#ifndef SWISSTOURNAMENT_H
#define SWISSTOURNAMENT_H

#include "tournament.h"
#include <QList>
#include <QMap>
#include <QPair>
#include <QVector>

/*!
 * \brief Swiss type chess tournament.
 *
 * The tournament is played in a fixed number of rounds (the round
 * multiplier, ie. -rounds N on the command line). In the first round
 * the players are paired by seed (top half against bottom half, in the
 * order the players were added). In every later round the players are
 * sorted by their score and paired against opponents with a similar
 * score, so that nobody meets the same opponent twice unless that
 * can't be avoided. A round starts only when every game of the
 * previous round has finished.
 *
 * Each pairing is an encounter of gamesPerEncounter() games. With an
 * odd number of players one player sits out each round (a "bye"), the
 * lowest ranked player who hasn't had one yet. For pairing purposes
 * a bye counts as a win; the bye is not a game, so it isn't in the
 * games, points or crosstable figures.
 */
class LIB_EXPORT SwissTournament : public Tournament
{
	Q_OBJECT

	public:
		/*! Creates a new Swiss tournament. */
		explicit SwissTournament(GameManager* gameManager,
					 QObject *parent = nullptr);

		// Inherited from Tournament
		virtual QString type() const;
		virtual int byePoints(int player) const;

		/*!
		 * Computes one round of pairings.
		 *
		 * \a points are the players' scores in half points (including
		 * byes), \a played tells how many times each unordered pair of
		 * players has already met, \a hadBye tells who has had a bye.
		 * Returns the pairs, higher ranked player first (colours are
		 * not decided here), and sets \a bye to the player who sits
		 * out, or -1 if there's none.
		 *
		 * This is a pure function and is public so that it can be
		 * unit tested.
		 */
		static QList< QPair<int, int> > computePairings(
			const QVector<int>& points,
			const QMap< QPair<int, int>, int >& played,
			const QVector<bool>& hadBye,
			int* bye);

	protected:
		// Inherited from Tournament
		virtual void initializePairing();
		virtual int gamesPerCycle() const;
		virtual TournamentPair* nextPair(int gameNumber);

	private:
		struct Encounter
		{
			int white;
			int black;
			int started;
		};

		void generateRound();

		QList<Encounter> m_encounters;
		int m_encounterIndex;
		QMap< QPair<int, int>, int > m_played;
		QVector<bool> m_hadBye;
		QVector<int> m_byePoints;
		QVector<int> m_colorBalance;
		QVector<int> m_lastColor;
};

#endif // SWISSTOURNAMENT_H
