/*
    This file is part of Cute Chess (fork).

    Cute Chess is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include <QtTest/QTest>
#include <QApplication>
#include <QComboBox>
#include <QSettings>
#include <QTemporaryDir>

#include <enginemanager.h>
#include <engineconfiguration.h>
#include <engineoption.h>
#include <enginespinoption.h>
#include <timecontrol.h>

// The tests set up dialog state directly instead of clicking through the
// modal Configure dialogs
#define private public
#include "newgamedlg.h"
#include "newtournamentdialog.h"
#undef private
#include "ui_newgamedlg.h"

// Stand-in for the application class, which is not part of this test
#include "cutechessapp.h"
QString CuteChessApplication::userName() { return "tester"; }

class tst_DialogPersist : public QObject
{
	Q_OBJECT

	private slots:
		void initTestCase();
		void init();
		void tournamentKeepsConfiguredEngines();
		void tournamentKeepsTimeControls();
		void tournamentIgnoresRemovedEngines();
		void tournamentFallsBackToOldNameList();
		void newGameKeepsConfiguredEngines();

	private:
		static EngineConfiguration engine(const QString& name, int hash, int threads);
		static int optionInt(const EngineConfiguration& c, const QString& name);
		EngineManager* engines();
		QTemporaryDir* m_dir = nullptr;
};

void tst_DialogPersist::initTestCase()
{
	QCoreApplication::setOrganizationName("CutechessTest");
	QCoreApplication::setApplicationName("dialogpersist");
	QSettings::setDefaultFormat(QSettings::IniFormat);
}

void tst_DialogPersist::init()
{
	delete m_dir;
	m_dir = new QTemporaryDir;
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir->path());
	QSettings().clear();
}

EngineConfiguration tst_DialogPersist::engine(const QString& name, int hash, int threads)
{
	EngineConfiguration c(name, "/bin/" + name, "uci");
	c.addOption(new EngineSpinOption("Hash", hash, 16, 1, 33554432));
	c.addOption(new EngineSpinOption("Threads", threads, 1, 1, 1024));
	return c;
}

int tst_DialogPersist::optionInt(const EngineConfiguration& c, const QString& name)
{
	for (const EngineOption* o : c.options())
		if (o->name() == name)
			return o->value().toInt();
	return -1;
}

EngineManager* tst_DialogPersist::engines()
{
	auto m = new EngineManager(this);
	m->addEngine(engine("alpha", 16, 1));
	m->addEngine(engine("beta", 16, 1));
	return m;
}

void tst_DialogPersist::tournamentKeepsConfiguredEngines()
{
	auto list = engines();
	{
		NewTournamentDialog dlg(list);
		dlg.m_addedEnginesManager->addEngine(engine("alpha", 16, 1));
		dlg.m_addedEnginesManager->addEngine(engine("beta", 16, 1));
		dlg.m_timeControls << TimeControl() << TimeControl();
		// "Configure": Hash and Threads edited for alpha only
		dlg.m_addedEnginesManager->updateEngineAt(0, engine("alpha", 4096, 12));
		dlg.saveEngineList();
		// no accept(): the dialog is cancelled
	}
	// the very next dialog in the same session, and (below) a new run
	for (int run = 0; run < 2; run++)
	{
		if (run == 1)
			QSettings().sync();
		NewTournamentDialog dlg(list);
		auto restored = dlg.m_addedEnginesManager->engines();
		QCOMPARE(restored.count(), 2);
		QCOMPARE(restored.at(0).name(), QString("alpha"));
		QCOMPARE(optionInt(restored.at(0), "Hash"), 4096);
		QCOMPARE(optionInt(restored.at(0), "Threads"), 12);
		QCOMPARE(optionInt(restored.at(1), "Hash"), 16);
		QCOMPARE(optionInt(restored.at(1), "Threads"), 1);
	}
}

void tst_DialogPersist::tournamentKeepsTimeControls()
{
	auto list = engines();
	{
		NewTournamentDialog dlg(list);
		dlg.m_addedEnginesManager->addEngine(engine("alpha", 16, 1));
		dlg.m_addedEnginesManager->addEngine(engine("beta", 16, 1));
		TimeControl tc;
		tc.setTimePerTc(60000);
		tc.setTimeIncrement(500);
		tc.setMovesPerTc(0);
		QVERIFY(tc.isValid());
		dlg.m_timeControls << TimeControl() << tc;
		dlg.saveEngineList();
	}
	NewTournamentDialog dlg(list);
	QCOMPARE(dlg.m_timeControls.count(), 2);
	QVERIFY(!dlg.m_timeControls.at(0).isValid());
	QCOMPARE(dlg.m_timeControls.at(1).timePerTc(), 60000);
	QCOMPARE(dlg.m_timeControls.at(1).timeIncrement(), 500);
}

void tst_DialogPersist::tournamentIgnoresRemovedEngines()
{
	auto list = engines();
	{
		NewTournamentDialog dlg(list);
		dlg.m_addedEnginesManager->addEngine(engine("alpha", 64, 2));
		dlg.m_addedEnginesManager->addEngine(engine("beta", 64, 2));
		dlg.m_timeControls << TimeControl() << TimeControl();
		dlg.saveEngineList();
	}
	list->removeEngineAt(0); // alpha removed in Settings > Engines
	NewTournamentDialog dlg(list);
	auto restored = dlg.m_addedEnginesManager->engines();
	QCOMPARE(restored.count(), 1);
	QCOMPARE(restored.at(0).name(), QString("beta"));
	QCOMPARE(dlg.m_timeControls.count(), 1);
}

void tst_DialogPersist::tournamentFallsBackToOldNameList()
{
	auto list = engines();
	QSettings().setValue("tournament/last_engine_names",
			     QStringList({ "beta", "alpha" }));
	NewTournamentDialog dlg(list);
	auto restored = dlg.m_addedEnginesManager->engines();
	QCOMPARE(restored.count(), 2);
	QCOMPARE(restored.at(0).name(), QString("beta"));
	QCOMPARE(dlg.m_timeControls.count(), 2);
}

void tst_DialogPersist::newGameKeepsConfiguredEngines()
{
	auto list = engines();
	{
		NewGameDialog dlg(list);
		auto white = dlg.ui->m_whiteEngineComboBox;
		white->setCurrentIndex(white->findText("beta"));
		QCOMPARE(white->currentText(), QString("beta"));
		// "Configure" for White: Hash 8192, Threads 16
		dlg.m_engineConfig[Chess::Side::White] = engine("beta", 8192, 16);
		dlg.saveEngineChoice(Chess::Side::White);
		dlg.saveEngineChoice(Chess::Side::Black);
	}
	for (int run = 0; run < 2; run++)
	{
		if (run == 1)
			QSettings().sync();
		NewGameDialog dlg(list);
		QCOMPARE(dlg.ui->m_whiteEngineComboBox->currentText(), QString("beta"));
		auto cfg = dlg.m_engineConfig[Chess::Side::White];
		QCOMPARE(optionInt(cfg, "Hash"), 8192);
		QCOMPARE(optionInt(cfg, "Threads"), 16);
		// Black was left as it was: unconfigured list entry
		QCOMPARE(optionInt(dlg.m_engineConfig[Chess::Side::Black], "Hash"), 16);
	}
}

QTEST_MAIN(tst_DialogPersist)
#include "tst_dialogpersist.moc"
