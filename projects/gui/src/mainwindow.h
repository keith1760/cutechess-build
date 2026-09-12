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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPointer>
#include <QList>
#include <QVariantMap>
#include <board/side.h>

namespace Chess {
	class Board;
	class Move;
}
class QMenu;
class QAction;
class QCloseEvent;
class QMoveEvent;
class QResizeEvent;
class QShowEvent;
class QTabBar;
class QDockWidget;
class GameViewer;
class MoveList;
class PlainTextLog;
class PgnGame;
class ChessGame;
class ChessPlayer;
class PgnTagsModel;
class Tournament;
class GameTabBar;
class EvalHistory;
class EvalWidget;
class QLabel;

/**
 * MainWindow
*/
class MainWindow : public QMainWindow
{
	Q_OBJECT

	public:
		explicit MainWindow(ChessGame* game);
		virtual ~MainWindow();
		QString windowListTitle() const;

		/*!
		 * Returns each View-menu dock widget's objectName() mapped to
		 * whether it's currently ticked/visible.
		 *
		 * Exposed publicly (rather than kept private, as it was
		 * before) so CuteChessApplication::backupViewMenuState() can
		 * snapshot it the instant the user gives the close/quit
		 * command -- see that method, and
		 * verifyViewMenuAgainstBackup() below, for how the snapshot
		 * is used again at the next startup.
		 */
		QVariantMap dockVisibilityMap() const;

	public slots:
		void addGame(ChessGame* game);

	protected:
		virtual void closeEvent(QCloseEvent* event);
		virtual void moveEvent(QMoveEvent* event);
		virtual void resizeEvent(QResizeEvent* event);
		virtual void showEvent(QShowEvent* event);
		void closeCurrentGame();

	private slots:
		void newGame();
		void newTournament();
		void onWindowMenuAboutToShow();
		void showGameWindow();
		void updateWindowTitle();
		void updateMenus();
		bool save();
		bool saveAs();
		void onTabChanged(int index);
		void onTabCloseRequested(int index);
		void closeTab(int index);
		void destroyGame(ChessGame* game);
		void onTournamentFinished();
		void onGameManagerFinished();
		void onGameStartFailed(ChessGame* game);
		void onGameFinished(ChessGame* game);
		void updateTournamentScore(ChessGame* game,
					   int gameNumber,
					   int whiteIndex,
					   int blackIndex);
		void onEngineGameFinished(ChessGame* game,
					   int gameNumber,
					   int whiteIndex,
					   int blackIndex);
		void editMoveComment(int ply, const QString& comment);
		void copyFen();
		void pasteFen();
		void copyPgn();
		void showAboutDialog();
		void closeAllGames();
		void adjudicateDraw();
		void adjudicateWhiteWin();
		void adjudicateBlackWin();
		void resignGame();
		void restoreSavedGeometry();
		void saveDockVisibilityImmediately(bool visible);

	private:
		struct TabData
		{
			explicit TabData(ChessGame* m_game,
					 Tournament* m_tournament = nullptr);

			ChessGame* m_id;
			QPointer<ChessGame> m_game;
			PgnGame* m_pgn;
			Tournament* m_tournament;
			QString m_titleSuffix;
			bool m_finished;
		};

		void createActions();
		void createMenus();
		void createToolBars();
		void createDockWindows();
		void createStatusBar();
		QString formatEloDiff(qreal diff) const;
		void applySavedGeometry();
		void stageGeometryForShutdown();

