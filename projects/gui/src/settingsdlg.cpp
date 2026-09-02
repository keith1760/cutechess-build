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

#include "settingsdlg.h"
#include "ui_settingsdlg.h"
#include "dialoggeometry.h"
#include <QShowEvent>
#include <QSettings>
#include <QFileDialog>
#include <QColorDialog>
#include <QLabel>
#include <gamemanager.h>
#include "cutechessapp.h"

SettingsDialog::SettingsDialog(QWidget* parent)
	: QDialog(parent),
	  ui(new Ui::SettingsDialog),
	  m_updatingBoardColorControls(false)
{
	ui->setupUi(this);
	restoreDialogGeometry(this, QStringLiteral("settingsdialog"));
	ui->m_gameSettings->enableSplitTimeControls(true);

	readSettings();

	connect(ui->m_highlightLegalMovesCheck, &QCheckBox::toggled,
		this, [=](bool checked)
	{
		QSettings().setValue("ui/highlight_legal_moves", checked);
	});

	connect(ui->m_showMoveArrowsCheck, &QCheckBox::toggled,
		this, [=](bool checked)
	{
		QSettings().setValue("ui/show_move_arrows", checked);
	});

	connect(ui->m_closeUnusedInitialTabCheck, &QCheckBox::toggled,
		this, [=](bool checked)
	{
		QSettings().setValue("ui/close_unused_initial_tab", checked);
	});

	// Show/hide board coordinates: persisted and pushed straight
	// through to CuteChessApplication so any open boards resize live
	// (turning coordinates off enlarges the board, and the pieces on
	// it, to fill the reclaimed space).
	connect(ui->m_showBoardCoordinatesCheck, &QCheckBox::toggled,
		this, [=](bool checked)
	{
		CuteChessApplication::instance()->setShowBoardCoordinates(checked);
	});

	connect(ui->m_useFullUserNameCheck, &QCheckBox::toggled,
		this, [=](bool checked)
	{
		QSettings().setValue("ui/use_full_user_name", checked);
	});

	connect(ui->m_playersSidesOnClocksCheck, &QCheckBox::toggled,
		this, [=](bool checked)
	{
		QSettings().setValue("ui/display_players_sides_on_clocks", checked);
	});

	connect(ui->m_autoFlipBoardForHumanGamesCheck, &QCheckBox::toggled,
		[=](bool checked)
	{
		QSettings().setValue("ui/auto_flip_board_for_human_games", checked);
	});

	connect(ui->m_keepFirstNamedEngineAtBottomCheck, &QCheckBox::toggled,
		[=](bool checked)
	{
		QSettings().setValue("ui/keep_first_named_engine_at_bottom", checked);
	});

	connect(ui->m_humanCanPlayAfterTimeoutCheck, &QCheckBox::toggled,
		[=](bool checked)
	{
		QSettings().setValue("games/human_can_play_after_timeout",
				      checked);
	});

	connect(ui->m_moveAnimationSpin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
		this, [=](int value)
	{
		QSettings().setValue("ui/move_animation_duration", value);
	});

	connect(ui->m_concurrencySpin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
		this, [=](int value)
	{
		QSettings().setValue("tournament/concurrency", value);
		CuteChessApplication::instance()->gameManager()->setConcurrency(value);
	});

	connect(ui->m_siteEdit, &QLineEdit::textChanged,
		[=](const QString& site)
	{
		QSettings().setValue("pgn/site", site);
	});

	connect(ui->m_defaultPgnOutFileEdit, &QLineEdit::textChanged,
		[=](const QString& defaultPgnFile)
	{
		QSettings().setValue("games/default_pgn_output_file", defaultPgnFile);
	});

	connect(ui->m_tbPathEdit, &QLineEdit::textChanged,
		[=](const QString& tbPath)
	{
		QSettings().setValue("ui/tb_path", tbPath);
	});

	connect(ui->m_tournamentDefaultPgnOutFileEdit, &QLineEdit::textChanged,
		[=](const QString& tourFile)
	{
		QSettings().setValue("tournament/default_pgn_output_file", tourFile);
	});

	connect(ui->m_tournamentDefaultEpdOutFileEdit, &QLineEdit::textChanged,
		[=](const QString& tourEpdFile)
	{
		QSettings().setValue("tournament/default_epd_output_file", tourEpdFile);
	});

	connect(ui->m_browseTbPathBtn, &QPushButton::clicked,
		this, &SettingsDialog::browseTbPath);
	connect(ui->m_defaultPgnOutFileBtn, &QPushButton::clicked,
		this, &SettingsDialog::browseDefaultPgnOutFile);
	connect(ui->m_tournamentDefaultPgnOutFileBtn, &QPushButton::clicked,
		this, &SettingsDialog::browseTournamentDefaultPgnOutFile);
	connect(ui->m_tournamentDefaultEpdOutFileBtn, &QPushButton::clicked,
		this, &SettingsDialog::browseTournamentDefaultEpdOutFile);

	// Board background color: keep each slider and its spinbox in
	// sync with each other, and push any change straight through
	// to CuteChessApplication so open boards recolor live.
	connect(ui->m_redSlider, &QSlider::valueChanged,
		ui->m_redSpin, &QSpinBox::setValue);
	connect(ui->m_redSpin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
		ui->m_redSlider, &QSlider::setValue);
	connect(ui->m_greenSlider, &QSlider::valueChanged,
		ui->m_greenSpin, &QSpinBox::setValue);
	connect(ui->m_greenSpin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
		ui->m_greenSlider, &QSlider::setValue);
	connect(ui->m_blueSlider, &QSlider::valueChanged,
		ui->m_blueSpin, &QSpinBox::setValue);
	connect(ui->m_blueSpin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
		ui->m_blueSlider, &QSlider::setValue);

	connect(ui->m_redSlider, &QSlider::valueChanged,
		this, &SettingsDialog::onBoardColorComponentChanged);
	connect(ui->m_greenSlider, &QSlider::valueChanged,
		this, &SettingsDialog::onBoardColorComponentChanged);
	connect(ui->m_blueSlider, &QSlider::valueChanged,
		this, &SettingsDialog::onBoardColorComponentChanged);

	connect(ui->m_resetBoardColorBtn, &QPushButton::clicked,
		this, &SettingsDialog::resetBoardColor);

	// Light/dark square colors: each swatch opens a colour picker
	// and pushes the choice straight through to CuteChessApplication
	// so any open boards recolor live.
	connect(ui->m_chooseLightSquareBtn, &QPushButton::clicked,
		this, &SettingsDialog::chooseLightSquareColor);
	connect(ui->m_chooseDarkSquareBtn, &QPushButton::clicked,
		this, &SettingsDialog::chooseDarkSquareColor);
	connect(ui->m_resetSquareColorsBtn, &QPushButton::clicked,
		this, &SettingsDialog::resetSquareColors);

	ui->m_gameSettings->onHumanCountChanged(0);
	ui->m_gameSettings->enableSettingsUpdates();
	ui->m_tournamentSettings->enableSettingsUpdates();
}

