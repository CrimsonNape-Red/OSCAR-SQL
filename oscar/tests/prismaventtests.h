/* Löwenstein prisma VENT Loader Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef PRISMAVENTTESTS_H
#define PRISMAVENTTESTS_H

#include "tests/AutoTest.h"

class PrismaVentTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    // Parsing helpers, on synthetic input
    void testParseLogTime();
    void testParseRespEvents();
    void testParameterLogFollowsActiveProgram();
    void testAutoSTSettings();

    // Whole-card import; skipped unless OSCAR_PRISMAVENT_CARD names a card folder
    void testSampleCard();

    // Every session of every card listed in OSCAR_PRISMAVENT_CARDS (';'-separated)
    void testEveryCardSession();
};
DECLARE_TEST(PrismaVentTests)

#endif // PRISMAVENTTESTS_H
