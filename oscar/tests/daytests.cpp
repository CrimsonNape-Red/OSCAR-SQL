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
#include "statistics.h"

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

/*! \brief Give \a sess a recorded measurement channel, as its summary holds it
    \param sess    session to add to
    \param code    channel
    \param average time-weighted average
    \param lowest  minimum
    \param highest maximum */
static void recordChannel(Session *sess, ChannelID code, EventDataType average,
                          EventDataType lowest, EventDataType highest)
{
    sess->m_cnt[code] = 3600;
    sess->m_wavg[code] = average;
    sess->m_min[code] = lowest;
    sess->m_max[code] = highest;
}

// GitLab #439: a night has oximetry when a session recorded SpO2 or pulse, whichever kind of
// device the session belongs to. A CPAP night without it is not a night of oximetry.
void DayTests::testOximetryFromAnyDevice()
{
    if (OXI_SPO2 == 0) { schema::init(); }
    ScopedProfile scoped;
    CPAP cpap(&scoped.profile(), 1);
    Oximeter oximeter(&scoped.profile(), 2);
    PositionSensor position(&scoped.profile(), 3);
    // The type a session takes from its device is the one in the device's info.
    cpap.setType(MT_CPAP);
    oximeter.setType(MT_OXIMETER);
    position.setType(MT_POSITION);

    Day cpapOnly;
    Session *therapy = addPlainSession(cpapOnly, cpap, 1000000);
    recordChannel(therapy, CPAP_Pressure, 9, 6, 12);
    QVERIFY(!cpapOnly.hasRecordedOximetry());
    QCOMPARE(cpapOnly.oximetryHours(), EventDataType(0));

    // Channels stored empty are not oximetry either.
    therapy->m_cnt[OXI_SPO2] = 0;
    therapy->m_wavg[OXI_SPO2] = 0;
    therapy->m_min[OXI_SPO2] = 0;
    therapy->m_max[OXI_SPO2] = 0;
    QVERIFY(!cpapOnly.hasRecordedOximetry());

    // An oximeter beside the CPAP device: the oximeter's hours are the oximetry hours.
    Day withOximeter;
    addPlainSession(withOximeter, cpap, 1000000);
    addPlainSession(withOximeter, cpap, 1010000);
    recordChannel(addPlainSession(withOximeter, oximeter, 1020000), OXI_Pulse, 60, 50, 80);
    QVERIFY(withOximeter.hasRecordedOximetry());
    QCOMPARE(withOximeter.oximetryHours(), EventDataType(1));

    // A CPAP device recording its own oximetry.
    Day builtIn;
    recordChannel(addPlainSession(builtIn, cpap, 1000000), OXI_SPO2, 96, 90, 99);
    QVERIFY(builtIn.hasRecordedOximetry());
    QCOMPARE(builtIn.oximetryHours(), EventDataType(1));

    // Oximetry filed under a device of another type still counts; disabled, it does not.
    Day other;
    Session *filed = addPlainSession(other, position, 1000000);
    recordChannel(filed, OXI_SPO2, 97, 94, 99);
    QVERIFY(other.hasRecordedOximetry());
    QCOMPARE(other.oximetryHours(), EventDataType(1));
    filed->setEnabled(false);
    QVERIFY(!other.hasRecordedOximetry());
    QCOMPARE(other.oximetryHours(), EventDataType(0));
}

