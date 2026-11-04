#include <QtTest/QTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>

#include <engineconfiguration.h>
#include <enginemanager.h>

#include "configrecord.h"

class tst_ConfigRecord : public QObject
{
	Q_OBJECT

	private slots:
		void init();
		void roundTrip();
		void windowLayoutIsSavedAndRestored();
		void recordWithoutWindowLayoutLeavesWindowAlone();
		void liveWindowLayoutReplacesStaleOne();
		void lastConfigDirIsNotSavedOrTouched();
		void missingKeysGoBackToDefault();
		void settingsOnlyRecordKeepsEngines();
		void enginesFileIsReadableByEngineManager();
		void rejectsBadFiles_data();
		void rejectsBadFiles();

	private:
		QString path(const QString& name) const
		{
			return m_dir->filePath(name);
		}
		static EngineManager* makeEngines(const QStringList& names);
		static QSettings* makeSettings(const QString& file);
		static void write(const QString& file, const QByteArray& data);

		QTemporaryDir* m_dir = nullptr;
};

void tst_ConfigRecord::init()
{
	delete m_dir;
	m_dir = new QTemporaryDir;
	QVERIFY(m_dir->isValid());
}

EngineManager* tst_ConfigRecord::makeEngines(const QStringList& names)
{
	auto manager = new EngineManager;
	for (const QString& name : names)
	{
		EngineConfiguration config(name, "/usr/bin/" + name, "uci");
		config.setPondering(name == "alpha");
		config.setWorkingDirectory("/tmp");
		manager->addEngine(config);
	}
	return manager;
}

QSettings* tst_ConfigRecord::makeSettings(const QString& file)
{
	return new QSettings(file, QSettings::IniFormat);
}

void tst_ConfigRecord::write(const QString& file, const QByteArray& data)
{
	QFile f(file);
	QVERIFY(f.open(QIODevice::WriteOnly));
	f.write(data);
}

void tst_ConfigRecord::roundTrip()
{
	QScopedPointer<QSettings> s(makeSettings(path("a.ini")));
	s->setValue("games/pondering", true);
	s->setValue("games/opening_book/depth", 12);
	s->setValue("tournament/type", "gauntlet");
	s->setValue("tournament/last_engine_names", QStringList{"alpha", "beta"});
	s->setValue("pgn/site", "My, site");
	QScopedPointer<EngineManager> engines(makeEngines({"alpha", "beta"}));

	QString error;
	QVERIFY2(ConfigRecord::save(path("rec.json"), *s, engines.data(), &error),
		 qPrintable(error));

	// Change everything, then restore
	s->setValue("games/pondering", false);
	s->setValue("games/opening_book/depth", 99);
	s->setValue("tournament/type", "knockout");
	s->setValue("tournament/last_engine_names", QStringList{"gamma"});
	s->remove("pgn/site");
	s->setValue("games/new_key_after_save", 5);
	engines->setEngines(makeEngines({"gamma"})->engines());

	ConfigRecord::Contents c;
	QVERIFY2(ConfigRecord::load(path("rec.json"), &c, &error), qPrintable(error));
	QVERIFY(c.hasEngines);
	QCOMPARE(c.engines.size(), 2);
	QVERIFY2(ConfigRecord::apply(c, *s, engines.data(), path("engines.json"), &error),
		 qPrintable(error));

	QCOMPARE(s->value("games/pondering").toBool(), true);
	QCOMPARE(s->value("games/opening_book/depth").toInt(), 12);
	QCOMPARE(s->value("tournament/type").toString(), QString("gauntlet"));
	QCOMPARE(s->value("tournament/last_engine_names").toStringList(),
		 QStringList({"alpha", "beta"}));
	QCOMPARE(s->value("pgn/site").toString(), QString("My, site"));

	QCOMPARE(engines->engineCount(), 2);
	QCOMPARE(engines->engineAt(0).name(), QString("alpha"));
	QCOMPARE(engines->engineAt(0).command(), QString("/usr/bin/alpha"));
	QCOMPARE(engines->engineAt(0).pondering(), true);
	QCOMPARE(engines->engineAt(1).name(), QString("beta"));
	QCOMPARE(engines->engineAt(1).pondering(), false);
}

