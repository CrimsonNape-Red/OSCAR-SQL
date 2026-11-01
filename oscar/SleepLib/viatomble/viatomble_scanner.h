/* Viatom BLE device scanner
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef VIATOMBLE_SCANNER_H
#define VIATOMBLE_SCANNER_H

#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QHash>
#include <QObject>

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

/*! Classifies a device from its advertised services and name. Viatom and EMAY both sell
 *  a "SleepO2": a service the device advertises decides, and otherwise preferEmay (the
 *  user chose EMAY in the wizard). Without either it is taken for a Viatom. */
DeviceFamily deviceFamily(const QBluetoothDeviceInfo &info, bool preferEmay = false);
//! The same from the name and advertised service UUIDs (exposed for the unit tests).
DeviceFamily deviceFamily(const QString &name, const QStringList &serviceUuids, bool preferEmay);
//! By Bluetooth name only; a "SleepO2" counts as Viatom (exposed for the unit tests).
DeviceFamily deviceFamilyFromName(const QString &name);
//! The brand to show for a device: "Viatom/Wellue", "EMAY" or "Contec".
QString brandName(const QString &name, DeviceFamily family);

//! Searches for oximeters over Bluetooth LE: Viatom/Wellue, EMAY, Contec.
class Scanner : public QObject
{
    Q_OBJECT

  public:
    explicit Scanner(QObject *parent = nullptr);

    void start(int timeoutMs = 10000);
    void stop();
    bool isActive() const { return m_agent.isActive(); }

  signals:
    //! Emitted once per device, and again if its signal strength changes.
    void deviceFound(const QBluetoothDeviceInfo &info, ViatomBle::DeviceFamily family);
    void finished();
    void error(const QString &reason);

  private:
    void consider(const QBluetoothDeviceInfo &info);

    QBluetoothDeviceDiscoveryAgent m_agent;
    //! Name last logged for each device seen in this scan, so the log shows every
    //! device once, and again if its name arrives later.
    QHash<QString, QString> m_seen;
    int m_viatomSeen = 0;
};

} // namespace ViatomBle

Q_DECLARE_METATYPE(ViatomBle::DeviceFamily)

#endif // VIATOMBLE_SCANNER_H
