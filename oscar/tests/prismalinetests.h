/* Löwenstein Prisma LINE Loader Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef PRISMALINETESTS_H
#define PRISMALINETESTS_H

#include "tests/AutoTest.h"

class PrismaLineTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    // Settings mapping, on synthetic session files
    void testModelTable();
    void testAcSVFixedRate();
    void testAcSVAutoF();
    void testTubeTypeInOtherModes();

    // Event mapping, on synthetic session files
    void testEventChannels();

    // Whole-card import; skipped unless OSCAR_PRISMACR_CARD names a prismaCR card folder
    void testPrismaCRCard();
};
DECLARE_TEST(PrismaLineTests)

#endif // PRISMALINETESTS_H
