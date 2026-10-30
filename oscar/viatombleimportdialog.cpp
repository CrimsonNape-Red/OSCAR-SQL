/* Viatom BLE Import dialog
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

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
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "viatombleimportdialog.h"
#include "SleepLib/common.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/viatomble/viatomble_availability.h"
#include "SleepLib/viatomble/viatomble_dialect.h"
#include "SleepLib/viatomble/viatomble_device.h"
#include "SleepLib/viatomble/viatomble_downloader.h"

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

ViatomBleImportDialog::ViatomBleImportDialog(std::function<bool(const QString &)> importFile, QWidget *parent)
    : QDialog(parent), m_importFile(std::move(importFile))
{
    setWindowTitle(tr("Viatom BLE Import"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    resize(820, 560);

    // The same light appearance as the oximeter import wizard.
    setStyleSheet(
        "QDialog, QStackedWidget, QStackedWidget > QWidget { background-color: #f0f0f0; }"
        "QLabel { color: black; }"
        "QCheckBox { color: black; }"
        "QListWidget, QPlainTextEdit { color: black; background-color: white; }"
        "QPushButton { color: black; background-color: #f0f0f0;"
        "    border: 1px solid #adadad; border-radius: 3px; padding: 4px 10px; }"
        "QPushButton:hover    { background-color: #e5f1fb; border-color: #0078d7; }"
        "QPushButton:pressed  { background-color: #cce4f7; border-color: #0078d7; }"
        "QPushButton:disabled { color: #a0a0a0; border-color: #d0d0d0; }");

    auto *title = new QLabel(tr("Viatom BLE Import"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(18);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignCenter);

    // Left panel, as in the wizard
    auto *panel = new QFrame(this);
    panel->setStyleSheet("QFrame { background: qlineargradient(x1:0 y1:0, x2:1 y2:0, stop:0 white, stop:1 #789abc);"
                         " border: 1px solid gray; border-radius: 15px; }");
    panel->setFixedWidth(150);
    auto *panelLayout = new QVBoxLayout(panel);
    auto *icon = new QLabel(panel);
    icon->setStyleSheet("border: 0px;");
    icon->setPixmap(QPixmap(":/icons/wellue.png").scaled(110, 110, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    icon->setAlignment(Qt::AlignCenter);
    panelLayout->addStretch();
    panelLayout->addWidget(icon);
    panelLayout->addStretch();

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(buildWelcomePage());
    m_pages->addWidget(buildDevicesPage());
    m_pages->addWidget(buildFilesPage());
    m_pages->addWidget(buildTransferPage());

    auto *middle = new QHBoxLayout;
    middle->addWidget(panel);
    middle->addWidget(m_pages, 1);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_closeButton = new QPushButton(tr("Close"), this);
    connect(m_closeButton, &QPushButton::clicked, this, &ViatomBleImportDialog::reject);
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(m_status, 1);
    bottom->addWidget(m_closeButton);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(title);
    layout->addLayout(middle, 1);
    layout->addLayout(bottom);

    m_scanner = new Scanner(this);
    connect(m_scanner, &Scanner::deviceFound, this, &ViatomBleImportDialog::onDeviceFound);
    connect(m_scanner, &Scanner::finished, this, &ViatomBleImportDialog::onScanFinished);
    connect(m_scanner, &Scanner::error, this, [this](const QString &reason) {
        m_status->setText(tr("Bluetooth search failed: %1").arg(reason));
        onScanFinished();
    });

    showPage(WelcomePage);
}

ViatomBleImportDialog::~ViatomBleImportDialog()
{
    closeConnection();
}

QWidget *ViatomBleImportDialog::buildWelcomePage()
{
    auto *page = new QWidget(this);
    auto *text = new QLabel(page);
    text->setWordWrap(true);
    text->setTextFormat(Qt::RichText);
    text->setText(tr(
        "<h3>Download recordings from your oximeter over Bluetooth or USB</h3>"
        "<p>This works with Viatom/Wellue oximeters such as the O2Ring, SleepU, Checkme O2 and "
        "Wearable O2. Support for the O2Ring S family (O2Ring S, SC, SF, SP) is new and "
        "experimental: if it fails, OSCAR's debug log shows what happened.</p>"
        "<p>A SleepU can also be read through its USB cable (on Linux, for now). Plug it in "
        "before searching; it is listed first. The O2Ring's cable should work the same way "
        "but has not been tested.</p>"
        "<p><b>Before you start:</b></p>"
        "<ul>"
        "<li>Close the ViHealth app on your phone, or turn the phone's Bluetooth off. "
        "The oximeter talks to only one device at a time.</li>"
        "<li>Put the oximeter in standby. For an O2Ring, take it off your finger and wait until "
        "the countdown ends and it shows END. If it is switched off, put your finger in briefly "
        "and take it out again.</li>"
        "<li>Keep it close to the computer. It switches itself off after about two minutes "
        "without a connection.</li>"
        "</ul>"
        "<p>Downloaded files are kept in <i>%1</i> and can also be imported later with "
        "<i>Data → Import Viatom/Wellue Data</i>.</p>")
        .arg(QDir::toNativeSeparators(oximetryDataFolder())));

    auto *search = new QPushButton(tr("Search for devices"), page);
    connect(search, &QPushButton::clicked, this, &ViatomBleImportDialog::startScan);

    auto *layout = new QVBoxLayout(page);
    layout->addWidget(text);
    layout->addStretch();
    layout->addWidget(search, 0, Qt::AlignRight);
    return page;
}

QWidget *ViatomBleImportDialog::buildDevicesPage()
{
    auto *page = new QWidget(this);
    auto *heading = new QLabel(tr("<h3>Choose your oximeter</h3>"), page);
    m_deviceList = new QListWidget(page);
    connect(m_deviceList, &QListWidget::currentRowChanged, this, &ViatomBleImportDialog::updateConnectButton);
    connect(m_deviceList, &QListWidget::itemDoubleClicked, this, &ViatomBleImportDialog::connectToSelected);

    m_rescanButton = new QPushButton(tr("Search again"), page);
    connect(m_rescanButton, &QPushButton::clicked, this, &ViatomBleImportDialog::startScan);
    m_connectButton = new QPushButton(tr("Connect"), page);
    connect(m_connectButton, &QPushButton::clicked, this, &ViatomBleImportDialog::connectToSelected);

    // ViHealth sets the clock on every connection. The recordings' start times come from
    // this clock, so keeping it right keeps oximetry in step with CPAP data.
    m_setClock = new QCheckBox(tr("Set the oximeter's clock to this computer's time when connecting"), page);
    m_setClock->setChecked(profileFlag(PREF_SetClock));
    connect(m_setClock, &QCheckBox::toggled, this, [](bool on) {
        if (p_profile) {
            (*p_profile)[PREF_SetClock] = on;
        }
    });

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_rescanButton);
    buttons->addStretch();
    buttons->addWidget(m_connectButton);

    auto *layout = new QVBoxLayout(page);
    layout->addWidget(heading);
    layout->addWidget(m_deviceList, 1);
    layout->addWidget(m_setClock);
    layout->addLayout(buttons);
    return page;
}

QWidget *ViatomBleImportDialog::buildFilesPage()
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
    connect(m_downloadButton, &QPushButton::clicked, this, &ViatomBleImportDialog::startDownload);

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

QWidget *ViatomBleImportDialog::buildTransferPage()
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
    connect(m_moreButton, &QPushButton::clicked, this, &ViatomBleImportDialog::showFiles);

    // After the connection is lost, start again from the device search.
    m_searchAgainButton = new QPushButton(tr("Search again"), page);
    m_searchAgainButton->setVisible(false);
    connect(m_searchAgainButton, &QPushButton::clicked, this, &ViatomBleImportDialog::startScan);

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

void ViatomBleImportDialog::showPage(Page page)
{
    m_pages->setCurrentIndex(page);
}

void ViatomBleImportDialog::log(const QString &text)
{
    m_log->appendPlainText(text);
    qDebug().noquote() << "ViatomBleImport:" << text;
}

QString ViatomBleImportDialog::deviceLabel(const QBluetoothDeviceInfo &info) const
{
    const QString id = info.address().isNull() ? info.deviceUuid().toString() : info.address().toString();
    QString label = QStringLiteral("%1   %2").arg(info.name().isEmpty() ? tr("(unnamed)") : info.name(), id);
    if (info.rssi() != 0) {
        label += tr("   signal %1 dBm").arg(info.rssi());
    }
    return label;
}

// ----------------------------------------------------------------------------------------
// Searching and connecting

void ViatomBleImportDialog::startScan()
{
    closeConnection();
    showPage(DevicesPage);
    // USB cables are found at once and listed before any Bluetooth device.
    listUsbPorts();

    const Availability ble = availability();
    if (ble != Availability::Available) {
        m_status->setText(m_usbPorts.isEmpty()
            ? tr("No oximeter on a USB cable, and Bluetooth can't be used: %1").arg(availabilityReason(ble))
            : tr("Choose your oximeter and click Connect. (Bluetooth can't be used: %1)").arg(availabilityReason(ble)));
        m_rescanButton->setEnabled(true);
        return;
    }
    m_rescanButton->setEnabled(false);
    m_status->setText(tr("Searching for Viatom devices..."));
    m_scanner->start(10000);
}

void ViatomBleImportDialog::listUsbPorts()
{
    m_usbPorts.clear();
    m_devices.clear();
    m_families.clear();
    m_deviceList->clear();
    for (const Usb::Port &port : Usb::findPorts()) {
        m_usbPorts.append(port);
        auto *item = new QListWidgetItem(tr("SleepU or O2Ring on the USB cable   %1").arg(port.location), m_deviceList);
        if (!port.accessible) {
            item->setText(item->text() + tr("   (no permission)"));
            item->setToolTip(Usb::permissionHint());
        }
    }
    if (m_deviceList->count() > 0) {
        m_deviceList->setCurrentRow(0);
    }
    updateConnectButton();
}

void ViatomBleImportDialog::startWithUsb()
{
    closeConnection();
    showPage(DevicesPage);
    listUsbPorts();
    m_rescanButton->setEnabled(true);   // "Search again" also searches for Bluetooth devices
    if (m_usbPorts.isEmpty()) {
        m_status->setText(tr("No oximeter found on a USB cable. Plug in the cable and click Search again."));
        return;
    }
    if (m_usbPorts.size() == 1 && m_usbPorts.first().accessible) {
        // Once the dialog is showing, so the connection's messages appear in it.
        QTimer::singleShot(0, this, &ViatomBleImportDialog::connectToSelected);
        return;
    }
    m_status->setText(m_usbPorts.size() == 1 ? Usb::permissionHint()
                                             : tr("Choose your oximeter and click Connect."));
}

void ViatomBleImportDialog::onDeviceFound(const QBluetoothDeviceInfo &info, DeviceFamily family)
{
    // deviceFound repeats when the signal strength changes; update the existing row.
    int row = -1;
    for (int i = 0; i < m_devices.size(); i++) {
        if (m_devices[i].address() == info.address() && m_devices[i].deviceUuid() == info.deviceUuid()) {
            row = i;
            break;
        }
    }
    QString text = deviceLabel(info);
    if (family == DeviceFamily::O2RingS) {
        text += tr("   (O2Ring S family: experimental)");
    }
    if (row < 0) {
        m_devices.append(info);
        m_families.append(family);
        new QListWidgetItem(text, m_deviceList);
        if (m_deviceList->currentRow() < 0) {
            m_deviceList->setCurrentRow(m_deviceList->count() - 1);
        }
    } else {
        // The family can change as more of the advertisement arrives, e.g. a late name.
        m_devices[row] = info;
        m_families[row] = family;
        m_deviceList->item(m_usbPorts.size() + row)->setText(text);
    }
    updateConnectButton();
}

void ViatomBleImportDialog::onScanFinished()
{
    if (m_link) {
        // Stopping the scan to connect reports "finished" too, possibly after the connect
        // has started; the connection now owns the buttons and status.
        return;
    }
    m_rescanButton->setEnabled(true);
    if (m_pages->currentIndex() != DevicesPage) {
        return;
    }
    if (m_devices.isEmpty() && m_usbPorts.isEmpty()) {
        m_status->setText(tr("No Viatom device found. Check that it is in standby, close to the computer, "
                             "and not connected to the phone app, then search again."));
    } else {
        m_status->setText(tr("Choose your oximeter and click Connect."));
    }
}

void ViatomBleImportDialog::updateConnectButton()
{
    QListWidgetItem *item = m_deviceList->currentItem();
    m_connectButton->setEnabled(item && (item->flags() & Qt::ItemIsEnabled) && !m_link);
}

void ViatomBleImportDialog::connectToSelected()
{
    const int row = m_deviceList->currentRow();
    if (row < 0 || row >= m_usbPorts.size() + m_devices.size() || m_link) {
        return;
    }
    m_scanner->stop();
    const bool usb = row < m_usbPorts.size();
    UsbLink *usbLink = nullptr;
    Device *device = nullptr;
    QBluetoothDeviceInfo info;
    DeviceFamily family = DeviceFamily::Classic;
    if (usb) {
        m_link = usbLink = new UsbLink(this);
        // The SleepU and O2Ring use the classic protocol over the cable.
        m_downloader = new Downloader(m_link, std::make_unique<ClassicUsbDialect>(), this);
    } else {
        info = m_devices.at(row - m_usbPorts.size());
        family = m_families.at(row - m_usbPorts.size());
        m_link = device = new Device(this);
        if (family == DeviceFamily::O2RingS) {
            m_downloader = new Downloader(m_link, std::make_unique<O2RingSDialect>(), this);
        } else {
            m_downloader = new Downloader(m_link, this);
        }
    }
    connect(m_link, &Link::ready, this, &ViatomBleImportDialog::onDeviceReady);
    connect(m_link, &Link::connectFailed, this, &ViatomBleImportDialog::onConnectFailed);
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
    connect(m_downloader, &Downloader::infoReceived, this, &ViatomBleImportDialog::onInfoReceived);
    connect(m_downloader, &Downloader::timeSet, this, &ViatomBleImportDialog::onTimeSet);
    connect(m_downloader, &Downloader::failed, this, &ViatomBleImportDialog::onFailed);
    connect(m_downloader, &Downloader::fileStarted, this, &ViatomBleImportDialog::onFileStarted);
    connect(m_downloader, &Downloader::fileProgress, this, &ViatomBleImportDialog::onFileProgress);
    connect(m_downloader, &Downloader::fileSaved, this, &ViatomBleImportDialog::onFileSaved);
    connect(m_downloader, &Downloader::fileSkipped, this, &ViatomBleImportDialog::onFileSkipped);
    connect(m_downloader, &Downloader::fileFailed, this, &ViatomBleImportDialog::onFileFailed);
    connect(m_downloader, &Downloader::downloadFinished, this, &ViatomBleImportDialog::onDownloadFinished);

    m_connectButton->setEnabled(false);
    m_rescanButton->setEnabled(false);
    if (usb) {
        const Usb::Port port = m_usbPorts.at(row);
        m_status->setText(tr("Connecting to the USB cable..."));
        qDebug().noquote() << "ViatomBleImport: connecting over USB to" << port.path << port.location;
        usbLink->open(port.path);
        return;
    }
    m_status->setText(tr("Connecting to %1...").arg(info.name()));
    qDebug().noquote() << "ViatomBleImport: connecting to" << info.name() << deviceLabel(info)
                       << (family == DeviceFamily::O2RingS ? "as O2Ring S family" : "as classic");
    device->connectToDevice(info, family);
}

void ViatomBleImportDialog::onDeviceReady()
{
    m_status->setText(tr("Connected. Reading the list of recordings..."));
    m_downloader->requestInfo();
}

void ViatomBleImportDialog::onConnectFailed(const QString &reason)
{
    closeConnection();
    showPage(DevicesPage);
    m_rescanButton->setEnabled(true);
    updateConnectButton();
    m_status->setText(tr("Could not connect: %1").arg(reason));
}

void ViatomBleImportDialog::closeConnection()
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

QString ViatomBleImportDialog::folderFor(const DeviceInfo &info) const
{
    // The Viatom loader uses the folder name as the device's serial number.
    QString name = info.serialNumber;
    name.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")));
    if (name.isEmpty()) {
        const int row = m_deviceList->currentRow() - m_usbPorts.size();
        if (row >= 0 && row < m_devices.size()) {
            const QBluetoothDeviceInfo &dev = m_devices.at(row);
            name = dev.address().isNull() ? dev.deviceUuid().toString() : dev.address().toString();
            name.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")));
        }
    }
    if (name.isEmpty()) {
        name = QStringLiteral("Unknown");
    }
    return QDir(oximetryDataFolder()).filePath(name);
}

void ViatomBleImportDialog::onInfoReceived(const DeviceInfo &info)
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

void ViatomBleImportDialog::onTimeSet(const QString &error)
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

void ViatomBleImportDialog::showFiles()
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
    m_status->setText(info.files.isEmpty() ? tr("There are no recordings on this device.")
                                           : tr("%n recording(s) on the device.", "", info.files.size()));
}

void ViatomBleImportDialog::onFailed(const QString &reason)
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

void ViatomBleImportDialog::startDownload()
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
}

void ViatomBleImportDialog::onFileStarted(const QString &name, qint64 size)
{
    m_currentFile->setText(describeFile(name));
    m_fileProgress->setRange(0, int(qMax<qint64>(size, 1)));
    m_fileProgress->setValue(0);
}

void ViatomBleImportDialog::onFileProgress(const QString &, qint64 received, qint64 size)
{
    m_fileProgress->setRange(0, int(qMax<qint64>(size, 1)));
    m_fileProgress->setValue(int(received));
}

void ViatomBleImportDialog::onFileSaved(const QString &name, const QString &path)
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

void ViatomBleImportDialog::onFileSkipped(const QString &name, const QString &)
{
    m_skippedCount++;
    m_totalProgress->setValue(++m_filesDone);
    log(tr("Skipped %1: already downloaded").arg(describeFile(name)));
}

void ViatomBleImportDialog::onFileFailed(const QString &name, const QString &reason)
{
    m_failedCount++;
    m_totalProgress->setValue(++m_filesDone);
    log(tr("Failed %1: %2").arg(describeFile(name), reason));
}

void ViatomBleImportDialog::onDownloadFinished(bool cancelled)
{
    m_cancelButton->setVisible(false);
    m_moreButton->setVisible(m_link && m_link->isReady());
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

void ViatomBleImportDialog::reject()
{
    if (m_downloader && m_downloader->isBusy() && m_pages->currentIndex() == TransferPage) {
        if (QMessageBox::question(this, tr("Viatom BLE Import"),
                                  tr("A download is in progress. Stop it and close?")) != QMessageBox::Yes) {
            return;
        }
        // Closing drops the connection; the partial file is removed by the downloader.
    }
    m_scanner->stop();
    closeConnection();
    QDialog::reject();
}
