/* Viatom BLE protocol dialects
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QDebug>

#include "viatomble_dialect.h"

namespace ViatomBle {

// ---------------------------------------------------------------------------------------
// Classic

Dialect::Request ClassicDialect::infoRequest(int)
{
    return Request{buildRequest(CMD_INFO, 0), -1, 0};
}

QString ClassicDialect::handleInfoReply(int, const Reply &reply, DeviceInfo &info)
{
    if (!reply.ok()) {
        return tr("The device refused the request for its information.");
    }
    info = DeviceInfo::fromPayload(reply.payload);
    if (!info.valid) {
        return tr("The device's information could not be read.");
    }
    return QString();
}

QString ClassicDialect::infoTimeoutMessage(int) const
{
    return tr("The device did not answer. It may need an encrypted connection, "
              "which OSCAR does not support yet.");
}

Dialect::Request ClassicDialect::setTimeRequest(const QDateTime &localTime)
{
    return Request{buildRequest(CMD_PARA_SYNC, 0, setTimePayload(localTime)), -1, 0};
}

Dialect::Request ClassicDialect::startRequest(const QString &name)
{
    QByteArray payload = name.toLatin1();
    payload.append('\0');
    return Request{buildRequest(CMD_READ_START, 0, payload), -1, 0};
}

Dialect::Request ClassicDialect::dataRequest(quint16 chunk, qint64)
{
    // The sequence number is the chunk number; some devices echo it in the reply.
    return Request{buildRequest(CMD_READ_CONTENT, chunk), -1, chunk};
}

Dialect::Request ClassicDialect::endRequest()
{
    return Request{buildRequest(CMD_READ_END, 0), -1, 0};
}

// ---------------------------------------------------------------------------------------
// O2Ring S

O2RingSDialect::O2RingSDialect(std::function<QDateTime()> now)
    : m_now(std::move(now))
{
}

Dialect::Request O2RingSDialect::make(quint8 cmd, const QByteArray &payload)
{
    QByteArray body = payload;
    if (!m_sessionKey.isEmpty() && cmd != O2RingS::CMD_ENCRYPT
            && !O2RingS::encryptPayload(m_sessionKey, payload, body)) {
        qWarning() << "ViatomBle::O2RingSDialect: could not encrypt the payload of" << Qt::hex << cmd;
    }
    // The SDK counts 0..254 and starts again at 0.
    const quint8 seq = m_seq;
    m_seq = quint8((m_seq + 1) % 255);
    return Request{O2RingS::buildRequest(cmd, seq, body), cmd, seq};
}

QList<Reply> O2RingSDialect::feed(const QByteArray &chunk)
{
    QList<Reply> replies = m_splitter.feed(chunk);
    for (Reply &reply : replies) {
        if (m_sessionKey.isEmpty() || reply.cmd == O2RingS::CMD_ENCRYPT || reply.payload.isEmpty()) {
            continue;
        }
        QByteArray plain;
        if (O2RingS::decryptPayload(m_sessionKey, reply.payload, plain)) {
            reply.payload = plain;
        } else {
            qWarning().noquote() << "ViatomBle::O2RingSDialect: could not decrypt the reply to"
                                 << QString::number(reply.cmd, 16) << reply.payload.toHex(' ');
            reply.status = 0xF3;   // the device's own code for "decryption failed"
            reply.payload.clear();
        }
    }
    return replies;
}

int O2RingSDialect::nextInfoStep(int step) const
{
    switch (step) {
    case PlainInfo:     return PlainFileList;
    case Handshake:     return EncryptedInfo;
    case EncryptedInfo: return EncryptedFileList;
    default:            return -1;
    }
}

int O2RingSDialect::infoFallbackStep(int step) const
{
    // A device that wants an encrypted session ignores or refuses plain requests.
    return step == PlainInfo ? int(Handshake) : -1;
}

Dialect::Request O2RingSDialect::infoRequest(int step)
{
    switch (step) {
    case PlainInfo:
        m_sessionKey.clear();
        return make(O2RingS::CMD_GET_INFO, QByteArray());
    case Handshake: {
        m_sessionKey.clear();
        const qint64 now = m_now().toSecsSinceEpoch();
        return make(O2RingS::CMD_ENCRYPT, O2RingS::scramble(O2RingS::handshakeToken(now)));
    }
    case EncryptedInfo:
        return make(O2RingS::CMD_GET_INFO, QByteArray());
    default:
        return make(O2RingS::CMD_GET_FILE_LIST, QByteArray());
    }
}

QString O2RingSDialect::handleInfoReply(int step, const Reply &reply, DeviceInfo &info)
{
    if (!reply.ok()) {
        const QString code = QStringLiteral("0x%1").arg(reply.status, 2, 16, QLatin1Char('0'));
        switch (step) {
        case Handshake:
            return tr("The device refused the encrypted connection (code %1).").arg(code);
        case PlainInfo:
        case EncryptedInfo:
            return tr("The device refused the request for its information (code %1).").arg(code);
        default:
            return tr("The device refused the request for its list of recordings (code %1).").arg(code);
        }
    }
    switch (step) {
    case Handshake:
        m_sessionKey = O2RingS::sessionKeyFromHandshake(reply.payload);
        if (m_sessionKey.isEmpty()) {
            qWarning().noquote() << "ViatomBle::O2RingSDialect: unexpected handshake reply" << reply.payload.toHex(' ');
            return tr("The device's answer to the encrypted connection could not be understood.");
        }
        return QString();
    case PlainInfo:
    case EncryptedInfo:
        info = O2RingS::parseInfo(reply.payload);
        if (!info.valid) {
            qWarning().noquote() << "ViatomBle::O2RingSDialect: unexpected info reply" << reply.payload.toHex(' ');
            return tr("The device's information could not be read.");
        }
        return QString();
    default:
        info.files = O2RingS::parseFileList(reply.payload);
        return QString();
    }
}

QString O2RingSDialect::infoTimeoutMessage(int step) const
{
    switch (step) {
    case Handshake:
        return tr("The device did not answer, neither to plain requests nor to the request "
                  "for an encrypted connection.");
    case PlainInfo:
    case EncryptedInfo:
        return tr("The device did not answer the request for its information.");
    default:
        return tr("The device did not answer the request for its list of recordings.");
    }
}

Dialect::Request O2RingSDialect::setTimeRequest(const QDateTime &localTime)
{
    return make(O2RingS::CMD_SET_UTC_TIME, O2RingS::utcTimePayload(localTime, localTime.offsetFromUtc()));
}

Dialect::Request O2RingSDialect::startRequest(const QString &name)
{
    return make(O2RingS::CMD_READ_FILE_START, O2RingS::readStartPayload(name));
}

qint64 O2RingSDialect::fileSize(const Reply &reply) const
{
    return fileSizeFromReadStart(reply);   // the same 32-bit size at the start of the payload
}

Dialect::Request O2RingSDialect::dataRequest(quint16, qint64 received)
{
    return make(O2RingS::CMD_READ_FILE_DATA, O2RingS::offsetPayload(quint32(received)));
}

Dialect::Request O2RingSDialect::endRequest()
{
    return make(O2RingS::CMD_READ_FILE_END, QByteArray());
}

} // namespace ViatomBle
