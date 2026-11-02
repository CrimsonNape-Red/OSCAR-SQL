/* Viatom USB link
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef VIATOMBLE_USB_H
#define VIATOMBLE_USB_H

#include <atomic>

#include <QByteArray>
#include <QList>
#include <QString>

#include "viatomble_link.h"

class QThread;

namespace ViatomBle {

/*! \brief The USB cable of the SleepU (and, untested, the O2Ring).
 *
 *  The cable holds a WCH "HID To Serial" bridge (USB ID 1a86:e010). Serial data travels
 *  in 32-byte HID reports: a length byte, then up to 31 data bytes. Before any data, the
 *  bridge needs one output report sent as a control transfer (SET_REPORT), which sets
 *  up its serial side; Wellue's O2 Insight Pro sends the same bytes. Over the cable the
 *  device speaks the classic protocol (see ClassicUsbDialect).
 *
 *  Linux's HID driver sends output reports over the interrupt endpoint, which the bridge
 *  doesn't take as setup, so on Linux the link uses usbfs directly: it detaches the HID
 *  driver while connected and hands the interface back when closed. On Windows,
 *  HidD_SetOutputReport() sends the setup as a control transfer, so the link uses the
 *  HID driver.
 */
namespace Usb {

constexpr quint16 VENDOR_ID = 0x1a86;
constexpr quint16 PRODUCT_ID = 0xe010;
constexpr int REPORT_SIZE = 32;
/*! The pause between the reports of one frame. A SleepU lost the rest of a frame sent
 *  as reports back to back (a frame longer than 31 bytes, such as setting the clock),
 *  then stopped answering until it was switched off; 20 ms apart, it took them. */
constexpr int REPORT_GAP_MS = 20;

//! A frame split into reports of REPORT_SIZE bytes: length byte, data, zero padding.
QList<QByteArray> toReports(const QByteArray &frame);
//! The data carried by one report; empty if the length byte is out of range.
QByteArray fromReport(const QByteArray &report);
//! The output report that sets up the bridge, as O2 Insight Pro sends it.
QByteArray bridgeSetupReport();

struct Port
{
    QString path;        //!< what UsbLink::open() takes
    QString location;    //!< e.g. "USB bus 3, port 7", for the device list
    bool accessible = false;
};

//! Whether this build of OSCAR can use the USB cable.
bool supported();
//! The cables plugged in now.
QList<Port> findPorts();
//! How to give OSCAR access to the cable, for when a port isn't accessible.
QString permissionHint();

} // namespace Usb

class UsbLink : public Link
{
    Q_OBJECT

  public:
    explicit UsbLink(QObject *parent = nullptr);
    ~UsbLink() override;

    //! Opens the port; answers with ready() or connectFailed().
    void open(const QString &path);

    void send(const QByteArray &frame) override;
    bool isReady() const override { return m_open; }
    void close() override;

  private:
    void fail(const QString &reason);
    void lose(const QString &reason);
    void readLoop();
    void startReader();
    //! Called from the reader thread.
    void deliver(const QByteArray &report);

    bool m_open = false;
    QThread *m_reader = nullptr;
    std::atomic<bool> m_stop { false };
#if defined(Q_OS_WIN)
    void *m_handle = nullptr;      //!< HANDLE of the HID device, opened for overlapped I/O
    int m_inputSize = 0;           //!< report ID byte + input report
    int m_outputSize = 0;          //!< report ID byte + output report
#else
    int m_fd = -1;
    bool m_claimed = false;
#endif
};

} // namespace ViatomBle

#endif // VIATOMBLE_USB_H
