/* Profile Rename Unit Tests
 *
 * Tests Profiles::Rename(): renaming a profile in place (data folder, database row,
 * Profile object, Profiles::profiles key) without a restart or a reload.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "profilerenametests.h"

#include <QDir>
#include <QFile>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "SleepLib/appsettings.h"
#include "SleepLib/common.h"
#include "SleepLib/machine.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "database/database_manager.h"
#include "database/profile_repository.h"

void ProfileRenameTests::initTestCase()
{
    // note: need dummy app for QtSql even though headless (each suite owns its own)
    static int argc = 1;
    static char appName[] = "test";
    static char *argv[] = { appName, nullptr };
    m_app = new QCoreApplication(argc, argv);

    if (DatabaseManager::instance().isOpen()) {
        DatabaseManager::instance().close();
    }

    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-profilerenametests-XXXXXX"));
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

    // Profiles::Rename() works in "{home}/Profiles": make sure that is the temporary folder
    // before any test renames anything.
    QCOMPARE(QDir::cleanPath(p_pref->Get("{home}/Profiles/")),
             QDir::cleanPath(m_tempDir->path() + QStringLiteral("/Profiles")));
}

QString ProfileRenameTests::folderOf(const QString &name) const
{
    return m_tempDir->path() + QStringLiteral("/Profiles/") + name;
}

Profile *ProfileRenameTests::createProfile(const QString &name)
{
    const QString dir = folderOf(name);
    return Profiles::Create(name, &dir);
}

int ProfileRenameTests::profileRowCount() const
{
    QSqlQuery query(DatabaseManager::instance().database());
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM profiles")) || !query.next()) {
        return -1;
    }
    return query.value(0).toInt();
}

void ProfileRenameTests::testRenameInPlace()
{
    const QString oldName = QStringLiteral("RenameOld");
    const QString newName = QStringLiteral("RenameNew");

    Profile *profile = createProfile(oldName);
    QVERIFY(profile != nullptr);
    const qint64 id = ProfileRepository().findByUsername(oldName).id;
    QVERIFY(id > 0);

    // An open profile holds a lockfile in its folder
    profile->addLock();
    QVERIFY(QFile::exists(folderOf(oldName) + QStringLiteral("/lockfile")));

    QCOMPARE(Profiles::Rename(profile, newName), Profiles::RenameOk);

    // On disk
    QVERIFY(!QDir(folderOf(oldName)).exists());
    QVERIFY(QDir(folderOf(newName)).exists());

    // In the database: the same row, under the new name
    ProfileRepository repo;
    QCOMPARE(repo.findByUsername(oldName).id, qint64(0));
    const ProfileData row = repo.findByUsername(newName);
    QCOMPARE(row.id, id);
    QCOMPARE(row.dataFolder, QStringLiteral("%PROFDIR%/") + newName);

    // In memory: the same object, found under the new name only
    QCOMPARE(profile->user->userName(), newName);
    QCOMPARE(Profiles::Get(newName), profile);
    QVERIFY(Profiles::Get(oldName) == nullptr);
    QCOMPARE(QDir::cleanPath(profile->path()), QDir::cleanPath(folderOf(newName)));
    QCOMPARE(QDir::cleanPath(profile->dataFolder()), QDir::cleanPath(folderOf(newName)));

    // Device data paths follow
    Machine *journal = profile->GetMachine(MT_JOURNAL);
    QVERIFY(journal != nullptr);
    QVERIFY(QDir::cleanPath(journal->getDataPath()).startsWith(QDir::cleanPath(folderOf(newName)) + "/"));

    // The lockfile moved with the folder and is still this profile's to remove
    QVERIFY(QFile::exists(folderOf(newName) + QStringLiteral("/lockfile")));
    QVERIFY(profile->removeLock());
    QVERIFY(!QFile::exists(folderOf(newName) + QStringLiteral("/lockfile")));
}

void ProfileRenameTests::testSaveAfterRenameAddsNoProfileRow()
{
    const QString oldName = QStringLiteral("SaveOld");
    const QString newName = QStringLiteral("SaveNew");

    Profile *profile = createProfile(oldName);
    QVERIFY(profile != nullptr);
    const qint64 id = ProfileRepository().findByUsername(oldName).id;
    const int rows = profileRowCount();
    QVERIFY(rows > 0);

    QCOMPARE(Profiles::Rename(profile, newName), Profiles::RenameOk);

    // Saving looks the profile up by name. A stale name here used to create a second
    // row under the old name and store the profile's settings in it.
    profile->user->setFirstName(QStringLiteral("Renamed"));
    QVERIFY(profile->Save());

    QCOMPARE(profileRowCount(), rows);
    ProfileRepository repo;
    QCOMPARE(repo.findByUsername(oldName).id, qint64(0));
    QCOMPARE(repo.findByUsername(newName).id, id);
    QVERIFY(!QDir(folderOf(oldName)).exists());
}

void ProfileRenameTests::testLastUsedProfileSettingFollows()
{
    Profile *current = createProfile(QStringLiteral("LastUsedOld"));
    Profile *other = createProfile(QStringLiteral("OtherOld"));
    QVERIFY(current != nullptr);
    QVERIFY(other != nullptr);

    AppSetting->setProfileName(QStringLiteral("LastUsedOld"));

    // Renaming another profile leaves the setting alone
    QCOMPARE(Profiles::Rename(other, QStringLiteral("OtherNew")), Profiles::RenameOk);
    QCOMPARE(AppSetting->profileName(), QStringLiteral("LastUsedOld"));

    // Renaming the profile it names takes the setting along
    QCOMPARE(Profiles::Rename(current, QStringLiteral("LastUsedNew")), Profiles::RenameOk);
    QCOMPARE(AppSetting->profileName(), QStringLiteral("LastUsedNew"));
}

void ProfileRenameTests::testNameInUseChangesNothing()
{
    const QString name = QStringLiteral("TakenA");
    const QString taken = QStringLiteral("TakenB");

    Profile *profile = createProfile(name);
    Profile *otherProfile = createProfile(taken);
    QVERIFY(profile != nullptr);
    QVERIFY(otherProfile != nullptr);
    const qint64 id = ProfileRepository().findByUsername(name).id;

    QCOMPARE(Profiles::Rename(profile, taken), Profiles::RenameNameInUse);

    // A folder alone, with no profile behind it, also blocks the name
    QVERIFY(QDir().mkpath(folderOf(QStringLiteral("TakenFolder"))));
    QCOMPARE(Profiles::Rename(profile, QStringLiteral("TakenFolder")), Profiles::RenameNameInUse);

    QCOMPARE(profile->user->userName(), name);
    QCOMPARE(Profiles::Get(name), profile);
    QCOMPARE(Profiles::Get(taken), otherProfile);
    QCOMPARE(ProfileRepository().findByUsername(name).id, id);
    QVERIFY(QDir(folderOf(name)).exists());
    QCOMPARE(QDir::cleanPath(profile->path()), QDir::cleanPath(folderOf(name)));
}

void ProfileRenameTests::testMissingFolderChangesNothing()
{
    const QString name = QStringLiteral("NoFolder");
    const QString newName = QStringLiteral("NoFolderNew");

    Profile *profile = createProfile(name);
    QVERIFY(profile != nullptr);
    const qint64 id = ProfileRepository().findByUsername(name).id;
    QVERIFY(QDir(folderOf(name)).removeRecursively());

    QCOMPARE(Profiles::Rename(profile, newName), Profiles::RenameFolderFailed);

    QCOMPARE(profile->user->userName(), name);
    QCOMPARE(Profiles::Get(name), profile);
    QVERIFY(Profiles::Get(newName) == nullptr);
    ProfileRepository repo;
    QCOMPARE(repo.findByUsername(name).id, id);
    QCOMPARE(repo.findByUsername(newName).id, qint64(0));
    QVERIFY(!QDir(folderOf(newName)).exists());
}

void ProfileRenameTests::cleanupTestCase()
{
    schema::resetChannelsToDefaults();

    const QList<Profile *> open = Profiles::profiles.values();
    Profiles::profiles.clear();
    for (Profile *prof : open) {
        delete prof;
    }
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
