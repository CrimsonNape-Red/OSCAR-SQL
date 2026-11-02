/* Oximeter Import Wizard Header
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 * Copyright (c) 2011-2018 Mark Watkins
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef OXIMETERIMPORT_H
#define OXIMETERIMPORT_H

#include <functional>

#include <QDialog>
#include <QTimer>

#include "SleepLib/serialoximeter.h"
#include "sessionbar.h"

class QButtonGroup;
class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class ViatomImportPanel;

namespace Ui {
class OximeterImport;
}

/*! \brief Imports oximetry: choose the oximeter, choose how to import, import, then
 *  line the recording up with CPAP data (or, for Viatom/Wellue, see a summary).
 *
 *  Viatom/Wellue oximeters are read over their USB cable, over Bluetooth (both in an
 *  embedded ViatomImportPanel) or from data files; the EMAY EMO-80 over Bluetooth; older
 *  Contec CMS50s over their USB-to-serial cable; Contec CMS50 and ChoiceMMed from the data
 *  files their own software saves. The step bar, device, method, serial-download and
 *  summary pages and the footer are built in code; the session list and CPAP-sync pages
 *  come from the .ui.
 */
class OximeterImport : public QDialog
{
    Q_OBJECT

public:
    explicit OximeterImport(QWidget *parent = nullptr);
    ~OximeterImport() override;

    //! Imports one downloaded Viatom file (each recording as it arrives); true on success.
    void setViatomFileImporter(std::function<bool(const QString &)> importFile);
    //! Lets the user choose Viatom data files and imports them; true if anything was imported.
    void setViatomFilesImporter(std::function<bool()> importFiles);

    //! Opens on the method step with Viatom/Wellue selected (Data → Import Viatom/Wellue Data).
    void showViatomImport();

    //! True if recordings were imported, so the caller should refresh its views.
    bool importedAny() const;

public slots:
    void reject() override;

private slots:
    void goNext();
    void goBack();

    void on_calendarWidget_clicked(const QDate &date);
    void on_calendarWidget_selectionChanged();
    void onSessionSelected(Session * session);
    void on_sessionBackButton_clicked();
    void on_sessionForwardButton_clicked();
    void on_radioSyncCPAP_clicked();
    void on_radioSyncOximeter_clicked();

private:
    enum Step { DeviceStep, MethodStep, ImportStep, FinishStep };
    enum Method { NoMethod, UsbMethod, BluetoothMethod, FilesMethod, SerialMethod };

    QWidget *buildStepBar();
    QWidget *buildFooter();
    QWidget *buildDevicePage();
    QWidget *buildMethodPage();
    QWidget *buildSummaryPage();
    QWidget *buildSerialPage();

    void showPage(QWidget *page, Step step);
    void updateStepBar();
    void updateFooter();
    void updateCableBadge();
    void refreshMethods();
    void showAbout();

    void startImport();
    //! CMS50 and ChoiceMMed: asks for a data file and reads it.
    void importOximeterFile();

    // Older CMS50 oximeters: download over their USB-to-serial cable.
    void startSerialImport();
    SerialOximeter *detectSerialOximeter();
    void doSerialImport();
    void onSerialProgress(int value, int total);
    void onSerialImportComplete(SerialOximeter *oxi);
    void serialStatus(const QString &text);
    void stopSerial();
    void chooseSession();
    void showSyncPage();
    //! Preselects the CPAP session that best fits the recording, if there is one.
    void suggestCpapSession();
    void saveSession();
    /*! Stores oxirec as a session of mod's device starting at startTime. askDevice: the
     *  recording came over the serial cable, so the reader can ask the device for its model. */
    void storeSession(SerialOximeter *mod, QVector<OxiRecord> *oxirec, const QDateTime &startTime,
                      bool askDevice = false);
    /*! Imports a downloaded recording file that carries its own start time (an EMAY
     *  .spo2); true if it was imported or is already in OSCAR. */
    bool importRecordingFile(const QString &path);
    void showSummary(const QString &text, const QString &heading = QString());

    int oximeterType() const;
    Method method() const;
    QString deviceName(int type) const;
    bool viatomUsbBuilt() const;

    Ui::OximeterImport *ui;

    // Built in code
    QList<QLabel *> m_stepNumbers;
    QList<QLabel *> m_stepTitles;
    QButtonGroup *m_devices = nullptr;
    QLabel *m_cableBadge = nullptr;
    QWidget *m_devicePage = nullptr;
    QWidget *m_methodPage = nullptr;
    QLabel *m_methodSubtitle = nullptr;
    QButtonGroup *m_methods = nullptr;
    QPushButton *m_usbOption = nullptr;
    QPushButton *m_bluetoothOption = nullptr;
    QPushButton *m_filesOption = nullptr;
    QLabel *m_usbNote = nullptr;
    QLabel *m_bluetoothNote = nullptr;
    QLabel *m_bluetoothDetail = nullptr;
    QLabel *m_filesDetail = nullptr;
    QPushButton *m_serialOption = nullptr;
    QLabel *m_serialDetail = nullptr;
    QCheckBox *m_serialSyncClock = nullptr;
    QWidget *m_serialPage = nullptr;
    QLabel *m_serialTitle = nullptr;
    QLabel *m_serialStatus = nullptr;
    QProgressBar *m_serialProgress = nullptr;
    QPlainTextEdit *m_serialLog = nullptr;
    QPushButton *m_serialRetry = nullptr;
    bool m_serialBusy = false;           //!< searching for or downloading from the device
    bool m_serialImport = false;         //!< the recording being matched came over the cable
    bool m_selectingSerialSession = false;
    int m_chosenSerialSession = -1;
    QLabel *m_deviceHelp = nullptr;
    QWidget *m_viatomPage = nullptr;
    ViatomImportPanel *m_viatomPanel = nullptr;
    QWidget *m_summaryPage = nullptr;
    QLabel *m_summaryHeading = nullptr;
    QLabel *m_summaryText = nullptr;
    QLabel *m_suggestion = nullptr;
    QPushButton *m_backButton = nullptr;
    QPushButton *m_nextButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QTimer m_cableTimer;

    Step m_step = DeviceStep;
    std::function<bool(const QString &)> m_importViatomFile;
    std::function<bool()> m_importViatomFiles;
    bool m_importedFiles = false;

    SerialOximeter * oximodule = nullptr;
    SessionBar * sessbar = nullptr;
};

#endif // OXIMETERIMPORT_H
