/*
    This file is part of Cute Chess (fork).

    Cute Chess is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include <QtTest/QTest>
#include <QApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QDir>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStyleOptionViewItem>
#include <QAbstractItemView>
#include <QLineEdit>
#include <QMessageBox>
#include <QTimer>
#include <QPlainTextEdit>
#include <QTemporaryDir>

#include <enginebuilder.h>
#include <engineconfiguration.h>
#include <engineoption.h>
#include <enginespinoption.h>
#include <chessengine.h>

#include "engineconfigurationdlg.h"
#include "enginelistsettings.h"

#ifndef FAKEUCI_PATH
#error FAKEUCI_PATH not defined
#endif

class tst_EngineDialog : public QObject
{
	Q_OBJECT

	private slots:
		void initTestCase();
		void init();
		void detectionGivesEngineDefaults();
		void optionValuesSurviveOkWithoutChanges();
		void editedThreadsAndHashAreReturned();
		void editorForSpinOptionsHonoursRange();
		void jsonRoundTripKeepsThreadsAndHash();
		void redetectKeepsValuesForSameEngine();
		void redetectKeepsAliases();
		void restoreDefaultsResetsThreadsAndHash();
		void fieldsWithoutWidgetsAreKept();
		void emptyingInitStringsRemovesThem();
		void checkboxesShowStoredState();
		void hugeMaximumStaysUsable();
		void engineReceivesThreadsAndHash();

	private:
		EngineConfiguration fakeEngine() const;
		// Types the engine into an Add Engine dialog and presses OK
		EngineConfiguration addFakeEngine();
		static EngineOption* findOption(const QList<EngineOption*>& options,
						const QString& name);
		static QVariant optionValue(const EngineConfiguration& config,
					    const QString& name);
		// Presses OK in a dialog and waits until it is accepted
		static bool accept(EngineConfigurationDialog& dlg);
		static void setOptionValue(EngineConfigurationDialog& dlg,
					   const QString& name, int value);

		QTemporaryDir* m_dir = nullptr;
};

void tst_EngineDialog::initTestCase()
{
	QVERIFY(QFile::exists(FAKEUCI_PATH));
	QFile::setPermissions(FAKEUCI_PATH, QFile::permissions(FAKEUCI_PATH)
		| QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);

	// A message box would block the test forever: report and close it
	auto timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, []()
	{
		if (auto box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
		{
			qWarning("Unexpected message box: %s", qPrintable(box->text()));
			box->close();
		}
	});
	timer->start(200);
}

void tst_EngineDialog::init()
{
	delete m_dir;
	m_dir = new QTemporaryDir;
	QVERIFY(m_dir->isValid());
	qputenv("FAKEUCI_LOG", m_dir->filePath("log.txt").toLocal8Bit());
}

EngineConfiguration tst_EngineDialog::fakeEngine() const
{
#ifdef Q_OS_WIN
	// Windows cannot run a .py file through its shebang line, so
	// start it through a small batch launcher instead.
	const QString bat = m_dir->filePath("fakeuci.bat");
	QFile f(bat);
	if (f.open(QIODevice::WriteOnly))
	{
		f.write("@echo off\r\npython \"" +
			QDir::toNativeSeparators(QString(FAKEUCI_PATH)).toLocal8Bit() +
			"\" %*\r\n");
		f.close();
	}
	return EngineConfiguration("Fake", QDir::toNativeSeparators(bat), "uci");
#else
	return EngineConfiguration("Fake", FAKEUCI_PATH, "uci");
#endif
}

EngineOption* tst_EngineDialog::findOption(const QList<EngineOption*>& options,
					   const QString& name)
{
	for (EngineOption* o : options)
		if (o->name() == name)
			return o;
	return nullptr;
}

QVariant tst_EngineDialog::optionValue(const EngineConfiguration& config,
				       const QString& name)
{
	auto o = findOption(config.options(), name);
	return o ? o->value() : QVariant();
}

bool tst_EngineDialog::accept(EngineConfigurationDialog& dlg)
{
	QSignalSpy spy(&dlg, &QDialog::accepted);
	auto box = dlg.findChild<QDialogButtonBox*>("m_buttonBox");
	if (!box || !box->button(QDialogButtonBox::Ok)->isEnabled())
		return false;
	box->button(QDialogButtonBox::Ok)->click();
	return spy.count() > 0 || spy.wait(15000);
}

void tst_EngineDialog::setOptionValue(EngineConfigurationDialog& dlg,
				      const QString& name, int value)
{
	auto view = dlg.findChild<QAbstractItemView*>("m_optionsView");
	QVERIFY(view);
	auto model = view->model();
	for (int row = 0; row < model->rowCount(); row++)
	{
		if (model->index(row, 0).data().toString() == name)
		{
			QVERIFY(model->setData(model->index(row, 1), value));
			return;
		}
	}
	QFAIL("option not found");
}

EngineConfiguration tst_EngineDialog::addFakeEngine()
{
	EngineConfigurationDialog dlg(EngineConfigurationDialog::AddEngine);
	dlg.findChild<QLineEdit*>("m_nameEdit")->setText("Fake");
	dlg.findChild<QLineEdit*>("m_commandEdit")->setText(
		fakeEngine().command());
	if (!accept(dlg))
		return EngineConfiguration();
	return dlg.engineConfiguration();
}

void tst_EngineDialog::detectionGivesEngineDefaults()
{
	auto config = addFakeEngine();
	QVERIFY(!config.options().isEmpty());
	QCOMPARE(optionValue(config, "Hash").toInt(), 16);
	QCOMPARE(optionValue(config, "Threads").toInt(), 1);
}

void tst_EngineDialog::optionValuesSurviveOkWithoutChanges()
{
	auto config = fakeEngine();
	config.setOption("Hash", 2048);
	config.setOption("Threads", 8);
	// setOption() on an unknown name creates a text option; use real spins
	config.setOptions({});
	config.addOption(new EngineSpinOption("Hash", 2048, 16, 1, 33554432));
	config.addOption(new EngineSpinOption("Threads", 8, 1, 1, 1024));

	EngineConfigurationDialog dlg(EngineConfigurationDialog::ConfigureEngine);
	dlg.applyEngineInformation(config);
	QVERIFY(accept(dlg));

	auto out = dlg.engineConfiguration();
	QCOMPARE(optionValue(out, "Hash").toInt(), 2048);
	QCOMPARE(optionValue(out, "Threads").toInt(), 8);
}

void tst_EngineDialog::editedThreadsAndHashAreReturned()
{
	auto detected = addFakeEngine();
	QVERIFY(!detected.options().isEmpty());

	// Second dialog on the detected configuration: edit, then OK
	EngineConfigurationDialog dlg2(EngineConfigurationDialog::ConfigureEngine);
	dlg2.applyEngineInformation(detected);
	setOptionValue(dlg2, "Hash", 4096);
	setOptionValue(dlg2, "Threads", 12);
	QVERIFY(accept(dlg2));

	auto out = dlg2.engineConfiguration();
	QCOMPARE(optionValue(out, "Hash").toInt(), 4096);
	QCOMPARE(optionValue(out, "Threads").toInt(), 12);
}

void tst_EngineDialog::editorForSpinOptionsHonoursRange()
{
	auto detected = addFakeEngine();
	QVERIFY(!detected.options().isEmpty());

	EngineConfigurationDialog dlg2(EngineConfigurationDialog::ConfigureEngine);
	dlg2.applyEngineInformation(detected);
	auto view = dlg2.findChild<QAbstractItemView*>("m_optionsView");
	QVERIFY(view);
	auto model = view->model();
	auto delegate = view->itemDelegate();

	for (int row = 0; row < model->rowCount(); row++)
	{
		const QString name = model->index(row, 0).data().toString();
		if (name != "Hash")
			continue;
		const QModelIndex index = model->index(row, 1);
		QWidget* editor = delegate->createEditor(&dlg2, QStyleOptionViewItem(), index);
		auto spin = qobject_cast<QSpinBox*>(editor);
		QVERIFY(spin);
		QCOMPARE(spin->maximum(), 33554432);
		spin->setValue(1024);
		delegate->setModelData(editor, model, index);
		delete editor;
		QCOMPARE(model->index(row, 1).data(Qt::EditRole).toMap()["value"].toInt(), 1024);
		return;
	}
	QFAIL("Hash not listed");
}

void tst_EngineDialog::jsonRoundTripKeepsThreadsAndHash()
{
	auto config = fakeEngine();
	config.addOption(new EngineSpinOption("Hash", 2048, 16, 1, 33554432));
	config.addOption(new EngineSpinOption("Threads", 8, 1, 1, 1024));

	QList<EngineConfiguration> back;
	QVERIFY(EngineListSettings::fromJson(
		EngineListSettings::toJson({ config }), &back));
	QCOMPARE(back.count(), 1);
	QCOMPARE(back.first().name(), QString("Fake"));
	QCOMPARE(optionValue(back.first(), "Hash").toInt(), 2048);
	QCOMPARE(optionValue(back.first(), "Threads").toInt(), 8);
	QCOMPARE(back.first().options().count(), 2);

	QVERIFY(!EngineListSettings::fromJson("", &back));
	QVERIFY(!EngineListSettings::fromJson("not json", &back));
}

void tst_EngineDialog::redetectKeepsValuesForSameEngine()
{
	auto config = fakeEngine();
	config.addOption(new EngineSpinOption("Hash", 2048, 16, 1, 33554432));
	config.addOption(new EngineSpinOption("Threads", 8, 1, 1, 1024));

	EngineConfigurationDialog dlg(EngineConfigurationDialog::ConfigureEngine);
	dlg.applyEngineInformation(config);

	QSignalSpy spy(&dlg, &EngineConfigurationDialog::detectionFinished);
	auto detect = dlg.findChild<QPushButton*>("m_detectBtn");
	QVERIFY(detect);
	detect->click();
	QVERIFY(spy.count() > 0 || spy.wait(15000));

	auto out = dlg.engineConfiguration();
	QCOMPARE(optionValue(out, "Hash").toInt(), 2048);
	QCOMPARE(optionValue(out, "Threads").toInt(), 8);
	// new options reported by the engine are present too
	QVERIFY(findOption(out.options(), "Style") != nullptr);
}

void tst_EngineDialog::redetectKeepsAliases()
{
	auto config = fakeEngine();
	auto hash = new EngineSpinOption("Hash", 2048, 16, 1, 33554432);
	hash->setAlias("Memory");
	config.addOption(hash);
	config.addOption(new EngineSpinOption("Threads", 8, 1, 1, 1024));

	EngineConfigurationDialog dlg(EngineConfigurationDialog::ConfigureEngine);
	dlg.applyEngineInformation(config);

	QSignalSpy spy(&dlg, &EngineConfigurationDialog::detectionFinished);
	dlg.findChild<QPushButton*>("m_detectBtn")->click();
	QVERIFY(spy.count() > 0 || spy.wait(15000));

	auto out = dlg.engineConfiguration();
	auto opt = findOption(out.options(), "Hash");
	QVERIFY(opt);
	QCOMPARE(opt->alias(), QString("Memory"));
	QCOMPARE(opt->value().toInt(), 2048);
	QCOMPARE(optionValue(out, "Threads").toInt(), 8);
}

void tst_EngineDialog::restoreDefaultsResetsThreadsAndHash()
{
	auto config = fakeEngine();
	config.addOption(new EngineSpinOption("Hash", 2048, 16, 1, 33554432));
	config.addOption(new EngineSpinOption("Threads", 8, 1, 1, 1024));

	EngineConfigurationDialog dlg(EngineConfigurationDialog::ConfigureEngine);
	dlg.applyEngineInformation(config);
	auto restore = dlg.findChild<QPushButton*>("m_restoreBtn");
	QVERIFY(restore && restore->isEnabled());
	restore->click();

	auto out = dlg.engineConfiguration();
	QCOMPARE(optionValue(out, "Hash").toInt(), 16);
	QCOMPARE(optionValue(out, "Threads").toInt(), 1);
}

void tst_EngineDialog::fieldsWithoutWidgetsAreKept()
{
	auto config = fakeEngine();
	config.setStderrFile("/tmp/engine-stderr.txt");
	config.setRestartMode(EngineConfiguration::RestartOn);
	config.setClaimsValidated(false);
	config.setArguments({ "--a", "--b" });
	config.setWorkingDirectory(m_dir->path());
	config.addOption(new EngineSpinOption("Hash", 64, 16, 1, 33554432));

	EngineConfigurationDialog dlg(EngineConfigurationDialog::ConfigureEngine);
	dlg.applyEngineInformation(config);
	QVERIFY(accept(dlg));

	auto out = dlg.engineConfiguration();
	QCOMPARE(out.stderrFile(), QString("/tmp/engine-stderr.txt"));
	QCOMPARE(out.restartMode(), EngineConfiguration::RestartOn);
	QVERIFY(!out.areClaimsValidated());
	QCOMPARE(out.arguments(), QStringList({ "--a", "--b" }));
	QCOMPARE(out.workingDirectory(), m_dir->path());
}

void tst_EngineDialog::emptyingInitStringsRemovesThem()
{
	auto config = fakeEngine();
	config.setInitStrings({ "first", "second" });
	config.addOption(new EngineSpinOption("Hash", 64, 16, 1, 33554432));

	EngineConfigurationDialog dlg(EngineConfigurationDialog::ConfigureEngine);
	dlg.applyEngineInformation(config);
	QCOMPARE(dlg.engineConfiguration().initStrings(),
		 QStringList({ "first", "second" }));

	auto edit = dlg.findChild<QPlainTextEdit*>("m_initStringEdit");
	QVERIFY(edit);
	edit->clear();
	QVERIFY(dlg.engineConfiguration().initStrings().isEmpty());
}

void tst_EngineDialog::checkboxesShowStoredState()
{
	auto config = EngineConfiguration("Xb", "xb", "xboard");
	config.setWhiteEvalPov(true);
	config.setDebugEnabled(false);

	EngineConfigurationDialog dlg(EngineConfigurationDialog::ConfigureEngine);
	dlg.applyEngineInformation(config);
	QVERIFY(dlg.engineConfiguration().whiteEvalPov());
	QVERIFY(!dlg.engineConfiguration().debugEnabled());

	auto uci = fakeEngine();
	uci.setDebugEnabled(true);
	EngineConfigurationDialog dlg2(EngineConfigurationDialog::ConfigureEngine);
	dlg2.applyEngineInformation(uci);
	QVERIFY(dlg2.engineConfiguration().debugEnabled());
}

void tst_EngineDialog::hugeMaximumStaysUsable()
{
	auto out = addFakeEngine();
	auto opt = findOption(out.options(), "HugeHash");
	QVERIFY(opt);
	// The engine's maximum, 4294967295, does not fit in an int. It used
	// to be read as 0, making every value invalid.
	QVERIFY(opt->isValid(5000));
	QVERIFY(opt->isValid(2000000000));
}

void tst_EngineDialog::engineReceivesThreadsAndHash()
{
	auto config = fakeEngine();
	config.addOption(new EngineSpinOption("Hash", 2048, 16, 1, 33554432));
	config.addOption(new EngineSpinOption("Threads", 8, 1, 1, 1024));

	EngineConfigurationDialog dlg(EngineConfigurationDialog::ConfigureEngine);
	dlg.applyEngineInformation(config);
	setOptionValue(dlg, "Hash", 512);
	setOptionValue(dlg, "Threads", 3);
	QVERIFY(accept(dlg));
	config = dlg.engineConfiguration();

	// Through a JSON round trip, as when it is stored and loaded again
	QList<EngineConfiguration> loaded;
	QVERIFY(EngineListSettings::fromJson(
		EngineListSettings::toJson({ config }), &loaded));

	EngineBuilder builder(loaded.first());
	QString error;
	auto engine = qobject_cast<ChessEngine*>(
		builder.create(nullptr, nullptr, this, &error));
	QVERIFY2(engine, qPrintable(error));
	QSignalSpy ready(engine, &ChessEngine::ready);
	QVERIFY(ready.count() > 0 || ready.wait(15000));
	engine->quit();
	QTest::qWait(500);

	QFile log(m_dir->filePath("log.txt"));
	QVERIFY(log.open(QIODevice::ReadOnly));
	const QString text = QString::fromUtf8(log.readAll());
	QVERIFY2(text.contains("setoption name Hash value 512\n"), qPrintable(text));
	QVERIFY2(text.contains("setoption name Threads value 3\n"), qPrintable(text));
	delete engine;
}

QTEST_MAIN(tst_EngineDialog)
#include "tst_enginedialog.moc"
