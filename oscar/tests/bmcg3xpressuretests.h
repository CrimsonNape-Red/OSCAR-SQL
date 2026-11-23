/* BMC G3X Pressure Unit Tests
 *
 * Tests how the BMC G3X parser turns what the card holds into the pressures OSCAR stores:
 * the pressure wave behind the Mask Pressure graph, the pressures stored for a waveform
 * packet, and the pressure records of a night that has events only (#419).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef BMCG3XPRESSURETESTS_H
#define BMCG3XPRESSURETESTS_H

#include "AutoTest.h"

class BmcG3xPressureTests : public QObject
{
    Q_OBJECT

private slots:
    //! \brief A pressure-wave sample within 0 to 40 cmH2O is stored unchanged
    void testPressureWaveInRangeIsKept();
    //! \brief A slightly negative sample (machine starting, mask off) is stored as zero
    void testPressureWaveNegativeIsZero();
    //! \brief A sample above 40 cmH2O is limited to 40
    void testPressureWaveAboveRangeIsLimited();
    //! \brief A packet's own pressure trend is what is stored for it
    void testPacketPressureUsesTrend();
    //! \brief A packet without a trend stores the last known pressure, in the same unit
    void testPacketPressureWithoutTrendIsInHundredths();
    //! \brief A packet without a trend and with no known pressure stores no sample
    void testPacketPressureWithNothingKnownIsEmpty();
    //! \brief The last pressure of an events-only session is repeated just before its end
    void testLastPressureHeldToSessionEnd();
    //! \brief Nothing is added when the last pressure record is already at the session's end
    void testNoHoldWhenLastPressureIsAtEnd();
    //! \brief A session with no pressure records gets none
    void testNoHoldWithoutPressureRecords();
};

DECLARE_TEST(BmcG3xPressureTests)

#endif // BMCG3XPRESSURETESTS_H
