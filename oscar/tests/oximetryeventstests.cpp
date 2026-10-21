/* Oximetry Event Unit Tests
 *
 * Tests the SpO2 drop and pulse change flags calculated from oximetry data.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "oximetryeventstests.h"

#include <QVector>

#include "SleepLib/calcs.h"
#include "SleepLib/machine.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"

#define TESTDATA_PATH "./testdata/"

namespace {

constexpr qint64 kStart = 1700000000000LL;
constexpr qint64 kSecondMs = 1000LL;

// One reading per second from kStart
void addReadings(Session &sess, ChannelID code, const QVector<EventDataType> &values)
{
    EventList *el = sess.AddEventList(code, EVL_Event);
    for (int i = 0; i < values.size(); ++i) {
        el->AddEvent(kStart + i * kSecondMs, values[i]);
    }
    sess.really_set_first(kStart);
    sess.really_set_last(kStart + (values.size() - 1) * kSecondMs);
}

// count seconds at value
QVector<EventDataType> run(EventDataType value, int count)
{
    return QVector<EventDataType>(count, value);
}

} // namespace

void OximetryEventsTests::initTestCase()
{
    p_profile = new Profile(TESTDATA_PATH "profile/", false);
    schema::init();
}

void OximetryEventsTests::testSpO2DropRecordsDepth()
{
    p_profile->oxi->setSpO2DropPercentage(3);
    p_profile->oxi->setSpO2DropDuration(8);
    p_profile->oxi->setSpO2BaselineMethod(SBM_Manual);
    p_profile->oxi->setSpO2ManualBaseline(96);

    Machine mach(nullptr, 3);
    Session sess(&mach, 1);
    // 95 and 94 are within 3 points of the baseline; the 93 → 90 → 93 dip is not.
    addReadings(sess, OXI_SPO2, run(96, 30) + run(95, 5) + run(93, 4) + run(90, 3)
                                + run(92, 4) + run(94, 5) + run(96, 30));

    QCOMPARE(calcSPO2Drop(&sess), 1);
    EventList *drops = sess.eventlist[OXI_SPO2Drop].at(0);
    QCOMPARE(drops->time(0), kStart + 46 * kSecondMs);   // the reading that ends the dip
    QCOMPARE(drops->data(0), EventDataType(11));           // 11 one-second readings
    QCOMPARE(drops->data2(0), EventDataType(6));           // baseline 96 to nadir 90
}

void OximetryEventsTests::testSpO2DropHonoursPreferences()
{
    p_profile->oxi->setSpO2BaselineMethod(SBM_Manual);
    p_profile->oxi->setSpO2ManualBaseline(96);
    const QVector<EventDataType> readings = run(96, 30) + run(93, 6) + run(96, 30);

    // A 3 point drop lasting 5 s: flagged only when both limits allow it
    p_profile->oxi->setSpO2DropPercentage(3);
    p_profile->oxi->setSpO2DropDuration(5);
    {
        Machine mach(nullptr, 3);
        Session sess(&mach, 1);
        addReadings(sess, OXI_SPO2, readings);
        QCOMPARE(calcSPO2Drop(&sess), 1);
    }

    p_profile->oxi->setSpO2DropDuration(8);
    {
        Machine mach(nullptr, 3);
        Session sess(&mach, 1);
        addReadings(sess, OXI_SPO2, readings);
        QCOMPARE(calcSPO2Drop(&sess), 0);
    }

    p_profile->oxi->setSpO2DropPercentage(4);
    p_profile->oxi->setSpO2DropDuration(5);
    {
        Machine mach(nullptr, 3);
        Session sess(&mach, 1);
        addReadings(sess, OXI_SPO2, readings);
        QCOMPARE(calcSPO2Drop(&sess), 0);
    }
}

void OximetryEventsTests::testSpO2DropIgnoresDropouts()
{
    p_profile->oxi->setSpO2DropPercentage(3);
    p_profile->oxi->setSpO2DropDuration(8);
    p_profile->oxi->setSpO2BaselineMethod(SBM_Manual);
    p_profile->oxi->setSpO2ManualBaseline(96);

    Machine mach(nullptr, 3);
    Session sess(&mach, 1);
    // A 2 s dip, then the finger comes out of the oximeter for 20 s: the dropout
    // must not stretch the dip into a long desaturation.
    addReadings(sess, OXI_SPO2, run(96, 30) + run(93, 3) + run(0, 20) + run(96, 30));

    QCOMPARE(calcSPO2Drop(&sess), 0);
}

void OximetryEventsTests::testRollingBaselineFollowsDrift()
{
    p_profile->oxi->setSpO2DropPercentage(3);
    p_profile->oxi->setSpO2DropDuration(8);
    p_profile->oxi->setSpO2BaselineMethod(SBM_Rolling);
    p_profile->oxi->setSpO2BaselineWindow(120);

    Machine mach(nullptr, 3);
    Session sess(&mach, 1);
    // SpO2 drifts down a point every 75 s from 97 to 93, then dips to 89 for 10 s. Against a
    // fixed 97 everything from 93 down would be one long drop; the rolling baseline follows
    // the drift and finds only the dip, 4 points below the new level.
    addReadings(sess, OXI_SPO2, run(97, 300) + run(96, 75) + run(95, 75) + run(94, 75)
                                + run(93, 300) + run(89, 11) + run(93, 120));

    QCOMPARE(calcSPO2Drop(&sess), 1);
    EventList *drops = sess.eventlist[OXI_SPO2Drop].at(0);
    QCOMPARE(drops->time(0), kStart + 836 * kSecondMs);
    QCOMPARE(drops->data(0), EventDataType(11));
    QCOMPARE(drops->data2(0), EventDataType(4));
}

void OximetryEventsTests::testRollingBaselineExcludesEarlierDrops()
{
    p_profile->oxi->setSpO2DropPercentage(3);
    p_profile->oxi->setSpO2DropDuration(8);
    p_profile->oxi->setSpO2BaselineMethod(SBM_Rolling);
    p_profile->oxi->setSpO2BaselineWindow(120);

    Machine mach(nullptr, 3);
    Session sess(&mach, 1);
    // Two 40 s dips to 92 a few seconds apart. If the first counted towards the baseline
    // for the second, the second would be measured from about 94.7 and not be flagged.
    addReadings(sess, OXI_SPO2, run(96, 120) + run(92, 40) + run(96, 5) + run(92, 40) + run(96, 30));

    QCOMPARE(calcSPO2Drop(&sess), 2);
    EventList *drops = sess.eventlist[OXI_SPO2Drop].at(0);
    QCOMPARE(drops->data2(0), EventDataType(4));
    QCOMPARE(drops->data2(1), EventDataType(4));
}

void OximetryEventsTests::testRollingBaselineNeedsHistory()
{
    p_profile->oxi->setSpO2DropPercentage(3);
    p_profile->oxi->setSpO2DropDuration(8);
    p_profile->oxi->setSpO2BaselineMethod(SBM_Rolling);
    p_profile->oxi->setSpO2BaselineWindow(120);

    Machine mach(nullptr, 3);
    Session sess(&mach, 1);
    // A dip 30 s into the recording, before half the 120 s window has readings
    addReadings(sess, OXI_SPO2, run(96, 30) + run(90, 15) + run(96, 120));

    QCOMPARE(calcSPO2Drop(&sess), 0);
}

void OximetryEventsTests::testStaticBaselineMethods()
{
    p_profile->oxi->setSpO2DropPercentage(3);
    p_profile->oxi->setSpO2DropDuration(8);

    // An hour at 93, then 97 for the rest: the first hour's baseline is 93, the whole
    // night's is 97, so only the whole-night baseline makes the first hour one long drop.
    const QVector<EventDataType> readings = run(93, 3600) + run(97, 3 * 3600);

    p_profile->oxi->setSpO2BaselineMethod(SBM_FirstHour);
    {
        Machine mach(nullptr, 3);
        Session sess(&mach, 1);
        addReadings(sess, OXI_SPO2, readings);
        QCOMPARE(calcSPO2Drop(&sess), 0);
        QCOMPARE(sess.settings.value(OXI_SPO2Drop).toInt(), 93);
    }

    p_profile->oxi->setSpO2BaselineMethod(SBM_WholeNight);
    {
        Machine mach(nullptr, 3);
        Session sess(&mach, 1);
        addReadings(sess, OXI_SPO2, readings);
        QCOMPARE(calcSPO2Drop(&sess), 1);
        QCOMPARE(sess.settings.value(OXI_SPO2Drop).toInt(), 97);
    }
}

void OximetryEventsTests::testChangeOnlyReadings()
{
    p_profile->oxi->setSpO2DropPercentage(3);
    p_profile->oxi->setSpO2DropDuration(8);
    p_profile->oxi->setSpO2BaselineMethod(SBM_Rolling);
    p_profile->oxi->setSpO2BaselineWindow(120);

    Machine mach(nullptr, 3);
    Session sess(&mach, 1);
    // ResMed stores SpO2 only when it changes: 96 for 5 min, 92 for 20 s, then 96 again.
    // The dip is a single reading, so it lasts until the reading that ends it.
    EventList *el = sess.AddEventList(OXI_SPO2, EVL_Event);
    el->AddEvent(kStart, 96);
    el->AddEvent(kStart + 300 * kSecondMs, 92);
    el->AddEvent(kStart + 320 * kSecondMs, 96);
    el->AddEvent(kStart + 600 * kSecondMs, 96);
    sess.really_set_first(kStart);
    sess.really_set_last(kStart + 600 * kSecondMs);

    QCOMPARE(calcSPO2Drop(&sess), 1);
    EventList *drops = sess.eventlist[OXI_SPO2Drop].at(0);
    QCOMPARE(drops->time(0), kStart + 320 * kSecondMs);
    QCOMPARE(drops->data(0), EventDataType(20));
    QCOMPARE(drops->data2(0), EventDataType(4));
}

void OximetryEventsTests::testChangeOnlyPercentileIsTimeWeighted()
{
    p_profile->oxi->setSpO2DropPercentage(3);
    p_profile->oxi->setSpO2DropDuration(8);
    p_profile->oxi->setSpO2BaselineMethod(SBM_WholeNight);

    Machine mach(nullptr, 3);
    Session sess(&mach, 1);
    // 97 for 50 minutes, stored once, then 10 minutes flickering between 94 and 95 every
    // second. By readings the 90th percentile would be 95; by time it is 97.
    EventList *el = sess.AddEventList(OXI_SPO2, EVL_Event);
    el->AddEvent(kStart, 97);
    for (int i = 0; i < 600; ++i) {
        el->AddEvent(kStart + (3000 + i) * kSecondMs, (i % 2) ? 95 : 94);
    }
    el->AddEvent(kStart + 3600 * kSecondMs, 95);
    sess.really_set_first(kStart);
    sess.really_set_last(kStart + 3600 * kSecondMs);

    calcSPO2Drop(&sess);
    QCOMPARE(sess.settings.value(OXI_SPO2Drop).toInt(), 97);
}

void OximetryEventsTests::testSplitRecordingIsCalculatedWhole()
{
    p_profile->oxi->setSpO2DropPercentage(3);
    p_profile->oxi->setSpO2DropDuration(15);
    p_profile->oxi->setSpO2BaselineMethod(SBM_Manual);
    p_profile->oxi->setSpO2ManualBaseline(96);

    // A recording cut at 300 s, as SessionSplit does it: the value at the cut is repeated on
    // both sides. Its 20 s dip straddles the cut, leaving 10 s in each piece, which is too
    // short on its own.
    Machine mach(nullptr, 3);
    Session head(&mach, 1);
    addReadings(head, OXI_SPO2, run(96, 290) + run(90, 11));
    Session tail(&mach, 2);
    EventList *el = tail.AddEventList(OXI_SPO2, EVL_Event);
    const QVector<EventDataType> rest = run(90, 10) + run(96, 90);
    for (int i = 0; i < rest.size(); ++i) {
        el->AddEvent(kStart + (300 + i) * kSecondMs, rest[i]);
    }
    tail.really_set_first(kStart + 300 * kSecondMs);
    tail.really_set_last(kStart + 399 * kSecondMs);

    QCOMPARE(calcSPO2Drop(QList<Session *>{ &tail, &head }), 1);

    // The drop ends in the tail, so it's stored there; both pieces get the baseline.
    QVERIFY(!head.eventlist.contains(OXI_SPO2Drop));
    EventList *drops = tail.eventlist[OXI_SPO2Drop].at(0);
    QCOMPARE(drops->time(0), kStart + 310 * kSecondMs);
    QCOMPARE(drops->data(0), EventDataType(20));
    QCOMPARE(head.settings.value(OXI_SPO2Drop).toInt(), 96);
    QCOMPARE(tail.settings.value(OXI_SPO2Drop).toInt(), 96);
}

void OximetryEventsTests::testRollingBaselineSpansDataGaps()
{
    p_profile->oxi->setSpO2DropPercentage(3);
    p_profile->oxi->setSpO2DropDuration(8);
    p_profile->oxi->setSpO2BaselineMethod(SBM_Rolling);
    p_profile->oxi->setSpO2BaselineWindow(120);

    Machine mach(nullptr, 3);
    Session sess(&mach, 1);
    // PRS1 starts a new EventList after each sensor dropout. The second list starts 10 s
    // after the first ends and dips 20 s later: its baseline comes from both lists.
    EventList *before = sess.AddEventList(OXI_SPO2, EVL_Event);
    for (int i = 0; i < 300; ++i) {
        before->AddEvent(kStart + i * kSecondMs, 96);
    }
    EventList *after = sess.AddEventList(OXI_SPO2, EVL_Event);
    const QVector<EventDataType> rest = run(96, 20) + run(90, 20) + run(96, 60);
    for (int i = 0; i < rest.size(); ++i) {
        after->AddEvent(kStart + (310 + i) * kSecondMs, rest[i]);
    }
    sess.really_set_first(kStart);
    sess.really_set_last(kStart + 409 * kSecondMs);

    QCOMPARE(calcSPO2Drop(&sess), 1);
    EventList *drops = sess.eventlist[OXI_SPO2Drop].at(0);
    QCOMPARE(drops->time(0), kStart + 350 * kSecondMs);
    QCOMPARE(drops->data2(0), EventDataType(6));
}

void OximetryEventsTests::testPulseChangeRecordsLargestChange()
{
    p_profile->oxi->setPulseChangeBPM(5);
    p_profile->oxi->setPulseChangeDuration(8);

    Machine mach(nullptr, 3);
    Session sess(&mach, 1);
    // A 12 bpm spike, then back to 60 within the same window: the event is sized by
    // the spike, not by the last reading scanned.
    addReadings(sess, OXI_Pulse, run(60, 1) + run(72, 1) + run(60, 30));

    QCOMPARE(calcPulseChange(&sess), 1);
    EventList *changes = sess.eventlist[OXI_PulseChange].at(0);
    QCOMPARE(changes->time(0), kStart + 1 * kSecondMs);
    QCOMPARE(changes->data2(0), EventDataType(12));
}

void OximetryEventsTests::testPulseChangeIgnoresDropouts()
{
    p_profile->oxi->setPulseChangeBPM(5);
    p_profile->oxi->setPulseChangeDuration(8);

    Machine mach(nullptr, 3);
    Session sess(&mach, 1);
    addReadings(sess, OXI_Pulse, run(60, 30) + run(0, 20) + run(60, 30));

    QCOMPARE(calcPulseChange(&sess), 0);
}

void OximetryEventsTests::cleanupTestCase()
{
    delete p_profile;
    p_profile = nullptr;
}
