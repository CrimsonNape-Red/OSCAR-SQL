/* Viatom BLE protocol (classic Viatom oximeters)
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QJsonDocument>
#include <QJsonObject>

#include "viatomble_protocol.h"

namespace ViatomBle {

const char *const SERVICE_UUID = "14839ac4-7d7e-415c-9a42-167340cf2339";
const char *const WRITE_UUID   = "8b00ace7-eb0b-49b0-bbe9-9aee0a26e1a3";
const char *const NOTIFY_UUID  = "0734594a-a8e7-4b1a-a6b1-cd5243059a57";

static const quint8 REQUEST_HEAD = 0xAA;
static const quint8 REPLY_HEAD = 0x55;
static const int FRAME_OVERHEAD = 8;   // head, cmd, ~cmd, seq(2), len(2), crc

quint8 crc8(const char *data, int len)
{
    quint8 crc = 0;
    for (int i = 0; i < len; i++) {
        crc ^= static_cast<quint8>(data[i]);
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? quint8((crc << 1) ^ 0x07) : quint8(crc << 1);
        }
    }
    return crc;
}

QByteArray buildRequest(quint8 cmd, quint16 seq, const QByteArray &payload)
{
    const int len = payload.size();
    QByteArray frame;
    frame.reserve(len + FRAME_OVERHEAD);
    frame.append(char(REQUEST_HEAD));
    frame.append(char(cmd));
    frame.append(char(~cmd));
    frame.append(char(seq & 0xFF));
    frame.append(char(seq >> 8));
    frame.append(char(len & 0xFF));
    frame.append(char(len >> 8));
    frame.append(payload);
    frame.append(char(crc8(frame.constData(), frame.size())));
    return frame;
}

QList<Reply> FrameSplitter::feed(const QByteArray &chunk)
{
    m_buffer.append(chunk);
    QList<Reply> replies;

    while (!m_buffer.isEmpty()) {
        const auto *b = reinterpret_cast<const quint8 *>(m_buffer.constData());
        if (b[0] != REPLY_HEAD) {
            int next = m_buffer.indexOf(char(REPLY_HEAD), 1);
            int drop = (next < 0) ? m_buffer.size() : next;
            m_discarded += drop;
            m_buffer.remove(0, drop);
            continue;
        }
        if (m_buffer.size() < 3) {
            break;
        }
        if (b[2] != quint8(~b[1])) {
            m_discarded++;
            m_buffer.remove(0, 1);
            continue;
        }
        if (m_buffer.size() < 7) {
            break;
        }
        const int len = b[5] | (b[6] << 8);
        if (len > MAX_PAYLOAD) {
            m_discarded++;
            m_buffer.remove(0, 1);
            continue;
        }
        const int total = len + FRAME_OVERHEAD;
        if (m_buffer.size() < total) {
            break;
        }
        if (crc8(m_buffer.constData(), total - 1) != b[total - 1]) {
            m_discarded++;
            m_buffer.remove(0, 1);
            continue;
        }

        Reply reply;
        reply.status = b[1];
        reply.seq = quint16(b[3] | (b[4] << 8));
        reply.payload = m_buffer.mid(7, len);
        replies.append(reply);
        m_buffer.remove(0, total);
    }
    return replies;
}

DeviceInfo DeviceInfo::fromPayload(const QByteArray &payload)
{
    QByteArray json = payload;
    while (!json.isEmpty() && json.back() == '\0') {
        json.chop(1);
    }

    DeviceInfo info;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return info;
    }

    // Values are strings on the devices oxytool has seen; accept numbers as well.
    const QJsonObject obj = doc.object();
    auto text = [&obj](const char *key) {
        const QJsonValue v = obj.value(QLatin1String(key));
        return v.isDouble() ? QString::number(v.toDouble()) : v.toString();
    };
    info.model = text("Model");
    info.serialNumber = text("SN").trimmed();
    info.softwareVersion = text("SoftwareVer");
    info.hardwareVersion = text("HardwareVer");
    info.battery = text("CurBAT");
    info.deviceTime = text("CurTIME");
    for (const QString &name : text("FileList").split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString trimmed = name.trimmed();
        if (!trimmed.isEmpty()) {
            info.files.append(trimmed);
        }
    }
    info.valid = true;
    return info;
}

qint64 fileSizeFromReadStart(const Reply &reply)
{
    if (reply.payload.size() < 4) {
        return -1;
    }
    const auto *p = reinterpret_cast<const quint8 *>(reply.payload.constData());
    return qint64(p[0]) | (qint64(p[1]) << 8) | (qint64(p[2]) << 16) | (qint64(p[3]) << 24);
}

} // namespace ViatomBle
