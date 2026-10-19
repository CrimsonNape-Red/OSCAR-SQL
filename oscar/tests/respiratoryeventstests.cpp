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

#include "respiratoryeventstests.h"

#include <QDateTime>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "SleepLib/appsettings.h"
#include "SleepLib/common.h"
#include "SleepLib/machine.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"
#include "database/database_manager.h"
#include "database/respiratory_events_repository.h"

namespace {

const QString kProfileName = QStringLiteral("RespiratoryEventsUnitTest");

// 3 obstructive apneas + 2 hypopneas: 5 respiratory_events rows per correct save.
constexpr int kEventCount = 5;

} // namespace

void RespiratoryEventsTests::initTestCase()
{
    // note: need dummy app for QtSql even though headless (each suite owns its own)
    static int argc = 1;
    static char appName[] = "test";
    static char *argv[] = { appName, nullptr };
    m_app = new QCoreApplication(argc, argv);

    if (DatabaseManager::instance().isOpen()) {
        DatabaseManager::instance().close();
    }

    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-respeventstests-XXXXXX"));
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
        MT_CPAP, 0, QStringLiteral("RespEventsTestLoader"), QStringLiteral("Test"),
        QStringLiteral("Test CPAP"), QString(), QStringLiteral("resp-events-cpap"),
        QStringLiteral("Test"), QDateTime::currentDateTime(), 1);
    m_machine = p_profile->CreateMachine(info);
    QVERIFY(m_machine != nullptr);
    QVERIFY(m_machine->SaveToDatabase());
    QVERIFY(m_machine->getProfileId() > 0);

    const qint64 startMs = QDateTime(QDate(2026, 10, 1), QTime(23, 0)).toMSecsSinceEpoch();
    m_session = new Session(m_machine, static_cast<SessionID>(startMs / 1000L));
    EventList *oa = m_session->AddEventList(CPAP_Obstructive, EVL_Event);
    oa->AddEvent(startMs + 10 * 60000LL, 12);
    oa->AddEvent(startMs + 20 * 60000LL, 15);
    oa->AddEvent(startMs + 30 * 60000LL, 12);
    EventList *h = m_session->AddEventList(CPAP_Hypopnea, EVL_Event);
    h->AddEvent(startMs + 40 * 60000LL, 20);
    h->AddEvent(startMs + 50 * 60000LL, 18);
    m_session->really_set_first(startMs);
    m_session->really_set_last(startMs + 60 * 60000LL);
    m_session->SetChanged(true);
    QVERIFY(m_machine->AddSession(m_session));
    QVERIFY(m_machine->SaveSession(m_session));
    QVERIFY(m_session->sessionRowId() > 0);
    QCOMPARE(rowCount(m_session), kEventCount);
}

int RespiratoryEventsTests::rowCount(const Session *sess) const
{
    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM respiratory_events WHERE session_id = ?"));
    query.addBindValue(sess->sessionRowId());
    if (!query.exec() || !query.next()) return -1;
    return query.value(0).toInt();
}

void RespiratoryEventsTests::testResaveReplacesRows()
{
    // Recompress, reindex and the day split all re-save a session like this.
    m_session->SetChanged(true);
    QVERIFY(m_machine->SaveSession(m_session));
    QCOMPARE(rowCount(m_session), kEventCount);

    RespiratoryEventsRepository repo;
    QVERIFY(repo.findSessionsWithExcessRows(m_machine->getProfileId()).isEmpty());
}

void RespiratoryEventsTests::testRepairRebuildsDuplicatedRows()
{
    const qint64 profileId = m_machine->getProfileId();
    const qint64 rowId = m_session->sessionRowId();
    QSqlQuery query(DatabaseManager::instance().database());

    // What a pre-fix re-save left behind: a second copy of every row...
    query.prepare(QStringLiteral(
        "INSERT INTO respiratory_events "
        "(session_id, profile_id, channel_id, event_type, start_time, end_time, duration) "
        "SELECT session_id, profile_id, channel_id, event_type, start_time, end_time, duration "
        "FROM respiratory_events WHERE session_id = ?"));
    query.addBindValue(rowId);
    QVERIFY(query.exec());
    // ...plus a stale row for a channel that has no event list any more.
    query.prepare(QStringLiteral(
        "INSERT INTO respiratory_events "
        "(session_id, profile_id, channel_id, event_type, start_time, end_time, duration) "
        "VALUES (?, ?, ?, 1, ?, ?, 10)"));
    query.addBindValue(rowId);
    query.addBindValue(profileId);
    query.addBindValue(static_cast<int>(CPAP_ClearAirway));
    query.addBindValue(m_session->first() + 5 * 60000LL);
    query.addBindValue(m_session->first() + 5 * 60000LL + 10000LL);
    QVERIFY(query.exec());
    QCOMPARE(rowCount(m_session), 2 * kEventCount + 1);

    RespiratoryEventsRepository repo;
    QCOMPARE(repo.findSessionsWithExcessRows(profileId), QList<qint64>{ rowId });

    // The repair runs at profile load, when only summaries are in memory.
    m_session->TrashEvents();
    QVERIFY(p_profile->repairRespiratoryEvents(profileId, nullptr));

    QCOMPARE(rowCount(m_session), kEventCount);
    QVERIFY(repo.findSessionsWithExcessRows(profileId).isEmpty());
    QVERIFY(!m_session->eventsLoaded());
}

void RespiratoryEventsTests::cleanupTestCase()
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
