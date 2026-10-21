/* Oximetry Event Unit Tests
 *
 * Tests the SpO2 drop and pulse change flags calculated from oximetry data.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef OXIMETRYEVENTSTESTS_H
#define OXIMETRYEVENTSTESTS_H

#include "AutoTest.h"

class OximetryEventsTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testSpO2DropRecordsDepth();
    void testSpO2DropHonoursPreferences();
    void testSpO2DropIgnoresDropouts();
    void testRollingBaselineFollowsDrift();
    void testRollingBaselineExcludesEarlierDrops();
    void testRollingBaselineNeedsHistory();
    void testStaticBaselineMethods();
    void testStaticBaselinePercentile();
    void testChangeOnlyReadings();
    void testChangeOnlyPercentileIsTimeWeighted();
    void testSplitRecordingIsCalculatedWhole();
    void testRollingBaselineSpansDataGaps();
    void testPulseChangeRecordsLargestChange();
    void testPulseChangeIgnoresDropouts();
    void cleanupTestCase();
};

DECLARE_TEST(OximetryEventsTests)

#endif // OXIMETRYEVENTSTESTS_H
