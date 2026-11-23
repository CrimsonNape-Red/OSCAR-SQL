/* BMC G3X Pressure Unit Tests
 *
 * Tests how the BMC G3X parser turns what the card holds into the pressures OSCAR stores:
 * the pressure wave behind the Mask Pressure graph, the pressures stored for a waveform
 * packet, and the pressure records of a night that has events only (#419).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "bmcg3xpressuretests.h"

#include <limits>

#include "SleepLib/loader_plugins/bmcG3xDataParsing.h"

void BmcG3xPressureTests::testPressureWaveInRangeIsKept()
{
    for (qint16 raw : {qint16(0), qint16(1), qint16(700), qint16(4000)}) {
        QCOMPARE(BmcG3xPressureWaveToHundredths(raw), raw);
    }
}

void BmcG3xPressureTests::testPressureWaveNegativeIsZero()
{
    // Values seen on a G3 B20A while the machine starts or the mask is off: -0.01 to
    // -1.25 cmH2O. Read as unsigned they were 65535 downwards, and became 40.00 cmH2O.
    for (qint16 raw : {qint16(-1), qint16(-24), qint16(-125), std::numeric_limits<qint16>::min()}) {
        QCOMPARE(BmcG3xPressureWaveToHundredths(raw), qint16(0));
    }
}

void BmcG3xPressureTests::testPressureWaveAboveRangeIsLimited()
{
    QCOMPARE(BmcG3xPressureWaveToHundredths(4001), qint16(4000));
    QCOMPARE(BmcG3xPressureWaveToHundredths(std::numeric_limits<qint16>::max()), qint16(4000));
}

void BmcG3xPressureTests::testPacketPressureUsesTrend()
{
    const BmcG3xPacketPressure p = BmcG3xPacketPressureHundredths(401, 601, 400, 400);
    QCOMPARE(p.epapHundredths, 401);
    QCOMPARE(p.ipapHundredths, 601);
}

void BmcG3xPressureTests::testPacketPressureWithoutTrendIsInHundredths()
{
    // The first packet of a session on a G3 B20A carried a zero trend, with EPAP 6.00 and
    // IPAP 4.00 known from the event stream. They were stored in the legacy half-cmH2O
    // unit (12 and 8) and so shown as 0.12 and 0.08 cmH2O.
    const BmcG3xPacketPressure p = BmcG3xPacketPressureHundredths(0, 0, 600, 400);
    QCOMPARE(p.epapHundredths, 600);
    QCOMPARE(p.ipapHundredths, 400);
}

void BmcG3xPressureTests::testPacketPressureWithNothingKnownIsEmpty()
{
    const BmcG3xPacketPressure p = BmcG3xPacketPressureHundredths(0, 0, 0, 0);
    QCOMPARE(p.epapHundredths, 0);
    QCOMPARE(p.ipapHundredths, 0);
}

namespace {

//! \brief A pressure record with the same EPAP and IPAP, as in CPAP mode
BmcPressureSnapshot snapshotAt(const QDateTime &when, int hundredths)
{
    BmcPressureSnapshot snap;
    snap.Timestamp      = when;
    snap.EpapHundredths = hundredths;
    snap.IpapHundredths = hundredths;
    return snap;
}

} // namespace

void BmcG3xPressureTests::testLastPressureHeldToSessionEnd()
{
    // A 7.1 hour CPAP night at 7.00 cmH2O with a 15 minute ramp: the device records the
    // pressure only while it ramps. The vendor program reports average 6.9, median 7.0.
    const QDateTime start(QDate(2026, 11, 16), QTime(22, 0, 0));
    const QDateTime end = start.addSecs(7 * 3600 + 400);
    QList<BmcPressureSnapshot> snapshots = {
        snapshotAt(start, 407),
        snapshotAt(start.addSecs(450), 550),
        snapshotAt(start.addSecs(900), 700),
    };

    BmcG3xHoldLastPressureToEnd(snapshots, end);

    QCOMPARE(snapshots.size(), 4);
    QCOMPARE(snapshots.last().Timestamp, end.addSecs(-1));
    QCOMPARE(snapshots.last().EpapHundredths, 700);
    QCOMPARE(snapshots.last().IpapHundredths, 700);
    // The records that were there are untouched.
    QCOMPARE(snapshots.at(2).Timestamp, start.addSecs(900));
    QCOMPARE(snapshots.at(0).EpapHundredths, 407);
}

void BmcG3xPressureTests::testNoHoldWhenLastPressureIsAtEnd()
{
    const QDateTime start(QDate(2026, 11, 16), QTime(22, 0, 0));
    const QDateTime end = start.addSecs(3600);

    // One second before the end, at the end, and (a late record) after it.
    for (int secondsBeforeEnd : {1, 0, -5}) {
        QList<BmcPressureSnapshot> snapshots = {
            snapshotAt(start, 407),
            snapshotAt(end.addSecs(-secondsBeforeEnd), 700),
        };
        BmcG3xHoldLastPressureToEnd(snapshots, end);
        QCOMPARE(snapshots.size(), 2);
    }
}

void BmcG3xPressureTests::testNoHoldWithoutPressureRecords()
{
    QList<BmcPressureSnapshot> snapshots;
    BmcG3xHoldLastPressureToEnd(snapshots, QDateTime(QDate(2026, 11, 16), QTime(22, 0, 0)));
    QVERIFY(snapshots.isEmpty());
}
