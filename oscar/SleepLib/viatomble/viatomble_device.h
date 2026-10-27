/* Viatom BLE device connection
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef VIATOMBLE_DEVICE_H
#define VIATOMBLE_DEVICE_H

#include <QBluetoothDeviceInfo>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QQueue>
#include <QTimer>

#include "viatomble_link.h"
#include "viatomble_scanner.h"

class QDBusMessage;
class QSocketNotifier;

namespace ViatomBle {

/*! \brief A Bluetooth LE connection to a classic Viatom device (ported from oxytool's OxyDevice).
 *
 *  On Linux, Qt's BlueZ backend silently drops writes to a characteristic that only
 *  allows write-without-response, which is what these devices offer. Writes therefore
 *  go straight to BlueZ over D-Bus (WriteValue), and notifications come through a file
 *  descriptor from AcquireNotify. Elsewhere, and on Linux if D-Bus fails, Qt's own
 *  calls are used. Nothing here blocks the event loop.
 */
class Device : public Link
{
    Q_OBJECT

  public:
    explicit Device(QObject *parent = nullptr);
    ~Device() override;

    //! Connects and sets up the family's service; answers with ready() or connectFailed().
    void connectToDevice(const QBluetoothDeviceInfo &info, DeviceFamily family = DeviceFamily::Classic);
    void disconnectFromDevice();
    bool isReady() const { return m_ready; }

    void send(const QByteArray &frame) override;

  signals:
    void ready();
    void connectFailed(const QString &reason);

  private slots:
    void onConnected();
    void onDisconnected();
    void onControllerError(QLowEnergyController::Error error);
    void onDiscoveryFinished();
    void onServiceStateChanged(QLowEnergyService::ServiceState state);
    void onCharacteristicChanged(const QLowEnergyCharacteristic &ch, const QByteArray &value);
    void onConnectTimeout();

  private:
    void fail(const QString &reason);
    void setReady();
    void enableQtNotifications();
    int writeChunkSize() const;
    void sendQt(const QByteArray &frame);

#ifdef OSCAR_BLE_BLUEZ_DBUS
    void startBluezSetup();
    //! BlueZ object path of the characteristic with this UUID on our device, or empty;
    //! reply is the answer to GetManagedObjects.
    QString bluezCharacteristicPath(const QDBusMessage &reply, const QString &uuid) const;
    void acquireNotify();
    void onNotifyFdReadable();
    void writeNextDbusChunk();
    void closeNotifyFd();

    QString m_writePath;
    QString m_notifyPath;
    int m_notifyFd = -1;
    QSocketNotifier *m_notifyWatcher = nullptr;
    QQueue<QByteArray> m_dbusWrites;
    bool m_dbusWriteBusy = false;
    QString m_dbusWriteType = QStringLiteral("command");   // "request" if "command" fails
#endif

    //! A GATT service with its write and notify characteristics.
    struct ServiceSpec
    {
        QString service;
        QString write;
        QString notify;
    };

    QBluetoothDeviceInfo m_info;
    QList<ServiceSpec> m_candidates;   //!< in order of preference
    ServiceSpec m_spec;                //!< the one in use
    QLowEnergyController *m_controller = nullptr;
    QLowEnergyService *m_service = nullptr;
    QLowEnergyCharacteristic m_writeChar;
    QLowEnergyCharacteristic m_notifyChar;
    QTimer m_connectTimer;
    int m_mtu = 23;
    bool m_ready = false;
    //! Bumped on every disconnect; asynchronous answers for an older connection are ignored.
    int m_connection = 0;
};

} // namespace ViatomBle

#endif // VIATOMBLE_DEVICE_H
