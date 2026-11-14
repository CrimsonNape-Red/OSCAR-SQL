/* Daily details Unit Tests
 *
 * Covers DailyDetails::classify(), the choice among the kinds of day the Daily page's Details
 * tab draws differently (no data, an ordinary CPAP night, compliance-only data, sessions all
 * off, a zero-length session, and data from other devices only).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "dailydetailstests.h"
#include "dailydetails.h"

using DailyDetails::DayKind;
using DailyDetails::classify;

void DailyDetailsTests::testClassifyNoData()
{
    QCOMPARE(classify(false, 0, 0, false, false), DayKind::NoData);
}

void DailyDetailsTests::testClassifyNonCpapOnly()
{
    QCOMPARE(classify(false, 0, 0, false, true), DayKind::NonCpapOnly);
}

void DailyDetailsTests::testClassifyCpap()
{
    QCOMPARE(classify(true, 6.5, 1, false, false), DayKind::Cpap);
    QCOMPARE(classify(true, 6.5, 2, false, true), DayKind::Cpap);     // with an oximeter too
}

void DailyDetailsTests::testClassifyComplianceOnly()
{
    QCOMPARE(classify(true, 6.5, 1, true, false), DayKind::ComplianceOnly);
}

void DailyDetailsTests::testClassifySessionsOff()
{
    QCOMPARE(classify(true, 0, 2, false, false), DayKind::SessionsOff);
}

void DailyDetailsTests::testClassifyZeroHours()
{
    QCOMPARE(classify(true, 0, 0, false, false), DayKind::ZeroHours);
}
