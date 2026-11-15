/* Graph card Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

/*! \class GraphCardTests
    \brief Where a graph card's header and menu button sit, and what the header shows */
class GraphCardTests : public QObject
{
    Q_OBJECT
private slots:
    void testHeaderInsideCard();
    void testMenuButtonAtTopRight();
    void testInHeader();
    void testElideSummary();
    void testHeaderUnits();
    void testSplitHeaderFits();
    void testSplitHeaderNarrow();
    void testSplitHeaderEmpty();
};
DECLARE_TEST(GraphCardTests)
