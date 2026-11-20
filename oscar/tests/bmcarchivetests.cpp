/* BMC Night Archive Unit Tests
 *
 * Builds small synthetic BMC cards and checks that the parsers list their nights, hand
 * over each night's raw packets, and read the same packets back from a CardArchive when
 * the ring files are gone (GitLab #380).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "bmcarchivetests.h"

#include <QBuffer>
#include <QByteArray>
#include <QDate>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QLocale>
#include <QTemporaryDir>
#include <QtEndian>

#include <cstring>
#include <memory>

#include "SleepLib/card_archive.h"
#include "SleepLib/loader_plugins/bmc_archive.h"
#include "SleepLib/loader_plugins/bmcDataParsing.h"
#include "SleepLib/loader_plugins/bmcG3xDataParsing.h"

namespace {

// ---------------------------------------------------------------------------------
// Synthetic legacy card
// ---------------------------------------------------------------------------------

const QString kLegacyBase = QStringLiteral("12345678");
constexpr int kLegacyPacket = 0x100;
constexpr int kLegacyPacketsPerNight = 60;
//! Offset of the first historic session record in a .USR file
constexpr int kUsrSessionsStart = 0x102340;
//! Length of the smallest historic record the parser accepts (see bmcusrsessionstests.cpp)
constexpr int kUsrRecordLength = 0x4A;

const QList<QDate> kLegacyNights { QDate(2026, 3, 10), QDate(2026, 3, 11), QDate(2026, 3, 12) };
//! Date of the card's in-progress session: the day after the last recorded night.
const QDate kLegacyInProgress(2026, 3, 13);

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
    return f.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { return QByteArray(); }
    return f.readAll();
}

//! A USR date as BmcEncodedDate::DecodeDate() expects it.
quint16 encodeUsrDate(const QDate &date)
{
    return static_cast<quint16>(((date.year() - 2000) << 9) | (date.month() << 5) | date.day());
}

//! One 256-byte legacy waveform packet stamped \a when.
QByteArray legacyPacket(const QDateTime &when)
{
    QByteArray p(kLegacyPacket, '\0');
    p[0] = static_cast<char>(0xAA);
    p[1] = static_cast<char>(0xAA);
    qToLittleEndian<quint16>(static_cast<quint16>(when.date().year()), p.data() + 0xF8);
    p[0xFA] = static_cast<char>(when.date().month());
    p[0xFB] = static_cast<char>(when.date().day());
    p[0xFC] = static_cast<char>(when.time().hour());
    p[0xFD] = static_cast<char>(when.time().minute());
    p[0xFE] = static_cast<char>(when.time().second());
    p[0xFF] = static_cast<char>(when.date().dayOfWeek() % 7);
    return p;
}

//! Time of packet \a index in a buffer of legacy packets, read straight from its bytes.
QDateTime legacyPacketTime(const QByteArray &packets, int index)
{
    const char *p = packets.constData() + index * kLegacyPacket;
    const int year = qFromLittleEndian<quint16>(p + 0xF8);
    return QDateTime(QDate(year, static_cast<quint8>(p[0xFA]), static_cast<quint8>(p[0xFB])),
                     QTime(static_cast<quint8>(p[0xFC]), static_cast<quint8>(p[0xFD]),
                           static_cast<quint8>(p[0xFE])));
}

//! When a synthetic night's recording starts: 23:00:00 on the night's date.
QDateTime nightStart(const QDate &night)
{
    return QDateTime(night, QTime(23, 0, 0));
}

/*! \brief Write a legacy BMC card into \a dir.

    One historic USR record, one IDX entry and kLegacyPacketsPerNight one-second packets
    per night, the packets back to back in a single .000 file that has not wrapped.
 */
bool makeLegacyCard(const QString &dir, const QList<QDate> &nights, const QDate &inProgress)
{
    // .USR: in-progress session, then the historic records chained by 'next' pointers.
    QByteArray usr(kUsrSessionsStart, '\0');
    qToLittleEndian<quint16>(encodeUsrDate(inProgress), usr.data() + 0x431);
    usr[0x441] = static_cast<char>(0xFF);                 // in-progress session has no messages
    for (const QDate &night : nights) {
        QByteArray rec(kUsrRecordLength, '\0');
        rec[0] = static_cast<char>(0xE1);
        qToLittleEndian<quint32>(static_cast<quint32>(usr.size() + kUsrRecordLength), rec.data() + 1);
        qToLittleEndian<quint16>(encodeUsrDate(night), rec.data() + 0x07);
        qToLittleEndian<quint16>(1, rec.data() + 0x0f);   // duration, minutes
        rec[0x45] = static_cast<char>(0xFF);              // empty message list
        usr.append(rec);
    }

    // .idx: 0x800 of header, then one 512-byte entry per night pointing at its first packet.
    QByteArray idx(0x800, '\0');
    QByteArray ring;
    for (int n = 0; n < nights.size(); ++n) {
        QByteArray entry(512, '\0');
        entry[0] = static_cast<char>(0xAA);
        entry[1] = static_cast<char>(0xAA);
        qToLittleEndian<quint16>(static_cast<quint16>(n), entry.data() + 2);
        entry[4] = static_cast<char>(nights.at(n).year() - 2000);
        entry[5] = static_cast<char>(nights.at(n).month());
        entry[6] = static_cast<char>(nights.at(n).day());
        qToLittleEndian<quint16>(static_cast<quint16>(n * kLegacyPacketsPerNight), entry.data() + 0x0d);
        qToLittleEndian<quint16>(0, entry.data() + 0x0f);                 // file index
        entry[0x13] = static_cast<char>(0xFF);                            // no 'next' pointer
        idx.append(entry);

        for (int s = 0; s < kLegacyPacketsPerNight; ++s) {
            ring.append(legacyPacket(nightStart(nights.at(n)).addSecs(s)));
        }
    }

    const QString stem = dir + QLatin1Char('/') + kLegacyBase;
    return writeFile(stem + ".USR", usr) && writeFile(stem + ".idx", idx)
           && writeFile(stem + ".000", ring) && writeFile(stem + ".evt", QByteArray("evt"))
           && writeFile(stem + ".log", QByteArray("log"));
}

QList<QDateTime> waveformTimes(const BmcDateSession &session)
{
    QList<QDateTime> times;
    for (const BmcWaveformPacket &packet : session.Waveforms) {
        times.append(packet.Timestamp);
    }
    return times;
}

QList<QDateTime> packetTimes(const QByteArray &packets)
{
    QList<QDateTime> times;
    for (int i = 0; i < packets.size() / kLegacyPacket; ++i) {
        times.append(legacyPacketTime(packets, i));
    }
    return times;
}

//! The record for \a date among \a records, or one with handle -1.
BmcArchiveRecord recordFor(const QList<BmcArchiveRecord> &records, const QDate &date)
{
    for (const BmcArchiveRecord &record : records) {
        if (record.info.date == date) { return record; }
    }
    return BmcArchiveRecord();
}

/*! \brief Turn the legacy card in \a cardDir into an archive in \a archiveDir.

    Stores every night with waveform data except those in \a leaveOut, and copies the
    card's index files. The ring file is not copied.
 */
bool archiveLegacyCard(const QString &cardDir, const QString &archiveDir,
                       const QList<QDate> &leaveOut = QList<QDate>())
{
    BmcData card(cardDir);
    card.ReadData();
    CardArchive archive(archiveDir);
    archive.setIdentity("BMC", card.BaseName());
    QList<BmcArchiveRecord> records = card.ArchiveRecords();
    for (BmcArchiveRecord &record : records) {
        if (leaveOut.contains(record.info.date)) { continue; }
        const QByteArray bytes = card.ReadArchiveRecordBytes(record, nullptr);
        if (bytes.isEmpty()) { continue; }
        if (archive.store(record.info, bytes, card.PacketSize()) != CardArchive::StoreResult::Stored) {
            return false;
        }
    }
    for (const QString &name : card.IndexFileNames()) {
        if (!QFile::copy(cardDir + QLatin1Char('/') + name, archiveDir + QLatin1Char('/') + name)) {
            return false;
        }
    }
    return true;
}

} // namespace

void BmcArchiveTests::testLegacyRecordsListed()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(makeLegacyCard(dir.path(), kLegacyNights, kLegacyInProgress));

    BmcData card(dir.path());
    card.ReadData();
    const QList<BmcArchiveRecord> records = card.ArchiveRecords();

    // Three recorded nights and the card's in-progress session.
    QCOMPARE(records.size(), 4);
    QCOMPARE(records.at(0).info.date, kLegacyNights.at(0));
    QCOMPARE(records.at(1).info.date, kLegacyNights.at(1));
    QCOMPARE(records.at(2).info.date, kLegacyNights.at(2));
    QCOMPARE(records.at(3).info.date, kLegacyInProgress);
    // Only the newest can still grow.
    QVERIFY(records.at(0).info.final);
    QVERIFY(records.at(1).info.final);
    QVERIFY(records.at(2).info.final);
    QVERIFY(!records.at(3).info.final);
    // A legacy card cannot say how much it holds for a night without reading it.
    QCOMPARE(records.at(1).info.bytes, qint64(-1));
    QVERIFY(!records.at(1).info.first.isValid());
    QVERIFY(records.at(0).handle != records.at(1).handle);
}

void BmcArchiveTests::testLegacyBytesMatchImport()
{
    QTemporaryDir dir;
    QVERIFY(makeLegacyCard(dir.path(), kLegacyNights, kLegacyInProgress));
    const QDate night2 = kLegacyNights.at(1);

    BmcData card(dir.path());
    card.ReadData();
    const QList<BmcArchiveRecord> records = card.ArchiveRecords();

    BmcArchiveRecord record = recordFor(records, night2);
    const QByteArray bytes = card.ReadArchiveRecordBytes(record, nullptr);
    QCOMPARE(bytes.size(), kLegacyPacketsPerNight * kLegacyPacket);
    QCOMPARE(record.info.bytes, qint64(bytes.size()));
    QCOMPARE(record.info.first, nightStart(night2));
    QCOMPARE(record.info.last, nightStart(night2).addSecs(kLegacyPacketsPerNight - 1));
    QVERIFY(record.info.complete);

    // The archive unit is exactly what the import reads for that night.
    const BmcDateSession imported = card.ReadDateSession(night2);
    QCOMPARE(imported.Waveforms.size(), kLegacyPacketsPerNight);
    QCOMPARE(packetTimes(bytes), waveformTimes(imported));

    // The oldest packets on the card may be the tail of a night whose start was
    // overwritten, so that night is not marked complete.
    BmcArchiveRecord oldest = recordFor(records, kLegacyNights.at(0));
    QCOMPARE(card.ReadArchiveRecordBytes(oldest, nullptr).size(), kLegacyPacketsPerNight * kLegacyPacket);
    QVERIFY(!oldest.info.complete);

    // The in-progress session has no packets yet.
    BmcArchiveRecord inProgress = recordFor(records, kLegacyInProgress);
    QVERIFY(card.ReadArchiveRecordBytes(inProgress, nullptr).isEmpty());

    // A cancelled read hands over nothing.
    BmcArchiveRecord cancelled = recordFor(records, night2);
    QVERIFY(card.ReadArchiveRecordBytes(cancelled, []() { return true; }).isEmpty());
}

void BmcArchiveTests::testLegacyShortCardReadOnce()
{
    // A card whose whole ring holds one short recording: going from the end of the last
    // file back to the first never meets older data, so the scan has to stop by itself
    // after one lap instead of collecting the same packets again and again.
    QTemporaryDir dir;
    const QDate onlyNight(2026, 3, 10);
    QVERIFY(makeLegacyCard(dir.path(), QList<QDate>() << onlyNight, QDate(2026, 3, 11)));

    BmcData card(dir.path());
    card.ReadData();
    BmcArchiveRecord record = recordFor(card.ArchiveRecords(), onlyNight);
    const QByteArray bytes = card.ReadArchiveRecordBytes(record, nullptr);
    QCOMPARE(bytes.size(), kLegacyPacketsPerNight * kLegacyPacket);
    QCOMPARE(card.ReadDateSession(onlyNight).Waveforms.size(), kLegacyPacketsPerNight);
}

void BmcArchiveTests::testLegacyDescribePackets()
{
    QTemporaryDir dir;
    QVERIFY(makeLegacyCard(dir.path(), kLegacyNights, kLegacyInProgress));
    BmcData card(dir.path());
    card.ReadData();
    QCOMPARE(card.PacketSize(), kLegacyPacket);

    QByteArray packets;
    for (int s = 0; s < kLegacyPacketsPerNight; ++s) {
        packets.append(legacyPacket(nightStart(kLegacyNights.at(1)).addSecs(s)));
    }
    QDateTime first, last;
    QVERIFY(card.DescribePackets(packets, first, last));
    QCOMPARE(first, QDateTime(kLegacyNights.at(1), QTime(23, 0, 0)));
    QCOMPARE(last, QDateTime(kLegacyNights.at(1), QTime(23, 0, 59)));

    QVERIFY(!card.DescribePackets(QByteArray(), first, last));
    QVERIFY(!card.DescribePackets(packets.left(300), first, last));
    QVERIFY(!card.DescribePackets(QByteArray(kLegacyPacket, '\0'), first, last));   // no valid time
}

