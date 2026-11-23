/* BMC G3X Waveform Packet Unit Tests
 *
 * Tests how values read from a BMC G3X waveform packet are turned into the samples OSCAR
 * stores: the pressure wave behind the Mask Pressure graph, and the pressures stored for
 * a packet (#419).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef BMCG3XWAVEFORMTESTS_H
#define BMCG3XWAVEFORMTESTS_H

#include "AutoTest.h"

class BmcG3xWaveformTests : public QObject
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
};

DECLARE_TEST(BmcG3xWaveformTests)

#endif // BMCG3XWAVEFORMTESTS_H
