/* Comparison and formatting Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

class ComparisonTests : public QObject
{
    Q_OBJECT
private slots:
    void testLowerIsBetter();
    void testHigherIsBetter();
    void testNeutral();
    void testNoChangeBand();
    void testZeroAverage();
    void testNoHistory();
    void testStatusTint();
    void testDeltaText();
    void testDirection();
    void testFormatting();
};
DECLARE_TEST(ComparisonTests)
