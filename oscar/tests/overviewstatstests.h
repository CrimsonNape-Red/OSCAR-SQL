/* Overview statistics row Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef OVERVIEWSTATSTESTS_H
#define OVERVIEWSTATSTESTS_H

#include "tests/AutoTest.h"

/*! \class OverviewStatsTests
    \brief The period a statistics figure is compared with, and whether a change is good or bad */
class OverviewStatsTests : public QObject
{
    Q_OBJECT
private slots:
    void testPreviousRange();
    void testVerdict();
};
DECLARE_TEST(OverviewStatsTests)

#endif // OVERVIEWSTATSTESTS_H
