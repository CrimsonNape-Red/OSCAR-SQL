/* OSCAR Preferences Dialog Headers
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 * Copyright (c) 2011-2018 Mark Watkins
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef PREFERENCESDIALOG_H
#define PREFERENCESDIALOG_H

#include <QDialog>
#include <QModelIndex>
#include <QListWidgetItem>
#include <QStringListModel>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTextEdit>
#include <QIcon>
#include "SleepLib/profiles.h"
#include "SleepLib/channel_overrides.h"

class QTreeView;
class QPushButton;

namespace Ui {
class PreferencesDialog;
}


/*! \class MySortFilterProxyModel
    \brief Filters the channel trees of the Events and Waveforms tabs.

    Group header rows (children of the root) are always accepted, and channel rows are
    matched against the search text. When "customized only" is on, a channel row must
    also carry the customized flag (CustomizedRole on its name item), and a group header
    is shown only while at least one of its channel rows is.
    */
class MySortFilterProxyModel: public QSortFilterProxyModel
{
    Q_OBJECT
  public:
    //! \brief Role on a channel row's name item: true when the row differs from the code defaults.
    static constexpr int CustomizedRole = Qt::UserRole + 1;

    MySortFilterProxyModel(QObject *parent = 0);

    //! \brief Show only customized channel rows (and their group headers) when \p on is true.
    void setCustomizedOnly(bool on);

    //! \brief True while only customized rows are shown.
    bool customizedOnly() const { return m_customizedOnly; }

    /*! \brief Re-applies the filter at the next pass of the event loop when only customized
        rows are shown, so group headers follow changes to their rows' customized flags.
        Deferred so that a caller part way through a series of setData() calls on one
        proxy index never sees that index vanish under it. Calls are coalesced. */
    void scheduleRefilter();

  protected:
    //! \brief Simply extends filterAcceptRow to scan children as well
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const;

  private:
    bool m_customizedOnly = false;   //!< show only rows whose name item has CustomizedRole set
    bool m_refilterPending = false;  //!< a scheduleRefilter() pass is already queued
};

/*! \class PreferencesDialog
    \brief OSCAR's Main Preferences Window

    This provides the Preferences form and logic to alter Preferences for OSCAR
*/
class PreferencesDialog : public QDialog
{
    Q_OBJECT

  public:
    explicit PreferencesDialog(QWidget *parent, Profile *_profile);
    ~PreferencesDialog();

    //! \brief Save the current preferences, called when Ok button is clicked on.
    bool Save();

    QString clinicalHelp();
#ifndef NO_CHECKUPDATES
    //! \brief Updates the date text of the last time updates where checked
    void RefreshLastChecked();
#endif

  private slots:
    void on_combineSlider_valueChanged(int value);

    void on_IgnoreSlider_valueChanged(int value);


    void on_createSDBackups_toggled(bool checked);
    void on_combineSimilarMachines_toggled(bool checked);

    void on_okButton_clicked();

    void on_scrollDampeningSlider_valueChanged(int value);

    void on_tooltipTimeoutSlider_valueChanged(int value);

    void on_gridLineOpacitySlider_valueChanged(int value);

    void on_createSDBackups_clicked(bool checked);

    void on_resetChannelDefaults_clicked();

    void on_channelSearch_textChanged(const QString &arg1);

    void on_chanView_doubleClicked(const QModelIndex &index);

    void on_waveSearch_textChanged(const QString &arg1);

    void on_resetWaveformChannels_clicked();

    void on_waveView_doubleClicked(const QModelIndex &index);

    void on_maskLeaks4Slider_valueChanged(int value);

    void on_maskLeaks20Slider_valueChanged(int value);

    void on_calculateUnintentionalLeaks_toggled(bool arg1);

    void on_resetOxiMetryDefaults_clicked();
    #if defined(STEADY_BREATHING_ENHANCED_TESTING)
        void on_resetSteadyBreathingDefaults_clicked();
    #endif

    //! \brief Events tab: put the current channel row back to its code defaults (until OK).
    void on_resetOneChannel_clicked();
    //! \brief Waveforms tab: put the current channel row back to its code defaults (until OK).
    void on_resetOneWaveform_clicked();
    //! \brief Events tab: show only customized channels, or all again.
    void on_chanCustomizedOnly_toggled(bool checked);
    //! \brief Waveforms tab: show only customized channels, or all again.
    void on_waveCustomizedOnly_toggled(bool checked);

private:
    /*! \brief Which column of a channel tree holds which field. The name and enabled check
        box are always column 0; -1 marks a column the tab does not have. */
    struct ChannelColumns {
        int color;
        int overview;
        int label;
        int details;
        int lower;
        int upper;
    };

