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
#include "SleepLib/appsettings.h"

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

void DailyDetailsTests::testCardBitsDefault()
{
    // The AHI, Device, Events and Statistics cards start open; the others closed
    const int bits = DefaultDailyCardsOpen;
    QVERIFY(DailyDetails::cardOpen(bits, DC_Ahi));
    QVERIFY(DailyDetails::cardOpen(bits, DC_Events));
    QVERIFY(DailyDetails::cardOpen(bits, DC_Statistics));
    QVERIFY(DailyDetails::cardOpen(bits, DC_Device));
    QVERIFY(!DailyDetails::cardOpen(bits, DC_EventBreakdown));
    QVERIFY(!DailyDetails::cardOpen(bits, DC_Oximeter));
    QVERIFY(!DailyDetails::cardOpen(bits, DC_DeviceSettings));
    QVERIFY(!DailyDetails::cardOpen(bits, DC_SessionInfo));
}

void DailyDetailsTests::testCardBitsRoundTrip()
{
    int bits = DefaultDailyCardsOpen;
    bits = DailyDetails::setCardOpen(bits, DC_SessionInfo, true);
    bits = DailyDetails::setCardOpen(bits, DC_Device, false);
    QVERIFY(DailyDetails::cardOpen(bits, DC_SessionInfo));
    QVERIFY(!DailyDetails::cardOpen(bits, DC_Device));
    // The others are untouched
    QVERIFY(!DailyDetails::cardOpen(bits, DC_EventBreakdown));
    QVERIFY(!DailyDetails::cardOpen(bits, DC_Oximeter));
    QVERIFY(!DailyDetails::cardOpen(bits, DC_DeviceSettings));
    QVERIFY(DailyDetails::cardOpen(bits, DC_Ahi));
    QVERIFY(DailyDetails::cardOpen(bits, DC_Events));
    QVERIFY(DailyDetails::cardOpen(bits, DC_Statistics));
    // Closing one of those leaves the rest
    bits = DailyDetails::setCardOpen(bits, DC_Statistics, false);
    QVERIFY(!DailyDetails::cardOpen(bits, DC_Statistics));
    QVERIFY(DailyDetails::cardOpen(bits, DC_Events));
    // Opening an open card changes nothing
    QCOMPARE(DailyDetails::setCardOpen(bits, DC_SessionInfo, true), bits);
}

void DailyDetailsTests::testCaptureCardsOpen()
{
    const int all = (1 << (DC_Statistics + 1)) - 1;             // the eight cards
    const int pie = 1 << DC_EventBreakdown;
    QCOMPARE(DailyDetails::captureCardsOpen(0), all & ~pie);    // the pie stays closed
    QCOMPARE(DailyDetails::captureCardsOpen(pie), all);         // the pie stays open
    QCOMPARE(DailyDetails::captureCardsOpen(all & ~pie), all & ~pie);
}

void DailyDetailsTests::testZeroEventsImage()
{
    // The "0.0!" image is the text alone in the colour asked for, on nothing: it has to suit
    // the dark panels as well as the white ones (GitLab #434)
    const QColor ink(0xe6, 0xe6, 0xe6);
    const QImage image = DailyDetails::zeroEventsImage(ink);
    QVERIFY(!image.isNull());
    QVERIFY(image.hasAlphaChannel());
    QVERIFY(image.pixelColor(0, 0).alpha() < 8);        // no background
    int solid = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() < 255) continue;
            ++solid;
            QCOMPARE(pixel.rgb(), ink.rgb());
        }
    }
    QVERIFY(solid > 1000);                              // the text is there
}
