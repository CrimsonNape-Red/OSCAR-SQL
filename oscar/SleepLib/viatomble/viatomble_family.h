/* Viatom BLE: oximeter families
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef VIATOMBLE_FAMILY_H
#define VIATOMBLE_FAMILY_H

// Apart from the scanner so that builds without Qt Bluetooth (MinGW on Windows) can
// still use the USB cable through the import panel.

namespace ViatomBle {

enum class DeviceFamily {
    NotViatom,   // not an oximeter OSCAR knows
    Classic,     // SleepU, Checkme O2, O2Ring, ...: supported
    O2RingS,     // O2Ring S and relatives: their own protocol (see O2RingSDialect)
    Emay,        // EMAY EMO-80: EMAY's own protocol (see EmayDownloader)
    Unsupported, // recognised but not supported yet: Contec's Bluetooth models ("SpO2xx")
                 // and EMAY's models that use Contec's protocol (SleepO2, EMO-90, EMO-100, ...)
};

//! True for the families OSCAR can download from.
inline bool isSupported(DeviceFamily family)
{
    return family == DeviceFamily::Classic || family == DeviceFamily::O2RingS || family == DeviceFamily::Emay;
}

} // namespace ViatomBle

#endif // VIATOMBLE_FAMILY_H