void BmcArchiveTests::testLegacyArchiveRoundTrip()
{
    QTemporaryDir cardDir;
    QTemporaryDir archiveDir;
    QVERIFY(makeLegacyCard(cardDir.path(), kLegacyNights, kLegacyInProgress));
    const QDate night2 = kLegacyNights.at(1);

    // Index files without ring data or an archive are not a card.
    {
        QTemporaryDir bare;
        QVERIFY(QFile::copy(cardDir.filePath(kLegacyBase + ".USR"), bare.filePath(kLegacyBase + ".USR")));
        QVERIFY(QFile::copy(cardDir.filePath(kLegacyBase + ".idx"), bare.filePath(kLegacyBase + ".idx")));
        QVERIFY(!BmcData::DirectoryHasBmcData(bare.path()));
    }

    QVERIFY(archiveLegacyCard(cardDir.path(), archiveDir.path()));
    QVERIFY(!QFile::exists(archiveDir.filePath(kLegacyBase + ".000")));
    QVERIFY(BmcData::DirectoryHasBmcData(archiveDir.path()));

    BmcData card(cardDir.path());
    card.ReadData();
    const BmcDateSession fromCard = card.ReadDateSession(night2);

    CardArchive archive(archiveDir.path());
    QVERIFY(archive.load());
    QCOMPARE(archive.entries().size(), 3);
    BmcData restored(archiveDir.path());
    restored.SetArchive(&archive);
    restored.ReadData();

    const BmcDateSession fromArchive = restored.ReadDateSession(night2);
    QCOMPARE(fromArchive.Waveforms.size(), kLegacyPacketsPerNight);
    QCOMPARE(waveformTimes(fromArchive), waveformTimes(fromCard));
    QCOMPARE(fromArchive.Sessions.size(), fromCard.Sessions.size());
    QCOMPARE(fromArchive.Sessions.first()->StartTimestamp, fromCard.Sessions.first()->StartTimestamp);
    QCOMPARE(fromArchive.Sessions.first()->EndTimestamp, fromCard.Sessions.first()->EndTimestamp);
    QVERIFY(restored.UnreadableNights().isEmpty());

    // In archive mode a record is described from the manifest, and reads back the same bytes.
    QList<BmcArchiveRecord> cardRecords = card.ArchiveRecords();
    QList<BmcArchiveRecord> archiveRecords = restored.ArchiveRecords();
    QCOMPARE(archiveRecords.size(), cardRecords.size());
    BmcArchiveRecord fromManifest = recordFor(archiveRecords, night2);
    QCOMPARE(fromManifest.info.bytes, qint64(kLegacyPacketsPerNight * kLegacyPacket));
    QCOMPARE(fromManifest.info.first, nightStart(night2));
    QCOMPARE(fromManifest.info.rule, kBmcLegacyCaptureRule);
    QVERIFY(fromManifest.info.final);
    BmcArchiveRecord cardRecord = recordFor(cardRecords, night2);
    QCOMPARE(restored.ReadArchiveRecordBytes(fromManifest, nullptr),
             card.ReadArchiveRecordBytes(cardRecord, nullptr));
    // A night the archive holds nothing for offers nothing.
    QCOMPARE(recordFor(archiveRecords, kLegacyInProgress).info.bytes, qint64(0));
}

void BmcArchiveTests::testLegacyMissingSegment()
{
    QTemporaryDir cardDir;
    QTemporaryDir archiveDir;
    QVERIFY(makeLegacyCard(cardDir.path(), kLegacyNights, kLegacyInProgress));
    const QDate night1 = kLegacyNights.at(0);
    QVERIFY(archiveLegacyCard(cardDir.path(), archiveDir.path(), QList<QDate>() << night1));

    CardArchive archive(archiveDir.path());
    QVERIFY(archive.load());
    QCOMPARE(archive.entries().size(), 2);
    BmcData restored(archiveDir.path());
    restored.SetArchive(&archive);
    restored.ReadData();

    // The night is still listed by the index files; it simply has no waveform.
    const BmcDateSession session = restored.ReadDateSession(night1);
    QVERIFY(session.Waveforms.isEmpty());
    QCOMPARE(session.StartTime.date(), night1);
    QVERIFY(restored.UnreadableNights().isEmpty());

    // Its neighbour is unaffected.
    QCOMPARE(restored.ReadDateSession(kLegacyNights.at(1)).Waveforms.size(), kLegacyPacketsPerNight);
}

void BmcArchiveTests::testLegacyCorruptSegment()
{
    QTemporaryDir cardDir;
    QTemporaryDir archiveDir;
    QVERIFY(makeLegacyCard(cardDir.path(), kLegacyNights, kLegacyInProgress));
    const QDate night2 = kLegacyNights.at(1);
    QVERIFY(archiveLegacyCard(cardDir.path(), archiveDir.path()));

    const QString segment = archiveDir.filePath(CardArchive::segmentDirName() + "/" + kLegacyBase
                                                + "_20260311.seg.gz");
    QVERIFY(QFile::exists(segment));
    QVERIFY(writeFile(segment, QByteArray("damaged beyond recognition")));
    const QByteArray manifestBefore = readFile(archiveDir.filePath(CardArchive::manifestName()));

    CardArchive archive(archiveDir.path());
    QVERIFY(archive.load());
    BmcData restored(archiveDir.path());
    restored.SetArchive(&archive);
    restored.ReadData();

    const BmcDateSession session = restored.ReadDateSession(night2);
    QVERIFY(session.Waveforms.isEmpty());
    QCOMPARE(restored.UnreadableNights(), QList<QDate>() << night2);
    // Asking again does not list it twice.
    restored.ReadDateSession(night2);
    QCOMPARE(restored.UnreadableNights(), QList<QDate>() << night2);

    // The parser reports; it does not change the archive.
    QCOMPARE(archive.entries().size(), 3);
    QCOMPARE(readFile(segment), QByteArray("damaged beyond recognition"));
    QCOMPARE(readFile(archiveDir.filePath(CardArchive::manifestName())), manifestBefore);

    QCOMPARE(restored.ReadDateSession(kLegacyNights.at(2)).Waveforms.size(), kLegacyPacketsPerNight);
}

void BmcArchiveTests::testLegacyRecordRule()
{
    QTemporaryDir dir;
    QVERIFY(makeLegacyCard(dir.path(), kLegacyNights, kLegacyInProgress));
    BmcData card(dir.path());
    card.ReadData();
    const QList<BmcArchiveRecord> records = card.ArchiveRecords();
    QVERIFY(!records.isEmpty());
    for (const BmcArchiveRecord &record : records) {
        QCOMPARE(record.info.rule, kBmcLegacyCaptureRule);
    }
}

void BmcArchiveTests::testLegacyFileClasses()
{
    QTemporaryDir dir;
    QVERIFY(makeLegacyCard(dir.path(), kLegacyNights, kLegacyInProgress));
    QVERIFY(writeFile(dir.filePath("unrelated.txt"), "x"));
    BmcData card(dir.path());
    card.ReadData();

    QCOMPARE(card.BaseName(), kLegacyBase);

    QVERIFY(card.IsRingFileName(kLegacyBase + ".000"));
    QVERIFY(card.IsRingFileName(kLegacyBase + ".029"));
    QVERIFY(!card.IsRingFileName(kLegacyBase + ".USR"));
    QVERIFY(!card.IsRingFileName(kLegacyBase + ".idx"));
    QVERIFY(!card.IsRingFileName(kLegacyBase + ".0000"));
    QVERIFY(!card.IsRingFileName(kLegacyBase + ".00"));
    QVERIFY(!card.IsRingFileName("other.000"));

    QStringList names = card.IndexFileNames();
    names.sort();
    QCOMPARE(names, QStringList() << kLegacyBase + ".USR" << kLegacyBase + ".evt"
                                  << kLegacyBase + ".idx" << kLegacyBase + ".log");

    QDate first, last;
    QVERIFY(card.IndexDateRange(&first, &last));
    QCOMPARE(first, kLegacyNights.first());
    QCOMPARE(last, kLegacyInProgress);
}

// ---------------------------------------------------------------------------------
// Synthetic G3X card
// ---------------------------------------------------------------------------------

namespace {

const QString kG3xBase = QStringLiteral("A3100000001");
constexpr int kG3xPacket = 0x800;
constexpr int kG3xPacketsPerNight = 30;
constexpr qint64 kG3xFileSpan = 64 * 1024 * 1024;

const QList<QDate> kG3xNights { QDate(2026, 3, 10), QDate(2026, 3, 11), QDate(2026, 3, 12) };

//! One 2048-byte G3X waveform packet stamped \a when, with a non-zero leak at 0x52A.
QByteArray g3xPacket(const QDateTime &when)
{
    QByteArray p(kG3xPacket, '\0');
    p[0] = static_cast<char>(0xAD);
    p[1] = static_cast<char>(0xAA);
    p[4] = static_cast<char>(when.date().year() - 1900);
    p[5] = static_cast<char>(when.date().month());
    p[6] = static_cast<char>(when.date().day());
    p[7] = static_cast<char>(when.time().hour());
    p[8] = static_cast<char>(when.time().minute());
    p[9] = static_cast<char>(when.time().second());
    qToLittleEndian<quint16>(static_cast<quint16>(100 + when.time().second()), p.data() + 0x52A);
    return p;
}

//! Time of packet \a index in a buffer of G3X packets, read straight from its bytes.
QDateTime g3xPacketTime(const QByteArray &packets, int index)
{
    const char *p = packets.constData() + index * kG3xPacket;
    return QDateTime(QDate(1900 + static_cast<quint8>(p[4]), static_cast<quint8>(p[5]), static_cast<quint8>(p[6])),
                     QTime(static_cast<quint8>(p[7]), static_cast<quint8>(p[8]), static_cast<quint8>(p[9])));
}

QList<QDateTime> g3xPacketTimes(const QByteArray &packets)
{
    QList<QDateTime> times;
    for (int i = 0; i < packets.size() / kG3xPacket; ++i) {
        times.append(g3xPacketTime(packets, i));
    }
    return times;
}

//! \a count one-second packets starting at \a start.
QByteArray g3xRun(const QDateTime &start, int count)
{
    QByteArray run;
    for (int s = 0; s < count; ++s) {
        run.append(g3xPacket(start.addSecs(s)));
    }
    return run;
}

//! Write \a bytes at \a offset of ring file \a index of the card in \a dir.
bool writeRing(const QString &dir, int index, qint64 offset, const QByteArray &bytes)
{
    QFile f(QString("%1/%2.%3").arg(dir, kG3xBase).arg(index, 3, 10, QLatin1Char('0')));
    if (!f.open(QIODevice::ReadWrite)) { return false; }
    if (!f.seek(offset)) { return false; }
    return f.write(bytes) == bytes.size();
}

//! One night of a synthetic G3X card: its IDX date and where its packets lie in the ring.
struct G3xNight {
    QDate date;
    quint32 waveStart;   //!< as the IDX stores it: an offset into the ring
    quint32 waveEnd;     //!< exclusive; below waveStart when the night crosses the wrap
    quint32 waveLength;
};

/*! \brief Write a G3X index for \a nights and an empty event file into \a dir.

    The firmware string is left blank, so the parser has to probe the waveform packets to
    choose its leak field, which exercises that read path too.
 */
bool makeG3xIndex(const QString &dir, const QList<G3xNight> &nights, int fileCount)
{
    QByteArray idx(0x800, '\0');
    memcpy(idx.data(), "BMC G/E/P INDEX", 15);
    memcpy(idx.data() + 0x30, "A3100000001", 11);
    memcpy(idx.data() + 0x100, "G3 A20", 6);
    qToLittleEndian<quint32>(static_cast<quint32>(kG3xFileSpan), idx.data() + 0xD4);
    qToLittleEndian<quint32>(static_cast<quint32>(fileCount), idx.data() + 0xD8);

    for (const G3xNight &night : nights) {
        QByteArray rec(0x800, '\0');
        rec[0] = static_cast<char>(0xAA);
        rec[1] = static_cast<char>(0xAA);
        rec[0x08] = static_cast<char>(night.date.year() - 1900);
        rec[0x09] = static_cast<char>(night.date.month());
        rec[0x0A] = static_cast<char>(night.date.day());
        qToLittleEndian<quint32>(night.waveStart, rec.data() + 0x10);
        qToLittleEndian<quint32>(night.waveEnd, rec.data() + 0x14);
        qToLittleEndian<quint32>(night.waveLength, rec.data() + 0x18);
        // IT block with a duration, so the night is still listed when it has no waveform.
        rec[0x80] = 'I';
        rec[0x81] = 'T';
        qToLittleEndian<quint32>(600, rec.data() + 0x80 + 0x14);
        idx.append(rec);
    }

    const QString stem = dir + QLatin1Char('/') + kG3xBase;
    return writeFile(stem + ".idx", idx) && writeFile(stem + ".evt", QByteArray());
}

/*! \brief Write a G3X card into \a dir: the given nights back to back at the start of .000.
    \return the nights as indexed
 */
QList<G3xNight> makeG3xCard(const QString &dir, const QList<QDate> &dates, int fileCount = 63)
{
    QList<G3xNight> nights;
    for (int n = 0; n < dates.size(); ++n) {
        G3xNight night;
        night.date = dates.at(n);
        night.waveStart = static_cast<quint32>(n * kG3xPacketsPerNight * kG3xPacket);
        night.waveLength = static_cast<quint32>(kG3xPacketsPerNight * kG3xPacket);
        night.waveEnd = night.waveStart + night.waveLength;
        nights.append(night);
        if (!writeRing(dir, 0, night.waveStart, g3xRun(nightStart(night.date), kG3xPacketsPerNight))) {
            return QList<G3xNight>();
        }
    }
    if (!makeG3xIndex(dir, nights, fileCount)) {
        return QList<G3xNight>();
    }
    return nights;
}

/*! \brief Turn the G3X card in \a cardDir into an archive in \a archiveDir, as
           archiveLegacyCard() does for a legacy card.
 */
bool archiveG3xCard(const QString &cardDir, const QString &archiveDir,
                    const QList<QDate> &leaveOut = QList<QDate>())
{
    BmcG3xData card(cardDir);
    card.ReadData();
    CardArchive archive(archiveDir);
    archive.setIdentity("BMCG3X", card.BaseName());
    QList<BmcArchiveRecord> records = card.ArchiveRecords();
    for (BmcArchiveRecord &record : records) {
        if (leaveOut.contains(record.info.date)) { continue; }
        const QByteArray bytes = card.ReadArchiveRecordBytes(record, nullptr);
        if (bytes.isEmpty()) { continue; }
        if (archive.store(record.info, bytes, card.PacketSize()) != CardArchive::StoreResult::Stored) {
            return false;
        }
    }
    for (const QString &name : card.IndexFileNames()) {
        if (!QFile::copy(cardDir + QLatin1Char('/') + name, archiveDir + QLatin1Char('/') + name)) {
            return false;
        }
    }
    return true;
}

QList<float> waveformLeaks(const BmcDateSession &session)
{
    QList<float> leaks;
    for (const BmcWaveformPacket &packet : session.Waveforms) {
        leaks.append(packet.Leak);
    }
    return leaks;
}

} // namespace