// GitLab #439: the Oximeter Statistics figures come from the nights that have oximetry, so
// CPAP nights without it are neither counted nor averaged in.
void DayTests::testOximetryStatisticsUseOximetryNights()
{
    if (OXI_SPO2 == 0) { schema::init(); }
    ScopedProfile scoped;
    Profile & profile = scoped.profile();
    CPAP cpap(&profile, 1);
    Oximeter oximeter(&profile, 2);
    cpap.setType(MT_CPAP);
    oximeter.setType(MT_OXIMETER);
    const QDate first(2026, 3, 1);

    // 1 March: CPAP only.
    Day *cpapNight = new Day();
    cpapNight->setDate(first);
    recordChannel(addPlainSession(*cpapNight, cpap, 1000000), CPAP_Pressure, 9, 6, 12);
    profile.daylist.insert(cpapNight->date(), cpapNight);

    // 3 March: an hour of CPAP, then an hour of oximeter with SpO2 and pulse.
    Day *both = new Day();
    both->setDate(first.addDays(2));
    addPlainSession(*both, cpap, 1000000);
    Session *full = addPlainSession(*both, oximeter, 1010000);
    recordChannel(full, OXI_SPO2, 96, 90, 99);
    recordChannel(full, OXI_Pulse, 60, 50, 80);
    full->m_cnt[OXI_PulseChange] = 4;
    full->m_cnt[OXI_SPO2Drop] = 3;
    full->m_sum[OXI_SPO2Drop] = 72;         // seconds
    profile.daylist.insert(both->date(), both);

    // 6 March: an hour of oximeter with pulse only.
    Day *pulseOnly = new Day();
    pulseOnly->setDate(first.addDays(5));
    Session *partial = addPlainSession(*pulseOnly, oximeter, 1000000);
    recordChannel(partial, OXI_Pulse, 70, 55, 90);
    partial->m_cnt[OXI_PulseChange] = 2;
    profile.daylist.insert(pulseOnly->date(), pulseOnly);

    const QDate last = first.addDays(9);
    const QList<Day *> nights = profile.oximetryDays(first, last);
    QCOMPARE(nights.size(), 2);
    QCOMPARE(nights.first(), both);
    QCOMPARE(nights.last(), pulseOnly);
    QVERIFY(profile.oximetryDays(first, first.addDays(1)).isEmpty());
    QCOMPARE(profile.oximetryDays(first.addDays(5), first.addDays(5)).size(), 1);
    QVERIFY(profile.oximetryDays(QDate(), last).isEmpty());

    auto value = [&](const char *channel, StatCalcType calc) {
        return StatisticsRow(channel, calc, MT_OXIMETER).oximetryValue(nights, first, last);
    };
    QCOMPARE(value("", SC_TOTAL_DAYS), QString("10"));
    QCOMPARE(value("", SC_DAYS_W_DATA), QString("2"));
    QCOMPARE(value("", SC_DAYS_WO_DATA), QString("8"));
    // SpO2 was recorded on one of the two nights only.
    QCOMPARE(value("SPO2", SC_WAVG), QString("96.00"));
    QCOMPARE(value("SPO2", SC_MIN), QString("90.00"));
    // Pulse: 60 over 1 hour of oximetry and 70 over 1 hour. 3 March also had an hour of
    // CPAP, which is no part of the pulse average's weight.
    QCOMPARE(value("Pulse", SC_WAVG), QString("65.00"));
    QCOMPARE(value("Pulse", SC_MIN), QString("50.00"));
    QCOMPARE(value("Pulse", SC_MAX), QString("90.00"));
    // Indices are per hour of oximetry: 6 pulse changes, and 72 seconds of drops, in 2 hours.
    QCOMPARE(value("PulseChange", SC_CPH), QString("3.00"));
    QCOMPARE(value("SPO2Drop", SC_SPH), QString("1.00%"));

    // A column without a night of oximetry has nothing to show.
    const QList<Day *> none;
    QCOMPARE(StatisticsRow("SPO2", SC_WAVG, MT_OXIMETER).oximetryValue(none, first, first), QString("-"));
    QCOMPARE(StatisticsRow("", SC_DAYS_W_DATA, MT_OXIMETER).oximetryValue(none, first, first), QString("0"));
    // Nights with pulse only: no SpO2 figure rather than 0.00.
    const QList<Day *> pulseNights = profile.oximetryDays(first.addDays(5), last);
    QCOMPARE(StatisticsRow("SPO2", SC_MIN, MT_OXIMETER).oximetryValue(pulseNights, first.addDays(5), last),
             QString("-"));

    // The days hold sessions of the devices above: remove them before the devices go.
    qDeleteAll(profile.daylist);
    profile.daylist.clear();
}

// A day's weight in a period average is the hours of the device type asked for. A night
// with 1 hour of CPAP and 3 hours of oximeter weighs 1 hour in a CPAP average, not 4.
void DayTests::testWavgWeightsByDeviceHours()
{
    if (CPAP_Pressure == 0) { schema::init(); }
    ScopedProfile scoped;
    Profile & profile = scoped.profile();
    CPAP cpap(&profile, 1);
    Oximeter oximeter(&profile, 2);
    cpap.setType(MT_CPAP);
    oximeter.setType(MT_OXIMETER);
    const QDate first(2026, 3, 1);

    // 1 March: an hour of CPAP at 10, and three hours of oximeter.
    Day *both = new Day();
    both->setDate(first);
    recordChannel(addPlainSession(*both, cpap, 1000000), CPAP_Pressure, 10, 10, 10);
    Session *oxi = new Session(&oximeter, 1010000);
    oxi->really_set_first(qint64(1010000) * 1000);
    oxi->really_set_last(qint64(1010000) * 1000 + 3 * 3600000);
    both->addSession(oxi);
    profile.daylist.insert(both->date(), both);

    // 2 March: an hour of CPAP at 20.
    Day *cpapOnly = new Day();
    cpapOnly->setDate(first.addDays(1));
    recordChannel(addPlainSession(*cpapOnly, cpap, 1100000), CPAP_Pressure, 20, 20, 20);
    profile.daylist.insert(cpapOnly->date(), cpapOnly);

    QCOMPARE(both->hours(), EventDataType(4));
    QCOMPARE(both->hours(MT_CPAP), EventDataType(1));
    // Two hours of CPAP, one at 10 and one at 20. Weighting 1 March by all 4 hours gave 12.
    QCOMPARE(profile.calcWavg(CPAP_Pressure, MT_CPAP, first, first.addDays(1)), EventDataType(15));

    qDeleteAll(profile.daylist);
    profile.daylist.clear();
}

QString formatTime(float time);     // statistics.cpp

// Statistics hours are rounded to the nearest minute, not truncated.
void DayTests::testStatisticsHoursRoundToMinute()
{
    QCOMPARE(formatTime(6.865f), QString("06:52"));    // 6 h 51.9 min
    QCOMPARE(formatTime(7.0261f), QString("07:02"));   // 7 h 1.57 min
    QCOMPARE(formatTime(7.66f), QString("07:40"));     // 7 h 39.6 min
    QCOMPARE(formatTime(7.995f), QString("08:00"));    // 7 h 59.7 min carries into the hour
    QCOMPARE(formatTime(0.0f), QString("00:00"));
    QCOMPARE(formatTime(10.5f), QString("10:30"));
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
    cpap.setType(MT_CPAP);
    oximeter.setType(MT_OXIMETER);
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
