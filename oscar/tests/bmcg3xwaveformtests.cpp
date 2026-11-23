/* BMC G3X Waveform Packet Unit Tests
 *
 * Tests how values read from a BMC G3X waveform packet are turned into the samples OSCAR
 * stores: the pressure wave behind the Mask Pressure graph, and the pressures stored for
 * a packet (#419).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "bmcg3xwaveformtests.h"

#include <limits>

#include "SleepLib/loader_plugins/bmcG3xDataParsing.h"

void BmcG3xWaveformTests::testPressureWaveInRangeIsKept()
{
    for (qint16 raw : {qint16(0), qint16(1), qint16(700), qint16(4000)}) {
        QCOMPARE(BmcG3xPressureWaveToHundredths(raw), raw);
    }
}

void BmcG3xWaveformTests::testPressureWaveNegativeIsZero()
{
    // Values seen on a G3 B20A while the machine starts or the mask is off: -0.01 to
    // -1.25 cmH2O. Read as unsigned they were 65535 downwards, and became 40.00 cmH2O.
    for (qint16 raw : {qint16(-1), qint16(-24), qint16(-125), std::numeric_limits<qint16>::min()}) {
        QCOMPARE(BmcG3xPressureWaveToHundredths(raw), qint16(0));
    }
}

void BmcG3xWaveformTests::testPressureWaveAboveRangeIsLimited()
{
    QCOMPARE(BmcG3xPressureWaveToHundredths(4001), qint16(4000));
    QCOMPARE(BmcG3xPressureWaveToHundredths(std::numeric_limits<qint16>::max()), qint16(4000));
}

void BmcG3xWaveformTests::testPacketPressureUsesTrend()
{
    const BmcG3xPacketPressure p = BmcG3xPacketPressureHundredths(401, 601, 400, 400);
    QCOMPARE(p.epapHundredths, 401);
    QCOMPARE(p.ipapHundredths, 601);
}

void BmcG3xWaveformTests::testPacketPressureWithoutTrendIsInHundredths()
{
    // The first packet of a session on a G3 B20A carried a zero trend, with EPAP 6.00 and
    // IPAP 4.00 known from the event stream. They were stored in the legacy half-cmH2O
    // unit (12 and 8) and so shown as 0.12 and 0.08 cmH2O.
    const BmcG3xPacketPressure p = BmcG3xPacketPressureHundredths(0, 0, 600, 400);
    QCOMPARE(p.epapHundredths, 600);
    QCOMPARE(p.ipapHundredths, 400);
}

void BmcG3xWaveformTests::testPacketPressureWithNothingKnownIsEmpty()
{
    const BmcG3xPacketPressure p = BmcG3xPacketPressureHundredths(0, 0, 0, 0);
    QCOMPARE(p.epapHundredths, 0);
    QCOMPARE(p.ipapHundredths, 0);
}
