/* Viatom BLE protocol (O2Ring S family)
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QDate>
#include <QTime>

#include "viatomble_o2rings.h"
#include "SleepLib/crypto.h"

namespace ViatomBle {
namespace O2RingS {

const char *const SERVICE_UUID = "e8fb0001-a14b-98f9-831b-4e2941d01248";
const char *const WRITE_UUID   = "e8fb0002-a14b-98f9-831b-4e2941d01248";
const char *const NOTIFY_UUID  = "e8fb0003-a14b-98f9-831b-4e2941d01248";

static const quint8 HEAD = 0xA5;
static const int FRAME_OVERHEAD = 8;   // head, cmd, ~cmd, type, seq, len(2), crc

// The app key hard-coded in the ViHealth app (Lepu SDK, EncryptUtil.SECRET_KEY). It only
// lets OSCAR talk to the user's own device; the session key comes from the device.
static const unsigned char APP_KEY[16] = {
    0xc2, 0xa7, 0xcf, 0x50, 0xda, 0xfe, 0xd8, 0x85, 0xa8, 0xf8, 0xf7, 0xea, 0xc4, 0x43, 0x35, 0xf3,
};

QByteArray buildRequest(quint8 cmd, quint8 seq, const QByteArray &payload)
{
    const int len = payload.size();
    QByteArray frame;
    frame.reserve(len + FRAME_OVERHEAD);
    frame.append(char(HEAD));
    frame.append(char(cmd));
    frame.append(char(~cmd));
    frame.append(char(TYPE_REQUEST));
    frame.append(char(seq));
    frame.append(char(len & 0xFF));
    frame.append(char(len >> 8));
    frame.append(payload);
    frame.append(char(crc8(frame.constData(), frame.size())));
    return frame;
}

quint8 statusFromType(quint8 type)
{
    if (type == TYPE_REPLY) {
        return 0;
    }
    return type == 0 ? 0xFF : type;   // never let an odd type pass as success
}

QList<Reply> FrameSplitter::feed(const QByteArray &chunk)
{
    m_buffer.append(chunk);
    QList<Reply> replies;

    while (!m_buffer.isEmpty()) {
        const auto *b = reinterpret_cast<const quint8 *>(m_buffer.constData());
        if (b[0] != HEAD) {
            const int next = m_buffer.indexOf(char(HEAD), 1);
            m_buffer.remove(0, next < 0 ? m_buffer.size() : next);
            continue;
        }
        if (m_buffer.size() < 3) {
            break;
        }
        if (b[2] != quint8(~b[1])) {
            m_buffer.remove(0, 1);
            continue;
        }
        if (m_buffer.size() < 7) {
            break;
        }
        const int len = b[5] | (b[6] << 8);
        if (len > MAX_PAYLOAD) {
            m_buffer.remove(0, 1);
            continue;
        }
        const int total = len + FRAME_OVERHEAD;
        if (m_buffer.size() < total) {
            break;
        }
        if (crc8(m_buffer.constData(), total - 1) != b[total - 1]) {
            m_buffer.remove(0, 1);
            continue;
        }

        Reply reply;
        reply.cmd = b[1];
        reply.status = statusFromType(b[3]);
        reply.seq = b[4];
        reply.payload = m_buffer.mid(7, len);
        replies.append(reply);
        m_buffer.remove(0, total);
    }
    return replies;
}

QByteArray handshakeToken(qint64 unixSeconds, const QByteArray &appId)
{
    QByteArray token(16, '\0');
    for (int i = 0; i < 8; i++) {
        token[i] = char(APP_KEY[i * 2]);
    }
    for (int i = 0; i < 4 && i < appId.size(); i++) {
        token[8 + i] = appId.at(i);
    }
    // The SDK shifts by i bits here, not i bytes; the device presumably doesn't care.
    for (int i = 0; i < 4; i++) {
        token[12 + i] = char(quint8(unixSeconds >> i));
    }
    return token;
}

QByteArray scramble(const QByteArray &data)
{
    QByteArray out(data.size(), '\0');
    for (int i = 0; i < data.size(); i++) {
        out[i] = char(quint8(data.at(i)) ^ APP_KEY[i % int(sizeof(APP_KEY))]);
    }
    return out;
}

QByteArray sessionKeyFromHandshake(const QByteArray &payload)
{
    // After unscrambling: type, key length, two unused bytes, then the key.
    const QByteArray plain = scramble(payload);
    if (plain.size() < 4) {
        return QByteArray();
    }
    const int len = quint8(plain.at(1));
    if (len != 16 || plain.size() < 4 + len) {
        return QByteArray();   // AES-128 needs exactly 16 bytes
    }
    return plain.mid(4, len);
}

bool encryptPayload(const QByteArray &key, const QByteArray &plain, QByteArray &out)
{
    if (plain.isEmpty()) {
        out.clear();
        return true;
    }
    return encrypt_aes128_ecb_pkcs7(key, plain, out) == OK;
}

bool decryptPayload(const QByteArray &key, const QByteArray &cipher, QByteArray &out)
{
    if (cipher.isEmpty()) {
        out.clear();
        return true;
    }
    return decrypt_aes128_ecb_pkcs7(key, cipher, out) == OK;
}

static QByteArray le32(quint32 v)
{
    QByteArray b;
    for (int i = 0; i < 4; i++) {
        b.append(char((v >> (8 * i)) & 0xFF));
    }
    return b;
}

QByteArray readStartPayload(const QString &name, quint32 offset)
{
    QByteArray payload = name.toLatin1().left(16);
    payload.append(QByteArray(16 - payload.size(), '\0'));
    payload.append(le32(offset));
    return payload;
}

QByteArray offsetPayload(quint32 offset)
{
    return le32(offset);
}

QByteArray utcTimePayload(const QDateTime &localTime, int utcOffsetSeconds)
{
    const QDate d = localTime.date();
    const QTime t = localTime.time();
    QByteArray p;
    p.append(char(d.year() & 0xFF));
    p.append(char(d.year() >> 8));
    p.append(char(d.month()));
    p.append(char(d.day()));
    p.append(char(t.hour()));
    p.append(char(t.minute()));
    p.append(char(t.second()));
    p.append(char(qint8(utcOffsetSeconds / 360)));   // tenths of an hour
    return p;
}

QStringList parseFileList(const QByteArray &payload)
{
    QStringList names;
    if (payload.isEmpty()) {
        return names;
    }
    const int count = quint8(payload.at(0));
    for (int i = 0; i < count; i++) {
        const int start = 1 + i * 16;
        if (start + 16 > payload.size()) {
            break;
        }
        QByteArray raw = payload.mid(start, 16);
        const int nul = raw.indexOf('\0');
        if (nul >= 0) {
            raw.truncate(nul);
        }
        const QString name = QString::fromLatin1(raw).trimmed();
        if (!name.isEmpty()) {
            names.append(name);
        }
    }
    return names;
}

DeviceInfo parseInfo(const QByteArray &payload)
{
    DeviceInfo info;
    if (payload.size() < 31) {
        return info;
    }
    const auto *b = reinterpret_cast<const quint8 *>(payload.constData());
    const QDateTime clock(QDate(b[24] | (b[25] << 8), b[26], b[27]), QTime(b[28], b[29], b[30]));
    if (clock.isValid()) {
        info.deviceTime = clock.toString(QStringLiteral("yyyy-MM-dd,HH:mm:ss"));
    }
    if (payload.size() > 38) {
        const int len = qMin<int>(b[37], 18);
        QByteArray sn = payload.mid(38, len);
        const int nul = sn.indexOf('\0');
        if (nul >= 0) {
            sn.truncate(nul);
        }
        info.serialNumber = QString::fromLatin1(sn).trimmed();
    }
    info.valid = true;
    return info;
}

} // namespace O2RingS
} // namespace ViatomBle
