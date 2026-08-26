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

#include "cutechessapp.h"

#include <QCoreApplication>
#include <QDir>
#include <QTime>
#include <QFileInfo>
#include <QSettings>
#include <QFontInfo>
#include <QColor>
#include <QPalette>

#include <mersenne.h>
#include <enginemanager.h>
#include <gamemanager.h>
#include <board/boardfactory.h>
#include <chessgame.h>
#include <timecontrol.h>
#include <humanbuilder.h>

#include "mainwindow.h"
#include "settingsdlg.h"
#include "tournamentresultsdlg.h"
#include "gamedatabasedlg.h"
#include "gamedatabasemanager.h"
#include "importprogressdlg.h"
#include "pgnimporter.h"
#include "gamewall.h"
#ifndef Q_OS_WIN32
#	include <sys/types.h>
#	include <pwd.h>
#endif


CuteChessApplication::CuteChessApplication(int& argc, char* argv[])
	: QApplication(argc, argv),
	  m_settingsDialog(nullptr),
	  m_tournamentResultsDialog(nullptr),
	  m_engineManager(nullptr),
	  m_gameManager(nullptr),
	  m_gameDatabaseManager(nullptr),
	  m_gameDatabaseDialog(nullptr),
	  m_gameWall(nullptr),
	  m_initialWindowCreated(false),
	  m_hasPendingMainWindowGeometry(false)
{
	Mersenne::initialize(QTime(0,0,0).msecsTo(QTime::currentTime()));

	// Set the application icon
	QIcon icon;
	icon.addFile(":/icons/cutechess_512x512.png");
	icon.addFile(":/icons/cutechess_256x256.png");
	icon.addFile(":/icons/cutechess_128x128.png");
	icon.addFile(":/icons/cutechess_64x64.png");
	icon.addFile(":/icons/cutechess_32x32.png");
	icon.addFile(":/icons/cutechess_24x24.png");
	icon.addFile(":/icons/cutechess_16x16.png");
	setWindowIcon(icon);

	setQuitOnLastWindowClosed(false);

	QCoreApplication::setOrganizationName("cutechess");
	QCoreApplication::setOrganizationDomain("cutechess.com");
	QCoreApplication::setApplicationName("cutechess");
	QCoreApplication::setApplicationVersion(CUTECHESS_VERSION);

	// Use Ini format on all platforms
	QSettings::setDefaultFormat(QSettings::IniFormat);

	// Load the engines
	engineManager()->loadEngines(configPath() + QLatin1String("/engines.json"));

	// Read the game database state
	gameDatabaseManager()->readState(configPath() + QLatin1String("/gamedb.bin"));

	connect(this, SIGNAL(lastWindowClosed()), this, SLOT(onLastWindowClosed()));
	connect(this, SIGNAL(aboutToQuit()), this, SLOT(onAboutToQuit()));

	applyCustomAppearance();
}

