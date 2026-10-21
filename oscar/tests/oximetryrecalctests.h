/* Oximetry Recalculation Unit Tests
 *
 * Tests that Profile::recalculateOximetryEvents() recalculates and saves the SpO2 drop and
 * pulse change flags of stored sessions.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef OXIMETRYRECALCTESTS_H
#define OXIMETRYRECALCTESTS_H

#include "AutoTest.h"

class Machine;
class QCoreApplication;
class QTemporaryDir;

class OximetryRecalcTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testRecalculateUsesCurrentSettings();
    void testRecalculateKeepsSplitRecordingWhole();
    void testRecalculateSkipsUnloadableSessions();
    void cleanupTestCase();

private:
    QCoreApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QString m_previousAppData;
    Machine *m_machine = nullptr;
};

DECLARE_TEST(OximetryRecalcTests)

#endif // OXIMETRYRECALCTESTS_H