SettingsDialog::~SettingsDialog()
{
	delete ui;
}

void SettingsDialog::closeEvent(QCloseEvent* event)
{
	if (ui->m_engineManagementWidget->hasConfigChanged())
		ui->m_engineManagementWidget->saveConfig();

	QDialog::closeEvent(event);
}

void SettingsDialog::browseTbPath()
{
	auto dlg = new QFileDialog(
		this, tr("Choose Directory"),
		ui->m_tbPathEdit->text());
	dlg->setFileMode(QFileDialog::Directory);
	dlg->setOption(QFileDialog::ShowDirsOnly);
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->setAcceptMode(QFileDialog::AcceptOpen);

	connect(dlg, &QFileDialog::fileSelected, [=](const QString& dir)
	{
		ui->m_tbPathEdit->setText(dir);
		QSettings().setValue("ui/tb_path", dir);
	});
	dlg->open();
}

void SettingsDialog::browseDefaultPgnOutFile()
{
	auto dlg = new QFileDialog(
		this, tr("Select PGN output file"),
		QString(),
		tr("Portable Game Notation (*.pgn)"));
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->setAcceptMode(QFileDialog::AcceptSave);
	connect(dlg,
		&QFileDialog::fileSelected,
		ui->m_defaultPgnOutFileEdit,
		&QLineEdit::setText);
	dlg->open();
}

void SettingsDialog::browseTournamentDefaultPgnOutFile()
{
	auto dlg = new QFileDialog(
		this, tr("Select PGN output file"),
		QString(),
		tr("Portable Game Notation (*.pgn)"));
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->setAcceptMode(QFileDialog::AcceptSave);
	connect(dlg,
		&QFileDialog::fileSelected,
		ui->m_tournamentDefaultPgnOutFileEdit,
		&QLineEdit::setText);
	dlg->open();
}

void SettingsDialog::browseTournamentDefaultEpdOutFile()
{
	auto dlg = new QFileDialog(
		this, tr("Select EPD output file"),
		QString(),
		tr("Extended Position Description (*.epd)"));
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->setAcceptMode(QFileDialog::AcceptSave);
	connect(dlg,
		&QFileDialog::fileSelected,
		ui->m_tournamentDefaultEpdOutFileEdit,
		&QLineEdit::setText);
	dlg->open();
}

void SettingsDialog::updateBoardColorPreview(const QColor& color)
{
	QPalette pal = ui->m_boardColorSwatch->palette();
	pal.setColor(QPalette::Window, color);
	ui->m_boardColorSwatch->setPalette(pal);
}

void SettingsDialog::applyBoardColor(const QColor& color)
{
	updateBoardColorPreview(color);
	CuteChessApplication::instance()->setBoardBackgroundColor(color);
}

