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

#ifndef CHANNELBACKUPTESTS_H
#define CHANNELBACKUPTESTS_H

#include "AutoTest.h"

#include <QSqlDatabase>

class Profile;
class QCoreApplication;
class QTemporaryDir;

//! \brief Tests for backing up and restoring channel overrides, against a temporary database.
class ChannelBackupTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void testOverridesSurviveBackupAndRestore();
    void testOldChannelsFileIsParkedAndConverted();
    void testReplaceModeDropsOldOverrides();

    void cleanupTestCase();

private:
    //! \brief Runs \p sql on the test database and fails the test if it does not succeed.
    void exec(const QString &sql);
    //! \brief Backs up the test profile; returns the package path, or an empty string on failure.
    QString makeBackup(const QString &name);

    QCoreApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QString m_previousAppData;
    Profile *m_profile = nullptr;      //!< The test profile
    qint64 m_profileId = 0;            //!< Its database id
    QSqlDatabase m_db;                 //!< The application's connection, set in initTestCase()
};

DECLARE_TEST(ChannelBackupTests)

#endif // CHANNELBACKUPTESTS_H
