/* BMC USR Session Chain Unit Tests
 *
 * Tests that the legacy BMC loader reads every historic session in a .USR file, including
 * those after a headerless record that breaks the 'next session' pointer chain (#343).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef BMCUSRSESSIONSTESTS_H
#define BMCUSRSESSIONSTESTS_H

#include "AutoTest.h"

class BmcUsrSessionsTests : public QObject
{
    Q_OBJECT

private slots:
    //! \brief Sessions after headerless records are still read, in file order
    void testSessionsAfterHeaderlessRecordsAreRead();
    //! \brief A last record whose 'next' pointer is unset still runs to the end of the file
    void testLastRecordWithUnsetNextPointer();
};

DECLARE_TEST(BmcUsrSessionsTests)

#endif // BMCUSRSESSIONSTESTS_H
