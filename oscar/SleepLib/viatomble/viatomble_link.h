/* Viatom BLE link interface
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef VIATOMBLE_LINK_H
#define VIATOMBLE_LINK_H

#include <QObject>
#include <QByteArray>

namespace ViatomBle {

//! A connected channel to a device: frames go out with send(), notification
//! chunks come back through dataReceived(). The implementations are ViatomBle::Device
//! (Bluetooth LE) and ViatomBle::UsbLink (the USB cable); the unit tests use a scripted fake.
class Link : public QObject
{
    Q_OBJECT

  public:
    explicit Link(QObject *parent = nullptr) : QObject(parent) {}
    ~Link() override = default;

    virtual void send(const QByteArray &frame) = 0;

    /*! Sends from now on with writes the device must acknowledge, for a device that
     *  ignores the faster unacknowledged ones. False if the link is already doing
     *  that or can't. */
    virtual bool useAcknowledgedWrites() { return false; }

    //! True once ready() has been emitted and until the link is closed or lost.
    virtual bool isReady() const { return true; }
    //! Closes the connection; nothing is emitted afterwards.
    virtual void close() {}

  signals:
    //! The connection is open; requests can be sent.
    void ready();
    void connectFailed(const QString &reason);
    void dataReceived(const QByteArray &chunk);
    void linkLost(const QString &reason);
};

} // namespace ViatomBle

#endif // VIATOMBLE_LINK_H
