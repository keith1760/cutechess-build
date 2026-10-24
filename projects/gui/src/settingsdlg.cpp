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
#include "scrollhelper.h"
#include <QShowEvent>
#include <QSettings>

#include <uciengine.h>
#include <QFileDialog>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <gamemanager.h>
#include "cutechessapp.h"

SettingsDialog::SettingsDialog(QWidget* parent)
	: QDialog(parent),
	  ui(new Ui::SettingsDialog),
	  m_updatingBoardColorControls(false),
	  m_updatingSquareColorControls(false)
{
	ui->setupUi(this);
	ScrollHelper::makeTabsScrollable(ui->tabWidget);
	ScrollHelper::fitToScreen(this);
	restoreDialogGeometry(this, QStringLiteral("settingsdialog"));
	ui->m_gameSettings->enableSplitTimeControls(true);

	// "Branch time per move": how long the engines think per move in a
	// branch (sideline) of an engine-vs-engine game, 1 to 60 seconds.
	// It used to be a slider under the board; the value is the same
	// setting as before (ui/sideline_move_ms, in ms; default 5 s) and
	// is read by GameViewer::onBoardUserMove() when the branch starts.
	if (QVBoxLayout* gamesLayout =
		qobject_cast<QVBoxLayout*>(ui->m_gamesTab->layout()))
	{
		QGroupBox* branchBox = new QGroupBox(tr("Branch"), ui->m_gamesTab);
		QHBoxLayout* branchLayout = new QHBoxLayout(branchBox);
		QLabel* caption = new QLabel(tr("Branch time per move:"), branchBox);
		QSlider* slider = new QSlider(Qt::Horizontal, branchBox);
		QLabel* valueLabel = new QLabel(branchBox);
		slider->setRange(1, 60);
		slider->setSingleStep(1);
		slider->setPageStep(5);
		slider->setTickPosition(QSlider::TicksBelow);
		slider->setTickInterval(5);
		slider->setToolTip(tr("How long the engines think per move in a "
			"branch (1 to 60 seconds)"));
		valueLabel->setMinimumWidth(40);
		valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		branchLayout->addWidget(caption);
		branchLayout->addWidget(slider, 1);
		branchLayout->addWidget(valueLabel);

		const int savedMs =
			QSettings().value("ui/sideline_move_ms", 5000).toInt();
		slider->setValue(qBound(1, (savedMs + 500) / 1000, 60));
		valueLabel->setText(tr("%1 s").arg(slider->value()));
		connect(slider, &QSlider::valueChanged, this, [=](int seconds)
		{
			valueLabel->setText(tr("%1 s").arg(seconds));
			QSettings().setValue("ui/sideline_move_ms", seconds * 1000);
		});
		gamesLayout->addWidget(branchBox);
	}

	readSettings();

	connect(ui->m_boldTextCheck, &QCheckBox::toggled,
		this, [=](bool checked)
	{
		// Ticked = normal-weight black text, so bold is the opposite.
		CuteChessApplication::instance()->setBoldTextEnabled(!checked);
	});

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

	connect(ui->m_uciInputFilterCheck, &QCheckBox::toggled,
		this, [=](bool checked)
	{
		QSettings().setValue("engines/uci_input_filter", checked);
		UciEngine::setInputFilterEnabled(checked);
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

	connect(ui->m_evalBarBlackAtBottomCheck, &QCheckBox::toggled,
		[=](bool checked)
	{
		QSettings().setValue("ui/eval_bar_black_at_bottom", checked);
		CuteChessApplication::instance()->setEvalBarBlackAtBottom(checked);
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

	// Light/dark square and piece colors: each one has Red/Green/Blue
	// sliders with a spin box on the right of each slider, the same as
	// the board background color. Slider and spin box are kept in sync,
	// and any change is pushed straight through to CuteChessApplication
	// so any open boards recolor live.
	for (ColorRole role : { LightSquare, DarkSquare, LightPiece, DarkPiece })
	{
		const ColorControls cc = colorControls(role);
		for (int i = 0; i < 3; i++)
		{
			QSlider* slider = cc.slider[i];
			QSpinBox* spin = cc.spin[i];
			connect(slider, &QSlider::valueChanged,
				spin, &QSpinBox::setValue);
			connect(spin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
				slider, &QSlider::setValue);
			connect(slider, &QSlider::valueChanged,
				this, [this, role]() { onColorComponentChanged(role); });
		}
	}
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
	ui->m_redSpin->setValue(defaultColor.red());
	ui->m_greenSpin->setValue(defaultColor.green());
	ui->m_blueSpin->setValue(defaultColor.blue());
	m_updatingBoardColorControls = false;

	applyBoardColor(defaultColor);
}

void SettingsDialog::updateSquareColorPreview(QLabel* swatch, const QColor& color)
{
	QPalette pal = swatch->palette();
	pal.setColor(QPalette::Window, color);
	swatch->setPalette(pal);
}

SettingsDialog::ColorControls SettingsDialog::colorControls(ColorRole role) const
{
	ColorControls cc;
	switch (role)
	{
	case LightSquare:
		cc.slider[0] = ui->m_lightSquareRedSlider;
		cc.slider[1] = ui->m_lightSquareGreenSlider;
		cc.slider[2] = ui->m_lightSquareBlueSlider;
		cc.spin[0] = ui->m_lightSquareRedSpin;
		cc.spin[1] = ui->m_lightSquareGreenSpin;
		cc.spin[2] = ui->m_lightSquareBlueSpin;
		cc.swatch = ui->m_lightSquareSwatch;
		break;
	case DarkSquare:
		cc.slider[0] = ui->m_darkSquareRedSlider;
		cc.slider[1] = ui->m_darkSquareGreenSlider;
		cc.slider[2] = ui->m_darkSquareBlueSlider;
		cc.spin[0] = ui->m_darkSquareRedSpin;
		cc.spin[1] = ui->m_darkSquareGreenSpin;
		cc.spin[2] = ui->m_darkSquareBlueSpin;
		cc.swatch = ui->m_darkSquareSwatch;
		break;
	case LightPiece:
		cc.slider[0] = ui->m_lightPieceRedSlider;
		cc.slider[1] = ui->m_lightPieceGreenSlider;
		cc.slider[2] = ui->m_lightPieceBlueSlider;
		cc.spin[0] = ui->m_lightPieceRedSpin;
		cc.spin[1] = ui->m_lightPieceGreenSpin;
		cc.spin[2] = ui->m_lightPieceBlueSpin;
		cc.swatch = ui->m_lightPieceSwatch;
		break;
	default:
		cc.slider[0] = ui->m_darkPieceRedSlider;
		cc.slider[1] = ui->m_darkPieceGreenSlider;
		cc.slider[2] = ui->m_darkPieceBlueSlider;
		cc.spin[0] = ui->m_darkPieceRedSpin;
		cc.spin[1] = ui->m_darkPieceGreenSpin;
		cc.spin[2] = ui->m_darkPieceBlueSpin;
		cc.swatch = ui->m_darkPieceSwatch;
		break;
	}
	return cc;
}

QColor SettingsDialog::currentColor(ColorRole role) const
{
	CuteChessApplication* app = CuteChessApplication::instance();
	switch (role)
	{
	case LightSquare: return app->lightSquareColor();
	case DarkSquare:  return app->darkSquareColor();
	case LightPiece:  return app->lightPieceColor();
	default:          return app->darkPieceColor();
	}
}

void SettingsDialog::setColorSliders(ColorRole role, const QColor& color)
{
	const ColorControls cc = colorControls(role);
	const bool wasUpdating = m_updatingSquareColorControls;
	m_updatingSquareColorControls = true;
	cc.slider[0]->setValue(color.red());
	cc.slider[1]->setValue(color.green());
	cc.slider[2]->setValue(color.blue());
	// readSettings() runs before the slider/spin box connections are made
	// in the constructor, so set the numbers explicitly as well.
	cc.spin[0]->setValue(color.red());
	cc.spin[1]->setValue(color.green());
	cc.spin[2]->setValue(color.blue());
	m_updatingSquareColorControls = wasUpdating;
	updateSquareColorPreview(cc.swatch, color);
}

void SettingsDialog::onColorComponentChanged(ColorRole role)
{
	// Ignore changes made by readSettings()/resetSquareColors() while
	// they are setting the three sliders one after another.
	if (m_updatingSquareColorControls)
		return;

	const ColorControls cc = colorControls(role);
	const QColor color(cc.slider[0]->value(),
			   cc.slider[1]->value(),
			   cc.slider[2]->value());
	updateSquareColorPreview(cc.swatch, color);

	CuteChessApplication* app = CuteChessApplication::instance();
	switch (role)
	{
	case LightSquare: app->setLightSquareColor(color); break;
	case DarkSquare:  app->setDarkSquareColor(color);  break;
	case LightPiece:  app->setLightPieceColor(color);  break;
	default:          app->setDarkPieceColor(color);   break;
	}
}

void SettingsDialog::resetSquareColors()
{
	// Also resets the piece colours (same group box, one reset button).
	const QColor defaultLight(0xff, 0xce, 0x9e);
	const QColor defaultDark(0xd1, 0x8b, 0x47);

	m_updatingSquareColorControls = true;
	setColorSliders(LightSquare, defaultLight);
	setColorSliders(DarkSquare, defaultDark);
	setColorSliders(LightPiece, Qt::white);
	setColorSliders(DarkPiece, Qt::black);
	m_updatingSquareColorControls = false;

	CuteChessApplication* app = CuteChessApplication::instance();
	app->setLightPieceColor(Qt::white);
	app->setDarkPieceColor(Qt::black);
	app->setLightSquareColor(defaultLight);
	app->setDarkSquareColor(defaultDark);
}

void SettingsDialog::readSettings()
{
	QSettings s;

	s.beginGroup("ui");
	// The tick box means "use normal-weight text", i.e. the inverse of
	// the stored "bold_text_enabled" value (bold stays the default).
	ui->m_boldTextCheck->setChecked(!s.value("bold_text_enabled", true).toBool());
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
	ui->m_evalBarBlackAtBottomCheck->setChecked(
		s.value("eval_bar_black_at_bottom", false).toBool());
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
	ui->m_redSpin->setValue(boardColor.red());
	ui->m_greenSpin->setValue(boardColor.green());
	ui->m_blueSpin->setValue(boardColor.blue());
	m_updatingBoardColorControls = false;
	updateBoardColorPreview(boardColor);

	m_updatingSquareColorControls = true;
	setColorSliders(LightSquare, currentColor(LightSquare));
	setColorSliders(DarkSquare, currentColor(DarkSquare));
	setColorSliders(LightPiece, currentColor(LightPiece));
	setColorSliders(DarkPiece, currentColor(DarkPiece));
	m_updatingSquareColorControls = false;

	ui->m_uciInputFilterCheck->setChecked(
		s.value("engines/uci_input_filter", true).toBool());

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
