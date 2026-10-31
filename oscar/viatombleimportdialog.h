/* Viatom BLE Import dialog
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef VIATOMBLEIMPORTDIALOG_H
#define VIATOMBLEIMPORTDIALOG_H

#include <functional>

#include <QBluetoothDeviceInfo>
#include <QDialog>
#include <QList>

#include "SleepLib/viatomble/viatomble_protocol.h"
#include "SleepLib/viatomble/viatomble_scanner.h"
#include "SleepLib/viatomble/viatomble_usb.h"

class QCheckBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTimer;

namespace ViatomBle {
class Downloader;
class Link;
}

/*! \brief Downloads recordings from a Viatom/Wellue oximeter over Bluetooth LE, or over
 *  its USB cable (SleepU, O2Ring S; the O2Ring's is untested). Titled "Viatom/Wellue Import".
 *
 *  Steps, in the style of the oximeter import wizard: instructions, choose a device,
 *  choose files, download. Files are kept in <profile folder>/Oximetry_Data/<serial>/ and,
 *  if the user wants, each one is imported as soon as it has arrived.
 *  See Notes/specs/2026-10-27-viatom-ble-import-design.md.
 */
class ViatomBleImportDialog : public QDialog
{
    Q_OBJECT

  public:
    //! importFile imports one downloaded file into the open profile; true on success.
    ViatomBleImportDialog(std::function<bool(const QString &)> importFile, QWidget *parent = nullptr);
    ~ViatomBleImportDialog() override;

    //! True if at least one file was imported, so the caller should refresh its views.
    bool importedAny() const { return m_importedCount > 0; }

    /*! Starts on the USB cable instead of the welcome page, without searching for
     *  Bluetooth devices; connects at once if exactly one usable cable is plugged in.
     *  Call before exec(). */
    void startWithUsb();

  public slots:
    void reject() override;

  private slots:
    void startScan();
    void onDeviceFound(const QBluetoothDeviceInfo &info, ViatomBle::DeviceFamily family);
    void onScanFinished();
    void connectToSelected();
    void onDeviceReady();
    void onConnectFailed(const QString &reason);
    void onInfoReceived(const ViatomBle::DeviceInfo &info);
    void onTimeSet(const QString &error);
    void onFailed(const QString &reason);
    void startDownload();
    void onFileStarted(const QString &name, qint64 size);
    void onFileProgress(const QString &name, qint64 received, qint64 size);
    void onFileSaved(const QString &name, const QString &path);
    void onFileSkipped(const QString &name, const QString &path);
    void onFileFailed(const QString &name, const QString &reason);
    void onDownloadFinished(bool cancelled);

  private:
    enum Page { WelcomePage, DevicesPage, FilesPage, TransferPage };

    QWidget *buildWelcomePage();
    QWidget *buildDevicesPage();
    QWidget *buildFilesPage();
    QWidget *buildTransferPage();
    void showPage(Page page);
    //! Clears the device list and lists the USB cables plugged in now.
    void listUsbPorts();
    void showFiles();
    void log(const QString &text);
    void updateConnectButton();
    void closeConnection();
    QString folderFor(const ViatomBle::DeviceInfo &info) const;
    QString deviceLabel(const QBluetoothDeviceInfo &info) const;

    std::function<bool(const QString &)> m_importFile;

    QStackedWidget *m_pages = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_warning = nullptr;     // stays up, unlike m_status, e.g. another program on the cable
    QPushButton *m_closeButton = nullptr;

    // Devices page
    QListWidget *m_deviceList = nullptr;
    QPushButton *m_rescanButton = nullptr;
    QPushButton *m_connectButton = nullptr;
    QCheckBox *m_setClock = nullptr;
    QList<ViatomBle::Usb::Port> m_usbPorts;      // listed first, then m_devices
    bool m_autoRescanned = false;                // a search that found nothing was repeated once
    QTimer *m_usbWatch = nullptr;                // waits for a cable after startWithUsb() found none
    QList<QBluetoothDeviceInfo> m_devices;
    QList<ViatomBle::DeviceFamily> m_families;   // parallel to m_devices

    // Files page
    QLabel *m_deviceInfo = nullptr;
    QLabel *m_folderLabel = nullptr;
    QListWidget *m_fileList = nullptr;
    QCheckBox *m_redownload = nullptr;
    QCheckBox *m_importAsArrive = nullptr;
    QPushButton *m_downloadButton = nullptr;

    // Transfer page
    QLabel *m_currentFile = nullptr;
    QProgressBar *m_fileProgress = nullptr;
    QProgressBar *m_totalProgress = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_moreButton = nullptr;
    QPushButton *m_searchAgainButton = nullptr;

    ViatomBle::Scanner *m_scanner = nullptr;
    ViatomBle::Link *m_link = nullptr;          // a Device or a UsbLink
    ViatomBle::Downloader *m_downloader = nullptr;
    ViatomBle::DeviceInfo m_info;
    QString m_folder;
    QString m_clockNote;

    int m_filesTotal = 0;
    int m_filesDone = 0;
    int m_savedCount = 0;
    int m_skippedCount = 0;
    int m_failedCount = 0;
    int m_importedCount = 0;          // since the dialog opened, for importedAny()
    int m_batchImportedCount = 0;     // in the current download
    int m_importFailedCount = 0;
};

#endif // VIATOMBLEIMPORTDIALOG_H
