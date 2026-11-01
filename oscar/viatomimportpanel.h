/* Viatom/Wellue import panel
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef VIATOMIMPORTPANEL_H
#define VIATOMIMPORTPANEL_H

#include <functional>

#include <QBluetoothDeviceInfo>
#include <QWidget>
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
class DownloaderBase;
class Link;
}

/*! \brief Downloads recordings from an oximeter over Bluetooth LE (Viatom/Wellue, EMAY
 *  EMO-80), or from a Viatom/Wellue oximeter over its USB cable (SleepU, O2Ring S; the
 *  O2Ring's is untested). Contec's Bluetooth models are listed but can't be used yet.
 *
 *  The Oximetry Wizard's import step for these oximeters: choose a device, choose
 *  recordings, download. Files are kept in <profile folder>/Oximetry_Data/<serial>/ and,
 *  if the user wants, each one is imported as soon as it has arrived.
 *  See Notes/specs/2026-10-27-viatom-ble-import-design.md.
 */
class ViatomImportPanel : public QWidget
{
    Q_OBJECT

  public:
    //! importFile imports one downloaded file into the open profile; true on success.
    ViatomImportPanel(std::function<bool(const QString &)> importFile, QWidget *parent = nullptr);
    ~ViatomImportPanel() override;

    //! True if at least one file was imported, so the caller should refresh its views.
    bool importedAny() const { return m_importedCount > 0; }
    int importedCount() const { return m_importedCount; }

    /*! Starts on the USB cable without searching for Bluetooth devices; connects at
     *  once if exactly one usable cable is plugged in. */
    void startWithUsb();

    /*! Before leaving the panel: asks whether to stop a download in progress. False if
     *  the user wants to carry on; otherwise everything is stopped. */
    bool stopForClose();
    //! Stops searching and drops the connection, without asking.
    void stop();

    //! Devices of these families are listed first (the brand chosen in the wizard).
    void setPreferredFamilies(const QList<ViatomBle::DeviceFamily> &families) { m_preferred = families; }
    //! The connected device's details, e.g. for importing what it sent.
    const ViatomBle::DeviceInfo &deviceInfo() const { return m_info; }
    //! The family of the connected device (Classic for the USB cable).
    ViatomBle::DeviceFamily connectedFamily() const { return m_connectedFamily; }

  public slots:
    //! Lists USB cables, then searches for Bluetooth devices.
    void startScan();

  private slots:
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
    enum Page { DevicesPage, FilesPage, TransferPage };

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
    ViatomBle::DownloaderBase *m_downloader = nullptr;
    QList<ViatomBle::DeviceFamily> m_preferred;
    ViatomBle::DeviceFamily m_connectedFamily = ViatomBle::DeviceFamily::Classic;
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

#endif // VIATOMIMPORTPANEL_H
