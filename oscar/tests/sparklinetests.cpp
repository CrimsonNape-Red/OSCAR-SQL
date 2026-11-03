/* Sparkline geometry Unit Tests
 *
 * Checks how a sparkline maps nightly values to points: even horizontal spacing, scaling
 * that includes the reference (average) line, flat series, and nights without data.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "sparklinetests.h"
#include "uikit/sparkline.h"

#include <cmath>
#include <limits>

using UiKit::Sparkline;

static const QSizeF kSize(200, 38);
static const double kPad = 4;

void SparklineTests::testEvenSpacing()
{
    QVector<QPointF> p = Sparkline::mapPoints({1, 2, 3}, 2, kSize, kPad);
    QCOMPARE(p.size(), 3);
    QCOMPARE(p[0].x(), 0.0);
    QCOMPARE(p[1].x(), 100.0);
    QCOMPARE(p[2].x(), 200.0);
}

void SparklineTests::testScale()
{
    QVector<QPointF> p = Sparkline::mapPoints({1, 3}, 2, kSize, kPad);
    QCOMPARE(p[0].y(), 34.0);
    QCOMPARE(p[1].y(), 4.0);
}

void SparklineTests::testFlatSeries()
{
    QVector<QPointF> p = Sparkline::mapPoints({5, 5}, 5, kSize, kPad);
    QCOMPARE(p[0].y(), 19.0);
    QCOMPARE(p[1].y(), 19.0);
}

void SparklineTests::testGaps()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    QVector<QPointF> p = Sparkline::mapPoints({1, nan, 3}, 2, kSize, kPad);
    QCOMPARE(p.size(), 3);
    QVERIFY(std::isnan(p[1].y()));
    QCOMPARE(p[0].y(), 34.0);
    QCOMPARE(p[2].y(), 4.0);
}
