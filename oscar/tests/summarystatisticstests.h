/* Summary Statistics Unit Tests
 *
 * Tests that session_summaries and daily_summaries store the same leak channel
 * (unintentional leak) and the same percentile level (95th), and that the one-time
 * load-time repair rewrites rows stored before that was made consistent (#212).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SUMMARYSTATISTICSTESTS_H
#define SUMMARYSTATISTICSTESTS_H

#include "AutoTest.h"

#include <QVariant>

#include "SleepLib/machine_common.h"

class Day;
class Machine;
class Session;
class QCoreApplication;
class QTemporaryDir;

class SummaryStatisticsTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testSessionRowHoldsUnintentionalLeak();
    void testDailyRowHolds95thPercentile();
    void testDailyRowLeavesUnintentionalAvgEmpty();
    void testDayAndSessionRowsAgree();
    void testRebuildFromChannelsKeepsLeak();
    void testRefillRestoresSessionLeak();
    void testRefillClearsLeakWithoutChannel();
    void testProfileRepairRewritesBothTables();
    void testRestoredEventCount();
    void testSummaryOnlyCountKeepsFraction();
    void testRepairRewritesWholeNumberCounts();
    void testRefillLeavesUncomputedPercentileEmpty();
    void cleanupTestCase();

private:
    //! \brief One column of a session's session_summaries row (a null QVariant for SQL NULL)
    QVariant sessionColumn(const Session *sess, const QString &column) const;
    //! \brief One column of a day's daily_summaries row (a null QVariant for SQL NULL)
    QVariant dailyColumn(const Day *day, const QString &column) const;
    //! \brief Overwrite the leak columns of a session's row, as rows written before #212 held
    bool setSessionLeak(const Session *sess, double avg, double p95, double max) const;
    //! \brief Build, summarise and save a one-hour session starting at \a startMs
    Session *addSession(qint64 startMs, bool withLeak);
    //! \brief Save a 2.8-hour summary-only session with an obstructive index of 0.1/h
    Session *addSummaryOnlySession(qint64 startMs);
    //! \brief session_channels.count of one channel of a session
    QVariant channelCount(const Session *sess, ChannelID id) const;

    QCoreApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QString m_previousAppData;
    Machine *m_machine = nullptr;
    Session *m_session = nullptr;       ///< pressure, unintentional leak and total leak
    Session *m_noLeakSession = nullptr; ///< pressure only, on another night
    Day *m_day = nullptr;               ///< the day holding m_session alone
    Session *m_summaryOnly = nullptr;   ///< summary-only, 0.28 obstructive events
};

DECLARE_TEST(SummaryStatisticsTests)

#endif // SUMMARYSTATISTICSTESTS_H
