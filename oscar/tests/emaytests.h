/* EMAY oximeter Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef EMAYTESTS_H
#define EMAYTESTS_H

#include "AutoTest.h"

class EmayTests : public QObject
{
    Q_OBJECT

  private slots:
    void initTestCase();
    void cleanupTestCase();

    void testFrame();
    void testSplitterHandlesFragmentsAndJunk();
    void testBcdTime();
    void testDeviceInfo();
    void testSpo2FileReadsBack();
    void testInfoAndList();
    void testNothingStored();
    void testDownload();
    void testSkipsExisting();
    void testResendsLostPackage();
    void testNeverDeletes();
    void testSetTime();
#ifdef OSCAR_HAVE_BLUETOOTH
    void testDeviceFamily();
#endif

  private:
    QCoreApplication *m_app = nullptr;
};

DECLARE_TEST(EmayTests)

#endif // EMAYTESTS_H
