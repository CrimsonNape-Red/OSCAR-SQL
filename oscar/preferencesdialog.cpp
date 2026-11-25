/* OSCAR Preferences Dialog Implementation
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 * Copyright (c) 2011-2018 Mark Watkins
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#define TEST_MACROS_ENABLEDoff
#include <test_macros.h>

#include <QLabel>
#include <QColorDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QProcess>
#include <QDesktopServices>
#include <QFileDialog>
#include <QTextStream>
#include <QCalendarWidget>
#include <QMenu>
#include <QPainter>
#include <QScopedValueRollback>
#include <QTextDocument>
#include <QTimer>
//#include <QMenuBar>
#include <QDebug>

#include <cmath>

#include "preferencesdialog.h"
#include "treecheckdelegate.h"
#include "tooltipcolors.h"
#include "colorscheme.h"
#include "version.h"

#include <Graphs/gGraphView.h>
#include <mainwindow.h>
#include "ui_preferencesdialog.h"
#include "SleepLib/machine_common.h"
#include "SleepLib/common.h"

#include "daily.h"

extern QFont *defaultfont;
extern QFont *mediumfont;
extern QFont *bigfont;
extern MainWindow *mainwin;

typedef QMessageBox::StandardButton StandardButton;
typedef QMessageBox::StandardButtons StandardButtons;

QHash<schema::ChanType, QString> channeltype;

namespace {
//! Role holding a cell's own tooltip, so the "Default:" line can be added and removed again.
constexpr int kBaseToolTipRole = Qt::UserRole + 2;

/*! \brief A small filled dot in the widget's text colour, marking a customized channel row.
    Drawn in code with the palette so it reads in light and dark mode. */
QIcon makeCustomizedDot(const QWidget *widget)
{
    const int side = 16;      // the item views' default decoration size
    const qreal diameter = 7;
    const qreal dpr = widget->devicePixelRatioF();
    QPixmap pm(QSize(side, side) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(widget->palette().color(QPalette::Text));
    p.drawEllipse(QRectF((side - diameter) / 2, (side - diameter) / 2, diameter, diameter));
    p.end();
    return QIcon(pm);
}

//! Threshold text as the Waveforms tab shows it.
QString thresholdText(double value)
{
    return QString::number(value, 'f', 1);
}

// Column layouts of the two channel tabs (see InitChanInfo() and InitWaveInfo()).
constexpr int kEventsColor = 1, kEventsOverview = 2, kEventsLabel = 4, kEventsDetails = 5;
constexpr int kWavesColor = 1, kWavesOverview = 2, kWavesLower = 3, kWavesUpper = 4,
              kWavesLabel = 5, kWavesDetails = 6;
} // namespace

QString PreferencesDialog::clinicalHelp() {
    QStringList str; str
            <<tr("Clinical Mode:")
           <<tr("Reports what is on the data card, all of it including any and all data deselected in the Permissive mode.")
          <<tr("Basically replicates the reports and data stored on the devices data card.")
         <<tr("This includes pap devices, oximeters, etc. Compliance reports fall under this mode.")
        <<tr("Compliance reports always include all data within the chosen Compliance period, even if otherwise deselected.")
       <<""
      <<tr("Permissive Mode:")
     <<tr("Allows user to select which data sets/ sessions to be used for calculations and display.")
    <<tr("Additional charts and calculations may be available that are not available from the vendor data.")
    <<tr("Enables Custom UserFlags displayed in the statistics Therapy Efficacy section")
    <<tr("Enables Steady Breathing Event Flags and Waveform (see CPAP tab)")
    <<"";
    return str.join("\n");
}