// Applies the user-requested global look: mid-cream window backgrounds,
// and headings/labels rendered bold and ~1.5px larger than the platform
// default.
//
// LOG: background colour used to be done with a stylesheet rule scoped
// to "QMainWindow, QDialog, QWidget#centralWidget". That missed most of
// the app's actual windows: the dockable panels (Moves, Tags, White's/
// Black's evaluation, Evaluation history, Engine Debug) are QDockWidgets,
// not QMainWindow/QDialog, so they -- and their floating/undocked state,
// which are real top-level windows -- stayed at the default background.
// Worse, the item views inside those docks (QTableView/QTreeView for
// Moves, Tags, the eval tables) and the EvalHistory QCustomPlot don't
// take their background from a stylesheet "background-color" rule at
// all: QAbstractItemView paints its viewport from QPalette::Base, and
// EvalHistory explicitly does
// `m_plot->setBackground(QApplication::palette().window())`
// (see evalhistory.cpp) -- i.e. it reads the *palette*, which the old
// code never touched, only the stylesheet. That's why those panels kept
// showing plain white regardless of the stylesheet rule.
//
// Fix: set the mid-cream colour on the application QPalette (Window and
// Base, plus a slightly darker AlternateBase so alternating table rows
// stay visible) instead of only via a stylesheet. Every widget that
// doesn't override its own palette -- QMainWindow, QDialog, QDockWidget
// (docked or floating), QTableView/QTreeView/QListView viewports, menus,
// combo/list popups, etc. -- inherits this automatically, and it's also
// what EvalHistory's QCustomPlot reads. The stylesheet is now only used
// for the bold/larger heading rules, which QSS can express fine.
//
// LOG / TODO for a future session: QSS can't express "current size +
// 1.5px" directly, since it has no relative unit and no access to
// per-widget inherited sizes at parse time -- it can only set an
// absolute font-size. What's done here is a reasonable approximation:
// we read the *application's* default font pixel size once at startup
// (via QFontInfo) and bake basePixelSize + 1.5, rounded, into the
// stylesheet as an absolute px value for QLabel / group box titles /
// dialog titles / header views / tab labels. This will drift from
// "current size + 1.5px" for any individual widget that already has a
// non-default font size set in code or in a .ui file (there are a
// handful of those, e.g. the monospace font in
// TournamentResultsDialog, which deliberately isn't touched here).
// A more precise version would walk the widget tree after each dialog
// is constructed and bump each heading/label's *own* pointSizeF by the
// mm-accurate equivalent of 1.5px for that widget's screen, rather than
// using one global absolute value.
void CuteChessApplication::applyCustomAppearance()
{
	const QColor midCream = boardBackgroundColor();
	const QColor midCreamAlternate = midCream.darker(105);

	// NOTE: only the background-family roles (Window/Base/AlternateBase/
	// Button) were ever set here. The foreground/text roles (WindowText,
	// Text, ButtonText) were left untouched, so they still come from
	// whatever the platform theme provides. On a session where the
	// platform theme's default text colour happens to be light (e.g. a
	// dark GTK/Qt platform theme), that leaves light/white text sitting
	// on this light cream background -- unreadable. Force a fixed dark
	// text colour on all three foreground roles so the theme is always
	// self-consistent regardless of the platform default.
	const QColor darkText(0x20, 0x20, 0x20);

	QPalette pal = palette();
	pal.setColor(QPalette::Window, midCream);
	pal.setColor(QPalette::Base, midCream);
	pal.setColor(QPalette::AlternateBase, midCreamAlternate);
	pal.setColor(QPalette::Button, midCream);
	pal.setColor(QPalette::WindowText, darkText);
	pal.setColor(QPalette::Text, darkText);
	pal.setColor(QPalette::ButtonText, darkText);
	setPalette(pal);

	QFontInfo baseInfo(font());
	int basePx = baseInfo.pixelSize();
	if (basePx <= 0)
		basePx = qRound(baseInfo.pointSizeF() * 96.0 / 72.0);
	int headingPx = basePx + 2; // ~1.5px, rounded up to a whole px

	QString sheet = QString(
		// Belt-and-braces alongside the palette change above: some
		// styles/widgets read text colour from the stylesheet in
		// preference to the palette, so state it here too, in
		// addition to (not instead of) the palette fix, to guarantee
		// legible bold black text regardless of platform theme.
		"QWidget {"
		"  color: #202020;"
		"  font-weight: bold;"
		"}"
		"QLabel {"
		"  font-weight: bold;"
		"  font-size: %1px;"
		"}"
		"QGroupBox::title {"
		"  font-weight: bold;"
		"  font-size: %1px;"
		"}"
		"QHeaderView::section {"
		"  font-weight: bold;"
		"  font-size: %1px;"
		"}"
		"QTabBar::tab {"
		"  font-weight: bold;"
		"  font-size: %1px;"
		"}"
	).arg(QString::number(headingPx));

	setStyleSheet(sheet);
}

