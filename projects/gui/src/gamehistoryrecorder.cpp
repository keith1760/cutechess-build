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

#include "gamehistoryrecorder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTextStream>
#include <cmath>
#include <limits>

#include <chessgame.h>
#include <pgngame.h>
#include <board/side.h>
#include <board/result.h>

namespace {

const char* EngineFileName = "cutechess_engine_matches.pgn";
const char* HumanFileName = "cutechess_human_matches.pgn";
const char* EnabledSettingsKey = "games/save_match_history";
const char* FilePathSettingsKey = "games/match_history_file";
// Every PGN game record starts with its mandatory "Event" tag, so
// that's used as the marker for where one game's text ends and the
// next one's begins in the concatenated history file.
const char* GameMarker = "[Event ";

} // anonymous namespace

bool GameHistoryRecorder::isEnabled()
{
	// On by default, so the facility works out of the box unless the
	// user turns it off from the New Tournament dialog.
	return QSettings().value(EnabledSettingsKey, true).toBool();
}

void GameHistoryRecorder::setEnabled(bool enabled)
{
	QSettings().setValue(EnabledSettingsKey, enabled);
}

QString GameHistoryRecorder::filePath()
{
	return QSettings().value(FilePathSettingsKey, QString()).toString();
}

void GameHistoryRecorder::setFilePath(const QString& path)
{
	QSettings().setValue(FilePathSettingsKey, path);
}

QString GameHistoryRecorder::historyFilePath(const QString& fileName)
{
	// When running from an AppImage, the AppImage runtime sets
	// $APPIMAGE to the full path of the .AppImage file before
	// handing control to AppRun, regardless of what AppRun itself
	// does. The AppImage's own contents are a read-only, mounted
	// squashfs at runtime, so history has to be written next to the
	// .AppImage file on the real filesystem, not inside it.
	//
	// Outside of an AppImage (e.g. a plain build during development,
	// or on platforms that don't use AppImage), fall back to the
	// folder the executable was launched from.
	QString appImagePath = QString::fromLocal8Bit(qgetenv("APPIMAGE"));
	QString folder = appImagePath.isEmpty()
			  ? QCoreApplication::applicationDirPath()
			  : QFileInfo(appImagePath).absolutePath();

	return QDir(folder).absoluteFilePath(fileName);
}

QStringList GameHistoryRecorder::splitPgnGames(const QString& content)
{
	QStringList games;

	int start = content.indexOf(QLatin1String(GameMarker));
	while (start != -1)
	{
		// Consecutive games written by PgnGame::write() are
		// separated by a blank line, so the next record begins
		// at the first "\n\n[Event " after this one.
		int next = content.indexOf(
			QLatin1String("\n\n") + QLatin1String(GameMarker), start);
		int end = (next == -1) ? content.size() : next;

		QString game = content.mid(start, end - start).trimmed();
		if (!game.isEmpty())
			games << game;

		if (next == -1)
			break;
		start = next + 2;
	}

	return games;
}

void GameHistoryRecorder::recordEngineGame(ChessGame* game,
					    const QString& tournamentName,
					    double eloDiff)
{
	record(game, QString::fromLatin1(EngineFileName), tournamentName, eloDiff);
}

void GameHistoryRecorder::recordHumanGame(ChessGame* game,
					   const QString& tournamentName)
{
	record(game, QString::fromLatin1(HumanFileName), tournamentName,
	       std::numeric_limits<double>::quiet_NaN());
}

void GameHistoryRecorder::record(ChessGame* game,
				  const QString& fileName,
				  const QString& tournamentName,
				  double eloDiff)
{
	if (!isEnabled() || game == nullptr)
		return;

	const PgnGame* sourcePgn = game->pgn();
	if (sourcePgn == nullptr || sourcePgn->isNull())
		return;
	// Skip games that never actually got underway.
	if (sourcePgn->moves().isEmpty() && sourcePgn->result().isNone())
		return;

	// Work on a copy so this doesn't alter the tags of the game
	// object itself, which the caller may still be using (e.g. to
	// write the default PGN output file) once this call returns.
	PgnGame pgn(*sourcePgn);

	// Preserve the tournament name even when it isn't already
	// reflected by the standard "Event" tag, and record the running
	// Elo difference when one is available. Both are non-standard
	// tags; compliant PGN readers ignore tags they don't recognize.
	if (!tournamentName.isEmpty() && tournamentName != pgn.event())
		pgn.setTag(QStringLiteral("Tournament"), tournamentName);
	if (!std::isnan(eloDiff))
	{
		pgn.setTag(QStringLiteral("EloDiff"),
			   QString::number(eloDiff, 'f', 2));
	}

	QString path = filePath();
	if (path.isEmpty())
		path = historyFilePath(fileName);

	QString content;
	QFile file(path);
	if (file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		QTextStream in(&file);
		content = in.readAll();
		file.close();
	}

	QStringList games = splitPgnGames(content);
	games << QLatin1String(""); // placeholder, replaced with real PGN text below

	// Keep only the most recent MaxGamesPerFile games, oldest first,
	// so the file reads chronologically and stays a valid multi-game
	// PGN file that other tools (or Cute Chess's own PGN database)
	// can open directly.
	while (games.size() > MaxGamesPerFile)
		games.removeFirst();
	games.removeLast(); // drop the placeholder again

	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
		return; //TODO: reaction on I/O error. This mirrors the existing
			// default-PGN-output code in MainWindow::onGameFinished(),
			// which likewise doesn't surface a write failure to the user.

	QTextStream out(&file);
	for (const QString& g : qAsConst(games))
		out << g << "\n\n";
	pgn.write(out, PgnGame::Verbose);
	file.close();
}
