/* Viatom BLE protocol dialects
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef VIATOMBLE_DIALECT_H
#define VIATOMBLE_DIALECT_H

#include <functional>

#include <QCoreApplication>
#include <QDateTime>
#include <QList>
#include <QString>

#include "viatomble_o2rings.h"
#include "viatomble_protocol.h"

namespace ViatomBle {

/*! \brief What differs between the device families, for the Downloader.
 *
 *  The Downloader owns the sequence of requests, the retries, the partial files and so
 *  on; a dialect builds the frames, decodes (and if need be decrypts) replies, and
 *  interprets the replies that carry information.
 */
class Dialect
{
    Q_DECLARE_TR_FUNCTIONS(ViatomBle::Dialect)

  public:
    struct Request
    {
        QByteArray frame;
        int cmd = -1;       //!< command a reply must carry, or -1 if replies don't say
        quint16 seq = 0;    //!< the request's packet number; 0 if not meaningful
    };

    virtual ~Dialect() = default;

    //! Decoded replies, decrypted where the dialect encrypts.
    virtual QList<Reply> feed(const QByteArray &chunk) = 0;
    //! Drops partly received data (not the session).
    virtual void clearBuffer() = 0;

    //! Reading the device info takes one or more request/reply steps, starting at 0.
    //! The step after this one succeeded, or -1 if the info is complete.
    virtual int nextInfoStep(int step) const = 0;
    //! The step to try when this one is refused or not answered, or -1 to give up.
    virtual int infoFallbackStep(int step) const { Q_UNUSED(step); return -1; }
    virtual Request infoRequest(int step) = 0;
    //! Interprets one info step's reply into info; returns an error message, or empty.
    virtual QString handleInfoReply(int step, const Reply &reply, DeviceInfo &info) = 0;
    //! Explains a timeout at this info step.
    virtual QString infoTimeoutMessage(int step) const = 0;

    virtual Request setTimeRequest(const QDateTime &localTime) = 0;
    //! How long the device needs after setting its clock before it takes another request.
    virtual int pauseAfterSetTimeMs() const { return 0; }
    virtual Request startRequest(const QString &name) = 0;
    //! File size from a successful start reply, or -1.
    virtual qint64 fileSize(const Reply &reply) const = 0;
    //! The request for the block after received bytes, which is block number chunk.
    virtual Request dataRequest(quint16 chunk, qint64 received) = 0;
    virtual Request endRequest() = 0;
};

//! SleepU, Checkme O2, O2Ring, ...: plain text, replies don't name their command.
class ClassicDialect : public Dialect
{
  public:
    QList<Reply> feed(const QByteArray &chunk) override { return m_splitter.feed(chunk); }
    void clearBuffer() override { m_splitter.clear(); }

    int nextInfoStep(int) const override { return -1; }
    Request infoRequest(int step) override;
    QString handleInfoReply(int step, const Reply &reply, DeviceInfo &info) override;
    QString infoTimeoutMessage(int step) const override;

    Request setTimeRequest(const QDateTime &localTime) override;
    Request startRequest(const QString &name) override;
    qint64 fileSize(const Reply &reply) const override { return fileSizeFromReadStart(reply); }
    Request dataRequest(quint16 chunk, qint64 received) override;
    Request endRequest() override;

  private:
    FrameSplitter m_splitter;
};

/*! SleepU and O2Ring on their USB cable: classic, but Wellue's O2 Insight Pro sends
 *  command 0x15 before the info request. Its meaning is unknown; the device answers
 *  with four zero bytes. If that request goes unanswered or is refused, info is
 *  asked for anyway.
 */
class ClassicUsbDialect : public ClassicDialect
{
  public:
    enum InfoStep { Hello, Info };

    int nextInfoStep(int step) const override { return step == Hello ? Info : -1; }
    int infoFallbackStep(int step) const override { return step == Hello ? Info : -1; }
    Request infoRequest(int step) override;
    QString handleInfoReply(int step, const Reply &reply, DeviceInfo &info) override;
    QString infoTimeoutMessage(int step) const override;
    //! A SleepU on its cable ignored the request sent right after setting its clock;
    //! 1.5 s later it answered.
    int pauseAfterSetTimeMs() const override { return 1500; }
};

/*! O2Ring S, SC, SF, SP, ...: replies name their command.
 *
 *  ViHealth sets up an encrypted session only for a few models (not the O2Ring S,
 *  whose payloads it sends in plain text), so info starts with plain device info and
 *  file list. If the device doesn't answer, it falls back to the handshake and asks
 *  again with encrypted payloads.
 */
class O2RingSDialect : public Dialect
{
  public:
    enum InfoStep { PlainInfo, PlainFileList, Handshake, EncryptedInfo, EncryptedFileList };

  public:
    //! now gives the current time (replaceable for tests).
    explicit O2RingSDialect(std::function<QDateTime()> now = [] { return QDateTime::currentDateTime(); });

    QList<Reply> feed(const QByteArray &chunk) override;
    void clearBuffer() override { m_splitter.clear(); }

    int nextInfoStep(int step) const override;
    int infoFallbackStep(int step) const override;
    Request infoRequest(int step) override;
    QString handleInfoReply(int step, const Reply &reply, DeviceInfo &info) override;
    QString infoTimeoutMessage(int step) const override;

    Request setTimeRequest(const QDateTime &localTime) override;
    Request startRequest(const QString &name) override;
    qint64 fileSize(const Reply &reply) const override;
    Request dataRequest(quint16 chunk, qint64 received) override;
    Request endRequest() override;

    bool hasSession() const { return !m_sessionKey.isEmpty(); }

  private:
    Request make(quint8 cmd, const QByteArray &payload);

    O2RingS::FrameSplitter m_splitter;
    std::function<QDateTime()> m_now;
    QByteArray m_sessionKey;
    quint8 m_seq = 0;
};

} // namespace ViatomBle

#endif // VIATOMBLE_DIALECT_H
