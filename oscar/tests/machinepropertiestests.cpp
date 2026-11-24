/* Machine Properties Unit Tests
 *
 * Tests that a device's properties (firmware version, model codes and similar values a
 * loader reads from the card) reach the database when the row is created and whenever
 * they change later, without losing stored keys the loader does not supply (GitLab #420).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "machinepropertiestests.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "SleepLib/appsettings.h"
#include "SleepLib/common.h"
#include "SleepLib/machine.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "database/database_manager.h"
#include "database/machine_repository.h"

namespace {

const QString kProfileName = QStringLiteral("MachinePropertiesUnitTest");
const QString kLoader = QStringLiteral("PropertiesTestLoader");
const QString kSerial = QStringLiteral("properties-cpap");

//! \brief The identity the test loader reports, with the given firmware property.
MachineInfo loaderInfo(const QString & firmware)
{
    MachineInfo info(MT_CPAP, 3, kLoader, QStringLiteral("Test"), QStringLiteral("Test CPAP"),
                     QString(), kSerial, QStringLiteral("Test"),
                     QDateTime(QDate(2026, 3, 1), QTime(8, 0)), 1);
    info.properties[QStringLiteral("firmware")] = firmware;
    info.properties[QStringLiteral("ModelCode")] = QStringLiteral("M1");
    return info;
}

} // namespace

QJsonObject MachinePropertiesTests::storedProperties() const
{
    MachineRepository repo;
    const MachineData data = repo.findById(m_machine->getDatabaseId());
    return QJsonDocument::fromJson(data.properties.toUtf8()).object();
}

void MachinePropertiesTests::initTestCase()
{
    // note: need dummy app for QtSql even though headless (each suite owns its own)
    static int argc = 1;
    static char appName[] = "test";
    static char *argv[] = { appName, nullptr };
    m_app = new QCoreApplication(argc, argv);

    if (DatabaseManager::instance().isOpen()) {
        DatabaseManager::instance().close();
    }

    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-propertiestests-XXXXXX"));
    QVERIFY(m_tempDir->isValid());

    m_previousAppData = GetAppData();
    SetAppData(m_tempDir->path());

    p_profile = nullptr;
    p_pref = new Preferences(QStringLiteral("Preferences"));
    p_pref->Open();
    AppSetting = new AppWideSetting(p_pref);
    QVERIFY(DatabaseManager::instance().initialize(m_tempDir->path() + QStringLiteral("/oscar.db")));

    schema::init();
    Profiles::Scan();

    const QString profileDir = m_tempDir->path() + QStringLiteral("/Profiles/") + kProfileName;
    p_profile = Profiles::Get(kProfileName);
    if (p_profile == nullptr) {
        p_profile = Profiles::Create(kProfileName, &profileDir);
    }
    QVERIFY(p_profile != nullptr);

    m_machine = p_profile->CreateMachine(loaderInfo(QStringLiteral("1.0")));
    QVERIFY(m_machine != nullptr);
    QVERIFY(m_machine->SaveToDatabase());
    QVERIFY(m_machine->getDatabaseId() > 0);
}

// Creating the row must store what the loader reported, not just the capabilities.
void MachinePropertiesTests::testNewRowStoresLoaderProperties()
{
    const QJsonObject stored = storedProperties();
    QCOMPARE(stored.value(QStringLiteral("firmware")).toString(), QStringLiteral("1.0"));
    QCOMPARE(stored.value(QStringLiteral("ModelCode")).toString(), QStringLiteral("M1"));
    QCOMPARE(stored.value(QStringLiteral("capabilities")).toInt(-1), 3);
}

// A firmware update reported through setInfo() reaches the database, and a stored key the
// loader does not supply (here a value carried over from OSCAR 1.x) is kept.
void MachinePropertiesTests::testSetInfoStoresChangedProperties()
{
    QJsonObject legacy = storedProperties();
    legacy[QStringLiteral("timeoffsets")] = QStringLiteral("25200000");
    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(QStringLiteral("UPDATE machines SET properties = :p WHERE id = :id"));
    query.bindValue(QStringLiteral(":p"),
                    QString::fromUtf8(QJsonDocument(legacy).toJson(QJsonDocument::Compact)));
    query.bindValue(QStringLiteral(":id"), m_machine->getDatabaseId());
    QVERIFY(query.exec());

    m_machine->setInfo(loaderInfo(QStringLiteral("2.0")));

    const QJsonObject stored = storedProperties();
    QCOMPARE(stored.value(QStringLiteral("firmware")).toString(), QStringLiteral("2.0"));
    QCOMPARE(stored.value(QStringLiteral("timeoffsets")).toString(), QStringLiteral("25200000"));
    QCOMPARE(stored.value(QStringLiteral("capabilities")).toInt(-1), 3);
}

// Some loaders (ResMed, Intellipap) write into info.properties directly instead of calling
// setInfo(). Profile::StoreMachines(), run after every import, must store those values.
void MachinePropertiesTests::testStoreMachinesStoresDirectWrites()
{
    m_machine->info.properties[QStringLiteral("firmware")] = QStringLiteral("3.0");
    m_machine->info.properties[QStringLiteral("NewKey")] = QStringLiteral("new");

    QVERIFY(p_profile->StoreMachines());

    const QJsonObject stored = storedProperties();
    QCOMPARE(stored.value(QStringLiteral("firmware")).toString(), QStringLiteral("3.0"));
    QCOMPARE(stored.value(QStringLiteral("NewKey")).toString(), QStringLiteral("new"));
    QCOMPARE(stored.value(QStringLiteral("timeoffsets")).toString(), QStringLiteral("25200000"));
}

// A stored number whose text form is unchanged is left as a number: the in-memory copy is
// text, and comparing it with the stored value must not count as a change.
void MachinePropertiesTests::testUnchangedNumericValueKeepsItsType()
{
    m_machine->info.properties[QStringLiteral("capabilities")] = QStringLiteral("3");

    QVERIFY(p_profile->StoreMachines());

    const QJsonValue cap = storedProperties().value(QStringLiteral("capabilities"));
    QVERIFY(cap.isDouble());
    QCOMPARE(cap.toInt(), 3);
}

void MachinePropertiesTests::cleanupTestCase()
{
    Profiles::profiles.clear();

    delete p_profile;
    p_profile = nullptr;
    delete AppSetting;
    AppSetting = nullptr;
    delete p_pref;
    p_pref = nullptr;

    DatabaseManager::instance().close();
    SetAppData(m_previousAppData);

    delete m_tempDir;
    m_tempDir = nullptr;
    delete m_app;
    m_app = nullptr;
}
