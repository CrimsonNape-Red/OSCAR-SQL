/* CSV oximetry Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef CSVOXIMETRYTESTS_H
#define CSVOXIMETRYTESTS_H

#include "AutoTest.h"

class CsvOximetryTests : public QObject
{
    Q_OBJECT

  private slots:
    void testPlainRows();
    void testHeaderInAnyOrder();
    void testSeparateDateAndTimeColumns();
    void testMissingAndOutOfRange();
    void testUnsortedAndRepeated();
    void testGapsSplitRecordings();
    void testOffsetAndVariants();
    void testO2InsightProExport();
    void testBadLinesReported();
    void testUnusableFiles();
};

DECLARE_TEST(CsvOximetryTests)

#endif // CSVOXIMETRYTESTS_H