void BmcArchiveTests::testG3xRecordsListed()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(!makeG3xCard(dir.path(), kG3xNights).isEmpty());

    BmcG3xData card(dir.path());
    card.ReadData();
    const QList<BmcArchiveRecord> records = card.ArchiveRecords();

    QCOMPARE(records.size(), 3);
    for (int n = 0; n < 3; ++n) {
        QCOMPARE(records.at(n).info.date, kG3xNights.at(n));
        // A G3X index gives each night's extent, so nothing has to be read to know it.
        QCOMPARE(records.at(n).info.bytes, qint64(kG3xPacketsPerNight * kG3xPacket));
        QCOMPARE(records.at(n).info.first, nightStart(kG3xNights.at(n)));
        QCOMPARE(records.at(n).info.last, nightStart(kG3xNights.at(n)).addSecs(kG3xPacketsPerNight - 1));
        QVERIFY(records.at(n).info.complete);
    }
    QVERIFY(records.at(0).info.final);
    QVERIFY(records.at(1).info.final);
    QVERIFY(!records.at(2).info.final);
}

void BmcArchiveTests::testG3xBytesMatchImport()
{
    QTemporaryDir dir;
    QVERIFY(!makeG3xCard(dir.path(), kG3xNights).isEmpty());
    const QDate night2 = kG3xNights.at(1);

    BmcG3xData card(dir.path());
    card.ReadData();
    BmcArchiveRecord record = recordFor(card.ArchiveRecords(), night2);
    const QByteArray bytes = card.ReadArchiveRecordBytes(record, nullptr);
    QCOMPARE(bytes.size(), kG3xPacketsPerNight * kG3xPacket);
    QCOMPARE(record.info.bytes, qint64(bytes.size()));

    const BmcDateSession imported = card.ReadDateSession(night2);
    QCOMPARE(imported.Waveforms.size(), kG3xPacketsPerNight);
    QCOMPARE(g3xPacketTimes(bytes), waveformTimes(imported));

    QDateTime first, last;
    QCOMPARE(card.PacketSize(), kG3xPacket);
    QVERIFY(card.DescribePackets(bytes, first, last));
    QCOMPARE(first, nightStart(night2));
    QCOMPARE(last, nightStart(night2).addSecs(kG3xPacketsPerNight - 1));
    QVERIFY(!card.DescribePackets(QByteArray(), first, last));
    QVERIFY(!card.DescribePackets(bytes.left(3000), first, last));
    QVERIFY(!card.DescribePackets(QByteArray(kG3xPacket, '\0'), first, last));

    BmcArchiveRecord cancelled = recordFor(card.ArchiveRecords(), night2);
    QVERIFY(card.ReadArchiveRecordBytes(cancelled, []() { return true; }).isEmpty());
}

void BmcArchiveTests::testG3xWrappedNight()
{
    // A ring of two files. The older night lies in .000 after the first 20 packets; the
    // newer one starts 10 packets before the end of .001 and runs on into the start of .000.
    QTemporaryDir dir;
    const QDate older(2026, 3, 10);
    const QDate wrapped(2026, 3, 11);
    const quint32 p = kG3xPacket;

    QList<G3xNight> nights;
    nights.append({ older, 20 * p, 50 * p, 30 * p });
    nights.append({ wrapped, static_cast<quint32>(2 * kG3xFileSpan - 10 * p), 20 * p, 30 * p });
    QVERIFY(writeRing(dir.path(), 0, 20 * p, g3xRun(nightStart(older), 30)));
    QVERIFY(writeRing(dir.path(), 1, kG3xFileSpan - 10 * p, g3xRun(nightStart(wrapped), 10)));
    QVERIFY(writeRing(dir.path(), 0, 0, g3xRun(nightStart(wrapped).addSecs(10), 20)));
    QVERIFY(makeG3xIndex(dir.path(), nights, 2));

    BmcG3xData card(dir.path());
    card.ReadData();
    BmcArchiveRecord record = recordFor(card.ArchiveRecords(), wrapped);
    QCOMPARE(record.info.bytes, qint64(30 * kG3xPacket));
    QVERIFY(record.info.complete);

    const QByteArray bytes = card.ReadArchiveRecordBytes(record, nullptr);
    QCOMPARE(bytes.size(), 30 * kG3xPacket);
    const QList<QDateTime> times = g3xPacketTimes(bytes);
    for (int i = 0; i < times.size(); ++i) {
        QCOMPARE(times.at(i), nightStart(wrapped).addSecs(i));     // in order, wrap removed
    }
    QCOMPARE(waveformTimes(card.ReadDateSession(wrapped)), times);

    // The older night is untouched by its neighbour's wrap.
    BmcArchiveRecord olderRecord = recordFor(card.ArchiveRecords(), older);
    QCOMPARE(g3xPacketTimes(card.ReadArchiveRecordBytes(olderRecord, nullptr)).first(), nightStart(older));
}

void BmcArchiveTests::testG3xTailOnly()
{
    QTemporaryDir dir;
    QVERIFY(!makeG3xCard(dir.path(), kG3xNights).isEmpty());
    // A later pass round the ring has overwritten the first 12 packets of the oldest night.
    QVERIFY(writeRing(dir.path(), 0, 0, g3xRun(QDateTime(QDate(2026, 6, 10), QTime(23, 0, 0)), 12)));

    BmcG3xData card(dir.path());
    card.ReadData();
    BmcArchiveRecord record = recordFor(card.ArchiveRecords(), kG3xNights.at(0));
    QCOMPARE(record.info.bytes, qint64(18 * kG3xPacket));
    QVERIFY(!record.info.complete);
    QCOMPARE(record.info.first, nightStart(kG3xNights.at(0)).addSecs(12));

    const QByteArray bytes = card.ReadArchiveRecordBytes(record, nullptr);
    QCOMPARE(bytes.size(), 18 * kG3xPacket);
    QCOMPARE(g3xPacketTimes(bytes).first(), nightStart(kG3xNights.at(0)).addSecs(12));

    // Its neighbours are whole.
    QVERIFY(recordFor(card.ArchiveRecords(), kG3xNights.at(1)).info.complete);
}

void BmcArchiveTests::testG3xArchiveRoundTrip()
{
    QTemporaryDir cardDir;
    QTemporaryDir archiveDir;
    QVERIFY(!makeG3xCard(cardDir.path(), kG3xNights).isEmpty());
    const QDate night2 = kG3xNights.at(1);

    // An index alone, with neither ring files, events nor an archive, is not a card.
    {
        QTemporaryDir bare;
        QVERIFY(QFile::copy(cardDir.filePath(kG3xBase + ".idx"), bare.filePath(kG3xBase + ".idx")));
        QVERIFY(!BmcG3xData::DirectoryHasBmcG3xData(bare.path()));
    }

    QVERIFY(archiveG3xCard(cardDir.path(), archiveDir.path()));
    QVERIFY(!QFile::exists(archiveDir.filePath(kG3xBase + ".000")));
    // Even without its event file the archive is recognised as this device's data.
    QVERIFY(QFile::remove(archiveDir.filePath(kG3xBase + ".evt")));
    QVERIFY(BmcG3xData::DirectoryHasBmcG3xData(archiveDir.path()));

    BmcG3xData card(cardDir.path());
    card.ReadData();
    const BmcDateSession fromCard = card.ReadDateSession(night2);
    QCOMPARE(fromCard.Waveforms.size(), kG3xPacketsPerNight);

    CardArchive archive(archiveDir.path());
    QVERIFY(archive.load());
    QCOMPARE(archive.entries().size(), 3);
    BmcG3xData restored(archiveDir.path());
    restored.SetArchive(&archive);
    restored.ReadData();

    const BmcDateSession fromArchive = restored.ReadDateSession(night2);
    QCOMPARE(waveformTimes(fromArchive), waveformTimes(fromCard));
    QCOMPARE(waveformLeaks(fromArchive), waveformLeaks(fromCard));
    QVERIFY(fromCard.Waveforms.first().Leak > 0.0f);       // the probed leak field was found
    QCOMPARE(fromArchive.Sessions.size(), fromCard.Sessions.size());
    QCOMPARE(fromArchive.Sessions.first()->StartTimestamp, fromCard.Sessions.first()->StartTimestamp);
    QCOMPARE(fromArchive.Sessions.first()->EndTimestamp, fromCard.Sessions.first()->EndTimestamp);
    QCOMPARE(fromArchive.StartTime, fromCard.StartTime);
    QCOMPARE(fromArchive.DurationMinutes, fromCard.DurationMinutes);
    QVERIFY(restored.UnreadableNights().isEmpty());

    // The session links the loader imports from are the same.
    QCOMPARE(restored.GetSessionLinks().size(), card.GetSessionLinks().size());

    // In archive mode a record is described from the manifest and reads back the same bytes.
    BmcArchiveRecord fromManifest = recordFor(restored.ArchiveRecords(), night2);
    BmcArchiveRecord cardRecord = recordFor(card.ArchiveRecords(), night2);
    QCOMPARE(fromManifest.info.bytes, cardRecord.info.bytes);
    QCOMPARE(fromManifest.info.first, cardRecord.info.first);
    QCOMPARE(fromManifest.info.last, cardRecord.info.last);
    QCOMPARE(fromManifest.info.rule, kBmcG3xCaptureRule);
    QCOMPARE(restored.ReadArchiveRecordBytes(fromManifest, nullptr),
             card.ReadArchiveRecordBytes(cardRecord, nullptr));
}

void BmcArchiveTests::testG3xMissingAndCorruptSegment()
{
    QTemporaryDir cardDir;
    QTemporaryDir archiveDir;
    QVERIFY(!makeG3xCard(cardDir.path(), kG3xNights).isEmpty());
    const QDate night1 = kG3xNights.at(0);
    const QDate night2 = kG3xNights.at(1);
    QVERIFY(archiveG3xCard(cardDir.path(), archiveDir.path(), QList<QDate>() << night1));

    const QString segment = archiveDir.filePath(CardArchive::segmentDirName() + "/" + kG3xBase
                                                + "_20260311.seg.gz");
    QVERIFY(QFile::exists(segment));
    QVERIFY(writeFile(segment, QByteArray("damaged beyond recognition")));
    const QByteArray manifestBefore = readFile(archiveDir.filePath(CardArchive::manifestName()));

    CardArchive archive(archiveDir.path());
    QVERIFY(archive.load());
    BmcG3xData restored(archiveDir.path());
    restored.SetArchive(&archive);
    restored.ReadData();

    // Both nights are still listed by the index; neither has real waveform packets. (A
    // G3X night without waveform carries one synthetic packet, as an overwritten one does.)
    QCOMPARE(restored.GetSessionLinks().size(), 3);
    QVERIFY(restored.ReadDateSession(night1).Waveforms.size() <= 1);
    QVERIFY(restored.UnreadableNights().isEmpty());        // missing is not unreadable

    QVERIFY(restored.ReadDateSession(night2).Waveforms.size() <= 1);
    QCOMPARE(restored.UnreadableNights(), QList<QDate>() << night2);
    restored.ReadDateSession(night2);
    QCOMPARE(restored.UnreadableNights(), QList<QDate>() << night2);

    // The parser reports; it does not change the archive.
    QCOMPARE(archive.entries().size(), 2);
    QCOMPARE(readFile(segment), QByteArray("damaged beyond recognition"));
    QCOMPARE(readFile(archiveDir.filePath(CardArchive::manifestName())), manifestBefore);

    QCOMPARE(restored.ReadDateSession(kG3xNights.at(2)).Waveforms.size(), kG3xPacketsPerNight);
}

void BmcArchiveTests::testG3xRecordRule()
{
    QTemporaryDir dir;
    QVERIFY(!makeG3xCard(dir.path(), kG3xNights).isEmpty());
    BmcG3xData card(dir.path());
    card.ReadData();
    const QList<BmcArchiveRecord> records = card.ArchiveRecords();
    QVERIFY(!records.isEmpty());
    for (const BmcArchiveRecord &record : records) {
        QCOMPARE(record.info.rule, kBmcG3xCaptureRule);
    }
}

void BmcArchiveTests::testG3xNoRingAccessInArchiveMode()
{
    QTemporaryDir cardDir;
    QTemporaryDir archiveDir;
    QVERIFY(!makeG3xCard(cardDir.path(), kG3xNights).isEmpty());
    QVERIFY(archiveG3xCard(cardDir.path(), archiveDir.path()));

    // No ring file exists beside the archive; everything below must come from segments.
    QVERIFY(QDir(archiveDir.path()).entryList(QStringList() << "*.0??", QDir::Files).isEmpty());

    CardArchive archive(archiveDir.path());
    QVERIFY(archive.load());
    BmcG3xData restored(archiveDir.path());
    restored.SetArchive(&archive);
    restored.ReadData();
    for (const QDate &night : kG3xNights) {
        const BmcDateSession session = restored.ReadDateSession(night);
        QCOMPARE(session.Waveforms.size(), kG3xPacketsPerNight);
        QCOMPARE(session.Waveforms.first().Timestamp, nightStart(night));
    }
    QVERIFY(restored.UnreadableNights().isEmpty());
}

void BmcArchiveTests::testG3xFileClasses()
{
    QTemporaryDir dir;
    QVERIFY(!makeG3xCard(dir.path(), kG3xNights).isEmpty());
    QVERIFY(writeFile(dir.filePath("nP3-35.ENG"), "language pack"));
    BmcG3xData card(dir.path());
    card.ReadData();

    QCOMPARE(card.BaseName(), kG3xBase);
    QVERIFY(card.IsRingFileName(kG3xBase + ".000"));
    QVERIFY(card.IsRingFileName(kG3xBase + ".062"));
    QVERIFY(!card.IsRingFileName(kG3xBase + ".idx"));
    QVERIFY(!card.IsRingFileName(kG3xBase + ".evt"));
    QVERIFY(!card.IsRingFileName("nP3-35.ENG"));
    QVERIFY(!card.IsRingFileName("other.000"));

    QStringList names = card.IndexFileNames();
    names.sort();
    QCOMPARE(names, QStringList() << kG3xBase + ".evt" << kG3xBase + ".idx");

    QDate first, last;
    QVERIFY(card.IndexDateRange(&first, &last));
    QCOMPARE(first, kG3xNights.first());
    QCOMPARE(last, kG3xNights.last());
}

