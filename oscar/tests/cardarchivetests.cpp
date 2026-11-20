/* Card Archive Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "cardarchivetests.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "SleepLib/card_archive.h"
#include "SleepLib/common.h"

namespace {

constexpr int kPacket = 256;
const QDate kNight(2026, 11, 14);

//! \a count packets of kPacket bytes, all filled with \a fill.
QByteArray packets(int count, char fill)
{
    return QByteArray(count * kPacket, fill);
}

//! A record for kNight running from \a from on that date to \a to on the following date.
CardArchiveRecord night(const QTime &from, const QTime &to, bool final, bool complete = true,
                        int rule = 1, const QDate &date = kNight)
{
    CardArchiveRecord rec;
    rec.date = date;
    rec.first = QDateTime(date, from);
    rec.last = QDateTime(date.addDays(1), to);
    rec.final = final;
    rec.complete = complete;
    rec.rule = rule;
    return rec;
}

//! A record within one calendar day, for cases that need spans that do not overlap.
CardArchiveRecord sameDay(const QTime &from, const QTime &to, bool final)
{
    CardArchiveRecord rec;
    rec.date = kNight;
    rec.first = QDateTime(kNight, from);
    rec.last = QDateTime(kNight, to);
    rec.final = final;
    rec.rule = 1;
    return rec;
}

QString segmentDir(const QTemporaryDir &dir)
{
    return dir.filePath(CardArchive::segmentDirName());
}

QStringList segmentFiles(const QTemporaryDir &dir, const QString &filter = QStringLiteral("*"))
{
    return QDir(segmentDir(dir)).entryList(QStringList() << filter, QDir::Files, QDir::Name);
}

QByteArray fileBytes(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { return QByteArray(); }
    return f.readAll();
}

bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
    return f.write(bytes) == bytes.size();
}

} // namespace

void CardArchiveTests::testStoreAndReadBack()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(!CardArchive::isArchive(dir.path()));

    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");
    const QByteArray data = packets(100, 'a');
    const CardArchiveRecord rec = night(QTime(22, 0), QTime(6, 0), true);

    QCOMPARE(archive.store(rec, data, kPacket), CardArchive::StoreResult::Stored);

    QCOMPARE(archive.entries().size(), 1);
    const CardArchiveEntry entry = archive.entries().first();
    QCOMPARE(entry.file, QString("base_20261114.seg.gz"));
    QVERIFY(QFile::exists(segmentDir(dir) + "/base_20261114.seg.gz"));
    QCOMPARE(entry.bytes, qint64(100 * kPacket));
    QCOMPARE(entry.packets, 100);
    QCOMPARE(entry.date, kNight);
    QVERIFY(CardArchive::isArchive(dir.path()));

    bool ok = false;
    QCOMPARE(archive.read(entry, &ok), data);
    QVERIFY(ok);
}

void CardArchiveTests::testBinaryContentRoundTrip()
{
    // Real packets are binary: every byte value must survive, not just text.
    QTemporaryDir dir;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");
    QByteArray data(100 * kPacket, '\0');
    for (int i = 0; i < data.size(); ++i) {
        data[i] = static_cast<char>((i * 7 + (i >> 8)) & 0xFF);
    }
    QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true), data, kPacket),
             CardArchive::StoreResult::Stored);

    bool ok = false;
    QCOMPARE(archive.read(archive.entries().first(), &ok), data);
    QVERIFY(ok);
}

void CardArchiveTests::testGzipChecksumOfHighBytes()
{
    // The gzip trailer is CRC-32 then length, both little-endian. CRC-32 of the single
    // byte 0xFF is 0xFF000000; of 0x00 it is 0xD202EF8D.
    const QByteArray high = gCompress(QByteArray(1, static_cast<char>(0xFF)));
    QCOMPARE(high.right(8).left(4).toHex(), QByteArray("000000ff"));
    const QByteArray zero = gCompress(QByteArray(1, '\0'));
    QCOMPARE(zero.right(8).left(4).toHex(), QByteArray("8def02d2"));

    QByteArray all(256, '\0');
    for (int i = 0; i < 256; ++i) { all[i] = static_cast<char>(i); }
    QCOMPARE(gUncompress(gCompress(all)), all);
}

void CardArchiveTests::testReloadKeepsEntries()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const CardArchiveRecord rec = night(QTime(22, 0), QTime(6, 0), true, false, 3);
    {
        CardArchive archive(dir.path());
        archive.setIdentity("BMC", "base");
        QCOMPARE(archive.store(rec, packets(10, 'b'), kPacket), CardArchive::StoreResult::Stored);
    }

    CardArchive again(dir.path());
    QVERIFY(again.load());
    QVERIFY(again.isWritable());
    QCOMPARE(again.baseName(), QString("base"));
    QCOMPARE(again.entries().size(), 1);
    const CardArchiveEntry entry = again.entries().first();
    QCOMPARE(entry.file, QString("base_20261114.seg.gz"));
    QCOMPARE(entry.date, rec.date);
    QCOMPARE(entry.first, rec.first);
    QCOMPARE(entry.last, rec.last);
    QCOMPARE(entry.final, true);
    QCOMPARE(entry.complete, false);
    QCOMPARE(entry.rule, 3);
    QCOMPARE(entry.sha256.size(), 32);

    bool ok = false;
    QCOMPARE(again.read(entry, &ok), packets(10, 'b'));
    QVERIFY(ok);
}

void CardArchiveTests::testFinalCompleteIsSkipped()
{
    QTemporaryDir dir;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");
    CardArchiveRecord rec = night(QTime(22, 0), QTime(6, 0), true);
    QCOMPARE(archive.store(rec, packets(100, 'a'), kPacket), CardArchive::StoreResult::Stored);

    QVERIFY(!archive.wants(rec));
    rec.bytes = 100 * kPacket;      // the same amount on offer: nothing to gain
    QVERIFY(!archive.wants(rec));
    CardArchiveRecord unknown = rec;
    unknown.first = QDateTime();    // nor when the span and size are unknown before reading
    unknown.last = QDateTime();
    unknown.bytes = -1;
    QVERIFY(!archive.wants(unknown));

    // But "complete" is only what the capturing card showed. A card that is known to
    // offer more of the night is believed over that, and the longer capture wins.
    rec.bytes = 200 * kPacket;
    QVERIFY(archive.wants(rec));
    QCOMPARE(archive.store(rec, packets(200, 'b'), kPacket), CardArchive::StoreResult::Replaced);
    QCOMPARE(archive.entries().first().bytes, qint64(200 * kPacket));
}

void CardArchiveTests::testLongerReplacesShorter()
{
    QTemporaryDir dir;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");
    QCOMPARE(archive.store(night(QTime(22, 0), QTime(2, 0), false), packets(100, 'a'), kPacket),
             CardArchive::StoreResult::Stored);

    // The night was still in progress; the next import offers more of it.
    CardArchiveRecord more = night(QTime(22, 0), QTime(6, 0), true);
    more.bytes = 150 * kPacket;
    QVERIFY(archive.wants(more));
    QCOMPARE(archive.store(more, packets(150, 'b'), kPacket), CardArchive::StoreResult::Replaced);

    QCOMPARE(archive.entries().size(), 1);
    const CardArchiveEntry entry = archive.entries().first();
    QCOMPARE(entry.bytes, qint64(150 * kPacket));
    QCOMPARE(entry.final, true);
    QCOMPARE(entry.last, more.last);
    bool ok = false;
    QCOMPARE(archive.read(entry, &ok), packets(150, 'b'));
    QVERIFY(ok);
    QCOMPARE(segmentFiles(dir).size(), 1);
}

void CardArchiveTests::testNeverShortened()
{
    QTemporaryDir dir;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");
    QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true, false), packets(150, 'a'), kPacket),
             CardArchive::StoreResult::Stored);

    // A later card holds less of the same night.
    CardArchiveRecord less = night(QTime(1, 0), QTime(6, 0), true, false);
    less.first = QDateTime(kNight.addDays(1), QTime(1, 0));
    less.bytes = 100 * kPacket;
    QVERIFY(!archive.wants(less));
    QCOMPARE(archive.store(less, packets(100, 'b'), kPacket), CardArchive::StoreResult::KeptExisting);

    bool ok = false;
    QCOMPARE(archive.read(archive.entries().first(), &ok), packets(150, 'a'));
    QVERIFY(ok);

    // Same size but already final: nothing to gain from rewriting it.
    CardArchiveRecord same = night(QTime(22, 0), QTime(6, 0), true, false);
    same.bytes = 150 * kPacket;
    QVERIFY(!archive.wants(same));
    QCOMPARE(archive.store(same, packets(150, 'c'), kPacket), CardArchive::StoreResult::KeptExisting);
}

void CardArchiveTests::testIdenticalContentNotRewritten()
{
    // The newest night is offered again on every import. When nothing about it has
    // changed there is nothing to write; the entry only gains standing.
    QTemporaryDir dir;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");
    const QByteArray data = packets(100, 'a');
    QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), false, false, 1), data, kPacket),
             CardArchive::StoreResult::Stored);
    const QString path = segmentDir(dir) + "/base_20261114.seg.gz";
    const QByteArray segmentBytes = fileBytes(path);

    // With the file out of the way, a rewrite would bring it back.
    QVERIFY(QFile::remove(path));
    QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true, true, 2), data, kPacket),
             CardArchive::StoreResult::KeptExisting);
    QVERIFY(!QFile::exists(path));
    QVERIFY(writeBytes(path, segmentBytes));

    CardArchiveEntry entry = archive.entries().first();
    QCOMPARE(entry.final, true);
    QCOMPARE(entry.complete, true);
    QCOMPARE(entry.rule, 2);

    // The same content offered with less standing takes nothing away.
    QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), false, false, 1), data, kPacket),
             CardArchive::StoreResult::KeptExisting);
    entry = archive.entries().first();
    QCOMPARE(entry.final, true);
    QCOMPARE(entry.complete, true);
    QCOMPARE(entry.rule, 2);

    // The gained standing was saved.
    CardArchive again(dir.path());
    QVERIFY(again.load());
    QCOMPARE(again.entries().first().final, true);
    QCOMPARE(again.entries().first().rule, 2);
    bool ok = false;
    QCOMPARE(again.read(again.entries().first(), &ok), data);
    QVERIFY(ok);
}

void CardArchiveTests::testTailThenFull()
{
    QTemporaryDir dir;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");

    // First seen on a card whose ring had already overwritten the start of the night.
    CardArchiveRecord tail = night(QTime(22, 0), QTime(6, 0), true, false);
    tail.first = QDateTime(kNight.addDays(1), QTime(2, 0));
    QCOMPARE(archive.store(tail, packets(100, 't'), kPacket), CardArchive::StoreResult::Stored);

    // An older copy of the card still has the whole night.
    CardArchiveRecord full = night(QTime(22, 0), QTime(6, 0), true, true);
    full.bytes = 200 * kPacket;
    QVERIFY(archive.wants(full));
    QCOMPARE(archive.store(full, packets(200, 'f'), kPacket), CardArchive::StoreResult::Replaced);

    QCOMPARE(archive.entries().size(), 1);
    const CardArchiveEntry entry = archive.entries().first();
    QCOMPARE(entry.complete, true);
    QCOMPARE(entry.first, full.first);
    QCOMPARE(entry.bytes, qint64(200 * kPacket));
}

void CardArchiveTests::testSameDateNoOverlap()
{
    QTemporaryDir dir;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");
    QCOMPARE(archive.store(sameDay(QTime(1, 0), QTime(2, 0), true), packets(10, 'a'), kPacket),
             CardArchive::StoreResult::Stored);
    QCOMPARE(archive.store(sameDay(QTime(5, 0), QTime(6, 0), true), packets(20, 'b'), kPacket),
             CardArchive::StoreResult::Stored);

    const QList<CardArchiveEntry> entries = archive.entries();
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).file, QString("base_20261114.seg.gz"));
    QCOMPARE(entries.at(1).file, QString("base_20261114_2.seg.gz"));
    QCOMPARE(segmentFiles(dir, "*.seg.gz").size(), 2);

    // Looked up by span, each record finds its own entry.
    const CardArchiveEntry *second = archive.find(sameDay(QTime(5, 30), QTime(5, 40), true));
    QVERIFY(second != nullptr);
    QCOMPARE(second->file, QString("base_20261114_2.seg.gz"));

    // Looked up by date alone, the longest one is returned.
    CardArchiveRecord dateOnly;
    dateOnly.date = kNight;
    const CardArchiveEntry *longest = archive.find(dateOnly);
    QVERIFY(longest != nullptr);
    QCOMPARE(longest->bytes, qint64(20 * kPacket));

    dateOnly.date = kNight.addDays(1);
    QVERIFY(archive.find(dateOnly) == nullptr);
}

void CardArchiveTests::testRejectsBadInput()
{
    QTemporaryDir dir;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");
    const CardArchiveRecord rec = night(QTime(22, 0), QTime(6, 0), true);

    QCOMPARE(archive.store(rec, QByteArray(), kPacket), CardArchive::StoreResult::Failed);
    QCOMPARE(archive.store(rec, QByteArray(257, 'x'), kPacket), CardArchive::StoreResult::Failed);
    QCOMPARE(archive.store(rec, packets(4, 'x'), 0), CardArchive::StoreResult::Failed);

    CardArchiveRecord noDate = rec;
    noDate.date = QDate();
    QCOMPARE(archive.store(noDate, packets(4, 'x'), kPacket), CardArchive::StoreResult::Failed);
    CardArchiveRecord noSpan = rec;
    noSpan.first = QDateTime();
    QCOMPARE(archive.store(noSpan, packets(4, 'x'), kPacket), CardArchive::StoreResult::Failed);

    QVERIFY(archive.entries().isEmpty());
    QVERIFY(segmentFiles(dir).isEmpty());
}

void CardArchiveTests::testCorruptSegment()
{
    QTemporaryDir dir;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");
    CardArchiveRecord rec = night(QTime(22, 0), QTime(6, 0), true);
    QCOMPARE(archive.store(rec, packets(100, 'a'), kPacket), CardArchive::StoreResult::Stored);
    const QString path = segmentDir(dir) + "/base_20261114.seg.gz";

    QVERIFY(writeBytes(path, QByteArray("this is not a gzip stream at all")));
    bool ok = true;
    QVERIFY(archive.read(archive.entries().first(), &ok).isEmpty());
    QVERIFY(!ok);

    // A valid gzip stream holding the wrong content is caught by the checksum.
    {
        QTemporaryDir other;
        CardArchive donor(other.path());
        donor.setIdentity("BMC", "base");
        QCOMPARE(donor.store(rec, packets(100, 'z'), kPacket), CardArchive::StoreResult::Stored);
        QVERIFY(writeBytes(path, fileBytes(segmentDir(other) + "/base_20261114.seg.gz")));
    }
    ok = true;
    QVERIFY(archive.read(archive.entries().first(), &ok).isEmpty());
    QVERIFY(!ok);

    archive.drop(archive.entries().first());
    QVERIFY(archive.entries().isEmpty());
    QVERIFY(!QFile::exists(path));
    QVERIFY(archive.wants(rec));

    CardArchive again(dir.path());
    QVERIFY(again.load());
    QVERIFY(again.entries().isEmpty());
}

void CardArchiveTests::testNewerVersionReadOnly()
{
    QTemporaryDir dir;
    const QString manifest = dir.filePath(CardArchive::manifestName());
    const QByteArray newer = "{\"version\":2,\"loader\":\"BMC\",\"base\":\"base\",\"segments\":[]}";
    QVERIFY(writeBytes(manifest, newer));

    CardArchive archive(dir.path());
    QVERIFY(!archive.load());
    QVERIFY(!archive.isWritable());
    QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true), packets(4, 'a'), kPacket),
             CardArchive::StoreResult::Failed);
    QVERIFY(!archive.save());
    QCOMPARE(fileBytes(manifest), newer);
    QVERIFY(segmentFiles(dir).isEmpty());
}

void CardArchiveTests::testSetReadOnly()
{
    QTemporaryDir dir;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");
    QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true), packets(8, 'a'), kPacket),
             CardArchive::StoreResult::Stored);
    const QString manifest = dir.filePath(CardArchive::manifestName());
    const QByteArray before = fileBytes(manifest);

    archive.setReadOnly(true);
    QVERIFY(!archive.isWritable());
    QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true, true, 1, kNight.addDays(1)),
                           packets(8, 'b'), kPacket),
             CardArchive::StoreResult::Failed);
    archive.drop(archive.entries().first());
    QVERIFY(!archive.save());

    QCOMPARE(archive.entries().size(), 1);
    QCOMPARE(segmentFiles(dir), QStringList() << "base_20261114.seg.gz");
    QCOMPARE(fileBytes(manifest), before);
    bool ok = false;
    QCOMPARE(archive.read(archive.entries().first(), &ok), packets(8, 'a'));
    QVERIFY(ok);

    archive.setReadOnly(false);
    QVERIFY(archive.isWritable());
}

void CardArchiveTests::testNewerRuleRecaptured()
{
    QTemporaryDir dir;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");
    QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true, true, 1), packets(150, 'a'), kPacket),
             CardArchive::StoreResult::Stored);

    // The loader's range logic was corrected: the night is offered again, and the new
    // rule's complete capture wins even though it is shorter.
    CardArchiveRecord fixed = night(QTime(22, 0), QTime(5, 0), true, true, 2);
    fixed.bytes = 120 * kPacket;
    QVERIFY(archive.wants(fixed));
    QCOMPARE(archive.store(fixed, packets(120, 'b'), kPacket), CardArchive::StoreResult::Replaced);

    const CardArchiveEntry entry = archive.entries().first();
    QCOMPARE(entry.rule, 2);
    QCOMPARE(entry.bytes, qint64(120 * kPacket));
    QVERIFY(!archive.wants(fixed));
}

void CardArchiveTests::testNewerRuleTailKept()
{
    QTemporaryDir dir;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");
    QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true, true, 1), packets(150, 'a'), kPacket),
             CardArchive::StoreResult::Stored);

    // Under the new rule the card can only offer the tail: the old capture is worth more.
    CardArchiveRecord tail = night(QTime(22, 0), QTime(6, 0), true, false, 2);
    tail.first = QDateTime(kNight.addDays(1), QTime(3, 0));
    QVERIFY(archive.wants(tail));
    QCOMPARE(archive.store(tail, packets(80, 'b'), kPacket), CardArchive::StoreResult::KeptExisting);

    const CardArchiveEntry entry = archive.entries().first();
    QCOMPARE(entry.rule, 1);
    QCOMPARE(entry.bytes, qint64(150 * kPacket));
    QVERIFY(archive.wants(tail));   // still offered, in case a fuller card turns up
}

void CardArchiveTests::testCoversSpan()
{
    QTemporaryDir dir;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");
    QVERIFY(!archive.coversSpan(QDateTime(kNight, QTime(22, 0)), QDateTime(kNight.addDays(1), QTime(6, 0))));
    QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true), packets(8, 'a'), kPacket),
             CardArchive::StoreResult::Stored);

    const QDate morning = kNight.addDays(1);
    QVERIFY(archive.coversSpan(QDateTime(morning, QTime(5, 0)), QDateTime(morning, QTime(7, 0))));
    QVERIFY(!archive.coversSpan(QDateTime(morning, QTime(6, 30)), QDateTime(morning, QTime(7, 0))));
    QVERIFY(!archive.coversSpan(QDateTime(kNight, QTime(20, 0)), QDateTime(kNight, QTime(21, 59))));
    QVERIFY(!archive.coversSpan(QDateTime(), QDateTime(morning, QTime(7, 0))));
}

void CardArchiveTests::testLoadDropsMissingFile()
{
    QTemporaryDir dir;
    {
        CardArchive archive(dir.path());
        archive.setIdentity("BMC", "base");
        QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true), packets(8, 'a'), kPacket),
                 CardArchive::StoreResult::Stored);
        QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true, true, 1, kNight.addDays(1)),
                               packets(8, 'b'), kPacket),
                 CardArchive::StoreResult::Stored);
    }
    QVERIFY(QFile::remove(segmentDir(dir) + "/base_20261114.seg.gz"));

    CardArchive again(dir.path());
    QVERIFY(again.load());
    QCOMPARE(again.entries().size(), 1);
    QCOMPARE(again.entries().first().date, kNight.addDays(1));
}

void CardArchiveTests::testUnknownKeysPreserved()
{
    QTemporaryDir dir;
    const QString manifest = dir.filePath(CardArchive::manifestName());
    QVERIFY(writeBytes(manifest,
        "{\"version\":1,\"loader\":\"BMC\",\"base\":\"base\",\"note\":\"kept\",\"segments\":[]}"));

    CardArchive archive(dir.path());
    QVERIFY(archive.load());
    QCOMPARE(archive.baseName(), QString("base"));
    QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true), packets(8, 'a'), kPacket),
             CardArchive::StoreResult::Stored);

    const QJsonObject root = QJsonDocument::fromJson(fileBytes(manifest)).object();
    QCOMPARE(root.value("note").toString(), QString("kept"));
    QCOMPARE(root.value("version").toInt(), 1);
    QCOMPARE(root.value("segments").toArray().size(), 1);
}

void CardArchiveTests::testNoTmpLeftBehind()
{
    QTemporaryDir dir;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");
    QCOMPARE(archive.store(night(QTime(22, 0), QTime(2, 0), false), packets(10, 'a'), kPacket),
             CardArchive::StoreResult::Stored);
    QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true), packets(30, 'b'), kPacket),
             CardArchive::StoreResult::Replaced);
    QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true), packets(5, 'c'), kPacket),
             CardArchive::StoreResult::KeptExisting);
    QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true), QByteArray(100, 'x'), kPacket),
             CardArchive::StoreResult::Failed);

    QVERIFY(segmentFiles(dir, "*.tmp").isEmpty());
    // One file for the one night; the replacement was written beside the old one first,
    // so it carries the next free name.
    QCOMPARE(segmentFiles(dir).size(), 1);
    QCOMPARE(segmentFiles(dir).first(), archive.entries().first().file);
    QVERIFY(QDir(dir.path()).entryList(QStringList() << "*.tmp", QDir::Files).isEmpty());
}

// ---------------------------------------------------------------------------------
// Index sets, manifest recovery and other card files
// ---------------------------------------------------------------------------------

namespace {

//! Stand-in for a loader's packet decoder: the night is told from the fill byte, 'a' being
//! kNight, 'b' the night after, and so on.
bool describeByFill(const QByteArray &data, QDateTime &first, QDateTime &last)
{
    if (data.isEmpty() || data.at(0) < 'a' || data.at(0) > 'z') { return false; }
    const QDate date = kNight.addDays(data.at(0) - 'a');
    first = QDateTime(date, QTime(22, 0));
    last = QDateTime(date.addDays(1), QTime(6, 0));
    return true;
}

//! Write the two index files of a pretend card into \a cardDir with \a content.
void makeIndexFiles(const QString &cardDir, const QByteArray &content)
{
    QDir().mkpath(cardDir);
    QVERIFY(writeBytes(cardDir + "/base.idx", "idx " + content));
    QVERIFY(writeBytes(cardDir + "/base.USR", "usr " + content));
}

const QStringList kIndexNames = QStringList() << "base.idx" << "base.USR";

} // namespace

void CardArchiveTests::testIndexActionTable()
{
    QTemporaryDir dir;
    QTemporaryDir card;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");

    // No index set yet: whatever the card has is installed.
    QCOMPARE(archive.indexAction(QDate(2026, 3, 1), QDate(2026, 3, 2)), CardArchive::IndexAction::Replace);
    QVERIFY(!archive.indexFirstDate().isValid());
    QVERIFY(!archive.indexLastDate().isValid());

    makeIndexFiles(card.path(), "v1");
    QVERIFY(archive.installIndexSet(card.path(), kIndexNames, QDate(2026, 1, 1), QDate(2026, 5, 31)));
    QCOMPARE(archive.indexFirstDate(), QDate(2026, 1, 1));
    QCOMPARE(archive.indexLastDate(), QDate(2026, 5, 31));

    // The card has grown, or is unchanged: its set covers the archive's.
    QCOMPARE(archive.indexAction(QDate(2026, 1, 1), QDate(2026, 6, 30)), CardArchive::IndexAction::Replace);
    QCOMPARE(archive.indexAction(QDate(2026, 1, 1), QDate(2026, 5, 31)), CardArchive::IndexAction::Replace);
    // An older copy of the same card.
    QCOMPARE(archive.indexAction(QDate(2026, 1, 1), QDate(2026, 4, 30)), CardArchive::IndexAction::Keep);
    QCOMPARE(archive.indexAction(QDate(2026, 2, 1), QDate(2026, 4, 30)), CardArchive::IndexAction::Keep);
    // The device restarted its index: newer, but no longer reaching back.
    QCOMPARE(archive.indexAction(QDate(2026, 6, 1), QDate(2026, 6, 30)), CardArchive::IndexAction::Retire);
    QCOMPARE(archive.indexAction(QDate(2026, 3, 1), QDate(2026, 6, 30)), CardArchive::IndexAction::Retire);
    // Reaches further back but not as far forward.
    QCOMPARE(archive.indexAction(QDate(2025, 12, 1), QDate(2026, 4, 30)), CardArchive::IndexAction::Retire);
}

void CardArchiveTests::testInstallReplace()
{
    QTemporaryDir dir;
    QTemporaryDir card;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");

    makeIndexFiles(card.path(), "v1");
    QVERIFY(archive.installIndexSet(card.path(), kIndexNames, QDate(2026, 1, 1), QDate(2026, 5, 31)));
    QCOMPARE(fileBytes(dir.filePath("base.idx")), QByteArray("idx v1"));

    makeIndexFiles(card.path(), "v2 longer");
    QVERIFY(archive.installIndexSet(card.path(), kIndexNames, QDate(2026, 1, 1), QDate(2026, 6, 30)));
    QCOMPARE(fileBytes(dir.filePath("base.idx")), QByteArray("idx v2 longer"));
    QCOMPARE(fileBytes(dir.filePath("base.USR")), QByteArray("usr v2 longer"));
    QVERIFY(archive.generationDirs().isEmpty());
    QVERIFY(QDir(dir.path()).entryList(QStringList() << "*.tmp", QDir::Files).isEmpty());

    // The recorded range moved with it, and survives a reload.
    CardArchive again(dir.path());
    QVERIFY(again.load());
    QCOMPARE(again.indexAction(QDate(2026, 1, 1), QDate(2026, 6, 30)), CardArchive::IndexAction::Replace);
    QCOMPARE(again.indexAction(QDate(2026, 1, 1), QDate(2026, 5, 31)), CardArchive::IndexAction::Keep);
}

void CardArchiveTests::testInstallKeep()
{
    QTemporaryDir dir;
    QTemporaryDir card;
    QTemporaryDir olderCard;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");

    makeIndexFiles(card.path(), "newest");
    QVERIFY(archive.installIndexSet(card.path(), kIndexNames, QDate(2026, 1, 1), QDate(2026, 5, 31)));

    makeIndexFiles(olderCard.path(), "older");
    QVERIFY(archive.installIndexSet(olderCard.path(), kIndexNames, QDate(2026, 1, 1), QDate(2026, 4, 30)));
    QCOMPARE(fileBytes(dir.filePath("base.idx")), QByteArray("idx newest"));
    QCOMPARE(fileBytes(dir.filePath("base.USR")), QByteArray("usr newest"));
    QVERIFY(archive.generationDirs().isEmpty());
    QCOMPARE(archive.indexAction(QDate(2026, 1, 1), QDate(2026, 5, 31)), CardArchive::IndexAction::Replace);
}

void CardArchiveTests::testInstallRetire()
{
    QTemporaryDir dir;
    QTemporaryDir card;
    QTemporaryDir restartedCard;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");

    makeIndexFiles(card.path(), "first run");
    QVERIFY(archive.installIndexSet(card.path(), kIndexNames, QDate(2026, 1, 1), QDate(2026, 5, 31)));

    makeIndexFiles(restartedCard.path(), "second run");
    QVERIFY(archive.installIndexSet(restartedCard.path(), kIndexNames, QDate(2026, 6, 1), QDate(2026, 6, 30)));

    const QStringList generations = archive.generationDirs();
    QCOMPARE(generations.size(), 1);
    QCOMPARE(QDir(generations.first()).dirName(), QString("20260531"));
    QVERIFY(QDir::isAbsolutePath(generations.first()));
    QCOMPARE(fileBytes(generations.first() + "/base.idx"), QByteArray("idx first run"));
    QCOMPARE(fileBytes(generations.first() + "/base.USR"), QByteArray("usr first run"));
    QCOMPARE(fileBytes(dir.filePath("base.idx")), QByteArray("idx second run"));
    QCOMPARE(fileBytes(dir.filePath("base.USR")), QByteArray("usr second run"));

    // Retiring again on the same closing date does not overwrite the first generation.
    QTemporaryDir thirdCard;
    makeIndexFiles(thirdCard.path(), "third run");
    QVERIFY(archive.installIndexSet(thirdCard.path(), kIndexNames, QDate(2026, 7, 1), QDate(2026, 7, 2)));
    QTemporaryDir fourthCard;
    makeIndexFiles(fourthCard.path(), "fourth run");
    // Pretend the third run closed on the same date as an existing generation folder.
    QVERIFY(QDir(dir.path()).mkpath(CardArchive::generationDirName() + "/20260702"));
    QVERIFY(archive.installIndexSet(fourthCard.path(), kIndexNames, QDate(2026, 8, 1), QDate(2026, 8, 2)));
    const QStringList all = archive.generationDirs();
    QCOMPARE(all.size(), 4);
    QCOMPARE(QDir(all.at(0)).dirName(), QString("20260531"));     // oldest first
    QCOMPARE(QDir(all.at(3)).dirName(), QString("20260702_2"));
    QCOMPARE(fileBytes(all.at(3) + "/base.idx"), QByteArray("idx third run"));

    CardArchive again(dir.path());
    QVERIFY(again.load());
    QCOMPARE(again.generationDirs().size(), 4);
}

void CardArchiveTests::testInstallFromArchiveFolderItself()
{
    // Migration: the Backup folder already holds the card's index files.
    QTemporaryDir dir;
    makeIndexFiles(dir.path(), "in place");
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");

    QVERIFY(archive.installIndexSet(dir.path(), kIndexNames, QDate(2026, 1, 1), QDate(2026, 5, 31)));
    QCOMPARE(fileBytes(dir.filePath("base.idx")), QByteArray("idx in place"));
    QCOMPARE(archive.indexAction(QDate(2026, 1, 1), QDate(2026, 4, 30)), CardArchive::IndexAction::Keep);
    QVERIFY(archive.mirrorOtherFiles(dir.path(), [](const QString &) { return false; }));
    QVERIFY(QDir(dir.path()).entryList(QStringList() << "*.tmp", QDir::Files).isEmpty());

    // A later card that restarted its index retires the in-place set like any other.
    QTemporaryDir card;
    makeIndexFiles(card.path(), "restarted");
    QVERIFY(archive.installIndexSet(card.path(), kIndexNames, QDate(2026, 6, 1), QDate(2026, 6, 2)));
    QCOMPARE(archive.generationDirs().size(), 1);
    QCOMPARE(fileBytes(archive.generationDirs().first() + "/base.USR"), QByteArray("usr in place"));
}

void CardArchiveTests::testAdoptOrphans()
{
    QTemporaryDir dir;
    {
        CardArchive archive(dir.path());
        archive.setIdentity("BMC", "base");
        QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true), packets(8, 'a'), kPacket),
                 CardArchive::StoreResult::Stored);
        QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true, true, 1, kNight.addDays(1)),
                               packets(12, 'b'), kPacket),
                 CardArchive::StoreResult::Stored);
        // Nothing to adopt while the manifest lists everything.
        QCOMPARE(archive.adoptOrphans(kPacket, describeByFill), 0);
    }
    QVERIFY(QFile::remove(dir.filePath(CardArchive::manifestName())));

    CardArchive lost(dir.path());
    QVERIFY(!lost.load());
    QVERIFY(lost.isWritable());
    QCOMPARE(lost.adoptOrphans(kPacket, describeByFill), 2);

    const QList<CardArchiveEntry> entries = lost.entries();
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).date, kNight);
    QCOMPARE(entries.at(0).file, QString("base_20261114.seg.gz"));
    QCOMPARE(entries.at(0).first, QDateTime(kNight, QTime(22, 0)));
    QCOMPARE(entries.at(0).last, QDateTime(kNight.addDays(1), QTime(6, 0)));
    QCOMPARE(entries.at(0).bytes, qint64(8 * kPacket));
    QCOMPARE(entries.at(0).packets, 8);
    QCOMPARE(entries.at(0).final, true);
    QCOMPARE(entries.at(0).complete, false);
    QCOMPARE(entries.at(0).rule, 0);
    QCOMPARE(entries.at(1).date, kNight.addDays(1));
    QCOMPARE(entries.at(1).packets, 12);
    QCOMPARE(lost.baseName(), QString("base"));

    bool ok = false;
    QCOMPARE(lost.read(entries.at(1), &ok), packets(12, 'b'));
    QVERIFY(ok);

    // The rebuilt manifest was saved.
    CardArchive again(dir.path());
    QVERIFY(again.load());
    QCOMPARE(again.entries().size(), 2);
    QCOMPARE(again.adoptOrphans(kPacket, describeByFill), 0);
}

void CardArchiveTests::testAdoptAfterGarbageManifest()
{
    QTemporaryDir dir;
    {
        CardArchive archive(dir.path());
        archive.setIdentity("BMC", "base");
        QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true), packets(8, 'a'), kPacket),
                 CardArchive::StoreResult::Stored);
        QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true, true, 1, kNight.addDays(1)),
                               packets(12, 'b'), kPacket),
                 CardArchive::StoreResult::Stored);
    }
    QVERIFY(writeBytes(dir.filePath(CardArchive::manifestName()), "not json"));

    CardArchive damaged(dir.path());
    QVERIFY(!damaged.load());
    QVERIFY(damaged.isWritable());
    QVERIFY(damaged.entries().isEmpty());
    QCOMPARE(damaged.adoptOrphans(kPacket, describeByFill), 2);
    QCOMPARE(damaged.entries().size(), 2);

    CardArchive again(dir.path());
    QVERIFY(again.load());
    QCOMPARE(again.entries().size(), 2);

    // A read-only archive is rebuilt in memory, so it can still be imported from, but
    // nothing is written to it.
    QVERIFY(writeBytes(dir.filePath(CardArchive::manifestName()), "not json"));
    CardArchive readOnly(dir.path());
    QVERIFY(!readOnly.load());
    readOnly.setReadOnly(true);
    QCOMPARE(readOnly.adoptOrphans(kPacket, describeByFill), 2);
    QCOMPARE(readOnly.entries().size(), 2);
    bool ok = false;
    QCOMPARE(readOnly.read(readOnly.entries().first(), &ok), packets(8, 'a'));
    QVERIFY(ok);
    QCOMPARE(fileBytes(dir.filePath(CardArchive::manifestName())), QByteArray("not json"));

    // One written by a later version is not second-guessed at all.
    QVERIFY(writeBytes(dir.filePath(CardArchive::manifestName()), "{\"version\":2,\"segments\":[]}"));
    CardArchive newer(dir.path());
    QVERIFY(!newer.load());
    QCOMPARE(newer.adoptOrphans(kPacket, describeByFill), 0);
    QVERIFY(newer.entries().isEmpty());
}

void CardArchiveTests::testArchiveRecognisedWithoutManifest()
{
    QTemporaryDir dir;
    QVERIFY(!CardArchive::isArchive(dir.path()));
    QVERIFY(!CardArchive::isNewerFormat(dir.path()));
    {
        CardArchive archive(dir.path());
        archive.setIdentity("BMC", "base");
        QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true), packets(8, 'a'), kPacket),
                 CardArchive::StoreResult::Stored);
    }
    QVERIFY(CardArchive::isArchive(dir.path()));
    QVERIFY(!CardArchive::isNewerFormat(dir.path()));

    // The manifest is lost, the segments are not: it is still an archive.
    QVERIFY(QFile::remove(dir.filePath(CardArchive::manifestName())));
    QVERIFY(CardArchive::isArchive(dir.path()));

    QVERIFY(writeBytes(dir.filePath(CardArchive::manifestName()), "{\"version\":2,\"segments\":[]}"));
    QVERIFY(CardArchive::isNewerFormat(dir.path()));
    QVERIFY(writeBytes(dir.filePath(CardArchive::manifestName()), "not json"));
    QVERIFY(!CardArchive::isNewerFormat(dir.path()));
}

void CardArchiveTests::testConversionCompleteFlag()
{
    QTemporaryDir dir;
    {
        CardArchive archive(dir.path());
        archive.setIdentity("BMC", "base");
        QVERIFY(!archive.conversionComplete());
        QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true), packets(8, 'a'), kPacket),
                 CardArchive::StoreResult::Stored);
        QVERIFY(!archive.conversionComplete());
        QVERIFY(archive.markConversionComplete());
        QVERIFY(archive.conversionComplete());
    }
    CardArchive again(dir.path());
    QVERIFY(again.load());
    QVERIFY(again.conversionComplete());
    // It survives later stores.
    QCOMPARE(again.store(night(QTime(22, 0), QTime(6, 0), true, true, 1, kNight.addDays(1)), packets(8, 'b'), kPacket),
             CardArchive::StoreResult::Stored);
    CardArchive third(dir.path());
    QVERIFY(third.load());
    QVERIFY(third.conversionComplete());

    CardArchive readOnly(dir.path());
    QVERIFY(readOnly.load());
    readOnly.setReadOnly(true);
    QVERIFY(!readOnly.markConversionComplete() || readOnly.conversionComplete());
}

void CardArchiveTests::testOrphanBadGzipIgnored()
{
    QTemporaryDir dir;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");
    QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true), packets(8, 'a'), kPacket),
             CardArchive::StoreResult::Stored);

    const QString garbage = segmentDir(dir) + "/base_20261120.seg.gz";
    QVERIFY(writeBytes(garbage, "definitely not gzip"));
    // Decompresses, but the loader's decoder does not recognise the packets.
    const QString unknown = segmentDir(dir) + "/base_20261121.seg.gz";
    QVERIFY(writeBytes(unknown, gCompress(packets(4, '!'))));
    // Decompresses, but is not a whole number of packets.
    const QString ragged = segmentDir(dir) + "/base_20261122.seg.gz";
    QVERIFY(writeBytes(ragged, gCompress(QByteArray(300, 'c'))));
    // Not named like a segment of any date.
    const QString unnamed = segmentDir(dir) + "/stray.seg.gz";
    QVERIFY(writeBytes(unnamed, gCompress(packets(4, 'd'))));

    QCOMPARE(archive.adoptOrphans(kPacket, describeByFill), 0);
    QCOMPARE(archive.entries().size(), 1);
    QVERIFY(QFile::exists(garbage));
    QVERIFY(QFile::exists(unknown));
    QVERIFY(QFile::exists(ragged));
    QVERIFY(QFile::exists(unnamed));

    // A good orphan beside them is still adopted, including one with a "_2" name.
    QVERIFY(writeBytes(segmentDir(dir) + "/base_20261116_2.seg.gz", gCompress(packets(6, 'c'))));
    QCOMPARE(archive.adoptOrphans(kPacket, describeByFill), 1);
    QCOMPARE(archive.entries().size(), 2);
    QCOMPARE(archive.entries().at(1).date, QDate(2026, 11, 16));
}

void CardArchiveTests::testMirrorOtherFiles()
{
    QTemporaryDir dir;
    QTemporaryDir card;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");

    QVERIFY(QDir(card.path()).mkpath("System/sub"));
    QVERIFY(writeBytes(card.filePath("lang.BIN"), "language pack"));
    QVERIFY(writeBytes(card.filePath("System/sub/info.txt"), "nested"));
    QVERIFY(writeBytes(card.filePath("base.000"), "ring data, never copied"));
    QVERIFY(writeBytes(card.filePath("same.dat"), "12345"));
    QVERIFY(writeBytes(dir.filePath("same.dat"), "abcde"));          // same size: left alone
    QVERIFY(writeBytes(dir.filePath("changed.dat"), "old"));
    QVERIFY(writeBytes(card.filePath("changed.dat"), "new and longer"));
    QVERIFY(writeBytes(dir.filePath("only-in-archive.dat"), "keep me"));

    const auto skipRing = [](const QString &relativePath) { return relativePath.endsWith(".000"); };
    QVERIFY(archive.mirrorOtherFiles(card.path(), skipRing));

    QCOMPARE(fileBytes(dir.filePath("lang.BIN")), QByteArray("language pack"));
    QCOMPARE(fileBytes(dir.filePath("System/sub/info.txt")), QByteArray("nested"));
    QVERIFY(!QFile::exists(dir.filePath("base.000")));
    QCOMPARE(fileBytes(dir.filePath("same.dat")), QByteArray("abcde"));
    QCOMPARE(fileBytes(dir.filePath("changed.dat")), QByteArray("new and longer"));
    QCOMPARE(fileBytes(dir.filePath("only-in-archive.dat")), QByteArray("keep me"));
    QVERIFY(QDir(dir.path()).entryList(QStringList() << "*.tmp", QDir::Files).isEmpty());

    // The skip function sees paths relative to the card, with forward slashes.
    QStringList seen;
    QVERIFY(archive.mirrorOtherFiles(card.path(), [&seen](const QString &relativePath) {
        seen << relativePath;
        return true;
    }));
    QVERIFY(seen.contains("System/sub/info.txt"));
    QVERIFY(seen.contains("lang.BIN"));

    // Nothing is written to a read-only archive.
    QVERIFY(writeBytes(card.filePath("later.dat"), "later"));
    archive.setReadOnly(true);
    QVERIFY(!archive.mirrorOtherFiles(card.path(), skipRing));
    QVERIFY(!QFile::exists(dir.filePath("later.dat")));
}

// ---------------------------------------------------------------------------------
// Telling "cannot be read just now" from "damaged", and never destroying either
// ---------------------------------------------------------------------------------

void CardArchiveTests::testReadStatus()
{
    QTemporaryDir dir;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");
    QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true), packets(8, 'a'), kPacket),
             CardArchive::StoreResult::Stored);
    const CardArchiveEntry entry = archive.entries().first();
    const QString path = segmentDir(dir) + "/" + entry.file;
    const QByteArray good = fileBytes(path);

    bool ok = false;
    CardArchive::ReadStatus status = CardArchive::ReadStatus::Damaged;
    QCOMPARE(archive.read(entry, &ok, &status), packets(8, 'a'));
    QVERIFY(ok);
    QCOMPARE(status, CardArchive::ReadStatus::Ok);

    // The file is there but cannot be opened (locked, not downloaded from a synced
    // folder): nothing is known about its content.
    QVERIFY(QFile::remove(path));
    QVERIFY(QDir().mkpath(path));               // a folder of that name cannot be opened as a file
    QVERIFY(archive.read(entry, &ok, &status).isEmpty());
    QVERIFY(!ok);
    QCOMPARE(status, CardArchive::ReadStatus::Unavailable);
    QVERIFY(QDir().rmdir(path));

    // The file opens and its content is wrong.
    QVERIFY(writeBytes(path, "garbage"));
    QVERIFY(archive.read(entry, &ok, &status).isEmpty());
    QVERIFY(!ok);
    QCOMPARE(status, CardArchive::ReadStatus::Damaged);
    QVERIFY(writeBytes(path, gCompress(packets(8, 'z'))));     // valid gzip, wrong checksum
    QVERIFY(archive.read(entry, &ok, &status).isEmpty());
    QCOMPARE(status, CardArchive::ReadStatus::Damaged);

    QVERIFY(writeBytes(path, good));
    QCOMPARE(archive.read(entry, &ok, &status), packets(8, 'a'));
    QCOMPARE(status, CardArchive::ReadStatus::Ok);
}

void CardArchiveTests::testQuarantine()
{
    QTemporaryDir dir;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");
    const CardArchiveRecord rec = night(QTime(22, 0), QTime(6, 0), true);
    QCOMPARE(archive.store(rec, packets(8, 'a'), kPacket), CardArchive::StoreResult::Stored);
    const CardArchiveEntry entry = archive.entries().first();
    const QString path = segmentDir(dir) + "/" + entry.file;
    QVERIFY(writeBytes(path, "damaged bytes"));

    // A read-only archive is left exactly as it is.
    archive.setReadOnly(true);
    QVERIFY(!archive.quarantine(entry));
    QCOMPARE(archive.entries().size(), 1);
    QVERIFY(QFile::exists(path));
    archive.setReadOnly(false);

    // Set aside, not deleted: whatever is in the file may still be worth something.
    QVERIFY(archive.quarantine(entry));
    QVERIFY(archive.entries().isEmpty());
    QVERIFY(!QFile::exists(path));
    const QString kept = segmentDir(dir) + "/" + CardArchive::damagedDirName() + "/" + entry.file;
    QCOMPARE(fileBytes(kept), QByteArray("damaged bytes"));
    QVERIFY(archive.wants(rec));

    // The folder of set-aside files is not taken for segments.
    CardArchive again(dir.path());
    QVERIFY(again.load());
    QVERIFY(again.entries().isEmpty());
    QCOMPARE(again.adoptOrphans(kPacket, describeByFill), 0);

    // A second file of the same name is kept beside the first.
    QCOMPARE(again.store(rec, packets(8, 'a'), kPacket), CardArchive::StoreResult::Stored);
    QVERIFY(writeBytes(path, "damaged again"));
    QVERIFY(again.quarantine(again.entries().first()));
    QCOMPARE(QDir(segmentDir(dir) + "/" + CardArchive::damagedDirName()).entryList(QDir::Files).size(), 2);
    QCOMPARE(fileBytes(kept), QByteArray("damaged bytes"));
}

void CardArchiveTests::testReplaceKeepsOldSegmentUntilManifestSaved()
{
    QTemporaryDir dir;
    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");
    QCOMPARE(archive.store(night(QTime(22, 0), QTime(2, 0), false), packets(10, 'a'), kPacket),
             CardArchive::StoreResult::Stored);
    const CardArchiveEntry before = archive.entries().first();
    const QString manifest = dir.filePath(CardArchive::manifestName());

    // The manifest cannot be written (here: something is in its way).
    QVERIFY(QFile::remove(manifest));
    QVERIFY(QDir().mkpath(manifest));
    QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true), packets(30, 'b'), kPacket),
             CardArchive::StoreResult::Failed);

    // The night the manifest still describes is untouched and still reads, and the
    // replacement that could not be recorded is not left lying about.
    QCOMPARE(archive.entries().size(), 1);
    QCOMPARE(archive.entries().first().file, before.file);
    bool ok = false;
    QCOMPARE(archive.read(archive.entries().first(), &ok), packets(10, 'a'));
    QVERIFY(ok);
    QCOMPARE(segmentFiles(dir), QStringList() << before.file);

    // Once the manifest can be written again the replacement goes through.
    QVERIFY(QDir().rmdir(manifest));
    QCOMPARE(archive.store(night(QTime(22, 0), QTime(6, 0), true), packets(30, 'b'), kPacket),
             CardArchive::StoreResult::Replaced);
    QCOMPARE(segmentFiles(dir).size(), 1);
    CardArchive again(dir.path());
    QVERIFY(again.load());
    QCOMPARE(again.entries().size(), 1);
    QCOMPARE(again.read(again.entries().first(), &ok), packets(30, 'b'));
    QVERIFY(ok);
}

void CardArchiveTests::testInstallRetiresWhenRangeUnknown()
{
    // Index files are in the folder but the archive has no record of what they cover (the
    // manifest was lost). They are not overwritten on a guess: they are moved aside.
    QTemporaryDir dir;
    QTemporaryDir card;
    makeIndexFiles(dir.path(), "of unknown range");
    makeIndexFiles(card.path(), "from the card");

    CardArchive archive(dir.path());
    archive.setIdentity("BMC", "base");
    QVERIFY(archive.installIndexSet(card.path(), kIndexNames, QDate(2026, 6, 1), QDate(2026, 6, 30)));

    QCOMPARE(fileBytes(dir.filePath("base.idx")), QByteArray("idx from the card"));
    const QStringList generations = archive.generationDirs();
    QCOMPARE(generations.size(), 1);
    QCOMPARE(fileBytes(generations.first() + "/base.idx"), QByteArray("idx of unknown range"));
    QCOMPARE(fileBytes(generations.first() + "/base.USR"), QByteArray("usr of unknown range"));

    // With the range now known, a card that covers it replaces it as usual.
    makeIndexFiles(card.path(), "from the card, later");
    QVERIFY(archive.installIndexSet(card.path(), kIndexNames, QDate(2026, 6, 1), QDate(2026, 7, 31)));
    QCOMPARE(archive.generationDirs().size(), 1);
    QCOMPARE(fileBytes(dir.filePath("base.idx")), QByteArray("idx from the card, later"));
}