void tst_ConfigRecord::windowLayoutIsSavedAndRestored()
{
	const QByteArray geom("\x01\x02\x00\xff\x03", 5);	// includes NUL and 0xff
	const QByteArray state("\xde\xad\xbe\xef", 4);

	QScopedPointer<QSettings> s(makeSettings(path("a.ini")));
	s->setValue("ui/show_move_arrows", false);
	s->setValue("ui/mainwindow/geometry", geom);
	s->setValue("ui/mainwindow/window_state", state);
	s->setValue("ui/mainwindow/docks/log", true);
	s->setValue("ui/mainwindow/docks/evaluation", false);

	QString error;
	QVERIFY(ConfigRecord::save(path("rec.json"), *s, nullptr, &error));

	s->setValue("ui/mainwindow/geometry", QByteArray("\x09\x09"));
	s->setValue("ui/mainwindow/window_state", QByteArray("zzz"));
	s->setValue("ui/mainwindow/docks/log", false);
	s->setValue("ui/mainwindow/docks/evaluation", true);

	ConfigRecord::Contents c;
	QVERIFY(ConfigRecord::load(path("rec.json"), &c, &error));
	QVERIFY(c.hasWindowLayout);
	QVERIFY(ConfigRecord::apply(c, *s, nullptr, QString(), &error));

	QCOMPARE(s->value("ui/mainwindow/geometry").toByteArray(), geom);
	QCOMPARE(s->value("ui/mainwindow/window_state").toByteArray(), state);
	QCOMPARE(s->value("ui/mainwindow/docks/log").toBool(), true);
	QCOMPARE(s->value("ui/mainwindow/docks/evaluation").toBool(), false);
}

void tst_ConfigRecord::liveWindowLayoutReplacesStaleOne()
{
	QScopedPointer<QSettings> s(makeSettings(path("a.ini")));
	// What the .ini holds is from the previous session
	s->setValue("ui/mainwindow/geometry", QByteArray("old"));
	s->setValue("ui/mainwindow/docks/log", false);
	s->setValue("ui/mainwindow/docks/stale_only", true);

	QVariantMap live;
	live.insert("ui/mainwindow/geometry", QByteArray("live-geometry"));
	live.insert("ui/mainwindow/docks/log", true);
	QString error;
	QVERIFY(ConfigRecord::save(path("rec.json"), *s, nullptr, &error, live));

	ConfigRecord::Contents c;
	QVERIFY(ConfigRecord::load(path("rec.json"), &c, &error));
	QCOMPARE(c.settings.value("ui/mainwindow/geometry").toByteArray(),
		 QByteArray("live-geometry"));
	QCOMPARE(c.settings.value("ui/mainwindow/docks/log").toBool(), true);
	QVERIFY(!c.settings.contains("ui/mainwindow/docks/stale_only"));
	// The .ini itself was not modified by saving
	QCOMPARE(s->value("ui/mainwindow/geometry").toByteArray(), QByteArray("old"));
}

void tst_ConfigRecord::recordWithoutWindowLayoutLeavesWindowAlone()
{
	QScopedPointer<QSettings> s(makeSettings(path("a.ini")));
	s->setValue("ui/show_move_arrows", false);
	QString error;
	QVERIFY(ConfigRecord::save(path("rec.json"), *s, nullptr, &error));

	s->setValue("ui/mainwindow/geometry", QByteArray("\x09\x09"));
	s->setValue("ui/mainwindow/docks/log", false);
	s->setValue("ui/show_move_arrows", true);

	ConfigRecord::Contents c;
	QVERIFY(ConfigRecord::load(path("rec.json"), &c, &error));
	QVERIFY(!c.hasWindowLayout);
	QVERIFY(ConfigRecord::apply(c, *s, nullptr, QString(), &error));
	QCOMPARE(s->value("ui/mainwindow/geometry").toByteArray(), QByteArray("\x09\x09"));
	QCOMPARE(s->value("ui/mainwindow/docks/log").toBool(), false);
	QCOMPARE(s->value("ui/show_move_arrows").toBool(), false);
}

void tst_ConfigRecord::lastConfigDirIsNotSavedOrTouched()
{
	QScopedPointer<QSettings> s(makeSettings(path("a.ini")));
	s->setValue("ui/last_config_dir", "/somewhere");
	QString error;
	QVERIFY(ConfigRecord::save(path("rec.json"), *s, nullptr, &error));

	QFile f(path("rec.json"));
	QVERIFY(f.open(QIODevice::ReadOnly));
	const QJsonObject settings =
		QJsonDocument::fromJson(f.readAll()).object().value("settings").toObject();
	QVERIFY(!settings.contains("ui/last_config_dir"));

	s->setValue("ui/last_config_dir", "/elsewhere");
	ConfigRecord::Contents c;
	QVERIFY(ConfigRecord::load(path("rec.json"), &c, &error));
	QVERIFY(ConfigRecord::apply(c, *s, nullptr, QString(), &error));
	QCOMPARE(s->value("ui/last_config_dir").toString(), QString("/elsewhere"));
}

