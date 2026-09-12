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

#include "newtournamentdialog.h"
#include "ui_newtournamentdlg.h"
#include "dialoggeometry.h"

#include <QFileDialog>
#include <QSettings>
#include <QMenu>
#include <algorithm>

#include <board/boardfactory.h>
#include <enginemanager.h>
#include <enginebuilder.h>
#include <timecontrol.h>
#include <tournament.h>
#include <tournamentfactory.h>
#include <openingsuite.h>

#include "engineconfigurationmodel.h"
#include "engineconfigproxymodel.h"
#include "engineconfigurationdlg.h"
#include "timecontroldlg.h"
#include "engineselectiondlg.h"
#include "gamehistoryrecorder.h"

#if 0
#include <modeltest.h>
#endif

NewTournamentDialog::NewTournamentDialog(EngineManager* engineManager,
					 QWidget *parent)
	: QDialog(parent),
	  m_srcEngineManager(engineManager),
	  ui(new Ui::NewTournamentDialog)
{
	Q_ASSERT(engineManager != nullptr);
	ui->setupUi(this);
	restoreDialogGeometry(this, QStringLiteral("newtournamentdialog"));

	m_srcEnginesModel = new EngineConfigurationModel(engineManager, this);
	#if 0
	new ModelTest(m_srcEnginesModel, this);
	#endif

	m_proxyModel = new EngineConfigurationProxyModel(this);
	m_proxyModel->setSourceModel(m_srcEnginesModel);
	m_proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
	m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
	m_proxyModel->sort(0);
	m_proxyModel->setDynamicSortFilter(true);

	connect(ui->m_gameSettings, SIGNAL(variantChanged(QString)),
		this, SLOT(onVariantChanged(QString)));

	connect(ui->m_addEngineBtn, SIGNAL(clicked()),
		this, SLOT(addEngine()));
	connect(ui->m_removeEngineBtn, SIGNAL(clicked()),
		this, SLOT(removeEngine()));
	connect(ui->m_configureEngineBtn, &QToolButton::clicked, [=]()
	{
		configureEngine(ui->m_playersList->currentIndex());
	});
	connect(ui->m_moveEngineUpBtn, &QToolButton::clicked, [=]()
	{
		moveEngine(-1);
	});
	connect(ui->m_moveEngineDownBtn, &QToolButton::clicked, [=]()
	{
		moveEngine(1);
	});

	connect(ui->m_browsePgnoutBtn, &QPushButton::clicked, this, [=]()
	{
		auto dlg = new QFileDialog(this, tr("Select PGN output file"),
			QString(), tr("Portable Game Notation (*.pgn)"));
		connect(dlg, &QFileDialog::fileSelected, ui->m_pgnoutEdit, &QLineEdit::setText);
		dlg->setAttribute(Qt::WA_DeleteOnClose);
		dlg->setAcceptMode(QFileDialog::AcceptSave);
		dlg->open();
	});
	connect(ui->m_browseEpdoutBtn, &QPushButton::clicked, this, [=]()
	{
		auto dlg = new QFileDialog(this, tr("Select EPD output file"),
			QString(), tr("Extended Position Description (*.epd)"));
		connect(dlg, &QFileDialog::fileSelected, ui->m_epdoutEdit, &QLineEdit::setText);
		dlg->setAttribute(Qt::WA_DeleteOnClose);
		dlg->setAcceptMode(QFileDialog::AcceptSave);
		dlg->open();
	});
	connect(ui->m_browseMatchHistoryBtn, &QPushButton::clicked, this, [=]()
	{
		auto dlg = new QFileDialog(this, tr("Select match history PGN file"),
			QString(), tr("Portable Game Notation (*.pgn)"));
		connect(dlg, &QFileDialog::fileSelected, ui->m_matchHistoryEdit, &QLineEdit::setText);
		dlg->setAttribute(Qt::WA_DeleteOnClose);
		dlg->setAcceptMode(QFileDialog::AcceptSave);
		dlg->open();
	});

	// The history file field only means anything while match history
	// saving itself is switched on.
	ui->m_matchHistoryEdit->setEnabled(ui->m_saveMatchHistoryCheck->isChecked());
	ui->m_browseMatchHistoryBtn->setEnabled(ui->m_saveMatchHistoryCheck->isChecked());
	connect(ui->m_saveMatchHistoryCheck, &QCheckBox::toggled,
		ui->m_matchHistoryEdit, &QLineEdit::setEnabled);
	connect(ui->m_saveMatchHistoryCheck, &QCheckBox::toggled,
		ui->m_browseMatchHistoryBtn, &QPushButton::setEnabled);

	m_addedEnginesManager = new EngineManager(this);
	m_addedEnginesModel = new EngineConfigurationModel(
		m_addedEnginesManager, this);
	ui->m_playersList->setModel(m_addedEnginesModel);

	connect(ui->m_playersList->selectionModel(),
		SIGNAL(selectionChanged(QItemSelection, QItemSelection)),
		this, SLOT(onPlayerSelectionChanged(QItemSelection, QItemSelection)));
	connect(ui->m_playersList, SIGNAL(doubleClicked(QModelIndex)),
		this, SLOT(configureEngine(QModelIndex)));

	ui->m_playersList->setContextMenuPolicy(Qt::ContextMenuPolicy::CustomContextMenu);
	connect(ui->m_playersList, SIGNAL(customContextMenuRequested(const QPoint&)),
		this, SLOT(onContextMenuRequest()));

	ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
	connect(ui->m_gameSettings, &GameSettingsWidget::statusChanged, [=](bool ok)
	{
		if (ok)
			ok = canStart();
		ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(ok);
	});

	ui->m_gameSettings->onHumanCountChanged(0);
	onVariantChanged(ui->m_gameSettings->chessVariant());

	// Make changes made *inside this dialog* (book, book depth, time
	// control, etc.) persist to QSettings live, the same way the main
	// Settings dialog does. Combined with GameSettingsWidget's own
	// readSettings() (already called from its constructor above via
	// ui->setupUi -> GameSettingsWidget ctor), this means book file,
	// book depth and time control are remembered across tournaments
	// without any extra plumbing here.
	ui->m_gameSettings->enableSettingsUpdates();
	// Same deal for the tournament-settings panel (type, seed count,
	// games/encounter, rounds, delay, opening repetitions, recovery,
	// save-unfinished-games, swap sides, reverse schedule, result
	// format). TournamentSettingsWidget::readSettings() already ran
	// from its own constructor above, but without this call none of
	// those fields were ever written back to QSettings, so edits made
	// here were silently lost the moment the dialog closed.
	ui->m_tournamentSettings->enableSettingsUpdates();

	readSettings();
	restoreLastEngineList();

	connect(ui->buttonBox, &QDialogButtonBox::accepted,
		this, &NewTournamentDialog::writeSettings);
}

