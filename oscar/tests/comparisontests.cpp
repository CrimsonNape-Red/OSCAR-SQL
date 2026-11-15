/* Comparison and formatting Unit Tests
 *
 * Covers how a night's value is compared with an average (direction, the no-change band,
 * empty or zero baselines), how two comparisons combine into a card tint, and the
 * duration formats used on the Welcome page cards.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "comparisontests.h"
#include "uikit/comparison.h"
#include "uikit/formatting.h"

using namespace UiKit;

void ComparisonTests::testLowerIsBetter()
{
    Comparison worse = compare(3.65, 3.32, 7, Better::Lower);
    QCOMPARE(worse.trend, Trend::Bad);
    QCOMPARE(worse.percent, 10);

    Comparison better = compare(2.0, 3.0, 7, Better::Lower);
    QCOMPARE(better.trend, Trend::Good);
    QCOMPARE(better.percent, -33);
}

void ComparisonTests::testHigherIsBetter()
{
    Comparison c = compare(6.30, 5.25, 7, Better::Higher);
    QCOMPARE(c.trend, Trend::Good);
    QCOMPARE(c.percent, 20);
}

void ComparisonTests::testNeutral()
{
    Comparison c = compare(11.3, 10.9, 7, Better::Neutral);
    QCOMPARE(c.trend, Trend::Neutral);
    QCOMPARE(c.percent, 4);
}

void ComparisonTests::testNoChangeBand()
{
    QCOMPARE(compare(3.33, 3.32, 7, Better::Lower).trend, Trend::Same);

    Comparison edge = compare(1.02, 1.0, 7, Better::Lower);
    QCOMPARE(edge.trend, Trend::Bad);
    QCOMPARE(edge.percent, 2);
}

void ComparisonTests::testZeroAverage()
{
    QCOMPARE(compare(0, 0, 7, Better::Lower).trend, Trend::Same);
    QCOMPARE(compare(72, 0, 7, Better::Lower).trend, Trend::None);
}

void ComparisonTests::testNoHistory()
{
    Comparison c = compare(3.0, 0, 0, Better::Lower);
    QCOMPARE(c.trend, Trend::None);
    QCOMPARE(deltaText(c), QString::fromUtf8("—"));
}

void ComparisonTests::testStatusTint()
{
    auto c = [](Trend t) { Comparison r; r.trend = t; return r; };
    QCOMPARE(statusTint(c(Trend::Bad), c(Trend::Bad)), StatusTint::Bad);
    QCOMPARE(statusTint(c(Trend::Good), c(Trend::Good)), StatusTint::Good);
    QCOMPARE(statusTint(c(Trend::Good), c(Trend::Bad)), StatusTint::None);
    QCOMPARE(statusTint(c(Trend::Bad), c(Trend::Same)), StatusTint::None);
    QCOMPARE(statusTint(c(Trend::Neutral), c(Trend::Neutral)), StatusTint::None);
}

void ComparisonTests::testDeltaText()
{
    // A real change is shown by the arrow alone, with no text
    Comparison up; up.trend = Trend::Bad; up.percent = 10;
    QCOMPARE(deltaText(up), QString());

    Comparison down; down.trend = Trend::Good; down.percent = -33;
    QCOMPARE(deltaText(down), QString());

    Comparison same; same.trend = Trend::Same; same.percent = 0;
    QCOMPARE(deltaText(same), QString("same"));
}

void ComparisonTests::testDirection()
{
    Comparison up; up.trend = Trend::Bad; up.percent = 10;
    QCOMPARE(direction(up), Direction::Up);

    Comparison down; down.trend = Trend::Good; down.percent = -33;
    QCOMPARE(direction(down), Direction::Down);

    Comparison neutralUp; neutralUp.trend = Trend::Neutral; neutralUp.percent = 5;
    QCOMPARE(direction(neutralUp), Direction::Up);

    Comparison same; same.trend = Trend::Same; same.percent = 1;
    QCOMPARE(direction(same), Direction::None);

    QCOMPARE(direction(Comparison()), Direction::None);
}

void ComparisonTests::testFormatting()
{
    QCOMPARE(formatHoursMinutes(6.30), QString("6h 18m"));
    QCOMPARE(formatHoursMinutes(0.5), QString("0h 30m"));
    QCOMPARE(formatMinutesSeconds(72), QString("1m 12s"));
    QCOMPARE(formatMinutesSeconds(45), QString("45s"));
    QCOMPARE(formatMinutesSeconds(3900), QString("1h 05m"));
}
