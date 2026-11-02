/* Viatom USB link
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QThread>
#include <QTimer>

#include "viatomble_usb.h"

#if defined(Q_OS_LINUX)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/usbdevice_fs.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
}
#endif

namespace ViatomBle {

namespace Usb {

static const char *const tr_context = "ViatomBle::UsbLink";

QList<QByteArray> toReports(const QByteArray &frame)
{
    QList<QByteArray> reports;
    for (int i = 0; i < frame.size(); i += REPORT_SIZE - 1) {
        const QByteArray part = frame.mid(i, REPORT_SIZE - 1);
        QByteArray report(1, char(part.size()));
        report.append(part);
        report.append(QByteArray(REPORT_SIZE - report.size(), '\0'));
        reports.append(report);
    }
    return reports;
}

QByteArray fromReport(const QByteArray &report)
{
    if (report.isEmpty()) {
        return QByteArray();
    }
    const int len = quint8(report.at(0));
    if (len > REPORT_SIZE - 1 || len > report.size() - 1) {
        return QByteArray();
    }
    return report.mid(1, len);
}

QByteArray bridgeSetupReport()
{
    // Captured from O2 Insight Pro. The leading 0xFF can't be a data length, which is
    // presumably how the bridge tells setup from data; the rest is its serial setup.
    QByteArray report = QByteArray::fromHex("ffc783cc30");
    report.append(QByteArray(REPORT_SIZE - report.size(), '\0'));
    return report;
}

QString permissionHint()
{
    return QCoreApplication::translate(tr_context,
        "OSCAR needs permission to use the oximeter's USB cable. Install the OSCAR package, "
        "or add a udev rule such as /etc/udev/rules.d/60-oscar-viatom-usb.rules containing\n"
        "SUBSYSTEM==\"usb\", ATTR{idVendor}==\"1a86\", ATTR{idProduct}==\"e010\", MODE=\"0660\", TAG+=\"uaccess\"\n"
        "then unplug the cable and plug it in again.");
}

#if defined(Q_OS_LINUX)

bool supported()
{
    return true;
}

static QString readSysfs(const QString &dir, const char *name)
{
    QFile file(dir + QLatin1Char('/') + QLatin1String(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromLatin1(file.readAll()).trimmed();
}

QList<Port> findPorts()
{
    QList<Port> ports;
    const QString root = QStringLiteral("/sys/bus/usb/devices");
    const QStringList entries = QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : entries) {
        const QString dir = root + QLatin1Char('/') + entry;
        bool okVendor = false, okProduct = false;
        if (readSysfs(dir, "idVendor").toUShort(&okVendor, 16) != VENDOR_ID || !okVendor
                || readSysfs(dir, "idProduct").toUShort(&okProduct, 16) != PRODUCT_ID || !okProduct) {
            continue;
        }
        const int bus = readSysfs(dir, "busnum").toInt();
        const int devnum = readSysfs(dir, "devnum").toInt();
        if (bus <= 0 || devnum <= 0) {
            continue;
        }
        Port port;
        port.path = QStringLiteral("/dev/bus/usb/%1/%2").arg(bus, 3, 10, QLatin1Char('0'))
                                                     .arg(devnum, 3, 10, QLatin1Char('0'));
        const QString devpath = readSysfs(dir, "devpath");
        port.location = QCoreApplication::translate(tr_context, "USB bus %1, port %2").arg(bus).arg(devpath);
        port.accessible = ::access(QFile::encodeName(port.path).constData(), R_OK | W_OK) == 0;
        ports.append(port);
    }
    return ports;
}

#elif defined(Q_OS_WIN)

bool supported()
{
    return true;
}

QList<Port> findPorts()
{
    QList<Port> ports;
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        return ports;
    }
    SP_DEVICE_INTERFACE_DATA iface;
    iface.cbSize = sizeof(iface);
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &iface); i++) {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &needed, nullptr);
        if (needed == 0) {
            continue;
        }
        QByteArray buffer(int(needed), '\0');
        auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, needed, nullptr, nullptr)) {
            continue;
        }
        const QString path = QString::fromWCharArray(detail->DevicePath);
        // No access rights are needed to read the attributes, so this works even while
        // another program has the device open.
        HANDLE h = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            continue;
        }
        HIDD_ATTRIBUTES attributes;
        attributes.Size = sizeof(attributes);
        const bool match = HidD_GetAttributes(h, &attributes)
                           && attributes.VendorID == VENDOR_ID && attributes.ProductID == PRODUCT_ID;
        CloseHandle(h);
        if (match) {
            Port port;
            port.path = path;
            port.location = ports.isEmpty() ? QCoreApplication::translate(tr_context, "USB")
                                            : QCoreApplication::translate(tr_context, "USB %1").arg(ports.size() + 1);
            port.accessible = true;
            ports.append(port);
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return ports;
}

#else

bool supported()
{
    return false;
}

QList<Port> findPorts()
{
    return QList<Port>();
}

#endif

} // namespace Usb

// ---------------------------------------------------------------------------------------

static const unsigned int TIMEOUT_MS = 1000;
static const unsigned int READ_POLL_MS = 100;   // how soon the reader notices close()

UsbLink::UsbLink(QObject *parent)
    : Link(parent)
{
}

UsbLink::~UsbLink()
{
    close();
}

void UsbLink::fail(const QString &reason)
{
    qWarning().noquote() << "ViatomUsb: connect failed:" << reason;
    close();
    QTimer::singleShot(0, this, [this, reason]() { emit connectFailed(reason); });
}

void UsbLink::lose(const QString &reason)
{
    if (!m_open) {
        return;
    }
    qWarning().noquote() << "ViatomUsb: link lost:" << reason;
    close();
    emit linkLost(reason);
}

void UsbLink::startReader()
{
    m_stop = false;
    m_reader = QThread::create([this]() { readLoop(); });
    m_reader->start();
    QTimer::singleShot(0, this, [this]() {
        if (m_open) {
            emit ready();
        }
    });
}

void UsbLink::deliver(const QByteArray &report)
{
    const QByteArray data = Usb::fromReport(report);
    if (!data.isEmpty()) {
        QMetaObject::invokeMethod(this, [this, data]() {
            if (m_open) {
                emit dataReceived(data);
            }
        }, Qt::QueuedConnection);
    }
}

#if defined(Q_OS_LINUX)

static const unsigned char EP_OUT = 0x02;
static const unsigned char EP_IN = 0x82;

static QString errorText(int err)
{
    return QString::fromLocal8Bit(strerror(err));
}

void UsbLink::open(const QString &path)
{
    close();
    m_fd = ::open(QFile::encodeName(path).constData(), O_RDWR | O_CLOEXEC);
    if (m_fd < 0) {
        const int err = errno;
        fail(err == EACCES || err == EPERM ? Usb::permissionHint()
                                           : tr("Could not open %1: %2").arg(path, errorText(err)));
        return;
    }
    m_open = true;

    // Take the interface from the HID driver for as long as the link is open.
    usbdevfs_ioctl detach {};
    detach.ifno = 0;
    detach.ioctl_code = USBDEVFS_DISCONNECT;
    if (::ioctl(m_fd, USBDEVFS_IOCTL, &detach) < 0 && errno != ENODATA) {
        qWarning().noquote() << "ViatomUsb: could not detach the HID driver:" << errorText(errno);
    }
    unsigned int iface = 0;
    if (::ioctl(m_fd, USBDEVFS_CLAIMINTERFACE, &iface) < 0) {
        const int err = errno;
        fail(err == EBUSY ? tr("The USB cable is in use by another program, or by a virtual machine.")
                          : tr("Could not claim the USB cable: %1").arg(errorText(err)));
        return;
    }
    m_claimed = true;

    // SET_IDLE, then the setup report as a SET_REPORT (output report 0), as O2 Insight Pro does.
    usbdevfs_ctrltransfer idle {};
    idle.bRequestType = 0x21;
    idle.bRequest = 0x0A;
    idle.timeout = TIMEOUT_MS;
    if (::ioctl(m_fd, USBDEVFS_CONTROL, &idle) < 0) {
        qDebug().noquote() << "ViatomUsb: SET_IDLE not accepted:" << errorText(errno);
    }
    QByteArray setup = Usb::bridgeSetupReport();
    usbdevfs_ctrltransfer report {};
    report.bRequestType = 0x21;
    report.bRequest = 0x09;
    report.wValue = 0x0200;
    report.wLength = quint16(setup.size());
    report.timeout = TIMEOUT_MS;
    report.data = setup.data();
    if (::ioctl(m_fd, USBDEVFS_CONTROL, &report) < 0) {
        fail(tr("The USB cable did not accept its setup: %1").arg(errorText(errno)));
        return;
    }

    qDebug().noquote() << "ViatomUsb: opened" << path;
    startReader();
}

void UsbLink::readLoop()
{
    const int fd = m_fd;
    char buffer[Usb::REPORT_SIZE];
    while (!m_stop) {
        usbdevfs_bulktransfer in {};
        in.ep = EP_IN;
        in.len = sizeof(buffer);
        in.timeout = READ_POLL_MS;
        in.data = buffer;
        const int n = ::ioctl(fd, USBDEVFS_BULK, &in);
        if (n < 0) {
            const int err = errno;
            if (err == ETIMEDOUT || m_stop) {
                continue;
            }
            const QString reason = tr("The USB cable stopped working: %1").arg(errorText(err));
            QMetaObject::invokeMethod(this, [this, reason]() { lose(reason); }, Qt::QueuedConnection);
            return;
        }
        deliver(QByteArray(buffer, n));
    }
}

void UsbLink::send(const QByteArray &frame)
{
    if (!m_open) {
        return;
    }
    bool first = true;
    for (QByteArray report : Usb::toReports(frame)) {
        if (!first) {
            QThread::msleep(Usb::REPORT_GAP_MS);   // see REPORT_GAP_MS
        }
        first = false;
        usbdevfs_bulktransfer out {};
        out.ep = EP_OUT;
        out.len = report.size();
        out.timeout = TIMEOUT_MS;
        out.data = report.data();
        if (::ioctl(m_fd, USBDEVFS_BULK, &out) < 0) {
            lose(tr("Could not send to the USB cable: %1").arg(errorText(errno)));
            return;
        }
    }
}

void UsbLink::close()
{
    if (m_reader) {
        m_stop = true;
        m_reader->wait();
        delete m_reader;
        m_reader = nullptr;
    }
    m_open = false;
    if (m_fd < 0) {
        return;
    }
    if (m_claimed) {
        unsigned int iface = 0;
        ::ioctl(m_fd, USBDEVFS_RELEASEINTERFACE, &iface);
        m_claimed = false;
    }
    // Give the interface back to the HID driver.
    usbdevfs_ioctl attach {};
    attach.ifno = 0;
    attach.ioctl_code = USBDEVFS_CONNECT;
    ::ioctl(m_fd, USBDEVFS_IOCTL, &attach);
    ::close(m_fd);
    m_fd = -1;
}

#elif defined(Q_OS_WIN)

static QString errorText(DWORD err)
{
    wchar_t *text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, 0, reinterpret_cast<wchar_t *>(&text), 0, nullptr);
    const QString message = text ? QString::fromWCharArray(text).trimmed() : QString::number(err);
    LocalFree(text);
    return message;
}

void UsbLink::open(const QString &path)
{
    close();
    HANDLE h = CreateFileW(reinterpret_cast<const wchar_t *>(path.utf16()), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        fail(err == ERROR_ACCESS_DENIED || err == ERROR_SHARING_VIOLATION
                 ? tr("The USB cable is in use by another program, such as O2 Insight Pro. Close it and try again.")
                 : tr("Could not open the USB cable: %1").arg(errorText(err)));
        return;
    }
    m_handle = h;
    m_open = true;

    // Report sizes as Windows has them, including the report ID byte (0 here).
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    HIDP_CAPS caps {};
    if (HidD_GetPreparsedData(h, &preparsed)) {
        HidP_GetCaps(preparsed, &caps);
        HidD_FreePreparsedData(preparsed);
    }
    m_inputSize = caps.InputReportByteLength ? caps.InputReportByteLength : Usb::REPORT_SIZE + 1;
    m_outputSize = caps.OutputReportByteLength ? caps.OutputReportByteLength : Usb::REPORT_SIZE + 1;

    // The setup report goes as a control transfer (SET_REPORT), as O2 Insight Pro sends it.
    QByteArray setup(1, '\0');
    setup.append(Usb::bridgeSetupReport());
    setup.resize(qMax(setup.size(), m_outputSize));
    if (!HidD_SetOutputReport(h, setup.data(), ULONG(m_outputSize))) {
        fail(tr("The USB cable did not accept its setup: %1").arg(errorText(GetLastError())));
        return;
    }

    qDebug().noquote() << "ViatomUsb: opened" << path << "input" << m_inputSize << "output" << m_outputSize;
    startReader();
}

void UsbLink::readLoop()
{
    HANDLE h = m_handle;
    OVERLAPPED ov {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    QByteArray buffer(m_inputSize, '\0');
    bool pending = false;
    while (!m_stop) {
        if (!pending) {
            ResetEvent(ov.hEvent);
            if (!ReadFile(h, buffer.data(), DWORD(buffer.size()), nullptr, &ov)
                    && GetLastError() != ERROR_IO_PENDING) {
                const QString reason = tr("The USB cable stopped working: %1").arg(errorText(GetLastError()));
                QMetaObject::invokeMethod(this, [this, reason]() { lose(reason); }, Qt::QueuedConnection);
                break;
            }
            pending = true;
        }
        if (WaitForSingleObject(ov.hEvent, READ_POLL_MS) != WAIT_OBJECT_0) {
            continue;
        }
        pending = false;
        DWORD got = 0;
        if (!GetOverlappedResult(h, &ov, &got, FALSE)) {
            const QString reason = tr("The USB cable stopped working: %1").arg(errorText(GetLastError()));
            QMetaObject::invokeMethod(this, [this, reason]() { lose(reason); }, Qt::QueuedConnection);
            break;
        }
        if (got > 1) {
            deliver(buffer.mid(1, int(got) - 1));   // without the report ID
        }
    }
    if (pending) {
        CancelIoEx(h, &ov);
        DWORD got = 0;
        GetOverlappedResult(h, &ov, &got, TRUE);
    }
    CloseHandle(ov.hEvent);
}

void UsbLink::send(const QByteArray &frame)
{
    if (!m_open) {
        return;
    }
    OVERLAPPED ov {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    QString error;
    bool first = true;
    for (const QByteArray &report : Usb::toReports(frame)) {
        if (!first) {
            QThread::msleep(Usb::REPORT_GAP_MS);   // see REPORT_GAP_MS
        }
        first = false;
        QByteArray out(1, '\0');   // report ID
        out.append(report);
        out.resize(qMax(out.size(), m_outputSize));
        ResetEvent(ov.hEvent);
        DWORD written = 0;
        if (!WriteFile(m_handle, out.constData(), DWORD(m_outputSize), nullptr, &ov)
                && GetLastError() != ERROR_IO_PENDING) {
            error = errorText(GetLastError());
            break;
        }
        if (WaitForSingleObject(ov.hEvent, TIMEOUT_MS) != WAIT_OBJECT_0) {
            CancelIoEx(m_handle, &ov);
            GetOverlappedResult(m_handle, &ov, &written, TRUE);
            error = tr("timed out");
            break;
        }
        if (!GetOverlappedResult(m_handle, &ov, &written, FALSE)) {
            error = errorText(GetLastError());
            break;
        }
    }
    CloseHandle(ov.hEvent);
    if (!error.isEmpty()) {
        lose(tr("Could not send to the USB cable: %1").arg(error));
    }
}

void UsbLink::close()
{
    if (m_reader) {
        m_stop = true;
        m_reader->wait();
        delete m_reader;
        m_reader = nullptr;
    }
    m_open = false;
    if (m_handle) {
        CloseHandle(m_handle);
        m_handle = nullptr;
    }
}

#else

void UsbLink::open(const QString &)
{
    fail(tr("This version of OSCAR can't use the USB cable."));
}

void UsbLink::readLoop()
{
}

void UsbLink::send(const QByteArray &)
{
}

void UsbLink::close()
{
    m_open = false;
}

#endif

} // namespace ViatomBle
