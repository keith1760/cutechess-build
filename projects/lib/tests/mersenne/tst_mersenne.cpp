#include <QtTest/QTest>
#include <mersenne.h>

class tst_Mersenne: public QObject
{
	Q_OBJECT

	private slots:
		void numbers_data();
		void numbers();
		void reseedAfterDraws();
};

void tst_Mersenne::numbers_data()
{
	QTest::addColumn<quint32>("seed");
	QTest::addColumn<quint32>("random1");
	QTest::addColumn<quint32>("random2");

	QTest::newRow("0")
		<< quint32(0U)
		<< quint32(2357136044U)
		<< quint32(1745961492U);

	QTest::newRow("0xFFFFFFFF")
		<< quint32(0xFFFFFFFFU)
		<< quint32(2720109276U)
		<< quint32(3792022478U);
}

void tst_Mersenne::numbers()
{
	QFETCH(quint32, seed);
	QFETCH(quint32, random1);
	QFETCH(quint32, random2);

	Mersenne::initialize(seed);
	QCOMPARE(Mersenne::random(), random1);
	QCOMPARE(Mersenne::random(), random2);
}

// Re-seeding after some draws must give the same sequence as a fresh seed.
void tst_Mersenne::reseedAfterDraws()
{
	Mersenne::initialize(12345);
	const quint32 a = Mersenne::random();
	const quint32 b = Mersenne::random();

	for (int i = 0; i < 1000; i++)
		Mersenne::random();

	Mersenne::initialize(12345);
	QCOMPARE(Mersenne::random(), a);
	QCOMPARE(Mersenne::random(), b);
}

QTEST_MAIN(tst_Mersenne)
#include "tst_mersenne.moc"
