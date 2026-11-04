/*
    This file is part of Cute Chess.

    Cute Chess is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include "tournamentarchive.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "dialoggeometry.h"

static const QString folderName = QStringLiteral("Previous Tournaments");

qint64 TournamentArchive::maxBytes()
{
	return 5000000LL; // 5 MB
}

static bool isWritableDir(const QString& path)
{
	if (!QDir().mkpath(path))
		return false;
	QFile probe(path + QStringLiteral("/.write-test"));
	if (!probe.open(QIODevice::WriteOnly))
		return false;
	probe.close();
	probe.remove();
	return true;
}

QString TournamentArchive::directory()
{
	static QString cached;
	if (!cached.isEmpty() && QDir(cached).exists())
		return cached;

	QStringList candidates;
	candidates << QCoreApplication::applicationDirPath() + '/' + folderName;
	QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	if (data.isEmpty())
		data = QDir::homePath() + QStringLiteral("/.cutechess");
	candidates << data + '/' + folderName;

	for (const QString& c : candidates)
	{
		if (isWritableDir(c))
		{
			cached = QDir::cleanPath(c);
			return cached;
		}
	}
	return QString();
}

// Records are named "yyyyMMdd-HHmmss-zzz-Name.html", so sorting by name
// sorts by the time the record was started (the oldest record first).
QFileInfoList TournamentArchive::records()
{
	const QString dir = directory();
	if (dir.isEmpty())
		return QFileInfoList();
	QFileInfoList list = QDir(dir).entryInfoList(
		QStringList() << QStringLiteral("*.html"),
		QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
	return list;
}

void TournamentArchive::enforceLimit(const QString& keepFile)
{
	const QString dir = directory();
	if (dir.isEmpty())
		return;

	// Everything in the folder counts towards the limit
	QFileInfoList all = QDir(dir).entryInfoList(
		QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
	qint64 total = 0;
	for (const QFileInfo& i : all)
		total += i.size();

	const QString keep = keepFile.isEmpty() ? QString()
		: QFileInfo(keepFile).absoluteFilePath();

	// Oldest first (name order); the record being written goes last
	for (int pass = 0; pass < 2 && total > maxBytes(); pass++)
	{
		for (const QFileInfo& i : all)
		{
			if (total <= maxBytes())
				break;
			const bool isKeep = i.absoluteFilePath() == keep;
			if (isKeep != (pass == 1))
				continue;
			if (QFile::remove(i.absoluteFilePath()))
				total -= i.size();
		}
	}
}

bool TournamentArchive::save(const QString& name, const QString& html,
			     QString* recordFile)
{
	Q_ASSERT(recordFile != nullptr);
	const QString dir = directory();
	if (dir.isEmpty())
		return false;

	const QByteArray data = html.toUtf8();
	if (data.size() > maxBytes())
		return false;

	if (recordFile->isEmpty() || !QFile::exists(*recordFile))
	{
		QString safe = name;
		for (int i = 0; i < safe.size(); ++i)
		{
			const QChar ch = safe.at(i);
			if (!ch.isLetterOrNumber() && ch != '-' && ch != '_')
				safe[i] = '_';
		}
		safe = safe.left(60);
		if (safe.isEmpty())
			safe = QStringLiteral("Tournament");
		*recordFile = dir + '/' + QStringLiteral("%1-%2.html").arg(
			QDateTime::currentDateTime().toString(
				QStringLiteral("yyyyMMdd-HHmmss-zzz")), safe);
	}

	QSaveFile file(*recordFile);
	if (!file.open(QIODevice::WriteOnly))
		return false;
	if (file.write(data) != data.size() || !file.commit())
		return false;

	enforceLimit(*recordFile);
	return true;
}

QString TournamentArchive::displayTitle(const QFileInfo& record)
{
	const QString base = record.completeBaseName();
	QDateTime when = QDateTime::fromString(base.left(15), QStringLiteral("yyyyMMdd-HHmmss"));
	if (!when.isValid())
		when = record.lastModified();

	// The name inside the record is the tournament's real name
	QString name = base.mid(20);
	QFile f(record.absoluteFilePath());
	if (f.open(QIODevice::ReadOnly))
	{
		const QString head = QString::fromUtf8(f.read(4096));
		static const QRegularExpression re(
			QStringLiteral("<title>(.*?)</title>"),
			QRegularExpression::DotMatchesEverythingOption);
		const auto m = re.match(head);
		if (m.hasMatch())
		{
			name = m.captured(1);
			name.replace(QStringLiteral("&amp;"), QStringLiteral("&"))
			    .replace(QStringLiteral("&lt;"), QStringLiteral("<"))
			    .replace(QStringLiteral("&gt;"), QStringLiteral(">"))
			    .replace(QStringLiteral("&quot;"), QStringLiteral("\""));
		}
	}
	return QStringLiteral("%1   %2").arg(
		QLocale().toString(when, QLocale::ShortFormat), name);
}

void TournamentArchive::showRecord(QWidget* parent, const QString& file)
{
	QFile f(file);
	if (!f.open(QIODevice::ReadOnly))
		return;

	auto dlg = new QDialog(parent);
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->setWindowTitle(displayTitle(QFileInfo(file)));
	auto browser = new QTextBrowser(dlg);
	browser->setOpenExternalLinks(true);
	browser->setHtml(QString::fromUtf8(f.readAll()));
	auto layout = new QVBoxLayout(dlg);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(browser);
	dlg->resize(1100, 500);
	dlg->show();
}
