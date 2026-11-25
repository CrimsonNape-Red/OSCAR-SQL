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

    void cleanupTestCase();

private:
    QCoreApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QString m_previousAppData;
    Profile *m_profile = nullptr;      //!< The test profile
    qint64 m_profileId = 0;            //!< Its database id
    QSqlDatabase m_db;                 //!< The application's connection, set in initTestCase()
};

DECLARE_TEST(ChannelConversionTests)

#endif // CHANNELCONVERSIONTESTS_H