    //! \brief One channel tab (Events or Waveforms): what the shared row helpers work on.
    struct ChannelTab {
        QStandardItemModel *model = nullptr;
        MySortFilterProxyModel *proxy = nullptr;
        QTreeView *view = nullptr;
        QPushButton *resetButton = nullptr;   //!< this tab's "Reset Channel" button
        ChannelColumns cols {};
    };

    void InitChanInfo();
    void InitWaveInfo();

    void saveChanInfo();
    void saveWaveInfo();

    //! \brief Connects a channel tab's model, view and proxy to the customized-row helpers.
    void setupChannelTab(ChannelTab &tab);
    //! \brief The name item (column 0) of the channel row at \p proxyIndex; nullptr for a group header or no row.
    QStandardItem *channelRowAt(const ChannelTab &tab, const QModelIndex &proxyIndex) const;
    //! \brief The cell in column \p col of the row whose name item is \p nameItem; nullptr when \p col < 0.
    static QStandardItem *rowCell(QStandardItem *nameItem, int col);
    //! \brief The row's current cell values, as they would be saved by OK.
    ChannelOverrides::Values rowValues(const ChannelTab &tab, QStandardItem *nameItem) const;
    //! \brief The row's fields that differ from the channel's code defaults (ChannelOverrides::diffFrom).
    ChannelOverrides::Values rowOverrides(const ChannelTab &tab, QStandardItem *nameItem) const;
    //! \brief Sets the dot, customized flag, bold fonts and "Default:" tooltip lines of one row.
    void updateRowMarkers(const ChannelTab &tab, QStandardItem *nameItem);
    //! \brief updateRowMarkers() for every channel row of the tab; used after the tree is (re)built.
    void updateAllMarkers(const ChannelTab &tab);
    //! \brief Writes the channel's code defaults into the row's cells (model only; saved by OK).
    void resetRow(const ChannelTab &tab, QStandardItem *nameItem);
    //! \brief Enables the tab's Reset Channel button only while the current row is customized.
    void updateResetButton(const ChannelTab &tab);
    //! \brief Reacts to any edit of a cell of the tab's model by refreshing that row's markers.
    void channelItemChanged(const ChannelTab &tab, QStandardItem *item);
    //! \brief Right-click menu on a channel row, offering Reset Channel.
    void showChannelMenu(const ChannelTab &tab, const QPoint &pos);

    //! \brief Show these SpO2 baseline settings in the Oximetry tab
    void showSpO2Baseline(SpO2BaselineMethod method, int window, int percentile, int manual);
    //! \brief Enable the rolling window and manual value only for the method that uses them
    void updateSpO2BaselineControls();
#if defined(STEADY_BREATHING)
    //! \brief Lock the Steady Breathing box in Clinical Mode, unlock it in Permissive Mode
    void updateSteadyBreathingForMode(bool clinical);
    bool m_steadyBreathingChoice = false;   //!< the user's on/off choice, kept while the box is locked
    QString m_steadyBreathingTitle;         //!< the box's own title, restored when unlocked
#endif

    QHash<MachineType, QStandardItem *> toplevel;
    QHash<MachineType, QStandardItem *> machlevel;

    Ui::PreferencesDialog *ui;
    Profile *profile;
    QHash<ChannelID, QColor> m_new_colors;

    QStringList importLocations;
    QStringListModel *importModel;

    MySortFilterProxyModel * chanFilterModel;
    QStandardItemModel *chanModel;

    MySortFilterProxyModel * waveFilterModel;
    QStandardItemModel *waveModel;

    ChannelTab m_eventsTab;            //!< the Events tab (chanView)
    ChannelTab m_wavesTab;             //!< the Waveforms tab (waveView)
    QIcon m_customizedDot;             //!< decoration shown on customized channel rows
    bool m_updatingMarkers = false;    //!< set while this dialog itself edits rows, to ignore the resulting itemChanged
};


#endif // PREFERENCESDIALOG_H
