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

#include "bmcg3xleaktests.h"

#include <QByteArray>

#include "SleepLib/loader_plugins/bmcG3xDataParsing.h"

namespace {

constexpr int kPacketSize = 2048;
constexpr int kLeak       = 0x52A;  //!< the leak field, uint16
constexpr int kArrayA     = 0x564;  //!< uint8[5], ends at 0x568
constexpr int kArrayB     = 0x569;  //!< uint8[5], an event counter

//! \brief A waveform packet with the given raw leak. The two 5-byte arrays that meet at
//!        0x568 are filled as on a real card during an event: the first with \a arrayA,
//!        the second with \a arrayB.
QByteArray packet(int rawLeak, int arrayA = 0x55, int arrayB = 0x01)
{
    QByteArray p(kPacketSize, '\0');
    p[0] = static_cast<char>(0xAD);
    p[1] = static_cast<char>(0xAA);
    p[kLeak]     = static_cast<char>(rawLeak & 0xFF);
    p[kLeak + 1] = static_cast<char>((rawLeak >> 8) & 0xFF);
    for (int i = 0; i < 5; ++i) {
        p[kArrayA + i] = static_cast<char>(arrayA);
        p[kArrayB + i] = static_cast<char>(arrayB);
    }
    return p;
}

} // namespace

void BmcG3xLeakTests::testLeakComesFromTheLeakField()
{
    // A G3 B20A night (firmware G3-2.12): raw 67 where the device's own statistics give a
    // median of 6.0 and a mean of 8.7 L/min. For that firmware the loader used to read a
    // uint16 at 0x568, which with these neighbouring bytes gave 0x0155 x 0.16 = 54.6 L/min:
    // the false large leak of the report.
    QCOMPARE(BmcG3xPacketLeakTenths(packet(67)), 67);
}

void BmcG3xLeakTests::testOneRawUnitIsATenthOfALitre()
{
    // A night whose median raw value is 501 and for which the device reports a median of
    // 50.0 L/min. At the 0.16 formerly used for the G3 it read 80.2.
    QCOMPARE(BmcG3xPacketLeakTenths(packet(501)), 501);
}

void BmcG3xLeakTests::testZeroLeak()
{
    // No leak, during an event: the first array saturated, the counter running.
    QCOMPARE(BmcG3xPacketLeakTenths(packet(0, 0xFF, 0x0A)), 0);
}

void BmcG3xLeakTests::testLeakLimits()
{
    QCOMPARE(BmcG3xPacketLeakTenths(packet(60000)), 5000);
    QCOMPARE(BmcG3xPacketLeakTenths(QByteArray(100, '\0')), 0);
}
