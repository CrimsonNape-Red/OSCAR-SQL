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
#endif

#include "viatomble_availability.h"

namespace ViatomBle {

Availability availability()
{
#ifdef OSCAR_HAVE_BLUETOOTH
    const QList<QBluetoothHostInfo> adapters = QBluetoothLocalDevice::allDevices();
    if (adapters.isEmpty()) {
        return Availability::NoAdapter;
    }
    for (const QBluetoothHostInfo &host : adapters) {
        QBluetoothLocalDevice local(host.address());
        if (local.isValid() && local.hostMode() != QBluetoothLocalDevice::HostPoweredOff) {
            return Availability::Available;
        }
    }
    return Availability::PoweredOff;
#else
    return Availability::NotBuilt;
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
