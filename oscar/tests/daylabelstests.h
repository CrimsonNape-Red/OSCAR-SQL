/* Day label Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DAYLABELSTESTS_H
#define DAYLABELSTESTS_H

#include "tests/AutoTest.h"

/*! \class DayLabelsTests
    \brief Which dates an Overview date axis labels, and where */
class DayLabelsTests : public QObject
{
    Q_OBJECT
private slots:
    void testLabelsDoNotOverlap();
    void testHoverIsBoldAndNeighboursStayPut();
    void testPinnedIsUnderlined();
    void testSingleDay();
    void testGridFollowsDates();
};
DECLARE_TEST(DayLabelsTests)

#endif // DAYLABELSTESTS_H
