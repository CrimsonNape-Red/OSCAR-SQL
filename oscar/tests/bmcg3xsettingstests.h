/* BMC G3X Device Settings Unit Tests
 *
 * Tests the decoding of the BMC G3X "TS" (therapy settings) block: the AutoS pressure
 * support / minimum IPAP pair, which different firmware stores in different fields, and
 * the heated tube level (#419).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef BMCG3XSETTINGSTESTS_H
#define BMCG3XSETTINGSTESTS_H

#include "AutoTest.h"

class QByteArray;
class BmcMachineSettings;

class BmcG3xSettingsTests : public QObject
{
    Q_OBJECT

private:
    //! \brief Calls the private decoder; a member, because friendship covers members only
    static bool decode(const QByteArray &ts, BmcMachineSettings &settings);

private slots:
    //! \brief Firmware that stores pressure support: minimum IPAP is minimum EPAP + PS
    void testAutoSStoredPressureSupport();
    //! \brief Firmware that stores minimum IPAP: PS is minimum IPAP - minimum EPAP
    void testAutoSStoredMinIpap();
    //! \brief Neither field stored: PS stays zero, minimum IPAP equals minimum EPAP
    void testAutoSNeitherStored();
    //! \brief A minimum IPAP outside minimum EPAP .. maximum IPAP is not a reading
    void testAutoSMinIpapOutOfRangeIgnored();
    //! \brief The heated tube level is read from the block
    void testHeatedTubeLevel();
    //! \brief A heated tube level above the highest setting is not a reading
    void testHeatedTubeLevelOutOfRangeIgnored();
    //! \brief The heated tube's Auto flag takes the place of the stored level
    void testHeatedTubeAuto();
    //! \brief The humidifier's Auto flag does not make the heated tube Auto
    void testHumidifierAutoLeavesHeatedTubeLevel();
};

DECLARE_TEST(BmcG3xSettingsTests)

#endif // BMCG3XSETTINGSTESTS_H
