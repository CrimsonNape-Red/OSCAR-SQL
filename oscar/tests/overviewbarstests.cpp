/* Overview bar Unit Tests
 *
 * Covers where a bar sits in its day's slot as the date range grows (the gap between days
 * shrinks with the bar, stays at least a pixel while there is room, then disappears), the
 * colour stops of the cylinder shading, and the text colour on a title strip.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "overviewbarstests.h"
#include "Graphs/overviewbars.h"

using namespace OverviewBars;

void OverviewBarsTests::testGeometryWideSlot()
{
    const BarGeometry g = barGeometry(84);
    QCOMPARE(g.width, 59.0);
    QCOMPARE(g.offset, (84.0 - 59.0) / 2.0);
    QVERIFY(g.cylinder);
}

void OverviewBarsTests::testGeometrySmallSlotKeepsOnePixelGap()
{
    const BarGeometry g = barGeometry(4);
    QCOMPARE(g.width, 3.0);
    QCOMPARE(4.0 - g.width, 1.0);
    QVERIFY(!g.cylinder);
}

void OverviewBarsTests::testGeometryNarrowSlot()
{
    const BarGeometry g = barGeometry(2.5);
    QCOMPARE(g.width, 2.5);
    QCOMPARE(g.offset, 0.0);
    QVERIFY(!g.cylinder);
}

void OverviewBarsTests::testCylinderGradientStops()
{
    const QLinearGradient grad = cylinderGradient(QColor(0x40, 0x80, 0xff), 10, 50);
    const QGradientStops stops = grad.stops();
    QCOMPARE(stops.size(), 5);
    QCOMPARE(stops.at(0).first, 0.0);
    QCOMPARE(stops.at(0).second, QColor(38, 77, 153));      // 40% towards black
    QCOMPARE(stops.at(1).first, 0.28);
    QCOMPARE(stops.at(1).second, QColor(59, 118, 235));     // 8% towards black
    QCOMPARE(stops.at(2).first, 0.45);
    QCOMPARE(stops.at(2).second, QColor(106, 156, 255));    // 22% towards white
    QCOMPARE(stops.at(3).first, 0.62);
    QCOMPARE(stops.at(3).second, QColor(59, 118, 235));
    QCOMPARE(stops.at(4).first, 1.0);
    QCOMPARE(stops.at(4).second, QColor(35, 70, 140));      // 45% towards black
    QCOMPARE(grad.start(), QPointF(10, 0));
    QCOMPARE(grad.finalStop(), QPointF(60, 0));
}

void OverviewBarsTests::testContrastingText()
{
    QCOMPARE(contrastingText(QColor("#90EE90")), QColor(Qt::black));
    QCOMPARE(contrastingText(QColor(Qt::black)), QColor(Qt::white));
    QCOMPARE(contrastingText(QColor("#6D4BC9")), QColor(Qt::white));
}
