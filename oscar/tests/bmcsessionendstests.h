/* BMC Session End Repair Unit Tests
 *
 * Tests the one-time repair of BMC legacy sessions that the loader stretched past their
 * waveforms on multi-session nights (#342).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef BMCSESSIONENDSTESTS_H
#define BMCSESSIONENDSTESTS_H

#include "AutoTest.h"

class Machine;
class Session;
class QCoreApplication;
class QTemporaryDir;

class BmcSessionEndsTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testRepairTrimsStretchedSessions();
    void cleanupTestCase();

private:
    /**
     * @brief Create, add and save a session with a one-sample-per-minute flow waveform
     * @param startMs     Session and waveform start (ms since epoch)
     * @param waveformMin Length of the waveform in minutes
     * @param storedEndMs Session end as the loader stored it
     * @return The saved session, with its events put away as at profile load
     */
    Session *addSession(qint64 startMs, int waveformMin, qint64 storedEndMs);

    //! \brief sessions.end_time stored for the session's database row
    qint64 storedEnd(const Session *sess) const;

    QCoreApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QString m_previousAppData;
    Machine *m_machine = nullptr;
};

DECLARE_TEST(BmcSessionEndsTests)

#endif // BMCSESSIONENDSTESTS_H
