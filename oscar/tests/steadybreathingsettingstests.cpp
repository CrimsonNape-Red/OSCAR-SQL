/* Steady Breathing Settings Unit Tests
 *
 * Tests that the Steady Breathing settings belong to each profile, and that the old
 * app-wide values are copied into a profile once, the first time it loads.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "steadybreathingsettingstests.h"

#include <QTemporaryDir>

#include "SleepLib/appsettings.h"
#include "SleepLib/common.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "database/database_manager.h"

void SteadyBreathingSettingsTests::initTestCase()
{
#if !defined(STEADY_BREATHING)
    // Nothing to set up; each test skips itself. (A QSKIP here would silently skip every
    // test class that runs after this one, see AutoTest.h.)
#else
    // note: need dummy app for QtSql even though headless (each suite owns its own)
    static int argc = 1;
    static char appName[] = "test";
    static char *argv[] = { appName, nullptr };
    m_app = new QCoreApplication(argc, argv);

    if (DatabaseManager::instance().isOpen()) {
        DatabaseManager::instance().close();
    }

    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-steadybreathingtests-XXXXXX"));
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

    // The value an older OSCAR left in the app-wide settings.
    (*p_pref)[STR_AS_SteadyBreathing] = (int)SB_ACTIVE;
#if defined(STEADY_BREATHING_ENHANCED_TESTING)
    (*p_pref)[STR_AS_SteadyBreathingThreshold] = 2.5;
    (*p_pref)[STR_AS_SteadyBreathingDuration] = 30;
#endif

    const QString dirA = m_tempDir->path() + QStringLiteral("/Profiles/SteadyBreathingA");
    const QString dirB = m_tempDir->path() + QStringLiteral("/Profiles/SteadyBreathingB");
    m_profileA = Profiles::Create(QStringLiteral("SteadyBreathingA"), &dirA);
    m_profileB = Profiles::Create(QStringLiteral("SteadyBreathingB"), &dirB);
    QVERIFY(m_profileA != nullptr);
    QVERIFY(m_profileB != nullptr);
#endif
}

void SteadyBreathingSettingsTests::testAppWideValueCopiedOnce()
{
#if defined(STEADY_BREATHING)
    // A new profile starts with the default until it is next loaded.
    QCOMPARE(m_profileA->cpap->steadyBreathing(), SB_OFF);
    QVERIFY(!(*m_profileA)[STR_PREF_SteadyBreathingPerProfile].toBool());

    QVERIFY(m_profileA->loadExtendedDataFromDatabase());
    QCOMPARE(m_profileA->cpap->steadyBreathing(), SB_ACTIVE);
#if defined(STEADY_BREATHING_ENHANCED_TESTING)
    QCOMPARE(m_profileA->cpap->steadyBreathingThreshold(), 2.5);
    QCOMPARE(m_profileA->cpap->steadyBreathingDuration(), 30);
#endif
    QVERIFY((*m_profileA)[STR_PREF_SteadyBreathingPerProfile].toBool());

    // Once copied, the profile's own value is kept even though the app-wide one is still there.
    m_profileA->cpap->setSteadyBreathing(SB_OFF);
    QVERIFY(m_profileA->saveProfilePreferencesToDatabase());
    QVERIFY(m_profileA->loadExtendedDataFromDatabase());
    QCOMPARE(m_profileA->cpap->steadyBreathing(), SB_OFF);
#else
    QSKIP("STEADY_BREATHING is not defined in this build");
#endif
}

void SteadyBreathingSettingsTests::testProfilesAreIndependent()
{
#if defined(STEADY_BREATHING)
    QVERIFY(m_profileB->loadExtendedDataFromDatabase());
    QCOMPARE(m_profileB->cpap->steadyBreathing(), SB_ACTIVE);
    QVERIFY(m_profileB->saveProfilePreferencesToDatabase());

    // Profile A was turned off above; that must not reach profile B.
    QVERIFY(m_profileA->loadExtendedDataFromDatabase());
    QVERIFY(m_profileB->loadExtendedDataFromDatabase());
    QCOMPARE(m_profileA->cpap->steadyBreathing(), SB_OFF);
    QCOMPARE(m_profileB->cpap->steadyBreathing(), SB_ACTIVE);
#else
    QSKIP("STEADY_BREATHING is not defined in this build");
#endif
}

void SteadyBreathingSettingsTests::testClinicalModeIsInactive()
{
#if defined(STEADY_BREATHING)
    // A profile with no CPAP data is opened in Clinical Mode, then gets the old app-wide
    // SB_ACTIVE copied in. It must not report SB_ACTIVE, or import would run detection (#340).
    const QString dirC = m_tempDir->path() + QStringLiteral("/Profiles/SteadyBreathingC");
    m_profileC = Profiles::Create(QStringLiteral("SteadyBreathingC"), &dirC);
    QVERIFY(m_profileC != nullptr);
    m_profileC->cpap->setClinicalMode(true);
    QVERIFY(m_profileC->saveProfilePreferencesToDatabase());
    QVERIFY(m_profileC->loadExtendedDataFromDatabase());
    QVERIFY((*m_profileC)[STR_PREF_SteadyBreathingPerProfile].toBool());
    QCOMPARE(m_profileC->cpap->steadyBreathing(), SB_INACTIVE);

    // The user's choice is kept, and takes effect once the profile is in Permissive Mode.
    m_profileC->cpap->setClinicalMode(false);
    QCOMPARE(m_profileC->cpap->steadyBreathing(), SB_ACTIVE);

    // A stored SB_INACTIVE (written by older builds) still means "turned on".
    m_profileC->cpap->setSteadyBreathing(SB_INACTIVE);
    QCOMPARE(m_profileC->cpap->steadyBreathing(), SB_ACTIVE);
#else
    QSKIP("STEADY_BREATHING is not defined in this build");
#endif
}

void SteadyBreathingSettingsTests::cleanupTestCase()
{
    Profiles::profiles.clear();

    delete m_profileA;
    m_profileA = nullptr;
    delete m_profileB;
    m_profileB = nullptr;
    delete m_profileC;
    m_profileC = nullptr;
    p_profile = nullptr;
    delete AppSetting;
    AppSetting = nullptr;
    delete p_pref;
    p_pref = nullptr;

    DatabaseManager::instance().close();
    if (m_tempDir) SetAppData(m_previousAppData);

    delete m_tempDir;
    m_tempDir = nullptr;
    delete m_app;
    m_app = nullptr;
}
