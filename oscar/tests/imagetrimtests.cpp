/* Image trimming Unit Tests
 *
 * Checks that the visible part of a device picture is found whether its empty margins are
 * transparent or white, and that the trim margin stays inside the image.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "imagetrimtests.h"
#include "uikit/imagetrim.h"

#include <QImage>
#include <QPainter>

namespace {

//! \brief A \a background image of 100x100 with a dark-red block at x 10-59, y 20-49
QImage makeImage(const QColor &background)
{
    QImage img(100, 100, QImage::Format_ARGB32);
    img.fill(background);
    QPainter p(&img);
    p.fillRect(QRect(10, 20, 50, 30), QColor(120, 0, 0));
    return img;
}

} // namespace

void ImageTrimTests::testTransparentMargins()
{
    QCOMPARE(UiKit::visibleRect(makeImage(Qt::transparent)), QRect(10, 20, 50, 30));
}

void ImageTrimTests::testWhiteMargins()
{
    // Off-white noise, as left by JPEG compression, counts as background too
    QCOMPARE(UiKit::visibleRect(makeImage(QColor(250, 252, 249))), QRect(10, 20, 50, 30));
}

void ImageTrimTests::testBlankImage()
{
    QVERIFY(UiKit::visibleRect(makeImage(Qt::transparent).copy(70, 70, 20, 20)).isEmpty());
}

void ImageTrimTests::testPaddedRect()
{
    // 4% of the longer side (50) is 2 px on every side
    QCOMPARE(UiKit::paddedRect(QRect(10, 20, 50, 30), QSize(100, 100), 0.04),
             QRect(8, 18, 54, 34));
}

void ImageTrimTests::testPaddedRectClipsToImage()
{
    QCOMPARE(UiKit::paddedRect(QRect(0, 1, 100, 98), QSize(100, 100), 0.04),
             QRect(0, 0, 100, 100));
}
