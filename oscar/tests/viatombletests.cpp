/* Viatom BLE Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

#include "viatombletests.h"
#include "SleepLib/viatomble/viatomble_downloader.h"
#include "SleepLib/viatomble/viatomble_link.h"
#include "SleepLib/viatomble/viatomble_protocol.h"

using namespace ViatomBle;

namespace {

// The lookup table oxytool uses (from the ViHealth SDK's BleCRC), as an independent reference.
quint8 tableCrc8(const QByteArray &data)
{
    static quint8 table[256];
    static bool built = false;
    if (!built) {
        for (int i = 0; i < 256; i++) {
            quint8 c = quint8(i);
            for (int bit = 0; bit < 8; bit++) {
                c = (c & 0x80) ? quint8((c << 1) ^ 0x07) : quint8(c << 1);
            }
            table[i] = c;
        }
        built = true;
    }
    quint8 crc = 0;
    for (char b : data) {
        crc = table[crc ^ quint8(b)];
    }
    return crc;
}

QByteArray makeReply(quint8 status, quint16 seq, const QByteArray &payload)
{
    QByteArray frame;
    frame.append(char(0x55));
    frame.append(char(status));
    frame.append(char(~status));
    frame.append(char(seq & 0xFF));
    frame.append(char(seq >> 8));
    frame.append(char(payload.size() & 0xFF));
    frame.append(char(payload.size() >> 8));
    frame.append(payload);
    frame.append(char(crc8(frame.constData(), frame.size())));
    return frame;
}

QByteArray le32(quint32 v)
{
    QByteArray b;
    for (int i = 0; i < 4; i++) {
        b.append(char((v >> (8 * i)) & 0xFF));
    }
    return b;
}

QByteArray pattern(int size, int seed)
{
    QByteArray data(size, '\0');
    for (int i = 0; i < size; i++) {
        data[i] = char((i * 31 + seed) & 0xFF);
    }
    return data;
}

//! Plays a classic Viatom device: answers each request with a reply split into
//! 20-byte notifications, delivered from the event loop like a real link.
class FakeDevice : public Link
{
  public:
    QMap<QString, QByteArray> files;
    QByteArray infoJson;
    bool silent = false;
    bool refuseSetTime = false;
    QString lastSetTime;
    bool echoSeq = true;
    QSet<int> dropContentReplyOnce;     // chunk numbers whose first reply is lost
    QMap<int, int> delayContentReplyOnce;   // chunk number -> ms the first reply is late
    int loseLinkAtChunk = -1;
    QList<QByteArray> sent;

    void send(const QByteArray &frame) override
    {
        sent.append(frame);
        if (silent) {
            return;
        }
        const auto *b = reinterpret_cast<const quint8 *>(frame.constData());
        QVERIFY(frame.size() >= 8);
        QCOMPARE(b[0], quint8(0xAA));
        QCOMPARE(quint8(b[2]), quint8(~b[1]));
        QCOMPARE(tableCrc8(frame.left(frame.size() - 1)), quint8(b[frame.size() - 1]));
        const quint8 cmd = b[1];
        const quint16 seq = quint16(b[3] | (b[4] << 8));
        const QByteArray payload = frame.mid(7, b[5] | (b[6] << 8));

        switch (cmd) {
        case CMD_INFO:
            reply(makeReply(0, 0, infoJson + QByteArray(1, '\0')));
            break;
        case CMD_READ_START: {
            const QString name = QString::fromLatin1(payload.left(payload.indexOf('\0')));
            if (!files.contains(name)) {
                reply(makeReply(1, 0, QByteArray()));
            } else {
                m_current = files.value(name);
                reply(makeReply(0, 0, le32(m_current.size())));
            }
            break;
        }
        case CMD_READ_CONTENT: {
            if (seq == loseLinkAtChunk) {
                QTimer::singleShot(0, this, [this]() { emit linkLost(QStringLiteral("link lost")); });
                return;
            }
            if (dropContentReplyOnce.remove(seq)) {
                return;
            }
            const QByteArray r = makeReply(0, echoSeq ? seq : 0, m_current.mid(seq * 512, 512));
            if (delayContentReplyOnce.contains(seq)) {
                const int delay = delayContentReplyOnce.take(seq);
                QTimer::singleShot(delay, this, [this, r]() { reply(r); });
            } else {
                reply(r);
            }
            break;
        }
        case CMD_READ_END:
            reply(makeReply(0, 0, QByteArray()));
            break;
        case CMD_PARA_SYNC:
            lastSetTime = QJsonDocument::fromJson(payload).object().value("SetTIME").toString();
            reply(makeReply(refuseSetTime ? 1 : 0, 0, QByteArray()));
            break;
        default:
            QFAIL("unexpected command");
        }
    }

    int countSent(quint8 cmd) const
    {
        int n = 0;
        for (const QByteArray &f : sent) {
            if (quint8(f[1]) == cmd) {
                n++;
            }
        }
        return n;
    }

  private:
    void reply(const QByteArray &frame)
    {
        for (int i = 0; i < frame.size(); i += 20) {
            const QByteArray chunk = frame.mid(i, 20);
            QTimer::singleShot(0, this, [this, chunk]() { emit dataReceived(chunk); });
        }
    }

    QByteArray m_current;
};

QStringList folderEntries(const QString &path)
{
    return QDir(path).entryList(QDir::Files, QDir::Name);
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return f.readAll();
}

} // namespace

// The downloader runs on timers and queued signals. The test runner creates no
// application object, so these tests need their own for the event loop.
void ViatomBleTests::initTestCase()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char name[] = "test";
        static char *argv[] = { name, nullptr };
        m_app = new QCoreApplication(argc, argv);
    }
}

void ViatomBleTests::cleanupTestCase()
{
    delete m_app;
    m_app = nullptr;
}

// ---------------------------------------------------------------------------------------
// Protocol

void ViatomBleTests::testCrc8MatchesLookupTable()
{
    // Spot values from the SDK's table.
    QCOMPARE(crc8("\x01", 1), quint8(0x07));
    QCOMPARE(crc8("\x80", 1), quint8(0x89));
    QCOMPARE(crc8("\xFF", 1), quint8(0xF3));
    for (int n = 0; n < 300; n += 7) {
        const QByteArray data = pattern(n, n);
        QCOMPARE(crc8(data.constData(), data.size()), tableCrc8(data));
    }
}

void ViatomBleTests::testBuildRequest()
{
    const QByteArray info = buildRequest(CMD_INFO, 0);
    QCOMPARE(info.left(7), QByteArray::fromHex("aa14eb00000000"));
    QCOMPARE(quint8(info.at(7)), tableCrc8(info.left(7)));

    const QByteArray start = buildRequest(CMD_READ_START, 0x0102, QByteArray("ab", 3));
    QCOMPARE(start.size(), 11);
    QCOMPARE(start.left(7), QByteArray::fromHex("aa03fc02010300"));
    QCOMPARE(start.mid(7, 3), QByteArray("ab", 3));
}

void ViatomBleTests::testSplitterHandlesFragmentsAndJunk()
{
    const QByteArray first = makeReply(0, 0, QByteArray("hello"));
    const QByteArray second = makeReply(1, 7, QByteArray());
    QByteArray badCrc = makeReply(0, 3, QByteArray("bad"));
    badCrc[badCrc.size() - 1] = char(badCrc.at(badCrc.size() - 1) ^ 0x5A);
    const QByteArray third = makeReply(0, 0x1234, pattern(600, 1));

    QByteArray stream;
    stream += QByteArray::fromHex("0102");   // junk
    stream += first;
    stream += QByteArray::fromHex("55");     // a stray header byte
    stream += second;
    stream += badCrc;
    stream += third;

    FrameSplitter splitter;
    QList<Reply> replies;
    for (int i = 0; i < stream.size(); i++) {
        replies += splitter.feed(stream.mid(i, 1));
    }

    QCOMPARE(replies.size(), 3);
    QCOMPARE(replies[0].payload, QByteArray("hello"));
    QVERIFY(replies[0].ok());
    QCOMPARE(replies[1].status, quint8(1));
    QCOMPARE(replies[1].seq, quint16(7));
    QVERIFY(replies[1].payload.isEmpty());
    QCOMPARE(replies[2].seq, quint16(0x1234));
    QCOMPARE(replies[2].payload, pattern(600, 1));
    QVERIFY(splitter.discardedBytes() > 0);
}

void ViatomBleTests::testDeviceInfo()
{
    const QByteArray json = "{\"Model\":\"1652\",\"SN\":\"2051300123 \",\"SoftwareVer\":\"1.4.0\","
                            "\"CurBAT\":\"85%\",\"CurTIME\":\"2026-10-27,08:00:00\","
                            "\"FileList\":\"20261025220000,20261026230000,\"}";
    DeviceInfo info = DeviceInfo::fromPayload(json + QByteArray(3, '\0'));
    QVERIFY(info.valid);
    QCOMPARE(info.model, QStringLiteral("1652"));
    QCOMPARE(info.serialNumber, QStringLiteral("2051300123"));
    QCOMPARE(info.battery, QStringLiteral("85%"));
    QCOMPARE(info.files, QStringList({"20261025220000", "20261026230000"}));

    QVERIFY(!DeviceInfo::fromPayload("not json").valid);
    QVERIFY(DeviceInfo::fromPayload("{}").files.isEmpty());
}

void ViatomBleTests::testFileSizeFromReadStart()
{
    Reply r;
    r.payload = le32(0x00012345);
    QCOMPARE(fileSizeFromReadStart(r), qint64(0x12345));
    r.payload = QByteArray("\x01\x02", 2);
    QCOMPARE(fileSizeFromReadStart(r), qint64(-1));
}

void ViatomBleTests::testSetTimePayload()
{
    const QDateTime t(QDate(2026, 10, 27), QTime(9, 5, 3));
    QCOMPARE(setTimePayload(t), QByteArray("{\"SetTIME\":\"2026-10-27,09:05:03\"}"));
    QCOMPARE(parseDeviceTime("2026-10-27,09:05:03"), t);
    QVERIFY(!parseDeviceTime("27/10/2026 09:05").isValid());
}

// ---------------------------------------------------------------------------------------
// Downloader

void ViatomBleTests::testDownloadAllFiles()
{
    QTemporaryDir dir;
    FakeDevice dev;
    dev.files["20261025220000"] = pattern(1300, 1);  // three chunks, the last one short
    dev.files["20261026230000"] = pattern(512, 2);   // exactly one chunk
    dev.files["20261027010000"] = QByteArray();      // empty file
    Downloader dl(&dev);
    QSignalSpy saved(&dl, &Downloader::fileSaved);
    QSignalSpy finished(&dl, &Downloader::downloadFinished);

    dl.download(dev.files.keys(), dir.path(), false);
    QVERIFY(finished.wait(5000));

    QCOMPARE(finished.first().first().toBool(), false);
    QCOMPARE(saved.size(), 3);
    QCOMPARE(folderEntries(dir.path()), QStringList(dev.files.keys()));
    for (auto it = dev.files.cbegin(); it != dev.files.cend(); ++it) {
        QCOMPARE(readFile(dir.filePath(it.key())), it.value());
    }
    QCOMPARE(dev.countSent(CMD_READ_END), 3);
    QVERIFY(!dl.isBusy());
}

void ViatomBleTests::testSkipsExistingFiles()
{
    QTemporaryDir dir;
    FakeDevice dev;
    dev.files["20261025220000"] = pattern(800, 1);
    {
        QFile f(dir.filePath("20261025220000"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(pattern(800, 9));    // same size, different content: must stay untouched
    }
    Downloader dl(&dev);
    QSignalSpy skipped(&dl, &Downloader::fileSkipped);
    QSignalSpy finished(&dl, &Downloader::downloadFinished);

    dl.download(dev.files.keys(), dir.path(), false);
    QVERIFY(finished.wait(5000));

    QCOMPARE(skipped.size(), 1);
    QCOMPARE(readFile(dir.filePath("20261025220000")), pattern(800, 9));
    QCOMPARE(dev.countSent(CMD_READ_CONTENT), 0);
    QCOMPARE(dev.countSent(CMD_READ_END), 1);
}

void ViatomBleTests::testOverwritesExistingFiles()
{
    QTemporaryDir dir;
    FakeDevice dev;
    dev.files["20261025220000"] = pattern(800, 1);
    {
        QFile f(dir.filePath("20261025220000"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(pattern(800, 9));
    }
    Downloader dl(&dev);
    QSignalSpy saved(&dl, &Downloader::fileSaved);
    QSignalSpy finished(&dl, &Downloader::downloadFinished);

    dl.download(dev.files.keys(), dir.path(), true);
    QVERIFY(finished.wait(5000));

    QCOMPARE(saved.size(), 1);
    QCOMPARE(readFile(dir.filePath("20261025220000")), pattern(800, 1));
    QCOMPARE(folderEntries(dir.path()), QStringList({"20261025220000"}));
}

void ViatomBleTests::testRetriesAfterLostReply()
{
    QTemporaryDir dir;
    FakeDevice dev;
    dev.files["20261025220000"] = pattern(2000, 3);
    dev.dropContentReplyOnce = {1, 3};
    Downloader dl(&dev);
    dl.setReplyTimeout(50);
    QSignalSpy saved(&dl, &Downloader::fileSaved);
    QSignalSpy finished(&dl, &Downloader::downloadFinished);

    dl.download(dev.files.keys(), dir.path(), false);
    QVERIFY(finished.wait(5000));

    QCOMPARE(saved.size(), 1);
    QCOMPARE(readFile(dir.filePath("20261025220000")), pattern(2000, 3));
    QCOMPARE(dev.countSent(CMD_READ_CONTENT), 4 + 2);
}

void ViatomBleTests::testDropsLateReplyAfterRetry_data()
{
    QTest::addColumn<bool>("echo");
    QTest::addColumn<int>("chunk");
    QTest::newRow("echo, late chunk 0") << true << 0;    // before echo is known
    QTest::newRow("echo, late chunk 2") << true << 2;
    QTest::newRow("no echo, late chunk 0") << false << 0;
    QTest::newRow("no echo, late chunk 2") << false << 2;
}

void ViatomBleTests::testDropsLateReplyAfterRetry()
{
    QFETCH(bool, echo);
    QFETCH(int, chunk);

    // The first reply to one chunk arrives after the timeout, so the request is sent
    // again and that chunk's reply arrives twice: once for the retry, then once late.
    QTemporaryDir dir;
    FakeDevice dev;
    dev.files["20261025220000"] = pattern(3000, 4);
    dev.echoSeq = echo;
    dev.delayContentReplyOnce[chunk] = 120;
    Downloader dl(&dev);
    dl.setReplyTimeout(50);
    QSignalSpy saved(&dl, &Downloader::fileSaved);
    QSignalSpy finished(&dl, &Downloader::downloadFinished);

    dl.download(dev.files.keys(), dir.path(), false);
    QVERIFY(finished.wait(5000));

    QCOMPARE(saved.size(), 1);
    QCOMPARE(readFile(dir.filePath("20261025220000")), pattern(3000, 4));
}

void ViatomBleTests::testDeviceRefusesFile()
{
    QTemporaryDir dir;
    FakeDevice dev;
    dev.files["20261026230000"] = pattern(100, 5);
    Downloader dl(&dev);
    QSignalSpy failed(&dl, &Downloader::fileFailed);
    QSignalSpy saved(&dl, &Downloader::fileSaved);
    QSignalSpy finished(&dl, &Downloader::downloadFinished);

    dl.download({"missing", "20261026230000"}, dir.path(), false);
    QVERIFY(finished.wait(5000));

    QCOMPARE(failed.size(), 1);
    QCOMPARE(failed.first().first().toString(), QStringLiteral("missing"));
    QCOMPARE(saved.size(), 1);
    QCOMPARE(folderEntries(dir.path()), QStringList({"20261026230000"}));
}

void ViatomBleTests::testLinkLostRemovesPartialFile()
{
    QTemporaryDir dir;
    FakeDevice dev;
    dev.files["20261025220000"] = pattern(3000, 6);
    dev.files["20261026230000"] = pattern(100, 7);
    dev.loseLinkAtChunk = 2;
    Downloader dl(&dev);
    QSignalSpy fileFailed(&dl, &Downloader::fileFailed);
    QSignalSpy failed(&dl, &Downloader::failed);

    dl.download(dev.files.keys(), dir.path(), false);
    QVERIFY(failed.wait(5000));

    QCOMPARE(fileFailed.size(), 1);
    QVERIFY(folderEntries(dir.path()).isEmpty());   // no .part left, second file not started
    QVERIFY(!dl.isBusy());
}

void ViatomBleTests::testCancelStopsAndCleansUp()
{
    QTemporaryDir dir;
    FakeDevice dev;
    dev.files["20261025220000"] = pattern(5000, 8);
    dev.files["20261026230000"] = pattern(100, 9);
    Downloader dl(&dev);
    QSignalSpy finished(&dl, &Downloader::downloadFinished);
    QObject::connect(&dl, &Downloader::fileProgress, &dl, [&dl](const QString &, qint64 received, qint64) {
        if (received >= 1024) {
            dl.cancel();
        }
    });

    dl.download(dev.files.keys(), dir.path(), false);
    QVERIFY(finished.wait(5000));

    QCOMPARE(finished.first().first().toBool(), true);
    QVERIFY(folderEntries(dir.path()).isEmpty());
    QCOMPARE(dev.countSent(CMD_READ_END), 1);         // the open file was closed on the device
    QCOMPARE(dev.countSent(CMD_READ_START), 1);       // the second file was never started
}

void ViatomBleTests::testInfo()
{
    FakeDevice dev;
    dev.infoJson = "{\"Model\":\"1652\",\"SN\":\"2051300123\",\"FileList\":\"20261025220000,\"}";
    Downloader dl(&dev);
    QSignalSpy info(&dl, &Downloader::infoReceived);

    dl.requestInfo();
    QVERIFY(info.wait(5000));

    const DeviceInfo got = info.first().first().value<DeviceInfo>();
    QCOMPARE(got.serialNumber, QStringLiteral("2051300123"));
    QCOMPARE(got.files, QStringList({"20261025220000"}));
}

void ViatomBleTests::testInfoTimeout()
{
    FakeDevice dev;
    dev.silent = true;
    Downloader dl(&dev);
    dl.setReplyTimeout(20);
    dl.setMaxRetries(2);
    QSignalSpy failed(&dl, &Downloader::failed);

    dl.requestInfo();
    QVERIFY(failed.wait(5000));

    QCOMPARE(dev.countSent(CMD_INFO), 3);
    QVERIFY(!dl.isBusy());
}

void ViatomBleTests::testSetTime()
{
    FakeDevice dev;
    Downloader dl(&dev);
    QSignalSpy timeSet(&dl, &Downloader::timeSet);

    dl.setTime(QDateTime(QDate(2026, 10, 27), QTime(22, 30, 0)));
    QVERIFY(timeSet.wait(5000));
    QCOMPARE(timeSet.first().first().toString(), QString());
    QCOMPARE(dev.lastSetTime, QStringLiteral("2026-10-27,22:30:00"));
    QVERIFY(!dl.isBusy());

    // A refusal is reported, and the downloader can carry on.
    dev.refuseSetTime = true;
    dl.setTime(QDateTime::currentDateTime());
    QVERIFY(timeSet.wait(5000));
    QVERIFY(!timeSet.last().first().toString().isEmpty());
    QVERIFY(!dl.isBusy());
}

#ifdef OSCAR_HAVE_BLUETOOTH
#include "SleepLib/viatomble/viatomble_scanner.h"

void ViatomBleTests::testDeviceFamilyFromName()
{
    QCOMPARE(deviceFamilyFromName("O2Ring 2345"), DeviceFamily::Classic);
    QCOMPARE(deviceFamilyFromName("O2Ring"), DeviceFamily::Classic);
    QCOMPARE(deviceFamilyFromName("SleepU 0123"), DeviceFamily::Classic);
    QCOMPARE(deviceFamilyFromName("PO2 77"), DeviceFamily::Classic);
    // O2Ring S family names start like classic ones
    QCOMPARE(deviceFamilyFromName("O2Ring S 2345"), DeviceFamily::O2RingS);
    QCOMPARE(deviceFamilyFromName("O2Ring SC 2345"), DeviceFamily::O2RingS);
    QCOMPARE(deviceFamilyFromName("PO2B 77"), DeviceFamily::O2RingS);
    // Not Viatom, or only a prefix of a longer word
    QCOMPARE(deviceFamilyFromName("O2Ringer"), DeviceFamily::NotViatom);
    QCOMPARE(deviceFamilyFromName("Galaxy Buds"), DeviceFamily::NotViatom);
    QCOMPARE(deviceFamilyFromName(""), DeviceFamily::NotViatom);
}
#endif
