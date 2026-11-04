#include <QtTest/QTest>
#include <QSet>
#include <swisstournament.h>

typedef QMap< QPair<int, int>, int > PlayedMap;

class tst_SwissTournament: public QObject
{
	Q_OBJECT

	private slots:
		void firstRoundDutch();
		void firstRoundOddBye();
		void scoreGroupsMeet();
		void noRematchWhenAvoidable();
		void fullSwissHasNoRematches_data();
		void fullSwissHasNoRematches();
		void rematchesWhenUnavoidable();
		void byeRotates();
		void twoPlayers();
};

static PlayedMap addPairs(PlayedMap played,
			  const QList< QPair<int, int> >& pairs)
{
	for (const auto& p : pairs)
		played[qMakePair(qMin(p.first, p.second),
				 qMax(p.first, p.second))]++;
	return played;
}

void tst_SwissTournament::firstRoundDutch()
{
	int bye = 0;
	auto pairs = SwissTournament::computePairings(
		QVector<int>(8, 0), PlayedMap(), QVector<bool>(8, false), &bye);
	QCOMPARE(bye, -1);
	QList< QPair<int, int> > expected = { {0,4}, {1,5}, {2,6}, {3,7} };
	QCOMPARE(pairs, expected);
}

void tst_SwissTournament::firstRoundOddBye()
{
	int bye = 0;
	auto pairs = SwissTournament::computePairings(
		QVector<int>(5, 0), PlayedMap(), QVector<bool>(5, false), &bye);
	QCOMPARE(bye, 4);
	QCOMPARE(pairs.size(), 2);
	QList< QPair<int, int> > expected = { {0,2}, {1,3} };
	QCOMPARE(pairs, expected);
}

void tst_SwissTournament::scoreGroupsMeet()
{
	// Players 0-3 won in round 1 (2 half points each x 2 games = 4),
	// players 4-7 lost. Round 2 must pair winners with winners.
	QVector<int> points = { 2, 2, 2, 2, 0, 0, 0, 0 };
	PlayedMap played = addPairs(PlayedMap(), { {0,4}, {1,5}, {2,6}, {3,7} });
	int bye;
	auto pairs = SwissTournament::computePairings(
		points, played, QVector<bool>(8, false), &bye);
	QCOMPARE(pairs.size(), 4);
	for (const auto& p : pairs)
		QCOMPARE(points[p.first], points[p.second]);
}

void tst_SwissTournament::noRematchWhenAvoidable()
{
	QVector<int> points = { 2, 2, 0, 0 };
	PlayedMap played = addPairs(PlayedMap(), { {0,2}, {1,3} });
	int bye;
	auto pairs = SwissTournament::computePairings(
		points, played, QVector<bool>(4, false), &bye);
	QList< QPair<int, int> > expected = { {0,1}, {2,3} };
	QCOMPARE(pairs, expected);
}

void tst_SwissTournament::fullSwissHasNoRematches_data()
{
	QTest::addColumn<int>("players");
	for (int n : { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 16, 17, 24, 31 })
		QTest::newRow(qPrintable(QString::number(n))) << n;
}

// Plays n-1 rounds (a complete round robin worth of Swiss rounds) with
// pseudo-random results: nobody may meet twice, every round must be a
// valid pairing and byes must be shared out.
void tst_SwissTournament::fullSwissHasNoRematches()
{
	QFETCH(int, players);
	const int n = players;
	QVector<int> points(n, 0);
	QVector<bool> hadBye(n, false);
	PlayedMap played;
	unsigned seed = 12345 + n;

	for (int round = 1; round <= n - 1; round++)
	{
		int bye;
		auto pairs = SwissTournament::computePairings(points, played, hadBye, &bye);
		QCOMPARE(pairs.size(), n / 2);
		QCOMPARE(bye == -1, n % 2 == 0);

		QSet<int> seen;
		for (const auto& p : pairs)
		{
			QVERIFY(p.first != p.second);
			QVERIFY(!seen.contains(p.first));
			QVERIFY(!seen.contains(p.second));
			seen << p.first << p.second;
			QCOMPARE(played.value(qMakePair(qMin(p.first, p.second),
							qMax(p.first, p.second))), 0);

			seed = seed * 1103515245u + 12345u;
			switch ((seed >> 16) % 3)
			{
			case 0: points[p.first] += 2; break;
			case 1: points[p.second] += 2; break;
			default: points[p.first]++; points[p.second]++; break;
			}
		}
		if (bye != -1)
		{
			QVERIFY(!seen.contains(bye));
			seen << bye;
			// Everybody had a bye before the same player gets a second
			if (round <= n)
				QVERIFY(!hadBye[bye]);
			hadBye[bye] = true;
			points[bye] += 2;
		}
		QCOMPARE(seen.size(), n);
		played = addPairs(played, pairs);
	}
}

void tst_SwissTournament::rematchesWhenUnavoidable()
{
	// 4 players, 4 rounds: after 3 rounds everybody has met everybody
	QVector<int> points(4, 0);
	PlayedMap played;
	for (int round = 0; round < 6; round++)
	{
		int bye;
		auto pairs = SwissTournament::computePairings(
			points, played, QVector<bool>(4, false), &bye);
		QCOMPARE(pairs.size(), 2);
		played = addPairs(played, pairs);
	}
}

void tst_SwissTournament::byeRotates()
{
	QVector<int> points(3, 0);
	QVector<bool> hadBye(3, false);
	PlayedMap played;
	QSet<int> byes;
	for (int round = 0; round < 3; round++)
	{
		int bye;
		auto pairs = SwissTournament::computePairings(points, played, hadBye, &bye);
		QVERIFY(bye >= 0);
		QVERIFY(!byes.contains(bye));
		byes << bye;
		hadBye[bye] = true;
		points[bye] += 2;
		played = addPairs(played, pairs);
	}
	QCOMPARE(byes.size(), 3);
}

void tst_SwissTournament::twoPlayers()
{
	int bye;
	PlayedMap played;
	for (int round = 0; round < 3; round++)
	{
		auto pairs = SwissTournament::computePairings(
			QVector<int>(2, 0), played, QVector<bool>(2, false), &bye);
		QCOMPARE(bye, -1);
		QCOMPARE(pairs.size(), 1);
		played = addPairs(played, pairs);
	}
}

QTEST_MAIN(tst_SwissTournament)
#include "tst_swisstournament.moc"
