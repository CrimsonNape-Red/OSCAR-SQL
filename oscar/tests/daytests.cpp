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
#include "SleepLib/session.h"

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
