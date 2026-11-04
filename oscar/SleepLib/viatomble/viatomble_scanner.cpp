/* Viatom BLE device scanner
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QBluetoothUuid>
#include <QCoreApplication>
#include <QDebug>
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
#include <QPermissions>
#endif

#include <QRegularExpression>

#include "viatomble_protocol.h"
#include "viatomble_scanner.h"

namespace ViatomBle {

// Bluetooth names from the ViHealth SDK (Bluetooth.BT_NAME_*). Devices usually add a
// suffix, e.g. "O2Ring 2345", so names are matched by prefix. SleepO2, Checkme O2,
// SnoreO2, KidsO2, BabyO2 and OxyLink use the classic protocol but the SDK gives no
// name for them; those entries are the product names and still need confirming.
static const char *const CLASSIC_NAMES[] = {
    "O2Ring", "O2RingF", "O2Ring-RE", "O2R WAVE", "WearO2", "SleepU", "SleepO2", "Checkme O2",
    "SnoreO2", "KidsO2", "BabyO2", "BabyO2N", "OxyLink", "OxyRing", "OxyU", "OxyU SE",
    "Oxyfit", "Oxyfit-WPS", "O2M", "O2M-WPS", "O2S", "O2NCI", "CMRing", "BBSM S1", "AI-S100",
    "Lepu PO6", "BM10", "JFR1001", "JFK1001", "OMP", "SHQFlex", "O2 Intg", "PO2",
};
static const char *const O2RING_S_NAMES[] = {
    "O2Ring S", "O2Ring SC", "O2Ring SF", "O2Ring SP", "O2RMed S", "O2MP", "S8-AW",
    "SHQO2Pro", "PO2B", "PO2B-PPG",
};
static const char *const O2RING_S_SERVICE = "e8fb0001-a14b-98f9-831b-4e2941d01248";

//! True if name is prefix itself or prefix followed by a space and more.
static bool matchesName(const QString &name, const char *prefix)
{
    const QString p = QString::fromLatin1(prefix);
    return name.compare(p, Qt::CaseInsensitive) == 0
        || name.startsWith(p + QLatin1Char(' '), Qt::CaseInsensitive);
}

// EMAY's names (its Android app matches them by prefix alone). EMO-80 has EMAY's own
// protocol; the others use Contec's, which OSCAR can't download from yet. "SleepO2" is
// also a Viatom name, so it is handled in deviceFamily().
static const char *const EMAY_CONTEC_NAMES[] = {
    "SleepO2-2", "EMO-100", "EM90", "EM60", "EMAY BT Oximeter", "EMAY Oximeter",
};
static const char *const EMAY_SERVICE = "0000ff12-0000-1000-8000-00805f9b34fb";

//! A "SleepO2" name, which Viatom and EMAY share (not EMAY's "SleepO2-2").
static bool isSleepO2(const QString &name)
{
    return name.startsWith(QLatin1String("SleepO2"), Qt::CaseInsensitive)
        && !name.startsWith(QLatin1String("SleepO2-2"), Qt::CaseInsensitive);
}

DeviceFamily deviceFamilyFromName(const QString &rawName)
{
    const QString name = rawName.trimmed();
    if (name.startsWith(QLatin1String("EMO-80"), Qt::CaseInsensitive)) {
        return DeviceFamily::Emay;
    }
    for (const char *prefix : EMAY_CONTEC_NAMES) {
        if (name.startsWith(QLatin1String(prefix), Qt::CaseInsensitive)) {
            return DeviceFamily::Unsupported;
        }
    }
    // Contec's SDK: "SpO2" and a two-digit model code, e.g. "SpO202" for a CMS50F.
    static const QRegularExpression contec(QStringLiteral("^SpO2\\d\\d"));
    if (contec.match(name).hasMatch()) {
        return DeviceFamily::Unsupported;
    }
    // The O2Ring S names overlap the classic "O2Ring" and "PO2", so check them first.
    for (const char *prefix : O2RING_S_NAMES) {
        if (matchesName(name, prefix)) {
            return DeviceFamily::O2RingS;
        }
    }
    for (const char *prefix : CLASSIC_NAMES) {
        if (matchesName(name, prefix)) {
            return DeviceFamily::Classic;
        }
    }
    // EMAY's SleepO2 names need no space after "SleepO2", unlike Viatom's.
    if (isSleepO2(name)) {
        return DeviceFamily::Unsupported;
    }
    return DeviceFamily::NotViatom;
}

DeviceFamily deviceFamily(const QString &rawName, const QStringList &services, bool preferEmay)
{
    const QString name = rawName.trimmed();
    auto has = [&services](const char *uuid) {
        return services.contains(QString::fromLatin1(uuid), Qt::CaseInsensitive);
    };
    if (has(O2RING_S_SERVICE)) {
        return DeviceFamily::O2RingS;
    }
    if (has(SERVICE_UUID)) {
        // The O2Ring S family can also be set to use this service, so let the name decide.
        DeviceFamily byName = deviceFamilyFromName(name);
        return byName == DeviceFamily::O2RingS ? byName : DeviceFamily::Classic;
    }
    if (isSleepO2(name)) {
        // Viatom's or EMAY's: EMAY's service says EMAY, and so does the user's choice.
        return (has(EMAY_SERVICE) || preferEmay) ? DeviceFamily::Unsupported : DeviceFamily::Classic;
    }
    return deviceFamilyFromName(name);
}

DeviceFamily deviceFamily(const QBluetoothDeviceInfo &info, bool preferEmay)
{
    QStringList services;
    for (const QBluetoothUuid &uuid : info.serviceUuids()) {
        services << uuid.toString(QUuid::WithoutBraces);
    }
    return deviceFamily(info.name(), services, preferEmay);
}

QString brandName(const QString &rawName, DeviceFamily family)
{
    switch (family) {
    case DeviceFamily::Classic:
    case DeviceFamily::O2RingS:
        return QStringLiteral("Viatom/Wellue");
    case DeviceFamily::Emay:
        return QStringLiteral("EMAY");
    case DeviceFamily::Unsupported:
        return rawName.trimmed().startsWith(QLatin1String("SpO2"), Qt::CaseInsensitive)
            ? QStringLiteral("Contec") : QStringLiteral("EMAY");
    case DeviceFamily::NotViatom:
        break;
    }
    return QString();
}

Scanner::Scanner(QObject *parent)
    : QObject(parent)
{
    connect(&m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, &Scanner::consider);
    connect(&m_agent, &QBluetoothDeviceDiscoveryAgent::deviceUpdated, this,
            [this](const QBluetoothDeviceInfo &info, QBluetoothDeviceInfo::Fields) { consider(info); });
    auto done = [this]() {
        qDebug().noquote() << "ViatomBle::Scanner: scan ended;" << m_seen.size() << "Bluetooth LE devices seen,"
                           << m_viatomSeen << "of them oximeters OSCAR knows";
        emit finished();
    };
    connect(&m_agent, &QBluetoothDeviceDiscoveryAgent::finished, this, done);
    connect(&m_agent, &QBluetoothDeviceDiscoveryAgent::canceled, this, done);
    connect(&m_agent, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this,
            [this](QBluetoothDeviceDiscoveryAgent::Error) { emit error(m_agent.errorString()); });
}

void Scanner::start(int timeoutMs)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    // macOS (and iOS/Android) need the user's permission at run time as well as the
    // Info.plist entry; elsewhere this reports Granted.
    QBluetoothPermission permission;
    permission.setCommunicationModes(QBluetoothPermission::Access);
    switch (qApp->checkPermission(permission)) {
    case Qt::PermissionStatus::Undetermined:
        qApp->requestPermission(permission, this, [this, timeoutMs](const QPermission &result) {
            if (result.status() == Qt::PermissionStatus::Granted) {
                start(timeoutMs);
            } else {
                emit error(tr("OSCAR is not allowed to use Bluetooth. You can allow it in the system's "
                              "privacy settings."));
            }
        });
        return;
    case Qt::PermissionStatus::Denied:
        emit error(tr("OSCAR is not allowed to use Bluetooth. You can allow it in the system's "
                      "privacy settings."));
        return;
    case Qt::PermissionStatus::Granted:
        break;
    }
#endif
    m_agent.stop();
    m_seen.clear();
    m_viatomSeen = 0;
    qDebug().noquote() << "ViatomBle::Scanner: scanning for" << timeoutMs << "ms";
    m_agent.setLowEnergyDiscoveryTimeout(timeoutMs);
    m_agent.start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void Scanner::stop()
{
    m_agent.stop();
}

void Scanner::consider(const QBluetoothDeviceInfo &info)
{
    if (!(info.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration)) {
        return;
    }
    const DeviceFamily family = deviceFamily(info);

    // Each device once, and again when its name changes: a Bluetooth stack can report a
    // device before its name, which OSCAR needs to recognise most Viatom devices.
    const QString id = info.address().isNull() ? info.deviceUuid().toString() : info.address().toString();
    const auto seen = m_seen.constFind(id);
    if (seen == m_seen.constEnd() || seen.value() != info.name()) {
        const bool wasViatom = seen != m_seen.constEnd() && deviceFamilyFromName(seen.value()) != DeviceFamily::NotViatom;
        if (family != DeviceFamily::NotViatom && !wasViatom) {
            m_viatomSeen++;
        }
        m_seen.insert(id, info.name());
        QStringList services;
        for (const QBluetoothUuid &uuid : info.serviceUuids()) {
            services << uuid.toString(QUuid::WithoutBraces);
        }
        qDebug().noquote() << "ViatomBle::Scanner: saw"
                           << (info.name().isEmpty() ? QStringLiteral("(no name)") : info.name())
                           << id << "rssi" << info.rssi() << "services [" << services.join(QLatin1Char(' ')) << "]"
                           << (family == DeviceFamily::O2RingS ? "-> O2Ring S family"
                               : family == DeviceFamily::Classic ? "-> classic Viatom"
                               : family == DeviceFamily::Emay ? "-> EMAY"
                               : family == DeviceFamily::Unsupported ? "-> not supported yet" : "");
    }

    if (family != DeviceFamily::NotViatom) {
        emit deviceFound(info, family);
    }
}

} // namespace ViatomBle
