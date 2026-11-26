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

#include "summarystatisticstests.h"

#include <QDateTime>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "SleepLib/appsettings.h"
#include "SleepLib/common.h"
#include "SleepLib/day.h"
#include "SleepLib/machine.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"
#include "database/daily_summary_repository.h"
#include "database/database_manager.h"
#include "database/session_summaries_repository.h"

namespace {

const QString kProfileName = QStringLiteral("SummaryStatisticsUnitTest");

// One sample a minute for an hour. Unintentional leak cycles 0..19 L/min and total leak
// 30..49 L/min, so the two never overlap and a stored value says which channel it came from.
constexpr int kSamples = 60;
constexpr double kLeakMax = 19.0;
constexpr double kTotalLeakMin = 30.0;

} // namespace

void SummaryStatisticsTests::initTestCase()
{
    // note: need dummy app for QtSql even though headless (each suite owns its own)
    static int argc = 1;
    static char appName[] = "test";
    static char *argv[] = { appName, nullptr };
    m_app = new QCoreApplication(argc, argv);

    if (DatabaseManager::instance().isOpen()) {
        DatabaseManager::instance().close();
    }

    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-summarystatstests-XXXXXX"));
    QVERIFY(m_tempDir->isValid());

    m_previousAppData = GetAppData();
    SetAppData(m_tempDir->path());

    p_profile = nullptr;
    p_pref = new Preferences(QStringLiteral("Preferences"));
    p_pref->Open();
    AppSetting = new AppWideSetting(p_pref);
    QVERIFY(DatabaseManager::instance().initialize(m_tempDir->path() + QStringLiteral("/oscar.db")));

    schema::init();
    Profiles::Scan();

    const QString profileDir = m_tempDir->path() + QStringLiteral("/Profiles/") + kProfileName;
    p_profile = Profiles::Get(kProfileName);
    if (p_profile == nullptr) {
        p_profile = Profiles::Create(kProfileName, &profileDir);
    }
    QVERIFY(p_profile != nullptr);

    const MachineInfo info(
        MT_CPAP, 0, QStringLiteral("SummaryStatsTestLoader"), QStringLiteral("Test"),
        QStringLiteral("Test CPAP"), QString(), QStringLiteral("summary-stats-cpap"),
        QStringLiteral("Test"), QDateTime::currentDateTime(), 1);
    m_machine = p_profile->CreateMachine(info);
    QVERIFY(m_machine != nullptr);
    QVERIFY(m_machine->SaveToDatabase());
    QVERIFY(m_machine->getProfileId() > 0);

    m_session = addSession(QDateTime(QDate(2026, 10, 1), QTime(23, 0)).toMSecsSinceEpoch(), true);
    QVERIFY(m_session != nullptr);
    m_noLeakSession = addSession(QDateTime(QDate(2026, 10, 3), QTime(23, 0)).toMSecsSinceEpoch(), false);
    QVERIFY(m_noLeakSession != nullptr);

    m_day = m_machine->day.value(QDate(2026, 10, 1), nullptr);
    QVERIFY(m_day != nullptr);
    QCOMPARE(m_day->sessions.size(), 1);

    // The fixture is only useful if the 90th and 95th percentiles differ.
    QVERIFY(m_day->percentile(CPAP_Pressure, 0.95f) > m_day->p90(CPAP_Pressure));
    QVERIFY(m_day->percentile(CPAP_Leak, 0.95f) > m_day->p90(CPAP_Leak));
}

Session *SummaryStatisticsTests::addSession(qint64 startMs, bool withLeak)
{
    Session *sess = new Session(m_machine, static_cast<SessionID>(startMs / 1000L));

    EventList *pressure = sess->AddEventList(CPAP_Pressure, EVL_Event, 0.1f);
    EventList *leak = withLeak ? sess->AddEventList(CPAP_Leak, EVL_Event, 1.0f) : nullptr;
    EventList *total = withLeak ? sess->AddEventList(CPAP_LeakTotal, EVL_Event, 1.0f) : nullptr;
    for (int i = 0; i < kSamples; ++i) {
        const qint64 t = startMs + i * 60000LL;
        pressure->AddEvent(t, 80 + i);                  // 8.0 .. 13.9 cmH2O
        if (leak) leak->AddEvent(t, i % 20);            // 0 .. 19 L/min
        if (total) total->AddEvent(t, 30 + i % 20);     // 30 .. 49 L/min
    }
    if (withLeak) {
        // The DV5's large-leak flag, which daily_summaries.leak_unintentional_avg used to average.
        sess->AddEventList(CPAP_LeakFlag, EVL_Event)->AddEvent(startMs + 30 * 60000LL, 5);
    }

    sess->really_set_first(startMs);
    sess->really_set_last(startMs + kSamples * 60000LL);
    sess->UpdateSummaries();
    sess->SetChanged(true);
    if (!m_machine->AddSession(sess) || !m_machine->SaveSession(sess) || sess->sessionRowId() <= 0) {
        return nullptr;
    }
    return sess;
}