void tst_ConfigRecord::missingKeysGoBackToDefault()
{
	QScopedPointer<QSettings> s(makeSettings(path("a.ini")));
	s->setValue("games/pondering", true);
	QString error;
	QVERIFY(ConfigRecord::save(path("rec.json"), *s, nullptr, &error));

	s->setValue("tournament/rounds", 7);
	ConfigRecord::Contents c;
	QVERIFY(ConfigRecord::load(path("rec.json"), &c, &error));
	QVERIFY(ConfigRecord::apply(c, *s, nullptr, QString(), &error));
	QVERIFY(!s->contains("tournament/rounds"));
	QCOMPARE(s->value("tournament/rounds", 1).toInt(), 1);
}

void tst_ConfigRecord::settingsOnlyRecordKeepsEngines()
{
	QScopedPointer<QSettings> s(makeSettings(path("a.ini")));
	QString error;
	// Saved without an engine manager -> no "engines" in the file
	QVERIFY(ConfigRecord::save(path("rec.json"), *s, nullptr, &error));

	ConfigRecord::Contents c;
	QVERIFY(ConfigRecord::load(path("rec.json"), &c, &error));
	QVERIFY(!c.hasEngines);

	QScopedPointer<EngineManager> engines(makeEngines({"alpha"}));
	QVERIFY(ConfigRecord::apply(c, *s, engines.data(), path("engines.json"), &error));
	QCOMPARE(engines->engineCount(), 1);
	QVERIFY(!QFile::exists(path("engines.json")));
}

void tst_ConfigRecord::enginesFileIsReadableByEngineManager()
{
	QScopedPointer<QSettings> s(makeSettings(path("a.ini")));
	QScopedPointer<EngineManager> engines(makeEngines({"alpha", "beta", "gamma"}));
	QString error;
	QVERIFY(ConfigRecord::save(path("rec.json"), *s, engines.data(), &error));

	QScopedPointer<EngineManager> target(new EngineManager);
	ConfigRecord::Contents c;
	QVERIFY(ConfigRecord::load(path("rec.json"), &c, &error));
	QVERIFY(ConfigRecord::apply(c, *s, target.data(), path("engines.json"), &error));

	// The file written by apply() is the normal engines.json
	QScopedPointer<EngineManager> reloaded(new EngineManager);
	reloaded->loadEngines(path("engines.json"));
	QCOMPARE(reloaded->engineCount(), 3);
	QCOMPARE(reloaded->engineAt(2).name(), QString("gamma"));
}

void tst_ConfigRecord::rejectsBadFiles_data()
{
	QTest::addColumn<QByteArray>("content");

	QTest::newRow("not json") << QByteArray("hello");
	QTest::newRow("json array") << QByteArray("[1,2,3]");
	QTest::newRow("wrong format")
		<< QByteArray("{\"format\":\"other\",\"version\":1,\"settings\":{}}");
	QTest::newRow("no settings")
		<< QByteArray("{\"format\":\"cutechess-configuration\",\"version\":1}");
	QTest::newRow("future version")
		<< QByteArray("{\"format\":\"cutechess-configuration\",\"version\":99,\"settings\":{}}");
	QTest::newRow("no version")
		<< QByteArray("{\"format\":\"cutechess-configuration\",\"settings\":{}}");
	QTest::newRow("engines not array")
		<< QByteArray("{\"format\":\"cutechess-configuration\",\"version\":1,"
			      "\"settings\":{},\"engines\":5}");
	QTest::newRow("engine not object")
		<< QByteArray("{\"format\":\"cutechess-configuration\",\"version\":1,"
			      "\"settings\":{},\"engines\":[1]}");
	QTest::newRow("empty") << QByteArray();
}

void tst_ConfigRecord::rejectsBadFiles()
{
	QFETCH(QByteArray, content);
	write(path("bad.json"), content);

	ConfigRecord::Contents c;
	QString error;
	QVERIFY(!ConfigRecord::load(path("bad.json"), &c, &error));
	QVERIFY(!error.isEmpty());

	QVERIFY(!ConfigRecord::load(path("does-not-exist.json"), &c, &error));
	QVERIFY(!error.isEmpty());
}

QTEST_MAIN(tst_ConfigRecord)
#include "tst_configrecord.moc"