// ---------------------------------------------------------------------------------
// Capturing a card into an archive (BmcCaptureToArchive)
// ---------------------------------------------------------------------------------

namespace {

enum class Kind { Legacy, G3x };

//! Date of synthetic night \a n (0-based).
QDate cardNight(int n)
{
    return QDate(2026, 3, 10).addDays(n);
}

QList<QDate> cardNights(int count)
{
    QList<QDate> nights;
    for (int n = 0; n < count; ++n) { nights.append(cardNight(n)); }
    return nights;
}

//! Write (or extend) a synthetic card of \a kind holding \a count nights into \a dir.
bool makeCard(Kind kind, const QString &dir, int count)
{
    if (kind == Kind::Legacy) {
        return makeLegacyCard(dir, cardNights(count), cardNight(count));
    }
    return !makeG3xCard(dir, cardNights(count)).isEmpty();
}

std::unique_ptr<BmcDataParser> makeParser(Kind kind, const QString &dir)
{
    if (kind == Kind::Legacy) { return std::make_unique<BmcData>(dir); }
    return std::make_unique<BmcG3xData>(dir);
}

QString baseOf(Kind kind)
{
    return kind == Kind::Legacy ? kLegacyBase : kG3xBase;
}

int packetsPerNight(Kind kind)
{
    return kind == Kind::Legacy ? kLegacyPacketsPerNight : kG3xPacketsPerNight;
}

//! Capture the card in \a cardDir into the archive in \a archiveDir.
//! \param total if non-null, receives the number of records the capture chose to read
BmcCaptureStats captureCard(Kind kind, const QString &cardDir, const QString &archiveDir, int *total = nullptr)
{
    std::unique_ptr<BmcDataParser> parser = makeParser(kind, cardDir);
    parser->ReadData();
    CardArchive archive(archiveDir);
    int seenTotal = 0;
    const BmcCaptureStats stats = BmcCaptureToArchive(*parser, cardDir, archive, "BMC",
        [&seenTotal](int, int totalRecords) { seenTotal = totalRecords; }, nullptr);
    if (total != nullptr) { *total = seenTotal; }
    return stats;
}

QList<CardArchiveEntry> archiveEntries(const QString &archiveDir)
{
    CardArchive archive(archiveDir);
    archive.load();
    return archive.entries();
}

//! The hex checksums of an archive's segments, sorted.
QStringList archiveChecksums(const QString &archiveDir)
{
    QStringList sums;
    for (const CardArchiveEntry &entry : archiveEntries(archiveDir)) {
        sums.append(QString::fromLatin1(entry.sha256.toHex()));
    }
    sums.sort();
    return sums;
}

//! Every file under \a dir, as "relative/path=hex of content", sorted.
QStringList folderSnapshot(const QString &dir)
{
    QStringList lines;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        lines.append(QDir(dir).relativeFilePath(path) + "=" + QString::fromLatin1(readFile(path).toHex()));
    }
    lines.sort();
    return lines;
}

bool hasTempFiles(const QString &dir)
{
    QDirIterator it(dir, QStringList() << "*.tmp", QDir::Files, QDirIterator::Subdirectories);
    return it.hasNext();
}

//! A legacy parser whose range logic has, as far as the archive is concerned, been revised.
class RevisedLegacyParser : public BmcData
{
public:
    using BmcData::BmcData;
    QList<BmcArchiveRecord> ArchiveRecords() override
    {
        QList<BmcArchiveRecord> records = BmcData::ArchiveRecords();
        for (BmcArchiveRecord &record : records) { record.info.rule = kBmcLegacyCaptureRule + 1; }
        return records;
    }
};

} // namespace

void BmcArchiveTests::testCaptureFresh()
{
    for (const Kind kind : { Kind::Legacy, Kind::G3x }) {
        QTemporaryDir cardDir;
        QTemporaryDir archiveDir;
        QVERIFY(makeCard(kind, cardDir.path(), 3));
        QVERIFY(writeFile(cardDir.filePath("lang.BIN"), "language pack"));

        int total = -1;
        const BmcCaptureStats stats = captureCard(kind, cardDir.path(), archiveDir.path(), &total);
        QCOMPARE(stats.stored, 3);
        QCOMPARE(stats.replaced, 0);
        QCOMPARE(stats.failed, 0);
        QVERIFY(stats.ok());
        QVERIFY(total >= 3);

        // Index files and the card's other file are there; the ring file is not.
        std::unique_ptr<BmcDataParser> card = makeParser(kind, cardDir.path());
        card->ReadData();
        const QStringList indexNames = card->IndexFileNames();
        QVERIFY(indexNames.size() >= 2);
        for (const QString &name : indexNames) {
            QCOMPARE(readFile(archiveDir.filePath(name)), readFile(cardDir.filePath(name)));
        }
        QCOMPARE(readFile(archiveDir.filePath("lang.BIN")), QByteArray("language pack"));
        QVERIFY(QFile::exists(cardDir.filePath(baseOf(kind) + ".000")));
        QVERIFY(!QFile::exists(archiveDir.filePath(baseOf(kind) + ".000")));
        QCOMPARE(QDir(archiveDir.filePath(CardArchive::segmentDirName()))
                     .entryList(QStringList() << "*.seg.gz", QDir::Files).size(), 3);

        // The archive is a complete source for an import.
        CardArchive archive(archiveDir.path());
        QVERIFY(archive.load());
        QCOMPARE(archive.entries().size(), 3);
        QCOMPARE(archive.baseName(), baseOf(kind));
        std::unique_ptr<BmcDataParser> restored = makeParser(kind, archiveDir.path());
        restored->SetArchive(&archive);
        restored->ReadData();
        QCOMPARE(restored->ReadDateSession(cardNight(1)).Waveforms.size(), packetsPerNight(kind));
    }
}

void BmcArchiveTests::testCaptureSkipsSystemFolders()
{
    // Folders the operating system keeps on a removable drive are not card data, and on a
    // real card they may not even be readable.
    QTemporaryDir cardDir;
    QTemporaryDir archiveDir;
    QVERIFY(makeCard(Kind::G3x, cardDir.path(), 3));
    QVERIFY(QDir(cardDir.path()).mkpath("System Volume Information"));
    QVERIFY(writeFile(cardDir.filePath("System Volume Information/IndexerVolumeGuid"), "guid"));
    QVERIFY(QDir(cardDir.path()).mkpath("$RECYCLE.BIN/S-1-5-21"));
    QVERIFY(writeFile(cardDir.filePath("$RECYCLE.BIN/S-1-5-21/desktop.ini"), "ini"));
    QVERIFY(QDir(cardDir.path()).mkpath("Firmware"));
    QVERIFY(writeFile(cardDir.filePath("Firmware/update.bin"), "kept"));

    const BmcCaptureStats stats = captureCard(Kind::G3x, cardDir.path(), archiveDir.path());
    QVERIFY(stats.ok());
    QVERIFY(!QDir(archiveDir.filePath("System Volume Information")).exists());
    QVERIFY(!QDir(archiveDir.filePath("$RECYCLE.BIN")).exists());
    QCOMPARE(readFile(archiveDir.filePath("Firmware/update.bin")), QByteArray("kept"));
}

void BmcArchiveTests::testCaptureAgainReadsNothing()
{
    for (const Kind kind : { Kind::Legacy, Kind::G3x }) {
        QTemporaryDir cardDir;
        QTemporaryDir archiveDir;
        QVERIFY(makeCard(kind, cardDir.path(), 3));
        QCOMPARE(captureCard(kind, cardDir.path(), archiveDir.path()).stored, 3);
        const QStringList snapshot = folderSnapshot(archiveDir.filePath(CardArchive::segmentDirName()));

        int total = -1;
        const BmcCaptureStats again = captureCard(kind, cardDir.path(), archiveDir.path(), &total);
        QCOMPARE(again.stored, 0);
        QCOMPARE(again.replaced, 0);
        QCOMPARE(again.failed, 0);
        QVERIFY(again.ok());
        // Only what could still have changed is read: the newest night, and on a legacy
        // card also the oldest, which may be a tail that an older copy could complete.
        QVERIFY2(total <= (kind == Kind::Legacy ? 2 : 1), qPrintable(QString::number(total)));
        QCOMPARE(folderSnapshot(archiveDir.filePath(CardArchive::segmentDirName())), snapshot);
    }
}

void BmcArchiveTests::testCaptureNewNight()
{
    for (const Kind kind : { Kind::Legacy, Kind::G3x }) {
        QTemporaryDir cardDir;
        QTemporaryDir archiveDir;
        QVERIFY(makeCard(kind, cardDir.path(), 3));
        QCOMPARE(captureCard(kind, cardDir.path(), archiveDir.path()).stored, 3);

        const QString segments = archiveDir.filePath(CardArchive::segmentDirName());
        const QList<CardArchiveEntry> before = archiveEntries(archiveDir.path());
        const QDateTime written0 = QFileInfo(segments + "/" + before.at(0).file).lastModified();
        const QDateTime written1 = QFileInfo(segments + "/" + before.at(1).file).lastModified();

        // The next day the card holds one more night.
        QVERIFY(makeCard(kind, cardDir.path(), 4));
        const BmcCaptureStats stats = captureCard(kind, cardDir.path(), archiveDir.path());
        QCOMPARE(stats.stored, 1);
        QCOMPARE(stats.failed, 0);

        const QList<CardArchiveEntry> after = archiveEntries(archiveDir.path());
        QCOMPARE(after.size(), 4);
        QCOMPARE(after.at(3).date, cardNight(3));
        QCOMPARE(after.at(3).packets, packetsPerNight(kind));
        // What was the newest night is now closed, with the same content.
        QVERIFY(after.at(2).final);
        QCOMPARE(after.at(2).sha256, before.at(2).sha256);
        // The older nights were not touched.
        QCOMPARE(after.at(0).sha256, before.at(0).sha256);
        QCOMPARE(after.at(1).sha256, before.at(1).sha256);
        QCOMPARE(QFileInfo(segments + "/" + after.at(0).file).lastModified(), written0);
        QCOMPARE(QFileInfo(segments + "/" + after.at(1).file).lastModified(), written1);
    }
}

void BmcArchiveTests::testUnionEitherOrder()
{
    // Card A holds nights 0-2. Card B is the same device later: nights 3 and 4 have
    // overwritten nights 0 and 1 in the ring, night 2 survives, and the index lists all five.
    QTemporaryDir cardA;
    QTemporaryDir cardB;
    QVERIFY(makeCard(Kind::G3x, cardA.path(), 3));

    const quint32 night = static_cast<quint32>(kG3xPacketsPerNight * kG3xPacket);
    QList<G3xNight> indexed;
    indexed.append({ cardNight(0), 0, night, night });
    indexed.append({ cardNight(1), night, 2 * night, night });
    indexed.append({ cardNight(2), 2 * night, 3 * night, night });
    indexed.append({ cardNight(3), 0, night, night });
    indexed.append({ cardNight(4), night, 2 * night, night });
    QVERIFY(writeRing(cardB.path(), 0, 0, g3xRun(nightStart(cardNight(3)), kG3xPacketsPerNight)));
    QVERIFY(writeRing(cardB.path(), 0, night, g3xRun(nightStart(cardNight(4)), kG3xPacketsPerNight)));
    QVERIFY(writeRing(cardB.path(), 0, 2 * night, g3xRun(nightStart(cardNight(2)), kG3xPacketsPerNight)));
    QVERIFY(makeG3xIndex(cardB.path(), indexed, 63));

    QTemporaryDir aThenB;
    QCOMPARE(captureCard(Kind::G3x, cardA.path(), aThenB.path()).stored, 3);
    const BmcCaptureStats second = captureCard(Kind::G3x, cardB.path(), aThenB.path());
    QCOMPARE(second.stored, 2);
    QVERIFY(second.ok());

    QTemporaryDir bThenA;
    QCOMPARE(captureCard(Kind::G3x, cardB.path(), bThenA.path()).stored, 3);
    const BmcCaptureStats older = captureCard(Kind::G3x, cardA.path(), bThenA.path());
    QCOMPARE(older.stored, 2);
    QVERIFY(older.ok());

    QCOMPARE(archiveEntries(aThenB.path()).size(), 5);
    QCOMPARE(archiveEntries(bThenA.path()).size(), 5);
    QCOMPARE(archiveChecksums(aThenB.path()), archiveChecksums(bThenA.path()));
    for (int n = 0; n < 5; ++n) {
        QCOMPARE(archiveEntries(aThenB.path()).at(n).date, cardNight(n));
        QCOMPARE(archiveEntries(bThenA.path()).at(n).date, cardNight(n));
    }

    // Either way the archive ends with the newer card's index, which lists all five nights.
    QCOMPARE(readFile(aThenB.filePath(kG3xBase + ".idx")), readFile(cardB.filePath(kG3xBase + ".idx")));
    QCOMPARE(readFile(bThenA.filePath(kG3xBase + ".idx")), readFile(cardB.filePath(kG3xBase + ".idx")));
    CardArchive archive(bThenA.path());
    QVERIFY(archive.load());
    QVERIFY(archive.generationDirs().isEmpty());
    BmcG3xData restored(bThenA.path());
    restored.SetArchive(&archive);
    restored.ReadData();
    for (int n = 0; n < 5; ++n) {
        QCOMPARE(restored.ReadDateSession(cardNight(n)).Waveforms.size(), kG3xPacketsPerNight);
    }
}

