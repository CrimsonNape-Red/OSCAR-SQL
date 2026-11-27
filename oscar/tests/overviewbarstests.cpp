/* Overview bar Unit Tests
 *
 * Covers where a bar sits in its day's slot as the date range grows (the gap between days
 * shrinks with the bar, stays at least a pixel while there is room, then disappears), the
 * colour stops of the bar shading, the lighter plain fill, the flat top with rounded corners, the text colour
 * on a title strip, and which day a mouse position is in and where that day's cursor line goes.
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

void OverviewBarsTests::testDarkBarColourIsMuted()
{
    // On a dark background pale colours glare, so they are brought down to a medium shade
    QVERIFY(darkBarColour(QColor(Qt::white)).lightnessF() <= 0.8f);
    QVERIFY(darkBarColour(QColor(0xE0, 0xF0, 0xFF)).lightnessF() <= 0.5f);
    // A colour keeps its hue, and is never made brighter
    const QColor blue(0x40, 0x80, 0xff);
    const QColor muted = darkBarColour(blue);
    QVERIFY(qAbs(muted.hslHue() - blue.hslHue()) <= 2);
    QVERIFY(muted.lightnessF() <= blue.lightnessF());
    QVERIFY(muted.hslSaturationF() < blue.hslSaturationF());
    const QColor navy(0x20, 0x30, 0x60);
    QVERIFY(darkBarColour(navy).lightnessF() <= navy.lightnessF() + 0.001f);
    // Shades of one colour stay distinct
    QVERIFY(darkBarColour(QColor(0xFF, 0xA0, 0xA0)).lightnessF() > darkBarColour(QColor(0xFF, 0x00, 0x00)).lightnessF() + 0.05f);
    // Transparency is kept
    QCOMPARE(darkBarColour(QColor(0x40, 0x80, 0xff, 100)).alpha(), 100);
}

void OverviewBarsTests::testDarkGradientDoesNotGlare()
{
    // In dark mode the shading starts from the muted colour and brightens enough to be seen,
    // without glaring
    const QLinearGradient grad = barGradient(QColor(0x40, 0x80, 0xff), 10, 50, true);
    const QGradientStops stops = grad.stops();
    QCOMPARE(stops.size(), 2);
    QCOMPARE(stops.at(0).second, darkBarColour(QColor(0x40, 0x80, 0xff)));
    QVERIFY(stops.at(1).second.lightnessF() >= stops.at(0).second.lightnessF() + 0.12f);
    QVERIFY(stops.at(1).second.lightnessF() <= 0.6f);
    QVERIFY(qAbs(stops.at(1).second.hslHue() - stops.at(0).second.hslHue()) <= 2);
}

void OverviewBarsTests::testDarkGreyBarsStayReadable()
{
    // A grey has no colour to set it off from the dark background, so it is kept well above it
    // (these are what GraphTheme::ink() makes of the Large Leak and Flow Limitation greys)
    const QColor low = darkBarColour(QColor(0x3c, 0x3c, 0x3c));
    const QColor mid = darkBarColour(QColor(0x86, 0x86, 0x86));
    QVERIFY(low.lightnessF() >= 0.4f);
    QVERIFY(mid.lightnessF() > low.lightnessF() + 0.05f);   // shades of grey stay distinct
    QVERIFY(mid.lightnessF() <= 0.8f);
    QCOMPARE(low.hslSaturationF(), 0.0f);                   // and stay grey
}

void OverviewBarsTests::testDarkOrangeStaysAmber()
{
    // Orange and yellow turn brown when darkened like the other colours, so they keep more light
    const QColor orange("orange");
    const QColor amber = darkBarColour(orange);
    QVERIFY(qAbs(amber.hslHue() - orange.hslHue()) <= 2);
    QVERIFY(amber.lightnessF() >= 0.42f);
    QVERIFY(amber.lightnessF() <= 0.5f);        // still no brighter than the original
    // Its lighter shade (the pressure chart's 95% part) stays distinct
    QVERIFY(darkBarColour(QColor(255, 200, 110)).lightnessF() > amber.lightnessF() + 0.03f);
    // Colours away from orange are untouched: blue stays as dark as before
    QVERIFY(darkBarColour(QColor(0x40, 0x80, 0xff)).lightnessF() < 0.40f);
}

void OverviewBarsTests::testOutlineColour()
{
    QCOMPARE(outlineColour(false), QColor(Qt::black));
    // Dark mode: a very light grey, short of white so that it doesn't glare
    const QColor c = outlineColour(true);
    QCOMPARE(c.hslSaturationF(), 0.0f);
    QVERIFY(c.lightnessF() >= 0.75f);
    QVERIFY(c.lightnessF() <= 0.88f);
}

void OverviewBarsTests::testLightBarHasThinBlackOutline()
{
    // Light mode: a one-pixel black line up the sides and across the top, as in OSCAR 2.1.0
    QImage img(60, 40, QImage::Format_ARGB32);
    img.fill(Qt::white);
    {
        QPainter p(&img);
        paintSegment(p, QRectF(0, 0, 40, 40), QColor(0x40, 0x80, 0xff), false, true);
        paintSegment(p, QRectF(50, 0, 3, 40), QColor(0x40, 0x80, 0xff), false, true);   // too narrow for one
    }
    auto black = [&img](int x, int y) { return qGray(img.pixel(x, y)) < 40; };
    QVERIFY(black(0, 20));
    QVERIFY(black(39, 20));
    QVERIFY(black(20, 0));
    QVERIFY(!black(1, 20));             // one pixel only
    QVERIFY(!black(20, 20));
    QVERIFY(!black(20, 39));            // no line along the bottom: the next part's top, or the axis, is there
    QVERIFY(!black(50, 20));
}

void OverviewBarsTests::testClosedOutlineRunsAlongBottom()
{
    // A piece that stands on nothing (a session) is outlined all round
    QImage img(40, 40, QImage::Format_ARGB32);
    img.fill(Qt::white);
    {
        QPainter p(&img);
        paintSegment(p, QRectF(0, 0, 40, 30), QColor(0x40, 0x80, 0xff), true, true, true);
    }
    auto black = [&img](int x, int y) { return qGray(img.pixel(x, y)) < 40; };
    QVERIFY(black(20, 29));
    QVERIFY(!black(20, 28));
    QVERIFY(black(0, 15));
}

void OverviewBarsTests::testOpenOutlineStopsAtBottom()
{
    // The lines up the sides end where the bar ends. Running on past it, they stuck out under
    // the axis line the bar stands on, like the legs of a staple.
    QImage img(40, 40, QImage::Format_ARGB32);
    img.fill(Qt::white);
    {
        QPainter p(&img);
        paintSegment(p, QRectF(0, 0, 40, 30), QColor(0x40, 0x80, 0xff), true, true);
    }
    QCOMPARE(img.pixel(0, 30), QColor(Qt::white).rgba());
    QCOMPARE(img.pixel(39, 30), QColor(Qt::white).rgba());
    QVERIFY(qGray(img.pixel(0, 29)) < 40);      // and they do reach the bottom
    QVERIFY(qGray(img.pixel(39, 29)) < 40);
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

void OverviewBarsTests::testDaySlot()
{
    // A plot from x = 20, 100 pixels wide, showing 10 days: 10 pixels a day
    QCOMPARE(daySlot(20, 100, 10, 20), 0);
    QCOMPARE(daySlot(20, 100, 10, 29), 0);
    QCOMPARE(daySlot(20, 100, 10, 30), 1);      // the line between two days belongs to the later one
    QCOMPARE(daySlot(20, 100, 10, 119), 9);
    QCOMPARE(daySlot(20, 100, 10, 120), 9);     // the right edge is still the last day
    // Outside the plot (and the mouse position of a view the mouse has left)
    QCOMPARE(daySlot(20, 100, 10, 19), -1);
    QCOMPARE(daySlot(20, 100, 10, 121), -1);
    QCOMPARE(daySlot(20, 100, 10, -1), -1);
    // Nothing to mark
    QCOMPARE(daySlot(20, 100, 0, 50), -1);
    QCOMPARE(daySlot(20, 0, 10, 20), -1);
    // A day width that is not a whole number of pixels (1380 / 90 = 15.33): the line between
    // days 2 and 3 falls exactly on pixel 126, which must come out as day 3
    QCOMPARE(daySlot(80, 1380, 90, 125), 2);
    QCOMPARE(daySlot(80, 1380, 90, 126), 3);
    // ... and so for every such line across the plot
    for (int k = 3; k < 90; k += 3) {
        QCOMPARE(daySlot(80, 1380, 90, 80 + k * 46 / 3), k);
        QCOMPARE(daySlot(80, 1380, 90, 80 + k * 46 / 3 - 1), k - 1);
    }
}

void OverviewBarsTests::testDaySlotLineX()
{
    // The middle of the day, on a pixel centre
    QCOMPARE(daySlotLineX(20, 100, 10, 0), 25.5);
    QCOMPARE(daySlotLineX(20, 100, 10, 9), 115.5);
    QCOMPARE(daySlotLineX(0, 100, 3, 1), 50.5);
    QCOMPARE(daySlotLineX(0, 100, 3, 2), 83.5);     // middle at 83.33
    // Every position in a day gives that day's line, and the line is inside the day
    for (int x = 80; x <= 80 + 1380; ++x) {
        const int slot = daySlot(80, 1380, 90, x);
        const double line = daySlotLineX(80, 1380, 90, slot);
        QCOMPARE(daySlot(80, 1380, 90, line), slot);
    }
}

void OverviewBarsTests::testDaySlotAtTime()
{
    // 90 days from a local midnight, across a plot 1380 pixels wide starting at x = 80
    const qint64 minx = 1790000000000LL;
    const qint64 maxx = minx + 90LL * 86400000LL;
    // The cursor time a graph makes of a mouse position (as gGraph::mouseMoveEvent()) gives the
    // same day as that mouse position, at every pixel, including those on the line between days
    const double msPerPixel = double(maxx - minx) / 1380.0;
    for (int x = 81; x < 80 + 1380; ++x) {
        const double time = double(x - 80) * msPerPixel + double(minx);
        QCOMPARE(daySlotAtTime(80, 1380, 90, minx, maxx, time), daySlot(80, 1380, 90, x));
    }
    // ... and in a plot of another width (the same day, wherever it now is)
    QCOMPARE(daySlotAtTime(80, 700, 90, minx, maxx, double(minx) + 3.5 * 86400000.0), 3);
    // Nothing at or beyond the ends: a mouse position outside the plot is held at them
    QCOMPARE(daySlotAtTime(80, 1380, 90, minx, maxx, double(minx)), -1);
    QCOMPARE(daySlotAtTime(80, 1380, 90, minx, maxx, double(maxx)), -1);
    QCOMPARE(daySlotAtTime(80, 1380, 90, minx, maxx, 0.0), -1);         // no cursor yet
    QCOMPARE(daySlotAtTime(80, 1380, 90, minx, minx, double(minx)), -1);
}
