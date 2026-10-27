/* Viatom BLE device connection
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QBluetoothUuid>
#include <QDebug>
#include <QLowEnergyDescriptor>

#ifdef OSCAR_BLE_BLUEZ_DBUS
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QDBusVariant>
#include <QSocketNotifier>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#endif

#include "viatomble_device.h"
#include "viatomble_o2rings.h"
#include "viatomble_protocol.h"

namespace ViatomBle {

static const int CONNECT_TIMEOUT_MS = 20000;

Device::Device(QObject *parent)
    : Link(parent)
{
    m_connectTimer.setSingleShot(true);
    connect(&m_connectTimer, &QTimer::timeout, this, &Device::onConnectTimeout);
}

Device::~Device()
{
    disconnectFromDevice();
}

void Device::connectToDevice(const QBluetoothDeviceInfo &info, DeviceFamily family)
{
    disconnectFromDevice();
    m_info = info;

    const ServiceSpec classic{QString::fromLatin1(SERVICE_UUID), QString::fromLatin1(WRITE_UUID),
                              QString::fromLatin1(NOTIFY_UUID)};
    const ServiceSpec o2ringS{QString::fromLatin1(O2RingS::SERVICE_UUID), QString::fromLatin1(O2RingS::WRITE_UUID),
                              QString::fromLatin1(O2RingS::NOTIFY_UUID)};
    // The SDK can also put an O2Ring S on the classic service (its UUIDProtocol V1);
    // the frames are the O2Ring S ones either way.
    m_candidates = (family == DeviceFamily::O2RingS) ? QList<ServiceSpec>{o2ringS, classic}
                                                     : QList<ServiceSpec>{classic};

    m_controller = QLowEnergyController::createCentral(info, this);
    connect(m_controller, &QLowEnergyController::connected, this, &Device::onConnected);
    connect(m_controller, &QLowEnergyController::disconnected, this, &Device::onDisconnected);
    connect(m_controller, &QLowEnergyController::errorOccurred, this, &Device::onControllerError);
    connect(m_controller, &QLowEnergyController::discoveryFinished, this, &Device::onDiscoveryFinished);
    connect(m_controller, &QLowEnergyController::mtuChanged, this, [this](int mtu) { m_mtu = mtu; });

    m_connectTimer.start(CONNECT_TIMEOUT_MS);
    m_controller->connectToDevice();
}

void Device::disconnectFromDevice()
{
    m_connectTimer.stop();
    m_ready = false;
    m_connection++;   // answers still on their way for this connection are ignored
#ifdef OSCAR_BLE_BLUEZ_DBUS
    closeNotifyFd();
    m_dbusWrites.clear();
    m_dbusWriteBusy = false;
    m_writePath.clear();
    m_notifyPath.clear();
#endif
    if (m_service) {
        m_service->disconnect(this);
        m_service->deleteLater();
        m_service = nullptr;
    }
    if (m_controller) {
        m_controller->disconnect(this);
        if (m_controller->state() != QLowEnergyController::UnconnectedState) {
            m_controller->disconnectFromDevice();
        }
        m_controller->deleteLater();
        m_controller = nullptr;
    }
}

void Device::fail(const QString &reason)
{
    const bool wasReady = m_ready;
    disconnectFromDevice();
    if (wasReady) {
        emit linkLost(reason);
    } else {
        emit connectFailed(reason);
    }
}

void Device::onConnected()
{
    const int mtu = m_controller->mtu();
    if (mtu > 0) {
        m_mtu = mtu;
    }
    m_controller->discoverServices();
}

void Device::onDisconnected()
{
    fail(m_ready ? tr("The device disconnected.") : tr("The device disconnected while connecting."));
}

void Device::onControllerError(QLowEnergyController::Error error)
{
    Q_UNUSED(error);
    fail(tr("Bluetooth error: %1").arg(m_controller ? m_controller->errorString() : QString()));
}

void Device::onConnectTimeout()
{
    fail(tr("The device did not respond. Make sure it is awake and not connected to the phone app."));
}

void Device::onDiscoveryFinished()
{
    QBluetoothUuid serviceUuid;
    for (const ServiceSpec &spec : m_candidates) {
        if (m_controller->services().contains(QBluetoothUuid(spec.service))) {
            m_spec = spec;
            serviceUuid = QBluetoothUuid(spec.service);
            break;
        }
    }
    if (serviceUuid.isNull()) {
        QStringList offered;
        for (const QBluetoothUuid &uuid : m_controller->services()) {
            offered << uuid.toString();
        }
        qWarning() << "ViatomBle::Device: services offered:" << offered;
        fail(tr("This device does not offer the Viatom data service OSCAR expected. "
                "It may be a model OSCAR cannot download from yet."));
        return;
    }
    qDebug() << "ViatomBle::Device: using service" << m_spec.service;
    m_service = m_controller->createServiceObject(serviceUuid, this);
    if (!m_service) {
        fail(tr("The Viatom data service could not be opened."));
        return;
    }
    connect(m_service, &QLowEnergyService::stateChanged, this, &Device::onServiceStateChanged);
    connect(m_service, &QLowEnergyService::characteristicChanged, this, &Device::onCharacteristicChanged);
    connect(m_service, &QLowEnergyService::errorOccurred, this, [this](QLowEnergyService::ServiceError error) {
        if (!m_ready) {
            // e.g. notifications could not be enabled: say so now rather than at the timeout
            fail(tr("The Viatom data service reported an error (%1).").arg(int(error)));
        } else {
            // A lost write shows up as a missing reply, which the downloader retries.
            qWarning() << "ViatomBle::Device: service error" << int(error);
        }
    });
    m_service->discoverDetails();
}

void Device::onServiceStateChanged(QLowEnergyService::ServiceState state)
{
    if (state != QLowEnergyService::RemoteServiceDiscovered) {
        return;
    }
    m_writeChar = m_service->characteristic(QBluetoothUuid(m_spec.write));
    m_notifyChar = m_service->characteristic(QBluetoothUuid(m_spec.notify));
    if (!m_writeChar.isValid() || !m_notifyChar.isValid()) {
        fail(tr("The Viatom data service is missing its characteristics."));
        return;
    }

#ifdef OSCAR_BLE_BLUEZ_DBUS
    startBluezSetup();
#else
    enableQtNotifications();
#endif
}

void Device::enableQtNotifications()
{
    const QLowEnergyDescriptor cccd =
        m_notifyChar.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
    if (!cccd.isValid()) {
        fail(tr("Notifications cannot be enabled on this device."));
        return;
    }
    connect(m_service, &QLowEnergyService::descriptorWritten, this,
            [this](const QLowEnergyDescriptor &, const QByteArray &) {
                if (!m_ready) {
                    setReady();
                }
            });
    m_service->writeDescriptor(cccd, QByteArray::fromHex("0100"));
}

void Device::setReady()
{
    m_connectTimer.stop();
    m_ready = true;
    emit ready();
}

void Device::onCharacteristicChanged(const QLowEnergyCharacteristic &ch, const QByteArray &value)
{
    if (ch.uuid() == m_notifyChar.uuid()) {
        emit dataReceived(value);
    }
}

int Device::writeChunkSize() const
{
    return qMax(20, m_mtu - 3);
}

void Device::send(const QByteArray &frame)
{
    if (!m_ready) {
        qWarning() << "ViatomBle::Device: send while not connected";
        return;
    }
#ifdef OSCAR_BLE_BLUEZ_DBUS
    if (!m_writePath.isEmpty()) {
        const int size = writeChunkSize();
        for (int offset = 0; offset < frame.size(); offset += size) {
            m_dbusWrites.enqueue(frame.mid(offset, size));
        }
        if (!m_dbusWriteBusy) {
            writeNextDbusChunk();
        }
        return;
    }
#endif
    sendQt(frame);
}

void Device::sendQt(const QByteArray &frame)
{
    const auto mode = (m_writeChar.properties() & QLowEnergyCharacteristic::WriteNoResponse)
                          ? QLowEnergyService::WriteWithoutResponse
                          : QLowEnergyService::WriteWithResponse;
    const int size = writeChunkSize();
    for (int offset = 0; offset < frame.size(); offset += size) {
        m_service->writeCharacteristic(m_writeChar, frame.mid(offset, size), mode);
    }
}

#ifdef OSCAR_BLE_BLUEZ_DBUS

static const QString BLUEZ = QStringLiteral("org.bluez");
static const QString GATT_CHARACTERISTIC = QStringLiteral("org.bluez.GattCharacteristic1");

void Device::startBluezSetup()
{
    // Asks BlueZ for its objects, finds our two characteristics, then asks for the
    // notification descriptor. Every step is asynchronous; if one fails, Qt's own
    // calls are used instead.
    const int connection = m_connection;
    QDBusMessage call = QDBusMessage::createMethodCall(BLUEZ, QStringLiteral("/"),
        QStringLiteral("org.freedesktop.DBus.ObjectManager"), QStringLiteral("GetManagedObjects"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, connection](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (connection != m_connection) {
            return;
        }
        const QDBusMessage reply = w->reply();
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            qWarning() << "ViatomBle::Device: GetManagedObjects failed:" << reply.errorMessage();
            enableQtNotifications();
            return;
        }
        m_writePath = bluezCharacteristicPath(reply, m_spec.write);
        m_notifyPath = bluezCharacteristicPath(reply, m_spec.notify);
        if (m_writePath.isEmpty()) {
            qWarning() << "ViatomBle::Device: BlueZ write path not found, using Qt writes";
        }
        if (m_notifyPath.isEmpty()) {
            enableQtNotifications();
            return;
        }
        acquireNotify();
    });
}

QString Device::bluezCharacteristicPath(const QDBusMessage &reply, const QString &uuid) const
{
    // BlueZ puts the address in the object path: /org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF/...
    QString device = QStringLiteral("dev_") + m_info.address().toString();
    device.replace(QLatin1Char(':'), QLatin1Char('_'));
    const QString wanted = uuid.toLower();

    // a{oa{sa{sv}}}: object path -> interface -> property -> value
    const QDBusArgument objects = reply.arguments().at(0).value<QDBusArgument>();
    QString found;
    objects.beginMap();
    while (!objects.atEnd()) {
        objects.beginMapEntry();
        QDBusObjectPath path;
        objects >> path;
        const QDBusArgument interfaces = objects.asVariant().value<QDBusArgument>();
        QString characteristicUuid;
        interfaces.beginMap();
        while (!interfaces.atEnd()) {
            interfaces.beginMapEntry();
            QString interface;
            interfaces >> interface;
            const QDBusArgument properties = interfaces.asVariant().value<QDBusArgument>();
            properties.beginMap();
            while (!properties.atEnd()) {
                properties.beginMapEntry();
                QString name;
                QDBusVariant value;
                properties >> name >> value;
                properties.endMapEntry();
                if (interface == GATT_CHARACTERISTIC && name == QLatin1String("UUID")) {
                    characteristicUuid = value.variant().toString().toLower();
                }
            }
            properties.endMap();
            interfaces.endMapEntry();
        }
        interfaces.endMap();
        objects.endMapEntry();

        if (found.isEmpty() && characteristicUuid == wanted
                && path.path().contains(device, Qt::CaseInsensitive)) {
            found = path.path();
        }
    }
    objects.endMap();
    return found;
}

void Device::acquireNotify()
{
    const int connection = m_connection;
    QDBusMessage call = QDBusMessage::createMethodCall(BLUEZ, m_notifyPath, GATT_CHARACTERISTIC,
                                                       QStringLiteral("AcquireNotify"));
    call << QVariantMap();
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, connection](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (connection != m_connection) {
            return;
        }
        const QDBusMessage reply = w->reply();
        QDBusUnixFileDescriptor fd;
        if (reply.type() == QDBusMessage::ReplyMessage && reply.arguments().size() >= 2) {
            fd = reply.arguments().at(0).value<QDBusUnixFileDescriptor>();
        }
        m_notifyFd = fd.isValid() ? ::dup(fd.fileDescriptor()) : -1;
        if (m_notifyFd < 0) {
            qWarning() << "ViatomBle::Device: AcquireNotify unavailable, enabling notifications through Qt:"
                       << reply.errorMessage();
            enableQtNotifications();
            return;
        }
        const int mtu = int(reply.arguments().at(1).toUInt());
        if (mtu > 0) {
            m_mtu = mtu;
        }
        m_notifyWatcher = new QSocketNotifier(m_notifyFd, QSocketNotifier::Read, this);
        connect(m_notifyWatcher, &QSocketNotifier::activated, this, [this]() { onNotifyFdReadable(); });
        setReady();
    });
}

void Device::onNotifyFdReadable()
{
    QByteArray buffer(qMax(m_mtu, 512), Qt::Uninitialized);
    const ssize_t n = ::read(m_notifyFd, buffer.data(), buffer.size());
    if (n > 0) {
        buffer.resize(int(n));
        emit dataReceived(buffer);
    } else if (n == 0) {
        // BlueZ closed the descriptor: the link is gone.
        fail(tr("The device disconnected."));
    } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        // A broken descriptor stays readable, so stop here rather than spin.
        fail(tr("The Bluetooth connection failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
    }
}

void Device::closeNotifyFd()
{
    if (m_notifyWatcher) {
        m_notifyWatcher->setEnabled(false);
        m_notifyWatcher->deleteLater();
        m_notifyWatcher = nullptr;
    }
    if (m_notifyFd >= 0) {
        ::close(m_notifyFd);
        m_notifyFd = -1;
    }
}

void Device::writeNextDbusChunk()
{
    if (m_dbusWrites.isEmpty() || m_writePath.isEmpty()) {
        m_dbusWriteBusy = false;
        return;
    }
    m_dbusWriteBusy = true;
    const QByteArray chunk = m_dbusWrites.head();

    QDBusMessage call = QDBusMessage::createMethodCall(BLUEZ, m_writePath, GATT_CHARACTERISTIC,
                                                       QStringLiteral("WriteValue"));
    QVariantMap options;
    options[QStringLiteral("type")] = m_dbusWriteType;
    call << QVariant::fromValue(chunk) << QVariant::fromValue(options);

    const int connection = m_connection;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, connection](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const bool error = w->isError();
        const QString message = w->error().message();
        if (!m_ready || connection != m_connection) {
            return;
        }
        if (error && m_dbusWriteType == QLatin1String("command")) {
            // Write-without-response refused; use acknowledged writes from now on.
            qDebug() << "ViatomBle::Device: WriteValue 'command' failed, switching to 'request':" << message;
            m_dbusWriteType = QStringLiteral("request");
            writeNextDbusChunk();
            return;
        }
        if (error) {
            qWarning() << "ViatomBle::Device: WriteValue failed:" << message;
        }
        m_dbusWrites.dequeue();
        writeNextDbusChunk();
    });
}

#endif // OSCAR_BLE_BLUEZ_DBUS

} // namespace ViatomBle
