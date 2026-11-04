/* EMAY oximeter protocol (EMO-80)
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QCoreApplication>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

#include "emay.h"
#include "viatomble_link.h"

namespace ViatomBle {
namespace Emay {

const char *const SERVICE_UUID = "0000ff12-0000-1000-8000-00805f9b34fb";
const char *const WRITE_UUID = "0000ff01-0000-1000-8000-00805f9b34fb";
const char *const NOTIFY_UUID = "0000ff02-0000-1000-8000-00805f9b34fb";

static constexpr quint8 FRAME_START = 0xE2;
static constexpr quint8 FRAME_END = 0xE3;

QByteArray buildFrame(quint8 cmd, quint8 type, const QByteArray &payload)
{
    QByteArray f;
    f.reserve(payload.size() + 7);
    f.append(char(FRAME_START));
    f.append(char(cmd));
    f.append(char(type));
    f.append(char((payload.size() >> 8) & 0xFF));
    f.append(char(payload.size() & 0xFF));
    f.append(payload);
    quint8 x = 0;
    for (int i = 1; i < f.size(); i++) {
        x ^= quint8(f[i]);
    }
    f.append(char(FRAME_END));
    f.append(char(x));
    return f;
}

QList<Frame> FrameSplitter::feed(const QByteArray &chunk)
{
    m_buffer.append(chunk);
    QList<Frame> frames;
    for (;;) {
        const int start = m_buffer.indexOf(char(FRAME_START));
        if (start < 0) {
            m_buffer.clear();
            break;
        }
        if (start > 0) {
            m_buffer.remove(0, start);
        }
        if (m_buffer.size() < 5) {
            break;
        }
        const int len = (quint8(m_buffer[3]) << 8) | quint8(m_buffer[4]);
        const int total = len + 7;
        if (m_buffer.size() < total) {
            break;
        }
        quint8 x = 0;
        for (int i = 1; i < total - 2; i++) {
            x ^= quint8(m_buffer[i]);
        }
        if (quint8(m_buffer[total - 2]) != FRAME_END || quint8(m_buffer[total - 1]) != x) {
            // Not a frame after all (or a damaged one): look for the next start byte.
            m_buffer.remove(0, 1);
            continue;
        }
        Frame f;
        f.cmd = quint8(m_buffer[1]);
        f.type = quint8(m_buffer[2]);
        f.payload = m_buffer.mid(5, len);
        frames.append(f);
        m_buffer.remove(0, total);
    }
    return frames;
}

static int fromBcd(quint8 b)
{
    return (b >> 4) * 10 + (b & 0x0F);
}

static quint8 toBcd(int v)
{
    return quint8(((v / 10) % 10) << 4 | (v % 10));
}

QByteArray bcdTime(const QDateTime &t)
{
    QByteArray b;
    b.append(char(toBcd(t.date().year() % 100)));
    b.append(char(toBcd(t.date().month())));
    b.append(char(toBcd(t.date().day())));
    b.append(char(toBcd(t.time().hour())));
    b.append(char(toBcd(t.time().minute())));
    b.append(char(toBcd(t.time().second())));
    return b;
}

QDateTime fromBcdTime(const QByteArray &b)
{
    if (b.size() < 6) {
        return QDateTime();
    }
    const QDate date(2000 + fromBcd(quint8(b[0])), fromBcd(quint8(b[1])), fromBcd(quint8(b[2])));
    const QTime time(fromBcd(quint8(b[3])), fromBcd(quint8(b[4])), fromBcd(quint8(b[5])));
    return (date.isValid() && time.isValid()) ? QDateTime(date, time) : QDateTime();
}

static quint16 be16(const QByteArray &b, int i)
{
    return quint16((quint8(b[i]) << 8) | quint8(b[i + 1]));
}

static quint32 be32(const QByteArray &b, int i)
{
    return (quint32(quint8(b[i])) << 24) | (quint32(quint8(b[i + 1])) << 16)
         | (quint32(quint8(b[i + 2])) << 8) | quint32(quint8(b[i + 3]));
}

bool parseRecording(const QByteArray &p, Recording &rec, bool &last)
{
    // status, endFlag, serial(2), user, time(6), duration(4), checksum
    if (p.size() < 16 || quint8(p[0]) != 0) {
        return false;
    }
    last = (p[1] == 1);
    rec.serial = be16(p, 2);
    rec.start = fromBcdTime(p.mid(5, 6));
    rec.duration = be32(p, 11);
    return true;
}

//! A length byte, then that many bytes; advances pos. Empty if out of range.
static QByteArray lengthPrefixed(const QByteArray &p, int &pos)
{
    if (pos >= p.size()) {
        return QByteArray();
    }
    const int n = quint8(p[pos++]);
    if (n == 0 || pos + n > p.size()) {
        return QByteArray();
    }
    const QByteArray s = p.mid(pos, n);
    pos += n;
    return s;
}

bool parseDeviceInfo(const QByteArray &p, DeviceInfo &info)
{
    if (p.size() < 2 || quint8(p[0]) != 0) {
        return false;
    }
    int pos = 1;
    info.model = QString::fromLatin1(lengthPrefixed(p, pos)).trimmed();
    info.serialNumber = QString::fromLatin1(lengthPrefixed(p, pos)).trimmed();
    lengthPrefixed(p, pos);   // Bluetooth address
    info.softwareVersion = QString::fromLatin1(lengthPrefixed(p, pos)).trimmed();
    const QDateTime clock = fromBcdTime(lengthPrefixed(p, pos));
    if (clock.isValid()) {
        info.deviceTime = clock.toString(QStringLiteral("yyyy-MM-dd,HH:mm:ss"));
    }
    info.valid = true;
    return true;
}

QString recordingName(const Recording &rec)
{
    const QString stamp = rec.start.isValid() ? rec.start.toString(QStringLiteral("yyyyMMddHHmmss"))
                                              : QStringLiteral("unknown");
    return QStringLiteral("EMAY_%1_%2.spo2").arg(stamp).arg(rec.serial);
}

QByteArray spo2File(const QDateTime &start, const QByteArray &data)
{
    const int samples = data.size() / 2;
    if (samples == 0 || !start.isValid()) {
        return QByteArray();
    }
    QByteArray out;
    QDataStream s(&out, QIODevice::WriteOnly);
    s.setByteOrder(QDataStream::LittleEndian);
    // Header offset 2; the reader then skips 200 bytes, the first of which is 1.
    s << quint16(2);
    s << quint8(1);
    for (int i = 1; i < 200; i++) {
        s << quint8(0);
    }
    s << quint32(start.date().year()) << quint32(start.date().month()) << quint32(start.date().day())
      << quint32(start.time().hour()) << quint32(start.time().minute()) << quint32(start.time().second());
    s << quint32(samples);
    for (int i = 0; i < samples; i++) {
        quint8 spo2 = quint8(data[2 * i]);
        quint8 pulse = quint8(data[2 * i + 1]);
        // As EMAY's app: SpO2 outside 20-100 and pulse 255 are no reading.
        if (spo2 < 20 || spo2 > 100) {
            spo2 = 0;
        }
        if (pulse == 255) {
            pulse = 0;
        }
        s << spo2 << pulse;
    }
    return out;
}

} // namespace Emay

// ----------------------------------------------------------------------------------------

using namespace Emay;

EmayDownloader::EmayDownloader(Link *link, QObject *parent)
    : DownloaderBase(parent), m_link(link)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &EmayDownloader::onTimeout);
    connect(m_link, &Link::dataReceived, this, &EmayDownloader::onDataReceived);
    connect(m_link, &Link::linkLost, this, &EmayDownloader::onLinkLost);
}

EmayDownloader::~EmayDownloader() = default;

void EmayDownloader::send(State state, quint8 cmd, quint8 type, const QByteArray &payload)
{
    m_state = state;
    m_expectCmd = cmd;
    m_lastFrame = buildFrame(cmd, type, payload);
    m_link->send(m_lastFrame);
    m_timer.start(m_replyTimeoutMs);
}

void EmayDownloader::acknowledge(quint8 cmd)
{
    const int generation = m_generation;
    const State state = m_state;
    m_timer.start(m_replyTimeoutMs + m_ackDelayMs);
    QTimer::singleShot(m_ackDelayMs, this, [this, generation, state, cmd]() {
        if (generation != m_generation || m_state != state) {
            return;
        }
        m_lastFrame = buildFrame(cmd, TYPE_ACK, QByteArray(1, '\0'));
        m_link->send(m_lastFrame);
        m_timer.start(m_replyTimeoutMs);
    });
}

void EmayDownloader::requestInfo()
{
    m_generation++;
    m_info = DeviceInfo();
    m_recordings.clear();
    m_splitter.clear();
    m_attempt = 0;
    send(WaitInfo, CMD_DEVICE_INFO, TYPE_REQUEST, QByteArray());
}

void EmayDownloader::setTime(const QDateTime &localTime)
{
    m_generation++;
    m_attempt = 0;
    send(WaitSetTime, CMD_SET_TIME, TYPE_REQUEST, bcdTime(localTime));
}

void EmayDownloader::requestList()
{
    m_info.files.clear();
    m_recordings.clear();
    send(WaitList, CMD_LIST, TYPE_REQUEST, QByteArray(3, '\0'));
}

void EmayDownloader::finishInfo()
{
    m_state = Idle;
    m_timer.stop();
    m_info.valid = true;
    qDebug().noquote() << "EmayDownloader: model" << m_info.model << "serial" << m_info.serialNumber
                       << "firmware" << m_info.softwareVersion << "-" << m_info.files.size() << "recording(s)";
    emit infoReceived(m_info);
}

void EmayDownloader::onDataReceived(const QByteArray &chunk)
{
    for (const Frame &f : m_splitter.feed(chunk)) {
        handle(f);
    }
}

void EmayDownloader::handle(const Frame &f)
{
    if (m_state == Idle || f.cmd != m_expectCmd) {
        qDebug().noquote() << "EmayDownloader: ignoring frame" << Qt::hex << int(f.cmd) << Qt::dec << "in state" << m_state;
        return;
    }
    m_timer.stop();
    m_attempt = 0;

    switch (m_state) {
    case WaitInfo:
        if (!parseDeviceInfo(f.payload, m_info)) {
            m_state = Idle;
            emit failed(tr("The oximeter didn't send its details (status %1).").arg(f.status()));
            return;
        }
        send(WaitBattery, CMD_BATTERY, TYPE_REQUEST, QByteArray());
        return;
    case WaitBattery:
        if (f.status() == 0 && f.payload.size() >= 3) {
            m_info.battery = QStringLiteral("%1%").arg(quint8(f.payload[2]));
        }
        send(WaitStorage, CMD_STORAGE, TYPE_REQUEST, QByteArray());
        return;
    case WaitStorage: {
        int stored = -1;
        if (f.status() == 0 && f.payload.size() >= 13) {
            stored = be16(f.payload, 11);
        }
        qDebug() << "EmayDownloader:" << stored << "recording(s) stored";
        if (stored == 0) {
            finishInfo();
        } else {
            m_listRetries = 0;
            requestList();
        }
        return;
    }
    case WaitList: {
        Recording rec;
        bool last = true;
        if (!parseRecording(f.payload, rec, last)) {
            if (m_info.files.isEmpty()) {
                finishInfo();   // e.g. nothing stored
            } else {
                m_state = Idle;
                emit failed(tr("The oximeter's list of recordings could not be read."));
            }
            return;
        }
        const QString name = recordingName(rec);
        if (!m_recordings.contains(name)) {
            m_recordings.insert(name, rec);
            m_info.files.append(name);
        }
        if (last) {
            finishInfo();
        } else {
            acknowledge(CMD_LIST);
        }
        return;
    }
    case WaitSetTime:
        m_state = Idle;
        emit timeSet(f.status() == 0 ? QString() : tr("the oximeter refused (status %1)").arg(f.status()));
        return;
    case WaitData: {
        const QByteArray &p = f.payload;
        if (f.status() != 0 || p.size() < 4) {
            failFile(tr("the oximeter refused (status %1)").arg(f.status()));
            return;
        }
        const bool last = (p[1] == 1);
        const quint16 package = be16(p, 2);
        if (package != m_package) {
            qDebug() << "EmayDownloader: got package" << package << "expected" << m_package;
            if (package < m_package) {
                m_timer.start(m_replyTimeoutMs);   // a repeat of one we have; wait for ours
            } else {
                requestPackage();                   // one went missing: ask from there again
            }
            return;
        }
        m_data.append(p.mid(4));
        m_package++;
        emit fileProgress(m_current, m_data.size(), qMax<qint64>(m_currentRec.duration * 2, m_data.size()));
        if (last || m_cancelRequested) {
            finishFile();
        } else {
            acknowledge(CMD_DATA);
        }
        return;
    }
    case Idle:
        return;
    }
}

void EmayDownloader::onTimeout()
{
    if (m_state == Idle) {
        return;
    }
    if (m_attempt < m_maxRetries) {
        m_attempt++;
        qDebug() << "EmayDownloader: no reply in state" << m_state << "- retry" << m_attempt;
        m_splitter.clear();
        if (m_state == WaitData) {
            requestPackage();
        } else if (m_state == WaitList) {
            requestList();   // start the list again
        } else {
            m_link->send(m_lastFrame);
            m_timer.start(m_replyTimeoutMs);
        }
        return;
    }
    const State state = m_state;
    m_state = Idle;
    switch (state) {
    case WaitSetTime:
        emit timeSet(tr("the oximeter didn't answer"));
        return;
    case WaitData:
        m_state = WaitData;
        failFile(tr("the oximeter stopped sending"));
        return;
    case WaitBattery:
        // Not essential.
        m_attempt = 0;
        send(WaitStorage, CMD_STORAGE, TYPE_REQUEST, QByteArray());
        return;
    default:
        emit failed(tr("The oximeter didn't answer. Check that it is switched on and close to the computer, "
                       "and that EMAY's phone app isn't connected to it."));
        return;
    }
}

void EmayDownloader::onLinkLost(const QString &reason)
{
    if (m_state == Idle) {
        return;
    }
    m_timer.stop();
    m_generation++;
    if (m_state == WaitData && !m_current.isEmpty()) {
        emit fileFailed(m_current, reason);
        m_current.clear();
    }
    m_state = Idle;
    emit failed(reason);
}

void EmayDownloader::download(const QStringList &files, const QString &folder, bool overwriteExisting)
{
    m_generation++;
    m_queue = files;
    m_folder = folder;
    m_overwrite = overwriteExisting;
    m_cancelRequested = false;
    QDir().mkpath(folder);
    startNextFile();
}

void EmayDownloader::startNextFile()
{
    m_current.clear();
    if (m_cancelRequested || m_queue.isEmpty()) {
        m_state = Idle;
        const bool cancelled = m_cancelRequested;
        m_cancelRequested = false;
        emit downloadFinished(cancelled);
        return;
    }
    const QString name = m_queue.takeFirst();
    const QString path = QDir(m_folder).filePath(name);
    if (!m_recordings.contains(name)) {
        emit fileFailed(name, tr("not on the oximeter"));
        QTimer::singleShot(0, this, &EmayDownloader::startNextFile);
        return;
    }
    if (!m_overwrite && QFileInfo::exists(path)) {
        emit fileSkipped(name, path);
        QTimer::singleShot(0, this, &EmayDownloader::startNextFile);
        return;
    }
    m_current = name;
    m_currentRec = m_recordings.value(name);
    m_data.clear();
    m_package = 1;
    m_attempt = 0;
    emit fileStarted(name, qint64(m_currentRec.duration) * 2);
    requestPackage();
}

void EmayDownloader::requestPackage()
{
    // 03, recording number, first package wanted, package size: as EMAY's app.
    QByteArray p;
    p.append(char(3));
    p.append(char(m_currentRec.serial >> 8)).append(char(m_currentRec.serial & 0xFF));
    p.append(char(m_package >> 8)).append(char(m_package & 0xFF));
    p.append(char(0)).append(char(200));
    m_splitter.clear();
    send(WaitData, CMD_DATA, TYPE_REQUEST, p);
}

void EmayDownloader::finishFile()
{
    m_timer.stop();
    if (m_cancelRequested) {
        emit fileFailed(m_current, tr("cancelled"));
        startNextFile();
        return;
    }
    const QByteArray bytes = spo2File(m_currentRec.start, m_data);
    if (bytes.isEmpty()) {
        failFile(tr("the recording is empty"));
        return;
    }
    const QString path = QDir(m_folder).filePath(m_current);
    const QString part = path + QStringLiteral(".part");
    QFile f(part);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) {
        f.remove();
        failFile(tr("could not write %1").arg(QDir::toNativeSeparators(part)));
        return;
    }
    f.close();
    QFile::remove(path);
    if (!QFile::rename(part, path)) {
        QFile::remove(part);
        failFile(tr("could not write %1").arg(QDir::toNativeSeparators(path)));
        return;
    }
    if (qint64(m_data.size()) != qint64(m_currentRec.duration) * 2) {
        qDebug() << "EmayDownloader:" << m_current << "has" << m_data.size() << "bytes; the list said"
                 << m_currentRec.duration * 2;
    }
    const QString name = m_current;
    m_state = Idle;   // between files, e.g. while the panel imports this one
    emit fileSaved(name, path);
    startNextFile();
}

void EmayDownloader::failFile(const QString &reason)
{
    m_timer.stop();
    const QString name = m_current;
    m_state = Idle;
    emit fileFailed(name, reason);
    startNextFile();
}

void EmayDownloader::cancel()
{
    if (m_state == Idle) {
        return;
    }
    if (m_state == WaitData) {
        // Stops at the next package; there is no command to stop a transfer.
        m_cancelRequested = true;
        m_queue.clear();
        return;
    }
    m_timer.stop();
    m_generation++;
    m_state = Idle;
}

} // namespace ViatomBle
