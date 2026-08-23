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

#ifndef CUTE_CHESS_APPLICATION_H
#define CUTE_CHESS_APPLICATION_H

#include <QApplication>
#include <QPointer>
#include <QColor>

class EngineManager;
class GameManager;
class MainWindow;
class SettingsDialog;
class TournamentResultsDialog;
class GameDatabaseManager;
class GameDatabaseDialog;
class PgnImporter;
class ChessGame;
class GameWall;

class CuteChessApplication : public QApplication
{
	Q_OBJECT

	public:
		CuteChessApplication(int& argc, char* argv[]);
		virtual ~CuteChessApplication();

		QString configPath();
		EngineManager* engineManager();
		GameManager* gameManager();
		GameDatabaseManager* gameDatabaseManager();
		QList<MainWindow*> gameWindows();
		void showGameWindow(int index);
		TournamentResultsDialog* tournamentResultsDialog();

		/*!
		 * Records \a geometry and \a windowState as the most recently
		 * known main window layout.
		 *
		 * This does NOT write anything to disk. The values are only
		 * kept in memory, ready to be written out by onAboutToQuit()
		 * at the very last possible moment before the application
		 * exits -- see the comment there for why that matters.
		 */
		void recordMainWindowGeometry(const QByteArray& geometry,
					       const QByteArray& windowState);

		static CuteChessApplication* instance();
		static QString userName();

		/*!
		 * Returns the colour used for the board's background /
		 * "wall" squares and the mid-cream application palette.
		 *
		 * Defaults to #EFE6C8, but can be overridden by the user
		 * via the RGB sliders in the Settings dialog (General
		 * tab). The chosen colour is persisted in QSettings under
		 * "ui/board_bg_color_r/g/b".
		 */
		QColor boardBackgroundColor() const;

		/*!
		 * Returns the colour used for the light squares on the
		 * main chessboard display.
		 *
		 * Defaults to #FFCE9E, but can be overridden by the user
		 * via the colour pickers in the Settings dialog (General
		 * tab). The chosen colour is persisted in QSettings under
		 * "ui/board_light_square_color_r/g/b".
		 */
		QColor lightSquareColor() const;
		/*!
		 * Returns the colour used for the dark squares on the
		 * main chessboard display.
		 *
		 * Defaults to #D18B47, but can be overridden by the user
		 * via the colour pickers in the Settings dialog (General
		 * tab). The chosen colour is persisted in QSettings under
		 * "ui/board_dark_square_color_r/g/b".
		 */
		QColor darkSquareColor() const;

	public slots:
		/*!
		 * Sets the board/application background colour to \a color,
		 * persists it, re-applies the palette, and notifies any
		 * listeners (board views, board scenes) via
		 * boardBackgroundColorChanged() so they can repaint
		 * immediately without restarting the application.
		 */
		void setBoardBackgroundColor(const QColor& color);
		/*!
		 * Sets the light-square colour to \a color, persists it,
		 * and notifies any listeners via lightSquareColorChanged()
		 * so open boards recolor immediately without restarting.
		 */
		void setLightSquareColor(const QColor& color);
		/*!
		 * Sets the dark-square colour to \a color, persists it,
		 * and notifies any listeners via darkSquareColorChanged()
		 * so open boards recolor immediately without restarting.
		 */
		void setDarkSquareColor(const QColor& color);
		MainWindow* newGameWindow(ChessGame* game);
		void newDefaultGame();
		void showSettingsDialog();
		void showTournamentResultsDialog();
		void showGameDatabaseDialog();
		void showGameWall();
		void closeDialogs();
		void onQuitAction();

	signals:
		/*!
		 * Emitted after setBoardBackgroundColor() changes the
		 * board/application background colour, so already-open
		 * board views and scenes can update without a restart.
		 */
		void boardBackgroundColorChanged(const QColor& color);
		/*!
		 * Emitted after setLightSquareColor() changes the light
		 * square colour, so already-open boards can update without
		 * a restart.
		 */
		void lightSquareColorChanged(const QColor& color);
		/*!
		 * Emitted after setDarkSquareColor() changes the dark
		 * square colour, so already-open boards can update without
		 * a restart.
		 */
		void darkSquareColorChanged(const QColor& color);

	private:
		void showDialog(QWidget* dlg);
		void applyCustomAppearance();

		SettingsDialog* m_settingsDialog;
		TournamentResultsDialog* m_tournamentResultsDialog;
		EngineManager* m_engineManager;
		GameManager* m_gameManager;
		GameDatabaseManager* m_gameDatabaseManager;
		QList<QPointer<MainWindow> > m_gameWindows;
		GameDatabaseDialog* m_gameDatabaseDialog;
		QPointer<GameWall> m_gameWall;
		bool m_initialWindowCreated;

		// Last known main window geometry/state, staged by
		// recordMainWindowGeometry() and flushed to disk by
		// onAboutToQuit(). See recordMainWindowGeometry() for why the
		// disk write itself is deferred to application shutdown.
		QByteArray m_pendingMainWindowGeometry;
		QByteArray m_pendingMainWindowState;
		bool m_hasPendingMainWindowGeometry;

	private slots:
		void onLastWindowClosed();
		void onAboutToQuit();
};

#endif // CUTE_CHESS_APPLICATION_H
