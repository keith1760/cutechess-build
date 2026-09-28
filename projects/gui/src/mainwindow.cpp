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

#include "mainwindow.h"

#include <QAction>
#include <QHBoxLayout>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QToolButton>
#include <QDockWidget>
#include <QAbstractButton>
#include <QTreeView>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QClipboard>
#include <QWindow>
#include <QSettings>
#include <QSysInfo>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QLabel>
#include <QStatusBar>
#include <QFontMetrics>

#include <cmath>
#include <limits>

#include <board/boardfactory.h>
#include <chessgame.h>
#include "gamehistoryrecorder.h"
#include <timecontrol.h>
#include <enginemanager.h>
#include <gamemanager.h>
#include <playerbuilder.h>
#include <chessplayer.h>
#include <humanbuilder.h>
#include <tournament.h>
#include <tournamentplayer.h>
#include <elo.h>

#include "cutechessapp.h"
#include "gameviewer.h"
#include "movelist.h"
#include "newgamedlg.h"
#include "newtournamentdialog.h"
#include "chessclock.h"
#include "plaintextlog.h"
#include "pgntagsmodel.h"
#include "gametabbar.h"
#include "evalhistory.h"
#include "evalwidget.h"
#include "boardview/boardscene.h"
#include "tournamentresultsdlg.h"

#if 0
#include <modeltest.h>
#endif

MainWindow::TabData::TabData(ChessGame* game, Tournament* tournament)
	: m_id(game),
	  m_game(game),
	  m_pgn(game->pgn()),
	  m_tournament(tournament),
	  m_finished(false)
{
	if (tournament)
	{
		int gameNum = tournament->gameNumber(game);
		int totalGames = tournament->finalGameCount();
		if (gameNum > 0 && totalGames > 0)
			m_titleSuffix = tr(" [%1/%2]").arg(gameNum).arg(totalGames);
	}
}

MainWindow::MainWindow(ChessGame* game)
	: m_game(nullptr),
	  m_closing(false),
	  m_readyToClose(false),
	  m_firstTabAutoCloseEnabled(true),
	  m_settingsRestored(false),
	  m_firstShowEventSeen(false)
{
	setAttribute(Qt::WA_DeleteOnClose, true);
	setDockNestingEnabled(true);

	m_gameViewer = new GameViewer(Qt::Horizontal, nullptr, true);
	for (int i = 0; i < 2; i++)
	{
		Chess::Side side = Chess::Side::Type(i);
		m_gameViewer->chessClock(side)->setPlayerName(side.toString());
	}
	m_gameViewer->setContentsMargins(6, 6, 6, 6);

	m_moveList = new MoveList(this);
	m_tagsModel = new PgnTagsModel(this);
	#if 0
	new ModelTest(m_tagsModel, this);
	#endif

	m_evalHistory = new EvalHistory(this);
	m_evalWidgets[0] = new EvalWidget(this);
	m_evalWidgets[1] = new EvalWidget(this);

	QVBoxLayout* mainLayout = new QVBoxLayout();
	mainLayout->addWidget(m_gameViewer);

	// The content margins look stupid when used with dock widgets
	mainLayout->setContentsMargins(0, 0, 0, 0);

	QWidget* mainWidget = new QWidget(this);
	mainWidget->setLayout(mainLayout);
	setCentralWidget(mainWidget);

	createActions();
	createMenus();
	createToolBars();
	createDockWindows();
	createStatusBar();

	connect(m_moveList, SIGNAL(moveClicked(int,bool)),
	        m_gameViewer, SLOT(viewMove(int,bool)));
	connect(m_moveList, SIGNAL(commentClicked(int, QString)),
		this, SLOT(editMoveComment(int, QString)));
	connect(m_gameViewer, SIGNAL(moveSelected(int)),
		m_moveList, SLOT(selectMove(int)));

	connect(CuteChessApplication::instance()->gameManager(),
		SIGNAL(finished()), this, SLOT(onGameManagerFinished()),
		Qt::QueuedConnection);

	// Apply the saved geometry now, before the window is first shown, so
	// it isn't visible at the wrong size/position for even an instant.
	// This early application is only cosmetic, though: the window
	// doesn't have a native handle yet at this point, and on Windows the
	// platform's own initial-placement handling can still override it
	// once the window is actually created and shown. showEvent() below
	// re-applies it a second time, later, to win that race -- see the
	// comment there.
	applySavedGeometry();
	addGame(game);
}

MainWindow::~MainWindow()
{
}

void MainWindow::createActions()
{
	m_newGameAct = new QAction(tr("&New..."), this);
	m_newGameAct->setShortcut(QKeySequence::New);

	m_closeGameAct = new QAction(tr("&Close"), this);
	#ifdef Q_OS_WIN32
	m_closeGameAct->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_W));
	#else
	m_closeGameAct->setShortcut(QKeySequence::Close);
	#endif

	m_saveGameAct = new QAction(tr("&Save"), this);
	m_saveGameAct->setShortcut(QKeySequence::Save);

	m_saveGameAsAct = new QAction(tr("Save &As..."), this);
	m_saveGameAsAct->setShortcut(QKeySequence::SaveAs);

	m_copyFenAct = new QAction(tr("Copy F&EN"), this);
	QAction* copyFenSequence = new QAction(m_gameViewer);
	copyFenSequence->setShortcut(QKeySequence::Copy);
	copyFenSequence->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	m_gameViewer->addAction(copyFenSequence);

	m_pasteFenAct = new QAction(tr("&Paste FEN"), this);
	m_pasteFenAct->setShortcut(QKeySequence(QKeySequence::Paste));

	m_copyPgnAct = new QAction(tr("Copy PG&N"), this);

	m_flipBoardAct = new QAction(tr("&Flip Board"), this);
	m_flipBoardAct->setShortcut(Qt::CTRL | Qt::Key_F);

	m_adjudicateDrawAct = new QAction(tr("Ad&judicate Draw"), this);
	m_adjudicateWhiteWinAct = new QAction(tr("Adjudicate Win for White"), this);
	m_adjudicateBlackWinAct = new QAction(tr("Adjudicate Win for Black"), this);

	m_resignGameAct = new QAction(tr("Resign"), this);

	// Text is set for real by updatePauseResumeAction(); this is just
	// the initial (nothing to pause yet) state.
	m_pauseResumeAct = new QAction(tr("&Pause"), this);
	m_pauseResumeAct->setShortcut(Qt::CTRL | Qt::Key_P);
	m_pauseResumeAct->setEnabled(false);

	m_quitGameAct = new QAction(tr("&Quit"), this);
	m_quitGameAct->setMenuRole(QAction::QuitRole);
	#ifdef Q_OS_WIN32
	m_quitGameAct->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));
	#else
	m_quitGameAct->setShortcut(QKeySequence::Quit);
	#endif

	m_newTournamentAct = new QAction(tr("&New..."), this);
	m_stopTournamentAct = new QAction(tr("&Stop"), this);
	m_showTournamentResultsAct = new QAction(tr("&Results..."), this);

	m_showSettingsAct = new QAction(tr("&Settings"), this);
	m_showSettingsAct->setMenuRole(QAction::PreferencesRole);

	m_showGameDatabaseWindowAct = new QAction(tr("&Game Database"), this);

	m_showGameWallAct = new QAction(tr("&Active Games"), this);

	m_minimizeAct = new QAction(tr("&Minimize"), this);
	m_minimizeAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));

	// Using three key codes on Qt 6 results in compilation error
	m_showPreviousTabAct = new QAction(tr("Show &Previous Tab"), this);
	#ifdef Q_OS_MAC
	m_showPreviousTabAct->setShortcut(QKeySequence(tr("Meta+Shift+Tab")));
	#else
	m_showPreviousTabAct->setShortcut(QKeySequence(tr("Ctrl+Shift+Tab")));
	#endif

	m_showNextTabAct = new QAction(tr("Show &Next Tab"), this);
	#ifdef Q_OS_MAC
	m_showNextTabAct->setShortcut(QKeySequence(Qt::MetaModifier + Qt::Key_Tab));
	#else
	m_showNextTabAct->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_Tab));
	#endif

	m_aboutAct = new QAction(tr("&About Cute Chess..."), this);
	m_aboutAct->setMenuRole(QAction::AboutRole);

	connect(m_newGameAct, SIGNAL(triggered()), this, SLOT(newGame()));
	connect(m_copyFenAct, SIGNAL(triggered()), this, SLOT(copyFen()));
	connect(m_pasteFenAct, SIGNAL(triggered()), this, SLOT(pasteFen()));
	connect(copyFenSequence, SIGNAL(triggered()), this, SLOT(copyFen()));
	connect(m_copyPgnAct, SIGNAL(triggered()), this, SLOT(copyPgn()));
	connect(m_flipBoardAct, SIGNAL(triggered()),
		m_gameViewer->boardScene(), SLOT(flip()));
	connect(m_closeGameAct, &QAction::triggered, this, [=]()
	{
		auto focusWindow = CuteChessApplication::activeWindow();
		if (!focusWindow)
			return;

		auto focusMainWindow = qobject_cast<MainWindow*>(focusWindow);
		if (focusMainWindow)
		{
			focusMainWindow->closeCurrentGame();
			return;
		}

		focusWindow->close();
	});

	auto app = CuteChessApplication::instance();

	connect(m_saveGameAct, SIGNAL(triggered()), this, SLOT(save()));
	connect(m_saveGameAsAct, SIGNAL(triggered()), this, SLOT(saveAs()));

	connect(m_adjudicateDrawAct, SIGNAL(triggered()), this, SLOT(adjudicateDraw()));
	connect(m_adjudicateWhiteWinAct, SIGNAL(triggered()), this, SLOT(adjudicateWhiteWin()));
	connect(m_adjudicateBlackWinAct, SIGNAL(triggered()), this, SLOT(adjudicateBlackWin()));

	connect(m_resignGameAct, SIGNAL(triggered()), this, SLOT(resignGame()));
	connect(m_pauseResumeAct, SIGNAL(triggered()), this, SLOT(togglePauseResume()));

	connect(m_quitGameAct, &QAction::triggered,
		app, &CuteChessApplication::onQuitAction);

	connect(m_newTournamentAct, SIGNAL(triggered()), this, SLOT(newTournament()));

	connect(m_minimizeAct, &QAction::triggered, this, [=]()
	{
		auto focusWindow = app->focusWindow();
		if (focusWindow != nullptr)
		{
			focusWindow->showMinimized();
		}
	});

	connect(m_showSettingsAct, SIGNAL(triggered()),
		app, SLOT(showSettingsDialog()));

	connect(m_showTournamentResultsAct, SIGNAL(triggered()),
		app, SLOT(showTournamentResultsDialog()));

	connect(m_showGameDatabaseWindowAct, SIGNAL(triggered()),
		app, SLOT(showGameDatabaseDialog()));

	connect(m_showGameWallAct, SIGNAL(triggered()),
		app, SLOT(showGameWall()));

	connect(m_aboutAct, SIGNAL(triggered()), this, SLOT(showAboutDialog()));
}

