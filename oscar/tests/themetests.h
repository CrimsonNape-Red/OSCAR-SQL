/* Theme module Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

class ThemeTests : public QObject
{
    Q_OBJECT
private slots:
    void testFollowsPalette();
    void testWizardStatusColours();
    void testEveryRoleDefined();
    void testTintsSitBetweenSurfaceAndColour();
    void testCss();
    void testFontScalesApplicationFont();
    void testRadiusMatchesWizard();
};
DECLARE_TEST(ThemeTests)
