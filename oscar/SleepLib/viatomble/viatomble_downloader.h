/* Viatom BLE file downloader
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef VIATOMBLE_DOWNLOADER_H
#define VIATOMBLE_DOWNLOADER_H

#include <functional>
#include <memory>

#include <QObject>
#include <QFile>
#include <QStringList>
#include <QTimer>

#include "viatomble_dialect.h"
#include "viatomble_protocol.h"

namespace ViatomBle {

class Link;

/*! \brief Reads device info and downloads files over a Link, without blocking.
 *
 *  The family-specific frames come from a Dialect (classic, or O2Ring S).
 *
 *  One request is outstanding at a time. Each request is retried after a timeout;
 *  when the retries run out the request fails. Where replies name their command,
 *  others are ignored. Classic replies don't, so a late reply to a retried request
 *  could be mistaken for the next reply. After
 *  accepting a reply to a retried request, the downloader therefore pauses and drops
 *  whatever arrives meanwhile. If the device has shown that it echoes sequence
 *  numbers, late file chunks are also recognised by number. A file is written to "<name>.part"
 *  and renamed to "<name>" only when it has the size the device announced, so an
 *  interrupted transfer never leaves a file that looks complete.
 */
class Downloader : public QObject
{
    Q_OBJECT

  public:
    //! Uses the classic dialect.
    explicit Downloader(Link *link, QObject *parent = nullptr);
    Downloader(Link *link, std::unique_ptr<Dialect> dialect, QObject *parent = nullptr);
    ~Downloader() override;

    //! Also sets the pause after a retried request to twice the timeout.
    void setReplyTimeout(int ms) { m_replyTimeoutMs = ms; }
    //! The timeout for opening a file, which can take the device a few seconds.
    void setOpenTimeout(int ms) { m_openTimeoutMs = ms; }
    void setMaxRetries(int retries) { m_maxRetries = retries; }

    bool isBusy() const { return m_state != Idle; }

    //! Asks the device for its info; answers with infoReceived() or failed().
    void requestInfo();

    //! Sets the device clock; answers with timeSet(). A failure here isn't fatal.
    void setTime(const QDateTime &localTime);

    /*! Downloads files into folder, one after another, then emits downloadFinished().
     *  A file already in folder with the size the device reports is skipped unless
     *  overwriteExisting is set. */
    void download(const QStringList &files, const QString &folder, bool overwriteExisting);

    //! Stops after closing the current file on the device; the partial file is removed.
    void cancel();

  signals:
    void infoReceived(const ViatomBle::DeviceInfo &info);
    //! error is empty on success.
    void timeSet(const QString &error);
    void fileStarted(const QString &name, qint64 size);
    void fileProgress(const QString &name, qint64 received, qint64 size);
    void fileSaved(const QString &name, const QString &path);
    void fileSkipped(const QString &name, const QString &path);
    void fileFailed(const QString &name, const QString &reason);
    void downloadFinished(bool cancelled);
    //! The info request failed, or the link was lost; nothing more will happen.
    void failed(const QString &reason);
    /*! Replies keep arriving that answer no request of ours, so another program is
     *  probably talking to the device too (over a USB cable, e.g. O2 Insight Pro).
     *  Emitted once. */
    void unrequestedReplies();

  private slots:
    void onDataReceived(const QByteArray &chunk);
    void onTimeout();
    int currentTimeout() const;
    void onLinkLost(const QString &reason);

  private:
    enum State { Idle, WaitInfo, WaitSetTime, WaitStart, WaitContent, WaitEnd };

    void request(State state, const Dialect::Request &request);
    void sendPending();
    //! Runs next now, or after a pause if the current request was retried or abandoned.
    void continueWith(std::function<void()> next);
    void handleReply(const Reply &reply);
    void handleInfo(const Reply &reply);
    //! Moves to the dialect's fallback for the current info step, if it has one.
    bool tryInfoFallback(const QString &why);
    void handleStart(const Reply &reply);
    void handleContent(const Reply &reply);
    void startNextFile();
    //! Ends the current file on the device; reason is empty on success.
    void endFile(const QString &reason);
    void finishFile();
    void discardPartFile();
    //! Counts a reply that can't be ours; emits unrequestedReplies() at the second.
    void noteUnrequested(const char *why);
    QString targetPath(const QString &name) const;

    Link *m_link;
    std::unique_ptr<Dialect> m_dialect;
    QTimer m_timer;
    int m_replyTimeoutMs = 2000;
    // The ViHealth SDK waits 6 s for every reply. A SleepU took just over 2 s to open
    // one recording, so a shorter wait resends the open while the device is working on it.
    int m_openTimeoutMs = 6000;
    int m_maxRetries = 3;

    State m_state = Idle;
    Dialect::Request m_pending;
    int m_infoStep = 0;
    DeviceInfo m_info;
    int m_attempt = 0;
    bool m_timedOut = false;   // the current request ran out of retries
    bool m_settling = false;
    bool m_heardFromDevice = false;      //!< any bytes at all have arrived
    int m_unrequested = 0;               //!< replies that answered no request of ours
    bool m_triedAcknowledged = false;    //!< asked the link for acknowledged writes
    int m_rawChunksLogged = 0;
    //! Bumped when an operation ends abnormally, so a pending pause doesn't resume it.
    int m_generation = 0;

    QStringList m_queue;
    QString m_folder;
    bool m_overwrite = false;
    bool m_cancelRequested = false;

    QString m_current;
    qint64 m_size = 0;
    qint64 m_received = 0;
    quint16 m_chunk = 0;
    QFile m_part;
    bool m_skipCurrent = false;
    QString m_fileError;
    //! Set once a reply has carried its request's (non-zero) sequence number. Never
    //! cleared: a mismatch might just be a late reply.
    bool m_echoSeq = false;
};

} // namespace ViatomBle

#endif // VIATOMBLE_DOWNLOADER_H