void MainWindow::createMenus()
{
	m_gameMenu = menuBar()->addMenu(tr("&Game"));
	m_gameMenu->addAction(m_newGameAct);
	m_gameMenu->addSeparator();
	m_gameMenu->addAction(m_closeGameAct);
	m_gameMenu->addAction(m_saveGameAct);
	m_gameMenu->addAction(m_saveGameAsAct);
	m_gameMenu->addSeparator();
	m_gameMenu->addAction(m_copyFenAct);
	m_gameMenu->addAction(m_copyPgnAct);
	m_gameMenu->addAction(m_pasteFenAct);
	m_gameMenu->addSeparator();
	m_gameMenu->addAction(m_adjudicateDrawAct);
	m_gameMenu->addAction(m_adjudicateWhiteWinAct);
	m_gameMenu->addAction(m_adjudicateBlackWinAct);
	m_gameMenu->addSeparator();
	m_gameMenu->addAction(m_resignGameAct);
	m_gameMenu->addSeparator();
	m_gameMenu->addAction(m_pauseResumeAct);
	m_gameMenu->addSeparator();
	m_gameMenu->addAction(m_quitGameAct);

	m_tournamentMenu = menuBar()->addMenu(tr("&Tournament"));
	m_tournamentMenu->addAction(m_newTournamentAct);
	m_tournamentMenu->addAction(m_stopTournamentAct);
	m_tournamentMenu->addAction(m_showTournamentResultsAct);
	m_stopTournamentAct->setEnabled(false);

	m_toolsMenu = menuBar()->addMenu(tr("T&ools"));
	m_toolsMenu->addAction(m_showSettingsAct);
        m_toolsMenu->addAction(m_showGameDatabaseWindowAct);

	m_viewMenu = menuBar()->addMenu(tr("&View"));
	m_viewMenu->addAction(m_flipBoardAct);
	m_viewMenu->addSeparator();

	m_windowMenu = menuBar()->addMenu(tr("&Window"));
	addDefaultWindowMenu();

	connect(m_windowMenu, SIGNAL(aboutToShow()), this,
		SLOT(onWindowMenuAboutToShow()));

	m_helpMenu = menuBar()->addMenu(tr("&Help"));
	m_helpMenu->addAction(m_aboutAct);

	// Pause/resume button, top right-hand side of the main screen.
	//
	// A QToolButton set as the menu bar's top-right corner widget is
	// used rather than a toolbar entry, because the only existing
	// toolbar (m_tabBar's, see createToolBars()) is hidden whenever
	// there's just one tab -- exactly the single-game case this
	// button also needs to work in -- and because a corner widget is
	// pinned to the right edge regardless of window width, without
	// needing a stretch spacer. It's plain text, not an icon, so it
	// renders correctly with no dependency on an icon theme being
	// present (this AppImage doesn't bundle one).
	//
	// This one QAction/button is shared by every tab: it always acts
	// on whichever tab is currently showing (see pausableGames() and
	// updatePauseResumeAction()), the same way the existing Resign/
	// Adjudicate actions already do.
	QToolButton* pauseResumeButton = new QToolButton(this);
	pauseResumeButton->setDefaultAction(m_pauseResumeAct);
	pauseResumeButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	pauseResumeButton->setFocusPolicy(Qt::NoFocus);
	menuBar()->setCornerWidget(pauseResumeButton, Qt::TopRightCorner);
}

void MainWindow::createToolBars()
{
	m_tabBar = new GameTabBar();
	m_tabBar->setDocumentMode(true);
	m_tabBar->setTabsClosable(true);
	m_tabBar->setMovable(false);
	m_tabBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	connect(m_tabBar, SIGNAL(currentChanged(int)),
		this, SLOT(onTabChanged(int)));
	connect(m_tabBar, SIGNAL(tabCloseRequested(int)),
		this, SLOT(onTabCloseRequested(int)));
	connect(m_showPreviousTabAct, SIGNAL(triggered()),
		m_tabBar, SLOT(showPreviousTab()));
	connect(m_showNextTabAct, SIGNAL(triggered()),
		m_tabBar, SLOT(showNextTab()));

	QToolBar* toolBar = new QToolBar(tr("Game Tabs"));
	toolBar->setObjectName("GameTabs");
	toolBar->setVisible(false);
	toolBar->setFloatable(false);
	toolBar->setMovable(false);
	toolBar->setAllowedAreas(Qt::TopToolBarArea);
	toolBar->addWidget(m_tabBar);
	addToolBar(toolBar);
}

