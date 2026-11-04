/* Image trimming Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

class ImageTrimTests : public QObject
{
    Q_OBJECT
private slots:
    void testTransparentMargins();
    void testWhiteMargins();
    void testBlankImage();
    void testPaddedRect();
    void testPaddedRectClipsToImage();
};
DECLARE_TEST(ImageTrimTests)
