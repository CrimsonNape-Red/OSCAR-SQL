/* BMC G2S Pressure Wave Unit Tests
 *
 * Tests the conversion of the legacy BMC G2S pressure wave, which the device stores in
 * raw sensor units instead of the tenths of cmH2O used by the G3 and E5 (#393).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef BMCPRESSUREWAVETESTS_H
#define BMCPRESSUREWAVETESTS_H

#include "AutoTest.h"

class BmcPressureWaveTests : public QObject
{
    Q_OBJECT

private slots:
    //! \brief Only G2S models are given the G2S pressure-wave conversion
    void testOnlyG2sModelsAreConverted();
    //! \brief Converted samples reproduce the set pressure they were calibrated against
    void testConversionMatchesSetPressure();
    //! \brief Out-of-range raw samples stay inside the range of a stored sample
    void testConversionIsClamped();
};

DECLARE_TEST(BmcPressureWaveTests)

#endif // BMCPRESSUREWAVETESTS_H