		/*!
		 * Compares the View menu's current ticked/visible dock state
		 * (as just applied by applySavedGeometry() above) against the
		 * backup snapshot taken by
		 * CuteChessApplication::backupViewMenuState() the last time
		 * the program was closed. If anything differs -- the normal
		 * restore silently reverted a dock (see the restoreState()
		 * fragility discussed in applySavedGeometry()), the settings
		 * file was partially written or corrupted, or the last
		 * session never reached a clean quit at all -- every dock is
		 * reset to the backed-up value instead.
		 *
		 * This is deliberately the LAST thing done as part of
		 * startup, called from restoreSavedGeometry() right after
		 * applySavedGeometry(), so it has the final say over the View
		 * menu's state and nothing later in startup can undo it.
		 */
		void verifyViewMenuAgainstBackup();
		QString genericTitle(const TabData& gameData) const;
		QString nameOnClock(const QString& name, Chess::Side side) const;
		void lockCurrentGame();
		void unlockCurrentGame();
		bool saveGame(const QString& fileName);
		bool askToSave();
		void setCurrentGame(const TabData& gameData);
		void removeGame(int index);
		int tabIndex(ChessGame* game) const;
		int tabIndex(Tournament* tournament, bool freeTab = false) const;
		void addDefaultWindowMenu();
		void adjudicateGame(Chess::Side winner);

		QMenu* m_gameMenu;
		QMenu* m_tournamentMenu;
		QMenu* m_toolsMenu;
		QMenu* m_viewMenu;
		QMenu* m_windowMenu;
		QMenu* m_helpMenu;

		GameTabBar* m_tabBar;

		GameViewer* m_gameViewer;
		MoveList* m_moveList;
		PgnTagsModel* m_tagsModel;

		QAction* m_quitGameAct;
		QAction* m_newGameAct;
		QAction* m_adjudicateBlackWinAct;
		QAction* m_adjudicateWhiteWinAct;
		QAction* m_adjudicateDrawAct;
		QAction* m_resignGameAct;
		QAction* m_closeGameAct;
		QAction* m_saveGameAct;
		QAction* m_saveGameAsAct;
		QAction* m_copyFenAct;
		QAction* m_pasteFenAct;
		QAction* m_copyPgnAct;
		QAction* m_flipBoardAct;
		QAction* m_newTournamentAct;
		QAction* m_stopTournamentAct;
		QAction* m_showTournamentResultsAct;
		QAction* m_minimizeAct;
		QAction* m_showGameDatabaseWindowAct;
		QAction* m_showGameWallAct;
		QAction* m_showPreviousTabAct;
		QAction* m_showNextTabAct;
		QAction* m_aboutAct;
		QAction* m_showSettingsAct;

		PlainTextLog* m_engineDebugLog;

		// Every dock widget whose toggleViewAction() lives in the View
		// menu (Moves, Tags, Engine Debug, Evaluation history, White's/
		// Black's evaluation). Kept so their ticked/visible state can be
		// saved and restored explicitly -- see dockVisibilityMap() and
		// applySavedGeometry() -- independently of the fragile combined
		// QMainWindow::saveState()/restoreState() blob.
		QList<QDockWidget*> m_dockWidgets;

		// Live "White wins - Draws - Black wins" + Elo diff readout for
		// two-player engine tournaments, refreshed whenever a
		// tournament game finishes. Not owned here -- it's the QLabel
		// GameViewer places between the two clocks (see
		// GameViewer::scoreLabel()), so it stays parented and cleaned
		// up there.
		QLabel* m_tournamentScoreLabel;

		EvalHistory* m_evalHistory;
		EvalWidget* m_evalWidgets[2];

		QPointer<ChessGame> m_game;
		QPointer<ChessPlayer> m_players[2];
		QList<TabData> m_tabs;

		QString m_currentFile;
		bool m_closing;
		bool m_readyToClose;

		bool m_firstTabAutoCloseEnabled;

		// Geometry/state persistence. Every open MainWindow shares the
		// same "ui/mainwindow" settings keys (there's one saved position,
		// not one per window), so only the window the user is actually
		// interacting with is allowed to stage it for saving -- see
		// moveEvent(), resizeEvent() and closeEvent() in mainwindow.cpp
		// for why. The actual write to disk happens later, in
		// CuteChessApplication::onAboutToQuit().
		bool m_settingsRestored;
		bool m_firstShowEventSeen;
};

#endif // MAINWINDOW_H
