/* EMAY oximeter protocol (EMO-80)
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef VIATOMBLE_EMAY_H
#define VIATOMBLE_EMAY_H

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QTimer>

#include "viatomble_downloaderbase.h"

namespace ViatomBle {

class Link;

/*! \brief The EMAY EMO-80's Bluetooth LE protocol, worked out from EMAY's Android app
 *  (see Notes/loaders/EMAY_EMO80_BLE.md). Not yet tried on a device.
 *
 *  Frames are "E2 cmd type lenHi lenLo payload E3 xor" in both directions; xor covers
 *  everything between E2 and E3. Requests have type '/', acknowledgements type 'O'.
 *  A reply carries its request's command; its first payload byte is a status, 0 = OK.
 */
namespace Emay {

extern const char *const SERVICE_UUID;   //!< 0000ff12-...
extern const char *const WRITE_UUID;     //!< 0000ff01-...
extern const char *const NOTIFY_UUID;    //!< 0000ff02-...

enum Command : quint8 {
    CMD_RESET = 0xA1,
    CMD_SET_TIME = 0xA2,
    CMD_GET_TIME = 0xA3,
    CMD_BATTERY = 0xA4,
    CMD_DEVICE_INFO = 0xA5,
    CMD_STORAGE = 0xA6,
    CMD_LIST = 0xA7,
    CMD_DATA = 0xA8,
    // 0xA9 deletes every recording on the device. OSCAR never sends it.
};

constexpr quint8 TYPE_REQUEST = '/';
constexpr quint8 TYPE_ACK = 'O';

QByteArray buildFrame(quint8 cmd, quint8 type, const QByteArray &payload = QByteArray());

struct Frame
{
    quint8 cmd = 0;
    quint8 type = 0;
    QByteArray payload;     //!< status byte first in a reply
    quint8 status() const { return payload.isEmpty() ? 0xFF : quint8(payload[0]); }
};

//! Collects notification chunks into frames, dropping anything that doesn't check out.
class FrameSplitter
{
  public:
    QList<Frame> feed(const QByteArray &chunk);
    void clear() { m_buffer.clear(); }

  private:
    QByteArray m_buffer;
};

//! One recording in the device's list.
struct Recording
{
    quint16 serial = 0;      //!< the device's number for it
    QDateTime start;         //!< local time
    quint32 duration = 0;    //!< seconds; one SpO2/pulse pair per second
};

//! Parses a list reply (after the status byte): endFlag, serial, user, BCD time, duration.
bool parseRecording(const QByteArray &payload, Recording &rec, bool &last);
//! Parses the device info reply into info (model, serial, firmware, clock).
bool parseDeviceInfo(const QByteArray &payload, DeviceInfo &info);
//! "yy MM dd hh mm ss" in BCD.
QByteArray bcdTime(const QDateTime &localTime);
QDateTime fromBcdTime(const QByteArray &bytes);

//! The name a recording is listed and saved under, e.g. "EMAY_20260715005945_3.spo2".
QString recordingName(const Recording &rec);

/*! A recording as a .spo2 file of the kind SpO2 Assistant saves, which OSCAR's CMS50F
 *  v3.7 reader imports: start time, then one SpO2/pulse pair per second. data is the
 *  device's pairs; readings it marks invalid become 0. Empty if there is no data. */
QByteArray spo2File(const QDateTime &start, const QByteArray &data);

} // namespace Emay

/*! \brief Lists and downloads the recordings on an EMAY EMO-80.
 *
 *  Info: device info (A5), battery (A4), storage (A6), then the list (A7), one recording
 *  per reply, each acknowledged until the last. A recording is asked for (A8) in packages
 *  of 200 bytes, each acknowledged until the last; a package that doesn't arrive in time
 *  is asked for again. OSCAR never deletes recordings on the device.
 */
class EmayDownloader : public DownloaderBase
{
    Q_OBJECT

  public:
    explicit EmayDownloader(Link *link, QObject *parent = nullptr);
    ~EmayDownloader() override;

    void setReplyTimeout(int ms) { m_replyTimeoutMs = ms; }
    void setMaxRetries(int retries) { m_maxRetries = retries; }
    //! The pause before each acknowledgement (the app waits 100 ms).
    void setAckDelay(int ms) { m_ackDelayMs = ms; }

    bool isBusy() const override { return m_state != Idle; }
    void requestInfo() override;
    void setTime(const QDateTime &localTime) override;
    void download(const QStringList &files, const QString &folder, bool overwriteExisting) override;
    void cancel() override;

  private:
    enum State { Idle, WaitInfo, WaitBattery, WaitStorage, WaitList, WaitSetTime, WaitData };

    void onDataReceived(const QByteArray &chunk);
    void onTimeout();
    void onLinkLost(const QString &reason);
    void handle(const Emay::Frame &frame);
    void send(State state, quint8 cmd, quint8 type, const QByteArray &payload);
    void acknowledge(quint8 cmd);
    void requestList();
    void requestPackage();
    void finishInfo();
    void startNextFile();
    void finishFile();
    void failFile(const QString &reason);

    Link *m_link;
    Emay::FrameSplitter m_splitter;
    QTimer m_timer;
    int m_replyTimeoutMs = 10000;   // the app's
    int m_maxRetries = 2;           // the app's, for packages
    int m_ackDelayMs = 100;
    State m_state = Idle;
    quint8 m_expectCmd = 0;
    QByteArray m_lastFrame;
    int m_attempt = 0;
    int m_generation = 0;

    DeviceInfo m_info;
    QHash<QString, Emay::Recording> m_recordings;
    int m_listRetries = 0;

    QStringList m_queue;
    QString m_folder;
    bool m_overwrite = false;
    bool m_cancelRequested = false;
    QString m_current;
    Emay::Recording m_currentRec;
    QByteArray m_data;
    quint16 m_package = 1;
};

} // namespace ViatomBle

#endif // VIATOMBLE_EMAY_H
