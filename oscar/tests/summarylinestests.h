/* Summary chart dotted lines Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SUMMARYLINESTESTS_H
#define SUMMARYLINESTESTS_H

#include "tests/AutoTest.h"

/*! \class SummaryLinesTests
    \brief Saving and loading which dotted lines are switched on for each Overview graph */
class SummaryLinesTests : public QObject
{
    Q_OBJECT
private slots:
    void testEncodeDecodeRoundTrip();
    void testDecodeSkipsMalformed();
};
DECLARE_TEST(SummaryLinesTests)

#endif // SUMMARYLINESTESTS_H
