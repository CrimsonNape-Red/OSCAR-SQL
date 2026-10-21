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

#include "oximetryrecalctests.h"

#include <QDateTime>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QVector>

#include "SleepLib/appsettings.h"
#include "SleepLib/common.h"
#include "SleepLib/machine.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"
#include "database/database_manager.h"

namespace {

const QString kProfileName = QStringLiteral("OximetryRecalcUnitTest");

} // namespace

void OximetryRecalcTests::initTestCase()
{
    // note: need dummy app for QtSql even though headless (each suite owns its own)
    static int argc = 1;
    static char appName[] = "test";
    static char *argv[] = { appName, nullptr };
    m_app = new QCoreApplication(argc, argv);

    if (DatabaseManager::instance().isOpen()) {
        DatabaseManager::instance().close();
    }

    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-oximetryrecalctests-XXXXXX"));
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
        MT_OXIMETER, 0, QStringLiteral("TestOximeter"), QStringLiteral("Test"),
        QStringLiteral("Oximeter"), QString(), QStringLiteral("oximetry-recalc"),
        QString(), QDateTime::currentDateTime(), 1);
    m_machine = p_profile->CreateMachine(info);
    QVERIFY(m_machine != nullptr);
    QVERIFY(m_machine->SaveToDatabase());
    QVERIFY(m_machine->getProfileId() > 0);
}

void OximetryRecalcTests::testRecalculateUsesCurrentSettings()
{
    const qint64 start = QDateTime(QDate(2026, 10, 1), QTime(23, 0)).toMSecsSinceEpoch();

    // Saved with a 7 point drop threshold, so the 6 point dip is not flagged...
    p_profile->oxi->setSpO2BaselineMethod(SBM_Manual);
    p_profile->oxi->setSpO2ManualBaseline(96);
    p_profile->oxi->setSpO2DropPercentage(7);
    p_profile->oxi->setSpO2DropDuration(8);

    Session *sess = new Session(m_machine, static_cast<SessionID>(start / 1000L));
    EventList *spo2 = sess->AddEventList(OXI_SPO2, EVL_Event);
    // 20 minutes, long enough not to be ignored as a short session
    QVector<EventDataType> readings = QVector<EventDataType>(600, 96) + QVector<EventDataType>(20, 90)
                                      + QVector<EventDataType>(600, 96);
    for (int i = 0; i < readings.size(); ++i) {
        spo2->AddEvent(start + i * 1000L, readings[i]);
    }
    sess->really_set_first(start);
    sess->really_set_last(start + (readings.size() - 1) * 1000L);
    sess->SetChanged(true);
    sess->UpdateSummaries();
    QVERIFY(m_machine->AddSession(sess));
    QVERIFY(m_machine->SaveSession(sess));
    QVERIFY(p_profile->GetDay(QDate(2026, 10, 1), MT_OXIMETER) != nullptr);
    QVERIFY(!sess->m_cnt.value(OXI_SPO2Drop));
    // Recalculation runs at profile load, when only summaries are in memory.
    sess->TrashEvents();

    // ...then the threshold is lowered to 3 points.
    p_profile->oxi->setSpO2DropPercentage(3);
    QVERIFY(p_profile->recalculateOximetryEvents(nullptr));

    QVERIFY(!sess->eventsLoaded());
    QCOMPARE(sess->m_cnt.value(OXI_SPO2Drop), EventDataType(1));

    // The new flag was saved: load the session's events back from the database.
    QVERIFY(sess->OpenEvents());
    QVERIFY(sess->eventlist.contains(OXI_SPO2Drop));
    EventList *drops = sess->eventlist[OXI_SPO2Drop].at(0);
    QCOMPARE(drops->count(), quint32(1));
    QCOMPARE(drops->data2(0), EventDataType(6));
    QCOMPARE(sess->settings.value(OXI_SPO2Drop).toInt(), 96);
}