void BmcArchiveTests::testMigrationSource()
{
    // The Backup folder as earlier versions left it: a full copy of the card.
    QTemporaryDir dir;
    QVERIFY(makeCard(Kind::Legacy, dir.path(), 3));
    const QByteArray usrBefore = readFile(dir.filePath(kLegacyBase + ".USR"));
    const QByteArray idxBefore = readFile(dir.filePath(kLegacyBase + ".idx"));

    BmcData mirror(dir.path());
    mirror.ReadData();
    CardArchive archive(dir.path());
    const BmcCaptureStats stats = BmcCaptureToArchive(mirror, dir.path(), archive, "BMC", nullptr, nullptr);
    QCOMPARE(stats.stored, 3);
    QVERIFY(stats.ok());
    QVERIFY(!hasTempFiles(dir.path()));

    QCOMPARE(BmcRemoveRingFiles(mirror, dir.path()), 1);
    QVERIFY(!QFile::exists(dir.filePath(kLegacyBase + ".000")));
    QCOMPARE(readFile(dir.filePath(kLegacyBase + ".USR")), usrBefore);
    QCOMPARE(readFile(dir.filePath(kLegacyBase + ".idx")), idxBefore);
    QCOMPARE(BmcRemoveRingFiles(mirror, dir.path()), 0);

    // The folder is still this device's data, now read through the archive.
    QVERIFY(BmcData::DirectoryHasBmcData(dir.path()));
    CardArchive converted(dir.path());
    QVERIFY(converted.load());
    QCOMPARE(converted.entries().size(), 3);
    BmcData restored(dir.path());
    restored.SetArchive(&converted);
    restored.ReadData();
    QCOMPARE(restored.ReadDateSession(cardNight(1)).Waveforms.size(), kLegacyPacketsPerNight);
}

void BmcArchiveTests::testArchiveToArchive()
{
    QTemporaryDir cardDir;
    QTemporaryDir first;
    QTemporaryDir second;
    QVERIFY(makeCard(Kind::Legacy, cardDir.path(), 3));
    QVERIFY(writeFile(cardDir.filePath("lang.BIN"), "language pack"));
    QCOMPARE(captureCard(Kind::Legacy, cardDir.path(), first.path()).stored, 3);
    const QStringList firstBefore = folderSnapshot(first.path());

    // Import pointed at a copy of an archive that is not the device's own Backup folder.
    CardArchive source(first.path());
    QVERIFY(source.load());
    source.setReadOnly(true);
    BmcData parser(first.path());
    parser.SetArchive(&source);
    parser.ReadData();
    CardArchive target(second.path());
    const BmcCaptureStats stats = BmcCaptureToArchive(parser, first.path(), target, "BMC", nullptr, nullptr);
    QCOMPARE(stats.stored, 3);
    QVERIFY(stats.ok());

    QCOMPARE(folderSnapshot(first.path()), firstBefore);        // the source is not modified
    QCOMPARE(archiveChecksums(second.path()), archiveChecksums(first.path()));
    const QList<CardArchiveEntry> from = archiveEntries(first.path());
    const QList<CardArchiveEntry> to = archiveEntries(second.path());
    QCOMPARE(to.size(), 3);
    for (int n = 0; n < 3; ++n) {
        QCOMPARE(to.at(n).date, from.at(n).date);
        QCOMPARE(to.at(n).first, from.at(n).first);
        QCOMPARE(to.at(n).final, from.at(n).final);
        QCOMPARE(to.at(n).complete, from.at(n).complete);
        QCOMPARE(to.at(n).rule, from.at(n).rule);
    }
    QCOMPARE(readFile(second.filePath(kLegacyBase + ".USR")), readFile(cardDir.filePath(kLegacyBase + ".USR")));
    QCOMPARE(readFile(second.filePath("lang.BIN")), QByteArray("language pack"));
    QCOMPARE(QDir(second.filePath(CardArchive::segmentDirName())).entryList(QDir::Files).size(), 3);
}

void BmcArchiveTests::testAbortMidway()
{
    QTemporaryDir cardDir;
    QTemporaryDir archiveDir;
    QVERIFY(makeCard(Kind::G3x, cardDir.path(), 3));

    {
        BmcG3xData card(cardDir.path());
        card.ReadData();
        CardArchive archive(archiveDir.path());
        bool cancel = false;
        const BmcCaptureStats stats = BmcCaptureToArchive(card, cardDir.path(), archive, "BMC",
            [&cancel](int done, int) { if (done >= 1) { cancel = true; } },
            [&cancel]() { return cancel; });
        QVERIFY(stats.aborted);
        QVERIFY(!stats.ok());
        QCOMPARE(stats.stored, 1);
    }
    QCOMPARE(archiveEntries(archiveDir.path()).size(), 1);
    QVERIFY(!hasTempFiles(archiveDir.path()));
    QCOMPARE(QDir(archiveDir.filePath(CardArchive::segmentDirName())).entryList(QDir::Files).size(), 1);

    // The next import finishes the job.
    const BmcCaptureStats rest = captureCard(Kind::G3x, cardDir.path(), archiveDir.path());
    QCOMPARE(rest.stored, 2);
    QVERIFY(rest.ok());
    QCOMPARE(archiveEntries(archiveDir.path()).size(), 3);
}

void BmcArchiveTests::testUnwritableArchive()
{
    QTemporaryDir cardDir;
    QTemporaryDir archiveDir;
    QVERIFY(makeCard(Kind::Legacy, cardDir.path(), 3));
    // An archive written by a later version of OSCAR.
    QVERIFY(writeFile(archiveDir.filePath(CardArchive::manifestName()),
                      "{\"version\":2,\"loader\":\"BMC\",\"base\":\"12345678\",\"segments\":[]}"));
    QVERIFY(writeFile(archiveDir.filePath(kLegacyBase + ".USR"), "their index"));
    const QStringList before = folderSnapshot(archiveDir.path());

    const BmcCaptureStats stats = captureCard(Kind::Legacy, cardDir.path(), archiveDir.path());
    QCOMPARE(stats.failed, 1);
    QCOMPARE(stats.stored, 0);
    QVERIFY(!stats.ok());
    QCOMPARE(folderSnapshot(archiveDir.path()), before);
}

void BmcArchiveTests::testWriteFailure()
{
    QTemporaryDir cardDir;
    QTemporaryDir archiveDir;
    QVERIFY(makeCard(Kind::Legacy, cardDir.path(), 3));
    // Something is in the way of the segment folder, so no segment can be written.
    QVERIFY(writeFile(archiveDir.filePath(CardArchive::segmentDirName()), "not a folder"));

    const BmcCaptureStats stats = captureCard(Kind::Legacy, cardDir.path(), archiveDir.path());
    QCOMPARE(stats.failed, 3);
    QCOMPARE(stats.stored, 0);
    QVERIFY(!stats.ok());
    QVERIFY(archiveEntries(archiveDir.path()).isEmpty());
    QVERIFY(!hasTempFiles(archiveDir.path()));
    QCOMPARE(readFile(archiveDir.filePath(CardArchive::segmentDirName())), QByteArray("not a folder"));
}

void BmcArchiveTests::testRuleBumpRecaptures()
{
    QTemporaryDir cardDir;
    QTemporaryDir archiveDir;
    QVERIFY(makeCard(Kind::Legacy, cardDir.path(), 3));
    QCOMPARE(captureCard(Kind::Legacy, cardDir.path(), archiveDir.path()).stored, 3);
    for (const CardArchiveEntry &entry : archiveEntries(archiveDir.path())) {
        QCOMPARE(entry.rule, kBmcLegacyCaptureRule);
    }
    const QStringList checksums = archiveChecksums(archiveDir.path());

    // A parser with a newer capture rule looks at every night again, complete or not.
    const auto captureRevised = [&](int *total) {
        RevisedLegacyParser revised(cardDir.path());
        revised.ReadData();
        CardArchive archive(archiveDir.path());
        return BmcCaptureToArchive(revised, cardDir.path(), archive, "BMC",
                                   [total](int, int totalRecords) { *total = totalRecords; }, nullptr);
    };
    int total = -1;
    const BmcCaptureStats stats = captureRevised(&total);
    QCOMPARE(total, 4);                 // three nights and the in-progress session
    QCOMPARE(stats.failed, 0);
    QVERIFY(stats.ok());
    // The revised rule found the same packets, so nothing was rewritten, and each night
    // is now on record as captured under it.
    for (const CardArchiveEntry &entry : archiveEntries(archiveDir.path())) {
        QCOMPARE(entry.rule, kBmcLegacyCaptureRule + 1);
    }
    QCOMPARE(archiveChecksums(archiveDir.path()), checksums);

    // After that it is back to reading only what could still change.
    total = -1;
    QVERIFY(captureRevised(&total).ok());
    QVERIFY2(total <= 2, qPrintable(QString::number(total)));
}

void BmcArchiveTests::testReadCheck()
{
    for (const Kind kind : { Kind::Legacy, Kind::G3x }) {
        QTemporaryDir cardDir;
        QTemporaryDir archiveDir;
        QVERIFY(makeCard(kind, cardDir.path(), 3));
        QCOMPARE(captureCard(kind, cardDir.path(), archiveDir.path()).stored, 3);

        const auto importAll = [&](CardArchive &archive) {
            std::unique_ptr<BmcDataParser> parser = makeParser(kind, archiveDir.path());
            parser->SetArchive(&archive);
            parser->ReadData();
            for (const BmcDataLink &link : parser->GetSessionLinks()) {
                parser->ReadDateSession(link.UsrSession.StartTimestamp.date());
            }
            return BmcArchiveReadCheck(QList<BmcDataParser *>() << parser.get(), archive);
        };

        // Everything the archive holds is imported.
        {
            CardArchive archive(archiveDir.path());
            QVERIFY(archive.load());
            const BmcArchiveReadReport report = importAll(archive);
            QVERIFY(report.clean());
        }

        // One segment is damaged, and one belongs to a night the index does not list.
        const QString damaged = archiveDir.filePath(CardArchive::segmentDirName() + "/" + baseOf(kind)
                                                    + "_20260311.seg.gz");
        QVERIFY(QFile::exists(damaged));
        QVERIFY(writeFile(damaged, "damaged"));
        const QDate stray(2026, 5, 1);
        {
            CardArchive archive(archiveDir.path());
            QVERIFY(archive.load());
            CardArchiveRecord rec;
            rec.date = stray;
            rec.first = nightStart(stray);
            rec.last = nightStart(stray).addSecs(3);
            rec.final = true;
            const int size = (kind == Kind::Legacy) ? kLegacyPacket : kG3xPacket;
            QCOMPARE(archive.store(rec, QByteArray(4 * size, 'x'), size), CardArchive::StoreResult::Stored);
        }

        CardArchive archive(archiveDir.path());
        QVERIFY(archive.load());
        const BmcArchiveReadReport report = importAll(archive);
        QVERIFY(!report.clean());
        QCOMPARE(report.unreadable, QList<QDate>() << cardNight(1));
        QCOMPARE(report.unclaimed, QList<QDate>() << stray);
    }
}

// ---------------------------------------------------------------------------------
// What the loaders' Open() does around the import (BmcOpenImportSource and friends)
// ---------------------------------------------------------------------------------

namespace {

BmcParserFactory factoryFor(Kind kind)
{
    return [kind](const QString &dir) { return makeParser(kind, dir); };
}

//! Stand-in for a loader's Detect(): does \a dir hold this kind of card?
std::function<bool(const QString &)> detectFor(Kind kind)
{
    return [kind](const QString &dir) {
        if (kind == Kind::Legacy) { return BmcData::DirectoryHasBmcData(dir); }
        return !BmcData::DirectoryHasBmcData(dir) && BmcG3xData::DirectoryHasBmcG3xData(dir);
    };
}

//! Read every night the source's parsers describe, as the import tasks do.
void importEverything(BmcImportSource &source)
{
    for (const std::unique_ptr<BmcDataParser> &parser : source.parsers) {
        for (const BmcDataLink &link : parser->GetSessionLinks()) {
            parser->ReadDateSession(link.UsrSession.StartTimestamp.date());
        }
    }
}

} // namespace

void BmcArchiveTests::testOpenImportSourceCard()
{
    for (const Kind kind : { Kind::Legacy, Kind::G3x }) {
        QTemporaryDir cardDir;
        QVERIFY(makeCard(kind, cardDir.path(), 3));

        BmcImportSource source = BmcOpenImportSource(cardDir.path(), false, factoryFor(kind));
        QVERIFY(!source.isArchive);
        QVERIFY(source.archive == nullptr);
        QCOMPARE(source.parsers.size(), size_t(1));
        QCOMPARE(source.dirs.size(), 1);
        // The parser is ready to import from.
        QVERIFY(source.parsers.front()->GetSessionLinks().size() >= 3);
        QCOMPARE(source.parsers.front()->ReadDateSession(cardNight(1)).Waveforms.size(), packetsPerNight(kind));
    }
}

void BmcArchiveTests::testOpenImportSourceArchiveWithGenerations()
{
    // The device's archive holds nights 0-2. Then its index is restarted: the next card
    // lists only nights 5 and 6, so the earlier index set is retired, not replaced.
    QTemporaryDir firstCard;
    QTemporaryDir laterCard;
    QTemporaryDir archiveDir;
    QVERIFY(makeCard(Kind::Legacy, firstCard.path(), 3));
    QVERIFY(makeLegacyCard(laterCard.path(), QList<QDate>() << cardNight(5) << cardNight(6), cardNight(7)));
    QCOMPARE(captureCard(Kind::Legacy, firstCard.path(), archiveDir.path()).stored, 3);
    QCOMPARE(captureCard(Kind::Legacy, laterCard.path(), archiveDir.path()).stored, 2);

    BmcImportSource source = BmcOpenImportSource(archiveDir.path(), true, factoryFor(Kind::Legacy));
    QVERIFY(source.isArchive);
    QVERIFY(source.archive != nullptr);
    QVERIFY(source.archive->isWritable());              // the device's own backup
    QCOMPARE(source.archive->entries().size(), 5);
    QCOMPARE(source.parsers.size(), size_t(2));         // current index set, then the retired one
    QCOMPARE(source.dirs.size(), 2);
    QCOMPARE(QDir(source.dirs.at(0)), QDir(archiveDir.path()));
    QVERIFY(source.dirs.at(1).contains(CardArchive::generationDirName()));

    // Between them the two index sets describe all five nights, each with its waveform.
    QCOMPARE(source.parsers.at(0)->ReadDateSession(cardNight(5)).Waveforms.size(), kLegacyPacketsPerNight);
    QCOMPARE(source.parsers.at(0)->ReadDateSession(cardNight(6)).Waveforms.size(), kLegacyPacketsPerNight);
    for (int n = 0; n < 3; ++n) {
        QCOMPARE(source.parsers.at(1)->ReadDateSession(cardNight(n)).Waveforms.size(), kLegacyPacketsPerNight);
    }
    importEverything(source);
    QVERIFY(BmcFinishArchiveImport(source).clean());

    // Someone else's archive, or a copy elsewhere, is opened read-only.
    BmcImportSource foreign = BmcOpenImportSource(archiveDir.path(), false, factoryFor(Kind::Legacy));
    QVERIFY(foreign.isArchive);
    QVERIFY(!foreign.archive->isWritable());
    QCOMPARE(foreign.parsers.size(), size_t(2));
}

