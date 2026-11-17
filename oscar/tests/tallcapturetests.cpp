/* Tall capture Unit Tests
 *
 * Covers the arithmetic of the full-height Daily screenshot: how tall the graph area has to be
 * so the graph crossing the bottom of the left sidebar is drawn whole, the cap on the page's
 * height, and the lengthening of the window grab that the tall page is painted onto.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tallcapturetests.h"
#include "Graphs/tallcapture.h"

#include <QImage>

void TallCaptureTests::testGraphAreaCrossingGraphDrawnWhole()
{
    // The graphs occupy 0-100, 108-208 and 216-316; the third crosses the line
    QCOMPARE(TallCapture::graphAreaHeight({100, 100, 100}, 8, 250), 316);
}

void TallCaptureTests::testGraphAreaGraphEndsOnLine()
{
    QCOMPARE(TallCapture::graphAreaHeight({100, 100, 100}, 8, 208), 208);
}

void TallCaptureTests::testGraphAreaLineInGap()
{
    // The next graph starts at 216, below the line: it is not drawn
    QCOMPARE(TallCapture::graphAreaHeight({100, 100, 100}, 8, 210), 210);
}

void TallCaptureTests::testGraphAreaGraphsEndAboveLine()
{
    QCOMPARE(TallCapture::graphAreaHeight({100, 100, 100}, 8, 500), 500);
}

void TallCaptureTests::testGraphAreaNoGraphs()
{
    QCOMPARE(TallCapture::graphAreaHeight({}, 8, 400), 400);
}

void TallCaptureTests::testGraphAreaFractionalHeights()
{
    QCOMPARE(TallCapture::graphAreaHeight({100.5f}, 8, 50), 101);
}

void TallCaptureTests::testCappedPageHeight()
{
    QCOMPARE(TallCapture::cappedPageHeight(5000, 900, 1.0), 5000);
    QCOMPARE(TallCapture::cappedPageHeight(40000, 900, 1.0), 30000);
    QCOMPARE(TallCapture::cappedPageHeight(40000, 900, 2.0), 15000);
    QCOMPARE(TallCapture::cappedPageHeight(500, 900, 1.0), 900);   // never below on-screen
}

void TallCaptureTests::testInsertRows()
{
    QImage image(4, 4, QImage::Format_RGB32);
    const QRgb rows[4] = { qRgb(10, 0, 0), qRgb(20, 0, 0), qRgb(30, 0, 0), qRgb(40, 0, 0) };
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) image.setPixel(x, y, rows[y]);
    }
    const QImage out = TallCapture::insertRows(image, 2, 3);
    QCOMPARE(out.size(), QSize(4, 7));
    // Rows 0-1 as they were, row 1 repeated three times, then rows 2-3
    const QRgb expected[7] = { rows[0], rows[1], rows[1], rows[1], rows[1], rows[2], rows[3] };
    for (int y = 0; y < 7; ++y) QCOMPARE(out.pixel(3, y), expected[y]);
}

void TallCaptureTests::testInsertRowsNone()
{
    QImage image(4, 4, QImage::Format_RGB32);
    image.fill(Qt::red);
    QCOMPARE(TallCapture::insertRows(image, 2, 0), image);
}

void TallCaptureTests::testInsertRowsKeepsRatio()
{
    QImage image(4, 4, QImage::Format_RGB32);
    image.fill(Qt::red);
    image.setDevicePixelRatio(1.5);
    const QImage out = TallCapture::insertRows(image, 4, 2);    // at the bottom edge
    QCOMPARE(out.devicePixelRatio(), 1.5);
    QCOMPARE(out.size(), QSize(4, 6));
    QCOMPARE(out.pixel(0, 5), qRgb(255, 0, 0));
}