void MainWindow::createDockWindows()
{
	// Engine debug
	QDockWidget* engineDebugDock = new QDockWidget(tr("Engine Debug"), this);
	engineDebugDock->setObjectName("EngineDebugDock");
	m_engineDebugLog = new PlainTextLog(engineDebugDock);
	engineDebugDock->setWidget(m_engineDebugLog);
	engineDebugDock->close();
	addDockWidget(Qt::BottomDockWidgetArea, engineDebugDock);

	// Evaluation history
	auto evalHistoryDock = new QDockWidget(tr("Evaluation history"), this);
	evalHistoryDock->setObjectName("EvalHistoryDock");
	evalHistoryDock->setWidget(m_evalHistory);
	addDockWidget(Qt::BottomDockWidgetArea, evalHistoryDock);

	// Players' eval widgets
	auto whiteEvalDock = new QDockWidget(tr("White's evaluation"), this);
	whiteEvalDock->setObjectName("WhiteEvalDock");
	whiteEvalDock->setWidget(m_evalWidgets[Chess::Side::White]);
	addDockWidget(Qt::RightDockWidgetArea, whiteEvalDock);
	auto blackEvalDock = new QDockWidget(tr("Black's evaluation"), this);
	blackEvalDock->setObjectName("BlackEvalDock");
	blackEvalDock->setWidget(m_evalWidgets[Chess::Side::Black]);
	addDockWidget(Qt::RightDockWidgetArea, blackEvalDock);

	// Move list
	QDockWidget* moveListDock = new QDockWidget(tr("Moves"), this);
	moveListDock->setObjectName("MoveListDock");
	moveListDock->setWidget(m_moveList);
	addDockWidget(Qt::RightDockWidgetArea, moveListDock);
	splitDockWidget(moveListDock, whiteEvalDock, Qt::Horizontal);
	splitDockWidget(whiteEvalDock, blackEvalDock, Qt::Vertical);

	// Tags
	QDockWidget* tagsDock = new QDockWidget(tr("Tags"), this);
	tagsDock->setObjectName("TagsDock");
	QTreeView* tagsView = new QTreeView(tagsDock);
	tagsView->setModel(m_tagsModel);
	tagsView->setAlternatingRowColors(true);
	tagsView->setRootIsDecorated(false);
	tagsDock->setWidget(tagsView);

	addDockWidget(Qt::RightDockWidgetArea, tagsDock);

	tabifyDockWidget(moveListDock, tagsDock);
	moveListDock->raise();

	// Add toggle view actions to the View menu
	m_viewMenu->addAction(moveListDock->toggleViewAction());
	m_viewMenu->addAction(tagsDock->toggleViewAction());
	m_viewMenu->addAction(engineDebugDock->toggleViewAction());
	m_viewMenu->addAction(evalHistoryDock->toggleViewAction());
	m_viewMenu->addAction(whiteEvalDock->toggleViewAction());
	m_viewMenu->addAction(blackEvalDock->toggleViewAction());

	// Keep a handle to every dock whose ticked state lives in the View
	// menu, so their visibility can be saved/restored explicitly and
	// independently of QMainWindow's own (fragile, all-or-nothing)
	// saveState()/restoreState() blob -- see applySavedGeometry() and
	// dockVisibilityMap().
	m_dockWidgets << moveListDock << tagsDock << engineDebugDock
		      << evalHistoryDock << whiteEvalDock << blackEvalDock;

	// Seed each dock's entry in m_userDockVisibility (see its doc comment
	// in mainwindow.h) from !isHidden() rather than isVisible(): this runs
	// from the MainWindow constructor, before the window is first shown,
	// at which point isVisible() is unconditionally false for every dock
	// regardless of its real state. isHidden() has no such dependency on
	// the window having been shown -- it only reflects an explicit
	// hide()/close(), which is exactly the case for "Engine Debug" above.
	// A dock with a real saved preference gets it from
	// applySavedGeometry() immediately after this, which overwrites this
	// default; this seed is only the final answer for a dock with
	// nothing saved yet.
	for (QDockWidget* dock : m_dockWidgets)
		m_userDockVisibility.insert(dock->objectName(), !dock->isHidden());

	// In addition to the batch write at a clean shutdown
	// (stageGeometryForShutdown() -> onAboutToQuit()), persist each
	// dock's ticked/visible state to disk the moment the user actually
	// changes it, so a crash or forced termination can't lose it.
	//
	// This connects to QAction::triggered(bool) rather than
	// QDockWidget::visibilityChanged(bool): the latter also fires when a
	// dock's on-screen visibility changes for purely internal layout
	// reasons (e.g. the main window squeezing a dock to zero size when
	// there isn't room for it, which happens transiently while tabs open
	// and close back-to-back during an engine-engine tournament), and
	// writing that transient state to disk as if it were a deliberate
	// choice caused ticks to revert unexpectedly. triggered() only fires
	// for a genuine activation of the action -- a real click, its
	// shortcut, or an explicit trigger()/activate() call -- never as a
	// side effect of setVisible() called by the layout engine or by the
	// startup restore code below (applySavedGeometry(),
	// verifyViewMenuAgainstBackup()).
	for (QDockWidget* dock : m_dockWidgets)
	{
		connect(dock->toggleViewAction(), &QAction::triggered,
			this, [this, dock](bool visible)
			{
				// A genuine, deliberate tick/untick -- see
				// m_userDockVisibility's doc comment in
				// mainwindow.h for why this (and the two
				// setVisible() call sites below, plus
				// eventFilter()'s QEvent::Close handling) are
				// the only places this map is ever written.
				m_userDockVisibility.insert(dock->objectName(), visible);
				saveDockVisibilityImmediately(dock, visible);
			});

		// Catches the dock's own title-bar close button via its
		// clicked() signal rather than QEvent::Close on the dock: the
		// button's close() call is indistinguishable at the event level
		// from QApplication::closeAllWindows()'s close() on every
		// visible top-level widget (floating docks included) during
		// Game > Quit / Ctrl+Q or a session logout, and treating that as
		// a deliberate untick loses ticks on quit that the main window's
		// own close button keeps. See eventFilter()'s doc comment in
		// mainwindow.h. clicked() only ever fires for a real click.
		QAbstractButton* closeButton = dock->findChild<QAbstractButton*>(
			QStringLiteral("qt_dockwidget_closebutton"));
		if (closeButton != nullptr)
			connect(closeButton, &QAbstractButton::clicked,
				this, [this, dock]() { onDockClosedByUser(dock); });
		else
			qWarning("MainWindow: no close button found on dock \"%s\"; "
				 "closing it with its title-bar button will not be "
				 "remembered", qPrintable(dock->objectName()));

		// Also watch for a *window-system* close of a floating dock
		// (see eventFilter()).
		dock->installEventFilter(this);
	}
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
	// Only a *spontaneous* Close -- one that originated from the window
	// system, e.g. the user closing a floating dock through its window
	// frame -- counts as a deliberate user gesture. A non-spontaneous
	// Close is just code calling close() on the dock, notably
	// QApplication::closeAllWindows() (Game > Quit, Ctrl+Q, session
	// logout, the UNIX-signal shutdown path), which closes every visible
	// top-level widget including floating docks and should not be read
	// as the user unticking anything. The dock's own close button is
	// handled separately, via its clicked() signal in
	// createDockWindows().
	if (event->type() == QEvent::Close && event->spontaneous())
	{
		QDockWidget* dock = qobject_cast<QDockWidget*>(watched);
		if (dock != nullptr && m_dockWidgets.contains(dock))
			onDockClosedByUser(dock);
	}
	return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onDockClosedByUser(QDockWidget* dock)
{
	Q_ASSERT(dock != nullptr);

	// Same treatment as a genuine triggered(false) from the View menu --
	// see the long comment on m_userDockVisibility in mainwindow.h.
	m_userDockVisibility.insert(dock->objectName(), false);
	saveDockVisibilityImmediately(dock, false);
}

void MainWindow::saveDockVisibilityImmediately(QDockWidget* dock, bool visible)
{
	Q_ASSERT(dock != nullptr);

	// Each write is its own fresh QSettings object, immediately
	// sync()ed, exactly like the other individual settings writes
	// throughout this file (see e.g. applySavedGeometry() above) --
	// deliberately not a long-lived QSettings member, so there's no
	// window in which a stale in-memory cache could get flushed back
	// over a more recent value written by another QSettings instance
	// elsewhere in the app (see the detailed comment in
	// CuteChessApplication::onAboutToQuit() about that exact failure
	// mode).
	QSettings s;
	s.beginGroup("ui");
	s.beginGroup("mainwindow");
	s.beginGroup("docks");
	s.setValue(dock->objectName(), visible);
	s.endGroup();
	s.endGroup();
	s.endGroup();
	s.sync();
}


void MainWindow::createStatusBar()
{
	// The label itself lives in m_gameViewer, centered between the two
	// clocks at the top of the window -- see GameViewer::scoreLabel().
	// We still call statusBar() here (via the QMainWindow accessor)
	// so a status bar exists for Qt's own use (e.g. size grip);
	// nothing is added to it any more.
	statusBar();
	m_tournamentScoreLabel = m_gameViewer->scoreLabel();
	Q_ASSERT(m_tournamentScoreLabel != nullptr);
	m_tournamentScoreLabel->setContentsMargins(4, 0, 4, 0);
}

void MainWindow::applySavedGeometry()
{
	QSettings s;
	s.beginGroup("ui");
	s.beginGroup("mainwindow");

	// restoreState() first, restoreGeometry() second: restoreState()
	// lays out the docks/toolbars against the window's size at the
	// time it runs, which can itself resize the window (see
	// enforceSavedWindowGeometry()'s doc comment for why, especially
	// with nested/split docks). Applying the saved window rectangle
	// with restoreGeometry() *after* that means it wins over whatever
	// restoreState() just did, rather than being immediately
	// overwritten by it.
	restoreState(s.value("window_state").toByteArray());
	restoreGeometry(s.value("geometry").toByteArray());

	// Explicitly re-apply each dock's last saved ticked/visible state,
	// on top of whatever restoreState() above just did.
	//
	// restoreState() bundles every dock's position *and* visibility into
	// one opaque blob, and if that blob doesn't match the current set of
	// dock widgets exactly (e.g. a dock was added, removed or renamed
	// since it was saved, or the value is corrupted/truncated) it does
	// nothing at all, silently leaving every dock at its hard-coded
	// createDockWindows() default. restoreGeometry() just above has no
	// such fragility, since it only concerns the main window's own
	// rectangle -- so each dock's ticked state is also saved as its own
	// plain boolean (see stageGeometryForShutdown()/dockVisibilityMap()
	// and CuteChessApplication::onAboutToQuit()) and reapplied here
	// directly, independent of whether the rest of restoreState()
	// succeeded.
	//
	// Entered here, still nested inside "mainwindow" (i.e.
	// "ui/mainwindow/docks"), the same settings group
	// saveDockVisibilityImmediately() and the batch write in
	// CuteChessApplication::onAboutToQuit() both use.
	s.beginGroup("docks");
	for (QDockWidget* dock : m_dockWidgets)
	{
		const QString key = dock->objectName();
		if (s.contains(key))
		{
			const bool visible = s.value(key).toBool();
			dock->setVisible(visible);
			// Restoring the user's last saved choice is itself a
			// genuine, deliberate state, so keep
			// m_userDockVisibility (see its doc comment in
			// mainwindow.h) in sync with it -- otherwise the very
			// next layout reflow's transient isVisible() would have
			// nothing correct to fall back on.
			m_userDockVisibility.insert(key, visible);
		}
	}
	s.endGroup();

	s.endGroup();
	s.endGroup();

	// The dock visibility changes just above are themselves a form of
	// layout activation and can resize the window on their own -- see
	// enforceSavedWindowGeometry()'s doc comment. Have the saved
	// window rectangle win over that too, here at the true end of this
	// function.
	enforceSavedWindowGeometry();
}

void MainWindow::enforceSavedWindowGeometry()
{
	QSettings s;
	s.beginGroup("ui");
	s.beginGroup("mainwindow");
	const QByteArray geometry = s.value("geometry").toByteArray();
	s.endGroup();
	s.endGroup();

	if (!geometry.isEmpty())
		restoreGeometry(geometry);
}

QVariantMap MainWindow::dockVisibilityMap() const
{
	// Deliberately m_userDockVisibility, not each dock's isVisible():
	// see its doc comment in mainwindow.h. isVisible() can be
	// transiently false during a layout reflow (e.g. the main window
	// squeezing a dock to zero size) for reasons unrelated to the
	// user's chosen ticked state; both the immediate-write/
	// shutdown-staging path and CuteChessApplication::backupViewMenuState()'s
	// snapshot call this function and need the user's actual intent.
	return m_userDockVisibility;
}

void MainWindow::showEvent(QShowEvent* event)
{
	QMainWindow::showEvent(event);

	if (m_firstShowEventSeen)
		return;
	m_firstShowEventSeen = true;

	// Re-apply the saved geometry/state one more time, deferred to the
	// next pass through the event loop via a zero-delay timer. The
	// constructor already applied it once, but that happened before the
	// window had a native handle; by the time the OS actually creates
	// and shows the window it can impose its own initial placement,
	// silently overriding what we set earlier -- this has been observed
	// on Windows in particular. Waiting for the *next* iteration of the
	// event loop, after this show event (and anything it triggers) has
	// fully finished processing, means our restore is the last thing to
	// touch the window's geometry, so it's what actually sticks.
	//
	// m_settingsRestored is only set true once this later, "real"
	// restore has happened, so moveEvent()/resizeEvent() below can't
	// mistake any intermediate/default placement for something worth
	// remembering.
	QTimer::singleShot(0, this, &MainWindow::restoreSavedGeometry);
}

void MainWindow::restoreSavedGeometry()
{
	applySavedGeometry();

	// The final job of startup: make sure the View menu's restored
	// state actually matches what was backed up at the last clean
	// close, restoring from that backup if not. See
	// verifyViewMenuAgainstBackup()'s doc comment in mainwindow.h.
	verifyViewMenuAgainstBackup();

	// verifyViewMenuAgainstBackup() can itself toggle dock visibility
	// (see enforceSavedWindowGeometry()'s doc comment for why that
	// matters), and this whole function is the deferred, "real" restore
	// that showEvent() schedules specifically to win any last-moment
	// placement race -- so this is the true last point before the
	// window is on screen for good. Enforce the saved rectangle one
	// final time here, after everything else above has had its say.
	enforceSavedWindowGeometry();

	m_settingsRestored = true;
}

void MainWindow::verifyViewMenuAgainstBackup()
{
	QSettings s;
	s.beginGroup("ui");
	s.beginGroup("mainwindow");
	s.beginGroup("docks_backup");
	QStringList backupKeys = s.childKeys();

	// Nothing to compare against -- either this is the very first run,
	// or the last session never reached CuteChessApplication::
	// onQuitAction() (backupViewMenuState()) to write one. Leave
	// whatever applySavedGeometry() just restored alone.
	if (backupKeys.isEmpty())
	{
		s.endGroup();
		s.endGroup();
		s.endGroup();
		return;
	}

	QVariantMap backup;
	for (const QString& key : backupKeys)
		backup.insert(key, s.value(key));
	s.endGroup();
	s.endGroup();
	s.endGroup();

	const QVariantMap current = dockVisibilityMap();

	bool changed = false;
	for (auto it = backup.constBegin(); it != backup.constEnd(); ++it)
	{
		if (!current.contains(it.key())
		||  current.value(it.key()).toBool() != it.value().toBool())
		{
			changed = true;
			break;
		}
	}

	if (!changed)
		return;

	// At least one dock's ticked state doesn't match the backup taken
	// when the program was last closed -- restore every dock from that
	// backup so the View menu ends up exactly as the user left it.
	for (QDockWidget* dock : m_dockWidgets)
	{
		const QString key = dock->objectName();
		if (backup.contains(key))
		{
			const bool visible = backup.value(key).toBool();
			dock->setVisible(visible);
			// See the matching comment in applySavedGeometry():
			// this is also a genuine, deliberate restore of the
			// user's real state, so m_userDockVisibility needs to
			// agree with it.
			m_userDockVisibility.insert(key, visible);
		}
	}
}

void MainWindow::stageGeometryForShutdown()
{
	// Only hand the current geometry/state to the application, which
	// keeps it in memory. The actual write to disk is deferred to
	// CuteChessApplication::onAboutToQuit(), the last point in the
	// application's life -- see the comment there for why.
	CuteChessApplication::instance()->recordMainWindowGeometry(
		saveGeometry(), saveState(), dockVisibilityMap());
}

void MainWindow::moveEvent(QMoveEvent* event)
{
	QMainWindow::moveEvent(event);

	// Cutechess can have several game windows open at once, but they all
	// share a single saved "last window position", so only the window
	// the user is currently interacting with should be allowed to stage
	// an update to it. Otherwise a background window (e.g. one about to
	// be closed as part of a tournament finishing, or during "Quit")
	// could stage the position of a window the user doesn't actually
	// care about, overwriting what the active window had staged.
	//
	// !m_closing is equally important: once a shutdown has actually
	// been requested (see closeEvent()), closeAllGames() and/or a
	// tournament stopping go on to hide docks and close tabs, which
	// can genuinely resize/move this still-active window as a side
	// effect of that teardown. Without this guard, that teardown-driven
	// resize (e.g. the window collapsing in height once its docks are
	// hidden) would overwrite the correct geometry -- already staged
	// the instant the shutdown request came in, see closeEvent() -- with
	// the shrunk one, right before it gets written to disk. This is
	// exactly the "geometry doesn't always stick" bug: only ever
	// noticeable via a quit that has something to tear down (an open
	// game/tournament with its docks populated), which is why it comes
	// and goes and why width (largely unaffected by the bottom docks
	// collapsing) kept sticking while height didn't.
	if (m_settingsRestored && isActiveWindow() && !isMinimized() && !m_closing)
		stageGeometryForShutdown();
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
	QMainWindow::resizeEvent(event);

	// See the matching comment in moveEvent() above for why !m_closing
	// is required here too.
	if (m_settingsRestored && isActiveWindow() && !isMinimized() && !m_closing)
		stageGeometryForShutdown();
}

void MainWindow::addGame(ChessGame* game)
{
	Tournament* tournament = qobject_cast<Tournament*>(QObject::sender());
	TabData tab(game, tournament);

	// The user paused this match (see togglePauseResume()) before this
	// particular game of it existed -- e.g. it's the next round, or a
	// concurrent slot that just freed up. Start it paused too, so
	// "pause the match" really means the whole match and not just
	// whichever games happened to already be on screen at the time.
	//
	// By the time addGame() runs, the game has already been handed off
	// to its own GameThread (see GameManager::onGameInitialized()), but
	// ChessGame::pause() is safe to call directly from any thread (see
	// its doc comment), so no cross-thread dance is needed here.
	if (tournament && m_pausedTournaments.contains(tournament))
		game->pause();

	if (tournament)
	{
		int index = tabIndex(tournament, true);
		if (index != -1)
		{
			delete m_tabs[index].m_pgn;
			m_tabs[index] = tab;

			m_tabBar->setTabText(index, genericTitle(tab));
			if (!m_closing && m_tabBar->currentIndex() == index)
				setCurrentGame(tab);

			return;
		}
	}
	else
		connect(game, SIGNAL(finished(ChessGame*)),
			this, SLOT(onGameFinished(ChessGame*)));

	m_tabs.append(tab);
	m_tabBar->setCurrentIndex(m_tabBar->addTab(genericTitle(tab)));

	// Close the initial (startup) tab if it's still unused and the
	// setting is enabled. This is a one-time opportunity: once a
	// second tab exists, m_tabs[0] is no longer necessarily "the
	// initial tab", so we must never re-evaluate this later using
	// whatever now happens to be at index 0 -- see m_firstTabAutoCloseEnabled.
	//
	// The one-time flag must only be spent once we've actually been
	// able to read tab 0's state. If a second tab arrives so quickly
	// after startup that the initial tab's game isn't attached yet
	// (m_tabs[0].m_game still null -- possible when e.g. a tournament's
	// first couple of games start back-to-back right after launch),
	// treating that as "checked and not empty" would burn the only
	// chance this session gets, silently disabling the feature for
	// the rest of the session even though the user has it enabled.
	// Deferring to the next addGame() call instead lets a slightly
	// later, valid read take the decision.
	if (m_tabs.size() >= 2
	&&  m_firstTabAutoCloseEnabled
	&&  !m_tabs[0].m_game.isNull())
	{
		if (QSettings().value("ui/close_unused_initial_tab", true).toBool()
		&&  m_tabs[0].m_game.data()->moves().isEmpty())
			closeTab(0);

		m_firstTabAutoCloseEnabled = false;
	}

	if (m_tabs.size() >= 2)
		m_tabBar->parentWidget()->show();
}

void MainWindow::removeGame(int index)
{
	Q_ASSERT(index != -1);

	m_tabs.removeAt(index);
	m_tabBar->removeTab(index);

	if (m_tabs.size() == 1)
		m_tabBar->parentWidget()->hide();
}

void MainWindow::destroyGame(ChessGame* game)
{
	Q_ASSERT(game != nullptr);

	int index = tabIndex(game);
	Q_ASSERT(index != -1);
	TabData tab = m_tabs.at(index);

	removeGame(index);

	if (tab.m_tournament == nullptr)
		game->deleteLater();
	delete tab.m_pgn;

	if (m_tabs.isEmpty())
		close();
}

void MainWindow::setCurrentGame(const TabData& gameData)
{
	if (gameData.m_game == m_game && m_game != nullptr)
		return;

	for (int i = 0; i < 2; i++)
	{
		ChessPlayer* player(m_players[i]);
		if (player != nullptr)
		{
			disconnect(player, nullptr, m_engineDebugLog, nullptr);
			disconnect(player, nullptr,
			           m_gameViewer->chessClock(Chess::Side::White), nullptr);
			disconnect(player, nullptr,
			           m_gameViewer->chessClock(Chess::Side::Black), nullptr);
		}
	}

	if (m_game != nullptr)
	{
		m_game->pgn()->setTagReceiver(nullptr);
		m_gameViewer->disconnectGame();
		disconnect(m_game, nullptr, m_moveList, nullptr);
		disconnect(m_game, &ChessGame::pausedChanged,
			   this, &MainWindow::updatePauseResumeAction);

		ChessGame* tmp = m_game;
		m_game = nullptr;

		// QObject::disconnect() is not atomic, so we need to flush
		// all pending events from the previous game before switching
		// to the next one.
		tmp->lockThread();
		CuteChessApplication::processEvents();
		tmp->unlockThread();

		// If the call to CuteChessApplication::processEvents() caused
		// a new game to be selected as the current game, then our
		// work here is done.
		if (m_game != nullptr)
			return;
	}

	m_game = gameData.m_game;

	lockCurrentGame();

	m_engineDebugLog->clear();

	m_moveList->setGame(m_game, gameData.m_pgn);
	m_evalHistory->setGame(m_game);

	if (m_game == nullptr)
	{
		m_gameViewer->setGame(gameData.m_pgn);
		m_evalHistory->setPgnGame(gameData.m_pgn);

		for (int i = 0; i < 2; i++)
		{
			Chess::Side side = Chess::Side::Type(i);
			auto clock = m_gameViewer->chessClock(side);
			clock->stop();
			clock->setInfiniteTime(true);
			QString name = nameOnClock(gameData.m_pgn->playerName(side),
						   side);
			clock->setPlayerName(name);
		}

		m_tagsModel->setTags(gameData.m_pgn->tags());

		updateWindowTitle();
		updateMenus();

		for (auto evalWidget : m_evalWidgets)
			evalWidget->setPlayer(nullptr);

		return;
	}
	else
		m_gameViewer->setGame(m_game);

	m_tagsModel->setTags(gameData.m_pgn->tags());
	gameData.m_pgn->setTagReceiver(m_tagsModel);

	for (int i = 0; i < 2; i++)
	{
		Chess::Side side = Chess::Side::Type(i);
		ChessPlayer* player(m_game->player(side));
		m_players[i] = player;

		connect(player, SIGNAL(debugMessage(QString)),
			m_engineDebugLog, SLOT(appendPlainText(QString)));

		auto clock = m_gameViewer->chessClock(side);

		clock->stop();
		QString name = nameOnClock(player->name(), side);
		clock->setPlayerName(name);
		connect(player, SIGNAL(nameChanged(QString)),
			clock, SLOT(setPlayerName(QString)));

		clock->setInfiniteTime(player->timeControl()->isInfinite());

		if (player->state() == ChessPlayer::Thinking)
			clock->start(player->timeControl()->activeTimeLeft());
		else
			clock->setTime(player->timeControl()->timeLeft());

		connect(player, SIGNAL(startedThinking(int)),
			clock, SLOT(start(int)));
		connect(player, SIGNAL(stoppedThinking()),
			clock, SLOT(stop()));
		m_evalWidgets[i]->setPlayer(player);
	}

	// Use setFlipped() (absolute) rather than a manual isFlipped()
	// comparison + flip(). m_gameViewer->setGame() above just reset
	// this BoardScene's squares layer to unflipped internally
	// (setBoard()/populate()) without telling anyone, so an
	// isFlipped() check can no longer be trusted to reflect the
	// board's real, displayed state. setFlipped() both applies the
	// orientation this game wants and guarantees the eval bar (which
	// only hears about orientation via BoardScene::flipped()) is
	// told the true current state even when no actual flip animation
	// is needed -- see the header comment on BoardScene::setFlipped().
	m_gameViewer->boardScene()->setFlipped(m_game->boardShouldBeFlipped());

	// Keep the pause/resume button in sync if this game's paused
	// state changes on its own -- e.g. a fresh human-to-move game
	// resuming itself the moment the board is ready (see beginPlay()'s
	// wokeUp()->resume() connection), or a sibling game of the same
	// match being paused/resumed while this tab is the one on screen.
	connect(m_game, &ChessGame::pausedChanged,
		this, &MainWindow::updatePauseResumeAction);

	updateMenus();
	updateWindowTitle();
	unlockCurrentGame();
}

int MainWindow::tabIndex(ChessGame* game) const
{
	Q_ASSERT(game != nullptr);

	for (int i = 0; i < m_tabs.size(); i++)
	{
		if (m_tabs.at(i).m_id == game)
			return i;
	}

	return -1;
}

int MainWindow::tabIndex(Tournament* tournament, bool freeTab) const
{
	Q_ASSERT(tournament != nullptr);

	for (int i = 0; i < m_tabs.size(); i++)
	{
		const TabData& tab = m_tabs.at(i);

		if (tab.m_tournament == tournament
		&&  (!freeTab || (tab.m_game == nullptr || tab.m_finished)))
			return i;
	}

	return -1;
}

void MainWindow::onTabChanged(int index)
{
	if (index == -1 || m_closing)
		m_game = nullptr;
	else
		setCurrentGame(m_tabs.at(index));
}

void MainWindow::onTabCloseRequested(int index)
{
	const TabData& tab = m_tabs.at(index);

	if (tab.m_tournament && tab.m_game)
	{
		auto btn = QMessageBox::question(this, tr("End tournament game"),
			   tr("Do you really want to end the active tournament game?"));
		if (btn != QMessageBox::Yes)
			return;
	}

	closeTab(index);
}

void MainWindow::closeTab(int index)
{
	const TabData& tab = m_tabs.at(index);

	if (tab.m_game == nullptr)
	{
		delete tab.m_pgn;
		removeGame(index);

		if (m_tabs.isEmpty())
			close();

		return;
	}

	if (tab.m_finished)
		destroyGame(tab.m_game);
	else
	{
		connect(tab.m_game, SIGNAL(finished(ChessGame*)),
			this, SLOT(destroyGame(ChessGame*)));
		QMetaObject::invokeMethod(tab.m_game, "stop", Qt::QueuedConnection);
	}
}

void MainWindow::closeCurrentGame()
{
	closeTab(m_tabBar->currentIndex());
}

void MainWindow::newGame()
{
	EngineManager* engineManager = CuteChessApplication::instance()->engineManager();
	NewGameDialog dlg(engineManager, this);
	if (dlg.exec() != QDialog::Accepted)
		return;

	auto game = dlg.createGame();
	if (!game)
	{
		QMessageBox::critical(this, tr("Could not initialize game"),
				      tr("The game could not be initialized "
					 "due to an invalid opening."));
		return;
	}

	PlayerBuilder* builders[2] = {
		dlg.createPlayerBuilder(Chess::Side::White),
		dlg.createPlayerBuilder(Chess::Side::Black)
	};

	if (builders[game->board()->sideToMove()]->isHuman())
		game->pause();

	// Start the game in a new tab
	connect(game, SIGNAL(initialized(ChessGame*)),
		this, SLOT(addGame(ChessGame*)));
	connect(game, SIGNAL(startFailed(ChessGame*)),
		this, SLOT(onGameStartFailed(ChessGame*)));
	CuteChessApplication::instance()->gameManager()->newGame(game,
		builders[Chess::Side::White], builders[Chess::Side::Black]);
}

void MainWindow::onGameStartFailed(ChessGame* game)
{
	QMessageBox::critical(this, tr("Game Error"), game->errorString());
}

void MainWindow::onGameFinished(ChessGame* game)
{
	int tIndex = tabIndex(game);
	if (tIndex == -1)
		return;

	auto& tab = m_tabs[tIndex];
	tab.m_finished = true;
	QString title = genericTitle(tab);
	m_tabBar->setTabText(tIndex, title);
	if (game == m_game)
	{
		// Finished tournament games are destroyed immediately
		// so we can't touch the game object any more.
		if (tab.m_tournament)
			m_game = nullptr;
		updateWindowTitle();
		updateMenus();
	}

	// save game notation of non-tournament games to default PGN file
	if (!tab.m_tournament
	&&  !game->pgn()->isNull()
	&&  	(  !game->pgn()->moves().isEmpty()   // ignore empty games
		|| !game->pgn()->result().isNone())) // without adjudication
	{
		QString fileName = QSettings().value("games/default_pgn_output_file", QString())
					      .toString();

		if (!fileName.isEmpty())
			game->pgn()->write(fileName);
			//TODO: reaction on error
	}

	// Save a copy of the finished game, in PGN format, to the rolling
	// match-history PGN files, split by whether a human took part.
	// Tournament games with two engines and no human are handled
	// separately by onEngineGameFinished(), which has the pairing
	// information needed to compute the Elo difference, so they're
	// skipped here to avoid recording them twice.
	bool isHumanGame = (game->player(Chess::Side::White)
			     && game->player(Chess::Side::White)->isHuman())
			 || (game->player(Chess::Side::Black)
			     && game->player(Chess::Side::Black)->isHuman());
	if (isHumanGame)
	{
		GameHistoryRecorder::recordHumanGame(game,
			tab.m_tournament ? tab.m_tournament->name() : QString());
	}
	else if (!tab.m_tournament)
	{
		GameHistoryRecorder::recordEngineGame(game, QString(), qQNaN());
	}
}

void MainWindow::onEngineGameFinished(ChessGame* game,
				       int gameNumber,
				       int whiteIndex,
				       int blackIndex)
{
	Q_UNUSED(gameNumber);

	Tournament* tournament = qobject_cast<Tournament*>(QObject::sender());
	Q_ASSERT(tournament != nullptr);

	// Games with a human player are recorded by onGameFinished()
	// instead, once the tab bookkeeping there confirms it's actually
	// finished; skip them here so they aren't written twice.
	bool isHumanGame = (game->player(Chess::Side::White)
			     && game->player(Chess::Side::White)->isHuman())
			 || (game->player(Chess::Side::Black)
			     && game->player(Chess::Side::Black)->isHuman());
	if (isHumanGame)
		return;

	// An Elo difference only means something for a straight two-engine
	// match; for bigger round-robins/knockouts there's no single figure
	// to report, matching the same convention already used for the
	// live scoreboard in updateTournamentScore() above.
	qreal eloDiff = qQNaN();
	if (whiteIndex >= 0 && blackIndex >= 0
	&&  tournament->playerCount() == 2
	&&  tournament->type() != "knockout")
	{
		const TournamentPlayer& fcp = tournament->playerAt(0);
		Elo elo(fcp.wins(), fcp.losses(), fcp.draws());
		eloDiff = elo.diff();
	}

	GameHistoryRecorder::recordEngineGame(game, tournament->name(), eloDiff);
}

QString MainWindow::formatEloDiff(qreal diff) const
{
	if (std::isnan(diff))
		return tr("n/a");
	if (std::isinf(diff))
		return diff > 0 ? QStringLiteral("+\u221E") : QStringLiteral("-\u221E");
	// Elo::diff() already carries the sign of the first player's
	// (White wins + Black wins side, i.e. fcp's) advantage.
	return QStringLiteral("%1%2")
	       .arg(diff >= 0 ? QStringLiteral("+") : QString())
	       .arg(diff, 0, 'f', 1);
}

void MainWindow::updateTournamentScore(ChessGame* game,
					int gameNumber,
					int whiteIndex,
					int blackIndex)
{
	Q_UNUSED(game);
	Q_UNUSED(gameNumber);
	Q_UNUSED(whiteIndex);
	Q_UNUSED(blackIndex);

	Tournament* tournament = qobject_cast<Tournament*>(QObject::sender());
	Q_ASSERT(tournament != nullptr);

	// The live scoreboard only makes sense for a two-engine match; for
	// bigger round-robins/knockouts a single White/Black/Elo readout
	// doesn't mean anything, so just keep it hidden.
	if (tournament->playerCount() != 2 || tournament->type() == "knockout")
	{
		m_tournamentScoreLabel->hide();
		return;
	}

	const TournamentPlayer& fcp = tournament->playerAt(0);
	const TournamentPlayer& scp = tournament->playerAt(1);

	int draws = fcp.draws();

	QString eloText;
	int gamesFinished = fcp.wins() + fcp.losses() + fcp.draws();
	if (gamesFinished > 0)
	{
		// Elo diff needs each engine's own record (not a White/Black
		// split) since engines swap colors between games.
		Elo elo(fcp.wins(), fcp.losses(), fcp.draws());
		eloText = tr("   Elo diff: %1").arg(formatEloDiff(elo.diff()));
	}

	QString text = tr("%1: \u2013 %2 wins   Draws: \u2013 %3   %4: \u2013 %5 wins%6")
		       .arg(fcp.name())
		       .arg(fcp.wins())
		       .arg(draws)
		       .arg(scp.name())
		       .arg(scp.wins())
		       .arg(eloText);

	// Fall back to a shorter form if the full sentence won't fit
	// comfortably in the space between the two clocks. There's no
	// single "container width" to measure here -- the label sits
	// between two stretches in the clock row -- so approximate the
	// room available to it as whatever's left of the game viewer's
	// width once both clocks (which have first claim on the space)
	// are accounted for.
	QFontMetrics fm(m_tournamentScoreLabel->font());
	int available = m_gameViewer->width()
		       - m_gameViewer->chessClock(Chess::Side::White)->width()
		       - m_gameViewer->chessClock(Chess::Side::Black)->width()
		       - 40;
	if (available < 0)
		available = 0;
	if (fm.horizontalAdvance(text) > available)
	{
		text = tr("%1: \u2013 %2   Draws: \u2013 %3   %4: \u2013 %5%6")
		       .arg(fcp.name())
		       .arg(fcp.wins())
		       .arg(draws)
		       .arg(scp.name())
		       .arg(scp.wins())
		       .arg(eloText);
	}

	m_tournamentScoreLabel->setText(text);
	m_tournamentScoreLabel->show();
}

void MainWindow::newTournament()
{
	NewTournamentDialog dlg(CuteChessApplication::instance()->engineManager(), this);
	if (dlg.exec() != QDialog::Accepted)
		return;

	GameManager* manager = CuteChessApplication::instance()->gameManager();

	Tournament* t = dlg.createTournament(manager);
	auto resultsDialog = CuteChessApplication::instance()->tournamentResultsDialog();
	m_tournamentScoreLabel->hide();
	connect(t, SIGNAL(finished()),
		this, SLOT(onTournamentFinished()));
	connect(t, SIGNAL(gameStarted(ChessGame*, int, int, int)),
		this, SLOT(addGame(ChessGame*)));
	connect(t, SIGNAL(gameFinished(ChessGame*, int, int, int)),
		resultsDialog, SLOT(update()));
	connect(t, SIGNAL(gameFinished(ChessGame*, int, int, int)),
		this, SLOT(onGameFinished(ChessGame*)));
	connect(t, SIGNAL(gameFinished(ChessGame*, int, int, int)),
		this, SLOT(updateTournamentScore(ChessGame*, int, int, int)));
	connect(t, SIGNAL(gameFinished(ChessGame*, int, int, int)),
		this, SLOT(onEngineGameFinished(ChessGame*, int, int, int)));
	t->start();

	connect(m_stopTournamentAct, &QAction::triggered, [=]()
	{
		auto btn = QMessageBox::question(this, tr("Stop tournament"),
			   tr("Do you really want to stop the ongoing tournament?"));
		if (btn != QMessageBox::Yes)
		{
			m_closing = false;
			return;
		}

		t->stop();
	});
	m_newTournamentAct->setEnabled(false);
	m_stopTournamentAct->setEnabled(true);
	resultsDialog->setTournament(t);
}

void MainWindow::onTournamentFinished()
{
	Tournament* tournament = qobject_cast<Tournament*>(QObject::sender());
	Q_ASSERT(tournament != nullptr);

	m_stopTournamentAct->disconnect();
	m_pausedTournaments.remove(tournament);

	QString error = tournament->errorString();
	QString name = tournament->name();

	tournament->deleteLater();
	m_newTournamentAct->setEnabled(true);
	m_stopTournamentAct->setEnabled(false);

	if (m_closing)
	{
		closeAllGames();
		return;
	}

	m_showTournamentResultsAct->trigger();

	if (!error.isEmpty())
	{
		QMessageBox::critical(this,
				      tr("Tournament error"),
				      tr("Tournament \"%1\" finished with an error.\n\n%2")
				      .arg(name, error));
	}

	CuteChessApplication::alert(this);
}

void MainWindow::onWindowMenuAboutToShow()
{
	m_windowMenu->clear();

	addDefaultWindowMenu();
	m_windowMenu->addSeparator();

	const QList<MainWindow*> gameWindows =
		CuteChessApplication::instance()->gameWindows();

	for (int i = 0; i < gameWindows.size(); i++)
	{
		MainWindow* gameWindow = gameWindows.at(i);

		QAction* showWindowAction = m_windowMenu->addAction(
			gameWindow->windowListTitle(), this, SLOT(showGameWindow()));
		showWindowAction->setData(i);
		showWindowAction->setCheckable(true);

		if (gameWindow == this)
			showWindowAction->setChecked(true);
	}
}

void MainWindow::showGameWindow()
{
	if (QAction* action = qobject_cast<QAction*>(sender()))
		CuteChessApplication::instance()->showGameWindow(action->data().toInt());
}

void MainWindow::updateWindowTitle()
{
	// setWindowTitle() requires "[*]" (see docs)
	const TabData& gameData(m_tabs.at(m_tabBar->currentIndex()));
	setWindowTitle(genericTitle(gameData) + QLatin1String("[*]"));
}

QString MainWindow::windowListTitle() const
{
	const TabData& gameData(m_tabs.at(m_tabBar->currentIndex()));

	#ifndef Q_OS_MAC
	if (isWindowModified())
		return genericTitle(gameData) + QLatin1String("*");
	#endif

	return genericTitle(gameData);
}

QString MainWindow::genericTitle(const TabData& gameData) const
{
	QString white;
	QString black;
	Chess::Result result;
	if (gameData.m_game)
	{
		white = gameData.m_game->player(Chess::Side::White)->name();
		black = gameData.m_game->player(Chess::Side::Black)->name();
		result = gameData.m_game->result();
	}
	else
	{
		white = gameData.m_pgn->playerName(Chess::Side::White);
		black = gameData.m_pgn->playerName(Chess::Side::Black);
		result = gameData.m_pgn->result();
	}

	if (result.isNone())
		return tr("%1 vs %2%3").arg(white, black, gameData.m_titleSuffix);
	else
		return tr("%1 vs %2 (%3)%4")
			   .arg(white, black, result.toShortString(), gameData.m_titleSuffix);
}

void MainWindow::updateMenus()
{
	QPointer<ChessPlayer> white = m_players[Chess::Side::White];
	QPointer<ChessPlayer> black = m_players[Chess::Side::Black];
	bool isHumanGame =  (!white.isNull() && white->isHuman())
			 || (!black.isNull() && black->isHuman());
	bool gameOn = (!m_game.isNull() && !m_game->isFinished());
	m_adjudicateBlackWinAct->setEnabled(gameOn);
	m_adjudicateWhiteWinAct->setEnabled(gameOn);
	m_adjudicateDrawAct->setEnabled(gameOn);
	m_resignGameAct->setEnabled(gameOn && isHumanGame);
	updatePauseResumeAction();
}

QString MainWindow::nameOnClock(const QString& name, Chess::Side side) const
{
	QString text = name;
	bool displaySide = QSettings().value("ui/display_players_sides_on_clocks", false)
				      .toBool();
	if (displaySide)
		text.append(QString(" (%1)").arg(side.toString()));
	return text;
}

void MainWindow::editMoveComment(int ply, const QString& comment)
{
	bool ok;
	QString text = QInputDialog::getMultiLineText(this, tr("Edit move comment"),
						      tr("Comment:"), comment, &ok);
	if (ok && text != comment)
	{
		lockCurrentGame();
		PgnGame* pgn(m_tabs.at(m_tabBar->currentIndex()).m_pgn);
		PgnGame::MoveData md(pgn->moves().at(ply));
		md.comment = text;
		pgn->setMove(ply, md);
		unlockCurrentGame();

		m_moveList->setMove(ply, md.move, md.moveString, text);
	}
}

void MainWindow::copyFen()
{
	QClipboard* cb = CuteChessApplication::clipboard();
	QString fen(m_gameViewer->board()->fenString());
	if (!fen.isEmpty())
		cb->setText(fen);
}

void MainWindow::pasteFen()
{
	auto cb = CuteChessApplication::clipboard();
	if (cb->text().isEmpty())
		return;

	QString variant = m_game.isNull() || m_game->board() == nullptr ?
				"standard" : m_game->board()->variant();

	auto board = Chess::BoardFactory::create(variant);
	if (!board->setFenString(cb->text()))
	{
		QMessageBox msgBox(QMessageBox::Critical,
				   tr("FEN error"),
				   tr("Invalid FEN string for the \"%1\" variant:")
				   .arg(variant),
				   QMessageBox::Ok, this);
		msgBox.setInformativeText(cb->text());
		msgBox.exec();

		delete board;
		return;
	}
	auto game = new ChessGame(board, new PgnGame());
	game->setTimeControl(TimeControl("inf"));
	game->setStartingFen(cb->text());
	game->pause();

	connect(game, &ChessGame::initialized, this, &MainWindow::addGame);
	connect(game, &ChessGame::startFailed, this, &MainWindow::onGameStartFailed);

	CuteChessApplication::instance()->gameManager()->newGame(game,
		new HumanBuilder(CuteChessApplication::userName()),
		new HumanBuilder(CuteChessApplication::userName()));
}

void MainWindow::showAboutDialog()
{
	QString html;
	html += "<h3>" + QString("Cute Chess %1")
		.arg(CuteChessApplication::applicationVersion()) + "</h3>";
	html += "<p>" + tr("Using Qt version %1").arg(qVersion()) + "</p>";
	html += "<p>" + tr("Running on %1/%2").arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture()) + "</p>";
	html += "<p>" + tr("Copyright 2008-2020 "
			   "Cute Chess authors") + "</p>";
	html += "<p>" + tr("This is free software; see the source for copying "
			   "conditions. There is NO warranty; not even for "
			   "MERCHANTABILITY or FITNESS FOR A PARTICULAR "
			   "PURPOSE.") + "</p>";
	html += "<a href=\"http://cutechess.com\">cutechess.com</a><br>";

	QMessageBox::about(this, tr("About Cute Chess"), html);
}

