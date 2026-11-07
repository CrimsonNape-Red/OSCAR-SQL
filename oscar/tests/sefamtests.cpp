/* SEFAM S.Box Archive Unit Tests
 *
 * Covers sessions that exist only in the memory image's archive: decoding each
 * block's start time and leak, choosing which blocks to import, and the session
 * built from one. Archive images here are synthetic byte-level fixtures; see
 * Notes/loaders/SEFAM_SBOX_CARD_ANALYSIS.md for the format.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QFile>
#include <QSet>
#include <QTemporaryDir>

#include "sefamtests.h"
#include "SleepLib/loader_plugins/sefamDataParsing.h"
#include "SleepLib/loader_plugins/sefam_loader.h"
#include "SleepLib/machine.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"

using namespace SefamParsing;

namespace {

//! One per-minute archive record, as the bytes the device stores.
struct RawMinute
{
    quint8 pressure;
    quint8 leak;
    quint8 events;      //!< Two bits each: OA, CA, OH, CH from the low end.
    quint8 flowLimit;
    quint8 snore;
};

void putWord(QByteArray &block, int word, quint16 value)
{
    block[word * 2]     = static_cast<char>(value >> 8);
    block[word * 2 + 1] = static_cast<char>(value & 0xFF);
}

quint16 bcd(int value)
{
    return static_cast<quint16>(((value / 10) << 4) | (value % 10));
}

/*! Build one archive block in APAP mode, 10.5 to 15.0 cmH2O. The start time words
    are given raw so that a test can store an impossible date. */
QByteArray makeBlock(const QVector<quint16> &startWords, const QVector<RawMinute> &minutes)
{
    QByteArray block(kArchiveHeaderLength, '\0');
    putWord(block, 0, kArchiveMarker);
    for (int i = 0; i < startWords.size(); ++i) { putWord(block, i + 1, startWords.at(i)); }
    putWord(block, 24, static_cast<quint16>(minutes.size()));
    putWord(block, 35, 150);
    putWord(block, 36, 105);

    int oa = 0, ca = 0, oh = 0, ch = 0, snore = 0, fl = 0;
    for (const RawMinute &m : minutes) {
        block.append(static_cast<char>(m.pressure));
        block.append(static_cast<char>(m.leak));
        block.append(static_cast<char>(m.events));
        block.append(static_cast<char>(m.flowLimit));
        block.append(static_cast<char>(m.snore));
        block.append('\0');
        oa    +=  m.events       & 3;
        ca    += (m.events >> 2) & 3;
        oh    += (m.events >> 4) & 3;
        ch    += (m.events >> 6) & 3;
        snore += m.snore;
        fl    += m.flowLimit;
    }
    putWord(block, 13, static_cast<quint16>(oa));
    putWord(block, 14, static_cast<quint16>(ca));
    putWord(block, 15, static_cast<quint16>(oh));
    putWord(block, 16, static_cast<quint16>(ch));
    putWord(block, 17, static_cast<quint16>(snore));
    putWord(block, 18, static_cast<quint16>(fl));
    return block;
}

QVector<quint16> startWords(const QDateTime &when)
{
    const QDate d = when.date();
    const QTime t = when.time();
    return { bcd(d.day()), bcd(d.month()), bcd(d.year() - 2000),
             bcd(t.hour()), bcd(t.minute()), bcd(t.second()) };
}

//! Write \a blocks as a memory image and read it back through the real decoder.
bool readBlocks(const QByteArray &blocks, QVector<SessionSummary> &out)
{
    QTemporaryDir dir;
    if (!dir.isValid()) { return false; }
    const QString path = dir.filePath("image.RAM");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { return false; }
    f.write(QByteArray(kArchiveOffset, '\0'));
    f.write(blocks);
    f.close();
    return readMemoryImage(path, out);
}

SessionSummary summaryAt(const QDateTime &start)
{
    SessionSummary s;
    s.start = start;
    s.minutes = 10;
    return s;
}

} // namespace