QVariant SummaryStatisticsTests::sessionColumn(const Session *sess, const QString &column) const
{
    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(QStringLiteral("SELECT %1 FROM session_summaries WHERE session_id = ?").arg(column));
    query.addBindValue(sess->sessionRowId());
    if (!query.exec() || !query.next()) return QVariant(QStringLiteral("no row"));
    return query.value(0);
}

QVariant SummaryStatisticsTests::dailyColumn(const Day *day, const QString &column) const
{
    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(QStringLiteral("SELECT %1 FROM daily_summaries WHERE profile_id = ? AND date = ?").arg(column));
    query.addBindValue(m_machine->getProfileId());
    query.addBindValue(day->date().toString(Qt::ISODate));
    if (!query.exec() || !query.next()) return QVariant(QStringLiteral("no row"));
    return query.value(0);
}

bool SummaryStatisticsTests::setSessionLeak(const Session *sess, double avg, double p95, double max) const
{
    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(QStringLiteral(
        "UPDATE session_summaries SET leak_total_avg = ?, leak_total_95th = ?, leak_total_max = ? "
        "WHERE session_id = ?"));
    query.addBindValue(avg);
    query.addBindValue(p95);
    query.addBindValue(max);
    query.addBindValue(sess->sessionRowId());
    return query.exec() && query.numRowsAffected() == 1;
}

void SummaryStatisticsTests::testSessionRowHoldsUnintentionalLeak()
{
    // The UI's leak figures all come from CPAP_Leak; most devices report no total leak at all.
    QCOMPARE(sessionColumn(m_session, "leak_total_max").toDouble(), kLeakMax);
    QCOMPARE(sessionColumn(m_session, "leak_total_avg").toDouble(), double(m_session->wavg(CPAP_Leak)));
    QCOMPARE(sessionColumn(m_session, "leak_total_95th").toDouble(),
             double(m_session->percentile(CPAP_Leak, 0.95f)));
    QVERIFY(sessionColumn(m_session, "leak_total_95th").toDouble() < kTotalLeakMin);
}

void SummaryStatisticsTests::testDailyRowHolds95thPercentile()
{
    DailySummaryRepository repo;
    QVERIFY(repo.calculateAndStoreFromDay(m_day, m_machine->getProfileId()));

    QCOMPARE(dailyColumn(m_day, "pressure_95th").toDouble(), double(m_day->percentile(CPAP_Pressure, 0.95f)));
    QCOMPARE(dailyColumn(m_day, "leak_total_95th").toDouble(), double(m_day->percentile(CPAP_Leak, 0.95f)));
}

void SummaryStatisticsTests::testDailyRowLeavesUnintentionalAvgEmpty()
{
    DailySummaryRepository repo;
    QVERIFY(repo.calculateAndStoreFromDay(m_day, m_machine->getProfileId()));

    // leak_total_* already is the unintentional leak; this column is unused until it can be dropped.
    QVERIFY(dailyColumn(m_day, "leak_unintentional_avg").isNull());
}

void SummaryStatisticsTests::testDayAndSessionRowsAgree()
{
    DailySummaryRepository repo;
    QVERIFY(repo.calculateAndStoreFromDay(m_day, m_machine->getProfileId()));

    // A day of one session is that session.
    for (const char *column : { "pressure_95th", "leak_total_95th", "leak_total_max" }) {
        const QVariant daily = dailyColumn(m_day, column);
        const QVariant session = sessionColumn(m_session, column);
        QVERIFY2(!daily.isNull() && !session.isNull(), column);
        QVERIFY2(qFuzzyCompare(daily.toDouble() + 1.0, session.toDouble() + 1.0), column);
    }
}

void SummaryStatisticsTests::testRebuildFromChannelsKeepsLeak()
{
    // The rebuild clears a statistic whose source channel the session lacks; it must look
    // for the channel the row is written from.
    QSqlDatabase db = DatabaseManager::instance().database();
    QVERIFY(SessionSummariesRepository::rebuildFromChannels(db, m_machine->getDatabaseId()));

    QCOMPARE(sessionColumn(m_session, "leak_total_max").toDouble(), kLeakMax);
    QVERIFY(!sessionColumn(m_session, "leak_total_avg").isNull());
    QVERIFY(!sessionColumn(m_session, "leak_total_95th").isNull());
}