void OximetryRecalcTests::testRecalculateKeepsSplitRecordingWhole()
{
    p_profile->session->setSplitNonCPAPSessions(true);
    p_profile->oxi->setSpO2BaselineMethod(SBM_Manual);
    p_profile->oxi->setSpO2ManualBaseline(96);
    p_profile->oxi->setSpO2DropPercentage(3);
    p_profile->oxi->setSpO2DropDuration(15);

    // 20 minutes across the noon day split, with a 20 s dip straddling it: 10 s in each piece,
    // too short on its own for the 15 s minimum.
    const qint64 start = QDateTime(QDate(2026, 10, 5), QTime(11, 50)).toMSecsSinceEpoch();
    Session *sess = new Session(m_machine, static_cast<SessionID>(start / 1000L));
    EventList *spo2 = sess->AddEventList(OXI_SPO2, EVL_Event);
    for (int i = 0; i < 1200; ++i) {
        spo2->AddEvent(start + i * 1000L, ((i >= 590) && (i < 610)) ? 90 : 96);
    }
    sess->really_set_first(start);
    sess->really_set_last(start + 1199 * 1000L);
    sess->SetChanged(true);
    QVERIFY(m_machine->AddSession(sess));

    QList<Session *> pieces;
    for (Session *s : m_machine->sessionlist) {
        if ((s->realFirst() >= start) && (s->realLast() <= start + 1199 * 1000L)) pieces.append(s);
    }
    QCOMPARE(pieces.size(), 2);
    auto drops = [&pieces]() {
        EventDataType total = 0;
        for (Session *s : pieces) total += s->m_cnt.value(OXI_SPO2Drop);
        return total;
    };
    for (Session *s : pieces) {
        s->UpdateSummaries();
        QVERIFY(m_machine->SaveSession(s));
        s->TrashEvents();
    }
    QCOMPARE(drops(), EventDataType(1));    // found over the whole recording at import

    QVERIFY(p_profile->recalculateOximetryEvents(nullptr));
    QCOMPARE(drops(), EventDataType(1));

    p_profile->session->setSplitNonCPAPSessions(false);
}

void OximetryRecalcTests::testRecalculateSkipsUnloadableSessions()
{
    p_profile->oxi->setSpO2BaselineMethod(SBM_Manual);
    p_profile->oxi->setSpO2ManualBaseline(96);
    p_profile->oxi->setSpO2DropPercentage(3);
    p_profile->oxi->setSpO2DropDuration(8);

    const qint64 start = QDateTime(QDate(2026, 10, 8), QTime(23, 0)).toMSecsSinceEpoch();
    Session *sess = new Session(m_machine, static_cast<SessionID>(start / 1000L));
    EventList *spo2 = sess->AddEventList(OXI_SPO2, EVL_Event);
    for (int i = 0; i < 1200; ++i) {
        spo2->AddEvent(start + i * 1000L, ((i >= 600) && (i < 620)) ? 90 : 96);
    }
    sess->really_set_first(start);
    sess->really_set_last(start + 1199 * 1000L);
    sess->SetChanged(true);
    sess->UpdateSummaries();
    QVERIFY(m_machine->AddSession(sess));
    QVERIFY(m_machine->SaveSession(sess));
    sess->TrashEvents();
    QCOMPARE(sess->m_cnt.value(OXI_SPO2Drop), EventDataType(1));

    // Lose its stored events, so they can't be loaded
    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare(QStringLiteral("DELETE FROM event_lists WHERE session_id = ?"));
    query.addBindValue(sess->sessionRowId());
    QVERIFY(query.exec());

    // It's left as it was, and doesn't stop the rest being saved.
    p_profile->oxi->setSpO2DropPercentage(7);
    QVERIFY(p_profile->recalculateOximetryEvents(nullptr));
    QCOMPARE(sess->m_cnt.value(OXI_SPO2Drop), EventDataType(1));
    QCOMPARE(sess->settings.value(OXI_SPO2Drop).toInt(), 96);
}

void OximetryRecalcTests::cleanupTestCase()
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