QColor CuteChessApplication::boardBackgroundColor() const
{
	// Historical default: the mid-cream #EFE6C8 that used to be
	// hard-coded in three places (here, GraphicsBoard's wall
	// colour, and BoardView's background brush). Now user
	// configurable from the Settings dialog's General tab.
	QSettings s;
	int r = s.value("ui/board_bg_color_r", 0xef).toInt();
	int g = s.value("ui/board_bg_color_g", 0xe6).toInt();
	int b = s.value("ui/board_bg_color_b", 0xc8).toInt();
	return QColor(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
}

void CuteChessApplication::setBoardBackgroundColor(const QColor& color)
{
	QSettings s;
	s.setValue("ui/board_bg_color_r", color.red());
	s.setValue("ui/board_bg_color_g", color.green());
	s.setValue("ui/board_bg_color_b", color.blue());

	// Re-derives the palette/stylesheet from the new colour.
	applyCustomAppearance();

	emit boardBackgroundColorChanged(color);
}

QColor CuteChessApplication::lightSquareColor() const
{
	// Historical default: the light tan #FFCE9E that used to be
	// hard-coded in GraphicsBoard's constructor. Now user
	// configurable from the Settings dialog's General tab.
	QSettings s;
	int r = s.value("ui/board_light_square_color_r", 0xff).toInt();
	int g = s.value("ui/board_light_square_color_g", 0xce).toInt();
	int b = s.value("ui/board_light_square_color_b", 0x9e).toInt();
	return QColor(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
}

QColor CuteChessApplication::darkSquareColor() const
{
	// Historical default: the brown #D18B47 that used to be
	// hard-coded in GraphicsBoard's constructor. Now user
	// configurable from the Settings dialog's General tab.
	QSettings s;
	int r = s.value("ui/board_dark_square_color_r", 0xd1).toInt();
	int g = s.value("ui/board_dark_square_color_g", 0x8b).toInt();
	int b = s.value("ui/board_dark_square_color_b", 0x47).toInt();
	return QColor(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
}

void CuteChessApplication::setLightSquareColor(const QColor& color)
{
	QSettings s;
	s.setValue("ui/board_light_square_color_r", color.red());
	s.setValue("ui/board_light_square_color_g", color.green());
	s.setValue("ui/board_light_square_color_b", color.blue());

	emit lightSquareColorChanged(color);
}

void CuteChessApplication::setDarkSquareColor(const QColor& color)
{
	QSettings s;
	s.setValue("ui/board_dark_square_color_r", color.red());
	s.setValue("ui/board_dark_square_color_g", color.green());
	s.setValue("ui/board_dark_square_color_b", color.blue());

	emit darkSquareColorChanged(color);
}

CuteChessApplication::~CuteChessApplication()
{
	delete m_gameDatabaseDialog;
	delete m_settingsDialog;
	delete m_tournamentResultsDialog;
	delete m_gameWall;
}

CuteChessApplication* CuteChessApplication::instance()
{
	return static_cast<CuteChessApplication*>(QApplication::instance());
}

QString CuteChessApplication::userName()
{
	#ifdef Q_OS_WIN32
	return qgetenv("USERNAME");
	#else
	if (QSettings().value("ui/use_full_user_name", true).toBool())
	{
		auto pwd = getpwnam(qgetenv("USER"));
		if (pwd != nullptr)
			return QString(pwd->pw_gecos).split(',')[0];
	}
	return qgetenv("USER");
	#endif
}

QString CuteChessApplication::configPath()
{
	// We want to have the exact same config path in "gui" and
	// "cli" applications so that they can share resources
	QSettings settings;
	QFileInfo fi(settings.fileName());
	QDir dir(fi.absolutePath());

	if (!dir.exists())
		dir.mkpath(fi.absolutePath());

	return fi.absolutePath();
}

EngineManager* CuteChessApplication::engineManager()
{
	if (m_engineManager == nullptr)
		m_engineManager = new EngineManager(this);

	return m_engineManager;
}

GameManager* CuteChessApplication::gameManager()
{
	if (m_gameManager == nullptr)
	{
		m_gameManager = new GameManager(this);
		int concurrency = QSettings()
			.value("tournament/concurrency", 1).toInt();
		m_gameManager->setConcurrency(concurrency);
	}

	return m_gameManager;
}

QList<MainWindow*> CuteChessApplication::gameWindows()
{
	m_gameWindows.removeAll(nullptr);

	QList<MainWindow*> gameWindowList;
	const auto gameWindows = m_gameWindows;
	for (const auto& window : gameWindows)
		gameWindowList << window.data();

	return gameWindowList;
}

MainWindow* CuteChessApplication::newGameWindow(ChessGame* game)
{
	MainWindow* mainWindow = new MainWindow(game);
	m_gameWindows.prepend(mainWindow);
	mainWindow->show();
	m_initialWindowCreated = true;

	return mainWindow;
}

void CuteChessApplication::newDefaultGame()
{
	// default game is a human versus human game using standard variant and
	// infinite time control
	ChessGame* game = new ChessGame(Chess::BoardFactory::create("standard"),
		new PgnGame());

	game->setTimeControl(TimeControl("inf"));
	game->pause();

	connect(game, SIGNAL(started(ChessGame*)),
		this, SLOT(newGameWindow(ChessGame*)));

	gameManager()->newGame(game,
			       new HumanBuilder(userName()),
			       new HumanBuilder(userName()));
}

void CuteChessApplication::showGameWindow(int index)
{
	auto gameWindow = m_gameWindows.at(index);
	gameWindow->activateWindow();
	gameWindow->raise();
}

GameDatabaseManager* CuteChessApplication::gameDatabaseManager()
{
	if (m_gameDatabaseManager == nullptr)
		m_gameDatabaseManager = new GameDatabaseManager(this);

	return m_gameDatabaseManager;
}

void CuteChessApplication::showSettingsDialog()
{
	if (m_settingsDialog == nullptr)
		m_settingsDialog = new SettingsDialog();

	showDialog(m_settingsDialog);
}

void CuteChessApplication::showTournamentResultsDialog()
{
	showDialog(tournamentResultsDialog());
}

TournamentResultsDialog*CuteChessApplication::tournamentResultsDialog()
{
	if (m_tournamentResultsDialog == nullptr)
		m_tournamentResultsDialog = new TournamentResultsDialog();

	return m_tournamentResultsDialog;
}

void CuteChessApplication::showGameDatabaseDialog()
{
	if (m_gameDatabaseDialog == nullptr)
		m_gameDatabaseDialog = new GameDatabaseDialog(gameDatabaseManager());

	showDialog(m_gameDatabaseDialog);
}

void CuteChessApplication::showGameWall()
{
	if (m_gameWall == nullptr)
	{
		m_gameWall = new GameWall(gameManager());
		auto flags = m_gameWall->windowFlags();
		m_gameWall->setWindowFlags(flags | Qt::Window);
		m_gameWall->setAttribute(Qt::WA_DeleteOnClose, true);
		m_gameWall->setWindowTitle(tr("Active Games"));
	}

	showDialog(m_gameWall);
}

void CuteChessApplication::onQuitAction()
{
	closeDialogs();
	closeAllWindows();
}

void CuteChessApplication::onLastWindowClosed()
{
	if (!m_initialWindowCreated)
		return;

	if (m_gameManager != nullptr)
	{
		connect(m_gameManager, SIGNAL(finished()), this, SLOT(quit()));
		m_gameManager->finish();
	}
	else
		quit();
}

void CuteChessApplication::recordMainWindowGeometry(const QByteArray& geometry,
						      const QByteArray& windowState)
{
	m_pendingMainWindowGeometry = geometry;
	m_pendingMainWindowState = windowState;
	m_hasPendingMainWindowGeometry = true;
}

void CuteChessApplication::onAboutToQuit()
{
	if (gameDatabaseManager()->isModified())
		gameDatabaseManager()->writeState(configPath() + QLatin1String("/gamedb.bin"));

	// This is the very last point in the application's life: every
	// window has already closed (aboutToQuit() only fires once the
	// event loop is unwinding for the final time) and nothing further
	// will run afterwards that could clobber what we're about to write.
	//
	// Earlier attempts wrote the main window's geometry to QSettings
	// from MainWindow::closeEvent() instead. That write landed on disk
	// fine, but closeEvent() does not run at the end of the
	// application's life -- it runs while windows are still being torn
	// down, with the event loop still very much alive and other
	// deferred/queued slots (tournament/game-manager shutdown, further
	// closeEvent()s on sibling windows, etc.) still to run afterwards.
	// If any of that later code touched a QSettings object for
	// anything -- even something unrelated to window geometry -- Qt's
	// shared per-file settings cache meant its eventual sync() could
	// silently carry stale (pre-close) geometry back over the value we
	// had just written, effectively resetting the saved position.
	//
	// By only ever staging the geometry in memory (see
	// recordMainWindowGeometry(), called from MainWindow as it closes)
	// and performing the actual write here, there is no longer any
	// window in which something else can re-save stale settings after
	// we've saved the real ones.
	if (m_hasPendingMainWindowGeometry)
	{
		QSettings s;
		s.beginGroup("ui");
		s.beginGroup("mainwindow");

		s.setValue("geometry", m_pendingMainWindowGeometry);
		s.setValue("window_state", m_pendingMainWindowState);

		s.endGroup();
		s.endGroup();
		s.sync();
	}
}

void CuteChessApplication::showDialog(QWidget* dlg)
{
	Q_ASSERT(dlg != nullptr);

	if (dlg->isMinimized())
		dlg->showNormal();
	else
		dlg->show();

	dlg->raise();
	dlg->activateWindow();
}

void CuteChessApplication::closeDialogs()
{
	if (m_tournamentResultsDialog)
		m_tournamentResultsDialog->close();
	if (m_gameDatabaseDialog)
		m_gameDatabaseDialog->close();
	if (m_settingsDialog)
		m_settingsDialog->close();
	if (m_gameWall)
		m_gameWall->close();
}
