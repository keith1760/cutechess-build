/*
    This file is part of Cute Chess.

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

#ifndef GAMEHISTORYRECORDER_H
#define GAMEHISTORYRECORDER_H

#include <QString>
#include <QStringList>

class ChessGame;

/*!
 * \brief Appends finished games to a rolling on-disk history file in
 * PGN (Portable Game Notation) format.
 *
 * By default, two multi-game PGN files (one game per record, standard
 * PGN tag roster plus movetext) are maintained next to the running
 * application -- next to the .AppImage itself when Cute Chess is run
 * as one, since an AppImage's own contents are read-only at runtime
 * and can't be written to:
 *
 *   - cutechess_engine_matches.pgn for engine-vs-engine games
 *   - cutechess_human_matches.pgn  for games that involved a human
 *
 * The New Tournament dialog's "Match history" field lets the user
 * pick a single PGN file instead; when one is set (see filePath()),
 * every recorded game -- engine-vs-engine or human -- is appended to
 * that one file rather than to the two default files above.
 *
 * Each file is trimmed to the most recent MaxGamesPerFile games,
 * dropping the oldest entries first. In addition to the tags already
 * present on the game (Event, Site, Date, White, Black, Result,
 * TimeControl, Opening/ECO, etc.), a non-standard "Tournament" tag
 * and, when available, an "EloDiff" tag are added so that context
 * which isn't otherwise part of the PGN standard is preserved; PGN
 * readers are required to ignore tags they don't recognize, so this
 * doesn't affect compatibility with other tools.
 *
 * Recording can be switched off from the New Tournament dialog; the
 * setting is stored under the "games/save_match_history" QSettings
 * key and defaults to on.
 */
class GameHistoryRecorder
{
	public:
		//! Maximum number of games kept in each history file.
		static const int MaxGamesPerFile = 500;

		/*!
		 * Records a finished engine-vs-engine game to the
		 * engine match history file.
		 *
		 * \a tournamentName is the name of the tournament the
		 * game belongs to. If empty, the game's PGN "Event"
		 * tag is used instead.
		 *
		 * \a eloDiff is the Elo difference between the two
		 * engines, calculated from the match score up to and
		 * including this game. Pass a NaN value (e.g.
		 * qQNaN()) if no meaningful difference is available,
		 * such as for tournaments with more than two engines.
		 */
		static void recordEngineGame(ChessGame* game,
					      const QString& tournamentName,
					      double eloDiff);

		/*!
		 * Records a finished game that involved at least one
		 * human player to the human match history file.
		 *
		 * \a tournamentName is the name of the tournament the
		 * game belongs to, or an empty string for a game
		 * started outside of a tournament.
		 */
		static void recordHumanGame(ChessGame* game,
					     const QString& tournamentName);

		//! Returns true if saving match history is enabled.
		static bool isEnabled();
		//! Enables or disables saving match history.
		static void setEnabled(bool enabled);

		/*!
		 * Returns the user-chosen PGN file that match history
		 * should be saved to, or an empty string if the
		 * default engine/human file pair should be used
		 * instead.
		 */
		static QString filePath();
		/*!
		 * Sets the PGN file that match history is saved to.
		 * Pass an empty string to go back to the default
		 * engine/human file pair.
		 */
		static void setFilePath(const QString& path);

	private:
		static void record(ChessGame* game,
				    const QString& fileName,
				    const QString& tournamentName,
				    double eloDiff);
		static QString historyFilePath(const QString& fileName);
		//! Splits the concatenated contents of a multi-game PGN
		//! file into one string per game, in file order.
		static QStringList splitPgnGames(const QString& content);
};

#endif // GAMEHISTORYRECORDER_H
