/* Viatom BLE protocol (classic Viatom oximeters)
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef VIATOMBLE_PROTOCOL_H
#define VIATOMBLE_PROTOCOL_H

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

// The classic protocol spoken by the SleepU, Checkme O2, O2Ring and their relatives
// (the ViHealth SDK's OxyCommonBleInterface; first ported to Qt in oxytool).
// See Notes/specs/2026-10-27-viatom-ble-import-design.md.
namespace ViatomBle {

// GATT service and characteristics
extern const char *const SERVICE_UUID;
extern const char *const WRITE_UUID;
extern const char *const NOTIFY_UUID;

enum Command : quint8 {
    CMD_READ_START   = 0x03,
    CMD_READ_CONTENT = 0x04,
    CMD_READ_END     = 0x05,
    CMD_INFO         = 0x14,
};

// Largest reply payload accepted; file chunks are at most 512 bytes and the
// info JSON is well under 4 KB.
constexpr int MAX_PAYLOAD = 8192;

//! CRC-8, polynomial 0x07, initial value 0, over all of data.
quint8 crc8(const char *data, int len);

//! Builds a request frame: AA cmd ~cmd seq(LE16) len(LE16) payload crc8.
QByteArray buildRequest(quint8 cmd, quint16 seq, const QByteArray &payload = QByteArray());

//! One reply frame: 55 status ~status seq(LE16) len(LE16) payload crc8.
struct Reply
{
    quint8 status = 0;   // 0 = success, 1 = error
    quint16 seq = 0;     // packet number as sent by the device
    QByteArray payload;

    bool ok() const { return status == 0; }
};

//! Turns notification chunks into complete reply frames. A reply may span several
//! notifications, and junk or corrupt frames are skipped by resynchronising on the
//! next 0x55 byte.
class FrameSplitter
{
  public:
    QList<Reply> feed(const QByteArray &chunk);
    void clear() { m_buffer.clear(); }
    //! Bytes thrown away while resynchronising since construction.
    int discardedBytes() const { return m_discarded; }

  private:
    QByteArray m_buffer;
    int m_discarded = 0;
};

//! Device information returned by CMD_INFO (a JSON object).
struct DeviceInfo
{
    bool valid = false;
    QString model;
    QString serialNumber;
    QString softwareVersion;
    QString hardwareVersion;
    QString battery;          // CurBAT, e.g. "85%"
    QString deviceTime;       // CurTIME
    QStringList files;        // parsed from the comma-separated FileList

    //! Parses the reply payload; trailing NUL bytes are ignored.
    static DeviceInfo fromPayload(const QByteArray &payload);
};

//! File size from a successful CMD_READ_START reply, or -1 if the payload is too short.
qint64 fileSizeFromReadStart(const Reply &reply);

} // namespace ViatomBle

Q_DECLARE_METATYPE(ViatomBle::DeviceInfo)

#endif // VIATOMBLE_PROTOCOL_H
