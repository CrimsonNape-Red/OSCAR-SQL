/* Session gap Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

/*! \class SessionGapTests
    \brief Which stretches of a night lie between its sessions (hatched on Modern Daily) */
class SessionGapTests : public QObject
{
    Q_OBJECT
private slots:
    void testNoGapForOneSession();
    void testGapBetweenTwo();
    void testOverlapMerged();
    void testUnsorted();
    void testEmpty();
};
DECLARE_TEST(SessionGapTests)
