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
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

#include "viatomble_usb.h"

#if defined(Q_OS_LINUX)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/usbdevice_fs.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
}
#elif defined(Q_OS_MACOS)
// AssertMacros.h (pulled in by IOKit) otherwise defines check(), verify() and require().
#define __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES 0
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <mach/mach_error.h>
#endif

namespace ViatomBle {

namespace Usb {

static const char *const tr_context = "ViatomBle::UsbLink";

QList<QByteArray> toReports(const QByteArray &frame, int reportSize)
{
    QList<QByteArray> reports;
    for (int i = 0; i < frame.size(); i += reportSize - 1) {
        const QByteArray part = frame.mid(i, reportSize - 1);
        QByteArray report(1, char(part.size()));
        report.append(part);
        report.append(QByteArray(reportSize - report.size(), '\0'));
        reports.append(report);
    }
    return reports;
}

QByteArray fromReport(const QByteArray &report, int reportSize)
{
    if (report.isEmpty()) {
        return QByteArray();
    }
    const int len = quint8(report.at(0));
    if (len > reportSize - 1 || len > report.size() - 1) {
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

const char *const UDEV_RULE =
    "SUBSYSTEM==\"usb\", ATTR{idVendor}==\"1a86\", ATTR{idProduct}==\"e010\", MODE=\"0660\", TAG+=\"uaccess\"\n"
    "SUBSYSTEM==\"hidraw\", ATTRS{idVendor}==\"1915\", ATTRS{idProduct}==\"f33d\", MODE=\"0660\", TAG+=\"uaccess\"";
const char *const UDEV_RULE_FILE = "/etc/udev/rules.d/60-oscar-viatom-usb.rules";

bool canInstallPermission()
{
#if defined(Q_OS_LINUX)
    return !QStandardPaths::findExecutable(QStringLiteral("pkexec")).isEmpty();
#else
    return false;
#endif
}

QStringList installPermissionCommand()
{
    // One root shell: write the rule, reload, and re-apply it to a cable already plugged in
    // (so it needn't be unplugged), then wait until udev has done so.
    const QString script = QStringLiteral(
        "printf '%s\\n' '%1' > '%2' && chmod 644 '%2' && udevadm control --reload-rules && "
        "udevadm trigger --action=change --subsystem-match=usb --attr-match=idVendor=1a86 "
        "--attr-match=idProduct=e010 && udevadm trigger --action=change --subsystem-match=hidraw && "
        "udevadm settle --timeout=5")
        .arg(QString::fromLatin1(UDEV_RULE), QString::fromLatin1(UDEV_RULE_FILE));
    return { QStringLiteral("pkexec"), QStringLiteral("/bin/sh"), QStringLiteral("-c"), script };
}

QString permissionHint()
{
    return QCoreApplication::translate(tr_context,
        "OSCAR needs permission to use the oximeter's USB cable. Install the OSCAR package, "
        "or add a udev rule such as /etc/udev/rules.d/60-oscar-viatom-usb.rules containing\n%1\n"
        "then unplug the cable and plug it in again.").arg(QString::fromLatin1(UDEV_RULE));
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

    // Checkme O2 cables, through hidraw: the HID device's uevent names it,
    // e.g. HID_ID=0003:00001915:0000F33D.
    const QString hidroot = QStringLiteral("/sys/class/hidraw");
    for (const QString &entry : QDir(hidroot).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System, QDir::Name)) {
        QFile uevent(hidroot + QLatin1Char('/') + entry + QStringLiteral("/device/uevent"));
        if (!uevent.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QString text = QString::fromLatin1(uevent.readAll()).toUpper();
        const QString id = QStringLiteral("HID_ID=0003:%1:%2")
                               .arg(CHECKME_VENDOR_ID, 8, 16, QLatin1Char('0'))
                               .arg(CHECKME_PRODUCT_ID, 8, 16, QLatin1Char('0')).toUpper();
        if (!text.contains(id)) {
            continue;
        }
        Port port;
        port.cable = Cable::Checkme;
        port.path = QStringLiteral("/dev/") + entry;
        port.location = QCoreApplication::translate(tr_context, "USB (%1)").arg(entry);
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
        const bool ok = HidD_GetAttributes(h, &attributes);
        const bool wch = ok && attributes.VendorID == VENDOR_ID && attributes.ProductID == PRODUCT_ID;
        const bool checkme = ok && attributes.VendorID == CHECKME_VENDOR_ID
                             && attributes.ProductID == CHECKME_PRODUCT_ID;
        CloseHandle(h);
        if (wch || checkme) {
            Port port;
            port.cable = checkme ? Cable::Checkme : Cable::WchBridge;
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

#elif defined(Q_OS_MACOS)

bool supported()
{
    return true;
}

static int intProperty(IOHIDDeviceRef device, CFStringRef key)
{
    int value = 0;
    CFTypeRef ref = IOHIDDeviceGetProperty(device, key);
    if (ref && CFGetTypeID(ref) == CFNumberGetTypeID()) {
        CFNumberGetValue(static_cast<CFNumberRef>(ref), kCFNumberIntType, &value);
    }
    return value;
}

static CFDictionaryRef matchingFor(int vendor, int product)
{
    CFMutableDictionaryRef dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 2, &kCFTypeDictionaryKeyCallBacks,
                                                            &kCFTypeDictionaryValueCallBacks);
    CFNumberRef v = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendor);
    CFNumberRef p = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &product);
    CFDictionarySetValue(dict, CFSTR(kIOHIDVendorIDKey), v);
    CFDictionarySetValue(dict, CFSTR(kIOHIDProductIDKey), p);
    CFRelease(v);
    CFRelease(p);
    return dict;
}

QList<Port> findPorts()
{
    QList<Port> ports;
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!manager) {
        return ports;
    }
    CFDictionaryRef wanted[2] = { matchingFor(VENDOR_ID, PRODUCT_ID),
                                  matchingFor(CHECKME_VENDOR_ID, CHECKME_PRODUCT_ID) };
    CFArrayRef matching = CFArrayCreate(kCFAllocatorDefault, reinterpret_cast<const void **>(wanted), 2,
                                        &kCFTypeArrayCallBacks);
    IOHIDManagerSetDeviceMatchingMultiple(manager, matching);
    CFRelease(matching);
    CFRelease(wanted[0]);
    CFRelease(wanted[1]);

    CFSetRef set = IOHIDManagerCopyDevices(manager);
    if (set) {
        const CFIndex count = CFSetGetCount(set);
        QList<const void *> devices(int(count), nullptr);
        CFSetGetValues(set, devices.data());
        for (const void *ref : devices) {
            IOHIDDeviceRef device = static_cast<IOHIDDeviceRef>(const_cast<void *>(ref));
            uint64_t entryId = 0;
            if (IORegistryEntryGetRegistryEntryID(IOHIDDeviceGetService(device), &entryId) != KERN_SUCCESS) {
                continue;
            }
            Port port;
            port.cable = intProperty(device, CFSTR(kIOHIDVendorIDKey)) == CHECKME_VENDOR_ID ? Cable::Checkme
                                                                                           : Cable::WchBridge;
            // The registry entry ID finds the same device again in UsbLink::open().
            port.path = QString::number(entryId);
            port.location = QCoreApplication::translate(tr_context, "USB (location %1)")
                                .arg(uint(intProperty(device, CFSTR(kIOHIDLocationIDKey))), 8, 16, QLatin1Char('0'));
            port.accessible = true;   // macOS asks no permission for an oximeter's HID device
            ports.append(port);
        }
        CFRelease(set);
    }
    CFRelease(manager);
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

void UsbLink::open(const QString &path)
{
    Usb::Port port;
    port.path = path;
    open(port);
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
    const QByteArray data = Usb::fromReport(report, m_reportSize);
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

void UsbLink::open(const Usb::Port &port)
{
    close();
    const QString path = port.path;
    m_cable = port.cable;
    m_reportSize = Usb::reportSize(port.cable);
    m_hidraw = (port.cable == Usb::Cable::Checkme);
    m_fd = ::open(QFile::encodeName(path).constData(), O_RDWR | O_CLOEXEC);
    if (m_fd < 0) {
        const int err = errno;
        fail(err == EACCES || err == EPERM ? Usb::permissionHint()
                                           : tr("Could not open %1: %2").arg(path, errorText(err)));
        return;
    }
    m_open = true;
    if (m_hidraw) {
        // A Checkme cable: its HID driver does the rest; no setup report.
        qDebug().noquote() << "ViatomUsb: opened" << path << "(Checkme cable, hidraw)";
        startReader();
        return;
    }

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
    char buffer[Usb::CHECKME_REPORT_SIZE];
    while (!m_stop && m_hidraw) {
        pollfd pfd { fd, POLLIN, 0 };
        const int r = ::poll(&pfd, 1, int(READ_POLL_MS));
        if (r == 0 || (r < 0 && errno == EINTR)) {
            continue;
        }
        const int n = r > 0 ? int(::read(fd, buffer, sizeof(buffer))) : -1;
        if (n <= 0) {
            if (m_stop) {
                break;
            }
            const QString reason = tr("The USB cable stopped working: %1")
                                       .arg(n == 0 ? tr("unplugged") : errorText(errno));
            QMetaObject::invokeMethod(this, [this, reason]() { lose(reason); }, Qt::QueuedConnection);
            return;
        }
        deliver(QByteArray(buffer, n));
    }
    while (!m_stop) {
        usbdevfs_bulktransfer in {};
        in.ep = EP_IN;
        in.len = Usb::REPORT_SIZE;
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
    for (QByteArray report : Usb::toReports(frame, m_reportSize)) {
        if (!first) {
            QThread::msleep(Usb::REPORT_GAP_MS);   // see REPORT_GAP_MS
        }
        first = false;
        if (m_hidraw) {
            // hidraw: the report number (0, the device has none) first, then the report.
            const QByteArray out = QByteArray(1, '\0') + report;
            if (::write(m_fd, out.constData(), size_t(out.size())) != out.size()) {
                lose(tr("Could not send to the USB cable: %1").arg(errorText(errno)));
                return;
            }
            continue;
        }
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
    if (m_hidraw) {
        ::close(m_fd);
        m_fd = -1;
        m_hidraw = false;
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

void UsbLink::open(const Usb::Port &port)
{
    close();
    const QString path = port.path;
    m_cable = port.cable;
    m_reportSize = Usb::reportSize(port.cable);
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
    m_inputSize = caps.InputReportByteLength ? caps.InputReportByteLength : m_reportSize + 1;
    m_outputSize = caps.OutputReportByteLength ? caps.OutputReportByteLength : m_reportSize + 1;
    if (m_cable == Usb::Cable::Checkme) {
        // A Checkme cable: no setup report.
        qDebug().noquote() << "ViatomUsb: opened" << path << "(Checkme cable) input" << m_inputSize
                           << "output" << m_outputSize;
        startReader();
        return;
    }

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
    for (const QByteArray &report : Usb::toReports(frame, m_reportSize)) {
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

#elif defined(Q_OS_MACOS)

static QString errorText(IOReturn err)
{
    const char *text = mach_error_string(err);
    return text ? QStringLiteral("%1 (0x%2)").arg(QString::fromLatin1(text)).arg(uint(err), 8, 16, QLatin1Char('0'))
                : QStringLiteral("0x%1").arg(uint(err), 8, 16, QLatin1Char('0'));
}

// IOKit calls these on the reader thread's run loop; context is the UsbLink.
static void inputReportCallback(void *context, IOReturn result, void *, IOHIDReportType, uint32_t,
                                uint8_t *report, CFIndex length)
{
    if (result == kIOReturnSuccess && length > 0) {
        static_cast<UsbLink *>(context)->deliverReport(QByteArray(reinterpret_cast<const char *>(report), int(length)));
    }
}

static void removalCallback(void *context, IOReturn, void *)
{
    static_cast<UsbLink *>(context)->deviceRemoved();
}

void UsbLink::deliverReport(const QByteArray &report)
{
    deliver(report);
}

//! The USB device (not its HID interface) that the HID device belongs to; released by the caller.
static io_service_t usbDeviceOf(IOHIDDeviceRef hid)
{
    io_registry_entry_t entry = IOHIDDeviceGetService(hid);
    IOObjectRetain(entry);
    while (entry != IO_OBJECT_NULL) {
        if (IOObjectConformsTo(entry, "IOUSBHostDevice") || IOObjectConformsTo(entry, "IOUSBDevice")) {
            return entry;
        }
        io_registry_entry_t parent = IO_OBJECT_NULL;
        const kern_return_t kr = IORegistryEntryGetParentEntry(entry, kIOServicePlane, &parent);
        IOObjectRelease(entry);
        entry = (kr == KERN_SUCCESS) ? parent : IO_OBJECT_NULL;
    }
    return IO_OBJECT_NULL;
}

static IOReturn controlRequest(IOUSBDeviceInterface **dev, UInt8 bRequest, UInt16 wValue, QByteArray *data)
{
    IOUSBDevRequest request {};
    request.bmRequestType = 0x21;   // host to device, class, interface 0
    request.bRequest = bRequest;
    request.wValue = wValue;
    request.wIndex = 0;
    request.wLength = data ? UInt16(data->size()) : 0;
    request.pData = data ? data->data() : nullptr;
    return (*dev)->DeviceRequest(dev, &request);
}

/*! Sends the WCH bridge its setup as a SET_REPORT control transfer on the device's default
 *  pipe, as O2 Insight Pro does (IOHIDDeviceSetReport() would use the interrupt OUT endpoint,
 *  which the bridge doesn't take as setup). Logs each step. */
static IOReturn sendBridgeSetup(IOHIDDeviceRef hid)
{
    io_service_t usb = usbDeviceOf(hid);
    if (usb == IO_OBJECT_NULL) {
        qWarning().noquote() << "ViatomUsb: no USB device above the cable's HID device";
        return kIOReturnNoDevice;
    }
    IOCFPlugInInterface **plugin = nullptr;
    SInt32 score = 0;
    const kern_return_t kr = IOCreatePlugInInterfaceForService(usb, kIOUSBDeviceUserClientTypeID,
                                                               kIOCFPlugInInterfaceID, &plugin, &score);
    IOObjectRelease(usb);
    if (kr != KERN_SUCCESS || !plugin) {
        qWarning().noquote() << "ViatomUsb: no USB device interface:" << errorText(kr);
        return kr != KERN_SUCCESS ? kr : kIOReturnError;
    }
    IOUSBDeviceInterface **dev = nullptr;
    const HRESULT hr = (*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID),
                                                 reinterpret_cast<LPVOID *>(&dev));
    IODestroyPlugInInterface(plugin);
    if (hr != S_OK || !dev) {
        qWarning().noquote() << "ViatomUsb: no USB device interface (QueryInterface" << hr << ")";
        return kIOReturnError;
    }

    QByteArray setup = Usb::bridgeSetupReport();
    IOReturn idle = controlRequest(dev, 0x0A, 0, nullptr);          // SET_IDLE
    IOReturn result = controlRequest(dev, 0x09, 0x0200, &setup);   // SET_REPORT, output report 0
    qDebug().noquote() << "ViatomUsb: setup without opening the device: SET_IDLE" << errorText(idle)
                       << "SET_REPORT" << errorText(result);
    if (result != kIOReturnSuccess) {
        // Some macOS versions want the device open for requests on its default pipe.
        const IOReturn opened = (*dev)->USBDeviceOpen(dev);
        qDebug().noquote() << "ViatomUsb: opening the USB device:" << errorText(opened);
        if (opened == kIOReturnSuccess) {
            idle = controlRequest(dev, 0x0A, 0, nullptr);
            result = controlRequest(dev, 0x09, 0x0200, &setup);
            qDebug().noquote() << "ViatomUsb: setup with the device open: SET_IDLE" << errorText(idle)
                               << "SET_REPORT" << errorText(result);
            (*dev)->USBDeviceClose(dev);
        }
    }
    (*dev)->Release(dev);
    return result;
}

void UsbLink::deviceRemoved()
{
    const QString reason = tr("The USB cable stopped working: %1").arg(tr("unplugged"));
    QMetaObject::invokeMethod(this, [this, reason]() { lose(reason); }, Qt::QueuedConnection);
}

void UsbLink::open(const Usb::Port &port)
{
    close();
    m_cable = port.cable;
    m_reportSize = Usb::reportSize(port.cable);
    bool ok = false;
    const uint64_t entryId = port.path.toULongLong(&ok);
    io_service_t service = ok ? IOServiceGetMatchingService(kIOMainPortDefault, IORegistryEntryIDMatching(entryId))
                              : IO_OBJECT_NULL;
    if (service == IO_OBJECT_NULL) {
        fail(tr("The USB cable is no longer connected."));
        return;
    }
    IOHIDDeviceRef device = IOHIDDeviceCreate(kCFAllocatorDefault, service);
    IOObjectRelease(service);
    if (!device) {
        fail(tr("Could not open the USB cable."));
        return;
    }
    const IOReturn err = IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone);
    if (err != kIOReturnSuccess) {
        CFRelease(device);
        fail(err == kIOReturnExclusiveAccess || err == kIOReturnBusy
                 ? tr("The USB cable is in use by another program, such as O2 Insight Pro. Close it and try again.")
                 : tr("Could not open the USB cable: %1").arg(errorText(err)));
        return;
    }
    m_device = device;
    m_open = true;
    // IOKit fills this buffer with each input report; it must outlive the registration.
    m_inputBuffer = QByteArray(qMax(m_reportSize, Usb::intProperty(device, CFSTR(kIOHIDMaxInputReportSizeKey))), '\0');
    IOHIDDeviceRegisterInputReportCallback(device, reinterpret_cast<uint8_t *>(m_inputBuffer.data()),
                                           m_inputBuffer.size(), inputReportCallback, this);
    IOHIDDeviceRegisterRemovalCallback(device, removalCallback, this);
    const bool checkme = (m_cable == Usb::Cable::Checkme);
    qDebug().noquote() << "ViatomUsb: opened" << port.location << (checkme ? "(Checkme cable)" : "(WCH bridge)")
                       << "input" << m_inputBuffer.size() << "output"
                       << Usb::intProperty(device, CFSTR(kIOHIDMaxOutputReportSizeKey));
    if (!checkme) {
        const IOReturn setup = sendBridgeSetup(device);
        if (setup != kIOReturnSuccess) {
            fail(tr("The USB cable did not accept its setup: %1").arg(errorText(setup)));
            return;
        }
    }
    startReader();
}

void UsbLink::readLoop()
{
    // The device's callbacks run on this thread's run loop until close().
    IOHIDDeviceRef device = static_cast<IOHIDDeviceRef>(m_device);
    IOHIDDeviceScheduleWithRunLoop(device, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    while (!m_stop) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, READ_POLL_MS / 1000.0, false);
    }
    IOHIDDeviceUnscheduleFromRunLoop(device, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
}

void UsbLink::send(const QByteArray &frame)
{
    if (!m_open) {
        return;
    }
    IOHIDDeviceRef device = static_cast<IOHIDDeviceRef>(m_device);
    bool first = true;
    for (const QByteArray &report : Usb::toReports(frame, m_reportSize)) {
        if (!first) {
            QThread::msleep(Usb::REPORT_GAP_MS);   // see REPORT_GAP_MS
        }
        first = false;
        // Report ID 0: the device has none, so the report goes without an ID byte. The
        // Checkme cable has no OUT endpoint, so macOS sends it as a SET_REPORT control
        // transfer; the WCH bridge's goes over its interrupt OUT endpoint, as on Linux.
        const IOReturn err = IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, 0,
                                                  reinterpret_cast<const uint8_t *>(report.constData()),
                                                  report.size());
        if (err != kIOReturnSuccess) {
            lose(tr("Could not send to the USB cable: %1").arg(errorText(err)));
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
    if (m_device) {
        IOHIDDeviceRef device = static_cast<IOHIDDeviceRef>(m_device);
        IOHIDDeviceRegisterInputReportCallback(device, reinterpret_cast<uint8_t *>(m_inputBuffer.data()),
                                               m_inputBuffer.size(), nullptr, nullptr);
        IOHIDDeviceRegisterRemovalCallback(device, nullptr, nullptr);
        IOHIDDeviceClose(device, kIOHIDOptionsTypeNone);
        CFRelease(device);
        m_device = nullptr;
    }
}

#else

void UsbLink::open(const Usb::Port &)
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
