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
    NotViatom,
    Classic,     // SleepU, Checkme O2, O2Ring, ...: supported
    O2RingS,     // O2Ring S and relatives: their own protocol (see O2RingSDialect)
};

//! Classifies a device from its advertised services and name.
DeviceFamily deviceFamily(const QBluetoothDeviceInfo &info);
//! The same, by Bluetooth name only (exposed for the unit tests).
DeviceFamily deviceFamilyFromName(const QString &name);

//! Searches for Viatom devices over Bluetooth LE.
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
