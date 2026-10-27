/* Viatom BLE device scanner
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QBluetoothUuid>

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

DeviceFamily deviceFamilyFromName(const QString &rawName)
{
    const QString name = rawName.trimmed();
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
    return DeviceFamily::NotViatom;
}

DeviceFamily deviceFamily(const QBluetoothDeviceInfo &info)
{
    const QList<QBluetoothUuid> services = info.serviceUuids();
    if (services.contains(QBluetoothUuid(QString::fromLatin1(O2RING_S_SERVICE)))) {
        return DeviceFamily::O2RingS;
    }
    if (services.contains(QBluetoothUuid(QString::fromLatin1(SERVICE_UUID)))) {
        // The O2Ring S family can also be set to use this service, so let the name decide.
        DeviceFamily byName = deviceFamilyFromName(info.name());
        return byName == DeviceFamily::O2RingS ? byName : DeviceFamily::Classic;
    }
    return deviceFamilyFromName(info.name());
}

Scanner::Scanner(QObject *parent)
    : QObject(parent)
{
    connect(&m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, &Scanner::consider);
    connect(&m_agent, &QBluetoothDeviceDiscoveryAgent::deviceUpdated, this,
            [this](const QBluetoothDeviceInfo &info, QBluetoothDeviceInfo::Fields) { consider(info); });
    connect(&m_agent, &QBluetoothDeviceDiscoveryAgent::finished, this, &Scanner::finished);
    connect(&m_agent, &QBluetoothDeviceDiscoveryAgent::canceled, this, &Scanner::finished);
    connect(&m_agent, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this,
            [this](QBluetoothDeviceDiscoveryAgent::Error) { emit error(m_agent.errorString()); });
}

void Scanner::start(int timeoutMs)
{
    m_agent.stop();
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
    if (family != DeviceFamily::NotViatom) {
        emit deviceFound(info, family);
    }
}

} // namespace ViatomBle