NewTournamentDialog::~NewTournamentDialog()
{
	delete ui;
}

void NewTournamentDialog::addEngineOnDblClick(const QModelIndex& index)
{
	const QListView* listView = ((QListView*)sender());
	const QModelIndex& idx = m_proxyModel->mapToSource(index);

	m_addedEnginesManager->addEngine(m_srcEngineManager->engineAt(idx.row()));
	m_timeControls << TimeControl();
	listView->selectionModel()->select(index, QItemSelectionModel::Deselect);

	QPushButton* button = ui->buttonBox->button(QDialogButtonBox::Ok);
	button->setEnabled(canStart());
}


void NewTournamentDialog::addEngine()
{
	EngineSelectionDialog dlg(m_proxyModel);
	connect(dlg.enginesList(), SIGNAL(doubleClicked(QModelIndex)), this,
		SLOT(addEngineOnDblClick(QModelIndex)));
	int value = dlg.exec();
	disconnect(dlg.enginesList(), SIGNAL(doubleClicked(QModelIndex)), this,
		   SLOT(addEngineOnDblClick(QModelIndex)));

	if (value != QDialog::Accepted)
		return;

	const QModelIndexList list(dlg.selectedRows());
	for (const QModelIndex& index : list)
	{
		m_addedEnginesManager->addEngine(m_srcEngineManager->engineAt(index.row()));
		m_timeControls << TimeControl();
	}

	QPushButton* button = ui->buttonBox->button(QDialogButtonBox::Ok);
	button->setEnabled(canStart());
}

void NewTournamentDialog::removeEngine()
{
	QList<QModelIndex> selected = ui->m_playersList->selectionModel()->selectedRows();

	// Can't use std::greater because operator> isn't implemented
	// for QModelIndex.
	std::sort(selected.begin(), selected.end(),
	[](const QModelIndex &a, const QModelIndex &b)
	{
		return b < a;
	});

	for (const QModelIndex& index : std::as_const(selected))
	{
		m_addedEnginesManager->removeEngineAt(index.row());
		m_timeControls.remove(index.row());
	}

	QPushButton* button = ui->buttonBox->button(QDialogButtonBox::Ok);
	button->setEnabled(canStart());
}

void NewTournamentDialog::configureEngine(const QModelIndex& index)
{
	EngineConfigurationDialog dlg(EngineConfigurationDialog::ConfigureEngine);

	int row = index.row();
	const EngineConfiguration& config = m_addedEnginesManager->engineAt(row);
	dlg.applyEngineInformation(config);

	QSet<QString> names = m_addedEnginesManager->engineNames();
	names.remove(config.name());
	dlg.setReservedNames(names);

	if (dlg.exec() == QDialog::Accepted)
		m_addedEnginesManager->updateEngineAt(row, dlg.engineConfiguration());

	QPushButton* button = ui->buttonBox->button(QDialogButtonBox::Ok);
	button->setEnabled(canStart());
}

