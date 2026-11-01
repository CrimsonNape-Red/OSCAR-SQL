/* EMAY oximeter Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QDir>
#include <QFile>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

#include "emaytests.h"
#include "SleepLib/loader_plugins/cms50f37_loader.h"
#include "SleepLib/viatomble/emay.h"
#include "SleepLib/viatomble/viatomble_link.h"

using namespace ViatomBle;
using namespace ViatomBle::Emay;

namespace {

struct FakeRecording
{
    quint16 serial;
    QDateTime start;
    QByteArray data;   // SpO2/pulse pairs
};

//! An EMO-80 as EMAY's app expects it to behave, sending replies in 20-byte notifications.
class FakeEmay : public Link
{
  public:
    QList<FakeRecording> recordings;
    QSet<int> dropPackageOnce;    // package numbers whose first reply is lost
    bool silent = false;
    bool deleted = false;
    QByteArray setTimeBcd;
    QList<Frame> received;

    void send(const QByteArray &bytes) override
    {
        for (const Frame &f : m_in.feed(bytes)) {
            received.append(f);
            handle(f);
        }
    }

  private:
    void reply(quint8 cmd, const QByteArray &payload)
    {
        const QByteArray frame = buildFrame(cmd, TYPE_ACK, payload);
        QTimer::singleShot(0, this, [this, frame]() {
            for (int i = 0; i < frame.size(); i += 20) {
                emit dataReceived(frame.mid(i, 20));
            }
        });
    }

    static QByteArray lp(const QByteArray &s)
    {
        return QByteArray(1, char(s.size())) + s;
    }

    void sendHeader()
    {
        const FakeRecording &r = recordings.at(m_listIndex);
        const quint32 duration = quint32(r.data.size() / 2);
        QByteArray p;
        p.append(char(0));
        p.append(char(m_listIndex == recordings.size() - 1 ? 1 : 0));
        p.append(char(r.serial >> 8)).append(char(r.serial & 0xFF));
        p.append(char(1));   // user
        p.append(bcdTime(r.start));
        p.append(char(duration >> 24)).append(char(duration >> 16)).append(char(duration >> 8)).append(char(duration));
        p.append(char(0));   // checksum
        reply(CMD_LIST, p);
    }

    void sendPackage()
    {
        const QByteArray &d = m_current.data;
        const int from = (m_package - 1) * m_packageSize;
        const bool last = from + m_packageSize >= d.size();
        if (dropPackageOnce.remove(m_package)) {
            return;
        }
        QByteArray p;
        p.append(char(0));
        p.append(char(last ? 1 : 0));
        p.append(char(m_package >> 8)).append(char(m_package & 0xFF));
        p.append(d.mid(from, m_packageSize));
        reply(CMD_DATA, p);
    }

    void handle(const Frame &f)
    {
        if (silent) {
            return;
        }
        switch (f.cmd) {
        case CMD_DEVICE_INFO:
            reply(f.cmd, QByteArray(1, '\0') + lp("EMO-80") + lp("E80A123") + lp("AA:BB:CC:DD:EE:FF")
                             + lp("1.0.3") + lp(bcdTime(QDateTime(QDate(2026, 11, 1), QTime(7, 30, 0)))));
            return;
        case CMD_BATTERY:
            reply(f.cmd, QByteArray::fromHex("000150"));   // 80%
            return;
        case CMD_STORAGE: {
            QByteArray p = QByteArray::fromHex("00" "00010000" "00008000" "0010");
            p.append(char(0)).append(char(recordings.size()));
            reply(f.cmd, p);
            return;
        }
        case CMD_LIST:
            if (f.type == TYPE_REQUEST) {
                m_listIndex = 0;
            } else {
                m_listIndex++;
            }
            if (m_listIndex < recordings.size()) {
                sendHeader();
            }
            return;
        case CMD_DATA:
            if (f.type == TYPE_REQUEST) {
                const quint16 serial = quint16((quint8(f.payload[1]) << 8) | quint8(f.payload[2]));
                m_package = (quint8(f.payload[3]) << 8) | quint8(f.payload[4]);
                m_packageSize = (quint8(f.payload[5]) << 8) | quint8(f.payload[6]);
                for (const FakeRecording &r : recordings) {
                    if (r.serial == serial) {
                        m_current = r;
                    }
                }
            } else {
                m_package++;
            }
            sendPackage();
            return;
        case CMD_SET_TIME:
            setTimeBcd = f.payload;
            reply(f.cmd, QByteArray(1, '\0'));
            return;
        case 0xA9:
            deleted = true;
            reply(f.cmd, QByteArray(1, '\0'));
            return;
        }
    }

    Emay::FrameSplitter m_in;
    int m_listIndex = 0;
    FakeRecording m_current;
    int m_package = 1;
    int m_packageSize = 200;
};

QByteArray pairs(int seconds, quint8 spo2, quint8 pulse)
{
    QByteArray d;
    for (int i = 0; i < seconds; i++) {
        d.append(char(spo2)).append(char(pulse));
    }
    return d;
}

void addTwoRecordings(FakeEmay &dev)
{
    dev.recordings.append({ 3, QDateTime(QDate(2026, 7, 15), QTime(0, 59, 45)), pairs(450, 96, 62) });   // 900 bytes: 5 packages
    dev.recordings.append({ 4, QDateTime(QDate(2026, 7, 16), QTime(23, 1, 2)), pairs(30, 94, 70) });
}

std::unique_ptr<EmayDownloader> makeDownloader(FakeEmay &dev)
{
    auto dl = std::make_unique<EmayDownloader>(&dev);
    dl->setReplyTimeout(100);
    dl->setAckDelay(1);
    return dl;
}

} // namespace

void EmayTests::initTestCase()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char name[] = "test";
        static char *argv[] = { name, nullptr };
        m_app = new QCoreApplication(argc, argv);
    }
}

void EmayTests::cleanupTestCase()
{
    delete m_app;
    m_app = nullptr;
}

void EmayTests::testFrame()
{
    const QByteArray f = buildFrame(CMD_DEVICE_INFO, TYPE_REQUEST);
    // E2 A5 2F 00 00 E3 xor(A5 2F 00 00)
    QCOMPARE(f, QByteArray::fromHex("e2a52f0000e3") + QByteArray(1, char(0xA5 ^ 0x2F)));

    const QByteArray g = buildFrame(CMD_DATA, TYPE_ACK, QByteArray(1, '\0'));
    QCOMPARE(g.size(), 8);
    QCOMPARE(quint8(g[6]), quint8(0xE3));
    QCOMPARE(quint8(g[7]), quint8(0xA8 ^ 'O' ^ 0 ^ 1 ^ 0));
}

void EmayTests::testSplitterHandlesFragmentsAndJunk()
{
    Emay::FrameSplitter s;
    const QByteArray a = buildFrame(CMD_BATTERY, TYPE_ACK, QByteArray::fromHex("000150"));
    const QByteArray b = buildFrame(CMD_STORAGE, TYPE_ACK, QByteArray(13, '\x01'));
    QByteArray broken = buildFrame(CMD_LIST, TYPE_ACK, QByteArray(3, '\0'));
    broken[broken.size() - 1] = char(broken[broken.size() - 1] ^ 0xFF);   // bad checksum

    const QByteArray stream = QByteArray::fromHex("0011") + a + broken + b;
    QList<Frame> frames;
    for (int i = 0; i < stream.size(); i += 3) {
        frames += s.feed(stream.mid(i, 3));
    }
    QCOMPARE(frames.size(), 2);
    QCOMPARE(frames[0].cmd, quint8(CMD_BATTERY));
    QCOMPARE(frames[0].payload, QByteArray::fromHex("000150"));
    QCOMPARE(frames[1].cmd, quint8(CMD_STORAGE));
    QCOMPARE(frames[1].payload.size(), 13);
}

void EmayTests::testBcdTime()
{
    const QDateTime t(QDate(2026, 7, 15), QTime(23, 59, 8));
    QCOMPARE(bcdTime(t), QByteArray::fromHex("260715235908"));
    QCOMPARE(fromBcdTime(bcdTime(t)), t);
    QVERIFY(!fromBcdTime(QByteArray::fromHex("261399000000")).isValid());
}

void EmayTests::testDeviceInfo()
{
    auto lp = [](const QByteArray &s) { return QByteArray(1, char(s.size())) + s; };
    DeviceInfo info;
    QVERIFY(parseDeviceInfo(QByteArray(1, '\0') + lp("EMO-80") + lp("SN1") + lp("MAC") + lp("1.2")
                                + lp(QByteArray::fromHex("261101073000")), info));
    QCOMPARE(info.model, QStringLiteral("EMO-80"));
    QCOMPARE(info.serialNumber, QStringLiteral("SN1"));
    QCOMPARE(info.softwareVersion, QStringLiteral("1.2"));
    QCOMPARE(info.deviceTime, QStringLiteral("2026-11-01,07:30:00"));

    DeviceInfo refused;
    QVERIFY(!parseDeviceInfo(QByteArray::fromHex("01"), refused));
}

void EmayTests::testSpo2FileReadsBack()
{
    QTemporaryDir dir;
    const QDateTime start(QDate(2026, 7, 15), QTime(0, 59, 45));
    QByteArray data = pairs(20, 95, 60);
    data[2 * 3] = char(127);       // SpO2 out of range: no reading
    data[2 * 4 + 1] = char(255);   // pulse 255: no reading
    data.append(char(97));         // an odd byte at the end is dropped

    QVERIFY(spo2File(start, QByteArray()).isEmpty());
    const QString path = dir.filePath(QStringLiteral("EMAY_20260715005945_3.spo2"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(spo2File(start, data));
    f.close();

    // OSCAR's own reader for these files.
    CMS50F37Loader reader;
    QVERIFY(reader.Open(path));
    QCOMPARE(reader.startTime(), start);
    QVERIFY(reader.oxisessions.contains(start));
    const QVector<OxiRecord> &recs = *reader.oxisessions.value(start);
    QCOMPARE(recs.size(), 20);
    QCOMPARE(int(recs[0].spo2), 95);
    QCOMPARE(int(recs[0].pulse), 60);
    QCOMPARE(int(recs[3].spo2), 0);
    QCOMPARE(int(recs[3].pulse), 60);
    QCOMPARE(int(recs[4].spo2), 95);
    QCOMPARE(int(recs[4].pulse), 0);
    reader.trashRecords();
}

void EmayTests::testInfoAndList()
{
    FakeEmay dev;
    addTwoRecordings(dev);
    auto dl = makeDownloader(dev);
    QSignalSpy info(dl.get(), &DownloaderBase::infoReceived);
    QSignalSpy failed(dl.get(), &DownloaderBase::failed);

    dl->requestInfo();
    QVERIFY(info.wait(3000));
    QCOMPARE(failed.count(), 0);
    const DeviceInfo i = info.first().first().value<DeviceInfo>();
    QCOMPARE(i.model, QStringLiteral("EMO-80"));
    QCOMPARE(i.serialNumber, QStringLiteral("E80A123"));
    QCOMPARE(i.battery, QStringLiteral("80%"));
    QCOMPARE(i.files, QStringList({ QStringLiteral("EMAY_20260715005945_3.spo2"),
                                    QStringLiteral("EMAY_20260716230102_4.spo2") }));
    QVERIFY(!dl->isBusy());
}

void EmayTests::testNothingStored()
{
    FakeEmay dev;
    auto dl = makeDownloader(dev);
    QSignalSpy info(dl.get(), &DownloaderBase::infoReceived);
    dl->requestInfo();
    QVERIFY(info.wait(3000));
    QVERIFY(info.first().first().value<DeviceInfo>().files.isEmpty());
    // The list isn't asked for when the device holds nothing.
    for (const Frame &f : dev.received) {
        QVERIFY(f.cmd != CMD_LIST);
    }
}

void EmayTests::testDownload()
{
    QTemporaryDir dir;
    FakeEmay dev;
    addTwoRecordings(dev);
    auto dl = makeDownloader(dev);
    QSignalSpy info(dl.get(), &DownloaderBase::infoReceived);
    dl->requestInfo();
    QVERIFY(info.wait(3000));
    const QStringList files = info.first().first().value<DeviceInfo>().files;

    QSignalSpy saved(dl.get(), &DownloaderBase::fileSaved);
    QSignalSpy finished(dl.get(), &DownloaderBase::downloadFinished);
    QSignalSpy fileFailed(dl.get(), &DownloaderBase::fileFailed);
    dl->download(files, dir.path(), false);
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.first().first().toBool(), false);
    QCOMPARE(fileFailed.count(), 0);
    QCOMPARE(saved.count(), 2);

    CMS50F37Loader reader;
    QVERIFY(reader.Open(saved.at(0).at(1).toString()));
    QCOMPARE(reader.startTime(), dev.recordings[0].start);
    QCOMPARE(reader.oxisessions.value(reader.startTime())->size(), 450);
    reader.trashRecords();
    QVERIFY(!QFileInfo::exists(saved.at(0).at(1).toString() + QStringLiteral(".part")));
}

void EmayTests::testSkipsExisting()
{
    QTemporaryDir dir;
    FakeEmay dev;
    addTwoRecordings(dev);
    auto dl = makeDownloader(dev);
    QSignalSpy info(dl.get(), &DownloaderBase::infoReceived);
    dl->requestInfo();
    QVERIFY(info.wait(3000));
    const QStringList files = info.first().first().value<DeviceInfo>().files;
    QFile existing(QDir(dir.path()).filePath(files.first()));
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.write("x");
    existing.close();

    QSignalSpy skipped(dl.get(), &DownloaderBase::fileSkipped);
    QSignalSpy saved(dl.get(), &DownloaderBase::fileSaved);
    QSignalSpy finished(dl.get(), &DownloaderBase::downloadFinished);
    dl->download(files, dir.path(), false);
    QVERIFY(finished.wait(5000));
    QCOMPARE(skipped.count(), 1);
    QCOMPARE(saved.count(), 1);
}

void EmayTests::testResendsLostPackage()
{
    QTemporaryDir dir;
    FakeEmay dev;
    addTwoRecordings(dev);
    dev.dropPackageOnce = { 3 };
    auto dl = makeDownloader(dev);
    QSignalSpy info(dl.get(), &DownloaderBase::infoReceived);
    dl->requestInfo();
    QVERIFY(info.wait(3000));
    const QStringList files = info.first().first().value<DeviceInfo>().files;

    QSignalSpy saved(dl.get(), &DownloaderBase::fileSaved);
    QSignalSpy finished(dl.get(), &DownloaderBase::downloadFinished);
    dl->download({ files.first() }, dir.path(), false);
    QVERIFY(finished.wait(5000));
    QCOMPARE(saved.count(), 1);

    // After the lost package, the recording is asked for again from that package.
    bool askedAgain = false;
    for (const Frame &f : dev.received) {
        if (f.cmd == CMD_DATA && f.type == TYPE_REQUEST && f.payload.size() >= 5 && f.payload[4] == 3) {
            askedAgain = true;
        }
    }
    QVERIFY(askedAgain);
    CMS50F37Loader reader;
    QVERIFY(reader.Open(saved.first().at(1).toString()));
    QCOMPARE(reader.oxisessions.value(reader.startTime())->size(), 450);
    reader.trashRecords();
}

void EmayTests::testNeverDeletes()
{
    QTemporaryDir dir;
    FakeEmay dev;
    addTwoRecordings(dev);
    auto dl = makeDownloader(dev);
    QSignalSpy info(dl.get(), &DownloaderBase::infoReceived);
    dl->requestInfo();
    QVERIFY(info.wait(3000));
    QSignalSpy finished(dl.get(), &DownloaderBase::downloadFinished);
    dl->download(info.first().first().value<DeviceInfo>().files, dir.path(), true);
    QVERIFY(finished.wait(5000));
    QVERIFY(!dev.deleted);
    for (const Frame &f : dev.received) {
        QVERIFY(f.cmd != 0xA9);
    }
}

void EmayTests::testSetTime()
{
    FakeEmay dev;
    auto dl = makeDownloader(dev);
    QSignalSpy set(dl.get(), &DownloaderBase::timeSet);
    dl->setTime(QDateTime(QDate(2026, 11, 1), QTime(8, 5, 9)));
    QVERIFY(set.wait(3000));
    QCOMPARE(set.first().first().toString(), QString());
    QCOMPARE(dev.setTimeBcd, QByteArray::fromHex("261101080509"));

    FakeEmay mute;
    mute.silent = true;
    auto dl2 = makeDownloader(mute);
    dl2->setMaxRetries(1);
    QSignalSpy set2(dl2.get(), &DownloaderBase::timeSet);
    dl2->setTime(QDateTime::currentDateTime());
    QVERIFY(set2.wait(3000));
    QVERIFY(!set2.first().first().toString().isEmpty());
}

#ifdef OSCAR_HAVE_BLUETOOTH
#include "SleepLib/viatomble/viatomble_scanner.h"

void EmayTests::testDeviceFamily()
{
    QCOMPARE(deviceFamilyFromName(QStringLiteral("EMO-80")), DeviceFamily::Emay);
    QCOMPARE(deviceFamilyFromName(QStringLiteral("EMO-80-1A2B")), DeviceFamily::Emay);
    QCOMPARE(deviceFamilyFromName(QStringLiteral("SpO202")), DeviceFamily::Unsupported);
    QCOMPARE(deviceFamilyFromName(QStringLiteral("SpO213 1234")), DeviceFamily::Unsupported);
    QCOMPARE(deviceFamilyFromName(QStringLiteral("SpO2")), DeviceFamily::NotViatom);
    QCOMPARE(deviceFamilyFromName(QStringLiteral("EMO-100")), DeviceFamily::Unsupported);
    QCOMPARE(deviceFamilyFromName(QStringLiteral("EM90 12")), DeviceFamily::Unsupported);
    QCOMPARE(deviceFamilyFromName(QStringLiteral("SleepO2-2 AB")), DeviceFamily::Unsupported);
    QCOMPARE(deviceFamilyFromName(QStringLiteral("SleepO2_AB12")), DeviceFamily::Unsupported);   // EMAY style
    QCOMPARE(deviceFamilyFromName(QStringLiteral("SleepO2 1234")), DeviceFamily::Classic);       // Viatom style
    QVERIFY(isSupported(DeviceFamily::Emay));
    QVERIFY(!isSupported(DeviceFamily::Unsupported));
    QCOMPARE(deviceFamilyFromName(QStringLiteral("O2Ring 1234")), DeviceFamily::Classic);

    // "SleepO2": the advertised service decides, then the brand chosen in the wizard.
    const QString viatomService = QStringLiteral("14839ac4-7d7e-415c-9a42-167340cf2339");
    const QString emayService = QStringLiteral("0000ff12-0000-1000-8000-00805f9b34fb");
    const QString name = QStringLiteral("SleepO2 1234");
    QCOMPARE(deviceFamily(name, {}, false), DeviceFamily::Classic);
    QCOMPARE(deviceFamily(name, {}, true), DeviceFamily::Unsupported);
    QCOMPARE(deviceFamily(name, { emayService }, false), DeviceFamily::Unsupported);
    QCOMPARE(deviceFamily(name, { viatomService }, true), DeviceFamily::Classic);
    // Other names don't depend on the choice.
    QCOMPARE(deviceFamily(QStringLiteral("O2Ring 1"), {}, true), DeviceFamily::Classic);
    QCOMPARE(deviceFamily(QStringLiteral("EMO-80"), {}, false), DeviceFamily::Emay);

    QCOMPARE(brandName(name, DeviceFamily::Classic), QStringLiteral("Viatom/Wellue"));
    QCOMPARE(brandName(name, DeviceFamily::Unsupported), QStringLiteral("EMAY"));
    QCOMPARE(brandName(QStringLiteral("SpO202"), DeviceFamily::Unsupported), QStringLiteral("Contec"));
}
#endif