PreferencesDialog::PreferencesDialog(QWidget *parent, Profile *_profile) :
    QDialog(parent, Qt::WindowTitleHint | Qt::WindowCloseButtonHint),
    ui(new Ui::PreferencesDialog),
    profile(_profile)
{
    ui->setupUi(this);

    channeltype.clear();
    channeltype[schema::FLAG] = tr("Flag");
    channeltype[schema::MINOR_FLAG] = tr("Minor Flag");
    channeltype[schema::SPAN] = tr("Span");
    channeltype[schema::UNKNOWN] = tr("Always Minor");
    bool haveResMed = false;
    QList<Machine *> machines = profile->GetMachines(MT_CPAP);
    if ( machines.size() > 0 ) {
        for (QList<Machine *>::iterator it = machines.begin(); it != machines.end(); ++it) {
            const QString & mclass=(*it)->loaderName();
            if (mclass == STR_MACH_ResMed) {
                haveResMed = true;
                break;
            }
        }
    } else {
        if (QMessageBox::question(this, tr("No CPAP devices detected"),
                                  tr("Will you be using a ResMed brand device?"),
                                  QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes )
            haveResMed = true;
    }

#ifdef LOCK_RESMED_SESSIONS
    // Remove access to session splitting options and show ResMed users a notice instead
    ui->ResMedWarning->setText(tr("<p><b>Please Note:</b> OSCAR's advanced session splitting capabilities are not possible with <b>ResMed</b> devices due to a limitation in the way their settings and summary data is stored, and therefore they have been disabled for this profile.</p><p>On ResMed devices, days will <b>split at noon</b> like in ResMed's commercial software.</p>"));
    ui->ResMedWarning->setVisible(haveResMed);

    if (haveResMed) {
        profile->forceResmedPrefs();
        ui->sessionSplitWidget->setVisible(!haveResMed);
        ui->prefCalcMax->setEnabled(false);
        ui->prefCalcMiddle->setEnabled(false);
        ui->prefCalcPercentile->setEnabled(false);
        ui->showUnknownFlags->setEnabled(false);
        ui->calculateUnintentionalLeaks->setEnabled(false);
        ui->createSDBackups->setChecked(true);
        ui->createSDBackups->setEnabled(false);

    }
    ui->resmedPrefCalcsNotice->setVisible(haveResMed);
#endif

    int gfxEngine=(int)currentGFXEngine();

    int selIdx = 0;
    for (int j=0, i=0; i<=(int)MaxGFXEngine; ++i) {
        if (gfxEgnineIsSupported((GFXEngine) i)) {
            ui->gfxEngineCombo->addItem(GFXEngineNames[i], i);
            if (i==gfxEngine) {
                selIdx = j;
            }
            ++j;
        }
    }
    ui->gfxEngineCombo->setCurrentIndex(selIdx);


    ui->culminativeIndices->setEnabled(false);

    QLocale locale = QLocale::system();
    QString shortformat = locale.dateFormat(QLocale::ShortFormat);

    if (!shortformat.toLower().contains("yyyy")) {
        shortformat.replace("yy", "yyyy");
    }



    if (profile == nullptr) {
        qCritical() << "Preferences dialog created without legit profile object";
        return;
    }
    ui->tabWidget->setCurrentIndex(0);


    ui->showLeakRedline->setChecked(profile->cpap->showLeakRedline());
    ui->leakRedlineSpinbox->setValue(profile->cpap->leakRedline());

    //change initialization from hard coded in schema to profile data.
    ui->oxiDesaturationThreshold->setValue(profile->oxi->oxiDesaturationThreshold());
    ui->flagPulseAbove->setValue(profile->oxi->flagPulseAbove());
    ui->flagPulseBelow->setValue(profile->oxi->flagPulseBelow());

    ui->spo2Drop->setValue(profile->oxi->spO2DropPercentage());
    ui->spo2DropDuration->setValue(profile->oxi->spO2DropDuration());
    ui->pulseChange->setValue(profile->oxi->pulseChangeBPM());
    ui->pulseChangeDuration->setValue(profile->oxi->pulseChangeDuration());
    ui->oxiDiscardThreshold->setValue(profile->oxi->oxiDiscardThreshold());

    ui->eventIndexCombo->setCurrentIndex(profile->general->calculateRDI() ? 1 : 0);

    ui->automaticImport->setChecked(profile->cpap->autoImport()); // Skip extra dialogs, this needs to rename.
    ui->warnOnDifferentSDCard->setChecked(profile->cpap->warnOnDifferentSDCard());

    ui->autoLoadLastUsed->setChecked(AppSetting->autoOpenLastUsed());

    ui->timeEdit->setTime(profile->session->daySplitTime());
    ui->splitNonCPAPSessions->setChecked(profile->session->splitNonCPAPSessions());
    int val = profile->session->combineCloseSessions();
    ui->combineSlider->setValue(val);

    ui->openingTabCombo->setCurrentIndex(AppSetting->openTabAtStart());
    ui->importTabCombo->setCurrentIndex(AppSetting->openTabAfterImport());


    if (val > 0) {
        ui->combineLCD->display(val);
    } else { ui->combineLCD->display(STR_TR_Off); }

    val = profile->session->ignoreShortSessions();
    ui->IgnoreSlider->setValue(val);

    if (val > 0) {
        ui->IgnoreLCD->display(val);
    } else { ui->IgnoreLCD->display(STR_TR_Off); }

    ui->LockSummarySessionSplitting->setChecked(profile->session->lockSummarySessions());
    ui->warnOnUntestedMachine->setChecked(profile->session->warnOnUntestedMachine());
    ui->warnOnUnexpectedData->setChecked(profile->session->warnOnUnexpectedData());

    // macOS default system fonts are not in QFontCombobox because they are "private":
    // See https://github.com/musescore/MuseScore/commit/0eecb165664a0196c2eee12e42fb273dcfc9c637
    // The below makes sure any default system font is present in QFontComboBox.
    QString systemFontFamily = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
    if (-1 == ui->applicationFont->findText(systemFontFamily)) {
        ui->applicationFont->addItem(systemFontFamily);
    }
    // If the current font is the system font, setCurrentFont() won't work as intended,
    // so select the font by searching for its name, which will always work.
    ui->applicationFont->setCurrentIndex(ui->applicationFont->findText(QApplication::font().family()));
    ui->applicationFontSize->setValue(QApplication::font().pointSize());
    ui->applicationFontBold->setChecked(QApplication::font().weight() == QFont::Bold);
    ui->applicationFontItalic->setChecked(QApplication::font().italic());

    ui->graphFont->setCurrentFont(*defaultfont);
    ui->graphFontSize->setValue(defaultfont->pointSize());
    ui->graphFontBold->setChecked(defaultfont->weight() == QFont::Bold);
    ui->graphFontItalic->setChecked(defaultfont->italic());

    ui->titleFont->setCurrentFont(*mediumfont);
    ui->titleFontSize->setValue(mediumfont->pointSize());
    ui->titleFontBold->setChecked(mediumfont->weight() == QFont::Bold);
    ui->titleFontItalic->setChecked(mediumfont->italic());

    ui->bigFont->setCurrentFont(*bigfont);
    ui->bigFontSize->setValue(bigfont->pointSize());
    ui->bigFontBold->setChecked(bigfont->weight() == QFont::Bold);
    ui->bigFontItalic->setChecked(bigfont->italic());

    ui->lineThicknessSlider->setValue(AppSetting->lineThickness()*2.0);
    ui->gridLineOpacitySlider->setValue(AppSetting->gridLineOpacity());
    on_gridLineOpacitySlider_valueChanged(AppSetting->gridLineOpacity());

    ui->resyncMachineDetectedEvents->setChecked(profile->cpap->resyncFromUserFlagging());

    ui->useAntiAliasing->setChecked(AppSetting->antiAliasing());
    ui->usePixmapCaching->setChecked(AppSetting->usePixmapCaching());
    ui->useSquareWavePlots->setChecked(AppSetting->squareWavePlots());
    ui->graphTooltips->setChecked(AppSetting->graphTooltips());
    ui->allowYAxisScaling->setChecked(AppSetting->allowYAxisScaling());
    ui->combineSimilarMachines->setChecked(AppSetting->combineSimilarMachines());
    ui->includeSerial->setChecked(AppSetting->includeSerial());
    ui->includeSerial->setEnabled(!AppSetting->combineSimilarMachines());
    ui->monochromePrinting->setChecked(AppSetting->monochromePrinting());
    ui->eventFlagSessionBar->setChecked(profile->appearance->eventFlagSessionBar());
    ui->disableDailyGraphTitles->setChecked(AppSetting->disableDailyGraphTitles());
    ui->useFusionTheme->setChecked(AppSetting->useFusionTheme());
    ui->colorScheme->setCurrentIndex(int(AppSetting->colorScheme()));
    ui->colorScheme->setToolTip(ColorScheme::canSwitchLive()
        ? tr("Follow system: light or dark as set in your operating system. Light or Dark: always that.\n"
             "The moon or sun button at the right end of the tabs also switches between Light and Dark.")
        : tr("Follow system: light or dark as set in your operating system. Light or Dark: always that.\n"
             "OSCAR will restart when you click OK."));
    ui->useNewPages->setChecked(AppSetting->useNewPages());
    ui->welcomeCardColour->setCurrentIndex(int(AppSetting->welcomeCardColour()));
    ui->welcomeCardColour->setEnabled(AppSetting->useNewPages());
    ui->welcomeCardColourLabel->setEnabled(AppSetting->useNewPages());
    connect(ui->useNewPages, &QCheckBox::toggled, ui->welcomeCardColour, &QWidget::setEnabled);
    connect(ui->useNewPages, &QCheckBox::toggled, ui->welcomeCardColourLabel, &QWidget::setEnabled);
    ui->useSystemTooltipColors->setChecked(AppSetting->useSystemTooltipColors());

#if defined(STEADY_BREATHING)
    // The user's stored choice; steadyBreathing() reports SB_INACTIVE (not SB_OFF) in Clinical Mode
    m_steadyBreathingChoice = profile->cpap->steadyBreathing()!=SB_OFF;
    m_steadyBreathingTitle = ui->steadyBreathing->title();
    ui->steadyBreathing->setChecked(m_steadyBreathingChoice);
    updateSteadyBreathingForMode(profile->cpap->clinicalMode());
    // Unlock or lock the Steady Breathing box as soon as the operating mode is picked on the
    // Clinical tab, so the user doesn't have to close Preferences and reopen it first.
    connect(ui->clinicalMode, &QRadioButton::toggled,
            this, &PreferencesDialog::updateSteadyBreathingForMode);
#if defined(STEADY_BREATHING_ENHANCED_TESTING)
    // Ui MUST BE MANUALLY ENABLED. was commented out
    ui->steadyBreathingDuration->setValue( profile->cpap->steadyBreathingDuration());
    ui->steadyBreathingThreshold->setValue(profile->cpap->steadyBreathingThreshold());
    ui->steadyBreathingDuration->show();
    ui->steadyBreathingThreshold->show();
    ui->resetSteadyBreathingDefaults->show();
#endif
#endif

    ui->complianceHours->setValue(profile->cpap->complianceHours());
    ui->clinicalMode->setChecked(profile->cpap->clinicalMode());
    ui->clinicalTextEdit->setPlainText(clinicalHelp());
    // clinicalMode and permissiveMode are radio buttons and must be set to opposite values. Once clinicalMode is used.
    // Radio Buttons illustrate the operating mode.
    ui->permissiveMode->setChecked(!profile->cpap->clinicalMode());

    ui->alternatingColorsCombo->setCurrentIndex(AppSetting->alternatingColorsCombo());
    ui->autoLaunchImporter->setChecked(AppSetting->autoLaunchImport());
#ifndef NO_CHECKUPDATES
    ui->updateCheckEvery->setValue(AppSetting->updateCheckFrequency());
    ui->allowEarlyUpdates->setChecked(AppSetting->allowEarlyUpdates());
    ui->test_invite->setVisible(false);
    if (!getVersion().IsReleaseVersion()) {
        // Test version
        ui->automaticallyCheckUpdates->setVisible(false);
        ui->allowEarlyUpdates->setVisible(false);
        ui->updateCheckEvery->setMaximum(7);
    }
    else {
        // Release version
        ui->updateCheckEvery->setMaximum(90);
        ui->always_look_for_updates->setVisible(false);
        if (!AppSetting->allowEarlyUpdates()) {
            ui->test_invite->setVisible(true);
            ui->allowEarlyUpdates->setVisible(false);
        }
    }
#else
    ui->automaticallyCheckUpdates_GroupBox->setVisible(false);
#endif

    ui->skipEmptyDays->setChecked(profile->general->skipEmptyDays());
    ui->showUnknownFlags->setChecked(profile->general->showUnknownFlags());
    ui->enableMultithreading->setVisible(false);
    ui->removeCardNotificationCheckbox->setChecked(AppSetting->removeCardReminder());
    ui->notifyMessageBoxCheckbox->setChecked(AppSetting->notifyMessagBoxOption());
    ui->dontAskWhenSavingScreenshotsCheckbox->setChecked(AppSetting->dontAskWhenSavingScreenshots());
    ui->showDatabaseMenuCheckbox->setChecked(AppSetting->showDatabaseMenu());
    ui->cacheSessionData->setChecked(AppSetting->cacheSessions());
    ui->preloadSummaries->setChecked(profile->session->preloadSummaries());
    ui->animationsAndTransitionsCheckbox->setChecked(AppSetting->animations());

    ui->prefCalcMiddle->setCurrentIndex(profile->general->prefCalcMiddle());
    ui->prefCalcMax->setCurrentIndex(profile->general->prefCalcMax());
    float f = profile->general->prefCalcPercentile();
    ui->prefCalcPercentile->setValue(f);

    ui->tooltipTimeoutSlider->setValue(AppSetting->tooltipTimeout());
    on_tooltipTimeoutSlider_valueChanged(ui->tooltipTimeoutSlider->value());

    ui->scrollDampeningSlider->setValue(AppSetting->scrollDampening() / 10);
    on_scrollDampeningSlider_valueChanged(ui->scrollDampeningSlider->value());

    bool bcd = profile->session->backupCardData();
    ui->createSDBackups->setChecked(bcd);
    ui->compressSDBackups->setEnabled(bcd);
    ui->compressSDBackups->setChecked(profile->session->compressBackupData());
    ui->compressSessionData->setChecked(profile->session->compressSessionData());
    ui->ignoreOlderSessionsCheck->setChecked(profile->session->ignoreOlderSessions());
    ui->ignoreOlderSessionsDate->setDate(profile->session->ignoreOlderSessionsDate().date());

    ui->graphHeight->setValue(AppSetting->graphHeight());

#ifndef NO_CHECKUPDATES
    ui->automaticallyCheckUpdates->setChecked(AppSetting->updatesAutoCheck());
    ui->updateCheckEvery->setValue(AppSetting->updateCheckFrequency());
    if (AppSetting->updatesLastChecked().isValid()) {
        RefreshLastChecked();
    } else {
        ui->updateLastChecked->setText(tr("Never"));
    }
#endif

    ui->overlayFlagsCombo->setCurrentIndex(AppSetting->overlayType());
#ifndef REMOVE_FITNESS
    ui->overviewLinecharts->setCurrentIndex(AppSetting->overviewLinechartMode());
#else
    ui->overviewLinecharts->hide();
    ui->overviewLinechartsLabel->hide();
#endif

    ui->ahiGraphWindowSize->setEnabled(false);
    ui->ahiGraphWindowSize->setValue(profile->cpap->AHIWindow());
    ui->ahiGraphZeroReset->setChecked(profile->cpap->AHIReset());

    ui->customEventGroupbox->setChecked(profile->cpap->userEventFlagging());
    ui->apneaDuration->setValue(profile->cpap->userEventDuration());
    ui->apneaFlowRestriction->setValue(profile->cpap->userEventRestriction());
    ui->apneaDuration2->setValue(profile->cpap->userEventDuration2());
    ui->apneaFlowRestriction2->setValue(profile->cpap->userEventRestriction2());
    ui->userEventDuplicates->setChecked(profile->cpap->userEventDuplicates());
    ui->userEventDuplicates->setVisible(false);
    // Resyncing only happens while device events suppress user flags, so it does nothing
    // when duplicates are allowed. (Turning off the group box disables it as well.)
    ui->resyncMachineDetectedEvents->setEnabled(!ui->userEventDuplicates->isChecked());
    connect(ui->userEventDuplicates, &QCheckBox::toggled, this, [this](bool checked) {
        ui->resyncMachineDetectedEvents->setEnabled(!checked);
    });

    ui->showUserFlagsInPie->setChecked(AppSetting->userEventPieChart());

    bool b;
    ui->calculateUnintentionalLeaks->setChecked(b=profile->cpap->calculateUnintentionalLeaks());

    ui->maskLeaks4Slider->setValue(profile->cpap->custom4cmH2OLeaks()*10.0);
    ui->maskLeaks20Slider->setValue(profile->cpap->custom20cmH2OLeaks()*10.0);

    ui->maskLeaks4Label->setText(QString("%1 %2").arg(profile->cpap->custom4cmH2OLeaks(), 5, 'f', 1).arg(STR_UNIT_LPM));
    ui->maskLeaks20Label->setText(QString("%1 %2").arg(profile->cpap->custom20cmH2OLeaks(), 5, 'f', 1).arg(STR_UNIT_LPM));

    ui->eventPostcontext->setValue(profile->cpap->eventPostcontext());
    ui->consolidateEvents->setChecked(profile->cpap->consolidateEvents());
    
    /*    QLocale locale=QLocale::system();
        QString shortformat=locale.dateFormat(QLocale::ShortFormat);
        if (!shortformat.toLower().contains("yyyy")) {
            shortformat.replace("yy","yyyy");
        }*/

    m_customizedDot = makeCustomizedDot(ui->chanView);

    chanFilterModel = new MySortFilterProxyModel(this);
    chanModel = new QStandardItemModel(this);
    chanFilterModel->setSourceModel(chanModel);
    chanFilterModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    chanFilterModel->setFilterKeyColumn(0);
    ui->chanView->setModel(chanFilterModel);

    m_eventsTab = { chanModel, chanFilterModel, ui->chanView, ui->resetOneChannel,
                    { kEventsColor, kEventsOverview, kEventsLabel, kEventsDetails, -1, -1 } };
    setupChannelTab(m_eventsTab);

    InitChanInfo();

    waveFilterModel = new MySortFilterProxyModel(this);
    waveModel = new QStandardItemModel(this);
    waveFilterModel->setSourceModel(waveModel);
    waveFilterModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    waveFilterModel->setFilterKeyColumn(0);
    ui->waveView->setModel(waveFilterModel);

    m_wavesTab = { waveModel, waveFilterModel, ui->waveView, ui->resetOneWaveform,
                   { kWavesColor, kWavesOverview, kWavesLabel, kWavesDetails, kWavesLower, kWavesUpper } };
    setupChannelTab(m_wavesTab);

    InitWaveInfo();

    ui->waveView->setSortingEnabled(true);
    ui->chanView->setSortingEnabled(true);

    ui->waveView->sortByColumn(0, Qt::AscendingOrder);
    ui->chanView->sortByColumn(0, Qt::AscendingOrder);

    // Draw the Name (enabled) and Overview checkboxes ourselves: on macOS 27 the
    // platform style leaves item-view check indicators invisible (#318).
    TreeCheckDelegate *checkDelegate = new TreeCheckDelegate(this);
    for (QTreeView *view : {ui->chanView, ui->waveView}) {
        view->setItemDelegateForColumn(0, checkDelegate);
        view->setItemDelegateForColumn(2, checkDelegate);
    }

    //added by Sheila 1/5/2026 for marks1
    ui->spo2BaselineMethod->addItem(tr("Rolling (recommended)"), SBM_Rolling);
    ui->spo2BaselineMethod->addItem(tr("Whole night"), SBM_WholeNight);
    ui->spo2BaselineMethod->addItem(tr("First hour"), SBM_FirstHour);
    ui->spo2BaselineMethod->addItem(tr("Manual"), SBM_Manual);
    // Add 99 down to 85
    for (int i = 99; i >= 85; --i) {
        ui->baseSpO2Option->addItem(QString("%1%").arg(i), i);
    }
    connect(ui->spo2BaselineMethod, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PreferencesDialog::updateSpO2BaselineControls);
    showSpO2Baseline(profile->oxi->spO2BaselineMethod(), profile->oxi->spO2BaselineWindow(),
                     profile->oxi->spO2BaselinePercentile(), profile->oxi->spO2ManualBaseline());




}

#include <QItemDelegate>
class SpinBoxDelegate : public QItemDelegate
{

public:
    SpinBoxDelegate(QObject *parent = 0):QItemDelegate(parent) {}

    virtual QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const;

    virtual void setEditorData(QWidget *editor, const QModelIndex &index) const;
    virtual void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const;
    virtual void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const;
};

QWidget *SpinBoxDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &/* option */, const QModelIndex &/* index */) const
{
    QDoubleSpinBox *editor = new QDoubleSpinBox(parent);

    return editor;
}

void SpinBoxDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    double value = index.model()->data(index, Qt::EditRole).toDouble();

    QDoubleSpinBox *spinBox = static_cast<QDoubleSpinBox*>(editor);
    spinBox->setMinimum(-9999999.0);
    spinBox->setMaximum(9999999.0);

    spinBox->setValue(value);
}

void SpinBoxDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
    QDoubleSpinBox *spinBox = static_cast<QDoubleSpinBox*>(editor);
    spinBox->interpretText();
    double value = spinBox->value();

    model->setData(index, value, Qt::EditRole);
}
void SpinBoxDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &/* index */) const
{
    editor->setGeometry(option.rect);
}


#include <QItemDelegate>
class ComboBoxDelegate : public QItemDelegate
{

public:
    ComboBoxDelegate(QObject *parent = 0):QItemDelegate(parent) {}

    virtual QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const;

    virtual void setEditorData(QWidget *editor, const QModelIndex &index) const;
    virtual void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const;
    virtual void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const;
};

QWidget *ComboBoxDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &/* option */, const QModelIndex &/* index */) const
{
    QComboBox *combo = new QComboBox(parent);

    QHash<schema::ChanType, QString>::iterator it;
    for (it = channeltype.begin(); it != channeltype.end(); ++it) {
        if (it.key() == schema::UNKNOWN) continue;
        combo->addItem(it.value());
    }

    return combo;
}

void ComboBoxDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    QString value = index.model()->data(index, Qt::EditRole).toString();

    QComboBox *combo = static_cast<QComboBox*>(editor);

    combo->setCurrentText(value);
}

void ComboBoxDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
    QComboBox *combo = static_cast<QComboBox*>(editor);

    model->setData(index, combo->currentText(), Qt::EditRole);
}
void ComboBoxDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &/* index */) const
{
    editor->setGeometry(option.rect);
}


void PreferencesDialog::InitChanInfo()
{
    QHash<MachineType, int> toprows;

    chanModel->clear();
    toplevel.clear();
    toprows.clear();
    QStringList headers;
    headers.append(tr("Name"));
    headers.append(tr("Color"));
    headers.append(tr("Overview"));
    headers.append(tr("Flag Type"));
    headers.append(tr("Label"));
    headers.append(tr("Details"));
    chanModel->setHorizontalHeaderLabels(headers);
    ui->chanView->setColumnWidth(0, 200);
    ui->chanView->setColumnWidth(1, 40);
    ui->chanView->setColumnWidth(2, 60);
    ui->chanView->setColumnWidth(3, 100);
    ui->chanView->setColumnWidth(4, 100);
    ui->chanView->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->chanView->setSelectionBehavior(QAbstractItemView::SelectItems);

    chanModel->setColumnCount(6);

    QStandardItem *hdr = nullptr;

    QMap<MachineType, QString> Section;

    Section[MT_CPAP] = tr("CPAP Events");
    Section[MT_OXIMETER] = tr("Oximeter Events");
    Section[MT_POSITION] = tr("Positional Events");
    Section[MT_SLEEPSTAGE] = tr("Sleep Stage Events");
    Section[MT_UNCATEGORIZED] = tr("Unknown Events");

    QMap<MachineType, QString>::iterator it;

    QHash<QString, schema::Channel *>::iterator ci;

    for (it = Section.begin(); it != Section.end(); ++it) {
        toplevel[it.key()] = hdr = new QStandardItem(it.value());
        hdr->setEditable(false);
        QList<QStandardItem *> list;
        list.append(hdr);
        for (int i=0; i<5; i++) {
            QStandardItem *it = new QStandardItem();
            it->setEnabled(false);
            list.append(it);
        }
        chanModel->appendRow(list);
    }

    ui->chanView->setAlternatingRowColors(true);

    // ui->graphView->setFirstColumnSpanned(0,daily->index(),true); // Crashes on windows.. Why do I need this again?


    ComboBoxDelegate * combobox = new ComboBoxDelegate(ui->waveView);

    ui->chanView->setItemDelegateForColumn(3,combobox);

    int row = 0;
    for (ci = schema::channel.names.begin(); ci != schema::channel.names.end(); ci++) {
        schema::Channel * chan = ci.value();
        if ((chan->type() == schema::DATA) || (chan->type() == schema::SETTING) || chan->type() == schema::WAVEFORM) continue;

        QList<QStandardItem *> items;
        QStandardItem *it = new QStandardItem(chan->fullname());
        it->setCheckable(true);
        it->setCheckState(chan->enabled() ? Qt::Checked : Qt::Unchecked);
        it->setEditable(true);
        it->setData(chan->id(), Qt::UserRole);

        // Dear translators: %1 is a unique ascii english string used to indentify channels in the code, I'd like feedback on how this goes..
        // It's here in case users mess up which field is which.. it will always show the Channel Code underneath in the tooltip.
        it->setToolTip(tr("Double click to change the descriptive name the '%1' channel.").arg(chan->code()));
        items.push_back(it);


        it = new QStandardItem();
        it->setBackground(QBrush(chan->defaultColor()));
        it->setEditable(false);
        it->setData(chan->defaultColor().rgba(), Qt::UserRole);
        it->setToolTip(tr("Double click to change the default color for this channel plot/flag/data."));
        it->setSelectable(false);
        items.push_back(it);

        it = new QStandardItem(QString());
        it->setToolTip(tr("Whether this flag has a dedicated overview chart."));
        it->setCheckable(true);
        it->setCheckState(chan->showInOverview() ? Qt::Checked : Qt::Unchecked);
        it->setTextAlignment(Qt::AlignCenter);
        it->setData(chan->id(), Qt::UserRole);
        items.push_back(it);

        schema::ChanType type = chan->type();

        it = new QStandardItem(channeltype[type]);
        it->setToolTip(tr("Here you can change the type of flag shown for this event"));
        it->setEditable(type != schema::UNKNOWN);
        items.push_back(it);

        it = new QStandardItem(chan->label());
        it->setToolTip(tr("This is the short-form label to indicate this channel on screen."));

        it->setEditable(true);
        items.push_back(it);

        it = new QStandardItem(chan->description());
        it->setToolTip(tr("This is a description of what this channel does."));

        it->setEditable(true);
        items.push_back(it);

        MachineType mt = chan->machtype();
        if (chan->type() == schema::UNKNOWN) mt = MT_UNCATEGORIZED;
        row = toprows[mt]++;
        toplevel[mt]->insertRow(row, items);
    }


    for(QHash<MachineType, QStandardItem *>::iterator i = toplevel.begin(); i != toplevel.end(); ++i) {
        if (i.value()->rowCount() == 0) {
            chanModel->removeRow(i.value()->row());
        }
    }

    updateAllMarkers(m_eventsTab);

    ui->chanView->expandAll();
    ui->chanView->setSortingEnabled(true);
}