void BmcArchiveTests::testBackUpMigratesThenCaptures()
{
    for (const Kind kind : { Kind::Legacy, Kind::G3x }) {
        // The Backup folder as an earlier version left it: a full copy of the card when
        // it held three nights. The card being imported now holds four.
        QTemporaryDir backupDir;
        QTemporaryDir cardDir;
        QVERIFY(makeCard(kind, backupDir.path(), 3));
        QVERIFY(makeCard(kind, cardDir.path(), 4));
        QVERIFY(!CardArchive::isArchive(backupDir.path()));

        BmcImportSource source = BmcOpenImportSource(cardDir.path(), false, factoryFor(kind));
        int progressCalls = 0;
        const BmcBackupOutcome outcome = BmcBackUpImportSource(source, backupDir.path(), factoryFor(kind),
            detectFor(kind), "BMC", [&progressCalls](int, int) { ++progressCalls; }, nullptr);

        QVERIFY(outcome.migrated);
        QCOMPARE(outcome.migration.stored, 3);
        QCOMPARE(outcome.capture.stored, 1);
        QCOMPARE(outcome.ringFilesRemoved, 1);
        QVERIFY(outcome.ok());
        QVERIFY(progressCalls > 0);

        QVERIFY(!QFile::exists(backupDir.filePath(baseOf(kind) + ".000")));
        QCOMPARE(archiveEntries(backupDir.path()).size(), 4);
        QVERIFY(!hasTempFiles(backupDir.path()));
        // The index is the card's, which lists all four nights.
        QCOMPARE(readFile(backupDir.filePath(baseOf(kind) + ".idx")), readFile(cardDir.filePath(baseOf(kind) + ".idx")));

        // A rebuild from the Backup folder now finds every night.
        BmcImportSource rebuilt = BmcOpenImportSource(backupDir.path(), true, factoryFor(kind));
        QVERIFY(rebuilt.isArchive);
        for (int n = 0; n < 4; ++n) {
            QCOMPARE(rebuilt.parsers.front()->ReadDateSession(cardNight(n)).Waveforms.size(), packetsPerNight(kind));
        }

        // The next import finds nothing to convert and nothing new.
        BmcImportSource again = BmcOpenImportSource(cardDir.path(), false, factoryFor(kind));
        const BmcBackupOutcome second = BmcBackUpImportSource(again, backupDir.path(), factoryFor(kind),
                                                              detectFor(kind), "BMC", nullptr, nullptr);
        QVERIFY(!second.migrated);
        QCOMPARE(second.capture.stored, 0);
        QCOMPARE(second.ringFilesRemoved, 0);
        QVERIFY(second.ok());
    }
}

void BmcArchiveTests::testBackUpKeepsMirrorOnFailure()
{
    // The Backup folder cannot take segments. The old mirror must survive untouched.
    QTemporaryDir backupDir;
    QTemporaryDir cardDir;
    QVERIFY(makeCard(Kind::Legacy, backupDir.path(), 3));
    QVERIFY(makeCard(Kind::Legacy, cardDir.path(), 4));
    QVERIFY(writeFile(backupDir.filePath(CardArchive::segmentDirName()), "not a folder"));
    const QByteArray ringBefore = readFile(backupDir.filePath(kLegacyBase + ".000"));
    QVERIFY(!ringBefore.isEmpty());

    BmcImportSource source = BmcOpenImportSource(cardDir.path(), false, factoryFor(Kind::Legacy));
    const BmcBackupOutcome outcome = BmcBackUpImportSource(source, backupDir.path(), factoryFor(Kind::Legacy),
                                                           detectFor(Kind::Legacy), "BMC", nullptr, nullptr);
    QVERIFY(outcome.migrated);
    QVERIFY(!outcome.ok());
    QVERIFY(outcome.migration.failed > 0);
    QCOMPARE(outcome.capture.stored, 0);                // the card is held back until the copy is safe
    QCOMPARE(outcome.ringFilesRemoved, 0);
    QCOMPARE(readFile(backupDir.filePath(kLegacyBase + ".000")), ringBefore);
    QVERIFY(archiveEntries(backupDir.path()).isEmpty());

    // The folder is still read as the card copy it is, so a rebuild loses nothing.
    BmcImportSource rebuild = BmcOpenImportSource(backupDir.path(), true, factoryFor(Kind::Legacy));
    QVERIFY(!rebuild.isArchive);
    QCOMPARE(rebuild.parsers.size(), size_t(1));
    for (int n = 0; n < 3; ++n) {
        QCOMPARE(rebuild.parsers.front()->ReadDateSession(cardNight(n)).Waveforms.size(), kLegacyPacketsPerNight);
    }
}

void BmcArchiveTests::testBackUpKeepsMirrorOnAbort()
{
    QTemporaryDir backupDir;
    QTemporaryDir cardDir;
    QVERIFY(makeCard(Kind::G3x, backupDir.path(), 3));
    QVERIFY(makeCard(Kind::G3x, cardDir.path(), 4));

    BmcImportSource source = BmcOpenImportSource(cardDir.path(), false, factoryFor(Kind::G3x));
    bool cancel = false;
    const BmcBackupOutcome outcome = BmcBackUpImportSource(source, backupDir.path(), factoryFor(Kind::G3x),
        detectFor(Kind::G3x), "BMC",
        [&cancel](int done, int) { if (done >= 2) { cancel = true; } },
        [&cancel]() { return cancel; });

    QVERIFY(!outcome.ok());
    QVERIFY(outcome.migration.aborted);
    QCOMPARE(outcome.capture.stored, 0);                // the card was not started
    QCOMPARE(outcome.ringFilesRemoved, 0);
    QVERIFY(QFile::exists(backupDir.filePath(kG3xBase + ".000")));
    QCOMPARE(archiveEntries(backupDir.path()).size(), 2);
    QVERIFY(!hasTempFiles(backupDir.path()));

    // Part-way through, with a manifest and the ring file side by side, the folder is
    // still read as the card copy, which holds all three of its nights.
    QVERIFY(CardArchive::isArchive(backupDir.path()));
    {
        BmcImportSource rebuild = BmcOpenImportSource(backupDir.path(), true, factoryFor(Kind::G3x));
        QVERIFY(!rebuild.isArchive);
        for (int n = 0; n < 3; ++n) {
            QCOMPARE(rebuild.parsers.front()->ReadDateSession(cardNight(n)).Waveforms.size(), kG3xPacketsPerNight);
        }
    }

    // The next import completes the conversion and then removes the ring file.
    BmcImportSource again = BmcOpenImportSource(cardDir.path(), false, factoryFor(Kind::G3x));
    const BmcBackupOutcome second = BmcBackUpImportSource(again, backupDir.path(), factoryFor(Kind::G3x),
                                                          detectFor(Kind::G3x), "BMC", nullptr, nullptr);
    QVERIFY(second.ok());
    QCOMPARE(second.ringFilesRemoved, 1);
    QCOMPARE(archiveEntries(backupDir.path()).size(), 4);
}

void BmcArchiveTests::testBackUpFromForeignArchive()
{
    // Import pointed at a copy of an archive that is not the device's own Backup folder:
    // the copy is not modified, and the device's own archive gains its nights.
    QTemporaryDir cardDir;
    QTemporaryDir copyDir;
    QTemporaryDir backupDir;
    QVERIFY(makeCard(Kind::G3x, cardDir.path(), 3));
    QCOMPARE(captureCard(Kind::G3x, cardDir.path(), copyDir.path()).stored, 3);
    const QStringList copyBefore = folderSnapshot(copyDir.path());

    BmcImportSource source = BmcOpenImportSource(copyDir.path(), false, factoryFor(Kind::G3x));
    QVERIFY(source.isArchive);
    importEverything(source);
    QVERIFY(BmcFinishArchiveImport(source).clean());
    const BmcBackupOutcome outcome = BmcBackUpImportSource(source, backupDir.path(), factoryFor(Kind::G3x),
                                                           detectFor(Kind::G3x), "BMC", nullptr, nullptr);
    QVERIFY(!outcome.migrated);
    QCOMPARE(outcome.capture.stored, 3);
    QVERIFY(outcome.ok());

    QCOMPARE(folderSnapshot(copyDir.path()), copyBefore);
    QCOMPARE(archiveChecksums(backupDir.path()), archiveChecksums(copyDir.path()));
    QCOMPARE(readFile(backupDir.filePath(kG3xBase + ".idx")), readFile(cardDir.filePath(kG3xBase + ".idx")));
}

void BmcArchiveTests::testFinishArchiveImportDropsUnreadable()
{
    for (const Kind kind : { Kind::Legacy, Kind::G3x }) {
        QTemporaryDir cardDir;
        QTemporaryDir backupDir;
        QVERIFY(makeCard(kind, cardDir.path(), 3));
        QCOMPARE(captureCard(kind, cardDir.path(), backupDir.path()).stored, 3);
        const QString damaged = backupDir.filePath(CardArchive::segmentDirName() + "/" + baseOf(kind)
                                                   + "_20260311.seg.gz");
        QVERIFY(writeFile(damaged, "damaged"));

        // A copy elsewhere is reported on but left as it is.
        {
            BmcImportSource foreign = BmcOpenImportSource(backupDir.path(), false, factoryFor(kind));
            importEverything(foreign);
            const BmcArchiveReadReport report = BmcFinishArchiveImport(foreign);
            QCOMPARE(report.unreadable, QList<QDate>() << cardNight(1));
            QVERIFY(QFile::exists(damaged));
            QCOMPARE(archiveEntries(backupDir.path()).size(), 3);
        }

        // In the device's own backup the damaged segment is dropped ...
        {
            BmcImportSource own = BmcOpenImportSource(backupDir.path(), true, factoryFor(kind));
            importEverything(own);
            const BmcArchiveReadReport report = BmcFinishArchiveImport(own);
            QCOMPARE(report.unreadable, QList<QDate>() << cardNight(1));
            QVERIFY(report.unclaimed.isEmpty());
        }
        // ... by setting it aside, not by deleting it.
        QVERIFY(!QFile::exists(damaged));
        QCOMPARE(readFile(backupDir.filePath(CardArchive::segmentDirName() + "/" + CardArchive::damagedDirName()
                                             + "/" + baseOf(kind) + "_20260311.seg.gz")),
                 QByteArray("damaged"));
        QCOMPARE(archiveEntries(backupDir.path()).size(), 2);

        // ... so the next import of a card that still holds the night stores it again.
        const BmcCaptureStats next = captureCard(kind, cardDir.path(), backupDir.path());
        QCOMPARE(next.stored, 1);
        QCOMPARE(archiveEntries(backupDir.path()).size(), 3);
        BmcImportSource healed = BmcOpenImportSource(backupDir.path(), true, factoryFor(kind));
        importEverything(healed);
        QVERIFY(BmcFinishArchiveImport(healed).clean());
    }
}

void BmcArchiveTests::testReadWarningText()
{
    BmcArchiveReadReport report;
    QVERIFY(BmcArchiveReadWarning(report).isEmpty());

    report.unreadable << QDate(2026, 3, 11);
    QString text = BmcArchiveReadWarning(report);
    QVERIFY(text.contains(QLocale().toString(QDate(2026, 3, 11), QLocale::ShortFormat)));
    QVERIFY(text.contains("could not be read"));
    QVERIFY(text.contains("rebuild again"));
    QVERIFY(!text.contains("no matching index record"));

    report.unclaimed << QDate(2026, 5, 1);
    text = BmcArchiveReadWarning(report);
    QVERIFY(text.contains("no matching index record"));
    QVERIFY(text.contains(QLocale().toString(QDate(2026, 5, 1), QLocale::ShortFormat)));

    // A long list is cut at ten dates.
    BmcArchiveReadReport many;
    for (int n = 0; n < 12; ++n) { many.unreadable << QDate(2026, 3, 1).addDays(n); }
    text = BmcArchiveReadWarning(many);
    QVERIFY(text.contains(QLocale().toString(QDate(2026, 3, 10), QLocale::ShortFormat)));
    QVERIFY(!text.contains(QLocale().toString(QDate(2026, 3, 11), QLocale::ShortFormat)));
    QVERIFY(text.contains("2 more"));
}

// ---------------------------------------------------------------------------------
// What a rebuild from backup could not restore (BmcNightsNotInArchive)
// ---------------------------------------------------------------------------------

namespace {

//! A night as the database holds it: recorded from 22:00 on \a day to 06:00 the next morning.
BmcStoredNight storedNight(const QDate &day)
{
    BmcStoredNight night;
    night.day = day;
    night.first = QDateTime(day, QTime(22, 0));
    night.last = QDateTime(day.addDays(1), QTime(6, 0));
    return night;
}

//! Put a segment for [\a first, \a last] dated \a date into \a archive.
bool addSegment(CardArchive &archive, const QDate &date, const QDateTime &first, const QDateTime &last)
{
    CardArchiveRecord rec;
    rec.date = date;
    rec.first = first;
    rec.last = last;
    rec.final = true;
    return archive.store(rec, QByteArray(4 * kLegacyPacket, 'n'), kLegacyPacket) == CardArchive::StoreResult::Stored;
}

} // namespace

