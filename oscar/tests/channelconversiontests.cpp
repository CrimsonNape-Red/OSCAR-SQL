/* Channel Conversion Unit Tests
 *
 * Tests the conversion of the pre-v20 per-profile channel copies into overrides, and the
 * list of channel names OSCAR has shipped that the conversion uses to recognise old defaults.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "channelconversiontests.h"

#include <QFile>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "SleepLib/appsettings.h"
#include "SleepLib/channel_store.h"
#include "SleepLib/common.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "SleepLib/shipped_channel_names.h"
#include "database/database_manager.h"
#include "database/profile_repository.h"

namespace {

const QString kProfileName = QStringLiteral("ChannelConversionUnitTest");
const QString kOtherProfileName = QStringLiteral("ChannelConversionUnitTestB");

//! Stands in for the list of shipped names: exactly "Old Shipped" counts as shipped.
const ChannelOverrides::ShippedLookup kFake = [](const QString &s) { return s == QStringLiteral("Old Shipped"); };

//! A Simplified Chinese translation of "Flow Limit." that OSCAR shipped before it was
//! corrected (GitLab #417); the current translation is different.
const QString kRetiredTranslation = QString::fromUtf16(u"气流限制.");

} // namespace

void ChannelConversionTests::initTestCase()
{
    // note: need dummy app for QtSql even though headless (each suite owns its own)
    static int argc = 1;
    static char appName[] = "test";
    static char *argv[] = { appName, nullptr };
    m_app = new QCoreApplication(argc, argv);

    if (DatabaseManager::instance().isOpen()) {
        DatabaseManager::instance().close();
    }

    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-channelconversiontests-XXXXXX"));
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

    // A second profile that exists in the database (with its folder, so Profiles::Scan()
    // accepts it) but is never opened by any test.
    QVERIFY(QDir().mkpath(m_tempDir->path() + QStringLiteral("/Profiles/") + kOtherProfileName));
    ProfileData other;
    other.username = kOtherProfileName;
    other.dataFolder = QStringLiteral("%PROFDIR%/") + kOtherProfileName;
    m_otherProfileId = ProfileRepository().create(other);
    QVERIFY(m_otherProfileId > 0);
}

void ChannelConversionTests::init()
{
    // Every test starts from the code defaults, with no stored overrides or old rows for the
    // test profiles, no "channels.dat imported" mark and no channels.dat in the data folder.
    schema::resetChannelsToDefaults();
    for (qint64 id : { m_profileId, m_otherProfileId }) {
        exec(QString("DELETE FROM profile_channel_overrides WHERE profile_id = %1").arg(id));
        exec(QString("DELETE FROM channels_legacy WHERE profile_id = %1").arg(id));
        exec(QString("DELETE FROM profile_preferences WHERE profile_id = %1 AND key = '%2'")
                 .arg(id).arg(STR_PREF_ChannelsDatImported));
    }
    p_profile->Erase(STR_PREF_ChannelsDatImported);
    const QString dat = p_profile->Get("{DataFolder}/") + QStringLiteral("channels.dat");
    if (QFile::exists(dat)) {
        QVERIFY(QFile::remove(dat));
    }
}

ChannelData ChannelConversionTests::legacyOf(schema::Channel &chan)
{
    const schema::ChannelDefaults &d = chan.defaults();
    ChannelData row;
    row.channelId = chan.id();
    row.channelCode = chan.code();
    row.type = chan.type();
    row.enabled = d.enabled;
    row.defaultColor = d.color;
    row.fullname = d.fullname;
    row.label = d.label;
    row.description = d.description;
    row.lowerThreshold = d.lowerThreshold;
    row.lowerThresholdColor = d.lowerThresholdColor;
    row.upperThreshold = d.upperThreshold;
    row.upperThresholdColor = d.upperThresholdColor;
    row.showInOverview = d.showInOverview;
    return row;
}

void ChannelConversionTests::exec(const QString &sql)
{
    QSqlQuery query(m_db);
    QVERIFY2(query.exec(sql), qPrintable(sql + ": " + query.lastError().text()));
}

bool ChannelConversionTests::isMarked()
{
    return p_profile->contains(STR_PREF_ChannelsDatImported)
        && (*p_profile)[STR_PREF_ChannelsDatImported].toBool();
}

// ---------------------------------------------------------------------------
// The list of shipped names
// ---------------------------------------------------------------------------

// The list holds the current English name of a channel, a translation that was later
// replaced, and nothing that is not a channel name.
void ChannelConversionTests::testShippedNamesLookup()
{
    QVERIFY(ShippedChannelNames::contains(schema::channel[CPAP_Obstructive].defaults().fullname));
    QVERIFY(ShippedChannelNames::contains(kRetiredTranslation));
    QVERIFY(!ShippedChannelNames::contains(QStringLiteral("zz not a channel name zz")));
    QVERIFY(!ShippedChannelNames::contains(QString()));
}

// ---------------------------------------------------------------------------
// Conversion of channels_legacy rows and of channels.dat
// ---------------------------------------------------------------------------

// Every profile with old rows is converted, opened or not; only real changes on
// overridable fields become overrides, and every matched row is removed.
void ChannelConversionTests::testConvertsEveryProfile() {
    ChannelRepository repo;
    schema::Channel &oa = schema::channel[CPAP_Obstructive];
    schema::Channel &ps = schema::channel[CPAP_PSMin];
    ChannelData same = legacyOf(schema::channel[CPAP_Hypopnea]);
    ChannelData renamed = legacyOf(oa);  renamed.label = "MyOA"; renamed.defaultColor = QColor("#123456");
    ChannelData setting = legacyOf(ps);  setting.fullname = "Never Shipped"; setting.defaultColor = QColor("#123456");
    ChannelData stale = legacyOf(oa);    stale.fullname = "Old Shipped";
    QVERIFY(repo.insertLegacy(m_profileId, { same, renamed, setting }));
    QVERIFY(repo.insertLegacy(m_otherProfileId, { stale }));          // this profile is never opened

    QCOMPARE(ChannelStore::convertLegacyRows(kFake), 4);

    const auto a = repo.findOverrides(m_profileId);
    QCOMPARE(a.size(), 1);
    QCOMPARE(a[0].channelId, ChannelID(CPAP_Obstructive));
    QCOMPARE(a[0].values.label.value(), QString("MyOA"));
    QCOMPARE(a[0].values.color.value().name(), QString("#123456"));
    QVERIFY(!a[0].values.fullname.has_value());
    QVERIFY(repo.findOverrides(m_otherProfileId).isEmpty());
    QVERIFY(repo.profilesWithLegacyRows().isEmpty());
}

// A row whose channel id changed is matched by its code; a row for a channel unknown by id
// and by code stays in channels_legacy for a later run.
void ChannelConversionTests::testMatchesByCodeAndKeepsUnknownRows() {        // Review Focus 4
    ChannelRepository repo;
    schema::Channel &oa = schema::channel[CPAP_Obstructive];
    ChannelData moved = legacyOf(oa); moved.channelId = 61001; moved.label = "MyOA";   // id changed, code still matches
    ChannelData unknown; unknown.channelId = 61002; unknown.channelCode = "NotInThisBuild"; unknown.label = "Keep";
    QVERIFY(repo.insertLegacy(m_profileId, { moved, unknown }));
    QCOMPARE(ChannelStore::convertLegacyRows(kFake), 1);
    QCOMPARE(repo.findOverrides(m_profileId)[0].channelId, ChannelID(CPAP_Obstructive));
    const auto left = repo.findLegacy(m_profileId);
    QCOMPARE(left.size(), 1);
    QCOMPARE(left[0].channelCode, QString("NotInThisBuild"));
}

// Converted rows are gone, so a second run finds nothing to do.
void ChannelConversionTests::testSecondRunChangesNothing() {                 // Review Focus 5
    ChannelRepository repo;
    ChannelData renamed = legacyOf(schema::channel[CPAP_Obstructive]); renamed.label = "MyOA";
    QVERIFY(repo.insertLegacy(m_profileId, { renamed }));
    QCOMPARE(ChannelStore::convertLegacyRows(kFake), 1);
    QCOMPARE(ChannelStore::convertLegacyRows(kFake), 0);
    QCOMPARE(repo.findOverrides(m_profileId).size(), 1);
}

// Profiles::Scan() runs the conversion (with the real list of shipped names).
void ChannelConversionTests::testScanConverts() {
    ChannelRepository repo;
    ChannelData renamed = legacyOf(schema::channel[CPAP_Obstructive]); renamed.label = "Zz Never Shipped Zz";
    QVERIFY(repo.insertLegacy(m_profileId, { renamed }));
    Profiles::Scan();
    p_profile = Profiles::Get(kProfileName);
    QVERIFY(repo.profilesWithLegacyRows().isEmpty());
    QCOMPARE(repo.findOverrides(m_profileId)[0].values.label.value(), QString("Zz Never Shipped Zz"));
}

// An OSCAR 1.x channels.dat goes through the same classification: only the real change is
// stored, not a full copy of every channel.
void ChannelConversionTests::testChannelsDatGoesThroughClassifier() {
    schema::Channel &oa = schema::channel[CPAP_Obstructive];
    oa.setLabel("Zz Dat Label Zz");
    p_profile->saveChannelsToDat();               // the old writer, still compiled
    schema::resetChannelsToDefaults();
    QVERIFY(p_profile->migrateChannelsToDatabase(p_profile->Get("{DataFolder}")));
    const auto rows = ChannelRepository().findOverrides(m_profileId);
    QCOMPARE(rows.size(), 1);                     // every other channel was at its default
    QCOMPARE(rows[0].values.label.value(), QString("Zz Dat Label Zz"));
}

// A profile whose old rows were converted never imports a channels.dat left in its folder.
void ChannelConversionTests::testStaleChannelsDatIsNotReimported() {         // Review Focus 3
    schema::channel[CPAP_Obstructive].setLabel("Zz Stale Zz");
    p_profile->saveChannelsToDat();               // a file left behind by an older build
    schema::resetChannelsToDefaults();
    ChannelRepository repo;
    QVERIFY(repo.insertLegacy(m_profileId, { legacyOf(schema::channel[CPAP_Hypopnea]) }));
    ChannelStore::convertLegacyRows(kFake);       // marks the profile as imported
    Profiles::Scan();
    p_profile = Profiles::Get(kProfileName);
    p_profile->loadChannels();
    QCOMPARE(schema::channel[CPAP_Obstructive].label(), schema::channel[CPAP_Obstructive].defaults().label);
}

// An unmarked profile imports the channels.dat in its own folder when its channels are
// loaded, and is marked afterwards.
void ChannelConversionTests::testLoadImportsChannelsDatOnceAndMarks() {
    schema::channel[CPAP_Obstructive].setLabel("Zz Lazy Zz");
    p_profile->saveChannelsToDat();
    schema::resetChannelsToDefaults();
    QVERIFY(!isMarked());
    p_profile->loadChannels();
    QCOMPARE(schema::channel[CPAP_Obstructive].label(), QString("Zz Lazy Zz"));
    QVERIFY(isMarked());
}

// When the channels.dat rows cannot be stored, the profile stays unmarked so the import is
// tried again at the next open. A temporary trigger makes the insert fail on this
// connection only; it is dropped before any assertion.
void ChannelConversionTests::testFailedStoreLeavesProfileUnmarked() {
    schema::channel[CPAP_Obstructive].setLabel("Zz Fail Store Zz");
    p_profile->saveChannelsToDat();
    schema::resetChannelsToDefaults();
    exec("CREATE TEMP TRIGGER test_block_legacy BEFORE INSERT ON channels_legacy"
         " BEGIN SELECT RAISE(ABORT, 'blocked by test'); END");
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("insertLegacy failed for channel"));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Failed to store the channels.dat rows"));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("channels.dat import failed"));
    p_profile->loadChannels();
    exec("DROP TRIGGER temp.test_block_legacy");
    QVERIFY(!isMarked());
    QCOMPARE(schema::channel[CPAP_Obstructive].label(), schema::channel[CPAP_Obstructive].defaults().label);
    QVERIFY(ChannelRepository().findLegacy(m_profileId).isEmpty());
}

// When the stored rows cannot be converted, the profile stays unmarked and the rows stay in
// channels_legacy for the next start. A temporary trigger blocks the override insert.
void ChannelConversionTests::testFailedConversionLeavesProfileUnmarked() {
    schema::channel[CPAP_Obstructive].setLabel("Zz Fail Convert Zz");
    p_profile->saveChannelsToDat();
    schema::resetChannelsToDefaults();
    exec("CREATE TEMP TRIGGER test_block_overrides BEFORE INSERT ON profile_channel_overrides"
         " BEGIN SELECT RAISE(ABORT, 'blocked by test'); END");
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("inserting the override for channel"));
    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression("convertLegacyRows: converting the stored channels"));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Converting the channels.dat rows failed"));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("channels.dat import failed"));
    p_profile->loadChannels();
    exec("DROP TRIGGER temp.test_block_overrides");
    QVERIFY(!isMarked());
    QVERIFY(ChannelRepository().findOverrides(m_profileId).isEmpty());
    QCOMPARE(ChannelRepository().profilesWithLegacyRows(), QList<qint64>{ m_profileId });
}

void ChannelConversionTests::cleanupTestCase()
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