void PreferencesDialog::InitWaveInfo()
{
    QHash<MachineType, int> toprows;

    waveModel->clear();
    machlevel.clear();
    toprows.clear();
    QStringList headers;
    headers.append(tr("Name"));
    headers.append(tr("Color"));
    headers.append(tr("Overview"));
    headers.append(tr("Lower"));
    headers.append(tr("Upper"));
    headers.append(tr("Label"));
    headers.append(tr("Details"));
    waveModel->setHorizontalHeaderLabels(headers);
    ui->waveView->setColumnWidth(0, 200);
    ui->waveView->setColumnWidth(1, 40);
    ui->waveView->setColumnWidth(2, 60);
    ui->waveView->setColumnWidth(3, 50);
    ui->waveView->setColumnWidth(4, 50);
    ui->waveView->setColumnWidth(5, 100);
    ui->waveView->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->waveView->setSelectionBehavior(QAbstractItemView::SelectItems);

    waveModel->setColumnCount(7);

    QStandardItem *hdr = nullptr;

    QMap<MachineType, QString> Section;

    Section[MT_CPAP] = tr("CPAP Waveforms");
    Section[MT_OXIMETER] = tr("Oximeter Waveforms");
    Section[MT_POSITION] = tr("Positional Waveforms");
    Section[MT_SLEEPSTAGE] = tr("Sleep Stage Waveforms");

    QMap<MachineType, QString>::iterator it;

    for (it = Section.begin(); it != Section.end(); ++it) {
        machlevel[it.key()] = hdr = new QStandardItem(it.value());
        hdr->setEditable(false);
        QList<QStandardItem *> list;
        list.append(hdr);
        for (int i=0; i<6; i++) {
            QStandardItem *it = new QStandardItem();
            it->setEnabled(false);
            list.append(it);
        }
        waveModel->appendRow(list);
    }

    ui->waveView->setAlternatingRowColors(true);

    // ui->graphView->setFirstColumnSpanned(0,daily->index(),true); // Crashes on windows.. Why do I need this again?

    QHash<QString, schema::Channel *>::iterator ci;

    SpinBoxDelegate * spinbox = new SpinBoxDelegate(ui->waveView);

    ui->waveView->setItemDelegateForColumn(3,spinbox);
    ui->waveView->setItemDelegateForColumn(4,spinbox);

    int row = 0;
    for (ci = schema::channel.names.begin(); ci != schema::channel.names.end(); ci++) {
        schema::Channel * chan = ci.value();
        if (chan->type() != schema::WAVEFORM) continue;

        QList<QStandardItem *> items;
        QStandardItem *it = new QStandardItem(chan->fullname());

        it->setCheckable(true);
        it->setCheckState(chan->enabled() ? Qt::Checked : Qt::Unchecked);
        it->setEditable(true);
        it->setData(chan->id(), Qt::UserRole);
        it->setToolTip(tr("Double click to change the descriptive name this channel."));
        items.push_back(it);

        it = new QStandardItem();
        it->setBackground(QBrush(chan->defaultColor()));
        it->setEditable(false);
        it->setData(chan->defaultColor().rgba(), Qt::UserRole);
        it->setToolTip(tr("Double click to change the default color for this channel plot/flag/data."));
        it->setSelectable(false);
        items.push_back(it);

        it = new QStandardItem();
        it->setCheckable(true);
        it->setCheckState(chan->showInOverview() ? Qt::Checked : Qt::Unchecked);
        it->setEditable(true);
        it->setData(chan->id(), Qt::UserRole);
        it->setToolTip(tr("Whether a breakdown of this waveform displays in overview."));
        items.push_back(it);


        it = new QStandardItem(QString::number(chan->lowerThreshold(),'f',1));
        it->setToolTip(tr("Here you can set the <b>lower</b> threshold used for certain calculations on the %1 waveform").arg(chan->fullname()));
        it->setEditable(true);
        items.push_back(it);

        it = new QStandardItem(QString::number(chan->upperThreshold(),'f',1));
        it->setToolTip(tr("Here you can set the <b>upper</b> threshold used for certain calculations on the %1 waveform").arg(chan->fullname()));
        it->setEditable(true);
        items.push_back(it);

        it = new QStandardItem(chan->label());
        it->setToolTip(tr("This is the short-form label to indicate this channel on screen."));

        it->setEditable(true);
        items.push_back(it);

        it = new QStandardItem(chan->description());
        it->setToolTip(tr("This is a description of what this channel does."));

        it->setEditable(true);
        items.push_back(it);

        row = toprows[chan->machtype()]++;
        machlevel[chan->machtype()]->insertRow(row, items);
    }

    for(QHash<MachineType, QStandardItem *>::iterator i = machlevel.begin(); i != machlevel.end(); ++i) {
        if (i.value()->rowCount() == 0) {
            waveModel->removeRow(i.value()->row());
        }
    }

    updateAllMarkers(m_wavesTab);

    ui->waveView->expandAll();
    ui->waveView->setSortingEnabled(true);
}


PreferencesDialog::~PreferencesDialog()
{
    delete ui;
}

