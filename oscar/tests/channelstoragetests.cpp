/* Channel Storage Unit Tests
 *
 * Tests the schema v20 channel storage: the channel_defaults table, the sparse
 * profile_channel_overrides table, the channels view over them, channels_legacy, and
 * Profile::loadChannels() / saveChannels() working through the overrides.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "channelstoragetests.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "SleepLib/appsettings.h"
#include "SleepLib/channel_store.h"
#include "SleepLib/common.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "database/channel_options_repository.h"
#include "database/channel_repository.h"
#include "database/database_manager.h"
#include "database/database_schema.h"
#include "database/profile_repository.h"

namespace {

const QString kProfileName = QStringLiteral("ChannelStorageUnitTest");
const QString kOtherProfileName = QStringLiteral("ChannelStorageUnitTestB");

} // namespace

void ChannelStorageTests::initTestCase()
{
    // note: need dummy app for QtSql even though headless (each suite owns its own)
    static int argc = 1;
    static char appName[] = "test";
    static char *argv[] = { appName, nullptr };
    m_app = new QCoreApplication(argc, argv);

    if (DatabaseManager::instance().isOpen()) {
        DatabaseManager::instance().close();
    }

    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-channelstoragetests-XXXXXX"));
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
    m_otherDir = m_tempDir->path() + QStringLiteral("/Profiles/") + kOtherProfileName;
    p_profile = Profiles::Get(kProfileName);
    if (p_profile == nullptr) {
        p_profile = Profiles::Create(kProfileName, &profileDir);
    }
    QVERIFY(p_profile != nullptr);
    m_profile = p_profile;

    m_profileId = ProfileRepository().findByUsername(kProfileName).id;
    QVERIFY(m_profileId > 0);
}

void ChannelStorageTests::init()
{
    // Every test starts from the code defaults and with no stored overrides.
    schema::resetChannelsToDefaults();
    QSqlQuery query(db());
    QVERIFY2(query.exec(QStringLiteral("DELETE FROM profile_channel_overrides")),
             qPrintable(query.lastError().text()));
}

QSqlDatabase &ChannelStorageTests::db()
{
    return m_db;
}

void ChannelStorageTests::exec(const QString &sql)
{
    QSqlQuery query(db());
    QVERIFY2(query.exec(sql), qPrintable(sql + ": " + query.lastError().text()));
}

QVariant ChannelStorageTests::scalar(const QString &sql)
{
    QSqlQuery query(db());
    if (!query.exec(sql)) {
        qWarning() << "scalar() failed:" << sql << query.lastError().text();
        return QVariant();
    }
    return query.next() ? query.value(0) : QVariant();
}

void ChannelStorageTests::makeV19Shape()
{
    exec("DROP VIEW channels");
    exec("ALTER TABLE channels_legacy RENAME TO channels");
    exec("DROP INDEX IF EXISTS idx_channels_legacy_profile");
    exec("DROP TABLE channel_defaults");
    exec("DROP TABLE profile_channel_overrides");
    exec("CREATE INDEX IF NOT EXISTS idx_channels_profile ON channels(profile_id)");
    exec("CREATE INDEX IF NOT EXISTS idx_channels_code ON channels(channel_code)");
    exec("CREATE INDEX IF NOT EXISTS idx_channels_lookup ON channels(profile_id, channel_id)");
}

int ChannelStorageTests::overrideCount()
{
    return scalar(QString("SELECT COUNT(*) FROM profile_channel_overrides WHERE profile_id = %1")
                  .arg(m_profileId)).toInt();
}

// ---------------------------------------------------------------------------
// Schema and repository
// ---------------------------------------------------------------------------

// A new database has the view and the three v20 tables.
void ChannelStorageTests::testFreshSchemaIsV20()
{
    QCOMPARE(scalar("SELECT type FROM sqlite_master WHERE name='channels'").toString(), QString("view"));
    for (const char *t : { "channel_defaults", "profile_channel_overrides", "channels_legacy" })
        QCOMPARE(scalar(QString("SELECT type FROM sqlite_master WHERE name='%1'").arg(t)).toString(), QString("table"));
}

// The v19 -> v20 migration keeps the old per-profile rows, unconverted, in channels_legacy.
void ChannelStorageTests::testMigrationParksOldRows()
{
    // Put the database back into its v19 shape, with its three indexes and one old row,
    // and upgrade it through the real entry point.
    makeV19Shape();
    exec(QString("INSERT INTO channels (profile_id, channel_id, channel_code, label) VALUES (%1, 60001, 'TestChan', 'MyOA')").arg(m_profileId));
    QVERIFY(DatabaseSchema::setSchemaVersion(db(), 19));
    QVERIFY(DatabaseSchema::upgradeSchema(db(), 19, {}));
    QCOMPARE(DatabaseSchema::getSchemaVersion(db()), 20);
    QCOMPARE(scalar("SELECT label FROM channels_legacy WHERE channel_id=60001").toString(), QString("MyOA"));
    QCOMPARE(scalar("SELECT type FROM sqlite_master WHERE name='channels'").toString(), QString("view"));
    for (const char *index : { "idx_channels_profile", "idx_channels_code", "idx_channels_lookup" }) {
        QCOMPARE(scalar(QString("SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name='%1'").arg(index)).toInt(), 0);
    }
    QCOMPARE(scalar("SELECT COUNT(*) FROM profile_channel_overrides").toInt(), 0);
    exec("DELETE FROM channels_legacy");
}

// The rename does not re-point a user's own view at channels_legacy: it keeps reading
// "channels", which is now the view (review M2).
void ChannelStorageTests::testMigrationLeavesUserViewsAlone()
{
    makeV19Shape();
    exec("CREATE VIEW user_view AS SELECT channel_code FROM channels");
    // Start from OFF so the restore is tested. Its default depends on the SQLite build:
    // Apple's system library has it ON, so Homebrew Qt on macOS would otherwise start at 1.
    exec("PRAGMA legacy_alter_table=OFF");
    const bool migrated = DatabaseSchema::migrateV19ToV20(db());
    const QString sql = scalar("SELECT sql FROM sqlite_master WHERE type='view' AND name='user_view'").toString();
    QSqlQuery select(db());
    const bool selectWorks = select.exec("SELECT COUNT(*) FROM user_view");
    select.finish();
    const int legacyAlter = scalar("PRAGMA legacy_alter_table").toInt();
    exec("DROP VIEW user_view");

    QVERIFY(migrated);
    QVERIFY2(sql.contains("FROM channels") && !sql.contains("channels_legacy"), qPrintable(sql));
    QVERIFY2(selectWorks, qPrintable(select.lastError().text()));
    QCOMPARE(legacyAlter, 0);          // the previous setting is back
}

// The channels view returns the override where one is set and the default elsewhere.
void ChannelStorageTests::testViewPrefersOverride()
{
    ChannelRepository repo;
    ChannelDefaultRow d; d.channelId = 60001; d.channelCode = "TestChan"; d.fullname = "Def Name"; d.label = "Def";
    d.type = schema::FLAG;
    QVERIFY(repo.syncDefaults({ d }));
    const QString where = QString(" FROM channels WHERE profile_id=%1 AND channel_id=60001").arg(m_profileId);
    QCOMPARE(scalar("SELECT label" + where).toString(), QString("Def"));
    QCOMPARE(scalar("SELECT is_customized" + where).toInt(), 0);
    QCOMPARE(scalar("SELECT type" + where).toInt(), int(schema::FLAG));
    ChannelOverrideRow o; o.channelId = 60001; o.channelCode = "TestChan"; o.values.label = QString("Mine");
    o.values.type = schema::SPAN;
    QVERIFY(repo.replaceOverrides(m_profileId, { o }, { 60001 }, { "TestChan" }));
    QCOMPARE(scalar("SELECT label" + where).toString(), QString("Mine"));
    QCOMPARE(scalar("SELECT type" + where).toInt(), int(schema::SPAN));
    QCOMPARE(scalar("SELECT fullname" + where).toString(), QString("Def Name"));
    QCOMPARE(scalar("SELECT is_customized" + where).toInt(), 1);
}

// Override values come back as stored; an empty override set is not stored at all.
void ChannelStorageTests::testOverridesRoundTrip()
{
    ChannelRepository repo;
    ChannelOverrideRow a; a.channelId = 60001; a.channelCode = "TestChan";
    a.values.label = QString(""); a.values.color = QColor("#123456"); a.values.enabled = false;
    a.values.type = schema::MINOR_FLAG;
    ChannelOverrideRow empty; empty.channelId = 60002; empty.channelCode = "Other";
    QVERIFY(repo.replaceOverrides(m_profileId, { a, empty }, { 60001, 60002 }, { "TestChan", "Other" }));
    const auto rows = repo.findOverrides(m_profileId);
    QCOMPARE(rows.size(), 1);                                  // the empty row is not stored
    QCOMPARE(rows[0].values.label.value(), QString(""));       // empty string, not NULL
    QCOMPARE(rows[0].values.color.value().name(), QString("#123456"));
    QCOMPARE(rows[0].values.enabled.value(), false);
    QCOMPARE(rows[0].values.type.value(), schema::MINOR_FLAG);
    QVERIFY(!rows[0].values.fullname.has_value());
}

// Rows for channels the running build does not know are left in place.
void ChannelStorageTests::testReplaceKeepsUnknownChannelRows()
{
    ChannelRepository repo;
    ChannelOverrideRow gone; gone.channelId = 61000; gone.channelCode = "NotInThisBuild"; gone.values.label = QString("Keep");
    QVERIFY(repo.replaceOverrides(m_profileId, { gone }, { 61000 }, { "NotInThisBuild" }));
    QVERIFY(repo.replaceOverrides(m_profileId, {}, { 60001 }, { "TestChan" }));   // 61000 is unknown now
    const auto rows = repo.findOverrides(m_profileId);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].channelId, ChannelID(61000));
}

// syncDefaults() rewrites changed rows and removes channels no longer in the build.
void ChannelStorageTests::testSyncDefaultsUpdatesAndDeletes()
{
    ChannelRepository repo;
    ChannelDefaultRow a; a.channelId = 60001; a.channelCode = "TestChan"; a.fullname = "English";
    ChannelDefaultRow b; b.channelId = 60002; b.channelCode = "Other";
    QVERIFY(repo.syncDefaults({ a, b }));
    a.fullname = "Deutsch";
    QVERIFY(repo.syncDefaults({ a }));
    QCOMPARE(scalar("SELECT fullname FROM channel_defaults WHERE channel_id=60001").toString(), QString("Deutsch"));
    QCOMPARE(scalar("SELECT COUNT(*) FROM channel_defaults WHERE channel_id=60002").toInt(), 0);
}

// Legacy rows can be written, listed per profile, read back and deleted.
void ChannelStorageTests::testLegacyRowsAndProfileDelete()
{
    ChannelRepository repo;
    ChannelData row; row.channelId = 60001; row.channelCode = "TestChan"; row.label = "Old";
    QVERIFY(repo.insertLegacy(m_profileId, { row }));
    QCOMPARE(repo.profilesWithLegacyRows(), QList<qint64>{ m_profileId });
    const auto rows = repo.findLegacy(m_profileId);
    QCOMPARE(rows.size(), 1);
    QVERIFY(repo.deleteLegacy({ rows[0].id }));
    QVERIFY(repo.profilesWithLegacyRows().isEmpty());
}

// A row still parked for the same channel does not make a later insert fail: it is
// replaced (review M3).
void ChannelStorageTests::testInsertLegacyReplacesParkedRow()
{
    ChannelRepository repo;
    ChannelData first; first.channelId = 60001; first.channelCode = "TestChan"; first.label = "First";
    ChannelData second = first; second.label = "Second";
    QVERIFY(repo.insertLegacy(m_profileId, { first }));
    const bool replaced = repo.insertLegacy(m_profileId, { second });
    const auto rows = repo.findLegacy(m_profileId);
    exec(QString("DELETE FROM channels_legacy WHERE profile_id = %1").arg(m_profileId));
    QVERIFY(replaced);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].label, QString("Second"));
}

// ---------------------------------------------------------------------------
// Profile load and save
// ---------------------------------------------------------------------------

// A profile with nothing changed stores no override rows.
void ChannelStorageTests::testUnchangedProfileStoresNothing()
{
    schema::resetChannelsToDefaults();
    p_profile->saveChannels();
    QCOMPARE(overrideCount(), 0);
}

// A rename is stored as one override and is the only field that comes back changed.
void ChannelStorageTests::testRenameSurvivesReload()
{
    schema::Channel &c = schema::channel[CPAP_Obstructive];
    const QString defName = c.defaults().fullname;
    c.setLabel("MyOA");
    p_profile->saveChannels();
    QCOMPARE(overrideCount(), 1);
    schema::resetChannelsToDefaults();
    p_profile->loadChannels();
    QCOMPARE(c.label(), QString("MyOA"));
    QCOMPARE(c.fullname(), defName);
}

// An empty label is a real override, not "no override".
void ChannelStorageTests::testEmptyLabelSurvivesReload()
{
    schema::Channel &c = schema::channel[CPAP_Obstructive];
    c.setLabel(QString(""));
    p_profile->saveChannels();
    schema::resetChannelsToDefaults();
    p_profile->loadChannels();
    QCOMPARE(c.label(), QString(""));
}

// An override set back to the default value is removed on the next save.
void ChannelStorageTests::testOverrideDroppedWhenBackToDefault()
{
    schema::Channel &c = schema::channel[CPAP_Obstructive];
    c.setLabel("MyOA");            p_profile->saveChannels();
    c.setLabel(c.defaults().label); p_profile->saveChannels();
    QCOMPARE(overrideCount(), 0);
}

// Preference-driven thresholds and SETTING channels never produce override rows.
void ChannelStorageTests::testPreferenceThresholdAndSettingNotStored()
{
    schema::channel[CPAP_Leak].setUpperThreshold(30);
    schema::channel[CPAP_PSMin].setLabel("Mine");
    p_profile->saveChannels();
    QCOMPARE(overrideCount(), 0);
}

// A Flag Type changed in Preferences is stored as an override and comes back (#430).
void ChannelStorageTests::testFlagTypeSurvivesReload()
{
    schema::Channel &c = schema::channel[CPAP_Obstructive];
    QCOMPARE(c.defaults().type, schema::FLAG);
    c.setType(schema::SPAN);
    p_profile->saveChannels();
    QCOMPARE(overrideCount(), 1);
    schema::resetChannelsToDefaults();
    QCOMPARE(c.type(), schema::FLAG);
    p_profile->loadChannels();
    QCOMPARE(c.type(), schema::SPAN);

    // Set back to the code's type, the override goes away.
    c.setType(schema::FLAG);
    p_profile->saveChannels();
    QCOMPARE(overrideCount(), 0);
}

// A Flag Type left in the registry by another profile is put back to the code's type
// when a profile without that override is loaded (#430).
void ChannelStorageTests::testFlagTypeGoesBackWithoutOverride()
{
    schema::Channel &c = schema::channel[CPAP_Obstructive];
    c.setType(schema::MINOR_FLAG);
    p_profile->loadChannels();
    QCOMPARE(c.type(), schema::FLAG);
}

// Loading a second profile does not carry over the first profile's overrides.
void ChannelStorageTests::testSecondProfileDoesNotInheritOverrides()      // Review Focus 1
{
    schema::channel[CPAP_Obstructive].setLabel("MyOA");
    p_profile->saveChannels();
    Profile *other = Profiles::Create(kOtherProfileName, &m_otherDir);
    QVERIFY(other != nullptr);
    // Create() makes the new profile current and rebuilds the channel registry (deleting
    // the old Channel objects), so restore p_profile and look the channel up afresh.
    p_profile = m_profile;
    schema::Channel &c = schema::channel[CPAP_Obstructive];
    c.setLabel("MyOA");                       // the registry still holds the first profile's values
    other->loadChannels();
    QCOMPARE(c.label(), c.defaults().label);
    p_profile->loadChannels();
    QCOMPARE(c.label(), QString("MyOA"));
}

// An override stored under an id this build does not know is matched by its code.
void ChannelStorageTests::testLoadMatchesOverrideByCode()
{
    schema::Channel &oa = schema::channel[CPAP_Obstructive];
    ChannelOverrideRow moved; moved.channelId = 61003; moved.channelCode = oa.code(); moved.values.label = QString("Zz Moved Zz");
    QVERIFY(ChannelRepository().replaceOverrides(m_profileId, { moved }, { 61003 }, { oa.code() }));
    p_profile->loadChannels();
    QCOMPARE(oa.label(), QString("Zz Moved Zz"));
}

// After that load, a save stores the override once, under the registry's id: the row under
// the old id is deleted by its code.
void ChannelStorageTests::testSaveRewritesMovedOverrideUnderRegistryId()
{
    schema::Channel &oa = schema::channel[CPAP_Obstructive];
    ChannelOverrideRow moved; moved.channelId = 61003; moved.channelCode = oa.code(); moved.values.label = QString("Zz Moved Zz");
    QVERIFY(ChannelRepository().replaceOverrides(m_profileId, { moved }, { 61003 }, { oa.code() }));
    p_profile->loadChannels();
    p_profile->saveChannels();
    const auto rows = ChannelRepository().findOverrides(m_profileId);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].channelId, ChannelID(CPAP_Obstructive));
    QCOMPARE(rows[0].values.label.value(), QString("Zz Moved Zz"));
}

// An override stored under an id that now belongs to another channel goes to the channel
// its code names, as in the conversion (review M1).
void ChannelStorageTests::testLoadPrefersCodeOverReassignedId()
{
    schema::Channel &oa = schema::channel[CPAP_Obstructive];
    schema::Channel &hy = schema::channel[CPAP_Hypopnea];
    ChannelOverrideRow row; row.channelId = hy.id(); row.channelCode = oa.code(); row.values.label = QString("Zz Reassigned Zz");
    QVERIFY(ChannelRepository().replaceOverrides(m_profileId, { row }, { hy.id() }, { oa.code() }));
    p_profile->loadChannels();
    QCOMPARE(oa.label(), QString("Zz Reassigned Zz"));
    QCOMPARE(hy.label(), hy.defaults().label);
}

// syncDefaults() writes the code defaults, never a profile's live values.
void ChannelStorageTests::testScanWritesPristineDefaults()                // Review Focus 2
{
    schema::Channel &c = schema::channel[CPAP_Obstructive];
    const schema::ChanType codeType = c.type();
    c.setLabel("MyOA");                       // an open profile's override is in memory
    c.setType(schema::SPAN);                  // a Flag Type changed in Preferences
    const bool synced = ChannelStore::syncDefaults();
    c.setType(codeType);
    QVERIFY(synced);
    QCOMPARE(scalar(QString("SELECT label FROM channel_defaults WHERE channel_id=%1").arg(CPAP_Obstructive)).toString(),
             c.defaults().label);
    QCOMPARE(scalar(QString("SELECT type FROM channel_defaults WHERE channel_id=%1").arg(CPAP_Obstructive)).toInt(),
             int(codeType));
    QCOMPARE(scalar("SELECT COUNT(*) FROM channel_defaults").toInt(), int(schema::channel.channels.size()));
}

// Stored option labels fill only channels that define none in code; code labels win (#333).
void ChannelStorageTests::testStoredOptionsFillOnlyChannelsWithoutCodeOptions()
{
    schema::Channel &plain = schema::channel[CPAP_Obstructive];
    schema::Channel &mode = schema::channel[CPAP_Mode];
    QVERIFY(plain.m_options.isEmpty());
    QVERIFY(!mode.m_options.isEmpty());
    const int key = mode.m_options.constBegin().key();
    const QString codeLabel = mode.m_options.value(key);

    ChannelOptionsRepository optionsRepo;
    QVERIFY(optionsRepo.saveBatch(CPAP_Obstructive, { { 1, QStringLiteral("StoredOption") } }));
    QVERIFY(optionsRepo.saveBatch(CPAP_Mode, { { key, codeLabel + QStringLiteral(" (stored)") } }));

    p_profile->loadChannels();
    const QString plainOption = plain.m_options.value(1);
    const QString modeLabel = mode.m_options.value(key);

    // Leave nothing behind for other tests.
    plain.m_options.clear();
    QVERIFY(optionsRepo.deleteByChannel(CPAP_Obstructive));
    QVERIFY(optionsRepo.deleteByChannel(CPAP_Mode));

    QCOMPARE(plainOption, QStringLiteral("StoredOption"));
    QCOMPARE(modeLabel, codeLabel);
}

void ChannelStorageTests::cleanupTestCase()
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
