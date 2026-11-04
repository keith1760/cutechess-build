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

#include "tournamentresultsdlg.h"

#include <QTextBrowser>
#include <QBoxLayout>

#include <tournament.h>

#include "dialoggeometry.h"
#include "tournamentarchive.h"

TournamentResultsDialog::TournamentResultsDialog(QWidget* parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Tournament Results"));
	restoreDialogGeometry(this, QStringLiteral("tournamentresultsdialog"));

	m_resultsEdit = new QTextBrowser(this);
	m_resultsEdit->setOpenExternalLinks(true);
	m_resultsEdit->setReadOnly(true);

	auto layout = new QBoxLayout(QBoxLayout::TopToBottom);
	layout->addWidget(m_resultsEdit);
	layout->setContentsMargins(0, 0, 0, 0);

	setLayout(layout);
	resize(1100, 400);
}

TournamentResultsDialog::~TournamentResultsDialog()
{
}

void TournamentResultsDialog::setTournament(Tournament* tournament)
{
	// A new tournament starts a new record in "Previous Tournaments"
	m_tournament = tournament;
	m_archiveFile.clear();
	refresh();
}

void TournamentResultsDialog::update()
{
	auto tournament = qobject_cast<Tournament*>(QObject::sender());
	if (tournament != nullptr && tournament != m_tournament)
	{
		m_tournament = tournament;
		m_archiveFile.clear();
	}
	refresh();
}

void TournamentResultsDialog::refresh()
{
	// The last report stays on screen after the tournament object is gone
	if (m_tournament.isNull())
		return;

	const QString html = m_tournament->htmlReport();
	const QString name = m_tournament->name();
	setWindowTitle(name.isEmpty() ? tr("Tournament Results")
				      : tr("%1 \u2014 Tournament Results").arg(name));
	m_resultsEdit->setHtml(html);

	// Finished or not, every tournament is kept in "Previous Tournaments"
	TournamentArchive::save(name, html, &m_archiveFile);
}
