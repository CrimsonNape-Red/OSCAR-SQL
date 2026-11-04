/* Viatom BLE availability check
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef VIATOMBLE_AVAILABILITY_H
#define VIATOMBLE_AVAILABILITY_H

#include <QString>

namespace ViatomBle {

enum class Availability {
    Available,
    NotBuilt,      // this build of OSCAR has no Qt Bluetooth
    NoAdapter,
    PoweredOff,
};

//! Whether Bluetooth LE can be used right now. Cheap enough to call when a menu opens.
Availability availability();

//! Why the BLE import is unavailable, for a tooltip; empty when it is available.
QString availabilityReason(Availability a);

} // namespace ViatomBle

#endif // VIATOMBLE_AVAILABILITY_H
