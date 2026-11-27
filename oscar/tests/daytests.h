/* Day Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

/*! \class DayTests
    \brief When a day counts as summary-only (Day::summaryOnly()), when a channel counts as
    recorded on a day (Day::channelHasRecordedData()), and which days are nights of oximetry
    with the Statistics figures taken from them (Day::hasRecordedOximetry(),
    Profile::oximetryDays(), StatisticsRow::oximetryValue()) */
class DayTests : public QObject
{
    Q_OBJECT
private slots:
    void testRecordedDataNeedsSamplesOrSummary();
    void testRecordedDataFromEventLists();
    void testRecordedDataIgnoresDisabledSessions();
    void testOximetryFromAnyDevice();
    void testOximetryStatisticsUseOximetryNights();
    void testAllSummarySessions();
    void testMixedDayIsNotSummaryOnly();
    void testOximeterDoesNotCountAsDetail();
    void testPerMachine();
    void testEmptyDay();
};
DECLARE_TEST(DayTests)
