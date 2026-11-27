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

#ifndef PROFILERENAMETESTS_H
#define PROFILERENAMETESTS_H

#include "AutoTest.h"

class Profile;
class QCoreApplication;
class QTemporaryDir;

//! \brief Tests for Profiles::Rename() against a temporary data folder and database.
class ProfileRenameTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void testRenameInPlace();
    void testSaveAfterRenameAddsNoProfileRow();
    void testLastUsedProfileSettingFollows();
    void testNameInUseChangesNothing();
    void testMissingFolderChangesNothing();

    void cleanupTestCase();

private:
    //! \brief Creates a profile with its data folder under the temporary Profiles folder.
    Profile *createProfile(const QString &name);

    //! \brief Full path of the data folder a profile called \p name would have.
    QString folderOf(const QString &name) const;

    //! \brief Number of rows in the profiles table, or -1 if the query fails.
    int profileRowCount() const;

    QCoreApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QString m_previousAppData;
};

DECLARE_TEST(ProfileRenameTests)

#endif // PROFILERENAMETESTS_H
