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
#include <QSocketNotifier>

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
		 * Async-signal-safe handler registered for SIGTERM,
		 * SIGINT and SIGHUP (see installSignalHandlers()).
		 *
		 * A plain signal handler cannot safely call into Qt or
		 * do anything beyond a very small set of async-signal-safe
		 * operations, so this only writes a single byte to one
		 * end of a self-pipe (a connected socket pair). That
		 * write wakes up the Qt event loop via the QSocketNotifier
		 * set up on the other end (see handleUnixSignal()), where
		 * the actual quit logic can then run safely as normal,
		 * queued Qt code.
		 */
		static void unixSignalHandler(int signalNumber);

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

		/*!
		 * Returns true if the file (a-h) and rank (1-8) coordinate
		 * labels around the edge of the board are shown; otherwise
		 * returns false.
		 *
		 * Defaults to true, but can be turned off by the user via
		 * the Settings dialog (General tab), which also enlarges
		 * the board (and the pieces on it) to fill the space the
		 * labels used to occupy. The choice is persisted in
		 * QSettings under "ui/show_board_coordinates".
		 */
		bool showBoardCoordinates() const;

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
		/*!
		 * Sets whether the board's coordinate labels are shown to
		 * \a show, persists it, and notifies any listeners via
		 * showBoardCoordinatesChanged() so open boards resize
		 * immediately without restarting.
		 */
		void setShowBoardCoordinates(bool show);
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
		/*!
		 * Emitted after setShowBoardCoordinates() changes whether
		 * the board's coordinate labels are shown, so already-open
		 * boards can resize without a restart.
		 */
		void showBoardCoordinatesChanged(bool show);

	private:
		void showDialog(QWidget* dlg);
		void applyCustomAppearance();
#ifndef Q_OS_WIN32
		void installSignalHandlers();
#endif

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

#ifndef Q_OS_WIN32
		// The self-pipe (as a Unix domain socket pair) used to get
		// out of async-signal-handler context safely: sig[0] is
		// read from Qt's event loop (via m_signalNotifier), sig[1]
		// is written to (write(2) only -- async-signal-safe) from
		// unixSignalHandler().
		static int m_signalFd[2];
		QSocketNotifier* m_signalNotifier;
#endif

	private slots:
		void onLastWindowClosed();
		void onAboutToQuit();
#ifndef Q_OS_WIN32
		// Runs on the Qt event loop thread once unixSignalHandler()
		// wakes m_signalNotifier; reads (and discards) the byte(s)
		// from the pipe, then quits the same way the Quit menu
		// action does, so the normal closeEvent()/aboutToQuit()
		// chain -- and therefore the normal settings save -- runs.
		void handleUnixSignal();
#endif
};

#endif // CUTE_CHESS_APPLICATION_H
