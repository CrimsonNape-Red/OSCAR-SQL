/* Overview bar Unit Tests
 *
 * Covers where a bar sits in its day's slot as the date range grows (the gap between days
 * shrinks with the bar, stays at least a pixel while there is room, then disappears), the
 * colour stops of the bar shading, the lighter plain fill, the flat top with rounded corners, and the text colour
 * on a title strip.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "overviewbarstests.h"
#include "Graphs/overviewbars.h"

#include <QImage>
#include <QPainter>

using namespace OverviewBars;

void OverviewBarsTests::testGeometryWideSlot()
{
    const BarGeometry g = barGeometry(84);
    QCOMPARE(g.width, 71.0);                    // gap is 15% of the slot
    QCOMPARE(g.offset, (84.0 - 71.0) / 2.0);
    QVERIFY(g.shaded);
}

void OverviewBarsTests::testGeometrySmallSlotKeepsOnePixelGap()
{
    const BarGeometry g = barGeometry(4);
    QCOMPARE(g.width, 3.0);
    QCOMPARE(4.0 - g.width, 1.0);
    QVERIFY(!g.shaded);
}

void OverviewBarsTests::testGeometryNarrowSlot()
{
    const BarGeometry g = barGeometry(2.5);
    QCOMPARE(g.width, 2.5);
    QCOMPARE(g.offset, 0.0);
    QVERIFY(!g.shaded);
}

void OverviewBarsTests::testBarGradientStops()
{
    // As in OSCAR 2.1.0: the channel colour on the left, twice as bright on the right
    const QLinearGradient grad = barGradient(QColor(0x20, 0x60, 0x90), 10, 50);
    const QGradientStops stops = grad.stops();
    QCOMPARE(stops.size(), 2);
    QCOMPARE(stops.at(0).first, 0.0);
    QCOMPARE(stops.at(0).second, QColor(0x20, 0x60, 0x90));
    QCOMPARE(stops.at(1).first, 1.0);
    QCOMPARE(stops.at(1).second, QColor(128, 192, 255));    // each channel at least 64, doubled
    QCOMPARE(grad.start(), QPointF(10, 0));
    QCOMPARE(grad.finalStop(), QPointF(60, 0));
}

void OverviewBarsTests::testPlainFillLighterAboveThreePixels()
{
    QImage img(10, 10, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        paintSegment(p, QRectF(0, 0, 6, 10), QColor(0x20, 0x60, 0x90), true, false);
        paintSegment(p, QRectF(7, 0, 3, 10), QColor(0x20, 0x60, 0x90), true, false);
    }
    QCOMPARE(QColor(img.pixel(3, 5)), QColor(80, 120, 180));     // 25% brighter, as 2.1.0
    QCOMPARE(QColor(img.pixel(8, 5)), QColor(0x20, 0x60, 0x90)); // 3 px or less: channel colour
}

void OverviewBarsTests::testTopIsFlatWithRoundedCorners()
{
    // A 40 px wide bar top painted onto a clear image
    QImage img(40, 40, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        paintSegment(p, QRectF(0, 0, 40, 40), QColor(0x40, 0x80, 0xff), true, true);
    }
    auto painted = [&img](int x, int y) { return qAlpha(img.pixel(x, y)) > 128; };
    QVERIFY(!painted(0, 0));            // rounded corner
    QVERIFY(!painted(39, 0));
    QVERIFY(painted(5, 0));             // flat across the top beyond the corner, not domed
    QVERIFY(painted(34, 0));
    QVERIFY(painted(20, 0));
    QVERIFY(painted(0, 10));            // straight sides below the corner
    QVERIFY(painted(39, 10));
}

void OverviewBarsTests::testContrastingText()
{
    QCOMPARE(contrastingText(QColor("#90EE90")), QColor(Qt::black));
    QCOMPARE(contrastingText(QColor(Qt::black)), QColor(Qt::white));
    QCOMPARE(contrastingText(QColor("#6D4BC9")), QColor(Qt::white));
}

void OverviewBarsTests::testClassicGeometryHasNoGap()
{
    // The original (2.1) look: bars touch, and are shaded once wider than 8 pixels
    const BarGeometry wide = classicBarGeometry(84);
    QCOMPARE(wide.offset, 0.0);
    QCOMPARE(wide.width, 84.0);
    QVERIFY(wide.shaded);
    const BarGeometry narrow = classicBarGeometry(6);
    QCOMPARE(narrow.width, 6.0);
    QVERIFY(!narrow.shaded);
}

void OverviewBarsTests::testClassicSegmentSquareTop()
{
    QImage img(40, 40, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        paintClassicSegment(p, QRectF(0, 0, 40, 40), QColor(0x40, 0x80, 0xff), 40);
    }
    QVERIFY(qAlpha(img.pixel(0, 0)) > 128);     // square corners, no rounding
    QVERIFY(qAlpha(img.pixel(39, 0)) > 128);
    // The slice colour at the left edge, brightening to the right
    QVERIFY(qAbs(qRed(img.pixel(0, 20)) - 0x40) <= 3);
    QVERIFY(qRed(img.pixel(39, 20)) > 0x70);
}
