/* CSV oximetry Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QTimeZone>

#include "csvoximetrytests.h"
#include "SleepLib/loader_plugins/csvoximetry.h"

using namespace CsvOximetry;

static QDateTime local(int y, int mo, int d, int h, int mi, int s)
{
    return QDateTime(QDate(y, mo, d), QTime(h, mi, s));
}

void CsvOximetryTests::testPlainRows()
{
    // The example shown in the wizard: no header needed, any sampling rate.
    const Result r = parse(QStringLiteral(
        "2026-11-02 02:24:12,97,58\n"
        "2026-11-02 02:24:16,96,59\n"
        "2026-11-02 02:24:17,95,61\n"));
    QVERIFY(r.ok());
    QCOMPARE(r.rows, 3);
    QCOMPARE(r.recordings.size(), 1);
    const Recording &rec = r.recordings.first();
    QCOMPARE(rec.samples.size(), 3);
    QCOMPARE(rec.start(), local(2026, 11, 2, 2, 24, 12));
    QCOMPARE(rec.end(), local(2026, 11, 2, 2, 24, 17));
    QCOMPARE(rec.samples[1].spo2, 96);
    QCOMPARE(rec.samples[1].pulse, 59);
}

void CsvOximetryTests::testHeaderInAnyOrder()
{
    const Result r = parse(QStringLiteral(
        "\xFEFFPulse Rate (bpm);Oxygen Level (%);Timestamp\r\n"
        "58;97;2026-11-02T02:24:12\r\n"
        "59;96;2026-11-02T02:24:16\r\n"));
    QVERIFY2(r.ok(), qPrintable(r.error));
    QCOMPARE(r.recordings.first().samples[0].spo2, 97);
    QCOMPARE(r.recordings.first().samples[0].pulse, 58);
    QCOMPARE(r.recordings.first().start(), local(2026, 11, 2, 2, 24, 12));
}

void CsvOximetryTests::testSeparateDateAndTimeColumns()
{
    const Result r = parse(QStringLiteral(
        "Date,Time,SpO2,PR\n"
        "\"2026-11-02\",\"02:24\",97,58\n"
        "2026-11-02,02:25,96,59\n"));
    QVERIFY2(r.ok(), qPrintable(r.error));
    QCOMPARE(r.recordings.first().samples.size(), 2);
    QCOMPARE(r.recordings.first().end(), local(2026, 11, 2, 2, 25, 0));
}

void CsvOximetryTests::testMissingAndOutOfRange()
{
    const Result r = parse(QStringLiteral(
        "2026-11-02 02:24:12,97,58\n"
        "2026-11-02 02:24:13,,\n"
        "2026-11-02 02:24:14,0,0\n"
        "2026-11-02 02:24:15,--,255\n"
        "2026-11-02 02:24:16,127,61\n"
        "2026-11-02 02:24:17,96.6,60\n"));
    QVERIFY(r.ok());
    const auto &s = r.recordings.first().samples;
    QCOMPARE(s.size(), 6);
    for (int i : { 1, 2, 3 }) {
        QCOMPARE(s[i].spo2, 0);
    }
    QCOMPARE(s[3].pulse, 0);
    QCOMPARE(s[4].spo2, 0);      // out of range
    QCOMPARE(s[4].pulse, 61);
    QCOMPARE(s[5].spo2, 97);     // rounded
    QCOMPARE(r.badRows, 0);      // a missing reading is not a bad row
}

void CsvOximetryTests::testUnsortedAndRepeated()
{
    const Result r = parse(QStringLiteral(
        "2026-11-02 02:24:20,95,60\n"
        "2026-11-02 02:24:12,97,58\n"
        "2026-11-02 02:24:16,90,50\n"
        "2026-11-02 02:24:16,96,59\n"));
    QVERIFY(r.ok());
    const auto &s = r.recordings.first().samples;
    QCOMPARE(s.size(), 3);
    QCOMPARE(s[0].spo2, 97);
    QCOMPARE(s[1].spo2, 96);     // the last row for a repeated time wins
    QCOMPARE(s[2].spo2, 95);
}

void CsvOximetryTests::testGapsSplitRecordings()
{
    const Result r = parse(QStringLiteral(
        "2026-11-01 23:00:00,97,58\n"
        "2026-11-01 23:04:59,96,59\n"     // 4:59 later: same recording
        "2026-11-02 23:00:00,95,60\n"     // the next night
        "2026-11-02 23:00:04,95,60\n"
        "2026-11-03 10:00:00,,\n"));      // alone and without readings: dropped
    QVERIFY(r.ok());
    QCOMPARE(r.recordings.size(), 2);
    QCOMPARE(r.recordings[0].samples.size(), 2);
    QCOMPARE(r.recordings[1].start(), local(2026, 11, 2, 23, 0, 0));

    const Result tight = parse(QStringLiteral(
        "2026-11-01 23:00:00,97,58\n"
        "2026-11-01 23:00:10,96,59\n"), 5);
    QCOMPARE(tight.recordings.size(), 2);
}

void CsvOximetryTests::testOffsetAndVariants()
{
    // With an offset, converted to local time.
    const Result r = parse(QStringLiteral(
        "2026-07-15T00:59:45+02:00,97,58\n"
        "2026-07-14 22:59:46Z,96,59\n"
        "2026-07-14T17:59:47-0500,96,59\n"));
    QVERIFY2(r.ok(), qPrintable(r.error));
    QCOMPARE(r.recordings.size(), 1);
    const auto &s = r.recordings.first().samples;
    QCOMPARE(s.size(), 3);
    const QDateTime utc(QDate(2026, 7, 14), QTime(22, 59, 45), QTimeZone::utc());
    QCOMPARE(s[0].time, utc.toLocalTime());
    QCOMPARE(s[1].time, utc.addSecs(1).toLocalTime());
    QCOMPARE(s[2].time, utc.addSecs(2).toLocalTime());

    // Milliseconds, and minutes without seconds.
    const Result m = parse(QStringLiteral(
        "2026-07-14 22:59:00,96,59\n"
        "2026-07-14 22:59:47.25,96,59\n"
        "2026-07-14 23:00,96,59\n"));
    QVERIFY(m.ok());
    QCOMPARE(m.recordings.size(), 1);
    QCOMPARE(m.recordings.first().samples.size(), 3);
    QCOMPARE(m.recordings.first().samples[1].time, local(2026, 7, 14, 22, 59, 47).addMSecs(250));
    QCOMPARE(m.recordings.first().end(), local(2026, 7, 14, 23, 0, 0));
}

void CsvOximetryTests::testO2InsightProExport()
{
    // As Wellue's O2 Insight Pro exports it: quoted 12-hour times with a comma inside,
    // a trailing comma, 255 / 65535 for no reading.
    const Result r = parse(QStringLiteral(
        "Time,SpO2(%),Pulse Rate(bpm),Motion,SpO2 Reminder,PR Reminder,\r\n"
        "\"10:59:17PM Jul 15, 2026\",97,84,0,0,0,\r\n"
        "\"10:59:21PM Jul 15, 2026\",96,85,0,0,0,\r\n"
        "\"12:00:01AM Jul 16, 2026\",95,80,3,0,0,\r\n"
        "\"08:20:01AM Jul 16, 2026\",255,65535,14,0,0,\r\n"));
    QVERIFY2(r.ok(), qPrintable(r.error));
    QCOMPARE(r.badRows, 0);
    QCOMPARE(r.recordings.size(), 2);   // split at the long pause; the last row, alone and empty, is dropped
    const auto &first = r.recordings[0].samples;
    QCOMPARE(first[0].time, local(2026, 7, 15, 22, 59, 17));
    QCOMPARE(first[0].spo2, 97);
    QCOMPARE(first[0].pulse, 84);       // not Motion or a reminder
    QCOMPARE(r.recordings[1].start(), local(2026, 7, 16, 0, 0, 1));   // 12 AM is midnight
}

void CsvOximetryTests::testBadLinesReported()
{
    const Result r = parse(QStringLiteral(
        "DateTime,SpO2,Pulse\n"
        "2026-11-02 02:24:12,97,58\n"
        "02/11/2026 02:24:13,97,58\n"
        "2026-11-02 02:24:14,96,59\n"
        "2026-11-02 02:24:15,96,59\n"));
    QVERIFY(r.ok());
    QCOMPARE(r.rows, 4);
    QCOMPARE(r.badRows, 1);
    QCOMPARE(r.problems.size(), 1);
    QVERIFY(r.problems.first().startsWith(QStringLiteral("line 3:")));
    QCOMPARE(r.recordings.first().samples.size(), 3);
}

void CsvOximetryTests::testUnusableFiles()
{
    QVERIFY(!parse(QString()).ok());
    QVERIFY(!parse(QStringLiteral("DateTime,SpO2,Pulse\n")).ok());
    QVERIFY(!parse(QStringLiteral("Time,Value\n2026-11-02 02:24:12,97\n")).ok());   // no SpO2/pulse columns
    // Mostly the wrong date format: refused rather than half imported.
    QVERIFY(!parse(QStringLiteral(
        "11/02/2026 02:24:12,97,58\n"
        "11/02/2026 02:24:13,97,58\n"
        "2026-11-02 02:24:14,96,59\n")).ok());
    QVERIFY(!parse(QStringLiteral("2026-11-02 02:24:12,,\n2026-11-02 02:24:13,0,0\n")).ok());   // no readings
}
