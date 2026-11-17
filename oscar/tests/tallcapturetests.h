/* Tall capture Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

/*! \class TallCaptureTests
    \brief How tall a full-height Daily screenshot is, and how the window grab is lengthened */
class TallCaptureTests : public QObject
{
    Q_OBJECT
private slots:
    void testGraphAreaCrossingGraphDrawnWhole();
    void testGraphAreaGraphEndsOnLine();
    void testGraphAreaLineInGap();
    void testGraphAreaGraphsEndAboveLine();
    void testGraphAreaNoGraphs();
    void testGraphAreaFractionalHeights();
    void testCappedPageHeight();
    void testInsertRows();
    void testInsertRowsNone();
    void testInsertRowsKeepsRatio();
};
DECLARE_TEST(TallCaptureTests)