bool PreferencesDialog::Save()
{
    bool recompress_events = false;
    bool recalc_events = false;
    bool needs_restart = false;
    bool needs_reload = false;

    if (ui->ahiGraphZeroReset->isChecked() != profile->cpap->AHIReset()) { recalc_events = true; }

    if (ui->useSquareWavePlots->isChecked() != AppSetting->squareWavePlots()) {
        needs_reload  = true;
    }

    if ((profile->session->daySplitTime() != ui->timeEdit->time()) ||
            (profile->session->combineCloseSessions() != ui->combineSlider->value()) ||
            (profile->session->ignoreShortSessions() != ui->IgnoreSlider->value())) {
        needs_reload = true;
    }

    if (profile->session->lockSummarySessions() != ui->LockSummarySessionSplitting->isChecked()) {
        needs_reload = true;
    }

    if (AppSetting->userEventPieChart() != ui->showUserFlagsInPie->isChecked()) {
        // lazy.. fix me
        needs_reload = true;
    }

    if ((GFXEngine)ui->gfxEngineCombo->currentData().toUInt() != currentGFXEngine()) {
        setCurrentGFXEngine((GFXEngine)ui->gfxEngineCombo->currentData().toUInt());
        needs_restart = true;
    }

    int rdi_set = profile->general->calculateRDI() ? 1 : 0;
    if (rdi_set != ui->eventIndexCombo->currentIndex()) {
        needs_reload  = true;
    }

    if ((profile->general->prefCalcMiddle() != ui->prefCalcMiddle->currentIndex())
            || (profile->general->prefCalcMax() != ui->prefCalcMax->currentIndex())
            || (profile->general->prefCalcPercentile() != ui->prefCalcPercentile->value())) {
        needs_reload = true;
    }

    if (profile->cpap->leakRedline() != ui->leakRedlineSpinbox->value()) {
        needs_reload = true;
    }


    if (profile->cpap->userEventFlagging() &&
            (profile->cpap->userEventDuration() != ui->apneaDuration->value() ||
             profile->cpap->userEventDuration2() != ui->apneaDuration2->value() ||
             profile->cpap->userEventDuplicates() != ui->userEventDuplicates->isChecked() ||
             profile->cpap->userEventRestriction2() != ui->apneaFlowRestriction2->value() ||
             profile->cpap->userEventRestriction() != ui->apneaFlowRestriction->value())) {
        recalc_events = true;
    }

    // Restart if turning user event flagging on/off
    if (profile->cpap->userEventFlagging() != ui->customEventGroupbox->isChecked()) {
        // Don't bother recalculating, just switch off
        needs_reload = true;
        //} else
        recalc_events = true;
    }

    // Resync only has an effect while Custom User Flagging is on and duplicates aren't allowed.
    bool resync_was_active = profile->cpap->userEventFlagging() && !profile->cpap->userEventDuplicates()
            && profile->cpap->resyncFromUserFlagging();
    bool resync_now_active = ui->customEventGroupbox->isChecked() && !ui->userEventDuplicates->isChecked()
            && ui->resyncMachineDetectedEvents->isChecked();
    if (ui->customEventGroupbox->isChecked()
            && (profile->cpap->resyncFromUserFlagging() != ui->resyncMachineDetectedEvents->isChecked())) {
        recalc_events = true;
    }
    if (resync_now_active && !resync_was_active
            && profile->countDays(MT_CPAP, profile->FirstDay(), profile->LastDay()) > 0) {
        if (QMessageBox::question(this, tr("Resync Device Detected Events"),
                                  tr("Resyncing moves device detected events and changes their durations, and the changes are saved with your data.\n\n"
                                     "Turning this option off later will not restore the original positions and durations. Only purging the data and re-importing it from the SD card or OSCAR's backup will.\n\n"
                                     "Are you sure you want to turn it on?"),
                                  QMessageBox::Yes, QMessageBox::No) == QMessageBox::No) {
            return false;
        }
    } else if (resync_was_active && !resync_now_active
            && profile->countDays(MT_CPAP, profile->FirstDay(), profile->LastDay()) > 0) {
        QMessageBox::information(this, tr("Resync Device Detected Events"),
                                 tr("Device detected events that have already been resynced keep their changed positions and durations.\n\n"
                                    "To restore them, purge the data and re-import it from the SD card or OSCAR's backup."));
    }

    if (profile->session->compressSessionData() != ui->compressSessionData->isChecked()) {
        recompress_events = true;
    }

    // SpO2 drop and pulse change flags are calculated once and saved with each session,
    // so changing how they're detected only takes effect once they're recalculated.
    // The window and manual value only matter to the method that uses them.
    auto new_base_method = (SpO2BaselineMethod)ui->spo2BaselineMethod->currentData().toInt();
    int new_base_window = ui->spo2BaselineWindow->value();
    int new_base_percentile = ui->spo2BaselinePercentile->value();
    bool new_base_static = (new_base_method == SBM_WholeNight) || (new_base_method == SBM_FirstHour);
    int new_base_manual = ui->baseSpO2Option->currentData().toInt();
    bool recalc_oximetry = (profile->oxi->spO2DropPercentage() != ui->spo2Drop->value())
            || (profile->oxi->spO2DropDuration() != ui->spo2DropDuration->value())
            || (profile->oxi->pulseChangeBPM() != ui->pulseChange->value())
            || (profile->oxi->pulseChangeDuration() != ui->pulseChangeDuration->value())
            || (profile->oxi->spO2BaselineMethod() != new_base_method)
            || ((new_base_method == SBM_Rolling) && (profile->oxi->spO2BaselineWindow() != new_base_window))
            || (new_base_static && (profile->oxi->spO2BaselinePercentile() != new_base_percentile))
            || ((new_base_method == SBM_Manual) && (profile->oxi->spO2ManualBaseline() != new_base_manual));
    if (recalc_oximetry) {
        recalc_events = true;
    }

    if (recompress_events) {
        if (profile->countDays(MT_CPAP, profile->FirstDay(), profile->LastDay()) > 0) {
            if (QMessageBox::question(this, tr("Data Processing Required"),
                                      tr("A data re/decompression procedure is required to apply these changes. This operation may take a couple of minutes to complete.\n\nAre you sure you want to make these changes?"),
                                      QMessageBox::Yes, QMessageBox::No) == QMessageBox::No) {
                return false;
            }
        } else { recompress_events = false; }
    }
    if (recalc_events) {
        int days = profile->countDays(MT_CPAP, profile->FirstDay(), profile->LastDay());
        if (recalc_oximetry) {
            days += profile->countDays(MT_OXIMETER, profile->FirstDay(), profile->LastDay());
        }
        if (days > 0) {
            if (QMessageBox::question(this, tr("Data Reindex Required"),
                                      tr("A data reindexing procedure is required to apply these changes. This operation may take a couple of minutes to complete.\n\nAre you sure you want to make these changes?"),
                                      QMessageBox::Yes, QMessageBox::No) == QMessageBox::No) {
                return false;
            }
        } else { recalc_events = false; }
    } else if (needs_restart) {
        if (QMessageBox::question(this, tr("Restart Required"),
                                  tr("One or more of the changes you have made will require this application to be restarted, in order for these changes to come into effect.\n\nWould you like do this now?"),
                                  QMessageBox::Yes, QMessageBox::No) == QMessageBox::No) {
            return false;
        }
    }

    AppSetting->setUserEventPieChart(ui->showUserFlagsInPie->isChecked());
    profile->session->setLockSummarySessions(ui->LockSummarySessionSplitting->isChecked());
    profile->session->setWarnOnUntestedMachine(ui->warnOnUntestedMachine->isChecked());
    profile->session->setWarnOnUnexpectedData(ui->warnOnUnexpectedData->isChecked());
    // Affects later imports only, so nothing needs reloading.
    profile->session->setSplitNonCPAPSessions(ui->splitNonCPAPSessions->isChecked());

    AppSetting->setOpenTabAtStart(ui->openingTabCombo->currentIndex());
    AppSetting->setOpenTabAfterImport(ui->importTabCombo->currentIndex());

    AppSetting->setAllowYAxisScaling(ui->allowYAxisScaling->isChecked());
    bool combineSimilarMachinesChanged =
            AppSetting->combineSimilarMachines() != ui->combineSimilarMachines->isChecked();
    AppSetting->setCombineSimilarMachines(ui->combineSimilarMachines->isChecked());
    AppSetting->setIncludeSerial(ui->includeSerial->isChecked());
    if (combineSimilarMachinesChanged) {
        QFile rxcache(p_profile->Get("{" + STR_GEN_DataFolder + "}/RXChanges.cache"));
        rxcache.remove();
        mainwin->GenerateStatistics();
    }
    AppSetting->setMonochromePrinting(ui->monochromePrinting->isChecked());
    p_profile->appearance->setEventFlagSessionBar(ui->eventFlagSessionBar->isChecked());
    AppSetting->setDisableDailyGraphTitles(ui->disableDailyGraphTitles->isChecked());

    if (ui->useFusionTheme->isChecked() != AppSetting->useFusionTheme()) {
        AppSetting->setUseFusionTheme(ui->useFusionTheme->isChecked());
        needs_restart = true;
    }
    const auto colorScheme = ColorSchemeChoice(ui->colorScheme->currentIndex());
    if (colorScheme != AppSetting->colorScheme()) {
        AppSetting->setColorScheme(colorScheme);
        if (ColorScheme::canSwitchLive()) {
            ColorScheme::apply();
        } else {
            needs_restart = true;   // older Qt applies the scheme only at startup
        }
    }
    // MainWindow rebuilds the Welcome and Overview tabs when these change.
    AppSetting->setUseNewPages(ui->useNewPages->isChecked());
    AppSetting->setWelcomeCardColour(WelcomeCardColour(ui->welcomeCardColour->currentIndex()));

    bool clicicalModeChanged = profile->cpap->clinicalMode() != ui->clinicalMode->isChecked() ;
#if defined(STEADY_BREATHING)
    // The effective state depends on clinical mode too, so capture it before either changes
    bool steadyBreathingWasActive = profile->cpap->steadyBreathing()==SB_ACTIVE;
#endif
    p_profile->cpap->setClinicalMode(ui->clinicalMode->isChecked());

#if defined(STEADY_BREATHING)
    // The group box is not checkable in Clinical Mode, and isChecked() is then always false,
    // so save the remembered choice rather than turning it off.
    if (ui->steadyBreathing->isCheckable()) {
        m_steadyBreathingChoice = ui->steadyBreathing->isChecked();
    }
    profile->cpap->setSteadyBreathing(m_steadyBreathingChoice ? SB_ACTIVE : SB_OFF);
    bool steadyBreathingGraphDIsplayChanged = steadyBreathingWasActive != (profile->cpap->steadyBreathing()==SB_ACTIVE);

#if defined(STEADY_BREATHING_ENHANCED_TESTING)
    profile->cpap->setSteadyBreathingDuration(ui->steadyBreathingDuration->value());
    profile->cpap->setSteadyBreathingThreshold(ui->steadyBreathingThreshold->value());
#endif
#endif



    if (ui->alternatingColorsCombo->currentIndex() != AppSetting->alternatingColorsCombo()) {
        AppSetting->setAlternatingColorsCombo(ui->alternatingColorsCombo->currentIndex());
        mainwin->GenerateStatistics();
    }

    AppSetting->setGraphTooltips(ui->graphTooltips->isChecked());

    if (ui->useSystemTooltipColors->isChecked() != AppSetting->useSystemTooltipColors()) {
        AppSetting->setUseSystemTooltipColors(ui->useSystemTooltipColors->isChecked());
        ToolTipColors::apply();
    }

    AppSetting->setAntiAliasing(ui->useAntiAliasing->isChecked());
    AppSetting->setUsePixmapCaching(ui->usePixmapCaching->isChecked());
    AppSetting->setSquareWavePlots(ui->useSquareWavePlots->isChecked());
    AppSetting->setLineThickness(float(ui->lineThicknessSlider->value()) / 2.0);
    AppSetting->setGridLineOpacity(ui->gridLineOpacitySlider->value());

    profile->general->setSkipEmptyDays(ui->skipEmptyDays->isChecked());

    AppSetting->setTooltipTimeout(ui->tooltipTimeoutSlider->value());
    AppSetting->setScrollDampening(ui->scrollDampeningSlider->value() * 10);

    profile->general->setShowUnknownFlags(ui->showUnknownFlags->isChecked());
    AppSetting->setMultithreading(ui->enableMultithreading->isChecked());
    AppSetting->setRemoveCardReminder(ui->removeCardNotificationCheckbox->isChecked());
    AppSetting->setNotifyMessagBoxOption(ui->notifyMessageBoxCheckbox->isChecked());
    AppSetting->setDontAskWhenSavingScreenshots(ui->dontAskWhenSavingScreenshotsCheckbox->isChecked());
    AppSetting->setShowDatabaseMenu(ui->showDatabaseMenuCheckbox->isChecked());

    AppSetting->setCacheSessions(ui->cacheSessionData->isChecked());
    profile->session->setPreloadSummaries(ui->preloadSummaries->isChecked());
    AppSetting->setAnimations(ui->animationsAndTransitionsCheckbox->isChecked());

    profile->cpap->setShowLeakRedline(ui->showLeakRedline->isChecked());
    profile->cpap->setLeakRedline(ui->leakRedlineSpinbox->value());

    profile->cpap->setComplianceHours(ui->complianceHours->value());

    if (ui->graphHeight->value() != AppSetting->graphHeight()) {
        AppSetting->setGraphHeight(ui->graphHeight->value());
        mainwin->getDaily()->ResetGraphLayout();
        mainwin->getOverview()->ResetGraphLayout();
    }

    profile->general->setPrefCalcMiddle(ui->prefCalcMiddle->currentIndex());
    profile->general->setPrefCalcMax(ui->prefCalcMax->currentIndex());
    profile->general->setPrefCalcPercentile(ui->prefCalcPercentile->value());

    profile->general->setCalculateRDI((ui->eventIndexCombo->currentIndex() == 1));
    profile->session->setBackupCardData(ui->createSDBackups->isChecked());
    profile->session->setCompressBackupData(ui->compressSDBackups->isChecked());
    profile->session->setCompressSessionData(ui->compressSessionData->isChecked());

    profile->session->setCombineCloseSessions(ui->combineSlider->value());
    profile->session->setIgnoreShortSessions(ui->IgnoreSlider->value());
    profile->session->setDaySplitTime(ui->timeEdit->time());
    profile->session->setIgnoreOlderSessions(ui->ignoreOlderSessionsCheck->isChecked());
    profile->session->setIgnoreOlderSessionsDate(ui->ignoreOlderSessionsDate->date());

    AppSetting->setOverlayType((OverlayDisplayType)ui->overlayFlagsCombo->currentIndex());
#ifndef REMOVE_FITNESS
    AppSetting->setOverviewLinechartMode((OverviewLinechartModes)ui->overviewLinecharts->currentIndex());
#endif

    profile->oxi->setSpO2DropPercentage(ui->spo2Drop->value());
    profile->oxi->setSpO2DropDuration(ui->spo2DropDuration->value());
    profile->oxi->setPulseChangeBPM(ui->pulseChange->value());
    profile->oxi->setPulseChangeDuration(ui->pulseChangeDuration->value());
    profile->oxi->setOxiDiscardThreshold(ui->oxiDiscardThreshold->value());

    profile->oxi->setOxiDesaturationThreshold(ui->oxiDesaturationThreshold->value());
    profile->oxi->setFlagPulseAbove(ui->flagPulseAbove->value());
    profile->oxi->setFlagPulseBelow(ui->flagPulseBelow->value());
    //added by Sheila 1/5/2026
    profile->oxi->setSpO2BaselineMethod(new_base_method);
    profile->oxi->setSpO2BaselineWindow(new_base_window);
    profile->oxi->setSpO2BaselinePercentile(new_base_percentile);
    profile->oxi->setSpO2ManualBaseline(new_base_manual);

    profile->cpap->setAHIWindow(ui->ahiGraphWindowSize->value());
    profile->cpap->setAHIReset(ui->ahiGraphZeroReset->isChecked());

    profile->cpap->setAutoImport(ui->automaticImport->isChecked()); // delete me???
    profile->cpap->setWarnOnDifferentSDCard(ui->warnOnDifferentSDCard->isChecked());
    AppSetting->setAutoOpenLastUsed(ui->autoLoadLastUsed->isChecked());

    profile->cpap->setUserEventFlagging(ui->customEventGroupbox->isChecked());

    profile->cpap->setResyncFromUserFlagging(ui->resyncMachineDetectedEvents->isChecked());
    profile->cpap->setUserEventDuration(ui->apneaDuration->value());
    profile->cpap->setUserEventRestriction(ui->apneaFlowRestriction->value());
    profile->cpap->setUserEventDuration2(ui->apneaDuration2->value());
    profile->cpap->setUserEventRestriction2(ui->apneaFlowRestriction2->value());

    profile->cpap->setUserEventDuplicates(ui->userEventDuplicates->isChecked());



    if ((ui->calculateUnintentionalLeaks->isChecked() != profile->cpap->calculateUnintentionalLeaks())
            || (fabs((ui->maskLeaks4Slider->value()/10.0)-profile->cpap->custom4cmH2OLeaks())>.1)
            || (fabs((ui->maskLeaks20Slider->value()/10.0)-profile->cpap->custom20cmH2OLeaks())>.1)) {
        recalc_events = true;
    }

    profile->cpap->setCalculateUnintentionalLeaks(ui->calculateUnintentionalLeaks->isChecked());
    profile->cpap->setCustom4cmH2OLeaks(double(ui->maskLeaks4Slider->value()) / 10.0f);
    profile->cpap->setCustom20cmH2OLeaks(double(ui->maskLeaks20Slider->value()) / 10.0f);

    profile->cpap->setEventPostcontext(double(ui->eventPostcontext->value()));
    profile->cpap->setConsolidateEvents(ui->consolidateEvents->isChecked());
    
    AppSetting->setAutoLaunchImport(ui->autoLaunchImporter->isChecked());

#ifndef NO_CHECKUPDATES
    AppSetting->setUpdatesAutoCheck(ui->automaticallyCheckUpdates->isChecked());
    AppSetting->setUpdateCheckFrequency(ui->updateCheckEvery->value());
    AppSetting->setAllowEarlyUpdates(ui->allowEarlyUpdates->isChecked());
#endif


    (*p_pref)["Fonts_Application_Name"] = ui->applicationFont->currentText();
    (*p_pref)["Fonts_Application_Size"] = ui->applicationFontSize->value();
    (*p_pref)["Fonts_Application_Bold"] = ui->applicationFontBold->isChecked();
    (*p_pref)["Fonts_Application_Italic"] = ui->applicationFontItalic->isChecked();

    (*p_pref)["Fonts_Graph_Name"] = ui->graphFont->currentText();
    (*p_pref)["Fonts_Graph_Size"] = ui->graphFontSize->value();
    (*p_pref)["Fonts_Graph_Bold"] = ui->graphFontBold->isChecked();
    (*p_pref)["Fonts_Graph_Italic"] = ui->graphFontItalic->isChecked();

    (*p_pref)["Fonts_Title_Name"] = ui->titleFont->currentText();
    (*p_pref)["Fonts_Title_Size"] = ui->titleFontSize->value();
    (*p_pref)["Fonts_Title_Bold"] = ui->titleFontBold->isChecked();
    (*p_pref)["Fonts_Title_Italic"] = ui->titleFontItalic->isChecked();

    (*p_pref)["Fonts_Big_Name"] = ui->bigFont->currentText();
    (*p_pref)["Fonts_Big_Size"] = ui->bigFontSize->value();
    (*p_pref)["Fonts_Big_Bold"] = ui->bigFontBold->isChecked();
    (*p_pref)["Fonts_Big_Italic"] = ui->bigFontItalic->isChecked();

    validateAllFonts();     // Make sure fonts are valid and reset to default values if not

    // Changes to the application font will be set by the caller (mainwindow.cpp)

    // Set other fonts for use by graph etc. pages. Updated in place (not re-created) so
    // pointers held by tooltips etc. stay valid and follow the change (#358).
    ApplyGraphFontPreferences();

    saveChanInfo();
    saveWaveInfo();
    // Write the channel edits now, not only when the profile closes, so a backup or an
    // external script sees them and they survive an abnormal exit (#431).
    profile->saveChannels();

    p_pref->Save();
    profile->Save();
    profile->resetOxiChannelPref();

#if defined(STEADY_BREATHING)
    // A clinical mode change reloads the profile below; don't reload it twice
    if (steadyBreathingGraphDIsplayChanged && !clicicalModeChanged) {
        mainwin->reloadProfile();
    }
#endif
    // steady Breathing
    if (clicicalModeChanged) {

        // this fails - causing duplicate reload.
        // while this one works.
        mainwin->reloadProfile();
    } else
        if (recompress_events) {
            mainwin->recompressEvents();
        } else if (recalc_events) {
            // send a signal instead?
            mainwin->reprocessEvents(needs_restart);
        } else if (needs_reload) {
            QTimer::singleShot(0, mainwin, SLOT(reloadProfile()));
        } else if (needs_restart) {
            mainwin->RestartApplication();
            return false; // Prevent accept() so post-dialog UI code doesn't run with null p_profile
        } else {
            mainwin->getDaily()->LoadDate(mainwin->getDaily()->getDate());
            // Save early.. just in case..
            mainwin->getDaily()->graphView()->SaveSettings("Daily");
            mainwin->getOverview()->graphView()->SaveSettings("Overview");
        }

    return true;
}

