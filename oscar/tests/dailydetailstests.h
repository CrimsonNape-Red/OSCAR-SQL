/* Daily details Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

/*! \class DailyDetailsTests
    \brief The kind of day the Daily page's Details tab shows, and its stored card states */
class DailyDetailsTests : public QObject
{
    Q_OBJECT
private slots:
    void testClassifyNoData();
    void testClassifyNonCpapOnly();
    void testClassifyCpap();
    void testClassifyComplianceOnly();
    void testClassifySessionsOff();
    void testClassifyZeroHours();
    void testCardBitsDefault();
    void testCardBitsRoundTrip();
    void testCaptureCardsOpen();
    void testZeroEventsImage();
};
DECLARE_TEST(DailyDetailsTests)
