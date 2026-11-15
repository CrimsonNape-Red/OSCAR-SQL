/* Right sidebar style Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

class SidebarStyleTests : public QObject
{
    Q_OBJECT
private slots:
    void testFollowsLightAndDarkPalettes();
    void testTextIsReadableOnItsBackground();
    void testModernAndClassicDiffer();
    void testStyleSheetHasNoFixedColours();
    void testDocumentWrapsBodyWithThemeColours();
};
DECLARE_TEST(SidebarStyleTests)
