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

#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QColor>

class QLabel;
class QSlider;
class QSpinBox;

namespace Ui {
	class SettingsDialog;
}

/*!
 * \brief Dialog for configuring global settings
 */
class SettingsDialog : public QDialog
{
	Q_OBJECT

	public:
		/*! Creates a new SettingsDialog. */
		SettingsDialog(QWidget* parent = nullptr);
		/*! Destroys the dialog. */
		virtual ~SettingsDialog();

	protected:
		// Inherited from QDialog
		virtual void closeEvent(QCloseEvent* event);

	private slots:
		void browseTbPath();
		void browseDefaultPgnOutFile();
		void browseTournamentDefaultPgnOutFile();
		void browseTournamentDefaultEpdOutFile();
		void onBoardColorComponentChanged();
		void resetBoardColor();
		void resetSquareColors();

	private:
		void readSettings();
		void updateBoardColorPreview(const QColor& color);
		void applyBoardColor(const QColor& color);
		void updateSquareColorPreview(QLabel* swatch, const QColor& color);

		// Light/dark square and piece colours: each is edited with
		// three RGB sliders (plus spin boxes on their right-hand side),
		// exactly like the board background colour.
		enum ColorRole { LightSquare, DarkSquare, LightPiece, DarkPiece };
		struct ColorControls
		{
			QSlider* slider[3];	// red, green, blue
			QSpinBox* spin[3];
			QLabel* swatch;
		};
		ColorControls colorControls(ColorRole role) const;
		QColor currentColor(ColorRole role) const;
		void setColorSliders(ColorRole role, const QColor& color);
		void onColorComponentChanged(ColorRole role);

		Ui::SettingsDialog* ui;
		bool m_updatingBoardColorControls;
		bool m_updatingSquareColorControls;
};

#endif // SETTINGSDIALOG_H
