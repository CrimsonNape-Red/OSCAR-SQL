/* Oximeter recording downloader interface
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef VIATOMBLE_DOWNLOADERBASE_H
#define VIATOMBLE_DOWNLOADERBASE_H

#include <QDateTime>
#include <QObject>
#include <QStringList>

#include "viatomble_protocol.h"

namespace ViatomBle {

/*! \brief What the import panel needs from a downloader, whatever the device's protocol.
 *
 *  Downloader implements it for Viatom devices (files on the device), EmayDownloader for
 *  the EMAY EMO-80 (numbered recordings, saved as .spo2 files). A device's recordings are
 *  listed in DeviceInfo::files by name; download() saves each one as a file of that name.
 */
class DownloaderBase : public QObject
{
    Q_OBJECT

  public:
    explicit DownloaderBase(QObject *parent = nullptr) : QObject(parent) {}
    ~DownloaderBase() override = default;

    virtual bool isBusy() const = 0;
    //! Asks the device for its info and list of recordings; answers with infoReceived() or failed().
    virtual void requestInfo() = 0;
    //! Sets the device clock; answers with timeSet(). A failure here isn't fatal.
    virtual void setTime(const QDateTime &localTime) = 0;
    /*! Downloads recordings into folder, one after another, then emits downloadFinished().
     *  One already in folder is skipped unless overwriteExisting is set. */
    virtual void download(const QStringList &files, const QString &folder, bool overwriteExisting) = 0;
    //! Stops after the current recording; a partial file is removed.
    virtual void cancel() = 0;

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
};

} // namespace ViatomBle

#endif // VIATOMBLE_DOWNLOADERBASE_H
