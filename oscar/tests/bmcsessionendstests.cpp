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

#include "bmcsessionendstests.h"

#include <QDateTime>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QVector>

#include "SleepLib/appsettings.h"
#include "SleepLib/common.h"
#include "SleepLib/day.h"
#include "SleepLib/machine.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"
#include "SleepLib/loader_plugins/bmc_loader.h"
#include "database/database_manager.h"
#include "database/session_repository.h"

namespace {

const QString kProfileName = QStringLiteral("BmcSessionEndsUnitTest");

constexpr qint64 kMinuteMs = 60000LL;

qint64 at(int day, int hour, int minute)
{
    return QDateTime(QDate(2026, 10, day), QTime(hour, minute)).toMSecsSinceEpoch();
}

} // namespace

void BmcSessionEndsTests::initTestCase()
{
    // note: need dummy app for QtSql even though headless (each suite owns its own)
    static int argc = 1;
    static char appName[] = "test";
    static char *argv[] = { appName, nullptr };
    m_app = new QCoreApplication(argc, argv);

    if (DatabaseManager::instance().isOpen()) {
        DatabaseManager::instance().close();
    }

    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-bmcsessionendstests-XXXXXX"));
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

    // The repair only looks at machines imported by the BMC legacy loader.
    const MachineInfo info(
        MT_CPAP, 0, bmc_class_name, QStringLiteral("BMC"),
        QStringLiteral("G2S A20"), QString(), QStringLiteral("bmc-session-ends"),
        QStringLiteral("BMC"), QDateTime::currentDateTime(), bmc_version);
    m_machine = p_profile->CreateMachine(info);
    QVERIFY(m_machine != nullptr);
    QVERIFY(m_machine->SaveToDatabase());
    QVERIFY(m_machine->getProfileId() > 0);
}

Session *BmcSessionEndsTests::addSession(qint64 startMs, int waveformMin, qint64 storedEndMs)
{
    Session *sess = new Session(m_machine, static_cast<SessionID>(startMs / 1000L));
    QVector<qint16> samples(waveformMin, 10);
    EventList *flow = sess->AddEventList(CPAP_FlowRate, EVL_Waveform, 1.0, 0.0, 0.0, 0.0,
                                         static_cast<EventDataType>(kMinuteMs));
    flow->AddWaveform(startMs, samples.data(), samples.size(), waveformMin * kMinuteMs);
    sess->really_set_first(startMs);
    sess->really_set_last(storedEndMs);
    sess->m_slices.append(SessionSlice(startMs, storedEndMs + 1000, MaskOn));
    sess->SetChanged(true);
    sess->UpdateSummaries();
    if (!m_machine->AddSession(sess) || !m_machine->SaveSession(sess)) {
        return nullptr;
    }
    // The repair runs at profile load, when only summaries are in memory.
    sess->TrashEvents();
    return sess;
}

qint64 BmcSessionEndsTests::storedEnd(const Session *sess) const
{
    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(QStringLiteral("SELECT end_time FROM sessions WHERE id = ?"));
    query.addBindValue(sess->sessionRowId());
    if (!query.exec() || !query.next()) return -1;
    return query.value(0).toLongLong();
}

void BmcSessionEndsTests::testRepairTrimsStretchedSessions()
{
    // Night 1: 3 h then 2 h of waveform. USR duration 299 min, which the pre-fix loader
    // measured from the second session's start, stretching it by 179 min.
    Session *n1a = addSession(at(1, 23, 0), 180, at(2, 2, 0));
    Session *n1b = addSession(at(2, 2, 1), 120, at(2, 2, 1) + 299 * kMinuteMs);
    // Night 2: one session whose waveform the ring buffer cut 2 h short. Its extension
    // is correct and must survive.
    Session *n2 = addSession(at(5, 23, 0), 120, at(6, 3, 0));
    // Night 3: 2 h then 1 h of waveform, with the second session's last 30 min lost to the
    // ring buffer. USR duration 210 min: only those 30 min should remain.
    Session *n3a = addSession(at(8, 23, 0), 120, at(9, 1, 0));
    Session *n3b = addSession(at(9, 1, 30), 60, at(9, 1, 30) + 210 * kMinuteMs);
    // Night 4: an afternoon nap from the previous USR day shares the OSCAR day with a 2 h +
    // 1 h night stretched by 119 min (USR 179 min). The nap must not count as earlier time.
    Session *n4nap = addSession(at(12, 13, 0), 20, at(12, 13, 20));
    Session *n4a = addSession(at(12, 23, 0), 120, at(13, 1, 0));
    Session *n4b = addSession(at(13, 1, 1), 60, at(13, 1, 1) + 179 * kMinuteMs);
    QVERIFY(n1a && n1b && n2 && n3a && n3b && n4nap && n4a && n4b);
    QCOMPARE(p_profile->GetDay(QDate(2026, 10, 12), MT_CPAP)->size(), 3);

    const qint64 profileId = m_machine->getProfileId();
    SessionRepository repo;
    bool ok = false;
    QCOMPARE(repo.findSessionsEndingAfterWaveform(profileId, bmc_class_name, kMinuteMs, &ok).size(), 4);
    QVERIFY(ok);

    QVERIFY(p_profile->repairBmcSessionEnds(profileId, nullptr));

    const qint64 n1bWaveformEnd = at(2, 2, 1) + 120 * kMinuteMs;
    QCOMPARE(n1b->realLast(), n1bWaveformEnd);
    QCOMPARE(storedEnd(n1b), n1bWaveformEnd);
    QCOMPARE(n1b->m_slices.size(), 1);
    QCOMPARE(n1b->m_slices.first().end, n1bWaveformEnd + 1000);
    QVERIFY(!n1b->eventsLoaded());

    QCOMPARE(n2->realLast(), at(6, 3, 0));
    QCOMPARE(storedEnd(n2), at(6, 3, 0));

    const qint64 n3bEnd = at(9, 1, 30) + 90 * kMinuteMs;
    QCOMPARE(n3b->realLast(), n3bEnd);
    QCOMPARE(storedEnd(n3b), n3bEnd);

    const qint64 n4bWaveformEnd = at(13, 1, 1) + 60 * kMinuteMs;
    QCOMPARE(storedEnd(n4b), n4bWaveformEnd);

    // The earlier sessions of each night are untouched.
    QCOMPARE(storedEnd(n1a), at(2, 2, 0));
    QCOMPARE(storedEnd(n3a), at(9, 1, 0));
    QCOMPARE(storedEnd(n4nap), at(12, 13, 20));
    QCOMPARE(storedEnd(n4a), at(13, 1, 0));

    // Only the correctly extended sessions are still candidates, and a rerun changes nothing.
    QCOMPARE(repo.findSessionsEndingAfterWaveform(profileId, bmc_class_name, kMinuteMs).size(), 2);
    QVERIFY(p_profile->repairBmcSessionEnds(profileId, nullptr));
    QCOMPARE(storedEnd(n3b), n3bEnd);
}

void BmcSessionEndsTests::cleanupTestCase()
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