void MainWindow::lockCurrentGame()
{
	if (m_game != nullptr)
		m_game->lockThread();
}

void MainWindow::unlockCurrentGame()
{
	if (m_game != nullptr)
		m_game->unlockThread();
}

bool MainWindow::save()
{
	if (m_currentFile.isEmpty())
		return saveAs();

	return saveGame(m_currentFile);
}

bool MainWindow::saveAs()
{
	const QString fileName = QFileDialog::getSaveFileName(
		this,
		tr("Save Game"),
		QString(),
		tr("Portable Game Notation (*.pgn);;All Files (*.*)"),
		nullptr,
		QFileDialog::DontConfirmOverwrite);
	if (fileName.isEmpty())
		return false;

	return saveGame(fileName);
}

bool MainWindow::saveGame(const QString& fileName)
{
	lockCurrentGame();
	bool ok = m_tabs.at(m_tabBar->currentIndex()).m_pgn->write(fileName);
	unlockCurrentGame();

	if (!ok)
		return false;

	m_currentFile = fileName;
	setWindowModified(false);

	return true;
}

void MainWindow::copyPgn()
{
	QString str("");
	QTextStream s(&str);
	PgnGame* pgn = m_tabs.at(m_tabBar->currentIndex()).m_pgn;
	if (pgn == nullptr)
		return;
	s << *pgn;

	QClipboard* cb = CuteChessApplication::clipboard();
	cb->setText(s.readAll());
}