void PreferencesDialog::saveChanInfo()
{
    // Change focus to force save of any open editors..
    ui->channelSearch->setFocus();

    int toprows = chanModel->rowCount();
    bool ok;

    for (int i=0; i < toprows; i++) {
        QStandardItem * topitem = chanModel->item(i,0);

        if (!topitem) continue;
        int rows = topitem->rowCount();
        for (int j=0; j< rows; ++j) {
            QStandardItem * item = topitem->child(j, 0);
            if (!item) continue;

            ChannelID id = item->data(Qt::UserRole).toUInt(&ok);
            schema::Channel & chan = schema::channel[id];
            if (chan.isNull()) continue;
            chan.setEnabled(item->checkState() == Qt::Checked ? true : false);
            chan.setFullname(item->text());
            chan.setDefaultColor(QColor(topitem->child(j,1)->data(Qt::UserRole).toUInt()));
            chan.setShowInOverview(topitem->child(j,2)->checkState() == Qt::Checked);
            QString ts = topitem->child(j,3)->text();
            schema::ChanType type = schema::MINOR_FLAG;
            for (QHash<schema::ChanType, QString>::iterator it = channeltype.begin(); it!= channeltype.end(); ++it) {
                if (it.value() == ts) {
                    type = it.key();
                    break;
                }
            }
            chan.setType(type);
            chan.setLabel(topitem->child(j,4)->text());
            chan.setDescription(topitem->child(j,5)->text());
        }
    }
}
void PreferencesDialog::saveWaveInfo()
{
    // Change focus to force save of any open editors..
    ui->waveSearch->setFocus();

    int toprows = waveModel->rowCount();

    bool ok;
    for (int i=0; i < toprows; i++) {
        QStandardItem * topitem = waveModel->item(i,0);

        if (!topitem) continue;
        int rows = topitem->rowCount();
        for (int j=0; j< rows; ++j) {
            QStandardItem * item = topitem->child(j, 0);
            if (!item) continue;

            ChannelID id = item->data(Qt::UserRole).toUInt(&ok);
            schema::Channel & chan = schema::channel[id];
            if (chan.isNull()) continue;
            chan.setEnabled(item->checkState() == Qt::Checked ? true : false);
            chan.setFullname(item->text());
            chan.setDefaultColor(QColor(topitem->child(j,1)->data(Qt::UserRole).toUInt()));
            chan.setShowInOverview(topitem->child(j,2)->checkState() == Qt::Checked);
            chan.setLowerThreshold(topitem->child(j,3)->text().toDouble());
            chan.setUpperThreshold(topitem->child(j,4)->text().toDouble());
            chan.setLabel(topitem->child(j,5)->text());
            chan.setDescription(topitem->child(j,6)->text());
        }
    }
}

void PreferencesDialog::on_combineSlider_valueChanged(int position)
{
    if (position > 0) {
        ui->combineLCD->display(position);
    } else { ui->combineLCD->display(STR_TR_Off); }
}

void PreferencesDialog::on_IgnoreSlider_valueChanged(int position)
{
    if (position > 0) {
        ui->IgnoreLCD->display(position);
    } else { ui->IgnoreLCD->display(STR_TR_Off); }
}

#ifndef NO_CHECKUPDATES
#include "mainwindow.h"
extern MainWindow *mainwin;
void PreferencesDialog::RefreshLastChecked()
{
    ui->updateLastChecked->setText(QLocale().toString(AppSetting->updatesLastChecked(), QLocale::LongFormat));
}
#endif

MySortFilterProxyModel::MySortFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{

}

bool MySortFilterProxyModel::filterAcceptsRow(int source_row,
                                              const QModelIndex &source_parent) const
{
    if (source_parent == qobject_cast<QStandardItemModel *>
            (sourceModel())->invisibleRootItem()->index()) {
        // always accept children of rootitem, since we want to filter their children
        if (!m_customizedOnly) {
            return true;
        }
        // ...except when showing customized rows only: then a group header stays only
        // while at least one of its channel rows is shown.
        const QModelIndex header = sourceModel()->index(source_row, 0, source_parent);
        const int children = sourceModel()->rowCount(header);
        for (int r = 0; r < children; ++r) {
            if (filterAcceptsRow(r, header)) {
                return true;
            }
        }
        return false;
    }

    if (!QSortFilterProxyModel::filterAcceptsRow(source_row, source_parent)) {
        return false;
    }
    if (m_customizedOnly) {
        const QModelIndex name = sourceModel()->index(source_row, 0, source_parent);
        return name.data(CustomizedRole).toBool();
    }
    return true;
}

void MySortFilterProxyModel::setCustomizedOnly(bool on)
{
    m_customizedOnly = on;
    invalidateFilter();
}

void MySortFilterProxyModel::scheduleRefilter()
{
    if (!m_customizedOnly || m_refilterPending) {
        return;
    }
    m_refilterPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_refilterPending = false;
        if (m_customizedOnly) {
            invalidateFilter();
        }
    });
}

// Might still be useful..
//void PreferencesDialog::on_resetGraphButton_clicked()

//    // Display confirmation dialog.



//    // Iterate over all graph containers.


//        // Iterate over all contained graphs.
//            g->setRecMaxY(0); // FIXME: should be g->reset(), but need other patches to land.



void PreferencesDialog::on_createSDBackups_toggled(bool checked)
{
    if (profile->session->backupCardData() && !checked) {
        QList<Machine *> mach = p_profile->GetMachines(MT_CPAP);
        bool haveS9 = false;

        for (int i = 0; i < mach.size(); i++) {
            if (mach[i]->loaderName() == STR_MACH_ResMed) {
                haveS9 = true;
                break;
            }
        }

        if (haveS9 && QMessageBox::question(this,
                                            tr("This may not be a good idea"),
                                            tr("ResMed S9 devices routinely delete certain data from your SD card older than 7 and 30 days (depending on resolution).") +
                                            tr(" If you ever need to reimport this data again (whether in OSCAR or ResScan) this data won't come back.") +
                                            tr(" If you need to conserve disk space, please remember to carry out manual backups.") +
                                            tr(" Are you sure you want to disable these backups?"),
                                            QMessageBox::Yes, QMessageBox::No) == QMessageBox::No) {
            ui->createSDBackups->setChecked(true);
            return;
        }
    }

    if (!checked) { ui->compressSDBackups->setChecked(false); }

    ui->compressSDBackups->setEnabled(checked);
}

void PreferencesDialog::on_okButton_clicked()
{
    if (Save()) {
        accept();
    }
}

void PreferencesDialog::on_scrollDampeningSlider_valueChanged(int value)
{
    if (value > 0) {
        ui->scrollDampDisplay->setText(QString("%1%2").arg(value * 10).arg(STR_UNIT_ms));
    } else { ui->scrollDampDisplay->setText(STR_TR_Off); }
}

void PreferencesDialog::on_tooltipTimeoutSlider_valueChanged(int value)
{
    ui->tooltipTimeoutDisplay->setText(QString("%1%2").arg(double(value)/1000.0,0,'f',1).arg(STR_UNIT_s));
}

