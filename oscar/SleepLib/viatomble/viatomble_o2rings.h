/* Viatom BLE protocol (O2Ring S family)
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef VIATOMBLE_O2RINGS_H
#define VIATOMBLE_O2RINGS_H

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QStringList>

#include "viatomble_protocol.h"

// The protocol of the O2Ring S, SC, SF, SP and relatives (the ViHealth SDK's
// OxyIIBleInterface, which uses Lepu's generic LpBleCmd commands for recordings).
// Decoded from the SDK only; see "Protocol: phase 2" in
// Notes/specs/2026-10-27-viatom-ble-import-design.md.
namespace ViatomBle {
namespace O2RingS {

// GATT service and characteristics (the SDK's Pf10Aw1BleManager)
extern const char *const SERVICE_UUID;
extern const char *const WRITE_UUID;
extern const char *const NOTIFY_UUID;

enum Command : quint8 {
    CMD_SET_UTC_TIME    = 0xC0,
    CMD_GET_INFO        = 0xE1,
    CMD_GET_FILE_LIST   = 0xF1,
    CMD_READ_FILE_START = 0xF2,
    CMD_READ_FILE_DATA  = 0xF3,
    CMD_READ_FILE_END   = 0xF4,
    CMD_ENCRYPT         = 0xFF,   // the handshake that sets up the session key
};

// Reply "type" byte: 1 is a normal reply, anything else an error code
// (e.g. 0xFB busy, 0xFD unsupported, 0xF3 decryption failed).
constexpr quint8 TYPE_REQUEST = 0;
constexpr quint8 TYPE_REPLY = 1;

//! Builds a request frame: A5 cmd ~cmd 00 seq len(LE16) payload crc8 (same CRC as the classic protocol).
QByteArray buildRequest(quint8 cmd, quint8 seq, const QByteArray &payload = QByteArray());

//! Turns notification chunks into frames. Reply::status is the frame's type byte
//! mapped so that 0 means a normal reply; Reply::cmd and Reply::seq are filled in.
class FrameSplitter
{
  public:
    QList<Reply> feed(const QByteArray &chunk);
    void clear() { m_buffer.clear(); }

  private:
    QByteArray m_buffer;
};

//! Maps a reply's type byte to Reply::status (0 = normal reply).
quint8 statusFromType(quint8 type);

// ---- Session encryption ------------------------------------------------------------
// The handshake token is XORed with a key hard-coded in the ViHealth app; the device
// answers with the AES-128 session key, XORed the same way. After that every non-empty
// payload, both ways, is AES-128-ECB with PKCS#7 padding under the session key.

//! The 16-byte token: every other byte of the app key, the app ID's first 4 bytes
//! ("0000" in ViHealth), then the time in seconds shifted right by 0..3 bits (as the SDK does).
QByteArray handshakeToken(qint64 unixSeconds, const QByteArray &appId = QByteArray("0000"));
//! XOR with the app key, repeated as needed. Its own inverse.
QByteArray scramble(const QByteArray &data);
//! The session key from the handshake reply payload, or empty if it is malformed.
QByteArray sessionKeyFromHandshake(const QByteArray &payload);
//! Empty data stays empty; on failure returns false.
bool encryptPayload(const QByteArray &key, const QByteArray &plain, QByteArray &out);
bool decryptPayload(const QByteArray &key, const QByteArray &cipher, QByteArray &out);

// ---- Payloads ----------------------------------------------------------------------

//! READ_FILE_START: the name in 16 bytes (zero-padded) and a 32-bit start offset.
QByteArray readStartPayload(const QString &name, quint32 offset = 0);
//! READ_FILE_DATA: the 32-bit offset of the next block.
QByteArray offsetPayload(quint32 offset);
//! SET_UTC_TIME: year(LE16) month day hour minute second (local), then the UTC offset
//! in tenths of an hour.
QByteArray utcTimePayload(const QDateTime &localTime, int utcOffsetSeconds);

//! GET_FILE_LIST reply: a count byte, then one 16-byte zero-padded name per file.
QStringList parseFileList(const QByteArray &payload);
//! GET_INFO reply: serial number (length at 37, text from 38, at most 18) and the device
//! clock (year at 24, month 26, day 27, hour 28, minute 29, second 30). Missing fields
//! are left empty or invalid.
DeviceInfo parseInfo(const QByteArray &payload);

} // namespace O2RingS
} // namespace ViatomBle

#endif // VIATOMBLE_O2RINGS_H