// AddEventList() looks the channel up in schema::channel, so the channel table must
// exist. Guarded as in apextests.cpp: another test class may have initialised it.
void SefamTests::initTestCase()
{
    if (CPAP_Obstructive == 0) { schema::init(); }
}

// Words 1 to 6 hold the start time in BCD and minute byte 1 holds the total leak.
// These supply the session's only clock and its leak trace when no directory exists.
void SefamTests::testArchiveBlockStartAndLeakDecoded()
{
    const QDateTime start(QDate(2026, 11, 6), QTime(1, 18, 32));
    const QByteArray block = makeBlock(startWords(start),
                                       { { 109, 71, 0, 0, 0 }, { 110, 72, 0, 0, 0 } });

    QVector<SessionSummary> archive;
    QVERIFY(readBlocks(block, archive));
    QCOMPARE(archive.size(), 1);
    QCOMPARE(archive.at(0).start, start);
    QCOMPARE(archive.at(0).minuteData.at(0).totalLeak, quint8(71));
    QCOMPARE(archive.at(0).minuteData.at(1).totalLeak, quint8(72));
}

// A hex digit in a BCD field, or a month 13, must give no start time. Reading 0x1A as
// day 20 would put a session on a night it never belonged to.
void SefamTests::testArchiveBlockBadStartIsInvalid()
{
    const QVector<RawMinute> one = { { 109, 71, 0, 0, 0 } };
    const QByteArray hexDigit = makeBlock({ 0x1A, 0x11, 0x26, 0x01, 0x18, 0x32 }, one);
    const QByteArray badMonth = makeBlock({ 0x06, 0x13, 0x26, 0x01, 0x18, 0x32 }, one);

    QVector<SessionSummary> archive;
    QVERIFY(readBlocks(hexDigit + badMonth, archive));
    QCOMPARE(archive.size(), 2);
    QVERIFY(!archive.at(0).start.isValid());
    QVERIFY(!archive.at(1).start.isValid());
}

// Picks only blocks inside the card's range that no existing session accounts for.
void SefamTests::testArchiveOnlySelection()
{
    const QDateTime oldest(QDate(2026, 5, 29), QTime(0, 53, 25));
    const QDateTime recorded(QDate(2026, 11, 5), QTime(1, 12, 57));
    const QDateTime missing(QDate(2026, 11, 6), QTime(1, 18, 32));
    const QDateTime after(QDate(2026, 11, 6), QTime(4, 13, 5));

    const QVector<SessionSummary> archive = {
        summaryAt(oldest.addSecs(-60)),     // 0: older than anything on the card
        summaryAt(oldest),                  // 1: has a session at this very second
        summaryAt(recorded.addSecs(4)),     // 2: same session, its clocks 4 s apart
        summaryAt(QDateTime()),             // 3: start time would not decode
        summaryAt(missing),                 // 4: no session: import
        summaryAt(after)                    // 5: no session, newest: import
    };
    const QSet<qint64> known = { oldest.toSecsSinceEpoch(), recorded.toSecsSinceEpoch() };

    const QVector<int> chosen = findArchiveOnlySessions(archive, oldest, known);
    QCOMPARE(chosen, QVector<int>({ 4, 5 }));
}

// Without a readable directory there is no range to work within, so nothing is chosen.
void SefamTests::testArchiveOnlyNeedsCardRange()
{
    const QVector<SessionSummary> archive = {
        summaryAt(QDateTime(QDate(2026, 11, 6), QTime(1, 18, 32)))
    };
    QVERIFY(findArchiveOnlySessions(archive, QDateTime(), QSet<qint64>()).isEmpty());
}