void NewTournamentDialog::moveEngine(int offset)
{
	if (offset == 0)
		return;

	QModelIndex index(ui->m_playersList->currentIndex());
	int row1 = index.row();
	int row2 = row1 + offset;

	// It should be impossible for either row to be out of bounds,
	// but we'll check it explicitly, which makes the compiler happy.
	if (row1 < 0 || row1 >= m_timeControls.size())
	{
		qWarning("row1 out of bounds");
		return;
	}
	if (row2 < 0 || row2 >= m_timeControls.size())
	{
		qWarning("row2 out of bounds");
		return;
	}

	EngineConfiguration tmp(m_addedEnginesManager->engineAt(row1));
	m_addedEnginesManager->updateEngineAt(row1, m_addedEnginesManager->engineAt(row2));
	m_addedEnginesManager->updateEngineAt(row2, tmp);

	m_timeControls.swapItemsAt(row1, row2);

	ui->m_playersList->setCurrentIndex(index.sibling(row2, 0));
}

bool NewTournamentDialog::canStart() const
{
	if (!ui->m_gameSettings->isValid())
		return false;

	if (m_addedEnginesManager->engineCount() < 2)
		return false;

	QPushButton* button = ui->buttonBox->button(QDialogButtonBox::Ok);

	// check for duplicate configuration names
	if (m_addedEnginesManager->engineNames().count()
	!=  m_addedEnginesManager->engineCount())
	{
		button->setText(tr("Resolve Duplicates!"));
		return false;
	}
	button->setText("&OK");

	QString variant = ui->m_gameSettings->chessVariant();
	return m_addedEnginesManager->supportsVariant(variant);
}

void NewTournamentDialog::onVariantChanged(const QString& variant)
{
	m_proxyModel->setFilterVariant(variant);
	ui->m_addEngineBtn->setEnabled(m_proxyModel->rowCount() > 0);

	m_addedEnginesModel->setChessVariant(variant);
	QPushButton* button = ui->buttonBox->button(QDialogButtonBox::Ok);
	button->setEnabled(canStart());

	onPlayerSelectionChanged(QItemSelection(), QItemSelection());
}

void NewTournamentDialog::onPlayerSelectionChanged(const QItemSelection& selected,
						   const QItemSelection& deselected)
{
	Q_UNUSED(selected);
	Q_UNUSED(deselected);

	bool enable = ui->m_playersList->selectionModel()->hasSelection();
	ui->m_configureEngineBtn->setEnabled(enable);
	ui->m_removeEngineBtn->setEnabled(enable);

	int i = ui->m_playersList->currentIndex().row();
	ui->m_moveEngineUpBtn->setEnabled(enable && i > 0);
	ui->m_moveEngineDownBtn->setEnabled(enable && i < m_addedEnginesManager->engineCount() - 1);
}

void NewTournamentDialog::onContextMenuRequest()
{
	QList<QModelIndex> selected = ui->m_playersList->selectionModel()->selectedRows();
	if (selected.isEmpty())
		return;

	QMenu menu(ui->m_playersList);

	auto editTimeControlAct = menu.addAction(tr("Edit Time Control"));
	connect(editTimeControlAct, &QAction::triggered, this, [=]()
	{
		int i = selected.first().row();
		TimeControl tc {m_timeControls.at(i)};
		if (!tc.isValid())
			tc = ui->m_gameSettings->timeControl();

		auto dlg = new TimeControlDialog(tc, this);
		QString name {m_addedEnginesManager->engines().at(i).name()};
		if (selected.count() > 1)
			name.append(tr(" - %0 engines").arg(selected.count()));
		dlg->setWindowTitle(tr("Time Control - %0").arg(name));

		if (dlg->exec() == QDialog::Accepted)
			for (const QModelIndex& index: selected)
				m_timeControls[index.row()] = dlg->timeControl();
		delete dlg;
	});

	menu.exec(QCursor::pos());
}