void MainWindow::onGameManagerFinished()
{
	m_readyToClose = true;
	close();
}

void MainWindow::closeAllGames()
{
	auto app = CuteChessApplication::instance();
	app->closeDialogs();

	for (int i = m_tabs.size() - 1; i >= 0; i--)
		closeTab(i);

	if (m_tabs.isEmpty())
		app->gameManager()->finish();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
	if (m_readyToClose)
	{
		// This window's real, user-set geometry was already staged
		// below, the moment this shutdown was first requested -- i.e.
		// before askToSave()/closeAllGames() (or a tournament being
		// stopped) had any chance to run, hide docks, close tabs, and
		// shrink the window as a side effect of that teardown. Nothing
		// is (re-)staged here: by the time control reaches this point,
		// any such teardown has typically already happened, so calling
		// stageGeometryForShutdown() at this late point would capture
		// and save that shrunk size instead of the correct one. See
		// moveEvent()/resizeEvent() for the other half of this guard.
		//
		// Note staging only ever writes the value to memory; the write
		// to disk happens separately. See
		// CuteChessApplication::onAboutToQuit() for where and why.
		QMainWindow::closeEvent(event);
		return;
	}

	// This is "a shutdown request", in the sense the user cares about:
	// the window's close button, File > Quit, Ctrl+Q, etc. Record the
	// geometry right now, before anything below (askToSave(),
	// closeAllGames(), or a running tournament being stopped) gets a
	// chance to hide docks/close tabs and resize the window -- which
	// would otherwise silently overwrite this correct, user-set
	// geometry with whatever smaller size the window happens to end up
	// at once that teardown has run. Only the active window (or the
	// last one, if none is active) stages: during "Quit" or a
	// tournament finishing, several windows can each reach this point
	// within the same event-loop pass, and only the one the user was
	// actually looking at should be allowed to be remembered as "the"
	// saved position.
	bool isLastWindow =
		CuteChessApplication::instance()->gameWindows().size() <= 1;
	if (isActiveWindow() || isLastWindow)
		stageGeometryForShutdown();

	if (askToSave())
	{
		m_closing = true;

		if (m_stopTournamentAct->isEnabled())
			m_stopTournamentAct->trigger();
		else
			closeAllGames();
	}

	event->ignore();
}

