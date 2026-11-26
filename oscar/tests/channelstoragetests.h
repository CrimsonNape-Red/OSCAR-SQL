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

#ifndef CHANNELSTORAGETESTS_H
#define CHANNELSTORAGETESTS_H

#include "AutoTest.h"

#include <QSqlDatabase>
#include <QVariant>

class Profile;
class QCoreApplication;
class QTemporaryDir;

//! \brief Tests for ChannelRepository, the v19 to v20 migration and ChannelStore against a
//! temporary database.
class ChannelStorageTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    // Schema and repository (Task 2)
    void testFreshSchemaIsV20();
    void testMigrationParksOldRows();
    void testMigrationLeavesUserViewsAlone();
    void testViewPrefersOverride();
    void testOverridesRoundTrip();
    void testReplaceKeepsUnknownChannelRows();
    void testSyncDefaultsUpdatesAndDeletes();
    void testLegacyRowsAndProfileDelete();
    void testInsertLegacyReplacesParkedRow();

    // Profile load and save (Task 3)
    void testUnchangedProfileStoresNothing();
    void testRenameSurvivesReload();
    void testEmptyLabelSurvivesReload();
    void testOverrideDroppedWhenBackToDefault();
    void testPreferenceThresholdAndSettingNotStored();
    void testSecondProfileDoesNotInheritOverrides();
    void testLoadMatchesOverrideByCode();
    void testSaveRewritesMovedOverrideUnderRegistryId();
    void testLoadPrefersCodeOverReassignedId();
    void testScanWritesPristineDefaults();
    void testStoredOptionsFillOnlyChannelsWithoutCodeOptions();

    void cleanupTestCase();

private:
    //! \brief The application's database connection (a reference: migrateV19ToV20() takes one).
    QSqlDatabase &db();

    //! \brief Runs \p sql and fails the current test if it does not succeed.
    void exec(const QString &sql);

    //! \brief The first column of the first row of \p sql, or an invalid QVariant.
    QVariant scalar(const QString &sql);

    //! \brief Puts the database back into its v19 channel shape: the channels table (the
    //! renamed channels_legacy) with its three v19 indexes, and no v20 tables or view.
    void makeV19Shape();

    //! \brief Number of override rows stored for the main test profile.
    int overrideCount();

    QCoreApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QString m_previousAppData;
    Profile *m_profile = nullptr;      //!< The main test profile
    qint64 m_profileId = 0;            //!< Its database id
    QString m_otherDir;                //!< Folder for the second profile
    QSqlDatabase m_db;                 //!< The application's connection, set in initTestCase()
};

DECLARE_TEST(ChannelStorageTests)

#endif // CHANNELSTORAGETESTS_H
