/* Viatom/Wellue import panel
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <algorithm>

#include <QCheckBox>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "viatomimportpanel.h"
#include "SleepLib/common.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/viatomble/viatomble_availability.h"
#include "SleepLib/viatomble/viatomble_dialect.h"
#ifdef OSCAR_HAVE_BLUETOOTH
#include "SleepLib/viatomble/viatomble_device.h"
#endif
#include "SleepLib/viatomble/viatomble_downloader.h"
#include "SleepLib/viatomble/emay.h"

using namespace ViatomBle;

static const QString PREF_ImportAsFilesArrive = QStringLiteral("ViatomBleImportAsFilesArrive");
static const QString PREF_SetClock = QStringLiteral("ViatomBleSetClock");

//! A profile setting that defaults to on.
static bool profileFlag(const QString &key)
{
    return p_profile == nullptr || !p_profile->contains(key) || (*p_profile)[key].toBool();
}
static const QString OXIMETRY_DATA_FOLDER = QStringLiteral("Oximetry_Data");

//! <profile folder>/Oximetry_Data: each user's recordings stay with their own profile.
static QString oximetryDataFolder()
{
    const QString base = p_profile ? p_profile->Get("{" + STR_GEN_DataFolder + "}") : GetAppData();
    return QDir(base).filePath(OXIMETRY_DATA_FOLDER);
}

//! A device file name with its recording start in front, e.g. "Sat Oct 25 2026 22:00  (20261025220000.vld)".
static QString describeFile(const QString &name)
{
    static const QRegularExpression stamp(QStringLiteral("(\\d{14})"));
    const QRegularExpressionMatch m = stamp.match(name);
    if (m.hasMatch()) {
        const QDateTime start = QDateTime::fromString(m.captured(1), QStringLiteral("yyyyMMddHHmmss"));
        if (start.isValid()) {
            return QStringLiteral("%1  (%2)").arg(QLocale().toString(start, QLocale::ShortFormat), name);
        }
    }
    return name;
}

ViatomImportPanel::ViatomImportPanel(std::function<bool(const QString &)> importFile, QWidget *parent)
    : QWidget(parent), m_importFile(std::move(importFile))
{
    m_pages = new QStackedWidget(this);
    m_pages->addWidget(buildDevicesPage());
    m_pages->addWidget(buildFilesPage());
    m_pages->addWidget(buildTransferPage());

    m_status = new QLabel(this);
    m_status->setWordWrap(true);

    m_warning = new QLabel(this);
    m_warning->setWordWrap(true);
    m_warning->setStyleSheet("QLabel { color: #a00000; font-weight: bold; }");
    m_warning->setVisible(false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_pages, 1);
    layout->addWidget(m_warning);
    layout->addWidget(m_status);

    // Some oximeters show up on USB only once they are put on the cable; after
    // startWithUsb() found none, look again every second and connect when one appears.
    m_usbWatch = new QTimer(this);
    m_usbWatch->setInterval(1000);
    connect(m_usbWatch, &QTimer::timeout, this, [this]() {
        if (m_link || m_pages->currentIndex() != DevicesPage) {
            m_usbWatch->stop();
            return;
        }
        if (!Usb::findPorts().isEmpty()) {
            m_usbWatch->stop();
            qDebug().noquote() << "ViatomBleImport: a USB cable appeared";
            startWithUsb();
        }
    });

#ifdef OSCAR_HAVE_BLUETOOTH
    m_scanner = new Scanner(this);
    connect(m_scanner, &Scanner::deviceFound, this, &ViatomImportPanel::onDeviceFound);
    connect(m_scanner, &Scanner::finished, this, &ViatomImportPanel::onScanFinished);
    connect(m_scanner, &Scanner::error, this, [this](const QString &reason) {
        m_status->setText(tr("Bluetooth search failed: %1").arg(reason));
        onScanFinished();
    });
#endif

    showPage(DevicesPage);
}

ViatomImportPanel::~ViatomImportPanel()
{
    closeConnection();
}

QWidget *ViatomImportPanel::buildDevicesPage()
{
    auto *page = new QWidget(this);
    auto *heading = new QLabel(tr("<h3>Choose your oximeter</h3>"), page);
    m_deviceList = new QListWidget(page);
    connect(m_deviceList, &QListWidget::currentRowChanged, this, &ViatomImportPanel::updateConnectButton);
    connect(m_deviceList, &QListWidget::itemDoubleClicked, this, &ViatomImportPanel::connectToSelected);

    m_rescanButton = new QPushButton(tr("Search again"), page);
    connect(m_rescanButton, &QPushButton::clicked, this, &ViatomImportPanel::startScan);
    m_connectButton = new QPushButton(tr("Connect"), page);
    connect(m_connectButton, &QPushButton::clicked, this, &ViatomImportPanel::connectToSelected);
    m_connectButton->setVisible(false);   // the wizard's Next button connects

    // ViHealth sets the clock on every connection. The recordings' start times come from
    // this clock, so keeping it right keeps oximetry in step with CPAP data.
    m_setClock = new QCheckBox(tr("Set the oximeter's clock to this computer's time when connecting"), page);
    m_setClock->setChecked(profileFlag(PREF_SetClock));
    connect(m_setClock, &QCheckBox::toggled, this, [](bool on) {
        if (p_profile) {
            (*p_profile)[PREF_SetClock] = on;
        }
    });

    m_permissionButton = new QPushButton(tr("Allow OSCAR to use the cable..."), page);
    m_permissionButton->setToolTip(tr("Installs a rule that lets you use the oximeter's USB cable "
                                      "(%1). You are asked for your password once.")
                                       .arg(QString::fromLatin1(Usb::UDEV_RULE_FILE)));
    m_permissionButton->setVisible(false);
    connect(m_permissionButton, &QPushButton::clicked, this, &ViatomImportPanel::installCablePermission);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_rescanButton);
    buttons->addWidget(m_permissionButton);
    buttons->addStretch();
    buttons->addWidget(m_connectButton);

    auto *layout = new QVBoxLayout(page);
    layout->addWidget(heading);
    layout->addWidget(m_deviceList, 1);
    layout->addWidget(m_setClock);
    layout->addLayout(buttons);
    return page;
}

QWidget *ViatomImportPanel::buildFilesPage()
{
    auto *page = new QWidget(this);
    auto *heading = new QLabel(tr("<h3>Choose recordings to download</h3>"), page);
    m_deviceInfo = new QLabel(page);
    m_deviceInfo->setWordWrap(true);
    m_folderLabel = new QLabel(page);
    m_folderLabel->setWordWrap(true);
    m_folderLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_fileList = new QListWidget(page);

    m_redownload = new QCheckBox(tr("Download again recordings already downloaded"), page);
    connect(m_redownload, &QCheckBox::toggled, this, [this](bool on) {
        for (int i = 0; i < m_fileList->count(); i++) {
            QListWidgetItem *item = m_fileList->item(i);
            if (item->data(Qt::UserRole + 1).toBool()) {   // already downloaded
                item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
            }
        }
    });

    m_importAsArrive = new QCheckBox(tr("Import into OSCAR as files arrive"), page);
    m_importAsArrive->setChecked(profileFlag(PREF_ImportAsFilesArrive));
    connect(m_importAsArrive, &QCheckBox::toggled, this, [](bool on) {
        if (p_profile) {
            (*p_profile)[PREF_ImportAsFilesArrive] = on;
        }
    });

    m_downloadButton = new QPushButton(tr("Download"), page);
    connect(m_downloadButton, &QPushButton::clicked, this, &ViatomImportPanel::startDownload);
    m_downloadButton->setVisible(false);   // the wizard's Next button downloads

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(m_downloadButton);

    auto *layout = new QVBoxLayout(page);
    layout->addWidget(heading);
    layout->addWidget(m_deviceInfo);
    layout->addWidget(m_folderLabel);
    layout->addWidget(m_fileList, 1);
    layout->addWidget(m_redownload);
    layout->addWidget(m_importAsArrive);
    layout->addLayout(buttons);
    return page;
}

QWidget *ViatomImportPanel::buildTransferPage()
{
    auto *page = new QWidget(this);
    auto *heading = new QLabel(tr("<h3>Downloading</h3>"), page);
    m_currentFile = new QLabel(page);
    m_fileProgress = new QProgressBar(page);
    m_totalProgress = new QProgressBar(page);
    m_log = new QPlainTextEdit(page);
    m_log->setReadOnly(true);

    m_cancelButton = new QPushButton(tr("Cancel download"), page);
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        if (m_downloader && m_downloader->isBusy()) {
            m_cancelButton->setEnabled(false);
            m_status->setText(tr("Stopping after the current block..."));
            m_downloader->cancel();
        }
    });

    // After a download, back to the file list (refreshed) while still connected.
    m_moreButton = new QPushButton(tr("Choose more recordings"), page);
    connect(m_moreButton, &QPushButton::clicked, this, &ViatomImportPanel::showFiles);

    // After the connection is lost, start again from the device search.
    m_searchAgainButton = new QPushButton(tr("Search again"), page);
    m_searchAgainButton->setVisible(false);
    connect(m_searchAgainButton, &QPushButton::clicked, this, &ViatomImportPanel::startScan);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(m_searchAgainButton);
    buttons->addWidget(m_moreButton);
    buttons->addWidget(m_cancelButton);

    auto *layout = new QVBoxLayout(page);
    layout->addWidget(heading);
    layout->addWidget(m_currentFile);
    layout->addWidget(new QLabel(tr("This recording:"), page));
    layout->addWidget(m_fileProgress);
    layout->addWidget(new QLabel(tr("All selected recordings:"), page));
    layout->addWidget(m_totalProgress);
    layout->addWidget(m_log, 1);
    layout->addLayout(buttons);
    return page;
}

void ViatomImportPanel::showPage(Page page)
{
    m_pages->setCurrentIndex(page);
    emit actionChanged();
}

QString ViatomImportPanel::primaryActionText() const
{
    switch (m_pages->currentIndex()) {
    case DevicesPage:
        return tr("Connect");
    case FilesPage:
        return tr("Download");
    default:
        return tr("Next");
    }
}

bool ViatomImportPanel::primaryActionEnabled() const
{
    switch (m_pages->currentIndex()) {
    case DevicesPage:
        return m_connectButton->isEnabled();
    case FilesPage:
        return m_downloadButton->isEnabled();
    default:
        return !(m_downloader && m_downloader->isBusy());
    }
}

bool ViatomImportPanel::primaryAction()
{
    switch (m_pages->currentIndex()) {
    case DevicesPage:
        if (m_connectButton->isEnabled()) {
            connectToSelected();
        }
        return true;
    case FilesPage:
        if (m_downloadButton->isEnabled()) {
            startDownload();
        }
        return true;
    default:
        return m_downloader && m_downloader->isBusy();
    }
}

void ViatomImportPanel::log(const QString &text)
{
    m_log->appendPlainText(text);
    qDebug().noquote() << "ViatomBleImport:" << text;
}

int ViatomImportPanel::bluetoothCount() const
{
#ifdef OSCAR_HAVE_BLUETOOTH
    return m_devices.size();
#else
    return 0;
#endif
}

#ifdef OSCAR_HAVE_BLUETOOTH
QString ViatomImportPanel::deviceLabel(const QBluetoothDeviceInfo &info) const
{
    const QString id = info.address().isNull() ? info.deviceUuid().toString() : info.address().toString();
    QString label = QStringLiteral("%1   %2").arg(info.name().isEmpty() ? tr("(unnamed)") : info.name(), id);
    if (info.rssi() != 0) {
        label += tr("   signal %1 dBm").arg(info.rssi());
    }
    return label;
}
#endif

// ----------------------------------------------------------------------------------------
// Searching and connecting

void ViatomImportPanel::startScan()
{
    m_usbWatch->stop();
    m_autoRescanned = false;
    closeConnection();
    showPage(DevicesPage);
    // USB cables are found at once and listed before any Bluetooth device.
    listUsbPorts();

    const Availability ble = availability();
    if (ble != Availability::Available || !m_scanner) {
        m_status->setText(m_usbPorts.isEmpty()
            ? tr("No oximeter on a USB cable, and Bluetooth can't be used: %1").arg(availabilityReason(ble))
            : tr("Choose your oximeter and click Connect. (Bluetooth can't be used: %1)").arg(availabilityReason(ble)));
        m_rescanButton->setEnabled(true);
        return;
    }
    m_rescanButton->setEnabled(false);
    m_status->setText(tr("Searching for oximeters..."));
#ifdef OSCAR_HAVE_BLUETOOTH
    m_scanner->start(10000);
#endif
}

void ViatomImportPanel::listUsbPorts()
{
    m_usbPorts.clear();
#ifdef OSCAR_HAVE_BLUETOOTH
    m_devices.clear();
#endif
    m_families.clear();
    m_deviceList->clear();
    for (const Usb::Port &port : Usb::findPorts()) {
        m_usbPorts.append(port);
        auto *item = new QListWidgetItem(port.cable == Usb::Cable::Checkme
                                             ? tr("Checkme O2 on the USB cable   %1").arg(port.location)
                                             : tr("Viatom/Wellue oximeter on the USB cable   %1").arg(port.location),
                                         m_deviceList);
        if (!port.accessible) {
            item->setText(item->text() + tr("   (no permission)"));
            item->setToolTip(Usb::permissionHint());
        }
    }
    if (m_deviceList->count() > 0) {
        m_deviceList->setCurrentRow(0);
    }
    const bool blocked = std::any_of(m_usbPorts.begin(), m_usbPorts.end(),
                                     [](const Usb::Port &p) { return !p.accessible; });
    m_permissionButton->setVisible(blocked && Usb::canInstallPermission());
    m_permissionButton->setEnabled(true);
    updateConnectButton();
}

void ViatomImportPanel::installCablePermission()
{
    const QStringList cmd = Usb::installPermissionCommand();
    auto *process = new QProcess(this);
    m_permissionButton->setEnabled(false);
    m_status->setText(tr("Waiting for your password..."));
    qDebug().noquote() << "ViatomBleImport: installing the cable's udev rule with" << cmd.first();
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        const QString err = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
        process->deleteLater();
        qDebug().noquote() << "ViatomBleImport: udev rule install finished, exit" << code << err;
        if (status == QProcess::NormalExit && code == 0) {
            m_status->setText(tr("Done: OSCAR can now use the cable."));
            // Listed again once udev has applied the rule; connects if it's the only cable.
            QTimer::singleShot(500, this, &ViatomImportPanel::startWithUsb);
            return;
        }
        m_permissionButton->setEnabled(true);
        if (code == 126) {   // pkexec: the password prompt was cancelled
            m_status->setText(tr("Not changed: the password prompt was cancelled."));
        } else {
            m_status->setText(tr("The permission could not be set up (%1). %2")
                                  .arg(err.isEmpty() ? tr("exit code %1").arg(code) : err, Usb::permissionHint()));
        }
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            m_permissionButton->setEnabled(true);
            m_status->setText(Usb::permissionHint());
        }
    });
    process->start(cmd.first(), cmd.mid(1));
}

void ViatomImportPanel::startWithUsb()
{
    closeConnection();
    showPage(DevicesPage);
    listUsbPorts();
    m_rescanButton->setEnabled(true);   // "Search again" also searches for Bluetooth devices
    qDebug().noquote() << "ViatomBleImport: USB cables found:" << m_usbPorts.size();
    if (m_usbPorts.isEmpty()) {
        m_status->setText(tr("Waiting for the oximeter on its USB cable. Plug in the cable; if the oximeter "
                             "is already on it, take it off the cable and put it back."));
        m_usbWatch->start();
        return;
    }
    if (m_usbPorts.size() == 1 && m_usbPorts.first().accessible) {
        // Once the dialog is showing, so the connection's messages appear in it.
        QTimer::singleShot(0, this, &ViatomImportPanel::connectToSelected);
        return;
    }
    const bool blocked = std::any_of(m_usbPorts.begin(), m_usbPorts.end(),
                                     [](const Usb::Port &p) { return !p.accessible; });
    if (blocked && Usb::canInstallPermission()) {
        m_status->setText(tr("OSCAR needs your permission to use the oximeter's USB cable. Click \"Allow OSCAR "
                             "to use the cable\"; you are asked for your password once."));
    } else {
        m_status->setText(m_usbPorts.size() == 1 ? Usb::permissionHint()
                                                 : tr("Choose your oximeter and click Connect."));
    }
}

#ifdef OSCAR_HAVE_BLUETOOTH
void ViatomImportPanel::onDeviceFound(const QBluetoothDeviceInfo &info, DeviceFamily scanned)
{
    // A name both brands use ("SleepO2") goes by the brand chosen in the wizard.
    const DeviceFamily family = m_preferred.contains(DeviceFamily::Emay) ? deviceFamily(info, true) : scanned;
    // deviceFound repeats when the signal strength changes; update the existing row.
    int row = -1;
    for (int i = 0; i < m_devices.size(); i++) {
        if (m_devices[i].address() == info.address() && m_devices[i].deviceUuid() == info.deviceUuid()) {
            row = i;
            break;
        }
    }
    QString text = deviceLabel(info) + QStringLiteral("   ") + brandName(info.name(), family);
    if (!isSupported(family)) {
        text += tr(": not supported yet");
    }
    QListWidgetItem *item = nullptr;
    if (row < 0) {
        // Devices of the brand chosen in the wizard go above the others.
        int at = m_devices.size();
        if (m_preferred.contains(family)) {
            for (int i = 0; i < m_families.size(); i++) {
                if (!m_preferred.contains(m_families[i])) {
                    at = i;
                    break;
                }
            }
        }
        m_devices.insert(at, info);
        m_families.insert(at, family);
        item = new QListWidgetItem(text);
        m_deviceList->insertItem(m_usbPorts.size() + at, item);
    } else {
        // The family can change as more of the advertisement arrives, e.g. a late name.
        m_devices[row] = info;
        m_families[row] = family;
        item = m_deviceList->item(m_usbPorts.size() + row);
        item->setText(text);
    }
    if (isSupported(family)) {
        item->setFlags(item->flags() | Qt::ItemIsEnabled);
        item->setToolTip(QString());
    } else {
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        item->setToolTip(tr("This oximeter uses Contec's Bluetooth protocol, which OSCAR can't download "
                            "from yet."));
    }
    QListWidgetItem *current = m_deviceList->currentItem();
    if (!current || !(current->flags() & Qt::ItemIsEnabled)
            || (m_preferred.contains(family) && isSupported(family) && row < 0
                && m_deviceList->row(current) > m_deviceList->row(item))) {
        if (item->flags() & Qt::ItemIsEnabled) {
            m_deviceList->setCurrentItem(item);
        }
    }
    updateConnectButton();
}
#endif

void ViatomImportPanel::onScanFinished()
{
    if (m_link) {
        // Stopping the scan to connect reports "finished" too, possibly after the connect
        // has started; the connection now owns the buttons and status.
        return;
    }
    // Windows can report a device before its name, and the name only by the next search,
    // so a search that found nothing is repeated once before giving up.
#ifdef OSCAR_HAVE_BLUETOOTH
    if (m_pages->currentIndex() == DevicesPage && m_devices.isEmpty() && m_usbPorts.isEmpty()
            && !m_autoRescanned && availability() == Availability::Available) {
        m_autoRescanned = true;
        qDebug().noquote() << "ViatomBleImport: nothing found, searching once more";
        m_status->setText(tr("Still searching for oximeters..."));
        m_scanner->start(10000);
        return;
    }
#endif
    m_rescanButton->setEnabled(true);
    if (m_pages->currentIndex() != DevicesPage) {
        return;
    }
    if (bluetoothCount() == 0 && m_usbPorts.isEmpty()) {
        m_status->setText(tr("No oximeter found. Check that it is switched on or in standby, close to the "
                             "computer, and not connected to a phone app, then search again."));
    } else {
        m_status->setText(tr("Choose your oximeter and click Connect."));
    }
}

void ViatomImportPanel::updateConnectButton()
{
    QListWidgetItem *item = m_deviceList->currentItem();
    m_connectButton->setEnabled(item && (item->flags() & Qt::ItemIsEnabled) && !m_link);
    emit actionChanged();
}

void ViatomImportPanel::connectToSelected()
{
    const int row = m_deviceList->currentRow();
    if (row < 0 || row >= m_usbPorts.size() + bluetoothCount() || m_link) {
        return;
    }
#ifdef OSCAR_HAVE_BLUETOOTH
    m_scanner->stop();
#endif
    const bool usb = row < m_usbPorts.size();
    m_warning->setVisible(false);   // a new connection starts afresh
    m_info = DeviceInfo();
    UsbLink *usbLink = nullptr;
#ifdef OSCAR_HAVE_BLUETOOTH
    Device *device = nullptr;
    QBluetoothDeviceInfo info;
    DeviceFamily family = DeviceFamily::Classic;
#endif
    if (usb) {
        m_connectedFamily = DeviceFamily::Classic;
        m_link = usbLink = new UsbLink(this);
        // The classic protocol over the cable. The SleepU's WCH bridge wants O2 Insight Pro's
        // hello first; a Checkme's own cable just takes the requests.
        if (m_usbPorts.at(row).cable == Usb::Cable::Checkme) {
            m_downloader = new Downloader(m_link, this);
        } else {
            m_downloader = new Downloader(m_link, std::make_unique<ClassicUsbDialect>(), this);
        }
    } else {
#ifdef OSCAR_HAVE_BLUETOOTH
        info = m_devices.at(row - m_usbPorts.size());
        family = m_families.at(row - m_usbPorts.size());
        if (!isSupported(family)) {
            return;
        }
        m_connectedFamily = family;
        m_link = device = new Device(this);
        if (family == DeviceFamily::Emay) {
            m_downloader = new EmayDownloader(m_link, this);
        } else if (family == DeviceFamily::O2RingS) {
            m_downloader = new Downloader(m_link, std::make_unique<O2RingSDialect>(), this);
        } else {
            m_downloader = new Downloader(m_link, this);
        }
#else
        return;   // only cables are listed without Bluetooth
#endif
    }
    connect(m_link, &Link::ready, this, &ViatomImportPanel::onDeviceReady);
    connect(m_link, &Link::connectFailed, this, &ViatomImportPanel::onConnectFailed);
    connect(m_link, &Link::linkLost, this, [this](const QString &reason) {
        if (!m_link || (m_downloader && m_downloader->isBusy())) {
            return;   // during a transfer the downloader reports it
        }
        if (m_pages->currentIndex() == TransferPage) {
            log(tr("The device disconnected: %1").arg(reason));
            m_moreButton->setVisible(false);
            m_searchAgainButton->setVisible(true);
            closeConnection();
            return;
        }
        // e.g. it switched itself off while the recordings were being chosen
        onFailed(tr("The device disconnected: %1").arg(reason));
    });
    connect(m_downloader, &DownloaderBase::infoReceived, this, &ViatomImportPanel::onInfoReceived);
    connect(m_downloader, &DownloaderBase::timeSet, this, &ViatomImportPanel::onTimeSet);
    connect(m_downloader, &DownloaderBase::failed, this, &ViatomImportPanel::onFailed);
    connect(m_downloader, &DownloaderBase::fileStarted, this, &ViatomImportPanel::onFileStarted);
    connect(m_downloader, &DownloaderBase::fileProgress, this, &ViatomImportPanel::onFileProgress);
    connect(m_downloader, &DownloaderBase::fileSaved, this, &ViatomImportPanel::onFileSaved);
    connect(m_downloader, &DownloaderBase::fileSkipped, this, &ViatomImportPanel::onFileSkipped);
    connect(m_downloader, &DownloaderBase::fileFailed, this, &ViatomImportPanel::onFileFailed);
    connect(m_downloader, &DownloaderBase::downloadFinished, this, &ViatomImportPanel::onDownloadFinished);
    if (usb) {
        // Over Bluetooth only one program can be connected; a cable can be shared.
        connect(m_downloader, &DownloaderBase::unrequestedReplies, this, [this]() {
            const QString text = tr("Another program seems to be using the oximeter's USB cable, such as "
                                    "O2 Insight Pro. Close it for a reliable download.");
            m_warning->setText(text);
            m_warning->setVisible(true);
            log(text);
        });
    }

    m_connectButton->setEnabled(false);
    m_rescanButton->setEnabled(false);
    if (usb) {
        const Usb::Port port = m_usbPorts.at(row);
        m_status->setText(tr("Connecting to the USB cable..."));
        qDebug().noquote() << "ViatomBleImport: connecting over USB to" << port.path << port.location;
        usbLink->open(port);
        return;
    }
#ifdef OSCAR_HAVE_BLUETOOTH
    m_status->setText(tr("Connecting to %1...").arg(info.name()));
    qDebug().noquote() << "ViatomBleImport: connecting to" << info.name() << deviceLabel(info)
                       << (family == DeviceFamily::O2RingS ? "as O2Ring S family"
            : family == DeviceFamily::Emay ? "as EMAY" : "as classic");
    device->connectToDevice(info, family);
#endif
}

void ViatomImportPanel::onDeviceReady()
{
    m_status->setText(tr("Connected. Reading the list of recordings..."));
    m_downloader->requestInfo();
}

void ViatomImportPanel::onConnectFailed(const QString &reason)
{
    closeConnection();
    showPage(DevicesPage);
    m_rescanButton->setEnabled(true);
    updateConnectButton();
    m_status->setText(tr("Could not connect: %1").arg(reason));
}

void ViatomImportPanel::closeConnection()
{
    if (m_downloader) {
        m_downloader->disconnect(this);
        m_downloader->deleteLater();
        m_downloader = nullptr;
    }
    if (m_link) {
        m_link->disconnect(this);
        m_link->close();
        m_link->deleteLater();
        m_link = nullptr;
    }
}

QString ViatomImportPanel::folderFor(const DeviceInfo &info) const
{
    // The Viatom loader uses the folder name as the device's serial number. EMAY
    // recordings (.spo2 files) get their own folders.
    QString name = info.serialNumber;
    name.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")));
    if (name.isEmpty()) {
#ifdef OSCAR_HAVE_BLUETOOTH
        const int row = m_deviceList->currentRow() - m_usbPorts.size();
        if (row >= 0 && row < m_devices.size()) {
            const QBluetoothDeviceInfo &dev = m_devices.at(row);
            name = dev.address().isNull() ? dev.deviceUuid().toString() : dev.address().toString();
            name.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")));
        }
#endif
    }
    if (name.isEmpty()) {
        name = QStringLiteral("Unknown");
    }
    if (m_connectedFamily == DeviceFamily::Emay) {
        name.prepend(QStringLiteral("EMAY_"));
    }
    return QDir(oximetryDataFolder()).filePath(name);
}

void ViatomImportPanel::onInfoReceived(const DeviceInfo &info)
{
    m_info = info;
    m_folder = folderFor(info);
    m_clockNote.clear();

    if (!m_setClock->isChecked()) {
        showFiles();
        return;
    }

    // Say how far off the clock was: recordings already on the device were timed by it.
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime deviceTime = parseDeviceTime(info.deviceTime);
    if (deviceTime.isValid()) {
        const qint64 offset = now.secsTo(deviceTime);
        if (qAbs(offset) >= 60) {
            const QString amount = qAbs(offset) >= 3600
                ? tr("%1 h %2 min").arg(qAbs(offset) / 3600).arg((qAbs(offset) % 3600) / 60)
                : tr("%1 min").arg(qAbs(offset) / 60);
            m_clockNote = (offset > 0 ? tr("The oximeter's clock was %1 fast.") : tr("The oximeter's clock was %1 slow."))
                              .arg(amount)
                        + QLatin1Char(' ')
                        + tr("Recordings already on it are timed by the old clock; "
                             "Data > Time Corrections can shift them after import.");
        }
    }
    m_status->setText(tr("Setting the oximeter's clock..."));
    m_downloader->setTime(now);
}

void ViatomImportPanel::onTimeSet(const QString &error)
{
    if (error.isEmpty()) {
        m_clockNote = (m_clockNote.isEmpty() ? QString() : m_clockNote + QLatin1Char(' '))
                    + tr("Its clock has been set to this computer's time.");
    } else {
        m_clockNote = (m_clockNote.isEmpty() ? QString() : m_clockNote + QLatin1Char(' '))
                    + tr("Its clock could not be set: %1").arg(error);
    }
    showFiles();
}

void ViatomImportPanel::showFiles()
{
    const DeviceInfo &info = m_info;

    QStringList details;
    if (!info.model.isEmpty()) {
        details << tr("Model %1").arg(info.model);
    }
    if (!info.serialNumber.isEmpty()) {
        details << tr("serial number %1").arg(info.serialNumber);
    }
    if (!info.battery.isEmpty()) {
        details << tr("battery %1").arg(info.battery);
    }
    if (!info.softwareVersion.isEmpty()) {
        details << tr("firmware %1").arg(info.softwareVersion);
    }
    m_deviceInfo->setText(details.join(QStringLiteral(", "))
                          + (m_clockNote.isEmpty() ? QString() : QStringLiteral("<br>") + m_clockNote));
    m_folderLabel->setText(tr("Files are saved in %1").arg(QDir::toNativeSeparators(m_folder)));

    m_fileList->clear();
    for (const QString &name : info.files) {
        const bool have = QFileInfo::exists(QDir(m_folder).filePath(QFileInfo(name).fileName()));
        QString text = describeFile(name);
        if (have) {
            text += tr("   already downloaded");
        }
        auto *item = new QListWidgetItem(text, m_fileList);
        item->setData(Qt::UserRole, name);
        item->setData(Qt::UserRole + 1, have);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(have && !m_redownload->isChecked() ? Qt::Unchecked : Qt::Checked);
    }

    showPage(FilesPage);
    m_downloadButton->setEnabled(!info.files.isEmpty());
    emit actionChanged();
    m_status->setText(info.files.isEmpty() ? tr("There are no recordings on this device.")
                                           : tr("%n recording(s) on the device.", "", info.files.size()));
}

void ViatomImportPanel::onFailed(const QString &reason)
{
    if (m_pages->currentIndex() == TransferPage) {
        // The link was lost during a download; the per-file failure is already logged.
        log(tr("Stopped: %1").arg(reason));
        closeConnection();
        onDownloadFinished(false);
        m_searchAgainButton->setVisible(true);
        return;
    }
    closeConnection();
    showPage(DevicesPage);
    m_rescanButton->setEnabled(true);
    updateConnectButton();
    m_status->setText(reason);
}

// ----------------------------------------------------------------------------------------
// Downloading

void ViatomImportPanel::startDownload()
{
    QStringList files;
    for (int i = 0; i < m_fileList->count(); i++) {
        if (m_fileList->item(i)->checkState() == Qt::Checked) {
            files << m_fileList->item(i)->data(Qt::UserRole).toString();
        }
    }
    if (files.isEmpty()) {
        m_status->setText(tr("Tick the recordings you want to download."));
        return;
    }

    m_filesTotal = files.size();
    m_filesDone = 0;
    m_savedCount = m_skippedCount = m_failedCount = 0;
    m_batchImportedCount = 0;
    m_importFailedCount = 0;
    m_log->clear();
    m_totalProgress->setRange(0, m_filesTotal);
    m_totalProgress->setValue(0);
    m_fileProgress->setRange(0, 1);
    m_fileProgress->setValue(0);
    m_cancelButton->setVisible(true);
    m_cancelButton->setEnabled(true);
    m_moreButton->setVisible(false);
    m_searchAgainButton->setVisible(false);
    showPage(TransferPage);
    m_status->setText(tr("Downloading. Keep the oximeter close to the computer."));

    m_downloader->download(files, m_folder, m_redownload->isChecked());
    emit actionChanged();
}

void ViatomImportPanel::onFileStarted(const QString &name, qint64 size)
{
    m_currentFile->setText(describeFile(name));
    m_fileProgress->setRange(0, int(qMax<qint64>(size, 1)));
    m_fileProgress->setValue(0);
}

void ViatomImportPanel::onFileProgress(const QString &, qint64 received, qint64 size)
{
    m_fileProgress->setRange(0, int(qMax<qint64>(size, 1)));
    m_fileProgress->setValue(int(received));
}

void ViatomImportPanel::onFileSaved(const QString &name, const QString &path)
{
    m_savedCount++;
    m_totalProgress->setValue(++m_filesDone);
    log(tr("Downloaded %1").arg(describeFile(name)));

    if (m_importAsArrive->isChecked() && m_importFile) {
        // Runs between transfers: the downloader asks for the next file after this returns.
        if (m_importFile(path)) {
            m_importedCount++;
            m_batchImportedCount++;
            log(tr("    imported into OSCAR"));
        } else {
            m_importFailedCount++;
            log(tr("    could not be imported; the file is kept"));
        }
    }
}

void ViatomImportPanel::onFileSkipped(const QString &name, const QString &path)
{
    m_skippedCount++;
    m_totalProgress->setValue(++m_filesDone);
    log(tr("Skipped %1: already downloaded").arg(describeFile(name)));

    // The saved copy is imported anyway, so a recording removed from OSCAR (purged)
    // comes back without downloading it again. One already in OSCAR is left as it is.
    if (m_importAsArrive->isChecked() && m_importFile) {
        if (m_importFile(path)) {
            m_importedCount++;
            m_batchImportedCount++;
            log(tr("    imported the saved copy into OSCAR (if it was already there, it is unchanged)"));
        } else {
            m_importFailedCount++;
            log(tr("    the saved copy could not be imported"));
        }
    }
}

void ViatomImportPanel::onFileFailed(const QString &name, const QString &reason)
{
    m_failedCount++;
    m_totalProgress->setValue(++m_filesDone);
    log(tr("Failed %1: %2").arg(describeFile(name), reason));
}

void ViatomImportPanel::onDownloadFinished(bool cancelled)
{
    m_cancelButton->setVisible(false);
    m_moreButton->setVisible(m_link && m_link->isReady());
    emit actionChanged();
    m_currentFile->clear();

    QStringList summary;
    summary << tr("%n downloaded", "", m_savedCount);
    if (m_skippedCount) {
        summary << tr("%n already downloaded", "", m_skippedCount);
    }
    if (m_failedCount) {
        summary << tr("%n failed", "", m_failedCount);
    }
    if (m_importAsArrive->isChecked()) {
        summary << tr("%n imported", "", m_batchImportedCount);
        if (m_importFailedCount) {
            summary << tr("%n could not be imported", "", m_importFailedCount);
        }
    }
    const QString text = (cancelled ? tr("Download cancelled: %1.") : tr("Finished: %1."))
                             .arg(summary.join(QStringLiteral(", ")));
    log(text);
    m_status->setText(text + QLatin1Char(' ') + tr("Files are in %1.").arg(QDir::toNativeSeparators(m_folder)));
}

bool ViatomImportPanel::stopForClose()
{
    if (m_downloader && m_downloader->isBusy() && m_pages->currentIndex() == TransferPage) {
        if (QMessageBox::question(this, tr("Oximeter Download"),
                                  tr("A download is in progress. Stop it?")) != QMessageBox::Yes) {
            return false;
        }
        // Closing drops the connection; the partial file is removed by the downloader.
    }
    stop();
    return true;
}

void ViatomImportPanel::stop()
{
    m_usbWatch->stop();
#ifdef OSCAR_HAVE_BLUETOOTH
    m_scanner->stop();
#endif
    closeConnection();
}
