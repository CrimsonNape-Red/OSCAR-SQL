/* Viatom BLE Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef VIATOMBLETESTS_H
#define VIATOMBLETESTS_H

#include "AutoTest.h"

class ViatomBleTests : public QObject
{
    Q_OBJECT

  private slots:
    void initTestCase();
    void cleanupTestCase();

    void testCrc8MatchesLookupTable();
    void testBuildRequest();
    void testSplitterHandlesFragmentsAndJunk();
    void testDeviceInfo();
    void testFileSizeFromReadStart();
    void testSetTimePayload();

    void testDownloadAllFiles();
    void testSkipsExistingFiles();
    void testOverwritesExistingFiles();
    void testRetriesAfterLostReply();
    void testDropsLateReplyAfterRetry_data();
    void testDropsLateReplyAfterRetry();
    void testDeviceRefusesFile();
    void testLinkLostRemovesPartialFile();
    void testCancelStopsAndCleansUp();
    void testInfo();
    void testInfoTimeout();
    void testSetTime();

#ifdef OSCAR_HAVE_BLUETOOTH
    void testDeviceFamilyFromName();
#endif

  private:
    QCoreApplication *m_app = nullptr;
};

DECLARE_TEST(ViatomBleTests)

#endif // VIATOMBLETESTS_H
