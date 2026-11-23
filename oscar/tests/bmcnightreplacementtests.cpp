/* BMC Night Replacement Unit Tests
 *
 * Tests the rule that decides whether a night read from a BMC card may replace what is
 * already stored for the same hours: a night the card holds as events only must not
 * replace a stored night that has waveform (#419).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "bmcnightreplacementtests.h"

#include "SleepLib/loader_plugins/bmc_loader.h"

namespace {

constexpr qint64 kHour = 3600 * 1000LL;
//! An arbitrary evening, ms since the epoch; only differences matter here.
constexpr qint64 kEvening = 1790000000000LL;

//! \brief A session from \a startHour to \a endHour after kEvening
BmcSessionSpan span(SessionID id, double startHour, double endHour, bool hasWaveform)
{
    BmcSessionSpan s;
    s.id          = id;
    s.startMs     = kEvening + static_cast<qint64>(startHour * kHour);
    s.endMs       = kEvening + static_cast<qint64>(endHour * kHour);
    s.hasWaveform = hasWaveform;
    return s;
}

} // namespace

void BmcNightReplacementTests::testNightWithWaveformReplacesStored()
{
    const BmcReplacementPlan plan = BmcPlanNightReplacement(
        { span(100, 0.0, 8.0, true) },
        { span(101, 0.01, 8.0, true) });
    QVERIFY(plan.keepStored.isEmpty());
    QVERIFY(plan.skipIncoming.isEmpty());
}

void BmcNightReplacementTests::testEventsOnlyNightKeepsStoredWaveform()
{
    // The card was formatted: the device wrote the night back with events only. The
    // session starts a few seconds apart from the stored one, so the ids differ.
    BmcReplacementPlan plan = BmcPlanNightReplacement(
        { span(100, 0.0, 8.0, false) },
        { span(101, 0.01, 8.0, true) });
    QCOMPARE(plan.keepStored, QSet<SessionID>({101}));
    QCOMPARE(plan.skipIncoming, QSet<SessionID>({100}));

    // The same when both carry the same id.
    plan = BmcPlanNightReplacement(
        { span(100, 0.0, 8.0, false) },
        { span(100, 0.0, 8.0, true) });
    QCOMPARE(plan.keepStored, QSet<SessionID>({100}));
    QCOMPARE(plan.skipIncoming, QSet<SessionID>({100}));
}

void BmcNightReplacementTests::testEventsOnlyNightReplacesStoredEventsOnly()
{
    const BmcReplacementPlan plan = BmcPlanNightReplacement(
        { span(100, 0.0, 8.0, false) },
        { span(101, 0.01, 8.0, false) });
    QVERIFY(plan.keepStored.isEmpty());
    QVERIFY(plan.skipIncoming.isEmpty());
}

void BmcNightReplacementTests::testLaterEventsOnlySessionIsImported()
{
    // Stored: the first part of the night, with waveform. On the card: that part and a
    // later session, both events only.
    const BmcReplacementPlan plan = BmcPlanNightReplacement(
        { span(100, 0.0, 4.0, false), span(200, 5.0, 8.0, false) },
        { span(101, 0.01, 4.0, true) });
    QCOMPARE(plan.keepStored, QSet<SessionID>({101}));
    QCOMPARE(plan.skipIncoming, QSet<SessionID>({100}));
}

void BmcNightReplacementTests::testStoredWaveformBetweenSessionsIsKept()
{
    // The stored session overlaps neither session on the card, but lies between them,
    // inside the span the import clears. It has waveform, so it stays.
    const BmcReplacementPlan plan = BmcPlanNightReplacement(
        { span(100, 0.0, 2.0, false), span(200, 6.0, 8.0, false) },
        { span(150, 3.0, 5.0, true), span(90, 0.0, 2.0, false) });
    QCOMPARE(plan.keepStored, QSet<SessionID>({150}));
    QVERIFY(plan.skipIncoming.isEmpty());
}
