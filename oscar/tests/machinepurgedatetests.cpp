/* Machine Purge Date Unit Tests
 *
 * Tests that a device's purge date reaches the database when it is set and when it is
 * cleared, so that it survives a restart of OSCAR (GitLab #415).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "machinepurgedatetests.h"

#include <QDateTime>
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

const QString kProfileName = QStringLiteral("MachinePurgeDateUnitTest");

} // namespace

void MachinePurgeDateTests::initTestCase()
{
    // note: need dummy app for QtSql even though headless (each suite owns its own)
    static int argc = 1;
    static char appName[] = "test";
    static char *argv[] = { appName, nullptr };
    m_app = new QCoreApplication(argc, argv);

    if (DatabaseManager::instance().isOpen()) {
        DatabaseManager::instance().close();
    }

    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-purgedatetests-XXXXXX"));
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

    const MachineInfo info(
        MT_CPAP, 0, QStringLiteral("PurgeDateTestLoader"), QStringLiteral("Test"),
        QStringLiteral("Test CPAP"), QString(), QStringLiteral("purge-date-cpap"),
        QStringLiteral("Test"), QDateTime(QDate(2026, 3, 1), QTime(8, 0)), 1);
    m_machine = p_profile->CreateMachine(info);
    QVERIFY(m_machine != nullptr);
    QVERIFY(m_machine->SaveToDatabase());
    QVERIFY(m_machine->getDatabaseId() > 0);
}

// A purge sets the purge date; it must be in the device's database row straight away,
// because nothing else stores a device that already has a row.
void MachinePurgeDateTests::testSetPurgeDateIsStored()
{
    MachineRepository repo;
    QVERIFY(repo.findById(m_machine->getDatabaseId()).purgeDate.isEmpty());

    m_machine->setPurgeDate(QDate(2026, 1, 1));

    QCOMPARE(m_machine->purgeDate(), QDate(2026, 1, 1));
    QCOMPARE(repo.findById(m_machine->getDatabaseId()).purgeDate, QStringLiteral("2026-01-01"));
}

// An import clears the purge date; the stored one must go too, or every later import
// would start at the old purge date.
void MachinePurgeDateTests::testClearPurgeDateIsStored()
{
    MachineRepository repo;
    m_machine->setPurgeDate(QDate(2026, 2, 2));
    QCOMPARE(repo.findById(m_machine->getDatabaseId()).purgeDate, QStringLiteral("2026-02-02"));

    m_machine->clearPurgeDate();

    QVERIFY(!m_machine->purgeDate().isValid());
    QVERIFY(repo.findById(m_machine->getDatabaseId()).purgeDate.isEmpty());
}

// Storing the purge date changes that column only.
void MachinePurgeDateTests::testOtherColumnsUntouched()
{
    MachineRepository repo;
    const MachineData before = repo.findById(m_machine->getDatabaseId());

    m_machine->setPurgeDate(QDate(2026, 1, 15));

    const MachineData after = repo.findById(m_machine->getDatabaseId());
    QCOMPARE(after.purgeDate, QStringLiteral("2026-01-15"));
    QCOMPARE(after.serialNumber, before.serialNumber);
    QCOMPARE(after.model, before.model);
    QCOMPARE(after.loaderName, before.loaderName);
    QCOMPARE(after.lastImported, before.lastImported);
    QCOMPARE(after.dataVersion, before.dataVersion);
    QCOMPARE(after.properties, before.properties);

    m_machine->clearPurgeDate();
}

// A device that has no database row yet keeps the date in memory and writes nothing.
void MachinePurgeDateTests::testNoDatabaseRowIsHarmless()
{
    Machine mach(nullptr, 99);
    QCOMPARE(mach.getDatabaseId(), qint64(0));

    mach.setPurgeDate(QDate(2026, 1, 1));
    QCOMPARE(mach.purgeDate(), QDate(2026, 1, 1));
    mach.clearPurgeDate();
    QVERIFY(!mach.purgeDate().isValid());
}

void MachinePurgeDateTests::cleanupTestCase()
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
