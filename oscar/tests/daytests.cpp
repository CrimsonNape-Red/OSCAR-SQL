/* Day Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "daytests.h"
#include "SleepLib/day.h"
#include "SleepLib/machine.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"

#include <QTemporaryDir>

/*! \brief Add a session to \a day, which then owns it
    \param day         day to add to
    \param mach        device the session belongs to
    \param id          session id; also sets the start time
    \param summaryOnly whether the session has summary data only */
static void addSession(Day & day, Machine & mach, SessionID id, bool summaryOnly)
{
    Session *sess = new Session(&mach, id);
    sess->really_set_first(qint64(id) * 1000);
    sess->really_set_last(qint64(id) * 1000 + 3600000);
    sess->setSummaryOnly(summaryOnly);
    day.addSession(sess);
}

/*! \brief A profile that is current (p_profile) for the life of the object
    \details Session::enabled() and Session::setEnabled() read the current profile. The
    previous one is put back on destruction. */
class ScopedProfile
{
public:
    ScopedProfile() : m_profile(m_dir.path(), false), m_previous(p_profile) { p_profile = &m_profile; }
    ~ScopedProfile() { p_profile = m_previous; }
    Profile & profile() { return m_profile; }
private:
    QTemporaryDir m_dir;
    Profile m_profile;
    Profile *m_previous;
};

/*! \brief Add a one-hour session to \a day and return it; the day owns it
    \param day  day to add to
    \param mach device the session belongs to
    \param id   session id; also sets the start time */
static Session *addPlainSession(Day & day, Machine & mach, SessionID id)
{
    Session *sess = new Session(&mach, id);
    sess->really_set_first(qint64(id) * 1000);
    sess->really_set_last(qint64(id) * 1000 + 3600000);
    day.addSession(sess);
    return sess;
}

// GitLab #436: a channel counts as recorded on a day when a session has samples for it, or
// a summary that is not all zero. A channel stored with a zero count and zero summaries (a
// list the loader created and never filled) and a channel the session does not have at all
// are both "nothing recorded".
void DayTests::testRecordedDataNeedsSamplesOrSummary()
{
    if (CPAP_Pressure == 0) { schema::init(); }
    ScopedProfile scoped;
    CPAP cpap(&scoped.profile(), 1);
    Day day;
    Session *sess = addPlainSession(day, cpap, 1000000);

    // As loaded from session_channels for a list that was never filled.
    sess->m_cnt[CPAP_Pressure] = 0;
    sess->m_wavg[CPAP_Pressure] = 0;
    sess->m_min[CPAP_Pressure] = 0;
    sess->m_max[CPAP_Pressure] = 0;
    QVERIFY(day.channelHasData(CPAP_Pressure));             // the older, looser test
    QVERIFY(!day.channelHasRecordedData(CPAP_Pressure));
    QVERIFY(!day.channelHasRecordedData(CPAP_PressureSet)); // not in the session at all

    // Samples whose values are all zero are still a recording (e.g. a night with no leak).
    sess->m_cnt[CPAP_Leak] = 3600;
    sess->m_wavg[CPAP_Leak] = 0;
    sess->m_min[CPAP_Leak] = 0;
    sess->m_max[CPAP_Leak] = 0;
    QVERIFY(day.channelHasRecordedData(CPAP_Leak));

    // No samples, but a summary the loader supplied directly.
    sess->m_cnt[CPAP_EPAP] = 0;
    sess->m_wavg[CPAP_EPAP] = 6.4f;
    QVERIFY(day.channelHasRecordedData(CPAP_EPAP));

    // A second session of the night with samples makes the day's answer true.
    Session *second = addPlainSession(day, cpap, 1010000);
    second->m_cnt[CPAP_Pressure] = 120;
    QVERIFY(day.channelHasRecordedData(CPAP_Pressure));
}

// A session still holding its events (not summarised yet): a filled list counts, an empty
// one does not.
void DayTests::testRecordedDataFromEventLists()
{
    if (CPAP_Pressure == 0) { schema::init(); }
    ScopedProfile scoped;
    CPAP cpap(&scoped.profile(), 1);
    Day day;
    Session *sess = addPlainSession(day, cpap, 1000000);

    sess->AddEventList(CPAP_IPAP, EVL_Event);
    QVERIFY(!day.channelHasRecordedData(CPAP_IPAP));

    EventList *pressure = sess->AddEventList(CPAP_Pressure, EVL_Event, 0.1f);
    pressure->AddEvent(qint64(1000000) * 1000, 80);
    QVERIFY(day.channelHasRecordedData(CPAP_Pressure));
}

// A disabled session's data is not part of the day.
void DayTests::testRecordedDataIgnoresDisabledSessions()
{
    if (CPAP_Pressure == 0) { schema::init(); }
    ScopedProfile scoped;
    CPAP cpap(&scoped.profile(), 1);
    Day day;
    Session *sess = addPlainSession(day, cpap, 1000000);
    sess->m_cnt[CPAP_Pressure] = 120;
    QVERIFY(day.channelHasRecordedData(CPAP_Pressure));

    sess->setEnabled(false);
    QVERIFY(!day.channelHasRecordedData(CPAP_Pressure));
}

// Every session of the night has summary data only: the day is a summary-only day.
void DayTests::testAllSummarySessions()
{
    CPAP cpap(nullptr, 1);
    Day day;
    addSession(day, cpap, 1000000, true);
    addSession(day, cpap, 1010000, true);
    QVERIFY(day.summaryOnly());
    QVERIFY(day.summaryOnly(&cpap));
}

// GitLab #425: one summary-only session (e.g. recorded with the SD card out) beside sessions
// with detail must not downgrade the whole night to summary-only.
void DayTests::testMixedDayIsNotSummaryOnly()
{
    CPAP cpap(nullptr, 1);
    Day day;
    addSession(day, cpap, 1000000, false);
    addSession(day, cpap, 1010000, true);
    addSession(day, cpap, 1020000, false);
    QVERIFY(!day.summaryOnly());
    QVERIFY(!day.summaryOnly(&cpap));
}

// An oximeter recording on a summary-only CPAP night is not CPAP detail.
void DayTests::testOximeterDoesNotCountAsDetail()
{
    CPAP cpap(nullptr, 1);
    Oximeter oximeter(nullptr, 2);
    Day day;
    addSession(day, cpap, 1000000, true);
    addSession(day, oximeter, 1000500, false);
    QVERIFY(day.summaryOnly());
    QVERIFY(day.summaryOnly(&cpap));
    QVERIFY(!day.summaryOnly(&oximeter));
}

// Asked about one device, only that device's sessions count.
void DayTests::testPerMachine()
{
    CPAP cpap(nullptr, 1);
    CPAP other(nullptr, 3);
    Day day;
    addSession(day, cpap, 1000000, false);
    QVERIFY(!day.summaryOnly(&cpap));
    QVERIFY(!day.summaryOnly(&other));
}

void DayTests::testEmptyDay()
{
    Day day;
    QVERIFY(!day.summaryOnly());
}