void BmcArchiveTests::testNightsNotInArchive()
{
    QTemporaryDir dir;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");
    const QDate a(2026, 3, 10);
    const QDate b(2026, 3, 11);
    const QDate c(2026, 3, 12);
    QVERIFY(addSegment(archive, a, QDateTime(a, QTime(22, 0)), QDateTime(a.addDays(1), QTime(6, 0))));
    QVERIFY(addSegment(archive, b, QDateTime(b, QTime(23, 0)), QDateTime(b.addDays(1), QTime(5, 0))));

    QList<BmcStoredNight> nights;
    nights << storedNight(a) << storedNight(b) << storedNight(c);
    const QList<BmcStoredNight> missing = BmcNightsNotInArchive(nights, archive);
    QCOMPARE(missing.size(), 1);
    QCOMPARE(missing.first().day, c);

    // A night the archive holds only a minute of still counts as held: the comparison is
    // about whether the backup has that night at all, not how much of it.
    BmcStoredNight brief;
    brief.day = a;
    brief.first = QDateTime(a.addDays(1), QTime(6, 0));
    brief.last = QDateTime(a.addDays(1), QTime(6, 30));
    QVERIFY(BmcNightsNotInArchive(QList<BmcStoredNight>() << brief, archive).isEmpty());

    // An afternoon nap between two archived nights is not covered by either.
    BmcStoredNight nap;
    nap.day = b;
    nap.first = QDateTime(b, QTime(14, 0));
    nap.last = QDateTime(b, QTime(15, 0));
    QCOMPARE(BmcNightsNotInArchive(QList<BmcStoredNight>() << nap, archive).size(), 1);
}

void BmcArchiveTests::testNightsNotInArchiveEmpty()
{
    QTemporaryDir dir;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");

    QList<BmcStoredNight> nights;
    nights << storedNight(QDate(2026, 3, 10)) << storedNight(QDate(2026, 3, 11));
    QCOMPARE(BmcNightsNotInArchive(nights, archive).size(), 2);          // empty archive: none are held
    QVERIFY(BmcNightsNotInArchive(QList<BmcStoredNight>(), archive).isEmpty());   // no nights: nothing to lose
}

void BmcArchiveTests::testRebuildLossText()
{
    QVERIFY(BmcRebuildLossText(QList<BmcStoredNight>()).isEmpty());

    QList<BmcStoredNight> nights;
    nights << storedNight(QDate(2026, 3, 12)) << storedNight(QDate(2026, 3, 10)) << storedNight(QDate(2026, 3, 11));
    const QString text = BmcRebuildLossText(nights);
    QVERIFY(text.contains("3"));
    QVERIFY(text.contains("cannot restore"));
    // The range runs from the earliest to the latest night, whatever order they came in.
    const QString first = QLocale().toString(QDate(2026, 3, 10), QLocale::ShortFormat);
    const QString last = QLocale().toString(QDate(2026, 3, 12), QLocale::ShortFormat);
    QVERIFY(text.contains(first));
    QVERIFY(text.contains(last));
    QVERIFY(text.indexOf(first) < text.indexOf(last));

    QVERIFY(!BmcRebuildCardCopyText().isEmpty());
}

void BmcArchiveTests::testRebuildLossTextFromArchive()
{
    // Used when the device's sessions are not loaded (the upgrade prompt): all that can be
    // said comes from the archive itself.
    QTemporaryDir cardDir;
    QVERIFY(makeCard(Kind::Legacy, cardDir.path(), 3));

    // No index set recorded: nothing can be said.
    {
        QTemporaryDir dir;
        CardArchive archive(dir.path());
        archive.setIdentity("BMC", "base");
        QVERIFY(BmcRebuildLossTextFromArchive(archive).isEmpty());
    }

    // The archive holds every night its index lists: nothing to warn about.
    {
        QTemporaryDir dir;
        QCOMPARE(captureCard(Kind::Legacy, cardDir.path(), dir.path()).stored, 3);
        CardArchive archive(dir.path());
        QVERIFY(archive.load());
        QVERIFY(BmcRebuildLossTextFromArchive(archive).isEmpty());
    }

    // The index goes back further than the segments do.
    {
        QTemporaryDir dir;
        QCOMPARE(captureCard(Kind::Legacy, cardDir.path(), dir.path()).stored, 3);
        CardArchive archive(dir.path());
        QVERIFY(archive.load());
        archive.drop(archive.entries().first());
        const QString text = BmcRebuildLossTextFromArchive(archive);
        QVERIFY(text.contains(QLocale().toString(cardNight(1), QLocale::ShortFormat)));
        QVERIFY(text.contains("Earlier nights"));
    }

    // An index but no segments at all.
    {
        QTemporaryDir dir;
        QCOMPARE(captureCard(Kind::Legacy, cardDir.path(), dir.path()).stored, 3);
        CardArchive archive(dir.path());
        QVERIFY(archive.load());
        while (!archive.entries().isEmpty()) { archive.drop(archive.entries().first()); }
        QVERIFY(BmcRebuildLossTextFromArchive(archive).contains("no detailed"));
    }
}

// ---------------------------------------------------------------------------------
// Read failures, short captures, interrupted conversions, unusable manifests
// ---------------------------------------------------------------------------------

namespace {

//! A legacy parser whose ring files cannot be opened (locked, evicted from a synced
//! folder, on a failing disk): the files exist, but reading them fails.
class UnopenableLegacyParser : public BmcData
{
public:
    using BmcData::BmcData;
protected:
    std::unique_ptr<QIODevice> OpenRingFile(const QString &) const override { return nullptr; }
};

class UnopenableG3xParser : public BmcG3xData
{
public:
    using BmcG3xData::BmcG3xData;
protected:
    std::unique_ptr<QIODevice> OpenRingFile(const QString &) const override { return nullptr; }
};

//! A G3X parser whose ring files read short: the file is whole on disk, but the last
//! five packets cannot be read.
class ShortReadG3xParser : public BmcG3xData
{
public:
    using BmcG3xData::BmcG3xData;
protected:
    std::unique_ptr<QIODevice> OpenRingFile(const QString &path) const override
    {
        QByteArray content = readFile(path);
        content.chop(5 * kG3xPacket);
        auto buffer = std::make_unique<QBuffer>();
        buffer->setData(content);
        if (!buffer->open(QIODevice::ReadOnly)) { return nullptr; }
        return buffer;
    }
};

} // namespace

void BmcArchiveTests::testCaptureCountsUnreadableRingFileAsFailure()
{
    // "Could not read" must never be taken for "there is nothing to read".
    {
        QTemporaryDir cardDir;
        QTemporaryDir archiveDir;
        QVERIFY(makeCard(Kind::Legacy, cardDir.path(), 3));
        UnopenableLegacyParser parser(cardDir.path());
        parser.ReadData();
        BmcArchiveRecord record = recordFor(parser.ArchiveRecords(), cardNight(1));
        QVERIFY(parser.ReadArchiveRecordBytes(record, nullptr).isEmpty());
        QVERIFY(record.readFailed);

        CardArchive archive(archiveDir.path());
        const BmcCaptureStats stats = BmcCaptureToArchive(parser, cardDir.path(), archive, "BMC", nullptr, nullptr);
        QVERIFY(stats.failed >= 3);
        QCOMPARE(stats.stored, 0);
        QVERIFY(!stats.ok());
        QVERIFY(archiveEntries(archiveDir.path()).isEmpty());
    }
    {
        QTemporaryDir cardDir;
        QTemporaryDir archiveDir;
        QVERIFY(makeCard(Kind::G3x, cardDir.path(), 3));
        UnopenableG3xParser parser(cardDir.path());
        parser.ReadData();
        CardArchive archive(archiveDir.path());
        const BmcCaptureStats stats = BmcCaptureToArchive(parser, cardDir.path(), archive, "BMC", nullptr, nullptr);
        QCOMPARE(stats.failed, 3);
        QCOMPARE(stats.stored, 0);
        QVERIFY(!stats.ok());
    }
    // A night that genuinely has no waveform is still just skipped.
    {
        QTemporaryDir cardDir;
        QVERIFY(makeCard(Kind::Legacy, cardDir.path(), 3));
        BmcData parser(cardDir.path());
        parser.ReadData();
        BmcArchiveRecord inProgress = recordFor(parser.ArchiveRecords(), cardNight(3));
        QVERIFY(parser.ReadArchiveRecordBytes(inProgress, nullptr).isEmpty());
        QVERIFY(!inProgress.readFailed);
    }
}

void BmcArchiveTests::testCaptureCountsShortReadAsFailure()
{
    QTemporaryDir cardDir;
    QTemporaryDir archiveDir;
    QVERIFY(makeCard(Kind::G3x, cardDir.path(), 3));
    ShortReadG3xParser parser(cardDir.path());
    parser.ReadData();

    CardArchive archive(archiveDir.path());
    const BmcCaptureStats stats = BmcCaptureToArchive(parser, cardDir.path(), archive, "BMC", nullptr, nullptr);
    // The first two nights read in full; the third came up five packets short.
    QCOMPARE(stats.stored, 2);
    QCOMPARE(stats.failed, 1);
    QVERIFY(!stats.ok());
    // The short night was not stored as if it were whole.
    const QList<CardArchiveEntry> entries = archiveEntries(archiveDir.path());
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).date, cardNight(0));
    QCOMPARE(entries.at(1).date, cardNight(1));
}

void BmcArchiveTests::testConversionKeepsRingFilesWhenUnreadable()
{
    for (const Kind kind : { Kind::Legacy, Kind::G3x }) {
        // An old full-card backup whose ring files cannot be read at conversion time.
        QTemporaryDir backupDir;
        QTemporaryDir cardDir;
        QVERIFY(makeCard(kind, backupDir.path(), 3));
        QVERIFY(makeCard(kind, cardDir.path(), 4));
        const QByteArray ringBefore = readFile(backupDir.filePath(baseOf(kind) + ".000"));

        const QString backupPath = backupDir.path();
        const BmcParserFactory factory = [kind, backupPath](const QString &dir) -> std::unique_ptr<BmcDataParser> {
            if (QDir(dir) == QDir(backupPath)) {
                if (kind == Kind::Legacy) { return std::make_unique<UnopenableLegacyParser>(dir); }
                return std::make_unique<UnopenableG3xParser>(dir);
            }
            return makeParser(kind, dir);
        };

        BmcImportSource source = BmcOpenImportSource(cardDir.path(), false, factory);
        const BmcBackupOutcome outcome = BmcBackUpImportSource(source, backupDir.path(), factory,
                                                               detectFor(kind), "BMC", nullptr, nullptr);
        QVERIFY(outcome.migrated);
        QVERIFY(outcome.migration.failed >= 3);
        QVERIFY(!outcome.ok());
        QCOMPARE(outcome.ringFilesRemoved, 0);
        QCOMPARE(outcome.capture.stored, 0);
        QCOMPARE(readFile(backupDir.filePath(baseOf(kind) + ".000")), ringBefore);
    }
}

void BmcArchiveTests::testG3xTruncatedRingNotComplete()
{
    // A copy of the card whose ring file was cut short: the index promises 30 packets for
    // the last night, the file holds 20 of them.
    QTemporaryDir truncated;
    QTemporaryDir whole;
    QTemporaryDir archiveDir;
    QVERIFY(makeCard(Kind::G3x, truncated.path(), 3));
    QVERIFY(makeCard(Kind::G3x, whole.path(), 3));
    QVERIFY(QFile::resize(truncated.filePath(kG3xBase + ".000"), 80 * kG3xPacket));

    BmcG3xData parser(truncated.path());
    parser.ReadData();
    const BmcArchiveRecord last = recordFor(parser.ArchiveRecords(), cardNight(2));
    QCOMPARE(last.info.bytes, qint64(20 * kG3xPacket));
    QVERIFY(!last.info.complete);                       // not what the index says the night holds
    QVERIFY(recordFor(parser.ArchiveRecords(), cardNight(1)).info.complete);

    QCOMPARE(captureCard(Kind::G3x, truncated.path(), archiveDir.path()).stored, 3);
    QCOMPARE(archiveEntries(archiveDir.path()).at(2).packets, 20);
    QVERIFY(!archiveEntries(archiveDir.path()).at(2).complete);

    // The whole card then supplies the rest.
    const BmcCaptureStats stats = captureCard(Kind::G3x, whole.path(), archiveDir.path());
    QCOMPARE(stats.replaced, 1);
    QCOMPARE(archiveEntries(archiveDir.path()).at(2).packets, kG3xPacketsPerNight);
    QVERIFY(archiveEntries(archiveDir.path()).at(2).complete);
}

void BmcArchiveTests::testFutureDatedRecordDoesNotCloseNewestNight()
{
    // A record with an impossible date (a device clock fault, a blank index slot) must not
    // make the real newest night look finished: a finished night is not read again.
    {
        QTemporaryDir dir;
        QVERIFY(makeLegacyCard(dir.path(), cardNights(3), QDate(2090, 1, 1)));
        BmcData parser(dir.path());
        parser.ReadData();
        const QList<BmcArchiveRecord> records = parser.ArchiveRecords();
        QVERIFY(recordFor(records, cardNight(1)).info.final);
        QVERIFY(!recordFor(records, cardNight(2)).info.final);
    }
    {
        QTemporaryDir dir;
        QVERIFY(makeCard(Kind::G3x, dir.path(), 3));
        QList<G3xNight> nights;
        const quint32 night = static_cast<quint32>(kG3xPacketsPerNight * kG3xPacket);
        for (int n = 0; n < 3; ++n) { nights.append({ cardNight(n), n * night, (n + 1) * night, night }); }
        nights.append({ QDate(2090, 1, 1), 0, 0, 0 });
        QVERIFY(makeG3xIndex(dir.path(), nights, 63));
        BmcG3xData parser(dir.path());
        parser.ReadData();
        const QList<BmcArchiveRecord> records = parser.ArchiveRecords();
        QVERIFY(recordFor(records, cardNight(1)).info.final);
        QVERIFY(!recordFor(records, cardNight(2)).info.final);
    }
}

