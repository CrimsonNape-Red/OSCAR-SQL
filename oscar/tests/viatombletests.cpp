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
#include "SleepLib/crypto.h"
#include "SleepLib/viatomble/viatomble_dialect.h"
#include "SleepLib/viatomble/viatomble_downloader.h"
#include "SleepLib/viatomble/viatomble_o2rings.h"
#include "SleepLib/viatomble/viatomble_link.h"
#include "SleepLib/viatomble/viatomble_protocol.h"
#include "SleepLib/viatomble/viatomble_usb.h"

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
    int setTimeReplyDelay = 0;
    QString lastSetTime;
    bool echoSeq = true;
    QSet<int> dropContentReplyOnce;     // chunk numbers whose first reply is lost
    QMap<int, int> delayContentReplyOnce;   // chunk number -> ms the first reply is late
    int lateOnlyChunk = -1;                 // this chunk's only reply is the first, and it's late
    int lateOnlyDelay = 0;
    int delayInfoReplyOnce = 0;             // ms the first info reply is late
    int endReplyDelay = 0;                  // ms before READ_END is acknowledged
    int startReplyDelay = 0;                // ms before READ_START is answered
    int loseLinkAtChunk = -1;
    bool onlyAcknowledged = false;          // ignores writes until useAcknowledgedWrites()
    bool ignoresUsbHello = false;           // gives no answer to CMD_USB_HELLO
    bool acknowledged = false;
    int acknowledgedSwitches = 0;
    QList<QByteArray> sent;

    bool useAcknowledgedWrites() override
    {
        if (!onlyAcknowledged || acknowledged) {
            return false;
        }
        acknowledged = true;
        acknowledgedSwitches++;
        return true;
    }

    void send(const QByteArray &frame) override
    {
        sent.append(frame);
        if (silent || (onlyAcknowledged && !acknowledged)) {
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
        case CMD_INFO: {
            const QByteArray r = makeReply(0, 0, infoJson + QByteArray(1, '\0'));
            if (delayInfoReplyOnce > 0) {
                const int delay = delayInfoReplyOnce;
                delayInfoReplyOnce = 0;
                QTimer::singleShot(delay, this, [this, r]() { reply(r); });
            } else {
                reply(r);
            }
            break;
        }
        case CMD_READ_START: {
            const QString name = QString::fromLatin1(payload.left(payload.indexOf('\0')));
            if (!files.contains(name)) {
                reply(makeReply(1, 0, QByteArray()));
            } else {
                m_current = files.value(name);
                const QByteArray r = makeReply(0, 0, le32(m_current.size()));
                QTimer::singleShot(startReplyDelay, this, [this, r]() { reply(r); });
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
            if (seq == lateOnlyChunk) {
                if (!m_lateSent) {
                    m_lateSent = true;
                    const QByteArray late = makeReply(0, echoSeq ? seq : 0, m_current.mid(seq * 512, 512));
                    QTimer::singleShot(lateOnlyDelay, this, [this, late]() { reply(late); });
                }
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
        case CMD_READ_END: {
            const QByteArray r = makeReply(0, 0, QByteArray());
            QTimer::singleShot(endReplyDelay, this, [this, r]() { reply(r); });
            break;
        }
        case CMD_USB_HELLO:
            if (!ignoresUsbHello) {
                reply(makeReply(0, 0, QByteArray(4, '\0')));
            }
            break;
        case CMD_PARA_SYNC:
        {
            lastSetTime = QJsonDocument::fromJson(payload).object().value("SetTIME").toString();
            const QByteArray r = makeReply(refuseSetTime ? 1 : 0, 0, QByteArray());
            QTimer::singleShot(setTimeReplyDelay, this, [this, r]() { reply(r); });
            break;
        }
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
    bool m_lateSent = false;
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

void ViatomBleTests::testWaitsLongerForFileOpen()
{
    // Opening a file can take the device longer than other replies (a SleepU took
    // just over 2 s). The open must not be resent while the device is still on it.
    QTemporaryDir dir;
    FakeDevice dev;
    dev.files["20261025220000"] = pattern(800, 5);
    dev.startReplyDelay = 150;
    Downloader dl(&dev);
    dl.setReplyTimeout(50);
    dl.setOpenTimeout(400);
    QSignalSpy saved(&dl, &Downloader::fileSaved);
    QSignalSpy finished(&dl, &Downloader::downloadFinished);

    dl.download(dev.files.keys(), dir.path(), false);
    QVERIFY(finished.wait(5000));

    QCOMPARE(saved.size(), 1);
    QCOMPARE(dev.countSent(CMD_READ_START), 1);
    QCOMPARE(readFile(dir.filePath("20261025220000")), pattern(800, 5));
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

void ViatomBleTests::testDropsLateReplyAfterGivingUp()
{
    // Chunk 1 is never answered in time: the downloader gives up on the first file and
    // moves on. The one late reply must not be taken for the next command's reply.
    QTemporaryDir dir;
    FakeDevice dev;
    dev.files["20261025220000"] = pattern(2000, 10);
    dev.files["20261026230000"] = pattern(400, 11);   // one chunk, so lateOnlyChunk does not apply
    dev.lateOnlyChunk = 1;
    dev.lateOnlyDelay = 150;          // after the last retry (4 x 30 ms), within the pause
    dev.endReplyDelay = 20;           // slow acknowledgements, as on a real link: without the
    dev.startReplyDelay = 20;         // pause, the late chunk lands while the next file opens
    Downloader dl(&dev);
    dl.setReplyTimeout(30);
    QSignalSpy failed(&dl, &Downloader::fileFailed);
    QSignalSpy saved(&dl, &Downloader::fileSaved);
    QSignalSpy finished(&dl, &Downloader::downloadFinished);

    dl.download(dev.files.keys(), dir.path(), false);
    QVERIFY(finished.wait(5000));

    QCOMPARE(failed.size(), 1);
    QCOMPARE(failed.first().first().toString(), QStringLiteral("20261025220000"));
    QCOMPARE(saved.size(), 1);
    QCOMPARE(readFile(dir.filePath("20261026230000")), pattern(400, 11));
    QCOMPARE(folderEntries(dir.path()), QStringList({"20261026230000"}));
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

void ViatomBleTests::testUsbReports()
{
    // A frame travels as reports of 32 bytes: length, up to 31 data bytes, zero padding.
    const QByteArray info = buildRequest(CMD_INFO, 0);
    QList<QByteArray> reports = Usb::toReports(info);
    QCOMPARE(reports.size(), 1);
    QCOMPARE(reports[0], QByteArray(1, char(info.size())) + info + QByteArray(32 - 1 - info.size(), '\0'));
    QCOMPARE(Usb::fromReport(reports[0]), info);

    const QByteArray big = buildRequest(CMD_READ_START, 0, QByteArray(40, 'x'));   // 48 bytes
    reports = Usb::toReports(big);
    QCOMPARE(reports.size(), 2);
    QCOMPARE(int(quint8(reports[0][0])), 31);
    QCOMPARE(int(quint8(reports[1][0])), big.size() - 31);
    QCOMPARE(Usb::fromReport(reports[0]) + Usb::fromReport(reports[1]), big);
    for (const QByteArray &r : reports) {
        QCOMPARE(r.size(), Usb::REPORT_SIZE);
    }

    // A length the report can't hold is not data, e.g. the bridge's setup report.
    QVERIFY(Usb::fromReport(Usb::bridgeSetupReport()).isEmpty());
    QVERIFY(Usb::fromReport(QByteArray()).isEmpty());
    QCOMPARE(Usb::bridgeSetupReport(), QByteArray::fromHex("ffc783cc30") + QByteArray(27, '\0'));
}

void ViatomBleTests::testUsbInfo_data()
{
    QTest::addColumn<bool>("ignoresHello");
    QTest::newRow("hello answered") << false;
    QTest::newRow("hello ignored") << true;
}

void ViatomBleTests::testUsbInfo()
{
    QFETCH(bool, ignoresHello);
    FakeDevice dev;
    dev.infoJson = "{\"Model\":\"1654\",\"SN\":\"20032B4004\",\"FileList\":\"20260715005945,\"}";
    dev.ignoresUsbHello = ignoresHello;
    Downloader dl(&dev, std::make_unique<ClassicUsbDialect>());
    dl.setReplyTimeout(20);
    dl.setMaxRetries(1);
    QSignalSpy info(&dl, &Downloader::infoReceived);
    QSignalSpy failed(&dl, &Downloader::failed);

    dl.requestInfo();
    QVERIFY(info.wait(5000));
    QCOMPARE(failed.count(), 0);

    // The hello goes first, as O2 Insight Pro sends it; info is asked for either way.
    QCOMPARE(quint8(dev.sent.first()[1]), quint8(CMD_USB_HELLO));
    QCOMPARE(dev.countSent(CMD_USB_HELLO), ignoresHello ? 2 : 1);
    QCOMPARE(dev.countSent(CMD_INFO), 1);
    const DeviceInfo got = info.first().first().value<DeviceInfo>();
    QCOMPARE(got.serialNumber, QStringLiteral("20032B4004"));
    QCOMPARE(got.files, QStringList({"20260715005945"}));
}

void ViatomBleTests::testSwitchesToAcknowledgedWrites()
{
    FakeDevice dev;
    dev.infoJson = "{\"SN\":\"1234\"}";
    dev.onlyAcknowledged = true;
    Downloader dl(&dev);
    dl.setReplyTimeout(20);
    dl.setMaxRetries(2);
    QSignalSpy info(&dl, &Downloader::infoReceived);
    QSignalSpy failed(&dl, &Downloader::failed);

    dl.requestInfo();
    QVERIFY(info.wait(5000));
    QCOMPARE(failed.count(), 0);
    QCOMPARE(dev.acknowledgedSwitches, 1);
    QCOMPARE(dev.countSent(CMD_INFO), 4);   // three ignored, then one acknowledged

    // A silent device that can't switch still fails after the usual retries.
    FakeDevice quiet;
    quiet.silent = true;
    quiet.onlyAcknowledged = true;
    Downloader dl2(&quiet);
    dl2.setReplyTimeout(20);
    dl2.setMaxRetries(2);
    QSignalSpy failed2(&dl2, &Downloader::failed);
    dl2.requestInfo();
    QVERIFY(failed2.wait(5000));
    QCOMPARE(quiet.acknowledgedSwitches, 1);
    QCOMPARE(quiet.countSent(CMD_INFO), 6);
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

void ViatomBleTests::testLateInfoReplyIsNotTakenForSetTime()
{
    // The info reply is slow, so the request is retried and two replies arrive. The
    // second must not be taken as the reply to the clock request that follows.
    FakeDevice dev;
    dev.infoJson = "{\"SN\":\"2051300123\",\"FileList\":\"\"}";
    dev.delayInfoReplyOnce = 80;      // the retry goes out at 50 ms and is answered at once
    dev.refuseSetTime = true;
    dev.setTimeReplyDelay = 40;       // so without the pause, the stray info reply (80 ms) comes first
    Downloader dl(&dev);
    dl.setReplyTimeout(50);
    QSignalSpy info(&dl, &Downloader::infoReceived);
    QSignalSpy timeSet(&dl, &Downloader::timeSet);
    QObject::connect(&dl, &Downloader::infoReceived, &dl, [&dl]() { dl.setTime(QDateTime::currentDateTime()); });

    dl.requestInfo();
    QVERIFY(timeSet.wait(5000));

    QCOMPARE(info.size(), 1);
    QVERIFY(!timeSet.first().first().toString().isEmpty());   // the refusal is reported
}

// ---------------------------------------------------------------------------------------
// O2Ring S family

namespace {

QByteArray makeA5Reply(quint8 cmd, quint8 type, quint8 seq, const QByteArray &payload)
{
    QByteArray frame;
    frame.append(char(0xA5));
    frame.append(char(cmd));
    frame.append(char(~cmd));
    frame.append(char(type));
    frame.append(char(seq));
    frame.append(char(payload.size() & 0xFF));
    frame.append(char(payload.size() >> 8));
    frame.append(payload);
    frame.append(char(crc8(frame.constData(), frame.size())));
    return frame;
}

//! Plays an O2Ring S: checks the handshake token, hands out a session key, then
//! encrypts and decrypts every payload as the SDK describes.
class FakeO2RingS : public Link
{
  public:
    QMap<QString, QByteArray> files;
    QByteArray sessionKey = QByteArray::fromHex("00112233445566778899aabbccddeeff");
    bool wrongKeyOnReplies = false;      // encrypt replies with another key
    bool liveDataDuringTransfer = false; // push an unsolicited frame while sending a file
    bool requiresEncryption = false;     // ignore plain requests until the handshake
    bool ignoresHandshake = false;       // never answer the handshake (like the S8-AW)
    QByteArray lastUtcTime;
    QList<QByteArray> sent;
    QString handshakeError;

    void send(const QByteArray &frame) override
    {
        sent.append(frame);
        const auto *b = reinterpret_cast<const quint8 *>(frame.constData());
        QCOMPARE(b[0], quint8(0xA5));
        QCOMPARE(quint8(b[2]), quint8(~b[1]));
        QCOMPARE(b[3], quint8(0));
        QCOMPARE(tableCrc8(frame.left(frame.size() - 1)), quint8(b[frame.size() - 1]));
        const quint8 cmd = b[1];
        const quint8 seq = b[4];
        QByteArray payload = frame.mid(7, b[5] | (b[6] << 8));
        if (m_session && !payload.isEmpty()) {
            QByteArray plain;
            QVERIFY(O2RingS::decryptPayload(sessionKey, payload, plain));
            payload = plain;
        }
        if (cmd == O2RingS::CMD_ENCRYPT ? ignoresHandshake : (requiresEncryption && !m_session)) {
            return;
        }

        switch (cmd) {
        case O2RingS::CMD_ENCRYPT: {
            const QByteArray token = O2RingS::scramble(payload);
            const QByteArray expected = O2RingS::handshakeToken(0).left(8);
            if (token.left(8) != expected || token.mid(8, 4) != QByteArray("0000")) {
                handshakeError = QStringLiteral("bad token");
                reply(cmd, 0xF3, seq, QByteArray());
                return;
            }
            QByteArray info;
            info.append(char(1));
            info.append(char(16));
            info.append(QByteArray(2, '\0'));
            info.append(sessionKey);
            reply(cmd, 1, seq, O2RingS::scramble(info));
            m_session = true;
            return;
        }
        case O2RingS::CMD_GET_INFO: {
            QByteArray info(60, '\0');
            info[24] = char(2026 & 0xFF);
            info[25] = char(2026 >> 8);
            info[26] = 10; info[27] = 27; info[28] = 21; info[29] = 55; info[30] = 7;
            const QByteArray sn("2224300099");
            info[37] = char(sn.size());
            info.replace(38, sn.size(), sn);
            reply(cmd, 1, seq, info);
            return;
        }
        case O2RingS::CMD_GET_FILE_LIST: {
            QByteArray list;
            list.append(char(files.size()));
            for (const QString &name : files.keys()) {
                QByteArray n = name.toLatin1();
                n.append(QByteArray(16 - n.size(), '\0'));
                list.append(n);
            }
            reply(cmd, 1, seq, list);
            return;
        }
        case O2RingS::CMD_SET_UTC_TIME:
            lastUtcTime = payload;
            reply(cmd, 1, seq, QByteArray());
            return;
        case O2RingS::CMD_READ_FILE_START: {
            QByteArray name = payload.left(16);
            name.truncate(name.indexOf('\0') < 0 ? 16 : name.indexOf('\0'));
            QCOMPARE(payload.mid(16, 4), QByteArray(4, '\0'));
            if (!files.contains(QString::fromLatin1(name))) {
                reply(cmd, 0xE0, seq, QByteArray());   // "file not found"
                return;
            }
            m_current = files.value(QString::fromLatin1(name));
            reply(cmd, 1, seq, le32(m_current.size()));
            return;
        }
        case O2RingS::CMD_READ_FILE_DATA: {
            const auto *o = reinterpret_cast<const quint8 *>(payload.constData());
            const int offset = o[0] | (o[1] << 8) | (o[2] << 16) | (o[3] << 24);
            if (liveDataDuringTransfer && offset > 0 && !m_pushedLive) {
                m_pushedLive = true;
                reply(0x04, 1, 0, QByteArray(20, '\x55'));   // live data, not asked for
            }
            reply(cmd, 1, seq, m_current.mid(offset, 300));
            return;
        }
        case O2RingS::CMD_READ_FILE_END:
            reply(cmd, 1, seq, QByteArray());
            return;
        default:
            QFAIL("unexpected command");
        }
    }

  private:
    void reply(quint8 cmd, quint8 type, quint8 seq, const QByteArray &plain)
    {
        QByteArray payload = plain;
        if (m_session && !plain.isEmpty()) {
            const QByteArray key = wrongKeyOnReplies ? QByteArray(16, '\x42') : sessionKey;
            QVERIFY(O2RingS::encryptPayload(key, plain, payload));
        }
        const QByteArray frame = makeA5Reply(cmd, type, seq, payload);
        for (int i = 0; i < frame.size(); i += 20) {
            const QByteArray chunk = frame.mid(i, 20);
            QTimer::singleShot(0, this, [this, chunk]() { emit dataReceived(chunk); });
        }
    }

    bool m_session = false;
    bool m_pushedLive = false;
    QByteArray m_current;
};

} // namespace

void ViatomBleTests::testAes128EcbPkcs7()
{
    // FIPS-197 appendix C.1
    const QByteArray key = QByteArray::fromHex("000102030405060708090a0b0c0d0e0f");
    const QByteArray plain = QByteArray::fromHex("00112233445566778899aabbccddeeff");
    QByteArray cipher;
    QCOMPARE(encrypt_aes128_ecb_pkcs7(key, plain, cipher), OK);
    QCOMPARE(cipher.size(), 32);   // a full block of padding follows
    QCOMPARE(cipher.left(16), QByteArray::fromHex("69c4e0d86a7b0430d8cdb78070b4c55a"));

    QByteArray back;
    QCOMPARE(decrypt_aes128_ecb_pkcs7(key, cipher, back), OK);
    QCOMPARE(back, plain);
    for (int n : {0, 1, 15, 17, 512}) {
        const QByteArray data = pattern(n, n);
        QCOMPARE(encrypt_aes128_ecb_pkcs7(key, data, cipher), OK);
        QCOMPARE(cipher.size() % 16, 0);
        QCOMPARE(decrypt_aes128_ecb_pkcs7(key, cipher, back), OK);
        QCOMPARE(back, data);
    }
    QVERIFY(decrypt_aes128_ecb_pkcs7(key, cipher.left(cipher.size() - 1), back) != OK);   // length
    QVERIFY(decrypt_aes128_ecb_pkcs7(QByteArray(16, '\x01'), cipher, back) != OK);        // padding
}

void ViatomBleTests::testO2RingSFrames()
{
    const QByteArray req = O2RingS::buildRequest(O2RingS::CMD_GET_FILE_LIST, 7);
    QCOMPARE(req.left(7), QByteArray::fromHex("a5f10e00070000"));
    QCOMPARE(quint8(req.at(7)), tableCrc8(req.left(7)));

    O2RingS::FrameSplitter splitter;
    QByteArray stream = QByteArray::fromHex("00ff") + makeA5Reply(0xF2, 1, 9, le32(1234))
                      + makeA5Reply(0xF2, 0xE0, 10, QByteArray());
    QList<Reply> replies;
    for (int i = 0; i < stream.size(); i += 3) {
        replies += splitter.feed(stream.mid(i, 3));
    }
    QCOMPARE(replies.size(), 2);
    QCOMPARE(replies[0].cmd, 0xF2);
    QVERIFY(replies[0].ok());
    QCOMPARE(replies[0].seq, quint16(9));
    QCOMPARE(fileSizeFromReadStart(replies[0]), qint64(1234));
    QVERIFY(!replies[1].ok());
    QCOMPARE(replies[1].status, quint8(0xE0));
    QCOMPARE(O2RingS::statusFromType(0), quint8(0xFF));
}

void ViatomBleTests::testO2RingSHandshake()
{
    // Every other byte of the app key, "0000", then the time shifted by 0..3 bits.
    const QByteArray token = O2RingS::handshakeToken(0x12345678);
    QCOMPARE(token.size(), 16);
    QCOMPARE(token.left(8), QByteArray::fromHex("c2cfdad8a8f7c435"));
    QCOMPARE(token.mid(8, 4), QByteArray("0000"));
    QCOMPARE(token.mid(12, 4), QByteArray::fromHex("783c9ecf"));

    QCOMPARE(O2RingS::scramble(O2RingS::scramble(pattern(40, 3))), pattern(40, 3));

    const QByteArray key = QByteArray::fromHex("0f0e0d0c0b0a09080706050403020100");
    QByteArray reply;
    reply.append(char(1)).append(char(16)).append(QByteArray(2, '\0')).append(key);
    QCOMPARE(O2RingS::sessionKeyFromHandshake(O2RingS::scramble(reply)), key);
    QVERIFY(O2RingS::sessionKeyFromHandshake(O2RingS::scramble(reply.left(10))).isEmpty());
}

void ViatomBleTests::testO2RingSPayloads()
{
    const QByteArray start = O2RingS::readStartPayload("20261027220000", 0x01020304);
    QCOMPARE(start.size(), 20);
    QCOMPARE(start.left(16), QByteArray("20261027220000\0\0", 16));
    QCOMPARE(start.mid(16), QByteArray::fromHex("04030201"));

    const QByteArray utc = O2RingS::utcTimePayload(QDateTime(QDate(2026, 10, 27), QTime(21, 5, 9)), -5 * 3600);
    QCOMPARE(utc, QByteArray::fromHex("ea070a1b150509ce"));   // 2026, 10, 27, 21:05:09, -50 tenths

    QByteArray list;
    list.append(char(2)).append(QByteArray("20261025220000\0\0", 16)).append(QByteArray("20261026230000\0\0", 16));
    QCOMPARE(O2RingS::parseFileList(list), QStringList({"20261025220000", "20261026230000"}));
    QVERIFY(O2RingS::parseFileList(QByteArray(1, '\x05')).isEmpty());   // count larger than the data

    QVERIFY(!O2RingS::parseInfo(QByteArray(10, '\0')).valid);
}

void ViatomBleTests::testO2RingSSession_data()
{
    QTest::addColumn<bool>("encrypted");
    QTest::newRow("plain, handshake ignored") << false;
    QTest::newRow("encrypted, plain ignored") << true;
}

void ViatomBleTests::testO2RingSSession()
{
    QFETCH(bool, encrypted);
    QTemporaryDir dir;
    FakeO2RingS dev;
    dev.files["20261025220000"] = pattern(1000, 20);   // several 300-byte blocks
    dev.files["20261026230000"] = pattern(300, 21);
    dev.liveDataDuringTransfer = true;
    dev.requiresEncryption = encrypted;
    dev.ignoresHandshake = !encrypted;
    Downloader dl(&dev, std::make_unique<O2RingSDialect>());
    dl.setReplyTimeout(30);
    dl.setOpenTimeout(30);
    dl.setMaxRetries(1);
    QSignalSpy info(&dl, &Downloader::infoReceived);
    QSignalSpy timeSet(&dl, &Downloader::timeSet);
    QSignalSpy saved(&dl, &Downloader::fileSaved);
    QSignalSpy finished(&dl, &Downloader::downloadFinished);

    dl.requestInfo();
    QVERIFY(info.wait(5000));
    QVERIFY(dev.handshakeError.isEmpty());
    const DeviceInfo got = info.first().first().value<DeviceInfo>();
    QCOMPARE(got.serialNumber, QStringLiteral("2224300099"));
    QCOMPARE(got.deviceTime, QStringLiteral("2026-10-27,21:55:07"));
    QCOMPARE(got.files, QStringList(dev.files.keys()));

    dl.setTime(QDateTime(QDate(2026, 10, 27), QTime(22, 0, 0)));
    QVERIFY(timeSet.wait(5000));
    QCOMPARE(timeSet.first().first().toString(), QString());
    QCOMPARE(dev.lastUtcTime.left(7), QByteArray::fromHex("ea070a1b160000"));

    dl.download(got.files, dir.path(), false);
    QVERIFY(finished.wait(5000));
    QCOMPARE(saved.size(), 2);
    for (auto it = dev.files.cbegin(); it != dev.files.cend(); ++it) {
        QCOMPARE(readFile(dir.filePath(it.key())), it.value());
    }

    int handshake = -1;
    for (int i = 0; i < dev.sent.size(); i++) {
        if (quint8(dev.sent[i].at(1)) == O2RingS::CMD_ENCRYPT) {
            handshake = i;
            break;
        }
    }
    QCOMPARE(handshake >= 0, encrypted);
    for (int i = 0; i < dev.sent.size(); i++) {
        const QByteArray &frame = dev.sent[i];
        const int len = quint8(frame.at(5)) | (quint8(frame.at(6)) << 8);
        if (encrypted && i > handshake) {
            QCOMPARE(len % 16, 0);   // an encrypted (block-sized) payload, or none
        } else if (quint8(frame.at(1)) == O2RingS::CMD_READ_FILE_START) {
            QCOMPARE(len, 20);       // the plain name and offset
        }
    }
}

void ViatomBleTests::testO2RingSWrongKey()
{
    FakeO2RingS dev;
    dev.wrongKeyOnReplies = true;
    dev.requiresEncryption = true;
    Downloader dl(&dev, std::make_unique<O2RingSDialect>());
    dl.setReplyTimeout(30);
    dl.setMaxRetries(1);
    QSignalSpy failed(&dl, &Downloader::failed);

    dl.requestInfo();
    QVERIFY(failed.wait(5000));
    QVERIFY(failed.first().first().toString().contains(QStringLiteral("0xf3")));
}

void ViatomBleTests::testO2RingSSilent()
{
    // Answers neither plain requests nor the handshake: one failure, after both tries.
    FakeO2RingS dev;
    dev.requiresEncryption = true;
    dev.ignoresHandshake = true;
    Downloader dl(&dev, std::make_unique<O2RingSDialect>());
    dl.setReplyTimeout(20);
    dl.setMaxRetries(1);
    QSignalSpy failed(&dl, &Downloader::failed);
    QSignalSpy info(&dl, &Downloader::infoReceived);

    dl.requestInfo();
    QVERIFY(failed.wait(5000));
    QCOMPARE(failed.size(), 1);
    QCOMPARE(info.size(), 0);
    QVERIFY(failed.first().first().toString().contains(QStringLiteral("encrypted")));
    QCOMPARE(dev.sent.size(), 4);   // plain info twice, then the handshake twice
    QCOMPARE(quint8(dev.sent[0].at(1)), quint8(O2RingS::CMD_GET_INFO));
    QCOMPARE(quint8(dev.sent[3].at(1)), quint8(O2RingS::CMD_ENCRYPT));
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
