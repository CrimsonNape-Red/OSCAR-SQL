/* Viatom BLE file downloader
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QDebug>
#include <QDir>
#include <QFileInfo>

#include "viatomble_downloader.h"
#include "viatomble_link.h"

namespace ViatomBle {

Downloader::Downloader(Link *link, QObject *parent)
    : QObject(parent), m_link(link)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &Downloader::onTimeout);
    connect(m_link, &Link::dataReceived, this, &Downloader::onDataReceived);
    connect(m_link, &Link::linkLost, this, &Downloader::onLinkLost);
}

Downloader::~Downloader()
{
    discardPartFile();
}

void Downloader::requestInfo()
{
    if (isBusy()) {
        qWarning() << "ViatomBle::Downloader: requestInfo while busy";
        return;
    }
    m_cancelRequested = false;
    m_splitter.clear();
    request(WaitInfo, CMD_INFO, 0);
}

void Downloader::setTime(const QDateTime &localTime)
{
    if (isBusy()) {
        qWarning() << "ViatomBle::Downloader: setTime while busy";
        return;
    }
    m_cancelRequested = false;
    m_splitter.clear();
    request(WaitSetTime, CMD_PARA_SYNC, 0, setTimePayload(localTime));
}

void Downloader::download(const QStringList &files, const QString &folder, bool overwriteExisting)
{
    if (isBusy()) {
        qWarning() << "ViatomBle::Downloader: download while busy";
        return;
    }
    m_queue = files;
    m_folder = folder;
    m_overwrite = overwriteExisting;
    m_cancelRequested = false;
    m_splitter.clear();
    QDir().mkpath(folder);
    startNextFile();
}

void Downloader::cancel()
{
    m_cancelRequested = true;
    if (m_state == WaitInfo) {
        m_timer.stop();
        m_state = Idle;
        m_generation++;
        emit failed(tr("Cancelled"));
    }
    // A file transfer stops at the next reply, so that the device's file is closed.
}

QString Downloader::targetPath(const QString &name) const
{
    // Device file names are plain names; never let one escape the folder.
    return QDir(m_folder).filePath(QFileInfo(name).fileName());
}

void Downloader::request(State state, quint8 cmd, quint16 seq, const QByteArray &payload)
{
    m_state = state;
    m_pendingFrame = buildRequest(cmd, seq, payload);
    m_attempt = 0;
    sendPending();
}

void Downloader::sendPending()
{
    m_link->send(m_pendingFrame);
    m_timer.start(m_replyTimeoutMs);
}

void Downloader::continueWith(std::function<void()> next)
{
    // Even when the device echoes sequence numbers, a late file chunk could arrive
    // after the last chunk and be taken for another command's reply, so always pause.
    if (m_attempt == 0) {
        next();
        return;
    }
    // The reply just accepted answered a retried request, so the reply to an earlier
    // attempt may still be on its way. Let it arrive and drop it before going on.
    m_settling = true;
    const int generation = m_generation;
    QTimer::singleShot(2 * m_replyTimeoutMs, this, [this, generation, next]() {
        m_settling = false;
        if (generation != m_generation) {
            return;
        }
        m_splitter.clear();
        next();
    });
}

void Downloader::onDataReceived(const QByteArray &chunk)
{
    const QList<Reply> replies = m_splitter.feed(chunk);
    for (const Reply &reply : replies) {
        if (m_settling) {
            qDebug() << "ViatomBle::Downloader: dropping late reply after a retry";
            continue;
        }
        if (m_state == Idle) {
            qDebug() << "ViatomBle::Downloader: ignoring reply while idle";
            continue;
        }
        handleReply(reply);
    }
}

void Downloader::handleReply(const Reply &reply)
{
    switch (m_state) {
    case WaitInfo: {
        m_timer.stop();
        m_state = Idle;
        if (!reply.ok()) {
            emit failed(tr("The device refused the request for its information."));
            return;
        }
        DeviceInfo info = DeviceInfo::fromPayload(reply.payload);
        if (!info.valid) {
            emit failed(tr("The device's information could not be read."));
            return;
        }
        emit infoReceived(info);
        return;
    }
    case WaitSetTime: {
        m_timer.stop();
        const QString error = reply.ok() ? QString() : tr("The device refused the new time.");
        continueWith([this, error]() {
            m_state = Idle;
            emit timeSet(error);
        });
        return;
    }
    case WaitStart:
        handleStart(reply);
        return;
    case WaitContent:
        handleContent(reply);
        return;
    case WaitEnd:
        m_timer.stop();
        continueWith([this]() { finishFile(); });
        return;
    case Idle:
        return;
    }
}

void Downloader::startNextFile()
{
    if (m_cancelRequested || m_queue.isEmpty()) {
        m_state = Idle;
        m_queue.clear();
        emit downloadFinished(m_cancelRequested);
        return;
    }
    m_current = m_queue.takeFirst();
    m_size = 0;
    m_received = 0;
    m_chunk = 0;
    m_skipCurrent = false;
    m_fileError.clear();

    QByteArray name = m_current.toLatin1();
    name.append('\0');
    request(WaitStart, CMD_READ_START, 0, name);
}

void Downloader::handleStart(const Reply &reply)
{
    m_timer.stop();
    if (!reply.ok()) {
        // The device did not open the file, so there is nothing to end.
        emit fileFailed(m_current, tr("The device refused to open the file."));
        continueWith([this]() { startNextFile(); });
        return;
    }
    m_size = fileSizeFromReadStart(reply);
    if (m_size < 0) {
        continueWith([this]() { endFile(tr("The device's reply did not include the file size.")); });
        return;
    }
    emit fileStarted(m_current, m_size);

    const QString target = targetPath(m_current);
    QString error;
    if (!m_overwrite && QFileInfo(target).exists() && QFileInfo(target).size() == m_size) {
        m_skipCurrent = true;
    } else if (m_cancelRequested) {
        error = tr("Cancelled");
    } else {
        m_part.setFileName(target + QStringLiteral(".part"));
        if (!m_part.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            error = tr("Could not write %1: %2").arg(m_part.fileName(), m_part.errorString());
        }
    }

    continueWith([this, error]() {
        if (m_skipCurrent || !error.isEmpty() || m_size == 0) {
            endFile(error);
        } else {
            request(WaitContent, CMD_READ_CONTENT, m_chunk);
        }
    });
}

void Downloader::handleContent(const Reply &reply)
{
    if (m_echoSeq && reply.seq != m_chunk) {
        // A late reply to an earlier, retried request.
        qDebug() << "ViatomBle::Downloader: dropping reply for chunk" << reply.seq << "while waiting for" << m_chunk;
        return;
    }
    m_timer.stop();
    if (m_chunk > 0 && reply.seq == m_chunk) {
        m_echoSeq = true;
    }

    QString error;
    if (!reply.ok()) {
        error = tr("The device reported an error while sending the file.");
    } else if (reply.payload.isEmpty()) {
        error = tr("The device sent an empty block before the end of the file.");
    } else if (m_part.write(reply.payload) != reply.payload.size()) {
        error = tr("Could not write %1: %2").arg(m_part.fileName(), m_part.errorString());
    } else {
        m_received += reply.payload.size();
        emit fileProgress(m_current, m_received, m_size);
        if (m_cancelRequested) {
            error = tr("Cancelled");
        }
    }

    continueWith([this, error]() {
        if (!error.isEmpty() || m_received >= m_size) {
            endFile(error);
        } else {
            m_chunk++;
            request(WaitContent, CMD_READ_CONTENT, m_chunk);
        }
    });
}

void Downloader::endFile(const QString &reason)
{
    m_fileError = reason;
    request(WaitEnd, CMD_READ_END, 0);
}

void Downloader::finishFile()
{
    const QString target = targetPath(m_current);
    if (m_skipCurrent) {
        emit fileSkipped(m_current, target);
    } else if (!m_fileError.isEmpty()) {
        discardPartFile();
        emit fileFailed(m_current, m_fileError);
    } else if (m_received != m_size) {
        discardPartFile();
        emit fileFailed(m_current, tr("Received %1 bytes, but the device announced %2.").arg(m_received).arg(m_size));
    } else {
        m_part.close();
        QFile::remove(target);
        if (!QFile::rename(m_part.fileName(), target)) {
            const QString reason = tr("Could not rename %1 to %2.").arg(m_part.fileName(), target);
            discardPartFile();
            emit fileFailed(m_current, reason);
        } else {
            m_part.setFileName(QString());
            emit fileSaved(m_current, target);
        }
    }
    startNextFile();
}

void Downloader::discardPartFile()
{
    if (m_part.isOpen()) {
        m_part.close();
    }
    if (!m_part.fileName().isEmpty() && QFile::exists(m_part.fileName())) {
        QFile::remove(m_part.fileName());
    }
    m_part.setFileName(QString());
}

void Downloader::onTimeout()
{
    if (m_attempt < m_maxRetries) {
        m_attempt++;
        qDebug() << "ViatomBle::Downloader: no reply, retry" << m_attempt << "of" << m_maxRetries;
        sendPending();
        return;
    }

    // Out of retries. Whatever follows is a fresh request, so no pause is needed.
    m_attempt = 0;
    switch (m_state) {
    case WaitInfo:
        m_state = Idle;
        emit failed(tr("The device did not answer. It may need an encrypted connection, "
                       "which OSCAR does not support yet."));
        return;
    case WaitSetTime:
        m_state = Idle;
        emit timeSet(tr("The device did not answer the request to set its clock."));
        return;
    case WaitStart:
        emit fileFailed(m_current, tr("The device did not answer the request to open the file."));
        startNextFile();
        return;
    case WaitContent:
        endFile(tr("The device stopped sending the file."));
        return;
    case WaitEnd:
        // The data is complete or already abandoned; a missing acknowledgement doesn't matter.
        finishFile();
        return;
    case Idle:
        return;
    }
}

void Downloader::onLinkLost(const QString &reason)
{
    m_timer.stop();
    m_settling = false;
    m_generation++;
    if (m_state == Idle) {
        return;
    }
    const bool transferring = (m_state != WaitInfo && m_state != WaitSetTime);
    m_state = Idle;
    if (transferring) {
        discardPartFile();
        emit fileFailed(m_current, reason);
        m_queue.clear();
    }
    emit failed(reason);
}

} // namespace ViatomBle
