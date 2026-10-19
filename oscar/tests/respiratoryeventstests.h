/* Respiratory Events Unit Tests
 *
 * Tests that re-saving a session replaces its respiratory_events rows rather than
 * appending another copy, and that the load-time repair rebuilds rows written before
 * that was fixed (#335).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef RESPIRATORYEVENTSTESTS_H
#define RESPIRATORYEVENTSTESTS_H

#include "AutoTest.h"

class Machine;
class Session;
class QCoreApplication;
class QTemporaryDir;

class RespiratoryEventsTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testResaveReplacesRows();
    void testRepairRebuildsDuplicatedRows();
    void cleanupTestCase();

private:
    //! \brief Number of respiratory_events rows for the session's database row
    int rowCount(const Session *sess) const;

    QCoreApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QString m_previousAppData;
    Machine *m_machine = nullptr;
    Session *m_session = nullptr;
};

DECLARE_TEST(RespiratoryEventsTests)

#endif // RESPIRATORYEVENTSTESTS_H