// The session is an ordinary one, not summary-only: a summary-only session would make
// Day::summaryOnly() downgrade any detailed session sharing its day.
void SefamTests::testArchiveSessionContents()
{
    const QDateTime start(QDate(2026, 11, 6), QTime(1, 18, 32));
    const QByteArray block = makeBlock(startWords(start), {
        { 109, 71, 0x10, 2, 0 },    // one obstructive hypopnea, two flow limitations
        { 110, 72, 0x40, 0, 1 },    // one central hypopnea, one snore
        { 111, 73, 0x00, 0, 0 }
    });
    QVector<SessionSummary> archive;
    QVERIFY(readBlocks(block, archive));

    Machine mach(nullptr, 1);
    Session session(&mach, 1);
    SefamLoader::populateArchiveSession(&session, archive.at(0));

    const qint64 startMs = start.toMSecsSinceEpoch();
    QCOMPARE(session.first(), startMs);
    QCOMPARE(session.last(), startMs + 3 * 60000);
    QVERIFY(!session.summaryOnly());
    QVERIFY(!session.eventlist.contains(CPAP_FlowRate));

    QVERIFY(session.eventlist.contains(CPAP_Pressure));
    EventList *pressure = session.eventlist[CPAP_Pressure].first();
    QCOMPARE(pressure->count(), 3U);
    QCOMPARE(pressure->first(), startMs);
    QVERIFY(qAbs(pressure->data(0) - 10.9f) < 0.001f);
    QVERIFY(qAbs(pressure->data(2) - 11.1f) < 0.001f);

    QVERIFY(session.eventlist.contains(CPAP_LeakTotal));
    EventList *leak = session.eventlist[CPAP_LeakTotal].first();
    QCOMPARE(leak->count(), 3U);
    QVERIFY(qAbs(leak->data(0) - 71 * 0.6f) < 0.001f);

    QCOMPARE(session.eventlist[CPAP_ObstructiveHypopnea].first()->count(), 1U);
    QCOMPARE(session.eventlist[CPAP_CentralHypopnea].first()->count(), 1U);
    QCOMPARE(session.eventlist[CPAP_VSnore].first()->count(), 1U);
    QCOMPARE(session.eventlist[CPAP_FlowLimit].first()->count(), 2U);
    // A lone event sits at the midpoint of its minute, as on a recorded S.Box session.
    QCOMPARE(session.eventlist[CPAP_CentralHypopnea].first()->time(0), startMs + 90000);

    QCOMPARE(session.settings[CPAP_Mode].toInt(), int(MODE_APAP));
    QVERIFY(qAbs(session.settings[CPAP_PressureMin].toFloat() - 10.5f) < 0.001f);
    QVERIFY(qAbs(session.settings[CPAP_PressureMax].toFloat() - 15.0f) < 0.001f);
}

// Minutes whose mean pressure says the blower was stopped are left out of the traces
// and out of usage, as blower-off stretches are on a recorded session.
void SefamTests::testArchiveSessionStoppedMinutes()
{
    const QDateTime start(QDate(2026, 11, 6), QTime(1, 18, 32));
    const QByteArray block = makeBlock(startWords(start), {
        { 109, 71, 0, 0, 0 },
        { 110, 72, 0, 0, 0 },
        {  17, 55, 0, 0, 0 },       // below 2.0 cmH2O: stopped
        { 111, 73, 0, 0, 0 },
        { 112, 74, 0, 0, 0 }
    });
    QVector<SessionSummary> archive;
    QVERIFY(readBlocks(block, archive));

    Machine mach(nullptr, 2);
    Session session(&mach, 1);
    SefamLoader::populateArchiveSession(&session, archive.at(0));

    QCOMPARE(session.eventlist[CPAP_Pressure].size(), 2);
    QCOMPARE(session.eventlist[CPAP_LeakTotal].size(), 2);
    QCOMPARE(session.eventlist[CPAP_Pressure].at(1)->first(),
             start.toMSecsSinceEpoch() + 3 * 60000);
    QVERIFY(qAbs(session.hours() - 4.0 / 60.0) < 0.0001);
}
