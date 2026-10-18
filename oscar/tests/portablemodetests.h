/* Portable Mode Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

class PortableModeTests : public QObject
{
    Q_OBJECT
private slots:
    void testInsideRootIsRelative();
    void testRootItself();
    void testOutsideRoot();
    void testDecode();
    void testRoundTrip();
    void testEmptyAndRelativeInputs();
    void testDataFolderSetting();
    void testResolveDataRoot();
    void testZipNamedParent();
};
DECLARE_TEST(PortableModeTests)
