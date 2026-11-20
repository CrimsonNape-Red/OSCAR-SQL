/* Device Time Correction Dialog Header
 *
 * The Time Corrections dialog (Data / Time Corrections...). It lists every device in the
 * profile and the time corrections saved for them (those for the day the Daily page shows,
 * or all of them), and edits one correction at a time with a live preview on the Daily graphs.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DEVICETIMECORRECTIONDIALOG_H
#define DEVICETIMECORRECTIONDIALOG_H

#include <QDialog>
#include <QDate>
#include <QHash>
#include <QTime>
#include <QTreeWidget>
#include <QCloseEvent>
#include "SleepLib/machine.h"
#include "database/device_time_correction_repository.h"

class QTableWidgetItem;

namespace Ui {
class DeviceTimeCorrectionDialog;
}

/*! \class DeviceTimeCorrectionDialog
    \brief Modeless dialog that lists, adds, edits and deletes device time corrections

    The dialog is not tied to one day. The device list shows every correctable device in
    the profile and the table can show every saved correction (by default it shows those
    of the selected device for the day shown in Daily); the day shown in Daily is only
    used to mark which corrections are in force, as the default date of a new correction,
    and for the live preview on the Daily graphs.

    A change is "staged" while it is being edited: it is applied to the Machine in memory so
    the graphs show it, but nothing is written to the database until Save. */
class DeviceTimeCorrectionDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DeviceTimeCorrectionDialog(QWidget *parent = nullptr);
    ~DeviceTimeCorrectionDialog();

    /*! \brief Tells the dialog which day the Daily page shows
        \param date The OSCAR day now shown in Daily (may be invalid when there is none)

        Asks to save or discard an unsaved correction when the day changes, then refreshes
        the device list, the table and the editor. */
    void setDate(const QDate& date);

    //! \brief Ends the dialog (Close button, Esc), first reverting an unsaved preview
    void done(int result) override;

signals:
    //! \brief Emitted when the corrections in memory change, so the graphs must be redrawn
    void correctionsChanged();

    //! \brief Emitted to ask the Daily page to show \a date (Show in Daily, or a double-click)
    void dateRequested(const QDate& date);

private slots:
    void onDeviceChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    void onNudgeMinus1h();
    void onNudgeMinus1m();
    void onNudgeMinus1s();
    void onNudgePlus1s();
    void onNudgePlus1m();
    void onNudgePlus1h();
    void onResetToZero();
    void onClearCorrection();
    void onSaveStaged();
    void onDiscardStaged();
    void onApplyLastNight();
    void onDeleteRow();
    void onHistoryRowSelected();
    //! \brief Loads a clicked or activated row that is selected but not in the editor (as after a Save)
    void onHistoryRowClicked(QTableWidgetItem* item);
    //! \brief Asks Daily to show the first day the selected correction applies to
    void onShowInDaily();
    void onAnyControlChanged();
    //! \brief Applies the type's default end date to a new correction, then onAnyControlChanged()
    void onTypeChanged();
    //! \brief Keeps the To date on or after the From date, then onAnyControlChanged()
    void onDateControlChanged();
    void onOffsetTimeChanged(const QTime &time);
    void onOffsetSignToggled(bool checked);

private:
    Ui::DeviceTimeCorrectionDialog *ui;
    QDate m_date;                       //!< The day shown in Daily
    Machine* m_machine = nullptr;       //!< The device in effect: the last device chosen in the list

    //! \brief Staged (preview) correction, not yet written to the database
    struct StagedRow {
        qint64  id        = 0;   //!< Database id of the row being replaced; 0 for a new correction
        qint64  machineId = 0;
        QString dateFrom, dateTo;
        QString type;
        qint64  offsetMs  = 0;
    };
    StagedRow m_staged;
    bool      m_hasStagedChange = false;
    bool      m_datesTouched = false;   //!< The user changed the dates of the new correction

    //! \brief Returns the device in effect (m_machine), or nullptr when there is none
    Machine* currentMachine() const;
    //! \brief Selects the list entry of the device in effect, after a group heading took the selection
    void highlightDevice();

    //! \brief Rebuilds the device list with every correctable device in the profile
    void populateSidebar();
    //! \brief Sets each device's name and tooltip (data span, corrections) and dims those with no data on m_date
    void refreshSidebarText();
    void refreshTitleArea(Machine* mach);
    void refreshCurrentOffset();
    //! \brief Reloads the corrections table from the database, applying the two filters
    void refreshHistory();
    //! \brief Updates the summary of what Save will store and the device's total for m_date
    void refreshSummary();

    void applyNudge(qint64 deltaMs);
    //! \brief Returns the dates in the editor as ISO strings; \a dateTo is empty for "No end"
    void effectiveDateRange(QString& dateFrom, QString& dateTo) const;
    //! \brief Returns false when the To date is before the From date
    bool datesValid() const;
    void previewStaged(Machine* mach);
    void clearStagedAndRevert();
    void resetToNewMode();
    void refreshModeLabel();
    void updateControlStates();
    //! \brief Shows \a text in the editor as the result of a Save (empty clears both status lines)
    void setStatus(const QString& text);
    //! \brief Shows \a text beside Close as the result of an action on the table's selected row
    void setListStatus(const QString& text);
    //! \brief Sets the text and the frame of the status line beside Close
    void showListStatus(const QString& text);

    QString currentTypeName() const;
    QString formatOffset(qint64 ms) const;
    //! \brief Returns "Brand Model (serial)" for \a mach, so two devices of one model can be told apart
    QString deviceLabel(Machine* mach) const;
    //! \brief Returns the display name of a correction type key ("dst" gives "DST Change")
    QString typeLabel(const QString& type) const;
    //! \brief Returns "on D", "from D onward" or "from D to E" for a correction's dates
    QString rangeText(const QString& dateFrom, const QString& dateTo) const;
    //! \brief Returns true when a correction from \a dateFrom to \a dateTo (empty = no end) covers \a date
    static bool coversDate(const QString& dateFrom, const QString& dateTo, const QDate& date);
    //! \brief Selects the table row with database id \a id without loading it into the editor
    void selectHistoryRowById(qint64 id);
    void rebuildMachine(Machine* mach);
    void commitAndRefresh(Machine* mach);

    void populateControlsFromRow(const DeviceTimeCorrectionData& row);

    // Delete correction helpers
    enum DeleteChoice { CloseForward, SplitOut, RemoveEntirely, DeleteCancelled };
    DeleteChoice showDeleteRangeDialog(const DeviceTimeCorrectionData& row);
    void splitOutDate(const DeviceTimeCorrectionData& row);

    // Rows shown in the table (parallel lists, same index as the table row)
    QList<DeviceTimeCorrectionData> m_historyRows;
    QList<Machine*>                 m_historyMachines;
    QHash<Machine*, int>            m_correctionCounts;   //!< Active corrections per device, unfiltered

    bool m_ready = false;           //!< Set at the end of the constructor; changeEvent() waits for it
    bool m_inThemeChange = false;   //!< Guards changeEvent() against re-entry while restyling

    //! \brief Sets the palette-derived colours of the labels and title bars
    void applyTheme();

protected:
    void closeEvent(QCloseEvent* event) override;
    //! \brief Restyles the dialog when the theme changes while it is open
    void changeEvent(QEvent* event) override;
};

#endif // DEVICETIMECORRECTIONDIALOG_H
