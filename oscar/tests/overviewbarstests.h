/* Overview bar Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

/*! \class OverviewBarsTests
    \brief Bar placement within a day's slot, the bar shading and strip text colour */
class OverviewBarsTests : public QObject
{
    Q_OBJECT
private slots:
    void testGeometryWideSlot();
    void testGeometrySmallSlotKeepsOnePixelGap();
    void testGeometryNarrowSlot();
    void testBarGradientStops();
    void testDarkBarColourIsMuted();
    void testDarkGradientDoesNotGlare();
    void testDarkGreyBarsStayReadable();
    void testDarkOrangeStaysAmber();
    void testOutlineColour();
    void testLightBarHasThinBlackOutline();
    void testClosedOutlineRunsAlongBottom();
    void testPlainFillLighterAboveThreePixels();
    void testTopIsFlatWithRoundedCorners();
    void testContrastingText();
    void testClassicGeometryHasNoGap();
    void testClassicSegmentSquareTop();
};
DECLARE_TEST(OverviewBarsTests)
