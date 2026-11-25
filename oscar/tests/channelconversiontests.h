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

#ifndef CHANNELCONVERSIONTESTS_H
#define CHANNELCONVERSIONTESTS_H

#include "AutoTest.h"

#include <QSqlDatabase>

#include "database/channel_repository.h"

class Profile;
class QCoreApplication;
class QTemporaryDir;

//! \brief Tests for the list of shipped channel names and the legacy channel conversion,
//! against a temporary database.
class ChannelConversionTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    // The list of shipped names (Task 4)
    void testShippedNamesLookup();

    // Conversion of channels_legacy rows and of channels.dat (Task 5)
    void testConvertsEveryProfile();
    void testMatchesByCodeAndKeepsUnknownRows();
    void testSecondRunChangesNothing();
    void testScanConverts();
    void testChannelsDatGoesThroughClassifier();
    void testStaleChannelsDatIsNotReimported();
    void testLoadImportsChannelsDatOnceAndMarks();
    void testFailedStoreLeavesProfileUnmarked();
    void testFailedConversionLeavesProfileUnmarked();

    void cleanupTestCase();

private:
    //! \brief A channels_legacy row holding \p chan's code defaults, as an older build stored it.
    ChannelData legacyOf(schema::Channel &chan);
    //! \brief Runs \p sql on the test database and fails the test if it does not succeed.
    void exec(const QString &sql);
    //! \brief Whether the open test profile holds ChannelsDatImported = true in memory.
    bool isMarked();

    QCoreApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QString m_previousAppData;
    Profile *m_profile = nullptr;      //!< The test profile
    qint64 m_profileId = 0;            //!< Its database id
    qint64 m_otherProfileId = 0;       //!< A second profile that the tests never open
    QSqlDatabase m_db;                 //!< The application's connection, set in initTestCase()
};

DECLARE_TEST(ChannelConversionTests)

#endif // CHANNELCONVERSIONTESTS_H