Tournament* NewTournamentDialog::createTournament(GameManager* gameManager) const
{
	Q_ASSERT(gameManager != nullptr);
	auto ts = ui->m_tournamentSettings;

	auto t = TournamentFactory::create(
		ts->tournamentType(), gameManager, parent());

	t->setPgnCleanupEnabled(false);
	t->setName(ui->m_nameEdit->text());
	t->setSite(ui->m_siteEdit->text());
	t->setVariant(ui->m_gameSettings->chessVariant());
	t->setPgnOutput(ui->m_pgnoutEdit->text());
	t->setEpdOutput(ui->m_epdoutEdit->text());

	t->setSeedCount(ts->seedCount());
	t->setGamesPerEncounter(ts->gamesPerEncounter());
	if (t->canSetRoundMultiplier())
		t->setRoundMultiplier(ts->rounds());
	t->setStartDelay(ts->delayBetweenGames());

	t->setAdjudicator(ui->m_gameSettings->adjudicator());

	t->setOpeningSuite(ui->m_gameSettings->openingSuite());
	t->setOpeningDepth(ui->m_gameSettings->openingSuiteDepth());

	t->setOpeningBookOwnership(true);
	auto book = ui->m_gameSettings->openingBook();
	int bookDepth = ui->m_gameSettings->bookDepth();

	t->setOpeningRepetitions(ts->openingRepetitions());
	t->setRecoveryMode(ts->engineRecovery());
	t->setPgnWriteUnfinishedGames(ts->savingOfUnfinishedGames());
	t->setSwapSides(ts->swappingSides());
	t->setReverseSides(ts->reversingSchedule());
	t->setResultFormat(ts->resultFormat());

	bool isHourglass = ui->m_gameSettings->timeControl().isHourglass();

	const auto engines = m_addedEnginesManager->engines();
	for (int i = 0; i < engines.count(); i++)
	{
		EngineConfiguration config = engines.at(i);
		ui->m_gameSettings->applyEngineConfiguration(&config);
		TimeControl tc = m_timeControls.at(i);
		// Hourglass mode must be the same for all players
		tc.setHourglass(isHourglass);

		t->addPlayer(new EngineBuilder(config),
			     tc.isValid() ? tc : ui->m_gameSettings->timeControl(),
			     book,
			     bookDepth);
	}

	return t;
}

void NewTournamentDialog::readSettings()
{
	ui->m_siteEdit->setText(QSettings().value("pgn/site").toString());

	QString pgnName = ui->m_pgnoutEdit->text();
	if (pgnName.isEmpty())
	{
		pgnName = QSettings().value("tournament/default_pgn_output_file",
					    QString()).toString();
		ui->m_pgnoutEdit->setText(pgnName);
	}

	QString epdName = ui->m_epdoutEdit->text();
	if (epdName.isEmpty())
	{
		epdName = QSettings().value("tournament/default_epd_output_file",
					    QString()).toString();
		ui->m_epdoutEdit->setText(epdName);
	}

	// Whether finished games get appended to the rolling match-history
	// PGN file(s) (see GameHistoryRecorder), and which single file to
	// use instead of the default engine/human file pair, if any.
	ui->m_saveMatchHistoryCheck->setChecked(GameHistoryRecorder::isEnabled());
	ui->m_matchHistoryEdit->setText(GameHistoryRecorder::filePath());
}

// Requirement: remember which engines were added to the last tournament
// that was set up, and pre-populate them next time this dialog opens
// (instead of starting with an empty player list every time).
//
// LOG / TODO for a future session: this only restores engines that are
// still present (by name) in the user's overall engine configuration
// list. If an engine was renamed or removed since the last tournament,
// it is silently skipped rather than reported to the user -- a nicer
// version would surface a one-line notice ("2 of 4 previously used
// engines could not be found and were skipped").
void NewTournamentDialog::restoreLastEngineList()
{
	QStringList lastEngineNames = QSettings().value(
		"tournament/last_engine_names").toStringList();
	if (lastEngineNames.isEmpty())
		return;

	const auto available = m_srcEngineManager->engines();
	for (const QString& name : std::as_const(lastEngineNames))
	{
		for (const EngineConfiguration& config : available)
		{
			if (config.name() == name)
			{
				m_addedEnginesManager->addEngine(config);
				m_timeControls << TimeControl();
				break;
			}
		}
	}

	QPushButton* button = ui->buttonBox->button(QDialogButtonBox::Ok);
	button->setEnabled(canStart());
}

// Called when the dialog is accepted (tournament actually started).
// Persists the fields the user asked to have remembered between
// tournaments: engine names here; opening book file/depth, time
// control, and all tournament-settings-panel fields are already
// persisted live by GameSettingsWidget and TournamentSettingsWidget
// themselves (see the two enableSettingsUpdates() calls in the
// constructor).
void NewTournamentDialog::writeSettings()
{
	QStringList names;
	const auto engines = m_addedEnginesManager->engines();
	for (const EngineConfiguration& config : engines)
		names << config.name();

	QSettings().setValue("tournament/last_engine_names", names);

	GameHistoryRecorder::setEnabled(ui->m_saveMatchHistoryCheck->isChecked());
	GameHistoryRecorder::setFilePath(ui->m_matchHistoryEdit->text());
}
