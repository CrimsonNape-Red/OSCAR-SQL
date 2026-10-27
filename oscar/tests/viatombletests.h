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
    void testDropsLateReplyAfterGivingUp();
    void testDeviceRefusesFile();
    void testLinkLostRemovesPartialFile();
    void testCancelStopsAndCleansUp();
    void testInfo();
    void testInfoTimeout();
    void testSetTime();
    void testLateInfoReplyIsNotTakenForSetTime();

    // O2Ring S family
    void testAes128EcbPkcs7();
    void testO2RingSFrames();
    void testO2RingSHandshake();
    void testO2RingSPayloads();
    void testO2RingSSession();
    void testO2RingSWrongKey();

#ifdef OSCAR_HAVE_BLUETOOTH
    void testDeviceFamilyFromName();
#endif

  private:
    QCoreApplication *m_app = nullptr;
};

DECLARE_TEST(ViatomBleTests)

#endif // VIATOMBLETESTS_H
