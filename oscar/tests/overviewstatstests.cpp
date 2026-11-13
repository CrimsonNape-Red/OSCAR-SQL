/* Overview statistics row Unit Tests
 *
 * Covers the comparison period (the same number of days just before the dates on screen)
 * and the rule that decides whether a change is shown as good, bad or neither.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "overviewstatstests.h"
#include "overviewstats.h"

using namespace OverviewStats;

void OverviewStatsTests::testPreviousRange()
{
    // 30 days ending 9 Nov are compared with the 30 days ending 10 Oct
    const Range month = previousRange({ QDate(2026, 10, 11), QDate(2026, 11, 9) });
    QCOMPARE(month.start, QDate(2026, 9, 11));
    QCOMPARE(month.end, QDate(2026, 10, 10));

    const Range day = previousRange({ QDate(2026, 3, 1), QDate(2026, 3, 1) });
    QCOMPARE(day.start, QDate(2026, 2, 28));
    QCOMPARE(day.end, QDate(2026, 2, 28));
}

void OverviewStatsTests::testVerdict()
{
    QCOMPARE(verdict(1.96, 2.10, UiKit::Better::Lower), Verdict::Good);
    QCOMPARE(verdict(5.8, 4.6, UiKit::Better::Lower), Verdict::Bad);
    QCOMPARE(verdict(6.5, 5.9, UiKit::Better::Higher), Verdict::Good);
    QCOMPARE(verdict(5.1, 5.9, UiKit::Better::Higher), Verdict::Bad);
    QCOMPARE(verdict(11.2, 11.4, UiKit::Better::Neutral), Verdict::Neutral);
    QCOMPARE(verdict(3.0, 3.0, UiKit::Better::Lower), Verdict::Neutral);
    // Equal at the precision shown counts as unchanged
    QCOMPARE(verdict(3.001, 3.004, UiKit::Better::Lower), Verdict::Neutral);
    QCOMPARE(verdict(87.52, 87.49, UiKit::Better::Higher, 1), Verdict::Neutral);
}
