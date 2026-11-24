/* BMC G3X Leak Unit Tests
 *
 * Tests how the leak rate is read from a BMC G3X waveform packet: which field it comes
 * from and how it is scaled to L/min (#403).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef BMCG3XLEAKTESTS_H
#define BMCG3XLEAKTESTS_H

#include "AutoTest.h"

class BmcG3xLeakTests : public QObject
{
    Q_OBJECT

private slots:
    //! \brief Leak comes from the leak field, whatever the bytes around 0x568 hold
    void testLeakComesFromTheLeakField();
    //! \brief One raw unit is 0.1 L/min
    void testOneRawUnitIsATenthOfALitre();
    //! \brief A packet reporting no leak gives zero, whatever the neighbouring bytes hold
    void testZeroLeak();
    //! \brief An absurd raw value is limited to 500 L/min, and a short packet gives zero
    void testLeakLimits();
};

DECLARE_TEST(BmcG3xLeakTests)

#endif // BMCG3XLEAKTESTS_H
