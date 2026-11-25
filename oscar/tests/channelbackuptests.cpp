/* Channel Backup Unit Tests
 *
 * Tests that channel overrides survive a profile backup and restore, and that a backup
 * made before schema v20 (which holds a channels.sql) is still restorable.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "channelbackuptests.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "SleepLib/appsettings.h"
#include "SleepLib/common.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "database/backup/profile_backup.h"
#include "database/backup/profile_restore.h"
#include "database/channel_repository.h"
#include "database/database_manager.h"
#include "database/profile_repository.h"
#include "zip.h"

namespace {

const QString kProfileName = QStringLiteral("ChannelBackupUnitTest");

//! Hash of all *.sql files in \p dir in name order, as the backup writes it to the manifest.
QString hashSqlFiles(const QString &dir)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString &name : QDir(dir).entryList({ "*.sql" }, QDir::Files, QDir::Name)) {
        QFile f(QDir(dir).filePath(name));
        if (!f.open(QIODevice::ReadOnly)) {
            return QString();
        }
        hash.addData(f.readAll());
    }
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace

void ChannelBackupTests::initTestCase()
{
    // note: need dummy app for QtSql even though headless (each suite owns its own)
    static int argc = 1;
    static char appName[] = "test";
    static char *argv[] = { appName, nullptr };
    m_app = new QCoreApplication(argc, argv);

    if (DatabaseManager::instance().isOpen()) {
        DatabaseManager::instance().close();
    }

    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-channelbackuptests-XXXXXX"));
    QVERIFY(m_tempDir->isValid());

    m_previousAppData = GetAppData();
    SetAppData(m_tempDir->path());

    p_profile = nullptr;
    p_pref = new Preferences(QStringLiteral("Preferences"));
    p_pref->Open();
    AppSetting = new AppWideSetting(p_pref);
    QVERIFY(DatabaseManager::instance().initialize(m_tempDir->path() + QStringLiteral("/oscar.db")));
    m_db = DatabaseManager::instance().database();

    schema::init();
    Profiles::Scan();

    const QString profileDir = m_tempDir->path() + QStringLiteral("/Profiles/") + kProfileName;
    p_profile = Profiles::Get(kProfileName);
    if (p_profile == nullptr) {
        p_profile = Profiles::Create(kProfileName, &profileDir);
    }
    QVERIFY(p_profile != nullptr);
    m_profile = p_profile;

    m_profileId = ProfileRepository().findByUsername(kProfileName).id;
    QVERIFY(m_profileId > 0);
}

void ChannelBackupTests::init()
{
    // Every test starts from the code defaults, with no stored overrides or old rows.
    schema::resetChannelsToDefaults();
    exec(QString("DELETE FROM profile_channel_overrides WHERE profile_id = %1").arg(m_profileId));
    exec(QString("DELETE FROM channels_legacy WHERE profile_id = %1").arg(m_profileId));
}

void ChannelBackupTests::exec(const QString &sql)
{
    QSqlQuery query(m_db);
    QVERIFY2(query.exec(sql), qPrintable(sql + ": " + query.lastError().text()));
}

QString ChannelBackupTests::makeBackup(const QString &name)
{
    ProfileBackup backup(m_profileId);
    backup.setOutputPath(m_tempDir->path());
    backup.setFilename(name);
    if (!backup.createBackup()) {
        qWarning() << "createBackup failed:" << backup.getErrorMessage();
        return QString();
    }
    return backup.getBackupPath();
}

// A label changed by the user is stored as an override, is exported by a backup, and is an
// override of the restored profile; nothing else is copied.
void ChannelBackupTests::testOverridesSurviveBackupAndRestore() {
    schema::channel[CPAP_Obstructive].setLabel("Zz Backup Label Zz");
    p_profile->saveChannels();

    ProfileBackup backup(m_profileId);
    backup.setOutputPath(m_tempDir->path());
    backup.setFilename("channelbackup");
    QVERIFY2(backup.createBackup(), "createBackup failed");
    const QString package = QDir(m_tempDir->path()).entryList({ "channelbackup*" }).value(0);
    QVERIFY(!package.isEmpty());

    ProfileRestore restore(m_tempDir->filePath(package));
    restore.setNewUsername("ChannelBackupRestored");
    QVERIFY(restore.validatePackage());
    QVERIFY(restore.checkCompatibility());
    QVERIFY2(restore.restoreProfile(), "restoreProfile failed");
    Profiles::Scan();
    p_profile = Profiles::Get(kProfileName);

    const qint64 restoredId = ProfileRepository().findByUsername("ChannelBackupRestored").id;
    QVERIFY(restoredId > 0);
    const auto rows = ChannelRepository().findOverrides(restoredId);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].channelId, ChannelID(CPAP_Obstructive));
    QCOMPARE(rows[0].values.label.value(), QString("Zz Backup Label Zz"));
}

// A backup of schema v12-v19 holds channels.sql. Restore parks its rows in channels_legacy
// and Profiles::Scan() converts them. The package is a real backup of the test profile with
// the new channel files replaced by an old-style channels.sql and a schema 19 manifest.
void ChannelBackupTests::testOldChannelsFileIsParkedAndConverted() {
    // One old row, with the exact columns of the pre-v20 channels table: a label that OSCAR
    // never shipped, every other field at the channel's default.
    schema::Channel &oa = schema::channel[CPAP_Obstructive];
    const schema::ChannelDefaults &d = oa.defaults();
    ChannelData row;
    row.channelId = oa.id();
    row.channelCode = oa.code();
    row.type = oa.type();
    row.enabled = d.enabled;
    row.defaultColor = d.color;
    row.fullname = d.fullname;
    row.label = QStringLiteral("Zz Old Backup Label Zz");
    row.description = d.description;
    row.lowerThreshold = d.lowerThreshold;
    row.lowerThresholdColor = d.lowerThresholdColor;
    row.upperThreshold = d.upperThreshold;
    row.upperThresholdColor = d.upperThresholdColor;
    row.showInOverview = d.showInOverview;
    QVERIFY(ChannelRepository().insertLegacy(m_profileId, { row }));

    const QString real = makeBackup("oldchannelbackup");
    QVERIFY(!real.isEmpty());
    // The source profile must not convert its own copy during the Scan() below.
    exec(QString("DELETE FROM channels_legacy WHERE profile_id = %1").arg(m_profileId));

    // Unpack, turn the exported channels_legacy rows into an old-style channels.sql, drop
    // the v20 channel files, and make the manifest say schema 19 with a matching checksum.
    const QString dir = m_tempDir->filePath("old-extract");
    UnzipFile unzip;
    QVERIFY(unzip.Open(real));
    QVERIFY(unzip.ExtractAll(dir));
    unzip.Close();
    const QString dbDir = dir + "/database";

    QFile legacy(dbDir + "/channels_legacy.sql");
    QVERIFY(legacy.open(QIODevice::ReadOnly));
    QString text = QString::fromUtf8(legacy.readAll());
    legacy.close();
    QVERIFY(text.contains("INSERT INTO channels_legacy"));
    QVERIFY(text.contains("Zz Old Backup Label Zz"));
    text.replace("INSERT INTO channels_legacy", "INSERT INTO channels");
    QFile old(dbDir + "/channels.sql");
    QVERIFY(old.open(QIODevice::WriteOnly));
    old.write(text.toUtf8());
    old.close();
    QVERIFY(QFile::remove(dbDir + "/channels_legacy.sql"));
    QVERIFY(QFile::remove(dbDir + "/profile_channel_overrides.sql"));

    QFile manifestFile(dir + "/manifest.json");
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    manifestFile.close();
    manifest["schema_version"] = 19;
    QJsonArray tables;
    for (const QString &f : QDir(dbDir).entryList({ "*.sql" }, QDir::Files, QDir::Name)) {
        tables.append(QFileInfo(f).baseName());
    }
    manifest["tables_exported"] = tables;
    QJsonObject sums = manifest["checksums"].toObject();
    sums["database_export"] = hashSqlFiles(dbDir);
    manifest["checksums"] = sums;
    QVERIFY(manifestFile.open(QIODevice::WriteOnly));
    manifestFile.write(QJsonDocument(manifest).toJson());
    manifestFile.close();

    const QString oldPackage = m_tempDir->filePath("oldchannels.oscar");
    ZipFile zip;
    QVERIFY(zip.Open(oldPackage));
    QVERIFY(zip.AddFile(dir + "/manifest.json", "manifest.json"));
    QVERIFY(zip.AddDirectory(dbDir, "database"));
    zip.Close();

    ProfileRestore restore(oldPackage);
    restore.setNewUsername("ChannelBackupOld");
    QVERIFY2(restore.validatePackage(), qPrintable(restore.getErrorMessage()));
    QVERIFY2(restore.checkCompatibility(), qPrintable(restore.getErrorMessage()));
    QVERIFY2(restore.restoreProfile(), qPrintable(restore.getErrorMessage()));

    const qint64 restoredId = ProfileRepository().findByUsername("ChannelBackupOld").id;
    QVERIFY(restoredId > 0);
    QCOMPARE(ChannelRepository().findLegacy(restoredId).size(), 1);   // parked until the Scan

    Profiles::Scan();
    p_profile = Profiles::Get(kProfileName);

    const auto rows = ChannelRepository().findOverrides(restoredId);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].channelId, ChannelID(CPAP_Obstructive));
    QCOMPARE(rows[0].values.label.value(), QString("Zz Old Backup Label Zz"));
    QVERIFY(ChannelRepository().findLegacy(restoredId).isEmpty());
}

void ChannelBackupTests::cleanupTestCase()
{
    schema::resetChannelsToDefaults();

    const QList<Profile *> open = Profiles::profiles.values();
    Profiles::profiles.clear();
    for (Profile *prof : open) {
        delete prof;
    }
    p_profile = nullptr;
    m_profile = nullptr;
    delete AppSetting;
    AppSetting = nullptr;
    delete p_pref;
    p_pref = nullptr;

    m_db = QSqlDatabase();
    DatabaseManager::instance().close();
    SetAppData(m_previousAppData);

    delete m_tempDir;
    m_tempDir = nullptr;
    delete m_app;
    m_app = nullptr;
}
