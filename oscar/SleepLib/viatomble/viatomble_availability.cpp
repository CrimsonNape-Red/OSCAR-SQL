/* Viatom BLE availability check
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QCoreApplication>

#ifdef OSCAR_HAVE_BLUETOOTH
#include <QBluetoothLocalDevice>
#include <QPointer>
#endif

#include "viatomble_availability.h"

namespace ViatomBle {

#if defined(OSCAR_HAVE_BLUETOOTH) && !defined(Q_OS_MACOS)
// The adapter is looked up once and then followed through its signals, so opening the
// Data menu doesn't query Bluetooth each time. A missing adapter is looked for again on
// each call, which is cheap when there is none.
static QPointer<QBluetoothLocalDevice> s_adapter;
static QBluetoothLocalDevice::HostMode s_hostMode = QBluetoothLocalDevice::HostPoweredOff;

static void findAdapter()
{
    const QList<QBluetoothHostInfo> adapters = QBluetoothLocalDevice::allDevices();
    if (adapters.isEmpty()) {
        return;
    }
    auto *adapter = new QBluetoothLocalDevice(adapters.first().address(), QCoreApplication::instance());
    if (!adapter->isValid()) {
        delete adapter;
        return;
    }
    s_hostMode = adapter->hostMode();
    QObject::connect(adapter, &QBluetoothLocalDevice::hostModeStateChanged, adapter,
                     [](QBluetoothLocalDevice::HostMode mode) { s_hostMode = mode; });
    s_adapter = adapter;
}
#endif

Availability availability()
{
#ifndef OSCAR_HAVE_BLUETOOTH
    return Availability::NotBuilt;
#elif defined(Q_OS_MACOS)
    // Any adapter query on macOS asks the user for Bluetooth permission, which shouldn't
    // happen just because a menu was opened. Macs have Bluetooth; the scan asks for
    // permission and reports it if Bluetooth is off.
    return Availability::Available;
#else
    if (!s_adapter) {
        findAdapter();
        if (!s_adapter) {
            return Availability::NoAdapter;
        }
    }
    return s_hostMode == QBluetoothLocalDevice::HostPoweredOff ? Availability::PoweredOff
                                                                : Availability::Available;
#endif
}

QString availabilityReason(Availability a)
{
    switch (a) {
    case Availability::Available:
        return QString();
    case Availability::NotBuilt:
        return QCoreApplication::translate("ViatomBle", "This build of OSCAR has no Bluetooth support");
    case Availability::NoAdapter:
        return QCoreApplication::translate("ViatomBle", "No Bluetooth adapter found");
    case Availability::PoweredOff:
        return QCoreApplication::translate("ViatomBle", "Bluetooth is turned off");
    }
    return QString();
}

} // namespace ViatomBle