void PreferencesDialog::on_gridLineOpacitySlider_valueChanged(int value)
{
    ui->gridLineOpacityDisplay->setText(QString::number(value));
}

void PreferencesDialog::on_resetChannelDefaults_clicked()
{
    if (QMessageBox::question(this, STR_MessageBox_Warning, QObject::tr("Are you sure you want to reset all your channel colors and settings to defaults?"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
        schema::resetChannels();
        saveWaveInfo();
        InitChanInfo();
    }
}

#if defined(STEADY_BREATHING)
void PreferencesDialog::updateSteadyBreathingForMode(bool clinical)
{
    if (clinical) {
        // Remember what the user picked in Permissive Mode before the box stops being checkable
        if (ui->steadyBreathing->isCheckable()) {
            m_steadyBreathingChoice = ui->steadyBreathing->isChecked();
        }
        ui->steadyBreathing->setCheckable(false);
        ui->steadyBreathing->setTitle(tr("Must enable Permissive Mode (see Clinical Tab)"));
        ui->steadyBreathing->setDisabled(true);
    } else {
        ui->steadyBreathing->setEnabled(true);
        ui->steadyBreathing->setTitle(m_steadyBreathingTitle);
        ui->steadyBreathing->setCheckable(true);
        ui->steadyBreathing->setChecked(m_steadyBreathingChoice);
    }
}
#endif

#if defined(STEADY_BREATHING_ENHANCED_TESTING)
void PreferencesDialog::on_resetSteadyBreathingDefaults_clicked()
{
    // Ui MUST BE MANUALLY ENABLED. was commented out
    profile->cpap->setSteadyBreathingDuration(profile->cpap->steadyBreathingDefaultDuration());
    profile->cpap->setSteadyBreathingThreshold(profile->cpap->steadyBreathingDefaultThreshold());
    ui->steadyBreathingDuration->setValue( profile->cpap->steadyBreathingDuration());
    ui->steadyBreathingThreshold->setValue(profile->cpap->steadyBreathingThreshold());
}
#endif

void PreferencesDialog::on_resetOxiMetryDefaults_clicked()
{

    if (QMessageBox::question(this, STR_MessageBox_Warning, QObject::tr("Are you sure you want to reset all your oximetry settings to defaults?"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {

        // reset ui with defaul values
        ui->spo2Drop->setValue(           profile->oxi->defaultValue_OS_SPO2DropPercentage);
        ui->spo2DropDuration->setValue(   profile->oxi->defaultValue_OS_SPO2DropDuration);
        ui->pulseChange->setValue(        profile->oxi->defaultValue_OS_PulseChangeBPM);
        ui->pulseChangeDuration->setValue(profile->oxi->defaultValue_OS_PulseChangeDuration);

        ui->oxiDiscardThreshold->setValue(profile->oxi->defaultValue_OS_OxiDiscardThreshold);
        ui->oxiDesaturationThreshold->setValue(  profile->oxi->defaultValue_OS_oxiDesaturationThreshold);
        ui->flagPulseAbove->setValue(     profile->oxi->defaultValue_OS_flagPulseAbove );
        ui->flagPulseBelow->setValue(     profile->oxi->defaultValue_OS_flagPulseBelow );
        showSpO2Baseline((SpO2BaselineMethod)profile->oxi->defaultValue_OS_SpO2BaselineMethod,
                         profile->oxi->defaultValue_OS_SpO2BaselineWindow,
                         profile->oxi->defaultValue_OS_SpO2BaselinePercentile,
                         profile->oxi->defaultValue_OS_SpO2ManualBaseline);

        if (Save() ) {
            // comment accept out to return to the preference tab
            // other wise the preference tab will close and return
            accept();
        }
    } else {
        // restore values changed
        ui->spo2Drop->setValue(profile->oxi->spO2DropPercentage());
        ui->spo2DropDuration->setValue(profile->oxi->spO2DropDuration());
        ui->pulseChange->setValue(profile->oxi->pulseChangeBPM());
        ui->pulseChangeDuration->setValue(profile->oxi->pulseChangeDuration());

        ui->oxiDiscardThreshold->setValue(profile->oxi->oxiDiscardThreshold());
        ui->oxiDesaturationThreshold->setValue(profile->oxi->oxiDesaturationThreshold());
        ui->flagPulseAbove->setValue(profile->oxi->flagPulseAbove());
        ui->flagPulseBelow->setValue(profile->oxi->flagPulseBelow());
        showSpO2Baseline(profile->oxi->spO2BaselineMethod(), profile->oxi->spO2BaselineWindow(),
                         profile->oxi->spO2BaselinePercentile(), profile->oxi->spO2ManualBaseline());
    }

}

void PreferencesDialog::showSpO2Baseline(SpO2BaselineMethod method, int window, int percentile, int manual)
{
    int idx = ui->spo2BaselineMethod->findData(method);
    ui->spo2BaselineMethod->setCurrentIndex(idx >= 0 ? idx : 0);
    ui->spo2BaselineWindow->setValue(window);
    ui->spo2BaselinePercentile->setValue(percentile);
    ui->baseSpO2Option->setCurrentIndex(ui->baseSpO2Option->findData(manual));
    updateSpO2BaselineControls();
}

void PreferencesDialog::updateSpO2BaselineControls()
{
    auto method = (SpO2BaselineMethod)ui->spo2BaselineMethod->currentData().toInt();
    ui->spo2BaselineWindow->setEnabled(method == SBM_Rolling);
    ui->label_spo2BaselineWindow->setEnabled(method == SBM_Rolling);
    const bool isStatic = (method == SBM_WholeNight) || (method == SBM_FirstHour);
    ui->spo2BaselinePercentile->setEnabled(isStatic);
    ui->label_spo2BaselinePercentile->setEnabled(isStatic);
    ui->baseSpO2Option->setEnabled(method == SBM_Manual);
    ui->label_baseSpO2Option->setEnabled(method == SBM_Manual);
}

void PreferencesDialog::on_createSDBackups_clicked(bool checked)
{
    if (!checked && p_profile->session->backupCardData()) {
        if (QMessageBox::question(this,
                                  STR_MessageBox_Warning, tr("Switching off backups is not a good idea, because OSCAR needs these to rebuild the database if errors are found.\n\n") +
                                  tr("Are you really sure you want to do this?"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
            // do nothing
        } else {
            ui->createSDBackups->setChecked(true);
        }
    }
}

void PreferencesDialog::on_channelSearch_textChanged(const QString &arg1)
{
    chanFilterModel->setFilterFixedString(arg1);
}

void PreferencesDialog::on_chanView_doubleClicked(const QModelIndex &index)
{
    if (index.column() == 1) {
        QColorDialog a;

        if (!(index.flags() & Qt::ItemIsEnabled)) return;
        quint32 color = index.data(Qt::UserRole).toUInt();

        a.setCurrentColor(QColor((QRgb)color));

        // Resolve the source cell before the modal loop: the proxy can refilter while the
        // colour dialog is open, leaving the proxy index stale. A persistent source index
        // follows the row; the first change can also hide the row from the proxy (Show
        // customized only), so both roles are set on the source item.
        const QPersistentModelIndex source(chanFilterModel->mapToSource(index));

        if (a.exec() == QColorDialog::Accepted) {
            quint32 cv = a.currentColor().rgba();

            QStandardItem *cell = source.isValid() ? chanModel->itemFromIndex(source) : nullptr;
            if (!cell) return;
            cell->setData(a.currentColor(), Qt::BackgroundRole);
            cell->setData(cv, Qt::UserRole);

        }

    }
}

void PreferencesDialog::on_waveSearch_textChanged(const QString &arg1)
{
    waveFilterModel->setFilterFixedString(arg1);
}

void PreferencesDialog::on_resetWaveformChannels_clicked()
{
    if (QMessageBox::question(this, STR_MessageBox_Warning, QObject::tr("Are you sure you want to reset all your waveform channel colors and settings to defaults?"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
        schema::resetChannels();
        saveChanInfo(); // reset clears EVERYTHING, so have to put these back in case they cancel.
        InitWaveInfo();
    }
}

void PreferencesDialog::on_waveView_doubleClicked(const QModelIndex &index)
{
    if (index.column() == 1) {
        QColorDialog a;

        if (!(index.flags() & Qt::ItemIsEnabled)) return;
        quint32 color = index.data(Qt::UserRole).toUInt();

        a.setCurrentColor(QColor((QRgb)color));

        // Resolve the source cell before the modal loop: the proxy can refilter while the
        // colour dialog is open, leaving the proxy index stale. A persistent source index
        // follows the row; the first change can also hide the row from the proxy (Show
        // customized only), so both roles are set on the source item.
        const QPersistentModelIndex source(waveFilterModel->mapToSource(index));

        if (a.exec() == QColorDialog::Accepted) {
            quint32 cv = a.currentColor().rgba();

            QStandardItem *cell = source.isValid() ? waveModel->itemFromIndex(source) : nullptr;
            if (!cell) return;
            cell->setData(a.currentColor(), Qt::BackgroundRole);
            cell->setData(cv, Qt::UserRole);

        }

    }
}

// ---------------------------------------------------------------------------------------
// Customized channels on the Events and Waveforms tabs: marker, Reset Channel and filter.
// A row is "customized" when ChannelOverrides::diffFrom() finds a field of its current
// cells that differs from the channel's code defaults, i.e. what OK would store as an
// override. Everything here works on the dialog's model only; OK saves it, Cancel drops it.
// ---------------------------------------------------------------------------------------

namespace {
//! The channel a tree row describes, or nullptr when it is unknown or has no recorded defaults.
schema::Channel *rowChannel(QStandardItem *nameItem)
{
    bool ok = false;
    const ChannelID id = nameItem->data(Qt::UserRole).toUInt(&ok);
    if (!ok) {
        return nullptr;
    }
    schema::Channel &chan = schema::channel[id];
    if (chan.isNull() || !chan.hasDefaults()) {
        return nullptr;
    }
    return &chan;
}
} // namespace

void PreferencesDialog::setupChannelTab(ChannelTab &tab)
{
    ChannelTab *t = &tab;   // a member of this dialog, so it outlives the connections
    connect(tab.model, &QStandardItemModel::itemChanged, this,
            [this, t](QStandardItem *item) { channelItemChanged(*t, item); });
    connect(tab.view->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this, t]() { updateResetButton(*t); });

    tab.view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tab.view, &QWidget::customContextMenuRequested, this,
            [this, t](const QPoint &pos) { showChannelMenu(*t, pos); });

    // A group header that the filter hides and shows again comes back collapsed.
    QTreeView *view = tab.view;
    MySortFilterProxyModel *proxy = tab.proxy;
    connect(proxy, &QAbstractItemModel::rowsInserted, view,
            [view, proxy](const QModelIndex &parent, int first, int last) {
        if (parent.isValid()) {
            return;
        }
        for (int r = first; r <= last; ++r) {
            view->expand(proxy->index(r, 0));
        }
    });
}

QStandardItem *PreferencesDialog::channelRowAt(const ChannelTab &tab, const QModelIndex &proxyIndex) const
{
    if (!proxyIndex.isValid()) {
        return nullptr;
    }
    const QModelIndex source = tab.proxy->mapToSource(proxyIndex);
    if (!source.isValid() || !source.parent().isValid()) {
        return nullptr;   // a group header row
    }
    return tab.model->itemFromIndex(source.siblingAtColumn(0));
}

QStandardItem *PreferencesDialog::rowCell(QStandardItem *nameItem, int col)
{
    if (col < 0 || !nameItem || !nameItem->parent()) {
        return nullptr;
    }
    return nameItem->parent()->child(nameItem->row(), col);
}

ChannelOverrides::Values PreferencesDialog::rowValues(const ChannelTab &tab, QStandardItem *nameItem) const
{
    // Read the cells exactly as saveChanInfo() / saveWaveInfo() do.
    const ChannelColumns &c = tab.cols;
    ChannelOverrides::Values v;
    v.enabled = nameItem->checkState() == Qt::Checked;
    v.fullname = nameItem->text();
    if (QStandardItem *cell = rowCell(nameItem, c.color)) {
        v.color = QColor(QRgb(cell->data(Qt::UserRole).toUInt()));
    }
    if (QStandardItem *cell = rowCell(nameItem, c.overview)) {
        v.showInOverview = cell->checkState() == Qt::Checked;
    }
    if (QStandardItem *cell = rowCell(nameItem, c.label)) {
        v.label = cell->text();
    }
    if (QStandardItem *cell = rowCell(nameItem, c.details)) {
        v.description = cell->text();
    }
    if (QStandardItem *cell = rowCell(nameItem, c.lower)) {
        v.lowerThreshold = cell->text().toDouble();
    }
    if (QStandardItem *cell = rowCell(nameItem, c.upper)) {
        v.upperThreshold = cell->text().toDouble();
    }
    return v;
}

ChannelOverrides::Values PreferencesDialog::rowOverrides(const ChannelTab &tab, QStandardItem *nameItem) const
{
    const schema::Channel *chan = rowChannel(nameItem);
    if (!chan) {
        return ChannelOverrides::Values();
    }
    return ChannelOverrides::diffFrom(chan->id(), chan->type(), chan->defaults(),
                                      rowValues(tab, nameItem));
}

void PreferencesDialog::updateRowMarkers(const ChannelTab &tab, QStandardItem *nameItem)
{
    if (!nameItem || !nameItem->parent()) {
        return;
    }
    const schema::Channel *chan = rowChannel(nameItem);
    const schema::ChannelDefaults defaults = chan ? chan->defaults() : schema::ChannelDefaults();
    const ChannelOverrides::Values ov = rowOverrides(tab, nameItem);
    const bool customized = !ov.isEmpty();
    const ChannelColumns &c = tab.cols;

    // Bold and a "Default:" tooltip line on an overridden text cell; plain and the cell's
    // own tooltip otherwise. The cell's text is never touched: it is what OK saves.
    auto mark = [](QStandardItem *cell, bool overridden, const QString &defaultText) {
        if (!cell) {
            return;
        }
        QVariant base = cell->data(kBaseToolTipRole);
        if (!base.isValid()) {
            base = cell->toolTip();
            cell->setData(base, kBaseToolTipRole);
        }
        QString tip = base.toString();
        if (overridden) {
            // An empty default would leave "Default: " with nothing after it.
            const QString shown = defaultText.isEmpty() ? PreferencesDialog::tr("(empty)") : defaultText;
            const QString line = PreferencesDialog::tr("Default: %1").arg(shown);
            tip += Qt::mightBeRichText(tip) ? QStringLiteral("<br/>") + line.toHtmlEscaped()
                                            : QStringLiteral("\n") + line;
        }
        cell->setToolTip(tip);
        if (overridden) {
            QFont bold;   // only the weight is set, so the view's own font supplies the rest
            bold.setBold(true);
            cell->setFont(bold);
        } else if (cell->data(Qt::FontRole).isValid()) {
            cell->setData(QVariant(), Qt::FontRole);
        }
    };

    {
        // Each change below emits itemChanged again; channelItemChanged() ignores it.
        const QScopedValueRollback<bool> guard(m_updatingMarkers, true);
        if (customized) {
            nameItem->setData(m_customizedDot, Qt::DecorationRole);
        } else if (nameItem->data(Qt::DecorationRole).isValid()) {
            nameItem->setData(QVariant(), Qt::DecorationRole);
        }
        nameItem->setData(customized, MySortFilterProxyModel::CustomizedRole);

        mark(nameItem, ov.fullname.has_value(), defaults.fullname);
        mark(rowCell(nameItem, c.label), ov.label.has_value(), defaults.label);
        mark(rowCell(nameItem, c.details), ov.description.has_value(), defaults.description);
        mark(rowCell(nameItem, c.lower), ov.lowerThreshold.has_value(),
             thresholdText(defaults.lowerThreshold));
        mark(rowCell(nameItem, c.upper), ov.upperThreshold.has_value(),
             thresholdText(defaults.upperThreshold));
    }

    // The proxy re-filters this row itself; group headers need a full pass.
    tab.proxy->scheduleRefilter();
    updateResetButton(tab);
}

void PreferencesDialog::updateAllMarkers(const ChannelTab &tab)
{
    for (int i = 0; i < tab.model->rowCount(); ++i) {
        QStandardItem *header = tab.model->item(i, 0);
        if (!header) {
            continue;
        }
        for (int j = 0; j < header->rowCount(); ++j) {
            updateRowMarkers(tab, header->child(j, 0));
        }
    }
    tab.proxy->scheduleRefilter();
    updateResetButton(tab);
}

void PreferencesDialog::resetRow(const ChannelTab &tab, QStandardItem *nameItem)
{
    if (!nameItem || !nameItem->parent()) {
        return;
    }
    const schema::Channel *chan = rowChannel(nameItem);
    if (!chan) {
        return;
    }
    const schema::ChannelDefaults &d = chan->defaults();
    const ChannelID id = chan->id();
    const schema::ChanType type = chan->type();
    auto may = [id, type](ChannelOverrides::Field field) {
        return ChannelOverrides::overridable(id, type, field);
    };
    const ChannelColumns &c = tab.cols;

    {
        // Markers are recomputed once below, not after every cell.
        const QScopedValueRollback<bool> guard(m_updatingMarkers, true);
        if (may(ChannelOverrides::Enabled)) {
            nameItem->setCheckState(d.enabled ? Qt::Checked : Qt::Unchecked);
        }
        if (may(ChannelOverrides::Fullname)) {
            nameItem->setText(d.fullname);
        }
        QStandardItem *cell = rowCell(nameItem, c.color);
        if (cell && may(ChannelOverrides::Color)) {
            cell->setBackground(QBrush(d.color));
            cell->setData(d.color.rgba(), Qt::UserRole);
        }
        cell = rowCell(nameItem, c.overview);
        if (cell && may(ChannelOverrides::ShowInOverview)) {
            cell->setCheckState(d.showInOverview ? Qt::Checked : Qt::Unchecked);
        }
        cell = rowCell(nameItem, c.label);
        if (cell && may(ChannelOverrides::Label)) {
            cell->setText(d.label);
        }
        cell = rowCell(nameItem, c.details);
        if (cell && may(ChannelOverrides::Description)) {
            cell->setText(d.description);
        }
        // Thresholds that a preference drives (leak, oximetry) are left alone.
        cell = rowCell(nameItem, c.lower);
        if (cell && may(ChannelOverrides::LowerThreshold)) {
            cell->setText(thresholdText(d.lowerThreshold));
        }
        cell = rowCell(nameItem, c.upper);
        if (cell && may(ChannelOverrides::UpperThreshold)) {
            cell->setText(thresholdText(d.upperThreshold));
        }
    }
    updateRowMarkers(tab, nameItem);
}

void PreferencesDialog::updateResetButton(const ChannelTab &tab)
{
    QStandardItem *nameItem = channelRowAt(tab, tab.view->currentIndex());
    tab.resetButton->setEnabled(nameItem
                                && nameItem->data(MySortFilterProxyModel::CustomizedRole).toBool());
}

void PreferencesDialog::channelItemChanged(const ChannelTab &tab, QStandardItem *item)
{
    if (m_updatingMarkers || !item || !item->parent()) {
        return;   // our own marker edits, or a group header row
    }
    updateRowMarkers(tab, item->parent()->child(item->row(), 0));
}

void PreferencesDialog::showChannelMenu(const ChannelTab &tab, const QPoint &pos)
{
    const QModelIndex index = tab.view->indexAt(pos);
    QStandardItem *nameItem = channelRowAt(tab, index);
    if (!nameItem) {
        return;
    }
    // Make the row the menu acts on the current one, so it is highlighted.
    tab.view->setCurrentIndex(index.siblingAtColumn(0));

    const QPersistentModelIndex row(nameItem->index());
    QMenu menu(this);
    QAction *reset = menu.addAction(tr("Reset Channel"));
    reset->setEnabled(nameItem->data(MySortFilterProxyModel::CustomizedRole).toBool());
    if (menu.exec(tab.view->viewport()->mapToGlobal(pos)) == reset && row.isValid()) {
        resetRow(tab, tab.model->itemFromIndex(row));
    }
}

void PreferencesDialog::on_resetOneChannel_clicked()
{
    resetRow(m_eventsTab, channelRowAt(m_eventsTab, ui->chanView->currentIndex()));
}

void PreferencesDialog::on_resetOneWaveform_clicked()
{
    resetRow(m_wavesTab, channelRowAt(m_wavesTab, ui->waveView->currentIndex()));
}

void PreferencesDialog::on_chanCustomizedOnly_toggled(bool checked)
{
    chanFilterModel->setCustomizedOnly(checked);
    updateResetButton(m_eventsTab);
}

void PreferencesDialog::on_waveCustomizedOnly_toggled(bool checked)
{
    waveFilterModel->setCustomizedOnly(checked);
    updateResetButton(m_wavesTab);
}

void PreferencesDialog::on_maskLeaks4Slider_valueChanged(int value)
{
    ui->maskLeaks4Label->setText(QString("%1 %2").arg(value/10.0f, 5,'f',1).arg(STR_UNIT_LPM));
}

void PreferencesDialog::on_maskLeaks20Slider_valueChanged(int value)
{
    ui->maskLeaks20Label->setText(QString("%1 %2").arg(value/10.0f, 5,'f',1).arg(STR_UNIT_LPM));
}

void PreferencesDialog::on_calculateUnintentionalLeaks_toggled(bool)
{

}

void PreferencesDialog::on_combineSimilarMachines_toggled(bool checked)
{
    ui->includeSerial->setEnabled(!checked);
}

