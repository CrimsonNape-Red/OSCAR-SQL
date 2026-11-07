/* SEFAM S.Box Archive Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SEFAMTESTS_H
#define SEFAMTESTS_H

#include "tests/AutoTest.h"

class SefamTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    // Reading the archive
    void testArchiveBlockStartAndLeakDecoded();
    void testArchiveBlockBadStartIsInvalid();

    // Choosing which archive sessions to import
    void testArchiveOnlySelection();
    void testArchiveOnlyNeedsCardRange();

    // Building the session
    void testArchiveSessionContents();
    void testArchiveSessionStoppedMinutes();
};
DECLARE_TEST(SefamTests)

#endif // SEFAMTESTS_H
