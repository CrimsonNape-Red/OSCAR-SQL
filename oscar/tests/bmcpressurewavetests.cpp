/* BMC G2S Pressure Wave Unit Tests
 *
 * Tests the conversion of the legacy BMC G2S pressure wave, which the device stores in
 * raw sensor units instead of the tenths of cmH2O used by the G3 and E5 (#393).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "bmcpressurewavetests.h"

#include <limits>

#include "SleepLib/loader_plugins/bmcDataParsing.h"

void BmcPressureWaveTests::testOnlyG2sModelsAreConverted()
{
    QVERIFY(BmcModelUsesG2sPressureWave(QStringLiteral("G2S A20")));
    QVERIFY(BmcModelUsesG2sPressureWave(QStringLiteral("  g2s a20 ")));

    // Models whose pressure wave was checked and is already tenths of cmH2O.
    QVERIFY(!BmcModelUsesG2sPressureWave(QStringLiteral("G3 A20")));
    QVERIFY(!BmcModelUsesG2sPressureWave(QStringLiteral("G3 B20A")));
    QVERIFY(!BmcModelUsesG2sPressureWave(QStringLiteral("G3 B25A")));
    QVERIFY(!BmcModelUsesG2sPressureWave(QStringLiteral("E5 B25A")));
    QVERIFY(!BmcModelUsesG2sPressureWave(QStringLiteral("E5 B25A Plus330")));
    QVERIFY(!BmcModelUsesG2sPressureWave(QString()));
}

void BmcPressureWaveTests::testConversionMatchesSetPressure()
{
    // Median raw sample observed on a G2S A20 at each set pressure (cmH2O).
    struct Point { qint16 raw; double setPressure; };
    const Point points[] = {
        { 203, 4.0 }, { 226, 5.0 }, { 249, 6.0 }, { 270, 7.0 },
        { 293, 8.0 }, { 316, 9.0 }, { 328, 9.5 },
    };

    for (const Point &p : points) {
        const double cmH2O = BmcG2sPressureWaveToHundredths(p.raw) * kBmcG2sPressureWaveGain;
        QVERIFY2(qAbs(cmH2O - p.setPressure) <= 0.1,
                 qPrintable(QStringLiteral("raw %1 converted to %2 cmH2O, expected %3")
                                .arg(p.raw).arg(cmH2O).arg(p.setPressure)));
    }
}

void BmcPressureWaveTests::testConversionIsClamped()
{
    // A zero-filled sample is no reading; it must not become a negative pressure.
    QCOMPARE(BmcG2sPressureWaveToHundredths(0), qint16(0));
    QCOMPARE(BmcG2sPressureWaveToHundredths(-500), qint16(0));
    QCOMPARE(BmcG2sPressureWaveToHundredths(std::numeric_limits<qint16>::max()),
             std::numeric_limits<qint16>::max());
}
