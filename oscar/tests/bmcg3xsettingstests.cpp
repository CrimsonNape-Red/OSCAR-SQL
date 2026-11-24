/* BMC G3X Device Settings Unit Tests
 *
 * Tests the decoding of the BMC G3X "TS" (therapy settings) block: the AutoS pressure
 * support / minimum IPAP pair, which different firmware stores in different fields, the
 * heated tube level (#419), and the CPAP Manual P setting (#421).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "bmcg3xsettingstests.h"

#include <QByteArray>

#include "SleepLib/loader_plugins/bmcG3xDataParsing.h"

namespace {

// Offsets within the 256-byte TS block (see Notes/Formats/BMC/G3X/BMC_G3X_SET_FORMAT.md).
constexpr int kMode        = 0x08;
constexpr int kTreatP      = 0x0A;
constexpr int kManualP     = 0x0C;
constexpr int kMinEpap     = 0x0E;
constexpr int kMinIpap     = 0x14;
constexpr int kMaxIpap     = 0x16;
constexpr int kPs          = 0x2A;
constexpr int kHumidifier  = 0x87;
constexpr int kTubeLevel   = 0x88;
constexpr int kFlags       = 0x8A;
constexpr int kTubeType    = 0x8F;

//! \brief Stores a little-endian 16-bit value, as the device does
void putU16(QByteArray &block, int offset, int value)
{
    block[offset]     = static_cast<char>(value & 0xFF);
    block[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
}

//! \brief An AutoS settings block with the given pressure limits (hundredths of cmH2O)
//!        and every other field zero
QByteArray autoSBlock(int minEpap, int maxIpap)
{
    QByteArray ts(0x100, '\0');
    ts[0] = 'T';
    ts[1] = 'S';
    ts[kMode] = static_cast<char>(BmcMode::AutoS);
    putU16(ts, kMinEpap, minEpap);
    putU16(ts, kMaxIpap, maxIpap);
    return ts;
}

//! \brief A CPAP settings block with the given pressures (hundredths of cmH2O) and every
//!        other field zero
QByteArray cpapBlock(int treatP, int manualP)
{
    QByteArray ts(0x100, '\0');
    ts[0] = 'T';
    ts[1] = 'S';
    ts[kMode] = static_cast<char>(BmcMode::CPAP);
    putU16(ts, kTreatP, treatP);
    putU16(ts, kManualP, manualP);
    return ts;
}

} // namespace

bool BmcG3xSettingsTests::decode(const QByteArray &ts, BmcMachineSettings &settings)
{
    return BmcG3xData::DecodeTsBlock(ts, settings);
}

void BmcG3xSettingsTests::testAutoSStoredPressureSupport()
{
    // E5 B25A Plus: PS is stored, the minimum IPAP field is zero.
    QByteArray ts = autoSBlock(750, 1500);
    putU16(ts, kPs, 350);

    BmcMachineSettings settings;
    QVERIFY(decode(ts, settings));
    QCOMPARE(settings.AutoS_MinEPAP, 7.5f);
    QCOMPARE(settings.AutoS_PS, 3.5f);
    QCOMPARE(settings.AutoS_MinIPAP, 11.0f);
}

void BmcG3xSettingsTests::testAutoSStoredMinIpap()
{
    // G3 B20A: minimum IPAP is stored, the PS field is zero. The vendor program shows
    // Min EPAP 7.0, Min IPAP 9.0, Max IPAP 20.0 for this block.
    QByteArray ts = autoSBlock(700, 2000);
    putU16(ts, kMinIpap, 900);

    BmcMachineSettings settings;
    QVERIFY(decode(ts, settings));
    QCOMPARE(settings.AutoS_MinEPAP, 7.0f);
    QCOMPARE(settings.AutoS_MinIPAP, 9.0f);
    QCOMPARE(settings.AutoS_PS, 2.0f);
    QCOMPARE(settings.AutoS_MaxIPAP, 20.0f);
}

void BmcG3xSettingsTests::testAutoSNeitherStored()
{
    BmcMachineSettings settings;
    QVERIFY(decode(autoSBlock(700, 2000), settings));
    QCOMPARE(settings.AutoS_PS, 0.0f);
    QCOMPARE(settings.AutoS_MinIPAP, 7.0f);
}

void BmcG3xSettingsTests::testAutoSMinIpapOutOfRangeIgnored()
{
    // Below minimum EPAP, then above maximum IPAP.
    for (int minIpap : {650, 2050}) {
        QByteArray ts = autoSBlock(700, 2000);
        putU16(ts, kMinIpap, minIpap);

        BmcMachineSettings settings;
        QVERIFY(decode(ts, settings));
        QCOMPARE(settings.AutoS_PS, 0.0f);
        QCOMPARE(settings.AutoS_MinIPAP, 7.0f);
    }
}

void BmcG3xSettingsTests::testHeatedTubeLevel()
{
    // Levels 3 and 4 are the two the vendor program was checked against.
    for (int level : {0, 3, 4, 5}) {
        QByteArray ts = autoSBlock(700, 2000);
        ts[kTubeType]  = static_cast<char>(BmcAirTubeType::Heated19mm);
        ts[kTubeLevel] = static_cast<char>(level);

        BmcMachineSettings settings;
        QVERIFY(decode(ts, settings));
        QCOMPARE(settings.AirTubeType, BmcAirTubeType::Heated19mm);
        QCOMPARE(settings.HeatedTubeLevel, level);
    }
}

void BmcG3xSettingsTests::testHeatedTubeLevelOutOfRangeIgnored()
{
    QByteArray ts = autoSBlock(700, 2000);
    ts[kTubeType]  = static_cast<char>(BmcAirTubeType::Heated19mm);
    ts[kTubeLevel] = static_cast<char>(9);

    BmcMachineSettings settings;
    QVERIFY(decode(ts, settings));
    QCOMPARE(settings.HeatedTubeLevel, 0);
}

void BmcG3xSettingsTests::testHeatedTubeAuto()
{
    // G3 A20: flags 0x0C (humidifier Auto + heated tube Auto). The vendor program shows
    // "Auto" for both, whatever level is stored: 3 on one night checked, 5 on another.
    for (int storedLevel : {3, 5}) {
        QByteArray ts = autoSBlock(700, 2000);
        ts[kTubeType]   = static_cast<char>(BmcAirTubeType::Heated19mm);
        ts[kHumidifier] = static_cast<char>(3);
        ts[kTubeLevel]  = static_cast<char>(storedLevel);
        ts[kFlags]      = static_cast<char>(0x0C);

        BmcMachineSettings settings;
        QVERIFY(decode(ts, settings));
        QCOMPARE(settings.HumidifierLevel, quint8(6));   // option 6 is "Auto"
        QCOMPARE(settings.HeatedTubeLevel, 6);           // option 6 is "Auto"
    }
}

void BmcG3xSettingsTests::testHumidifierAutoLeavesHeatedTubeLevel()
{
    QByteArray ts = autoSBlock(700, 2000);
    ts[kTubeType]   = static_cast<char>(BmcAirTubeType::Heated19mm);
    ts[kHumidifier] = static_cast<char>(3);
    ts[kTubeLevel]  = static_cast<char>(4);
    ts[kFlags]      = static_cast<char>(0x04);

    BmcMachineSettings settings;
    QVERIFY(decode(ts, settings));
    QCOMPARE(settings.HumidifierLevel, quint8(6));
    QCOMPARE(settings.HeatedTubeLevel, 4);
}

void BmcG3xSettingsTests::testCpapManualPIsItsOwnField()
{
    // Checked by changing the field on a card: with Treat P 7.0 stored and this field set
    // to 9.00, the vendor program shows Treat P 7.0 and Manual P 9.0.
    BmcMachineSettings settings;
    QVERIFY(decode(cpapBlock(700, 900), settings));
    QCOMPARE(settings.CPAP_TreatP, 7.0f);
    QCOMPARE(settings.CPAP_ManualP, 9.0f);
}

void BmcG3xSettingsTests::testCpapManualPZeroIsNotTreatP()
{
    // What every card seen so far holds: the vendor program shows Treat P 11.0 and
    // Manual P 0.0 for such a night. Manual P used to be given the Treat P value.
    BmcMachineSettings settings;
    QVERIFY(decode(cpapBlock(1100, 0), settings));
    QCOMPARE(settings.CPAP_TreatP, 11.0f);
    QCOMPARE(settings.CPAP_ManualP, 0.0f);
}

void BmcG3xSettingsTests::testCpapManualPOutOfRangeIgnored()
{
    BmcMachineSettings settings;
    QVERIFY(decode(cpapBlock(700, 5000), settings));
    QCOMPARE(settings.CPAP_ManualP, 0.0f);
}