bool MainWindow::askToSave()
{
	if (isWindowModified())
	{
		QMessageBox::StandardButton result;
		result = QMessageBox::warning(this, QApplication::applicationName(),
			tr("The game was modified.\nDo you want to save your changes?"),
				QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

		if (result == QMessageBox::Save)
			return save();
		else if (result == QMessageBox::Cancel)
			return false;
	}
	return true;
}

void MainWindow::adjudicateDraw()
{
	adjudicateGame(Chess::Side::NoSide);
}

void MainWindow::adjudicateWhiteWin()
{
	adjudicateGame(Chess::Side::White);
}

void MainWindow::adjudicateBlackWin()
{
	adjudicateGame(Chess::Side::Black);
}

void MainWindow::adjudicateGame(Chess::Side winner)
{
	if (!m_game)
		return;

	auto result = Chess::Result(Chess::Result::Adjudication,
				    winner,
				    tr("user decision"));
	QMetaObject::invokeMethod(m_game, "onAdjudication",
				  Qt::QueuedConnection,
				  Q_ARG(Chess::Result, result));
}

void MainWindow::resignGame()
{
	if (m_game.isNull() || m_game->isFinished())
		return;

	ChessPlayer * player = m_game->playerToMove();
	if (!player->isHuman())
	{
		player = m_game->playerToWait();
		if (!player->isHuman())
			return;
	}
	Chess::Side side = player->side();
	auto result = Chess::Result(Chess::Result::Resignation,
				    side.opposite());
	QMetaObject::invokeMethod(m_game, "onResignation",
				  Qt::QueuedConnection,
				  Q_ARG(Chess::Result, result));
}

QList<ChessGame*> MainWindow::pausableGames(int index) const
{
	QList<ChessGame*> games;
	if (index < 0 || index >= m_tabs.size())
		return games;

	const TabData& tab = m_tabs.at(index);
	Tournament* tournament = tab.m_tournament;

	if (!tournament)
	{
		// Standalone game (New Game / pasted FEN): just itself.
		if (!tab.m_game.isNull() && !tab.m_game->isFinished())
			games.append(tab.m_game);
		return games;
	}

	// Part of a tournament/match: every one of ITS games that is
	// currently running, across every tab -- a tournament can have
	// more than one game in progress at once (concurrency > 1), and
	// they don't all have to be the tab that's on screen right now.
	for (const TabData& t : m_tabs)
	{
		if (t.m_tournament == tournament
		&&  !t.m_game.isNull()
		&&  !t.m_game->isFinished())
			games.append(t.m_game);
	}
	return games;
}

void MainWindow::updatePauseResumeAction()
{
	QList<ChessGame*> games = pausableGames(m_tabBar->currentIndex());

	if (games.isEmpty())
	{
		m_pauseResumeAct->setEnabled(false);
		m_pauseResumeAct->setText(tr("&Pause"));
		return;
	}

	// If the match is a mix of paused and running games (possible for
	// a moment right after the button is clicked, before every game's
	// own thread has caught up), go by the first one; togglePauseResume()
	// always brings them all back into agreement anyway.
	bool paused = games.first()->isPaused();

	m_pauseResumeAct->setEnabled(true);
	m_pauseResumeAct->setText(paused ? tr("&Resume") : tr("&Pause"));
}

void MainWindow::togglePauseResume()
{
	int index = m_tabBar->currentIndex();
	QList<ChessGame*> games = pausableGames(index);
	if (games.isEmpty())
		return;

	Tournament* tournament = m_tabs.at(index).m_tournament;
	bool pausing = !games.first()->isPaused();

	// Each of these games lives on its own GameThread (see
	// game->moveToThread() in GameManager::onGameInitialized()), while
	// this slot runs in the GUI thread. That's fine: pause()/resume()
	// are explicitly safe to call directly from any thread (see their
	// doc comments in chessgame.h) -- the request becomes visible to
	// the game's own thread the instant the call below returns, which
	// is what lets that thread stop at the next safe point instead of
	// one move late, regardless of whatever's already ahead of it in
	// that thread's own event queue (such as an engine's just-arrived
	// move).
	for (ChessGame* game : games)
	{
		if (pausing)
			game->pause();
		else
			game->resume();
	}

	// Remember the whole match's paused state, not just the games
	// that happen to be running right now, so that a game which
	// starts later in the same match (the next round, or the next
	// slot to free up under a concurrency limit) also starts paused
	// -- see addGame() -- instead of the pause silently only having
	// applied to whichever games were already on screen when the
	// button was clicked.
	if (tournament)
	{
		if (pausing)
			m_pausedTournaments.insert(tournament);
		else
			m_pausedTournaments.remove(tournament);
	}

	updatePauseResumeAction();
}

void MainWindow::addDefaultWindowMenu()
{
	m_windowMenu->addAction(m_minimizeAct);
	m_windowMenu->addSeparator();
	m_windowMenu->addAction(m_showGameWallAct);
	m_windowMenu->addSeparator();
	m_windowMenu->addAction(m_showPreviousTabAct);
	m_windowMenu->addAction(m_showNextTabAct);
}