void BmcArchiveTests::testLegacyGarbageCrumbDoesNotHideTail()
{
    // Never-written ring space can decode to a date centuries ago. Such a packet at a
    // sampling point must not be taken for the oldest data on the card, or the night that
    // really starts there would be recorded as complete.
    QTemporaryDir dir;
    QVERIFY(makeCard(Kind::Legacy, dir.path(), 3));

    BmcData parser(dir.path());
    parser.ReadData();
    // The sampling points are sorted by time, so such a packet comes first. (It is added
    // here, after the sessions were located, so that only this check sees it.)
    BmcWaveformCrumb stale;
    stale.Filepath = dir.filePath(kLegacyBase + ".000");
    stale.Timestamp = QDateTime(QDate(1031, 5, 5), QTime(1, 1, 1));
    parser.WaveformCrumbs.prepend(stale);
    const QList<BmcArchiveRecord> records = parser.ArchiveRecords();
    BmcArchiveRecord oldest = recordFor(records, cardNight(0));
    QCOMPARE(parser.ReadArchiveRecordBytes(oldest, nullptr).size(), kLegacyPacketsPerNight * kLegacyPacket);
    QVERIFY(!oldest.info.complete);
    BmcArchiveRecord second = recordFor(records, cardNight(1));
    QCOMPARE(parser.ReadArchiveRecordBytes(second, nullptr).size(), kLegacyPacketsPerNight * kLegacyPacket);
    QVERIFY(second.info.complete);
}

void BmcArchiveTests::testInterruptedRingDeletionStillArchive()
{
    for (const Kind kind : { Kind::Legacy, Kind::G3x }) {
        QTemporaryDir backupDir;
        QTemporaryDir cardDir;
        QVERIFY(makeCard(kind, backupDir.path(), 3));
        QVERIFY(makeCard(kind, cardDir.path(), 4));
        const QByteArray ring = readFile(backupDir.filePath(baseOf(kind) + ".000"));

        BmcImportSource source = BmcOpenImportSource(cardDir.path(), false, factoryFor(kind));
        const BmcBackupOutcome outcome = BmcBackUpImportSource(source, backupDir.path(), factoryFor(kind),
                                                               detectFor(kind), "BMC", nullptr, nullptr);
        QVERIFY(outcome.ok());
        QCOMPARE(outcome.ringFilesRemoved, 1);

        // As if the deletion had been cut short: a ring file is still lying there, cut in
        // half. The conversion itself had finished, so the archive is what counts.
        QVERIFY(writeFile(backupDir.filePath(baseOf(kind) + ".000"), ring.left(ring.size() / 2)));

        BmcImportSource rebuild = BmcOpenImportSource(backupDir.path(), true, factoryFor(kind));
        QVERIFY(rebuild.isArchive);
        for (int n = 0; n < 4; ++n) {
            QCOMPARE(rebuild.parsers.front()->ReadDateSession(cardNight(n)).Waveforms.size(), packetsPerNight(kind));
        }

        // The next import clears the leftover without converting anything again.
        BmcImportSource again = BmcOpenImportSource(cardDir.path(), false, factoryFor(kind));
        const BmcBackupOutcome second = BmcBackUpImportSource(again, backupDir.path(), factoryFor(kind),
                                                              detectFor(kind), "BMC", nullptr, nullptr);
        QVERIFY(!second.migrated);
        QCOMPARE(second.ringFilesRemoved, 1);
        QVERIFY(second.ok());
        QVERIFY(!QFile::exists(backupDir.filePath(baseOf(kind) + ".000")));
        QCOMPARE(archiveEntries(backupDir.path()).size(), 4);
    }
}

void BmcArchiveTests::testOpenImportSourceLostManifest()
{
    for (const Kind kind : { Kind::Legacy, Kind::G3x }) {
        QTemporaryDir cardDir;
        QTemporaryDir backupDir;
        QVERIFY(makeCard(kind, cardDir.path(), 3));
        QCOMPARE(captureCard(kind, cardDir.path(), backupDir.path()).stored, 3);
        const QString manifest = backupDir.filePath(CardArchive::manifestName());

        for (const QByteArray &damage : { QByteArray(), QByteArray("not json") }) {
            // The manifest is gone, or unreadable; the segments are intact.
            if (damage.isEmpty()) { QVERIFY(QFile::remove(manifest)); }
            else { QVERIFY(writeFile(manifest, damage)); }
            QVERIFY(detectFor(kind)(backupDir.path()));

            // Someone else's copy: every night still imports, and nothing is written.
            {
                BmcImportSource foreign = BmcOpenImportSource(backupDir.path(), false, factoryFor(kind));
                QVERIFY(foreign.isArchive);
                QCOMPARE(foreign.parsers.size(), size_t(1));
                for (int n = 0; n < 3; ++n) {
                    QCOMPARE(foreign.parsers.front()->ReadDateSession(cardNight(n)).Waveforms.size(), packetsPerNight(kind));
                }
                QCOMPARE(readFile(manifest), damage);
            }

            // The device's own backup: the same, and the manifest is rebuilt.
            {
                BmcImportSource own = BmcOpenImportSource(backupDir.path(), true, factoryFor(kind));
                QVERIFY(own.isArchive);
                for (int n = 0; n < 3; ++n) {
                    QCOMPARE(own.parsers.front()->ReadDateSession(cardNight(n)).Waveforms.size(), packetsPerNight(kind));
                }
                importEverything(own);
                QVERIFY(BmcFinishArchiveImport(own).clean());
            }
            QCOMPARE(archiveEntries(backupDir.path()).size(), 3);
        }
    }
}

void BmcArchiveTests::testOpenImportSourceNewerManifest()
{
    for (const Kind kind : { Kind::Legacy, Kind::G3x }) {
        QTemporaryDir cardDir;
        QTemporaryDir backupDir;
        QVERIFY(makeCard(kind, cardDir.path(), 3));
        QCOMPARE(captureCard(kind, cardDir.path(), backupDir.path()).stored, 3);
        // As left by a later version of OSCAR whose rules this one does not know.
        QVERIFY(writeFile(backupDir.filePath(CardArchive::manifestName()),
                          "{\"version\":2,\"loader\":\"BMC\",\"base\":\"x\",\"segments\":[]}"));
        const QStringList before = folderSnapshot(backupDir.path());

        // Not recognised as importable, so a rebuild stops before it purges anything.
        QVERIFY(!detectFor(kind)(backupDir.path()));
        BmcImportSource source = BmcOpenImportSource(backupDir.path(), true, factoryFor(kind));
        QVERIFY(source.parsers.empty());
        QCOMPARE(folderSnapshot(backupDir.path()), before);
    }
}

// ---------------------------------------------------------------------------------
// Second review: nothing is destroyed on the strength of one failed read
// ---------------------------------------------------------------------------------

void BmcArchiveTests::testFinishArchiveImportKeepsUnavailableSegment()
{
    for (const Kind kind : { Kind::Legacy, Kind::G3x }) {
        QTemporaryDir cardDir;
        QTemporaryDir backupDir;
        QVERIFY(makeCard(kind, cardDir.path(), 3));
        QCOMPARE(captureCard(kind, cardDir.path(), backupDir.path()).stored, 3);
        const QString segment = backupDir.filePath(CardArchive::segmentDirName() + "/" + baseOf(kind)
                                                   + "_20260311.seg.gz");
        const QByteArray good = readFile(segment);

        // During a rebuild the segment cannot be opened: locked by another program, or
        // not downloaded from a synced folder. It is not damaged.
        QVERIFY(QFile::remove(segment));
        QVERIFY(QDir().mkpath(segment));
        {
            BmcImportSource own = BmcOpenImportSource(backupDir.path(), true, factoryFor(kind));
            QVERIFY(own.isArchive);
            importEverything(own);
            const BmcArchiveReadReport report = BmcFinishArchiveImport(own);
            QCOMPARE(report.unreadable, QList<QDate>() << cardNight(1));    // the user is told
        }
        // Nothing was removed or set aside, and the night is still listed.
        QVERIFY(QDir(segment).exists());
        QVERIFY(!QDir(backupDir.filePath(CardArchive::segmentDirName() + "/" + CardArchive::damagedDirName())).exists());
        QCOMPARE(archiveEntries(backupDir.path()).size(), 3);

        // When the file can be read again, so can the night.
        QVERIFY(QDir().rmdir(segment));
        QVERIFY(writeFile(segment, good));
        BmcImportSource later = BmcOpenImportSource(backupDir.path(), true, factoryFor(kind));
        QCOMPARE(later.parsers.front()->ReadDateSession(cardNight(1)).Waveforms.size(), packetsPerNight(kind));
        importEverything(later);
        QVERIFY(BmcFinishArchiveImport(later).clean());
    }
}

void BmcArchiveTests::testConversionVerifiesSegmentsBeforeDeleting()
{
    for (const Kind kind : { Kind::Legacy, Kind::G3x }) {
        // An old full-card backup whose conversion was begun: the nights are stored, the
        // ring files are still there. Then one stored segment goes bad on disk.
        QTemporaryDir backupDir;
        QTemporaryDir cardDir;
        QVERIFY(makeCard(kind, backupDir.path(), 3));
        QVERIFY(makeCard(kind, cardDir.path(), 4));
        QCOMPARE(captureCard(kind, backupDir.path(), backupDir.path()).stored, 3);
        const QString segment = backupDir.filePath(CardArchive::segmentDirName() + "/" + baseOf(kind)
                                                   + "_20260311.seg.gz");
        QVERIFY(writeFile(segment, "went bad"));
        const QByteArray ring = readFile(backupDir.filePath(baseOf(kind) + ".000"));

        // The conversion reads every segment back before it deletes the only other copy.
        BmcImportSource source = BmcOpenImportSource(cardDir.path(), false, factoryFor(kind));
        const BmcBackupOutcome outcome = BmcBackUpImportSource(source, backupDir.path(), factoryFor(kind),
                                                               detectFor(kind), "BMC", nullptr, nullptr);
        QVERIFY(outcome.migrated);
        QVERIFY(!outcome.ok());
        QCOMPARE(outcome.ringFilesRemoved, 0);
        QCOMPARE(readFile(backupDir.filePath(baseOf(kind) + ".000")), ring);
        // The bad segment was set aside, so the next import takes that night from the ring files again.
        QCOMPARE(archiveEntries(backupDir.path()).size(), 2);

        BmcImportSource again = BmcOpenImportSource(cardDir.path(), false, factoryFor(kind));
        const BmcBackupOutcome second = BmcBackUpImportSource(again, backupDir.path(), factoryFor(kind),
                                                              detectFor(kind), "BMC", nullptr, nullptr);
        QVERIFY(second.ok());
        QCOMPARE(second.migration.stored, 1);
        QCOMPARE(second.ringFilesRemoved, 1);
        QCOMPARE(archiveEntries(backupDir.path()).size(), 4);
    }
}

void BmcArchiveTests::testUnparseableOldCopyIsSetAside()
{
    // The Backup folder holds ring files from an old copy that was never finished (the old
    // code removed the folder and copied the card again each time; that copy was cut
    // short). It cannot be read as a card, so it cannot be converted. It must not be
    // deleted, overwritten, or left where a rebuild would take it for the backup.
    QTemporaryDir backupDir;
    QTemporaryDir cardDir;
    QVERIFY(makeCard(Kind::G3x, cardDir.path(), 3));
    QVERIFY(writeFile(backupDir.filePath(kG3xBase + ".000"), "part of an old copy"));
    QVERIFY(writeFile(backupDir.filePath(kG3xBase + ".001"), "more of it"));
    QVERIFY(!detectFor(Kind::G3x)(backupDir.path()));

    BmcImportSource source = BmcOpenImportSource(cardDir.path(), false, factoryFor(Kind::G3x));
    const BmcBackupOutcome outcome = BmcBackUpImportSource(source, backupDir.path(), factoryFor(Kind::G3x),
                                                           detectFor(Kind::G3x), "BMC", nullptr, nullptr);
    QVERIFY(!outcome.migrated);
    QCOMPARE(outcome.setAside, 2);
    QCOMPARE(outcome.capture.stored, 3);
    QVERIFY(outcome.capture.ok());

    QVERIFY(!QFile::exists(backupDir.filePath(kG3xBase + ".000")));
    QCOMPARE(readFile(backupDir.filePath(BmcUnconvertedDirName() + "/" + kG3xBase + ".000")), QByteArray("part of an old copy"));
    QCOMPARE(readFile(backupDir.filePath(BmcUnconvertedDirName() + "/" + kG3xBase + ".001")), QByteArray("more of it"));

    // The folder is now a plain archive, and a rebuild reads it as one.
    BmcImportSource rebuild = BmcOpenImportSource(backupDir.path(), true, factoryFor(Kind::G3x));
    QVERIFY(rebuild.isArchive);
    QCOMPARE(rebuild.parsers.front()->ReadDateSession(cardNight(1)).Waveforms.size(), kG3xPacketsPerNight);
    // The user is told once that something was set aside.
    QVERIFY(BmcBackupWarning(outcome).contains(BmcUnconvertedDirName()));

    // The set-aside files are not mirrored back or converted on the next import.
    BmcImportSource again = BmcOpenImportSource(cardDir.path(), false, factoryFor(Kind::G3x));
    const BmcBackupOutcome second = BmcBackUpImportSource(again, backupDir.path(), factoryFor(Kind::G3x),
                                                          detectFor(Kind::G3x), "BMC", nullptr, nullptr);
    QCOMPARE(second.setAside, 0);
    QVERIFY(second.ok());
    QVERIFY(BmcBackupWarning(second).isEmpty());
}

void BmcArchiveTests::testBackupWarningText()
{
    BmcBackupOutcome fine;
    fine.capture.stored = 3;
    QVERIFY(BmcBackupWarning(fine).isEmpty());

    // Cancelled by the user: they know.
    BmcBackupOutcome cancelled;
    cancelled.capture.aborted = true;
    cancelled.cancelledByUser = true;
    QVERIFY(BmcBackupWarning(cancelled).isEmpty());

    BmcBackupOutcome failed;
    failed.capture.failed = 2;
    QString text = BmcBackupWarning(failed);
    QVERIFY(text.contains("could not"));
    QVERIFY(text.contains("next import"));

    // The old copy could not be converted, so the new nights were held back.
    BmcBackupOutcome blocked;
    blocked.migrated = true;
    blocked.migration.failed = 1;
    blocked.capture.aborted = true;
    text = BmcBackupWarning(blocked);
    QVERIFY(text.contains("earlier backup"));
    QVERIFY(text.contains("not been backed up"));
}