void SummaryStatisticsTests::testRefillRestoresSessionLeak()
{
    // What a row written before #212 held on a device that reports total leak.
    QVERIFY(setSessionLeak(m_session, 39.5, 48.0, 49.0));

    QSqlDatabase db = DatabaseManager::instance().database();
    QVERIFY(SessionSummariesRepository::refillLeakFromChannels(db, m_machine->getProfileId()));

    QCOMPARE(sessionColumn(m_session, "leak_total_max").toDouble(), kLeakMax);
    QCOMPARE(sessionColumn(m_session, "leak_total_avg").toDouble(), double(m_session->wavg(CPAP_Leak)));
    QCOMPARE(sessionColumn(m_session, "leak_total_95th").toDouble(),
             double(m_session->percentile(CPAP_Leak, 0.95f)));
}

void SummaryStatisticsTests::testRefillClearsLeakWithoutChannel()
{
    QVERIFY(setSessionLeak(m_noLeakSession, 39.5, 48.0, 49.0));

    QSqlDatabase db = DatabaseManager::instance().database();
    QVERIFY(SessionSummariesRepository::refillLeakFromChannels(db, m_machine->getProfileId()));

    for (const char *column : { "leak_total_avg", "leak_total_95th", "leak_total_max" }) {
        QVERIFY2(sessionColumn(m_noLeakSession, column).isNull(), column);
    }
}

void SummaryStatisticsTests::testProfileRepairRewritesBothTables()
{
    const qint64 profileId = m_machine->getProfileId();
    DailySummaryRepository repo;
    QVERIFY(repo.calculateAndStoreFromDay(m_day, profileId));

    // Rows as they were stored before #212: the day's 90th percentile under the 95th's
    // name, the flag average, and the session's total leak.
    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(QStringLiteral(
        "UPDATE daily_summaries SET pressure_95th = ?, leak_total_95th = ?, leak_unintentional_avg = 5 "
        "WHERE profile_id = ? AND date = ?"));
    query.addBindValue(double(m_day->p90(CPAP_Pressure)));
    query.addBindValue(double(m_day->p90(CPAP_Leak)));
    query.addBindValue(profileId);
    query.addBindValue(m_day->date().toString(Qt::ISODate));
    QVERIFY(query.exec());
    QCOMPARE(query.numRowsAffected(), 1);
    QVERIFY(setSessionLeak(m_session, 39.5, 48.0, 49.0));

    QVERIFY(p_profile->repairSummaryStatistics(profileId, nullptr));

    QCOMPARE(dailyColumn(m_day, "pressure_95th").toDouble(), double(m_day->percentile(CPAP_Pressure, 0.95f)));
    QCOMPARE(dailyColumn(m_day, "leak_total_95th").toDouble(), double(m_day->percentile(CPAP_Leak, 0.95f)));
    QVERIFY(dailyColumn(m_day, "leak_unintentional_avg").isNull());
    QCOMPARE(sessionColumn(m_session, "leak_total_max").toDouble(), kLeakMax);
}

void SummaryStatisticsTests::testRefillLeavesUncomputedPercentileEmpty()
{
    // Runs last: it strips the fixture session's leak histogram. session_channels.p95 is
    // then the 0 that stands for "never computed", which must not be taken as a 0 L/min leak.
    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(QStringLiteral(
        "DELETE FROM session_channel_values WHERE session_channel_id IN "
        "(SELECT id FROM session_channels WHERE session_id = ? AND channel_id = ?)"));
    query.addBindValue(m_session->sessionRowId());
    query.addBindValue(static_cast<int>(CPAP_Leak));
    QVERIFY(query.exec());
    QVERIFY(query.numRowsAffected() > 0);
    query.prepare(QStringLiteral("UPDATE session_channels SET p95 = 0 WHERE session_id = ? AND channel_id = ?"));
    query.addBindValue(m_session->sessionRowId());
    query.addBindValue(static_cast<int>(CPAP_Leak));
    QVERIFY(query.exec());

    QSqlDatabase db = DatabaseManager::instance().database();
    QVERIFY(SessionSummariesRepository::refillLeakFromChannels(db, m_machine->getProfileId()));

    QVERIFY(sessionColumn(m_session, "leak_total_95th").isNull());
    QCOMPARE(sessionColumn(m_session, "leak_total_max").toDouble(), kLeakMax);
}

void SummaryStatisticsTests::cleanupTestCase()
{
    Profiles::profiles.clear();

    delete p_profile;
    p_profile = nullptr;
    delete AppSetting;
    AppSetting = nullptr;
    delete p_pref;
    p_pref = nullptr;

    DatabaseManager::instance().close();
    SetAppData(m_previousAppData);

    delete m_tempDir;
    m_tempDir = nullptr;
    delete m_app;
    m_app = nullptr;
}