void SettingsDialog::onBoardColorComponentChanged()
{
	// Guard against the programmatic setValue() calls made by
	// readSettings()/resetBoardColor() re-entering this slot and
	// writing back out the value we're still in the middle of
	// loading.
	if (m_updatingBoardColorControls)
		return;

	QColor color(ui->m_redSlider->value(),
		     ui->m_greenSlider->value(),
		     ui->m_blueSlider->value());
	applyBoardColor(color);
}

void SettingsDialog::resetBoardColor()
{
	const QColor defaultColor(0xef, 0xe6, 0xc8);

	m_updatingBoardColorControls = true;
	ui->m_redSlider->setValue(defaultColor.red());
	ui->m_greenSlider->setValue(defaultColor.green());
	ui->m_blueSlider->setValue(defaultColor.blue());
	m_updatingBoardColorControls = false;

	applyBoardColor(defaultColor);
}

void SettingsDialog::updateSquareColorPreview(QLabel* swatch, const QColor& color)
{
	QPalette pal = swatch->palette();
	pal.setColor(QPalette::Window, color);
	swatch->setPalette(pal);
}

void SettingsDialog::chooseLightSquareColor()
{
	const QColor current =
		CuteChessApplication::instance()->lightSquareColor();
	const QColor color = QColorDialog::getColor(
		current, this, tr("Choose light square color"));
	if (!color.isValid())
		return;

	updateSquareColorPreview(ui->m_lightSquareSwatch, color);
	CuteChessApplication::instance()->setLightSquareColor(color);
}

void SettingsDialog::chooseDarkSquareColor()
{
	const QColor current =
		CuteChessApplication::instance()->darkSquareColor();
	const QColor color = QColorDialog::getColor(
		current, this, tr("Choose dark square color"));
	if (!color.isValid())
		return;

	updateSquareColorPreview(ui->m_darkSquareSwatch, color);
	CuteChessApplication::instance()->setDarkSquareColor(color);
}

void SettingsDialog::resetSquareColors()
{
	const QColor defaultLight(0xff, 0xce, 0x9e);
	const QColor defaultDark(0xd1, 0x8b, 0x47);

	updateSquareColorPreview(ui->m_lightSquareSwatch, defaultLight);
	updateSquareColorPreview(ui->m_darkSquareSwatch, defaultDark);
	CuteChessApplication::instance()->setLightSquareColor(defaultLight);
	CuteChessApplication::instance()->setDarkSquareColor(defaultDark);
}

void SettingsDialog::readSettings()
{
	QSettings s;

	s.beginGroup("ui");
	ui->m_highlightLegalMovesCheck->setChecked(
		s.value("highlight_legal_moves", true).toBool());
	ui->m_showMoveArrowsCheck->setChecked(
		s.value("show_move_arrows", true).toBool());
	ui->m_closeUnusedInitialTabCheck->setChecked(
		s.value("close_unused_initial_tab", true).toBool());
	ui->m_showBoardCoordinatesCheck->setChecked(
		s.value("show_board_coordinates", true).toBool());
	ui->m_useFullUserNameCheck->setChecked(
		s.value("use_full_user_name", true).toBool());
	ui->m_playersSidesOnClocksCheck->setChecked(
		s.value("display_players_sides_on_clocks", false).toBool());
	ui->m_autoFlipBoardForHumanGamesCheck->setChecked(
		s.value("auto_flip_board_for_human_games", false).toBool());
	ui->m_keepFirstNamedEngineAtBottomCheck->setChecked(
		s.value("keep_first_named_engine_at_bottom", false).toBool());
	ui->m_tbPathEdit->setText(s.value("tb_path").toString());
	ui->m_moveAnimationSpin->setValue(
		s.value("move_animation_duration", 300).toInt());
	s.endGroup();

	const QColor boardColor =
		CuteChessApplication::instance()->boardBackgroundColor();
	m_updatingBoardColorControls = true;
	ui->m_redSlider->setValue(boardColor.red());
	ui->m_greenSlider->setValue(boardColor.green());
	ui->m_blueSlider->setValue(boardColor.blue());
	m_updatingBoardColorControls = false;
	updateBoardColorPreview(boardColor);

	updateSquareColorPreview(ui->m_lightSquareSwatch,
		CuteChessApplication::instance()->lightSquareColor());
	updateSquareColorPreview(ui->m_darkSquareSwatch,
		CuteChessApplication::instance()->darkSquareColor());

	s.beginGroup("pgn");
	ui->m_siteEdit->setText(s.value("site").toString());
	s.endGroup();

	s.beginGroup("games");
	ui->m_humanCanPlayAfterTimeoutCheck
		->setChecked(s.value("human_can_play_after_timeout", true).toBool());
	ui->m_defaultPgnOutFileEdit
		->setText(s.value("default_pgn_output_file").toString());
	s.endGroup();

	s.beginGroup("tournament");
	ui->m_tournamentDefaultPgnOutFileEdit
		->setText(s.value("default_pgn_output_file").toString());
	ui->m_tournamentDefaultEpdOutFileEdit
		->setText(s.value("default_epd_output_file").toString());
	ui->m_concurrencySpin->setValue(s.value("concurrency", 1).toInt());
	s.endGroup();
}
