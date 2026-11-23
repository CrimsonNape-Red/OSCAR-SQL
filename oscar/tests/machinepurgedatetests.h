/* Machine Purge Date Unit Tests
 *
 * Tests that a device's purge date reaches the database when it is set and when it is
 * cleared, so that it survives a restart of OSCAR (GitLab #415).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef MACHINEPURGEDATETESTS_H
#define MACHINEPURGEDATETESTS_H

#include "AutoTest.h"

class Machine;
class QCoreApplication;
class QTemporaryDir;

//! \brief Tests for Machine::setPurgeDate() and Machine::clearPurgeDate() against a
//! temporary database.
class MachinePurgeDateTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testSetPurgeDateIsStored();
    void testClearPurgeDateIsStored();
    void testOtherColumnsUntouched();
    void testNoDatabaseRowIsHarmless();
    void cleanupTestCase();

private:
    QCoreApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QString m_previousAppData;
    Machine *m_machine = nullptr;   //!< A device with a row in the temporary database
};

DECLARE_TEST(MachinePurgeDateTests)

#endif // MACHINEPURGEDATETESTS_H
