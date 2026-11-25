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

#include <QTemporaryDir>

#include "SleepLib/appsettings.h"
#include "SleepLib/common.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "SleepLib/shipped_channel_names.h"
#include "database/database_manager.h"
#include "database/profile_repository.h"

namespace {

const QString kProfileName = QStringLiteral("ChannelConversionUnitTest");

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
}

void ChannelConversionTests::init()
{
    // Every test starts from the code defaults.
    schema::resetChannelsToDefaults();
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
