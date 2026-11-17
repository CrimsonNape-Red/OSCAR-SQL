/* Daily Panel
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 * Copyright (c) 2011-2018 Mark Watkins
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */


#include <QTextCharFormat>
#include <QPalette>
#include <QApplication>
#include <QTimeZone>
#include <QTextBlock>
#include <QColorDialog>
#include <QSpacerItem>
#include <QElapsedTimer>
#include <QBuffer>
#include <QPixmap>
#include <QMessageBox>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSpacerItem>
#include <QFontMetrics>
#include <QPainter>
#include <QPointer>
#include <QScopeGuard>
#include <QTextDocument>
#include <QLabel>
#include <QMutexLocker>
#include <QFile>
#include <QToolButton>

#include <algorithm>
#include <cmath>

#define TEST_MACROS_ENABLEDoff
#include <test_macros.h>

#define CONFIGURE_MODE
#define COMBINE_MODE_3

#include "saveGraphLayoutSettings.h"
#include "daily.h"
#include "ui_daily.h"
#include "dailySearchTab.h"

#include "common_gui.h"
#include "SleepLib/profiles.h"
#include "SleepLib/journal.h"
#include "SleepLib/session.h"
#include "SleepLib/performance_timer.h"
#include "SleepLib/loader_plugins/applehealth_loader.h"
#include "SleepLib/loader_plugins/viatom_loader.h"
#include "database/session_repository.h"

#include "Graphs/gLineOverlay.h"
#include "Graphs/graphtheme.h"
#include "Graphs/gFlagsLine.h"
#include "Graphs/gSleepStageChart.h"
#include "Graphs/gSessionBarLayer.h"
#include "Graphs/gSessionGapLayer.h"
#include "Graphs/gFooBar.h"
#include "Graphs/gXAxis.h"
#include "Graphs/gYAxis.h"
#include "Graphs/gSegmentChart.h"
#include "Graphs/gStatsLine.h"
#include "Graphs/gdailysummary.h"
#include "Graphs/MinutesAtPressure.h"
#include "Graphs/tallcapture.h"
#include "dailytimeaxis.h"
#include "dailytoolbar.h"
#include "dailydetails.h"
#include "dailydetailspanel.h"
#include "welcomemetrics.h"
#include "uikit/theme.h"

extern MainWindow * mainwin;

QString htmlLeftHeader;
QString htmlLeftAHI;
QString htmlLeftMachineInfo;
QString htmlLeftSleepTime;
QString htmlLeftIndices;
QString htmlLeftPieChart = "";
QString htmlLeftNoHours = "";
QString htmlLeftStatistics;
QString htmlLeftOximeter;
QString htmlLeftMachineSettings;
QString htmlLeftSessionInfo;
QString htmlLeftFooter;

extern ChannelID PRS1_PeakFlow;
extern ChannelID WM_rRMV;
extern ChannelID Prisma_ObstructLevel, Prisma_rMVFluctuation, Prisma_rRMV, Prisma_PressureMeasured, Prisma_FlowFull;

#define WEIGHT_SPIN_BOX_DECIMALS 1

// This was Sean Stangl's idea.. but I couldn't apply that patch.
inline QString channelInfo(ChannelID code) {
    return schema::channel[code].fullname()+"\n"+schema::channel[code].description()+"\n("+schema::channel[code].units()+")";
//    return schema::channel[code].fullname()+"\n"+schema::channel[code].description()
}

// Charts displayed on the Daily page are defined in the Daily::Daily constructor.  They consist of some hard-coded charts and a table
// of channel codes for which charts are generated.  If the list of channel codes is changed, the graph order lists below will need to
// be changed correspondingly.
//
// Note that "graph codes" are strings used to identify graphs and are not the same as "channel codes."  The mapping between channel codes
// and graph codes is found in schema.cpp.  (What we here call 'graph cdoes' are called 'lookup codes' in schema.cpp.)
//
//
// List here the graph codes in the order they are to be displayed.
// Do NOT list a code twice, or Oscar will crash when the profile is closed!
//
// Standard graph order
const QList<QString> standardGraphOrder = {
    STR_GRAPH_SleepFlags, STR_GRAPH_FlowRate, STR_GRAPH_Pressure, STR_GRAPH_PressureWave, STR_GRAPH_LeakRate, STR_GRAPH_FlowLimitation,
    STR_GRAPH_Snore, STR_GRAPH_IE_Ratio, STR_GRAPH_FlowAbnormality, STR_GRAPH_TidalVolume, STR_GRAPH_MaskPressure, STR_GRAPH_RespRate, STR_GRAPH_MinuteVent,
    "RMVENT_AlvMinVent", "RMVENT_SpontCyc", "RMVENT_SpontTrig",   // ResMed ventilation (match channel codes)
    STR_GRAPH_PTB, STR_GRAPH_RespEvent, STR_GRAPH_Ti, STR_GRAPH_Te, STR_GRAPH_IE,
    STR_GRAPH_SleepStage, STR_GRAPH_Inclination, STR_GRAPH_Orientation, STR_GRAPH_Motion, STR_GRAPH_TestChan1,
    STR_GRAPH_Oxi_Pulse, STR_GRAPH_Oxi_SPO2, STR_GRAPH_Oxi_Perf, STR_GRAPH_Oxi_Plethy,
    "AWRespRate", "AWHRV",   // Apple Watch (match channel codes)
    STR_GRAPH_AHI, STR_GRAPH_TAP, STR_GRAPH_ObstructLevel, STR_GRAPH_PressureMeasured, STR_GRAPH_rRMV, STR_GRAPH_rMVFluctuation,
    "WM_rRMV",   // Weinmann relative minute volume (matches the channel code)
    STR_GRAPH_FlowFull
    #if defined(STEADY_BREATHING)
    , STR_GRAPH_CPAP_SteadyBreathing
    #endif
};

// Advanced graph order
const QList<QString> advancedGraphOrder = {
    STR_GRAPH_SleepFlags, STR_GRAPH_FlowRate, STR_GRAPH_PressureWave, STR_GRAPH_MaskPressure, STR_GRAPH_TidalVolume, STR_GRAPH_MinuteVent,
    "RMVENT_AlvMinVent", "RMVENT_SpontCyc", "RMVENT_SpontTrig",   // ResMed ventilation (match channel codes)
    STR_GRAPH_Ti, STR_GRAPH_Te, STR_GRAPH_IE, STR_GRAPH_FlowLimitation, STR_GRAPH_FlowAbnormality, STR_GRAPH_Pressure, STR_GRAPH_LeakRate, STR_GRAPH_Snore,
    STR_GRAPH_IE_Ratio, STR_GRAPH_RespRate, STR_GRAPH_PTB, STR_GRAPH_RespEvent,
    STR_GRAPH_SleepStage, STR_GRAPH_Inclination, STR_GRAPH_Orientation, STR_GRAPH_Motion, STR_GRAPH_TestChan1,
    STR_GRAPH_Oxi_Pulse, STR_GRAPH_Oxi_SPO2, STR_GRAPH_Oxi_Perf, STR_GRAPH_Oxi_Plethy,
    "AWRespRate", "AWHRV",   // Apple Watch (match channel codes)
    STR_GRAPH_AHI, STR_GRAPH_TAP, STR_GRAPH_ObstructLevel, STR_GRAPH_PressureMeasured, STR_GRAPH_rRMV, STR_GRAPH_rMVFluctuation,
    "WM_rRMV",   // Weinmann relative minute volume (matches the channel code)
    STR_GRAPH_FlowFull
    #if defined(STEADY_BREATHING)
    , STR_GRAPH_CPAP_SteadyBreathing
    #endif
};

// CPAP modes that should have Advanced graphs
const QList<int> useAdvancedGraphs = {MODE_ASV, MODE_ASV_VARIABLE_EPAP, MODE_AVAPS};


// QTreeWidgetItem specialization for sorting by timestamp
// Note: This is just used for the consolidated events, but for consistency
// it would make sense to use it for all QTreeWidgetItem's.
class EventTreeWidgetItem : public QTreeWidgetItem {
public:
    EventTreeWidgetItem(const QStringList &strings) : QTreeWidgetItem(strings) {}
    EventTreeWidgetItem(QTreeWidgetItem *parent, const QStringList &strings) : QTreeWidgetItem(parent, strings) {}

    bool operator<(const QTreeWidgetItem &other) const override {
        // Compare timestamps stored in user data
        qint64 thisTime = data(0, Qt::UserRole).toLongLong();
        qint64 otherTime = other.data(0, Qt::UserRole).toLongLong();
        return thisTime < otherTime;
    }
};

void Daily::setCalendarVisible(bool visible)
{
    on_calButton_toggled(visible);
}

void Daily::setSidebarVisible(bool visible)
{
    QList<int> a;


    int panel_width = visible ? AppSetting->dailyPanelWidth() : 0;
    qDebug() << "Daily::setSidebarVisible(): Daily Left Panel Width is " << panel_width;
    a.push_back(panel_width);
    a.push_back(this->width() - panel_width);
    ui->splitter_2->setStretchFactor(1,1);
    ui->splitter_2->setSizes(a);
    ui->splitter_2->setStretchFactor(1,1);
}

#include "combocheckdelegate.h"

Daily::Daily(QWidget *parent,gGraphView * shared)
    :QWidget(parent), ui(new Ui::Daily)
{
    PERF_TIMER_SCOPE("Daily::Daily");
    qDebug() << "Daily::Daily(): Creating new Daily object";
    m_modern = AppSetting->useNewPages();
    ui->setupUi(this);
    m_calendarStyleSheet = ui->calendar->styleSheet();
    m_formPalette = palette();
    m_formPaletteKept = true;
    m_calButtonStyleSheet = ui->calButton->styleSheet();
    applyTheme();

    ui->JournalNotesBold->setShortcut(QKeySequence::Bold);
    ui->JournalNotesItalic->setShortcut(QKeySequence::Italic);
    ui->JournalNotesUnderline->setShortcut(QKeySequence::Underline);

    // Remove Incomplete Extras Tab

    BookmarksChanged=false;

    lastcpapday=nullptr;
    leakchart=nullptr;

    setSidebarVisible(true);

    layout=new QHBoxLayout();
    layout->setSpacing(0);
    layout->setContentsMargins(0,0,0,0);

    dateDisplay=new MyLabel(this);
    dateDisplay->setAlignment(Qt::AlignCenter);
    if (m_modern) {
        // A slim status line under the time axis (placed below, once the graph view exists)
        dateDisplay->setFixedHeight(QFontMetrics(dateDisplay->font()).height() + 6);
        applyTheme();   // its text colour
    } else {
        QFont font = dateDisplay->font();
        font.setPointSizeF(font.pointSizeF()*1.3F);
        dateDisplay->setFont(font);
        QPalette palette = dateDisplay->palette();
        palette.setColor(QPalette::Base, Qt::blue);
        dateDisplay->setPalette(palette);
        ui->sessionBarLayout->addWidget(dateDisplay,1);
    }

//    const bool sessbar_under_graphs=false;
//    } else {


    ui->calNavWidget->setMaximumHeight(ui->calNavWidget->height());
    ui->calNavWidget->setMinimumHeight(ui->calNavWidget->height());
    QWidget *widget = new QWidget(ui->tabWidget);
    sessionbar = new SessionBar(widget);
    sessionbar->setMinimumHeight(25);
    sessionbar->setMouseTracking(true);
    connect(sessionbar, SIGNAL(sessionClicked(Session*)), this, SLOT(doToggleSession(Session*)));
    QVBoxLayout *layout2 = new QVBoxLayout();
    layout2->setContentsMargins(0, 0, 0, 0);
    widget->setLayout(layout2);

    webView=new MyTextBrowser(widget);
    webView->setOpenLinks(false);
    layout2->insertWidget(0,webView, 1);
    layout2->insertWidget(1,sessionbar,0);
    // add the sessionbar after it.

    if (m_modern) {
        // Modern: the Details tab shows cards. The HTML page is still filled (hidden), since
        // printed daily reports read it (GetDetailsText()).
        m_detailsPanel = new DailyDetailsPanel(widget);
        m_detailsPanel->setCardsOpen(AppSetting->dailyCardsOpen());
        layout2->insertWidget(0, m_detailsPanel, 1);
        webView->hide();
        connect(m_detailsPanel, &DailyDetailsPanel::eventClicked, this, [this](ChannelID code) {
            Link_clicked(QUrl(QString("event=%1").arg(code)));
        });
        connect(m_detailsPanel, &DailyDetailsPanel::cardToggled, this, [this] {
            AppSetting->setDailyCardsOpen(m_detailsPanel->cardsOpen());
        });
        connect(m_detailsPanel, &DailyDetailsPanel::sessionToggled, this, &Daily::doToggleSession);
        sessionbar->setThemed(true);
    }

    ui->tabWidget->insertTab(0, widget, QIcon(), tr("Details"));

    ui->graphFrame->setLayout(layout);

    ui->graphMainArea->setAutoFillBackground(false);

    PERF_TIMER_START("Daily::Daily::Phase 3");

    PERF_TIMER_START("Daily::Daily::Phase 3D");
    GraphView=new gGraphView(ui->graphFrame,shared);
    GraphView->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    // Modern: the graphs are outlined cards, as on Modern Overview. Set before the graphs are
    // made. The pie chart's view (snapGV, below) keeps the Classic look: its image is shown
    // without a frame or title.
    if (m_modern) {
        GraphView->setLook(GraphLook::Card);
        // One time axis under the graphs, outside their scrolling list, then the status line
        GraphView->setSharedXAxis(true);
        m_timeAxis = new DailyTimeAxis(GraphView, ui->layoutWidget);
        const int at = ui->verticalLayout_3->indexOf(ui->graphMainArea) + 1;
        ui->verticalLayout_3->insertWidget(at, m_timeAxis);
        ui->verticalLayout_3->insertWidget(at + 1, dateDisplay);

        // The top bar takes over the drop-downs and buttons of the bar under the graphs
        m_toolbar = new DailyToolbar(ui->layoutWidget);
        ui->verticalLayout_3->insertWidget(0, m_toolbar);
        m_toolbar->adoptCombos(ui->eventsCombo, ui->graphCombo);
        ui->frame->hide();
        connect(m_toolbar, &DailyToolbar::layoutClicked, this, &Daily::on_layout_clicked);
        connect(m_toolbar, &DailyToolbar::helpClicked, this, &Daily::on_graphHelp_clicked);
    }

    GraphView->setEmptyImage(QPixmap(":/icons/logo-md.png"));
    PERF_TIMER_STOP("Daily::Daily::Phase 3D");

    snapGV=new gGraphView(GraphView);
    snapGV->setMinimumSize(172,172);
    snapGV->hideSplitter();
    snapGV->hide();
    scrollbar=new MyScrollBar(ui->graphFrame);
    scrollbar->setOrientation(Qt::Vertical);
    scrollbar->setSizePolicy(QSizePolicy::Maximum,QSizePolicy::Expanding);
    scrollbar->setMaximumWidth(20);

    ui->bookmarkTable->setColumnCount(2);
    ui->bookmarkTable->setColumnWidth(0,70);
    GraphView->setScrollBar(scrollbar);
    layout->addWidget(GraphView,1);
    layout->addWidget(scrollbar,0);

    PERF_TIMER_STOP("Daily::Daily::Phase 3");

    int default_height = AppSetting->graphHeight();

    gGraph *GAHI = nullptr,
//            *TAP = nullptr,
            *SF = nullptr,
            *AHI = nullptr;

//    const QString STR_GRAPH_DailySummary = "DailySummary";

//    gGraph * SG;

    graphlist[STR_GRAPH_SleepFlags] = SF = new gGraph(STR_GRAPH_SleepFlags, GraphView, STR_TR_EventFlags, STR_TR_EventFlags, default_height);
    sleepFlags = SF;
    SF->setPinned(true);

    // Build union of channels with actual data across all loaded machines.
    // Empty set (no machines/data loaded) means all channel graphs will be created.
    QSet<ChannelID> availableChannels;
    for (auto * mach : p_profile->m_machlist) {
        for (ChannelID code : mach->availableChannels(0)) {
            availableChannels.insert(code);
        }
    }

    //============================================
    // Create graphs from 'interesting' CPAP codes
    //
    // If this list of codes is changed, you must
    // also adjust the standard and advanced graph
    // order at the beginning of daily.cpp.
    //============================================
    const ChannelID cpapcodes[] = {
        CPAP_FlowRate, CPAP_Pressure, CPAP_Leak, CPAP_FLG, CPAP_Snore, CPAP_TidalVolume,
        CPAP_MaskPressure, CPAP_RespRate, CPAP_MinuteVent, CPAP_PTB, PRS1_PeakFlow, CPAP_RespEvent, CPAP_Ti, CPAP_Te,
        CPAP_IE, SLEEP_Stage, POS_Inclination, POS_Orientation, POS_Movement, CPAP_Test1,
        Prisma_ObstructLevel, Prisma_rRMV, Prisma_rMVFluctuation, Prisma_PressureMeasured, Prisma_FlowFull
        ,  WM_rRMV
        ,  BMC_PressureWave, BMC_FlowAbnormality, BMC_IE_Ratio
        ,  RMVENT_AlvMinVent, RMVENT_SpontCyc, RMVENT_SpontTrig
        #if defined(STEADY_BREATHING)
        ,    CPAP_SteadyBreathing
        #endif

    };

    // Create graphs from the cpap code list
    int cpapsize = sizeof(cpapcodes) / sizeof(ChannelID);

    for (int i=0; i < cpapsize; ++i) {
        ChannelID code = cpapcodes[i];
        if (!availableChannels.isEmpty() && !availableChannels.contains(code)) continue;
        QString cpap_label = schema::channel[code].code();
        DEBUGXD O(i) O(code) O(cpap_label);
        if (graphlist.contains(cpap_label)) {
            // Ignores duplicates labels (e.g., Prisma ones undefined in tests).
            // Otherwise, it leads to segmentation faults during cleanup.
            qDebug() << "Ignoring duplicate code at offset" << i << code << cpap_label;
            continue;
        }
        graphlist[schema::channel[code].code()] = new gGraph(schema::channel[code].code(), GraphView, schema::channel[code].label(), channelInfo(code), default_height);
    }

    const ChannelID oximetercodes[] = {
        OXI_Pulse, OXI_SPO2, OXI_Perf, OXI_Plethy, AW_RespRate, AW_HRV
    };

    // Add graphs from the Oximeter code list
    int oxisize = sizeof(oximetercodes) / sizeof(ChannelID);

    //int oxigrp=p_profile->ExistsAndTrue("SyncOximetry") ? 0 : 1; // Contemplating killing this setting...
    for (int i=0; i < oxisize; ++i) {
        ChannelID code = oximetercodes[i];
        if (!availableChannels.isEmpty() && !availableChannels.contains(code)) continue;
        graphlist[schema::channel[code].code()] = new gGraph(schema::channel[code].code(), GraphView, schema::channel[code].label(), channelInfo(code), default_height);
    }

    // Check for some impossible conditions
    if ( p_profile == nullptr ) {
        qDebug() << "In daily, p_profile is NULL";
        return;
    }
    else if (p_profile->general == nullptr ) {
        qDebug() << "In daily, p_profile->general is NULL";
        return;
    }

    // Decide whether we are using AHI or RDI and create graph for the one we are using
    if (p_profile->general->calculateRDI()) {
        AHI=new gGraph(STR_GRAPH_AHI, GraphView,STR_TR_RDI, channelInfo(CPAP_RDI), default_height);
    } else {
        AHI=new gGraph(STR_GRAPH_AHI, GraphView,STR_TR_AHI, channelInfo(CPAP_AHI), default_height);
    }

    graphlist[STR_GRAPH_AHI] = AHI;

    // Event breakdown graph
    graphlist[STR_GRAPH_EventBreakdown] = GAHI = new gGraph(STR_GRAPH_EventBreakdown, snapGV,tr("Breakdown"),tr("events"),172);
    gSegmentChart * evseg=new gSegmentChart(GST_Pie);
    evseg->AddSlice(CPAP_Hypopnea,QColor(0x40,0x40,0xff,0xff),STR_TR_H);
    evseg->AddSlice(CPAP_ObstructiveHypopnea,QColor(0x30,0x90,0xd0,0xff),STR_TR_OH);
    evseg->AddSlice(CPAP_CentralHypopnea,QColor(0xd0,0x60,0xd0,0xff),STR_TR_CH);
    evseg->AddSlice(CPAP_Apnea,QColor(0x20,0x80,0x20,0xff),STR_TR_UA);
    evseg->AddSlice(CPAP_Obstructive,QColor(0x40,0xaf,0xbf,0xff),STR_TR_OA);
    evseg->AddSlice(CPAP_ClearAirway,QColor(0xb2,0x54,0xcd,0xff),STR_TR_CA);
    evseg->AddSlice(CPAP_AllApnea,QColor(0x40,0xaf,0xbf,0xff),STR_TR_A);
    evseg->AddSlice(CPAP_RERA,QColor(0xff,0xff,0x80,0xff),STR_TR_RE);
    evseg->AddSlice(CPAP_NRI,QColor(0x00,0x80,0x40,0xff),STR_TR_NR);
    evseg->AddSlice(CPAP_FlowLimit,QColor(0x40,0x40,0x40,0xff),STR_TR_FL);
    evseg->AddSlice(CPAP_SensAwake,QColor(0x40,0xC0,0x40,0xff),STR_TR_SA);
    if (AppSetting->userEventPieChart()) {
        evseg->AddSlice(CPAP_UserFlag1,QColor(0xe0,0xe0,0xe0,0xff),tr("UF1"));
        evseg->AddSlice(CPAP_UserFlag2,QColor(0xc0,0xc0,0xe0,0xff),tr("UF2"));
    }

    GAHI->AddLayer(evseg);
    GAHI->setMargins(0,0,0,0);

    // Add event flags to the event flags graph
    gFlagsGroup *fg=new gFlagsGroup();
    sleepFlagsGroup = fg;
    SF->AddLayer(fg);

    SF->setBlockZoom(true);
    SF->AddLayer(new gShadowArea());
    SF->AddLayer(new gLabelArea(fg),LayerLeft,gYAxis::Margin);
    SF->AddLayer(new gXAxis(COLOR_Text,false),LayerBottom,0,gXAxis::Margin);


    // Now take care of xgrid/yaxis labels for all graphs

    // The following list contains graphs that don't have standard xgrid/yaxis labels
    QStringList skipgraph;
    skipgraph.push_back(STR_GRAPH_EventBreakdown);
    skipgraph.push_back(STR_GRAPH_SleepFlags);
    skipgraph.push_back(STR_GRAPH_TAP);
    skipgraph.push_back(STR_GRAPH_SleepStage);

    QHash<QString, gGraph *>::iterator it;

    for (it = graphlist.begin(); it != graphlist.end(); ++it) {
        if (skipgraph.contains(it.key())) continue;
        it.value()->AddLayer(new gXGrid());
    }


    gLineChart *l = new gLineChart(CPAP_FlowRate,false,false);
    if (gGraph *FRW = graphlist.value(schema::channel[CPAP_FlowRate].code())) {
        FRW->AddLayer(l);
        l->setMinimumHeight(80);      // set the layer height to 80. or about 130 graph height.
    } else {
        delete l; l = nullptr;
    }

    bool square=AppSetting->squareWavePlots();
    gLineChart *pc = nullptr;
    if (gGraph *g = graphlist.value(schema::channel[CPAP_Pressure].code())) {
        pc = new gLineChart(CPAP_Pressure, square);
        g->AddLayer(pc);
        pc->addPlot(CPAP_EPAP, square);
        pc->addPlot(CPAP_IPAPLo, square);
        pc->addPlot(CPAP_IPAP, square);
        pc->addPlot(CPAP_IPAPHi, square);
        pc->addPlot(CPAP_EEPAP, square);
        pc->addPlot(CPAP_PressureSet, false);
        pc->addPlot(CPAP_EPAPSet, false);
        pc->addPlot(CPAP_IPAPSet, false);
    }

    // Create Timea at Pressure graph
    gGraph * TAP2;
    graphlist[STR_GRAPH_TAP] = TAP2 = new gGraph(STR_GRAPH_TAP, GraphView, tr("Time at Pressure"), tr("Time at Pressure"), default_height);
    MinutesAtPressure * map;
    TAP2->AddLayer(map = new MinutesAtPressure());
    TAP2->AddLayer(new gLabelArea(map),LayerLeft,gYAxis::Margin);
    TAP2->AddLayer(new gXAxisPressure(),LayerBottom,gXAxisPressure::Margin);
    TAP2->setBlockSelect(true);

    // Fill in the AHI graph
    if (p_profile->general->calculateRDI()) {
        AHI->AddLayer(new gLineChart(CPAP_RDI, square));
    } else {
        AHI->AddLayer(new gLineChart(CPAP_AHI, square));
    }

    // this is class wide because the leak redline can be reset in preferences..
    // Better way would be having a search for linechart layers in graphlist[...]
    if (gGraph *g = graphlist.value(schema::channel[CPAP_Leak].code())) {
        leakchart=new gLineChart(CPAP_Leak, square);
        leakchart->addPlot(CPAP_LeakTotal, square);
        leakchart->addPlot(CPAP_MaxLeak, square);
        g->AddLayer(leakchart);
    }
    if (auto *g = graphlist.value(schema::channel[CPAP_Snore].code())) g->AddLayer(new gLineChart(CPAP_Snore, true));
    if (auto *g = graphlist.value(schema::channel[BMC_FlowAbnormality].code())) g->AddLayer(new gLineChart(BMC_FlowAbnormality, square));
    if (auto *g = graphlist.value(schema::channel[BMC_PressureWave].code())) g->AddLayer(new gLineChart(BMC_PressureWave, square));
    if (auto *g = graphlist.value(schema::channel[BMC_IE_Ratio].code())) g->AddLayer(new gLineChart(BMC_IE_Ratio, square));
    if (auto *g = graphlist.value(schema::channel[CPAP_PTB].code())) g->AddLayer(new gLineChart(CPAP_PTB, square));
    if (auto *g = graphlist.value(schema::channel[PRS1_PeakFlow].code())) g->AddLayer(new gLineChart(PRS1_PeakFlow, square));

    if (auto *g = graphlist.value(schema::channel[Prisma_ObstructLevel].code())) g->AddLayer(new gLineChart(Prisma_ObstructLevel, square));
    if (auto *g = graphlist.value(schema::channel[Prisma_PressureMeasured].code())) g->AddLayer(new gLineChart(Prisma_PressureMeasured, square));
    if (auto *g = graphlist.value(schema::channel[Prisma_rRMV].code())) g->AddLayer(new gLineChart(Prisma_rRMV, square));
    if (auto *g = graphlist.value(schema::channel[Prisma_rMVFluctuation].code())) g->AddLayer(new gLineChart(Prisma_rMVFluctuation, square));
    if (auto *g = graphlist.value(schema::channel[Prisma_FlowFull].code())) g->AddLayer(new gLineChart(Prisma_FlowFull, square));
    if (auto *g = graphlist.value(schema::channel[WM_rRMV].code())) g->AddLayer(new gLineChart(WM_rRMV, square));
    #if defined(STEADY_BREATHING)
    if (p_profile->cpap->steadyBreathing()==SB_ACTIVE) {
        if (auto *g = graphlist.value(schema::channel[CPAP_SteadyBreathing].code())) g->AddLayer(new gLineChart(CPAP_SteadyBreathing, false));
    }
    #endif
    if (auto *g = graphlist.value(schema::channel[CPAP_Test1].code())) g->AddLayer(new gLineChart(CPAP_Test1, square));


    gLineChart *lc = nullptr;
    if (auto *g = graphlist.value(schema::channel[CPAP_MaskPressure].code())) g->AddLayer(new gLineChart(CPAP_MaskPressure, false));
    if (auto *g = graphlist.value(schema::channel[CPAP_RespRate].code())) g->AddLayer(lc=new gLineChart(CPAP_RespRate, square));

    if (auto *g = graphlist.value(schema::channel[POS_Inclination].code())) g->AddLayer(new gLineChart(POS_Inclination));
    if (auto *g = graphlist.value(schema::channel[POS_Orientation].code())) g->AddLayer(new gLineChart(POS_Orientation));
    if (auto *g = graphlist.value(schema::channel[POS_Movement].code())) {
        g->AddLayer(new gLineChart(POS_Movement));
        g->AddLayer(new gSessionBarLayer());
    }

    if (auto *g = graphlist.value(schema::channel[CPAP_MinuteVent].code())) {
        g->AddLayer(lc=new gLineChart(CPAP_MinuteVent, square));
        lc->addPlot(CPAP_TgMV, square);
    }

    if (auto *g = graphlist.value(schema::channel[CPAP_TidalVolume].code())) {
        g->AddLayer(lc=new gLineChart(CPAP_TidalVolume, square));
    }

    if (auto *g = graphlist.value(schema::channel[CPAP_FLG].code())) g->AddLayer(new gLineChart(CPAP_FLG, true));
    if (auto *g = graphlist.value(schema::channel[CPAP_IE].code())) g->AddLayer(lc=new gLineChart(CPAP_IE, false));      // this should be inverse of supplied value
    if (auto *g = graphlist.value(schema::channel[CPAP_Te].code())) g->AddLayer(lc=new gLineChart(CPAP_Te, false));
    if (auto *g = graphlist.value(schema::channel[CPAP_Ti].code())) g->AddLayer(lc=new gLineChart(CPAP_Ti, false));

    // ResMed bilevel/iVAPS ventilation waveforms
    if (auto *g = graphlist.value(schema::channel[RMVENT_AlvMinVent].code())) g->AddLayer(new gLineChart(RMVENT_AlvMinVent, square));
    if (auto *g = graphlist.value(schema::channel[RMVENT_SpontCyc].code())) g->AddLayer(new gLineChart(RMVENT_SpontCyc, square));
    if (auto *g = graphlist.value(schema::channel[RMVENT_SpontTrig].code())) g->AddLayer(new gLineChart(RMVENT_SpontTrig, square));

    if (auto *g = graphlist.value(schema::channel[SLEEP_Stage].code())) {
        gSleepStageChart *ss = new gSleepStageChart(SLEEP_Stage);
        g->AddLayer(ss);
        g->AddLayer(new gLabelArea(ss), LayerLeft, gYAxis::Margin);
        g->AddLayer(new gXAxis(), LayerBottom, 0, gXAxis::Margin);
    }


    if (auto *g = graphlist.value(schema::channel[OXI_Pulse].code())) {
        g->AddLayer(new gLineChart(OXI_Pulse, square));
        g->AddLayer(new gSessionBarLayer());
    }
    if (auto *g = graphlist.value(schema::channel[OXI_SPO2].code())) {
        g->AddLayer(new gLineChart(OXI_SPO2, true));
        g->AddLayer(new gSessionBarLayer());
    }
    if (auto *g = graphlist.value(schema::channel[OXI_Perf].code())) g->AddLayer(new gLineChart(OXI_Perf, false));
    if (auto *g = graphlist.value(schema::channel[OXI_Plethy].code())) g->AddLayer(new gLineChart(OXI_Plethy, false));
    if (auto *g = graphlist.value(schema::channel[AW_RespRate].code())) g->AddLayer(new gLineChart(AW_RespRate, square));
    if (auto *g = graphlist.value(schema::channel[AW_HRV].code())) g->AddLayer(new gLineChart(AW_HRV, square));


    // Fix me

    if (auto *g = graphlist.value(schema::channel[CPAP_PTB].code())) g->setForceMaxY(100);
    if (auto *g = graphlist.value(schema::channel[OXI_SPO2].code())) g->setForceMaxY(100);

    for (it = graphlist.begin(); it != graphlist.end(); ++it) {
        if (skipgraph.contains(it.key())) continue;

        it.value()->AddLayer(new gYAxis(),LayerLeft,gYAxis::Margin);
        it.value()->AddLayer(new gXAxis(),LayerBottom,0,gXAxis::Margin);
    }

    if (m_modern) {
        // Hatch the time between sessions on every graph of the night's time, beneath the
        // data (first in the layer list). Not Time at Pressure, whose scale is pressure.
        for (gGraph *g : std::as_const(graphlist)) {
            const QVector<Layer *> &layers = g->layers();
            const bool timeScale = std::any_of(layers.begin(), layers.end(),
                                               [](Layer *l) { return dynamic_cast<gXAxis *>(l) != nullptr; });
            if (!timeScale) continue;
            g->AddLayer(new gSessionGapLayer());
            g->layers().move(g->layers().size() - 1, 0);
        }
    }

    if (p_profile->cpap->showLeakRedline()) {
        schema::channel[CPAP_Leak].setUpperThreshold(p_profile->cpap->leakRedline());
    } else {
        schema::channel[CPAP_Leak].setUpperThreshold(0); // switch it off
    }

    layout->layout();

    applyWeekendFormat();

    Qt::DayOfWeek dow=firstDayOfWeekFromLocale();

    ui->calendar->setFirstDayOfWeek(dow);

    ui->tabWidget->setCurrentWidget(widget);

    connect(webView,SIGNAL(anchorClicked(QUrl)),this,SLOT(Link_clicked(QUrl)));

    int ews=p_profile->general->eventWindowSize();
    ui->evViewSlider->setValue(ews);
    ui->evViewLCD->display(ews);


    icon_on=new QIcon(":/icons/session-on.png");
    icon_off=new QIcon(":/icons/session-off.png");
    icon_up_down=new QIcon(":/icons/up-down.png");
    icon_warning=new QIcon(":/icons/warning.png");

    auto *comboDelegate = new ComboCheckDelegate(this);
    ui->graphCombo->view()->setItemDelegate(comboDelegate);
    ui->eventsCombo->view()->setItemDelegate(new ComboCheckDelegate(this));

    ui->splitter->setVisible(false);

#ifndef REMOVE_FITNESS
#else // REMOVE_FITNESS
        // hide the parent widget to make the gridlayout invisible
        QWidget *myparent ;
        myparent = ui->bioMetrics->parentWidget();
        if(myparent) myparent->hide();
#endif

    GraphView->setEmptyImage(QPixmap(":/icons/logo-md.png"));
    GraphView->setEmptyText(STR_Empty_NoData);
    previous_date=QDate();

    ui->calButton->setChecked(AppSetting->calendarVisible() ? true : false);
    on_calButton_toggled(AppSetting->calendarVisible());

    GraphView->resetLayout();
    GraphView->SaveDefaultSettings();
    GraphView->LoadSettings("Daily");

    connect(GraphView, SIGNAL(updateCurrentTime(double)), this, SLOT(on_LineCursorUpdate(double)));
    connect(GraphView, SIGNAL(updateRange(double,double)), this, SLOT(on_RangeUpdate(double,double)));
    connect(GraphView, SIGNAL(GraphsChanged()), this, SLOT(updateGraphCombo()));

    // Watch for focusOut events on the JournalNotes widget
    ui->JournalNotes->installEventFilter(this);
    saveGraphLayoutSettings=nullptr;
    dailySearchTab = new DailySearchTab(this,ui->searchTab,ui->tabWidget);
    htmlLsbSectionHeaderInit(false);
#ifndef REMOVE_FITNESS
    set_ZombieUI(0,true);
#endif
}

Daily::~Daily()
{
    disconnect(GraphView, SIGNAL(updateCurrentTime(double)), this, SLOT(on_LineCursorUpdate(double)));
    disconnect(GraphView, SIGNAL(updateRange(double,double)), this, SLOT(on_RangeUpdate(double,double)));
    disconnect(GraphView, SIGNAL(GraphsChanged()), this, SLOT(updateGraphCombo()));

    disconnect(sessionbar, SIGNAL(sessionClicked(Session*)), this, SLOT(doToggleSession(Session*)));
    disconnect(webView,SIGNAL(anchorClicked(QUrl)),this,SLOT(Link_clicked(QUrl)));
    ui->JournalNotes->removeEventFilter(this);

    if (previous_date.isValid()) {
        Unload(previous_date);
    }

    // Save graph orders and pin status, etc...
    GraphView->SaveSettings("Daily");

    // The window holding popped-out graphs (gGraphView::dock) is a child of this page and is
    // deleted with it; forget it, or the next popout would use a deleted window (#388)
    if (gGraphView::dock && gGraphView::dock->parentWidget() == this) {
        gGraphView::dock = nullptr;
    }

    delete ui;
    delete icon_on;
    delete icon_off;
    delete icon_up_down;
    delete icon_warning;
    if (saveGraphLayoutSettings!=nullptr) delete saveGraphLayoutSettings;
    delete dailySearchTab;
}

void Daily::showEvent(QShowEvent *)
{
    RedrawGraphs();
}

bool Daily::rejectToggleSessionEnable( Session*sess) {
    if (!sess) return true;
    if (p_profile->cpap->clinicalMode()) {
       QMessageBox mbox(QMessageBox::Warning, tr("Clinical Mode"), tr(" Disabling Sessions requires Permissive Mode be set in OSCAR Preferences in the Clinical tab."), QMessageBox::Ok  , this);
            mbox.exec();
            return true;
    }
    sess->setEnabled(!sess->enabled());
    return false;
}

void Daily::doToggleSession(Session * sess)
{
    if (rejectToggleSessionEnable( sess) ) return;
    LoadDate(previous_date);
    mainwin->getOverview()->graphView()->dataChanged();
    mainwin->GenerateStatistics();
}

void Daily::Link_clicked(const QUrl &url)
{
    QString code=url.toString().section("=",0,0).toLower();
    QString data=url.toString().section("=",1);
    SessionID sid=data.toUInt();
    Day *day=nullptr;

    if (code=="togglecpapsession") { // Enable/Disable CPAP session
        day=p_profile->GetDay(previous_date,MT_CPAP);
        if (!day) return;
        Session *sess=day->find(sid, MT_CPAP);
        if (rejectToggleSessionEnable( sess) ) return;

        // Reload day
        LoadDate(previous_date);
        mainwin->getOverview()->graphView()->dataChanged();
    } else  if (code=="toggleoxisession") { // Enable/Disable Oximetry session
        day=p_profile->GetDay(previous_date,MT_OXIMETER);
        if (!day) return;
        Session *sess=day->find(sid, MT_OXIMETER);
        if (rejectToggleSessionEnable( sess) ) return;

        // Reload day
        LoadDate(previous_date);
        mainwin->getOverview()->graphView()->dataChanged();
    } else  if (code=="togglestagesession") { // Enable/Disable Sleep Stage session
        day=p_profile->GetDay(previous_date,MT_SLEEPSTAGE);
        if (!day) return;
        Session *sess=day->find(sid, MT_SLEEPSTAGE);
        if (rejectToggleSessionEnable( sess) ) return;
        LoadDate(previous_date);
        mainwin->getOverview()->graphView()->dataChanged();
    } else  if (code=="togglepositionsession") { // Enable/Disable Position session
        day=p_profile->GetDay(previous_date,MT_POSITION);
        if (!day) return;
        Session *sess=day->find(sid, MT_POSITION);
        if (rejectToggleSessionEnable( sess) ) return;
        LoadDate(previous_date);
        mainwin->getOverview()->graphView()->dataChanged();
    } else if (code=="cpap")  {
        day=p_profile->GetDay(previous_date,MT_CPAP);
        if (day) {
            Session *sess=day->machine(MT_CPAP)->sessionlist[sid];
            if (sess && sess->enabled()) {
                GraphView->SetXBounds(sess->first(),sess->last());
            }
        }
    } else if (code=="oxi") {
        day=p_profile->GetDay(previous_date,MT_OXIMETER);
        if (day) {
            Session *sess=day->machine(MT_OXIMETER)->sessionlist[sid];
            if (sess && sess->enabled()) {
                GraphView->SetXBounds(sess->first(),sess->last());
            }
        }

    } else if (code=="event")  {
        QList<QTreeWidgetItem *> list=ui->treeWidget->findItems(schema::channel[sid].fullname(),Qt::MatchContains);
        if (list.size()>0) {
            ui->treeWidget->collapseAll();
            ui->treeWidget->expandItem(list.at(0));
            QTreeWidgetItem *wi=list.at(0)->child(0);
            ui->treeWidget->setCurrentItem(wi);
            ui->tabWidget->setCurrentIndex(1);
        } else {
            mainwin->Notify(tr("No %1 events are recorded on this day").arg(schema::channel[sid].fullname()),"",1500);
        }
    } else if (code=="graph") {
        qDebug() << "Select graph " << data;
    } else if (code=="leftsidebarenable") {
        leftSideBarEnable.toggleBit(data.toInt());
        int bits = 0;
        for (int i = 0; i < leftSideBarEnable.size(); i++)
            if (leftSideBarEnable.testBit(i)) bits |= (1 << i);
        AppSetting->setLeftSideBarEnable(bits);
        LoadDate(previous_date);
    } else {
        qDebug() << "Clicked on" << code << data;
    }
}

void Daily::ReloadGraphs()
{
    PERF_TIMER_SCOPE("ReloadGraphs");

    GraphView->setDay(nullptr);

    ui->splitter->setVisible(true);
    QDate d;

    if (previous_date.isValid()) {
        d=previous_date;
    }
    QDate lastcpap = p_profile->LastDay(MT_CPAP);
    QDate lastoxi = p_profile->LastDay(MT_OXIMETER);

    d = qMax(lastcpap, lastoxi);

    if (!d.isValid()) {
        d=ui->calendar->selectedDate();
    }
    on_calendar_currentPageChanged(d.year(),d.month());
    // this fires a signal which unloads the old and loads the new
    ui->calendar->blockSignals(true);
    ui->calendar->setSelectedDate(d);
    ui->calendar->blockSignals(false);
    Load(d);
    ui->calButton->setText(QLocale().toString(ui->calendar->selectedDate(), MedDateFormat));
    graphView()->redraw();
}

void Daily::updateLeftSidebar() {
    if (webView && !htmlLeftHeader.isEmpty())
        webView->setHtml(getLeftSidebar(true));
    // Modern: the cards are built when the day loads (the pie preference changed: F3)
    if (m_detailsPanel && previous_date.isValid()) LoadDate(previous_date);
}

void Daily::hideSpaceHogs()
{
    if (AppSetting->calendarVisible()) {
        ui->calendarFrame->setVisible(false);
    }
    if (AppSetting->showPieChart()) {
        webView->setHtml(getLeftSidebar(false));
    }
}
void Daily::showSpaceHogs()
{
    if (AppSetting->calendarVisible()) {
        ui->calendarFrame->setVisible(true);
    }
    if (AppSetting->showPieChart()) {
        webView->setHtml(getLeftSidebar(true));
    }
}

bool Daily::tallCaptureAvailable() const
{
    // The tabs can be dragged into another order, so the Details tab is found by its widget
    // (the HTML view's parent, which also holds the Modern cards), not by its index.
    return ui->splitter->isVisible()
           && ui->splitter_2->sizes().value(0) > 0
           && ui->tabWidget->currentWidget() == webView->parentWidget();
}

QPixmap Daily::grabTall()
{
    // The Details tab's scrolling view: the cards (Modern) or the HTML text (Classic)
    QAbstractScrollArea *details = m_detailsPanel ? static_cast<QAbstractScrollArea *>(m_detailsPanel)
                                                  : static_cast<QAbstractScrollArea *>(webView);
    // Layout requests are normally delivered by the event loop. Delivering them here keeps
    // the whole capture in one pass, with nothing painted on screen in between. They are
    // delivered to this page and the widgets in it only: each layout that runs asks its
    // parent to lay out again, and a request reaching the page's own parent would have that
    // layout put the page back to its on-screen size. Children come before their parents
    // (the reverse of findChildren()'s order), so one pass carries a change up to the page.
    // A scroll area shows or hides its scroll bars through a queued call, so the Details
    // view's queued calls are run as well; otherwise a scroll bar no longer needed stays.
    const auto deliverLayouts = [this, details] {
        for (int pass = 0; pass < 3; ++pass) {
            const QList<QWidget *> widgets = findChildren<QWidget *>();
            for (auto it = widgets.crbegin(); it != widgets.crend(); ++it) {
                QApplication::sendPostedEvents(*it, QEvent::LayoutRequest);
            }
            QApplication::sendPostedEvents(this, QEvent::LayoutRequest);
            QApplication::sendPostedEvents(details, QEvent::MetaCall);
        }
    };
    // How much taller the Details view has to be to show all of its contents
    const auto detailsShort = [this, details] {
        const int contents = m_detailsPanel ? m_detailsPanel->contentHeight()
                                            : int(std::ceil(webView->document()->size().height()));
        return qMax(0, contents - details->viewport()->height());
    };
    const auto graphAreaRect = [this] { return ui->graphFrame->contentsRect(); };

    // 1. What is put back afterwards
    const QSize pageSize = size();
    const int scroll = details->verticalScrollBar()->value();
    const QPointer<QWidget> focused = QApplication::focusWidget();
    const bool axisShown = m_timeAxis && !m_timeAxis->isHidden();
    const bool viewShown = !GraphView->isHidden();
    const bool barShown = !scrollbar->isHidden();

    // 2. The graphs: from the top, own time scales, the on-screen scale held (the view must
    //    still be shown here)
    GraphView->beginTallCapture();
    // 3. Modern: the cards open, the shared time scale out of the way
    if (m_detailsPanel) m_detailsPanel->setCaptureOpen(true);
    if (axisShown) m_timeAxis->hide();
    // 4. The graph view is hidden, so the layout leaves it alone and grab() skips it
    GraphView->hide();
    scrollbar->hide();

    const auto restore = qScopeGuard([&] {
        resize(pageSize);
        GraphView->setVisible(viewShown);
        scrollbar->setVisible(barShown);
        if (axisShown) m_timeAxis->show();
        if (m_detailsPanel) m_detailsPanel->setCaptureOpen(false);
        // Back at its own size, the page can be laid out by its parents again
        QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QApplication::sendPostedEvents(details, QEvent::MetaCall);
        QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        details->verticalScrollBar()->setValue(scroll);
        if (focused) focused->setFocus();
        GraphView->endTallCapture();
    });

    deliverLayouts();

    // 5-7. The height that shows the sidebar to its bottom, lengthened so the graph crossing
    //      that line is whole
    const int extra = detailsShort();
    const int needed = graphAreaRect().height() + extra;
    const int graphArea = GraphView->tallCaptureHeight(needed);
    const int wanted = pageSize.height() + extra + (graphArea - needed);
    const int pageHeight = TallCapture::cappedPageHeight(wanted, pageSize.height(), devicePixelRatioF());
    resize(pageSize.width(), pageHeight);
    deliverLayouts();

    // The extra height should all have gone to the Details view and the graph area. Where
    // the layout gave some of it to something else, make up the difference once.
    if (pageHeight == wanted) {
        const int lack = qMax(detailsShort(), graphArea - graphAreaRect().height());
        if (lack > 0) {
            resize(pageSize.width(),
                   TallCapture::cappedPageHeight(pageHeight + lack, pageSize.height(), devicePixelRatioF()));
            deliverLayouts();
        }
    }

    // 8. The page without its graphs, then the graphs painted into their frame
    QPixmap page = grab();
    const QRect inner = graphAreaRect();
    const QRect area(ui->graphFrame->mapTo(this, inner.topLeft()), inner.size());
    if (area.width() > 0 && area.height() > 0) {
        const QPixmap graphs = GraphView->renderTallRasterPixmap(area.size());
        QPainter painter(&page);
        painter.drawPixmap(area.topLeft(), graphs);
    }
    qDebug() << "Daily::grabTall(): page" << pageSize << "drawn at" << size() << "graph area" << area
             << "sidebar lacked" << extra << "graphs wanted" << graphArea;

    // 9. 'restore' puts everything back as this returns
    return page;
}

void Daily::applyTheme()
{
    // The form's style sheets are in light colours. In dark mode the backgrounds are darkened and
    // the text lightened, as on the graphs; the colours marking each day's data keep their hue.
    auto withBackgrounds = [](QString sheet) {
        sheet.replace("background-color: #e0e0e0", "background-color: " + GraphTheme::fill(QColor(0xe0, 0xe0, 0xe0)).name());
        sheet.replace("background-color: white", "background-color: " + GraphTheme::fill(Qt::white).name());
        sheet.replace("background: white", "background: " + GraphTheme::fill(Qt::white).name());
        sheet.replace("color: black", "color: " + GraphTheme::ink(Qt::black).name());
        sheet.replace("color: #808080", "color: " + GraphTheme::ink(QColor(0x80, 0x80, 0x80)).name());
        return sheet;
    };
    applyPagePalette();
    ui->calendar->setStyleSheet(withBackgrounds(m_calendarStyleSheet));
    ui->calButton->setStyleSheet(withBackgrounds(m_calButtonStyleSheet));
    applyWeekendFormat();
    // The Events and Bookmarks lists' alternate rows: in dark mode the desktop's palette can make
    // them a vivid accent colour (Windows uses its darkest accent shade), so they take the theme's
    // row tint. In light mode the form's palette leaves them the same white as the other rows.
    const QString rowTint = Theme::isDark(QGuiApplication::palette())
        ? QString("QAbstractItemView { alternate-background-color: %1; }").arg(Theme::css(Theme::Role::RowTint))
        : QString();
    ui->treeWidget->setStyleSheet(rowTint);
    ui->bookmarkTable->setStyleSheet(rowTint);
    if (dailySearchTab) dailySearchTab->applyTheme();
    if (sessionbar) sessionbar->update();
    if (m_modern && dateDisplay) {
        // The status line under the time axis, in the theme's secondary text colour. Given as
        // a colour rather than through the label's palette, which Qt puts back to an earlier
        // one when the application is restyled (see applyPagePalette()).
        dateDisplay->setTextColor(Theme::color(Theme::Role::Ink2));
    }
    if (m_modern) applyModernStyles();
    if (p_profile) {    // the days' text colours are set when each day is formatted
        on_calendar_currentPageChanged(ui->calendar->yearShown(), ui->calendar->monthShown());
    }
}

void Daily::applyPagePalette()
{
    if (!m_formPaletteKept || m_applyingPalette) return;
    // The form sets a white palette on the whole page, which in dark mode left the Details panel
    // and the other tabs white with black text. In dark mode the page takes the application's
    // colours instead (an empty palette only clears the form's).
    m_applyingPalette = true;
    setPalette(Theme::isDark(QGuiApplication::palette()) ? QPalette() : m_formPalette);
    m_applyingPalette = false;
}

void Daily::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    // After a switch between light and dark, restyling the application put the page's earlier
    // palette back. Nothing showed it on screen in dark mode, but the page drawn on its own
    // for the full-height screenshot had the form's white behind everything.
    if (event->type() == QEvent::PaletteChange) applyPagePalette();
}

void Daily::applyModernStyles()
{
    const QString surface = Theme::css(Theme::Role::Surface);
    const QString line = Theme::css(Theme::Role::Line);
    const QString ink = Theme::css(Theme::Role::Ink);
    const QString ink2 = Theme::css(Theme::Role::Ink2);

    // The selected day and today are outlined, so their own colour and shading stay readable
    ui->calendar->setOutlineSelection(true);

    // The calendar sits in a card
    ui->calendarFrame->setFrameShape(QFrame::NoFrame);
    ui->calendarFrame->setStyleSheet(QString("#calendarFrame { background: %1; border: 1px solid %2; border-radius: 10px; }")
                                         .arg(surface, line));
    ui->verticalLayout_5->setContentsMargins(6, 6, 6, 6);

    // The day bar's buttons: no box until hovered or pressed
    const QString dayButton = QString("QToolButton { background: transparent; border: none; border-radius: 6px; }"
                                      "QToolButton:hover { background: %1; }"
                                      "QToolButton:pressed { background: %2; }")
                                  .arg(Theme::css(Theme::Role::Surface2), Theme::css(Theme::Role::AccentSoft));
    for (QToolButton *button : { ui->prevDayButton, ui->nextDayButton, ui->todayButton }) {
        button->setStyleSheet(dayButton);
    }

    // The tab bar, and the Events, Notes, Bookmarks and Search tabs: the card background and
    // theme text colours only (their layout and controls are unchanged)
    const QString pages = "#events, #notes, #bookmarkTab, #searchTab";
    const auto inPages = [&](const QString &selector) {
        QStringList out;
        for (const QString &page : pages.split(", ")) out << page + " " + selector;
        return out.join(", ");
    };
    ui->tabWidget->setStyleSheet(QString(
        "QTabWidget::pane { border: 1px solid %2; border-radius: 6px; background: %1; top: -1px; }"
        "QTabBar::tab { background: transparent; color: %4; padding: 4px 8px; border: none;"
        " border-bottom: 2px solid transparent; }"
        "QTabBar::tab:selected { color: %3; border-bottom: 2px solid %5; }"
        "QTabBar::tab:hover:!selected { color: %3; }"
        "%6 { background: %1; }"
        "%7 { background: %1; color: %3; }"
        "%8 { color: %3; }")
        .arg(surface, line, ink, ink2, Theme::css(Theme::Role::Accent), pages,
             inPages("QTreeWidget") + ", " + inPages("QTextEdit") + ", " + inPages("QTableWidget"),
             inPages("QLabel")));
}

void Daily::applyWeekendFormat()
{
    // Weekends in the same colour as weekdays (QCalendarWidget makes them red by default)
    QTextCharFormat format = ui->calendar->weekdayTextFormat(Qt::Saturday);
    format.setForeground(QBrush(GraphTheme::ink(COLOR_Black), Qt::SolidPattern));
    ui->calendar->setWeekdayTextFormat(Qt::Saturday, format);
    ui->calendar->setWeekdayTextFormat(Qt::Sunday, format);
}

void Daily::on_calendar_currentPageChanged(int year, int month)
{
    QDate d(year,month,1);
    int dom=d.daysInMonth();

    for (int i=1;i<=dom;i++) {
        d=QDate(year,month,i);
        this->UpdateCalendarDay(d);
    }
}

void Daily::UpdateEventsTree(QTreeWidget *tree,Day *day)
{
    PERF_TIMER_SCOPE("Daily::UpdateEventsTree");
    DEBUGXD O("Daily::UpdateEventsTree") O(tree) O(day);
    tree->clear();
    if (!day) return;

    tree->setColumnCount(1); // 1 visible common.. (1 hidden)
    // Disable automatic sorting of the top-level items
    tree->setSortingEnabled(false);

    QTreeWidgetItem *root=nullptr;
    QHash<ChannelID,QTreeWidgetItem *> mcroot;
    QHash<ChannelID,int> mccnt;
    QList<EventTreeWidgetItem> all_events;

    qint64 drift=0;
    // Get post-context preference for event display
    double event_post_context_secs=p_profile->cpap->eventPostcontext();
    DEBUGXD Q(event_post_context_secs);

    quint32 chantype = schema::FLAG | schema::SPAN | schema::MINOR_FLAG;
    if (p_profile->general->showUnknownFlags()) chantype |= schema::UNKNOWN;
    QList<ChannelID> chans = day->getSortedMachineChannels(chantype);

    int sessnum = 0;
    // Go through all the enabled sessions of the day
    qint64 max_t_post_context = 0;
    for (QList<Session *>::iterator s=day->begin();s!=day->end();++s) {
        Session * sess = *s;
        if (!sess->enabled()) {
            qDebug() << "Daily::UpdateEventsTree() session" << sessnum << sess->session() << "starts"
                     << QDateTime::fromSecsSinceEpoch(sess->first()/1000).toString("yyyy-MM-dd HH:mm:ss") << "is not enabled";
            continue;
        }

        // For each session, go through all the channels
        QHash<ChannelID,QVector<EventList *> >::iterator m;
         for (int c=0; c < chans.size(); ++c) {
            ChannelID code = chans.at(c);
            m = sess->eventlist.find(code);
            if (m == sess->eventlist.end()) continue;

            drift=sess->correctionMs();

            // Prepare title for this code, if there are any events
            QTreeWidgetItem *mcr;
            if (mcroot.find(code)==mcroot.end()) {
                EventDataType fCnt = day->count(code);
                int cnt=fCnt;
//                    qDebug() << "Daily::UpdateEventsTree() counting for" << code << "got" << cnt << "(int) events, EventDataType value for fCnt"
//                    qDebug() << "UpdateEventsTree session" << ++sessnum << sess->session() << "type" << sess->machine()->type() << "starts"//                           << QDateTime::fromSecsSinceEpoch(sess->first()/1000).toString("yyyy-MM-dd HH:mm:ss");
                if (!cnt) continue; // If no events than don't bother showing..
                QString st=schema::channel[code].fullname();
                if (st.isEmpty())  {
                    st=QString("Fixme %1").arg(code);
                }
                st+=" ";
                if (cnt==1) st+=tr("%1 event").arg(cnt);
                else st+=tr("%1 events").arg(cnt);

                QStringList l(st);
                l.append("");
                mcroot[code]=mcr=new QTreeWidgetItem(root,l);
                mccnt[code]=0;
            } else {
                mcr=mcroot[code];
            }

            // number of digits required for count depends on total for day
            int numDigits = ceil(log10(day->count(code)+1));

            // Now we go through the event list for the *session* (not for the day)
            for (int z=0;z<m.value().size();z++) {
                EventList & ev=*(m.value()[z]);

                for (quint32 o=0;o<ev.count();o++) {
                    qint64 t=ev.time(o)+drift;

                    if ((code == CPAP_CSR) || (code == CPAP_PB)) { // center it in the middle of span
                        t -= float(ev.raw(o) / 2.0) * 1000.0;
                    }
                    // Add in postcontext to event display (e.g., 30 seconds)
                    // Note: a no-op by default (i.e., t_post_context = t)
                    qint64 t_post_context = t + (event_post_context_secs * 1000.0);
                    max_t_post_context = max(t_post_context, max_t_post_context);

                    QStringList a, a_all;
                    QDateTime d=QDateTime::fromMSecsSinceEpoch(t); // Localtime
                    QString s=QString("#%1: %2").arg((int)(++mccnt[code]),(int)numDigits,(int)10,QChar('0')).arg(d.toString("HH:mm:ss"));
                    QString s_all=QString("%1").arg(d.toString("HH:mm:ss"));
                    if (m.value()[z]->raw(o) > 0) {
                        QString duration_spec = QString(" (%3)").arg(m.value()[z]->raw(o));
                        s += duration_spec;
                        s_all += duration_spec;
                    }

                    a.append(s);
                    QTreeWidgetItem *item=new QTreeWidgetItem(a);
                    item->setData(0,Qt::UserRole,t_post_context);
                    mcr->addChild(item);

                    // Similarly add entry for consolidated node
                    if (p_profile->cpap->consolidateEvents()) {
                        s_all += " " + schema::channel[code].label();
                        a_all.append(s_all);
                        EventTreeWidgetItem item_all=EventTreeWidgetItem(a_all);
                        item_all.setData(0,Qt::UserRole,t_post_context);
                        all_events.append(item_all);
                    }
                }
            }
        }
    }
    int cnt=0;
    for (QHash<ChannelID,QTreeWidgetItem *>::iterator m=mcroot.begin();m!=mcroot.end();m++) {
        tree->insertTopLevelItem(cnt++,m.value());
    }
    qDebug() << "Daily::UpdateEventsTree(): max_t_post_context:" << max_t_post_context;

    // Sort the top-level nodes (i.e., event types)
    // note: Done here so that UA occurs before Session Start/End
    tree->sortByColumn(0,Qt::AscendingOrder);

    if (day->hasMachine(MT_CPAP) || day->hasMachine(MT_OXIMETER) || day->hasMachine(MT_POSITION)) {
        QTreeWidgetItem * start = new QTreeWidgetItem(QStringList(tr("Session Start Times")),eventTypeStart);
        QTreeWidgetItem * end = new QTreeWidgetItem(QStringList(tr("Session End Times")),eventTypeEnd);
        tree->insertTopLevelItem(cnt++ , start);
        tree->insertTopLevelItem(cnt++ , end);
        for (QList<Session *>::iterator s=day->begin(); s!=day->end(); ++s) {
            Session* sess = *s;
            if ( (sess->type() != MT_CPAP) && (sess->type() != MT_OXIMETER) && (sess->type() != MT_POSITION)) continue;
            QDateTime st = QDateTime::fromMSecsSinceEpoch(sess->first()); // Localtime
            QDateTime et = QDateTime::fromMSecsSinceEpoch(sess->last()); // Localtime

            QTreeWidgetItem * item = new QTreeWidgetItem(QStringList(st.toString("HH:mm:ss")));
            item->setData(0,Qt::UserRole, sess->first());
            start->addChild(item);

            item = new QTreeWidgetItem(QStringList(et.toString("HH:mm:ss")));
            item->setData(0,Qt::UserRole, sess->last());
            end->addChild(item);
        }
    }

    // Add consolidated list
    // Note: The test for consolidation is based on debugging output, so make
    // sure changes here are reflected in tests/eventstabtests.cpp. In addition,
    // mainwin is undefined during tests and access should be guarded elsewhere.
    qDebug() << "Daily::UpdateEventsTree(): consolidate:" << p_profile->cpap->consolidateEvents();
    if (p_profile->cpap->consolidateEvents()) {

        // Sort all items by date (e.g., so CA's interleaved w/ OA's, etc.)
        std::sort(all_events.begin(), all_events.end());

        // Number items to account for timestamp rank
        int numDigits_all=ceil(log10(all_events.count()+1));
        QStringList l_all=QStringList(tr("All"));
        EventTreeWidgetItem *all_numbered_events=new EventTreeWidgetItem(root,l_all);
        for (int event_num = 0; event_num < all_events.count(); event_num++) {
            // ex: "04:15:22 (12)" => "#16: 04:15:22 (12)"
            EventTreeWidgetItem child = all_events.at(event_num);
            QString label = child.text(0);
            QString new_label=QString("#%1: %2").arg((int)event_num + 1,(int)numDigits_all,(int)10,QChar('0')).arg(label);
            qDebug() << "Daily::UpdateEventsTree(): new_label:" << new_label;
            child.setText(0, new_label);
            all_numbered_events->addChild(new EventTreeWidgetItem(child));
        }

        // Add to control
        if (all_numbered_events->childCount()) {
            tree->insertTopLevelItem(cnt++,all_numbered_events);
        }
    }

    // Trace out event-type nodes
    qDebug() << "Daily::UpdateEventsTree(): final top-level items:";
    for (int level = 0; level < tree->topLevelItemCount(); level++) {
        qDebug() << "Daily::UpdateEventsTree(): " << tree->topLevelItem(level)->text(0);
    }
}

/*! \brief The device of a type on a day and its time, e.g. "Löwenstein prisma20A, 6 h 12 min". Plain text.
 *  \return Empty if the day has neither a device to name nor any time.
 */
static QString calendarDeviceText(Day *day, MachineType type)
{
    Machine *machine = day ? day->machine(type) : nullptr;
    QString text;
    if (machine) {
        // Some loaders repeat the brand in the model name ("Viatom Oximeter").
        const QString brand = machine->brand().trimmed();
        const QString model = machine->model().trimmed();
        text = model.startsWith(brand, Qt::CaseInsensitive) ? model : (brand + QLatin1Char(' ') + model).trimmed();
    }
    const int minutes = day ? qRound(day->hours(type) * 60.0) : 0;
    if (minutes > 0) {
        QString duration = minutes < 60 ? Daily::tr("%L1 min").arg(minutes)
                                        : Daily::tr("%L1 h %L2 min").arg(minutes / 60).arg(minutes % 60);
        duration.replace(QLatin1Char(' '), QChar(QChar::Nbsp));  // never wrapped inside
        text = text.isEmpty() ? duration : text + QStringLiteral(", ") + duration;
    }
    return text;
}

/*! \brief A calendar hint line, e.g. "CPAP: Löwenstein prisma20A, 6 h 12 min", or just the label when there
 *  is nothing to name.
 */
static QString calendarDeviceLine(const QString &label, Day *day, MachineType type)
{
    const QString text = calendarDeviceText(day, type);
    //: A calendar tooltip line: a label, then what belongs to it (device and time, a count)
    return text.isEmpty() ? label : Daily::tr("%1: %2").arg(label, text);
}

/*! \brief The calendar's hint for one day: the date, what data the day has, and the legend.
 *  \param hasOxiData  The day has oximetry (SpO2 or pulse), from an oximeter or from another device's sessions.
 *  \param hasMovement The day's CPAP sessions carry movement data.
 *  \param journalLine From Daily::calendarJournalLine(), empty without a journal entry.
 *  \param legend      What each of the day's styles stands for, collected where UpdateCalendarDay() sets them.
 */
static QString calendarDayToolTip(const QDate &date, Day *cpapDay, Day *oxiDay, bool hasOxiData, bool hasMovement,
                                  Day *stageDay, Day *posDay, const QString &journalLine, const QStringList &legend)
{
    QStringList lines;   // plain text, escaped once below
    if (cpapDay) {
        lines << calendarDeviceLine(STR_TR_CPAP, cpapDay, MT_CPAP);
    }
    if (hasOxiData) {
        // Without an oximeter the readings come from another device's sessions of the day.
        lines << (oxiDay ? calendarDeviceLine(STR_TR_Oximetry, oxiDay, MT_OXIMETER) : STR_TR_Oximetry);
    }
    if (hasMovement) {
        lines << schema::channel[POS_Movement].label();  // recorded by the CPAP, whose line names the device
    }
    if (stageDay) {
        lines << calendarDeviceLine(STR_TR_SleepStage, stageDay, MT_SLEEPSTAGE);
    }
    if (posDay) {
        lines << calendarDeviceLine(Daily::tr("Sleep position"), posDay, MT_POSITION);
    }
    if (!journalLine.isEmpty()) {
        lines << journalLine;
    }
    QString html = QStringLiteral("<b>%1</b>").arg(QLocale().toString(date, QLocale::LongFormat).toHtmlEscaped());
    if (lines.isEmpty()) {
        return html + QStringLiteral("<br>") + Daily::tr("No data for this day").toHtmlEscaped();
    }
    for (const QString &line : lines) {
        html += QStringLiteral("<br>") + line.toHtmlEscaped();
    }
    if (!legend.isEmpty()) {
        html += QStringLiteral("<br><i>%1</i>")
                    .arg(Daily::tr("Legend: %1").arg(legend.join(QStringLiteral(", "))).toHtmlEscaped());
    }
    return html;
}

QString Daily::calendarJournalLine(Day *journalDay, int bookmarks)
{
    QStringList parts;
    Session *journal = journalDay->firstSession(MT_JOURNAL);
    if (journal) {
#ifndef REMOVE_FITNESS
        const double kg = journal->settings.value(Journal_Weight).toDouble();
        if (kg > zeroD) {
            parts << tr("weight %1").arg(weightString(kg, p_profile->general->unitSystem()));
        }
        // Stored as 0-100 (0 = not set), shown on the scale the user chose (see setup_ZombieUIWidgets).
        const int feeling = journal->settings.value(Journal_ZombieMeter).toInt();
        if (feeling > 0) {
            parts << (p_profile->appearance->zombieMode()
                          ? tr("feeling %1/100").arg(feeling)
                          : tr("feeling %1/10").arg(QLocale().toString(feeling / 10.0, 'f', 1)));
        }
#endif
        // Unload() removes notes whose editor is empty, so a stored value means there are notes.
        if (!journal->settings.value(Journal_Notes).toString().isEmpty()) {
            //: In a calendar tooltip: the day's journal has notes
            parts << tr("notes");
        }
    }
    if (bookmarks > 0) {
        parts << tr("%1: %2").arg(STR_TR_Bookmarks).arg(bookmarks);
    }
    //: In a calendar tooltip: the day's journal entry holds nothing
    const QString empty = tr("empty");
    return tr("Journal: %1").arg(parts.isEmpty() ? empty : parts.join(QStringLiteral(", ")));
}

// Sets font appearence in calendar to reflect what data is present for that day.
void Daily::UpdateCalendarDay(QDate date)
{
    if (!date.isValid()) {
        return;  // setDateTextFormat() with a null date would clear every day's format
    }
    QFont font = ui->calendar->font();
    font.setPointSize(10);
    ui->calendar->setFont(font);

    QTextCharFormat charAttr;

    Day * cpapDay = p_profile->FindDay(date, MT_CPAP);
    bool hascpap = cpapDay != nullptr;
    Day * oxiDay = p_profile->FindDay(date, MT_OXIMETER);
    const bool hasOxiData = oxiDay != nullptr
                            || (cpapDay && (cpapDay->channelHasData(OXI_SPO2) || cpapDay->channelHasData(OXI_Pulse)));
    const bool hasMovement = cpapDay && cpapDay->channelHasData(POS_Movement);
    bool hasoxi = hasOxiData || hasMovement;
    Day * journalDay = p_profile->FindDay(date, MT_JOURNAL);
    bool hasjournal = journalDay != nullptr;
    Day * stageDay = p_profile->FindDay(date, MT_SLEEPSTAGE);
    bool hasstage = stageDay != nullptr;
    Day * posDay = p_profile->FindDay(date, MT_POSITION);
    bool haspos = posDay != nullptr;

    Session *journal = hasjournal ? journalDay->firstSession(MT_JOURNAL) : nullptr;
    const int bookmarks = journal ? journal->settings.value(Bookmark_Start).toList().size() : 0;
    bool hasbookmarks = bookmarks > 0;

    // The tooltip's legend: each style adds its entry where it is set, so the two cannot drift apart.
    QStringList legend;

    charAttr.setFontWeight(QFont::DemiBold);        // Make everything a little darker

    if (hascpap) {
        if (hasoxi) {
            charAttr.setForeground(QBrush(GraphTheme::ink(COLOR_Red), Qt::SolidPattern)); // CPAP + Oxi
            legend << (hasOxiData ? tr("red = CPAP and oximetry") : tr("red = CPAP and movement data"));
        } else {
            charAttr.setForeground(QBrush(GraphTheme::ink(COLOR_Blue), Qt::SolidPattern)); // CPAP, no Oxi
            legend << tr("blue = CPAP");
        }
        charAttr.setFontWeight(QFont::Black);        // Make everything a little darker
    } else if (hasoxi) {
        charAttr.setForeground(QBrush(GraphTheme::ink(COLOR_DarkGreen), Qt::SolidPattern)); // Oxi, no CPAP
        charAttr.setFontWeight(QFont::Black);        // Make everything a little darker
        legend << tr("green = oximetry");
    }

    if (hasjournal) {
        charAttr.setFontUnderline(true);            // has a journal entry
        legend << tr("underlined = journal entry");
    }

    if (hasbookmarks) {
        charAttr.setFontItalic(true);               // has bookmarks
        legend << tr("italic = bookmarks");
    }

    if (hasstage) {
        charAttr.setBackground(QBrush(GraphTheme::fill(COLOR_BlanchedAlmond), Qt::SolidPattern)); // has staging (pale, low-glare tint; #341)
        legend << tr("shaded background = sleep stages");
    }

    if (haspos) {
        charAttr.setFontOverline(true);            // has sleep position info
        legend << tr("line above = sleep position");
    }

    // Explain the day's data and its calendar styling in a tooltip.
    const QString journalLine = hasjournal ? calendarJournalLine(journalDay, bookmarks) : QString();
    charAttr.setToolTip(calendarDayToolTip(date, cpapDay, oxiDay, hasOxiData, hasMovement, stageDay, posDay,
                                           journalLine, legend));

    ui->calendar->setDateTextFormat(date, charAttr);
/***
    QTextCharFormat nodata;
    QTextCharFormat cpaponly;
    QTextCharFormat cpapjour;
    QTextCharFormat oxiday;
    QTextCharFormat oxicpap;
    QTextCharFormat jourday;
    QTextCharFormat stageday;

    cpaponly.setForeground(QBrush(COLOR_Blue, Qt::SolidPattern));
    cpaponly.setFontWeight(QFont::Normal);
    cpapjour.setForeground(QBrush(COLOR_Blue, Qt::SolidPattern));
    cpapjour.setFontWeight(QFont::Bold);
    cpapjour.setFontUnderline(true);
    oxiday.setForeground(QBrush(COLOR_Red, Qt::SolidPattern));
    oxiday.setFontWeight(QFont::Normal);
    oxicpap.setForeground(QBrush(COLOR_Red, Qt::SolidPattern));
    oxicpap.setFontWeight(QFont::Bold);
    stageday.setForeground(QBrush(COLOR_Magenta, Qt::SolidPattern));
    stageday.setFontWeight(QFont::Bold);
    jourday.setForeground(QBrush(COLOR_DarkYellow, Qt::SolidPattern));
    jourday.setFontWeight(QFont::Bold);
    nodata.setForeground(QBrush(COLOR_Black, Qt::SolidPattern));
    nodata.setFontWeight(QFont::Normal);

    bool hascpap = p_profile->FindDay(date, MT_CPAP)!=nullptr;
    bool hasoxi = p_profile->FindDay(date, MT_OXIMETER)!=nullptr;
    bool hasjournal = p_profile->FindDay(date, MT_JOURNAL)!=nullptr;
    bool hasstage = p_profile->FindDay(date, MT_SLEEPSTAGE)!=nullptr;
    bool haspos = p_profile->FindDay(date, MT_POSITION)!=nullptr;
    if (hascpap) {
        if (hasoxi) {
            ui->calendar->setDateTextFormat(date, oxicpap);
        } else if (hasjournal) {
            ui->calendar->setDateTextFormat(date, cpapjour);
        } else if (hasstage || haspos) {
            ui->calendar->setDateTextFormat(date, stageday);
        } else {
            ui->calendar->setDateTextFormat(date, cpaponly);
        }
    } else if (hasoxi) {
        ui->calendar->setDateTextFormat(date, oxiday);
    } else if (hasjournal) {
        ui->calendar->setDateTextFormat(date, jourday);
    } else if (hasstage) {
        ui->calendar->setDateTextFormat(date, oxiday);
    } else if (haspos) {
        ui->calendar->setDateTextFormat(date, oxiday);
    } else {
        ui->calendar->setDateTextFormat(date, nodata);
    }
***/
    ui->calendar->setHorizontalHeaderFormat(QCalendarWidget::ShortDayNames);
}

// Refreshes the calendar appearance for a list of dates whose data has changed
// outside of the normal load/unload cycle (for example after a range purge).
void Daily::updateCalendarDays(const QList<QDate> &dates)
{
    for (const QDate &date : dates) {
        if (date.isValid())
            UpdateCalendarDay(date);
    }
}

void Daily::LoadDate(QDate date)
{
    DEBUGXD O("Daily::LoadDate") O(date);
    if (!date.isValid()) {
        qDebug() << "Daily::LoadDate(): LoadDate called with invalid date";
        return;
    }
    ui->calendar->blockSignals(true);
    if (date.month()!=previous_date.month()) {
        on_calendar_currentPageChanged(date.year(),date.month());
    }
    ui->calendar->setSelectedDate(date);
    ui->calendar->blockSignals(false);
    on_calendar_selectionChanged();
}

void Daily::on_calendar_selectionChanged()
{
    QTimer::singleShot(0, this, SLOT(on_ReloadDay()));
}

void Daily::on_ReloadDay()
{
    //! \brief True while on_ReloadDay() is running, to log re-entry from nested event processing.
    //! Only touched on the GUI thread, so a plain bool is enough.
    static bool inReload = false;

    if (inReload) {
        qDebug() << "Daily::on_ReloadDay(): attempt to renter on_ReloadDay()";
    }
    inReload = true;
    QElapsedTimer time;
    time_t unload_time, load_time, other_time;
    time.start();

    this->setCursor(Qt::BusyCursor);
    if (previous_date.isValid()) {
        Unload(previous_date);
    }

    unload_time=time.restart();
    Load(ui->calendar->selectedDate());
    load_time=time.restart();

    GraphView->redraw();
    ui->calButton->setText(QLocale().toString(ui->calendar->selectedDate(), MedDateFormat));
    ui->calendar->setFocus(Qt::ActiveWindowFocusReason);

#ifndef REMOVE_FITNESS
    ui->weightSpinBox->setDecimals(WEIGHT_SPIN_BOX_DECIMALS);
#endif
    this->setCursor(Qt::ArrowCursor);
    other_time=time.restart();

    qDebug() << "Daily::on_ReloadDay(): Page change time (in ms): Unload ="<<unload_time<<"Load =" << load_time << "Other =" << other_time;
    inReload = false;
}
void Daily::ResetGraphLayout()
{
    GraphView->resetLayout();
}
void Daily::ResetGraphOrder(int type)
{
    if (type == 0) {  // Auto order
        Day * day = p_profile->GetDay(previous_date,MT_CPAP);

        int cpapMode = day->getCPAPMode();

        if (useAdvancedGraphs.contains(cpapMode))
            GraphView->resetGraphOrder(true, advancedGraphOrder);
        else
            GraphView->resetGraphOrder(true, standardGraphOrder);
    } else if (type == 2) { // Advanced order
        GraphView->resetGraphOrder(true, advancedGraphOrder);
    } else {                // type == 1, standard order
        GraphView->resetGraphOrder(true, standardGraphOrder);
    }

    // Enable all graphs (make them not hidden)
    showAllGraphs(true);
    showAllEvents(true);

    // Reset graph heights (and repaint)
    ResetGraphLayout();
}

void Daily::graphtogglebutton_toggled(bool b)
{
    Q_UNUSED(b)
    for (int i=0;i<GraphView->size();i++) {
        QString title=(*GraphView)[i]->title();
        (*GraphView)[i]->setVisible(GraphToggles[title]->isChecked());
    }
    GraphView->updateScale();
    GraphView->redraw();
}

QString Daily::getSessionInformation(Day * day)
{
    QString html;
    if (!day) return html;

    htmlLsbSectionHeader(html,tr("Session Information"),LSB_SESSION_INFORMATION );
    if (!leftSideBarEnable[LSB_SESSION_INFORMATION] ) {
        html+="<hr/>"; // Have sep at the end of sections.
        return html;
    }
    html+="<table cellpadding=0 cellspacing=0 border=0 width=100%>";
    html+="<tr><td colspan=5 align=center>&nbsp;</td></tr>";

    // The sessions are collected by DailyDetails::sessions()
    for (const DailyDetails::SessionGroup &group : DailyDetails::sessions(day)) {
        QString type;
        switch (group.type) {
            case MT_CPAP: type="cpap"; break;
            case MT_OXIMETER: type="oxi"; break;
            case MT_SLEEPSTAGE: type="stage"; break;
            case MT_POSITION: type="position"; break;
            default: type="unknown"; break;
        }
        html += "<tr><td colspan=5 align=center><i>";
        html += group.heading;
        html+="</i></td></tr>\n";
        html+=QString("<tr>"
                      "<td>"+STR_TR_On+"</td>"
                      "<td>"+STR_TR_Date+"</td>"
                      "<td>"+STR_TR_Start+"</td>"
                      "<td>"+STR_TR_End+"</td>"
                      "<td>"+tr("Duration")+"</td></tr>");

        for (const DailyDetails::SessionRow &row : group.rows) {
            QString tooltip = tr("Click to %1 this session.").arg(row.on ? tr("disable") : tr("enable"));
            html+=QString("<tr><td colspan=5 align=center>%2</td></tr>"
                          "<tr>"
                          "<td width=26>"
#ifdef DITCH_ICONS
                          "[<a href='toggle"+type+"session=%1'><b title='"+tooltip+"'>%4</b></a>]"
#else
                          "<a href='toggle"+type+"session=%1'><img src='qrc:/icons/session-%4.png' title=\""+tooltip+"\"></a>"
#endif
                          "</td>"
                          "<td align=left>%5</td>"
                          "<td align=left>%6</td>"
                          "<td align=left>%7</td>"
                          "<td align=left>%3</td></tr>"
                          )
                    .arg(row.session->session())
                    .arg(row.name)
                    .arg(row.duration)
                    .arg((row.on ? "on" : "off"))
                    .arg(row.date)
                    .arg(row.start)
                    .arg(row.end);



#ifdef SESSION_DEBUG
            for (int i=0; i< row.session->session_files.size(); ++i) {
                html+=QString("<tr><td colspan=5 align=center>%1</td></tr>").arg(row.session->session_files[i].section("/",-1));
            }
#endif
        }
    }

    html+="</table>";
    html+="<hr/>"; // Have sep at the end of sections.
    return html;
}

QString Daily::getMachineSettings(Day * day) {
    QString html;

    // The settings are collected by DailyDetails::settings()
    const DailyDetails::Settings settings = DailyDetails::settings(day);
    if (settings.present) {
        if (settings.loaderMissing) {
            htmlLsbSectionHeader(html,tr("DEVICE SETTINGS ERROR"),LSB_DEVICE_SETTINGS );
            return html;
        }
        htmlLsbSectionHeader(html,tr("Device Settings"),LSB_DEVICE_SETTINGS );
        if (!leftSideBarEnable[LSB_DEVICE_SETTINGS] ) {
            return html;
        }
        html+="<table cellpadding=0 cellspacing=0 border=0 width=100%>";
        if (settings.assumed) {
            html+="<tr><td colspan=5 align=center><i><font color='red'>"+tr("<b>Please Note:</b> All settings shown below are based on assumptions that nothing has changed since previous days.")+"</font></i></td></tr>\n";
        } else {
            html+="<tr><td colspan=5>&nbsp;</td></tr>";
        }
        QString fmt = QString("<tr><td colspan=3 width='60%'><p title='%2'>%1</p></td><td colspan=2 width='40%'>%3</td></tr>");

        for (const DailyDetails::TextRow &row : settings.rows) {
            html += QString(fmt)
                    .arg(row.label)
                    .arg(row.tooltip)
                    .arg(row.value);
        }

        html+="</table>";
        html+="<hr/>\n";
    }
    return html;
}

QString Daily::getOximeterInformation(Day * day)
{
    QString html;
    // The figures are collected by DailyDetails::oximeter()
    const DailyDetails::Oximeter oximeter = DailyDetails::oximeter(day);
    if (oximeter.present) {
        htmlLsbSectionHeader(html,tr("Oximeter Information"),LSB_OXIMETER_INFORMATION );
        if (!leftSideBarEnable[LSB_OXIMETER_INFORMATION] ) {
            return html;
        }
        html+="<table cellpadding=0 cellspacing=0 border=0 width=100%>";
        html+="<tr><td colspan=5 align=center>"+oximeter.heading+"</td></tr>\n";
        for (const DailyDetails::TextRow &row : oximeter.rows) {
            switch (row.style) {
            case DailyDetails::TextRow::Pair:
                html+=QString("<tr><td colspan=3 width='60%'>%1</td><td colspan=2 width='40%'>%2</td></tr>").arg(row.label, row.value);
                break;
            case DailyDetails::TextRow::Line:
                html+=QString("<tr><td colspan=5 align=center>%1: %2</td></tr>").arg(row.label, row.value);
                break;
            case DailyDetails::TextRow::Subheading:
                html+=QString("<tr><td colspan=5 align=center><i>%1</i></td></tr>").arg(row.label);
                break;
            }
        }
        if (!oximeter.note.isEmpty()) {
            html+=QString("<tr><td colspan=5><i>%1</i></td></tr>").arg(oximeter.note);
        }
        html+="</table>\n";
        html+="<hr/>\n";
    }
    return html;
}

QString Daily::getCPAPInformation(Day * day)
{
    QString html;
    const DailyDetails::Device device = DailyDetails::device(day);
    if (!device.present) return html;

    htmlLsbSectionHeader(html , device.brand ,LSB_MACHINE_INFO );
    if (!leftSideBarEnable[LSB_MACHINE_INFO] ) {
        return html;
    }
    html+="<table cellspacing=0 cellpadding=0 border=0 width='100%'>\n";

    QString tooltip=device.tooltip;
    tooltip=tooltip.replace(" ","&nbsp;");
    html+="<tr><td align=center><p title=\""+tooltip+"\">"+device.model+"</p></td></tr>\n";
    html+="<tr><td align=center>";

    html+=tr("PAP Mode: %1").arg(device.mode)+"<br/>";
    html+= device.pressure;
    html+="</td></tr>\n";
    if (device.settingsMissing) {
        html+=QString("<tr><td align=center><font color='red'><i>%1</i></font></td></tr>").arg(tr("(Mode and Pressure settings missing; yesterday's shown.)"));
    }

    html+="</table>\n";
    html+="<hr/>\n";
    return html;
}


QString Daily::getStatisticsInfo(Day * day)
{
    if (day == nullptr) return QString();

    // The figures are collected by DailyDetails::statistics()
    const DailyDetails::Statistics stats = DailyDetails::statistics(day, GraphView->isEmpty());

    QString html;

    htmlLsbSectionHeader(html,tr("Statistics"),LSB_STATISTICS );
    if (!leftSideBarEnable[LSB_STATISTICS] ) {
        return html;
    }
    html+="<table cellspacing=0 cellpadding=0 border=0 width='100%'>\n";
    html+=QString("<tr><td><b>%1</b></td><td align=right><b>%2</b></td><td align=right><b>%3</b></td><td align=right><b>%4</b></td><td align=right><b>%5</b></td></tr>")
            .arg(STR_TR_Channel)
            .arg(STR_TR_Min)
            .arg(stats.midName)
            .arg(stats.percName)
            .arg(stats.maxName);

    for (const DailyDetails::StatRow &row : stats.rows) {
        QString tooltip = row.tooltip;
        tooltip.replace("'", "&apos;");

        html+=QString("<tr><td align=left title='%6'>%1</td><td align=right>%2</td><td align=right>%3</td><td align=right>%4</td><td align=right>%5</td></tr>")
            .arg(row.label)
            .arg(row.min)
            .arg(row.mid)
            .arg(row.perc)
            .arg(row.max)
            .arg(tooltip);
    }

    if (stats.summaryOnlyNote) {
        html+="<tr><td colspan=5>&nbsp;</td></tr>\n";
        html+=QString("<tr><td colspan=5 align=center><i>%1</i></td></tr>").arg("<b>"+STR_MessageBox_PleaseNote+"</b> "+
                    tr("This day just contains summary data, only limited information is available."));
    } else if (stats.cpap) {
        html+="<tr><td colspan=5>&nbsp;</td></tr>";

        // Time in apnea and over the leak redline are headed in bold, the ramp times not
        const QStringList plain = { tr("Total ramp time"), tr("Time outside of ramp") };
        for (const auto &extra : stats.extras) {
            if (plain.contains(extra.first)) {
                html+="<tr><td colspan=3 align='left'>"+extra.first+
                        QString("</td><td colspan=2>%1</td></tr>").arg(extra.second);
            } else {
                html+="<tr><td colspan=3 align='left'><b>"+extra.first+
                        QString("</b></td><td colspan=2>%1</td></tr>").arg(extra.second);
            }
        }
    }


    html+="</table>\n";
    html+="<hr/>\n";
    return html;
}

QString Daily::getEventBreakdown(Day * cpap)
{
    Q_UNUSED(cpap)
    QString html;
    html+="<table cellspacing=0 cellpadding=0 border=0 width='100%'>\n";

    html+="</table>";
    return html;
}

QString Daily::getSleepTime(Day * day)
{
    //cpap, Day * oxi
    QString html;

    const DailyDetails::Usage usage = DailyDetails::usage(day);
    if (!usage.present)
        return html;

#ifndef COMBINE_MODE_3
    htmlLsbSectionHeader(html , tr("General") ,LSB_SLEEPTIME_INDICES );
    if (!leftSideBarEnable[LSB_SLEEPTIME_INDICES] ) {
        return html;
    }
#else
    if (!leftSideBarEnable[LSB_MACHINE_INFO] )  return html;
#endif

    html+="<table cellspacing=0 cellpadding=0 border=0 width='100%'>\n";
    html+="<tr><td align='center'><b>"+STR_TR_Date+"</b></td><td align='center'><b>"+tr("Start")+"</b></td><td align='center'><b>"+tr("End")+"</b></td><td align='center'><b>"+STR_UNIT_Hours+"</b></td></tr>";
    int tt=int(usage.seconds);
    const QDateTime &date=usage.start;
    const QDateTime &date2=usage.end;

    int h=tt/3600;
    int m=(tt/60)%60;
    int s=tt % 60;
    html+=QString("<tr><td align='center'>%1</td><td align='center'>%2</td><td align='center'>%3</td><td align='center'>%4</td></tr>\n")
            .arg(date.date().toString(QLocale::system().dateFormat(QLocale::ShortFormat)))
            .arg(date.toString("HH:mm:ss"))
            .arg(date2.toString("HH:mm:ss"))
            .arg(QString::asprintf("%02i:%02i:%02i",h,m,s));
    html+="</table>\n";
    html+="<hr/>";

    return html;
}


QString Daily::getAHI(Day * day, bool isBrick) {
    QString html;

    const DailyDetails::Headline ahi = DailyDetails::headline(day);

    html ="<table cellspacing=0 cellpadding=0 border=0 width='100%'>\n";

    // Show application font, for debugging font issues

    html +="<tr>";
    if (!isBrick) {
        html +=QString("<td colspan=5 bgcolor='%1' align=center><div title='  %4 \t  ' style='white-space: nowrap;'><font size=+3 color='%2'><b>%3</b></font>&nbsp;&nbsp;&nbsp;&nbsp; <font size=+3 color='%2'><b>%5</b></font></div></td>\n")
                .arg("#F88017").arg(COLOR_Text.name()).arg(ahi.name).arg(ahi.fullName).arg(ahi.value,0,'f',2);
    } else {
        html +=QString("<td colspan=5 bgcolor='%1' align=center><font size=+3 color='yellow'><b>%2</b></font></td>\n")
                .arg("#F88017").arg(tr("This CPAP device does NOT record detailed data"));
    }
    html +="</tr>\n";

    // Obstructive and central breakdown of the same index (see DailyDetails::headline()),
    // shown together on one line directly beneath it
    if (!isBrick && ahi.hasShares) {
        const double oahi = ahi.obstructive;
        const double cahi = ahi.central;
        html +="<tr>";
        html +=QString("<td colspan=5 bgcolor='%1' align=center><div title='  %2 \t  ' style='white-space: nowrap;'><font color='%3'>%4&nbsp;<b>%5</b>&nbsp;&nbsp;&nbsp;&nbsp;%6&nbsp;<b>%7</b></font></div></td>\n")
                .arg("#F88017")
                .arg(tr("Obstructive and Central shares of the %1").arg(STR_TR_AHI))
                .arg(COLOR_Text.name())
                .arg(STR_TR_OAHI).arg(oahi,0,'f',2)
                .arg(STR_TR_CAHI).arg(cahi,0,'f',2);
        html +="</tr>\n";
    }

    html +="</table>\n";

    return html;
}

QString Daily::getIndices(Day * day, QHash<ChannelID, EventDataType>& values ) {
    QString html;

    html = "<table cellspacing=0 cellpadding=0 border=0 width='100%'>\n";

    // The figures are collected by DailyDetails::events(); channel rows link to the Events tab
    for (const DailyDetails::EventRow &row : DailyDetails::events(day)) {
        QString tooltip = row.tooltip;
        tooltip.replace("'", "&apos;");
        QColor altcolor = (brightness(row.colour) < 0.3) ? Qt::white : Qt::black; // pick a contrasting color
        if (row.link) {
            values[row.code] = row.value;
            html+=QString("<tr><td align='left' bgcolor='%1'><b><font color='%2'><a href='event=%5' style='text-decoration:none;color:%2' title='<p>%6</p>'>%3</a></font></b></td><td width=20% bgcolor='%1'><b><font color='%2'>%4</font></b></td></tr>")
                    .arg(row.colour.name())
                    .arg(altcolor.name())
                    .arg(row.name)
                    .arg(row.rate)
                    .arg(row.code)
                    .arg(tooltip);
        } else {
            html+=QString("<tr><td align='left' bgcolor='%1' title='<p>%5</p>'><b><font color='%2'>%3</font></b></td><td width=20% bgcolor='%1'><b><font color='%2'>%4</font></b></td></tr>")
                    .arg(row.colour.name())
                    .arg(altcolor.name())
                    .arg(row.name)
                    .arg(row.rate)
                    .arg(tooltip);
        }
    }

    html+="</table><hr/>";

#ifndef COMBINE_MODE_3
    if (!leftSideBarEnable[LSB_SLEEPTIME_INDICES] )
#else
    if (!leftSideBarEnable[LSB_MACHINE_INFO] )
#endif
    {
        return QString();       // must calculate values first for pie chart.
    }

    return html;
}


void Daily::htmlLsbSectionHeaderInit (bool section) {
    if (!section) {
        int bits = AppSetting->leftSideBarEnable();
        for (int i = 0; i < leftSideBarEnable.size(); i++)
            leftSideBarEnable.setBit(i, (bits >> i) & 1);
    }
    htmlLsbPrevSectionHeader = true;
}

void Daily::htmlLsbSectionHeader (QString&html , const QString& name,LEFT_SIDEBAR checkBox) {
        QString handle = "leftsidebarenable";
        bool on   = leftSideBarEnable[checkBox];  // this section is enabled
        bool prev = htmlLsbPrevSectionHeader;  // prev section will be displayed
                                                // only false when just section name is display
                                                // true when full section should be displayed.
        htmlLsbPrevSectionHeader = on;

        if ( (!prev) && on) {
            html+="<hr/>";
        }
        html += QString(
            "<table cellspacing=0 cellpadding=0 border=0 width='100%'>"
            "<tr>"
            "<td align=left>"
            #ifdef CONFIGURE_MODE
                "<a href='leftsidebarenable=%3'>" "<img src='qrc:/icons/session-%2.png'/a>"
            #else
                " "
            #endif
            "</td>"
            "<td align=centered;>"
                "<a href='leftsidebarenable=%3' style='text-decoration:none;color:%4'>"
                   "<b>%1</b></a>"
            "</td>"
            "</table>\n"
            )
            .arg(name)
            .arg(on ? "on" : "off")
            .arg(checkBox)
            .arg(QApplication::palette().color(QPalette::Text).name());
}

/*! \brief True when the day's device scores the events the breakdown pie shows (so a night
    without any gets the "0.0" image), whether or not it recorded any today.

    day->channelHasData() alone is not sufficient: a channel with a zero count and no event
    list is never written to session_channels, so on an event-free day it reports false for
    every AHI channel. The day's device is asked as well: Machine::m_availableChannels is the
    union over every session, so a device that scores events on other days qualifies, while a
    device that never reports them (a brick) does not. */
static bool reportsBreakdownChannels(Day *day)
{
    Machine * cpap = day->machine(MT_CPAP);
    auto reportsChannel = [&](ChannelID id) {
        return day->channelHasData(id) || (cpap && cpap->hasChannel(id));
    };

    bool gotsome = false;
    for (int i = 0; i < ahiChannels.size() && !gotsome; i++)
        gotsome = reportsChannel(ahiChannels.at(i));

    return gotsome
           || reportsChannel(CPAP_RERA)
           || reportsChannel(CPAP_FlowLimit)
           || reportsChannel(CPAP_SensAwake);
}

QString Daily::getPieChart (float values, Day * day) {
    QString pieChartName = tr("Event Breakdown");
    QString html ;
    htmlLsbSectionHeader(html , pieChartName,LSB_PIE_CHART );
    if (!leftSideBarEnable[LSB_PIE_CHART] ) {
        return html;
    }
    // The table is pulled up 8px to sit snugly under the "Event Breakdown" title, and the
    // separator below gets 8px back to compensate.  That offset is absorbed by the empty
    // spacer row emitted below the table tag, so the spacer must be present in *both*
    // branches -- without it the "0.0!" image is dragged up over the title.
    html += "<table style='margin-top:-8px' cellspacing=0 cellpadding=0 border=0 width='100%'>";
    html += QString("<tr><td align=center><b>%1</b></td></tr>").arg("");
    if (values > 0) {
        eventBreakdownPie()->setShowTitle(false);

        int w=155;
        int h=155;
        QPixmap pixmap=eventBreakdownPie()->renderPixmap(w,h,false);
        if (!pixmap.isNull()) {
            QByteArray byteArray;
            QBuffer buffer(&byteArray); // use buffer to store pixmap into byteArray
            buffer.open(QIODevice::WriteOnly);
            pixmap.save(&buffer, "PNG");
            // Close section where pie chart is pressed
            html += QString("<tr><td align=center>"
                "<a href='%1' style='text-decoration:none;color:black'>"
                "<img src=\"data:image/png;base64," + byteArray.toBase64() + "\"></td></tr>\n")
                .arg("leftsidebarenable=%1").arg(LSB_PIE_CHART)
                ;
        } else {
            html += "<tr><td align=center>"+tr("Unable to display Pie Chart on this system")+"</td></tr>\n";
        }
    } else {
        // Show the "0.0!" image when the device scores these events but recorded none today.
        //
        // day->channelHasData() alone is not sufficient: a channel with a zero count and no
        // event list is never written to session_channels (Session::UpdateSummaries() rebuilds
        // m_availableChannels from the event lists, and StoreToDatabase() only persists those),
        // so on an event-free day it reports false for every AHI channel and the image was
        // silently dropped.  OSCAR 1.7 serialised the whole m_cnt hash, zero entries included,
        // which is why it still shows "0.0!" here.  Fall back to asking the day's device
        // whether it reports the channel at all -- Machine::m_availableChannels is the union
        // over every session, so a device that scores events on other days qualifies, while a
        // device that never reports them (a brick) still shows nothing.
        if (reportsBreakdownChannels(day)) {
            html += "<tr><td align=center><img src=\"qrc:/docs/0.0.gif\"></td></tr>\n";
        }
    }
    html+="</table>\n";
    html+="<hr style='margin-top:8px'/>\n";

    return html;
}

// honorPieChart true - show pie chart if it is enabled.  False, do not show pie chart
QString Daily::getLeftSidebar (bool honorPieChart) {
    QString html =   htmlLeftHeader
                   + htmlLeftAHI
                   + htmlLeftMachineInfo
                   + htmlLeftSleepTime
                   + htmlLeftIndices;
    // Include pie chart if wanted and enabled.
    if (honorPieChart && AppSetting->showPieChart())
        html += htmlLeftPieChart;

    html +=   htmlLeftNoHours
            + htmlLeftStatistics
            + htmlLeftOximeter
            + htmlLeftMachineSettings
            + htmlLeftSessionInfo
            + htmlLeftFooter;

    return html;
}

DailyDetails::Page Daily::detailsPage(Day *day, QDate date, bool brick, bool otherDevices)
{
    DailyDetails::Page page;
    page.date = date;
    Machine *cpap = day ? day->machine(MT_CPAP) : nullptr;
    // Sessions count as "all off" only when none of the CPAP sessions is on: a session that is
    // on but adds up to no time is an impossibly short one (Classic reports both as off)
    const int offSessions = (cpap && !day->hasEnabledSessions(MT_CPAP)) ? day->size() : 0;
    page.kind = DailyDetails::classify(cpap != nullptr, cpap ? day->hours(MT_CPAP) : 0, offSessions,
                                       brick, otherDevices);
    if (!day) return page;

    page.headline = DailyDetails::headline(day);
    page.usage = DailyDetails::usage(day);
    page.device = DailyDetails::device(day);
    page.events = DailyDetails::events(day);
    page.statistics = DailyDetails::statistics(day, GraphView->isEmpty());
    page.oximeter = DailyDetails::oximeter(day);
    page.settings = DailyDetails::settings(day);
    page.sessions = DailyDetails::sessions(day);
    page.showPie = AppSetting->showPieChart();
    if (page.showPie && page.kind == DailyDetails::DayKind::Cpap) {
        if (DailyDetails::pieTotal(page.events) > 0) {
            eventBreakdownPie()->setShowTitle(false);
            page.pie = eventBreakdownPie()->renderPixmap(155, 155, false);
        } else {
            page.pieZero = reportsBreakdownChannels(day);
        }
    }

    if (page.headline.present) {
        // The index over the nights before this one, as the Welcome page compares it. Only the
        // AHI card is computed.
        QSet<QString> others;
        for (const CardInfo &info : WelcomeMetrics::catalog()) {
            if (info.id != MetricId::Ahi) others.insert(info.key);
        }
        for (const MetricData &m : WelcomeMetrics::cards(date, others)) {
            if (m.id != MetricId::Ahi) continue;
            page.avg7 = m.avg7;
            page.avg30 = m.avg30;
            page.nights7 = m.nights7;
            page.nights30 = m.nights30;
        }
    }
    return page;
}

QVariant MyTextBrowser::loadResource(int type, const QUrl &url)
{
    if (type == QTextDocument::ImageResource && url.scheme().compare(QLatin1String("data"), Qt::CaseInsensitive) == 0) {
        static QRegularExpression re("^image/[^;]+;base64,.+={0,2}$");
        QRegularExpressionMatch match = re.match(url.path());
        if (match.hasMatch())
            return QVariant();
    }
    return QTextBrowser::loadResource(type, url);
}


void Daily::Load(QDate date)
{
    PERF_TIMER_SCOPE("Daily::Load()");

    qDebug() << "Daily::Load(): Called for" << date.toString() << "using" << QApplication::font().toString();

    setApplicationFont();

    const QString dateText = date.toString(QLocale::system().dateFormat(QLocale::ShortFormat));
    // The status line draws plain text, so Modern leaves out the tags Classic shows
    dateDisplay->setText(m_modern ? dateText : "<i>" + dateText + "</i>");
    previous_date=date;

    Day * day = p_profile->GetDay(date);
    Machine *cpap = nullptr,
            *oxi = nullptr,
            //*stage = nullptr,
            *posit = nullptr;

    if (day) {
        cpap = day->machine(MT_CPAP);
        oxi = day->machine(MT_OXIMETER);
        posit = day->machine(MT_POSITION);
    }
    else {
        qWarning() << "Daily::Load(): Warning: unable to load day" << date.toString(QLocale::system().dateFormat(QLocale::ShortFormat));
    }

    if (m_toolbar) {
        // The top bar names the device, its mode and its pressures, so a screenshot shows them
        if (cpap) {
            const MachineInfo info = cpap->getInfo();
            // With no session on there are no settings to show, only the device
            QString settings;
            if (day->hasEnabledSessions(MT_CPAP)) {
                settings = day->getCPAPModeStr();
                const QString pressure = convertHtmlToPlainText(day->getPressureSettings());
                if (!pressure.isEmpty()) settings += QStringLiteral(" · ") + pressure;
            }
            m_toolbar->setDeviceLine((info.brand + " " + info.model).trimmed(), settings);
        } else {
            m_toolbar->setDeviceLine(QString(), QString());
        }
    }

    PERF_TIMER_START("Daily::Load::Sessions");
    if (!AppSetting->cacheSessions()) {
        // Getting trashed on purge last day...

        // lastcpapday can get purged and be invalid
        if (lastcpapday && (lastcpapday!=day)) {
            for (QMap<QDate, Day *>::iterator di = p_profile->daylist.begin(); di!= p_profile->daylist.end(); ++di) {
                Day * d = di.value();
                if (d->eventsLoaded()) {
                    if (d->useCounter() == 0) {
                        d->CloseEvents();
                    }
                }
            }
        }
    }
    PERF_TIMER_STOP("Daily::Load::Sessions");

    lastcpapday=day;

    // Clear the components of the left sidebar prior to recreating them
    htmlLeftAHI.clear();
    htmlLeftMachineInfo.clear();
    htmlLeftSleepTime.clear();
    htmlLeftIndices.clear();
    htmlLeftPieChart.clear();
    htmlLeftNoHours.clear();
    htmlLeftStatistics.clear();
    htmlLeftOximeter.clear();
    htmlLeftMachineSettings.clear();
    htmlLeftSessionInfo.clear();

    htmlLeftHeader = "<html><head>"
    "<style>body { line-height: 0.95; }</style>"
    "</head>"
    "<body leftmargin=0 rightmargin=0 topmargin=0 marginwidth=0 marginheight=0>";

    PERF_TIMER_START("Daily::Load::OpenEvents");
    if (day) {
        day->OpenEvents();
    }
    GraphView->setDay(day);
    PERF_TIMER_STOP("Daily::Load::OpenEvents");


    UpdateEventsTree(ui->treeWidget, day);

    // FIXME:
    // Generating entire statistics because bookmarks may have changed.. (This updates the side panel too)
    PERF_TIMER_START("Daily::Load::GenerateStatistics");
    if (mainwin) {
        mainwin->GenerateStatistics();
    }
    PERF_TIMER_STOP("Daily::Load::GenerateStatistics");

    snapGV->setDay(day);

    QString modestr;
    CPAPMode mode=MODE_UNKNOWN;
    QString a;
    bool isBrick=false;

    PERF_TIMER_START("Daily::Load::UpdateCombos");

    updateGraphCombo();

    updateEventsCombo(day);
    PERF_TIMER_STOP("Daily::Load::UpdateCombos");

    if (!cpap) {
        GraphView->setEmptyImage(QPixmap(":/icons/logo-md.png"));
    }
    if (cpap) {
        float hours=day->hours(MT_CPAP);
        if (GraphView->isEmpty() && (hours>0)) {
            // TODO: Eventually we should get isBrick from the loader, since some summary days
            // on a non-brick might legitimately have no graph data.
            bool gotsome = false;
            for (int i = 0; i < ahiChannels.size() && !gotsome ; i++) {
                gotsome = p_profile->hasChannel(ahiChannels.at(i));
            }
            if (!gotsome) {
                GraphView->setEmptyText(STR_Empty_Brick);

                GraphView->setEmptyImage(QPixmap(":/icons/sadface.png"));
                isBrick=true;
            } else {
                GraphView->setEmptyImage(QPixmap(":/icons/logo-md.png"));
            }
        }

        mode=(CPAPMode)(int)day->settings_max(CPAP_Mode);
        const DailyDetails::DayKind kind = DailyDetails::classify(true, hours, day->size(), isBrick, oxi || posit);

        PERF_TIMER_START("Daily::Load::LeftPanel");
        modestr=schema::channel[CPAP_Mode].m_options[mode];
        if (kind == DailyDetails::DayKind::Cpap || kind == DailyDetails::DayKind::ComplianceOnly) {
            htmlLeftAHI= getAHI(day,isBrick);

            htmlLeftMachineInfo = getCPAPInformation(day);

            htmlLsbSectionHeaderInit();

            htmlLeftSleepTime = getSleepTime(day);

            QHash<ChannelID, EventDataType> values;
            htmlLeftIndices = getIndices(day,values);

            htmlLeftPieChart = getPieChart((values[CPAP_Obstructive] + values[CPAP_Hypopnea] + values[CPAP_AllApnea] +
                                            values[CPAP_ObstructiveHypopnea] + values[CPAP_CentralHypopnea] +
                                            values[CPAP_ClearAirway] + values[CPAP_Apnea] + values[CPAP_RERA] +
                                            values[CPAP_FlowLimit] + values[CPAP_SensAwake]), day);
            htmlLsbSectionHeaderInit();

        } else {  // No hours
            htmlLeftNoHours+="<table cellspacing=0 cellpadding=0 border=0 width='100%'>\n";
            if (!isBrick) {
                htmlLeftNoHours+="<tr><td colspan='5'>&nbsp;</td></tr>\n";
                if (kind == DailyDetails::DayKind::SessionsOff) {
                    htmlLeftNoHours+="<tr><td colspan=5 align='center'><font size='+3'>"+tr("Sessions all off!")+"</font></td></tr>";
                    htmlLeftNoHours+="<tr><td colspan=5 align='center><img src='qrc:/icons/logo-md.png'></td></tr>";
                    htmlLeftNoHours+="<tr bgcolor='#89abcd'><td colspan=5 align='center'><i><font color=white size=+1>"+tr("Sessions exist for this day but are switched off.")+"</font></i></td></tr>\n";
                    GraphView->setEmptyText(STR_Empty_NoSessions);
                } else {
                    htmlLeftNoHours+="<tr><td colspan=5 align='center'><b><h2>"+tr("Impossibly short session")+"</h2></b></td></tr>";
                    htmlLeftNoHours+="<tr><td colspan=5 align='center'><i>"+tr("Zero hours??")+"</i></td></tr>\n";
                }
            } else { // Device is a brick
                htmlLeftNoHours+="<tr><td colspan=5 align='center'><b><h2>"+tr("no data :(")+"</h2></b></td></tr>";
                htmlLeftNoHours+="<tr><td colspan=5 align='center'><i>"+tr("Sorry, this device only provides compliance data.")+"</i></td></tr>\n";
                htmlLeftNoHours+="<tr><td colspan=5 align='center'><i>"+tr("Complain to your Equipment Provider!")+"</i></td></tr>\n";
            }
            htmlLeftNoHours+="<tr><td colspan='5'>&nbsp;</td></tr>\n";
            htmlLeftNoHours+="</table><hr/>\n";
        }

    } else { // if (!CPAP)

        htmlLeftSleepTime = getSleepTime(day);
    }
    PERF_TIMER_STOP("Daily::Load::LeftPanel");
    if (day) {
        htmlLeftOximeter = getOximeterInformation(day);
        htmlLeftMachineSettings = getMachineSettings(day);
        htmlLeftSessionInfo= getSessionInformation(day);
    }

    if ((cpap && !isBrick && (day->hours()>0)) || oxi || posit) {

        if ( (!cpap) && (!isBrick) ) {
            // add message when there is no cpap data but there exists oximeter data.
            QString  msg = tr("No CPAP data is available for this day");
            QString  beg = "<table cellspacing=0 cellpadding=0 border=0 width='100%'>"
            "<tr><td colspan=5 bgcolor='#89abcd' align=center><p title=''> &nbsp; <b><font size=+0 color=white>";
            htmlLeftAHI  = QString("%1 %2 </b></td></tr></table>").arg(beg).arg(msg);
        }

        htmlLeftStatistics = getStatisticsInfo(day);

    } else {
        if (cpap && day->hours(MT_CPAP)<0.0000001) {
        } else if (!isBrick) {
            htmlLeftAHI.clear();  // clear AHI (no cpap data) msg when there is no oximeter data
            htmlLeftSessionInfo.clear(); // clear session info

            htmlLeftStatistics ="<table cellspacing=0 cellpadding=0 border=0 height=100% width=100%>";
            htmlLeftStatistics+="<tr height=25%><td align=center></td></tr>";
            htmlLeftStatistics+="<tr><td align=center><font size='+3'>"+tr("\"Nothing's here!\"")+"</font></td></tr>";
            htmlLeftStatistics+="<tr><td align=center><img src='qrc:/icons/logo-md.png'></td></tr>";
            htmlLeftStatistics+="<tr height=5px><td align=center></td></tr>";
            htmlLeftStatistics+="<tr bgcolor='#89abcd'><td align=center><i><font size=+1 color=white>"+tr("No data is available for this day.")+"</font></i></td></tr>";
            htmlLeftStatistics+="<tr height=25%><td align=center></td></tr>";
            htmlLeftStatistics+="</table>\n";
        }

    }

    htmlLeftFooter ="</body></html>";
    PERF_TIMER_STOP("Daily::Load::LeftPanel");

    // SessionBar colors.  Colors alternate.
    QColor cols[]={
        COLOR_Gold,
//      QColor("light blue"),
        QColor("skyblue"),
    };
    const int maxcolors=sizeof(cols)/sizeof(QColor);
    QList<Session *>::iterator i;

    sessionbar->clear();    // clear sessionbar as some days don't have sessions

    if (cpap) {
        int c=0;

        for (i=day->begin();i!=day->end();++i) {
            Session * s=*i;
            if ((*s).type() == MT_CPAP)
                sessionbar->add(s, cols[c % maxcolors]);
            c++;
        }
    }


    // Generate HTML shown in [Details] tab
    // Note: Outputs to local file if debugging
    QString html = getLeftSidebar(true);
    webView->setHtml(html);

    if (m_detailsPanel) {
        m_detailsPanel->setPage(detailsPage(day, date, isBrick, oxi || posit));
    }
    
#if defined(DEBUG_DAILY_HTML) || defined(UNITTEST_MODE)
    QString name = GetAppData()+"/test.html";
    QFile file(name);
    if (file.open(QFile::WriteOnly)) {
        const QByteArray & b = html.toLocal8Bit();
        file.write(b.data(), b.size());
        file.close();
    }
#endif

    ui->JournalNotes->clear();

    ui->bookmarkTable->clearContents();
    ui->bookmarkTable->setRowCount(0);
    QStringList sl;
    ui->bookmarkTable->setHorizontalHeaderLabels(sl);
#ifndef REMOVE_FITNESS
    user_height_cm  = p_profile->user->height();
    set_ZombieUI(0);
    set_WeightUI(0);

#endif
    BookmarksChanged=false;
    Session *journal=GetJournalSession(date);
    if (journal) {
        if (journal->settings.contains(Journal_Notes)) {
            set_NotesUI(journal->settings[Journal_Notes].toString());
        }

#ifndef REMOVE_FITNESS
        bool ok;
        if (journal->settings.contains(Journal_Weight)) {
            double kg=journal->settings[Journal_Weight].toDouble(&ok);
            set_WeightUI(kg);
        }

        if (journal->settings.contains(Journal_ZombieMeter)) {
            int value = journal->settings[Journal_ZombieMeter].toInt(&ok);
            set_ZombieUI(value);
        }
#endif

        if (journal->settings.contains(Bookmark_Start)) {
            QVariantList start=journal->settings[Bookmark_Start].toList();
            QVariantList end=journal->settings[Bookmark_End].toList();
            QStringList notes=journal->settings[Bookmark_Notes].toStringList();

            if (start.size() > 0) {
                // Careful with drift here - apply to the label but not the
                // stored data (which will be saved if journal changes occur).
                qint64 drift = 0;
                Day * dday=p_profile->GetDay(previous_date,MT_CPAP);
                if (dday) {
                    for (Session* s : dday->sessions)
                        if (s->type() == MT_CPAP) { drift = s->correctionMs(); break; }
                }
                set_BookmarksUI(start ,end , notes, drift);
            }
        } // if (journal->settings.contains(Bookmark_Start))
    } // if (journal)

    emit dateLoaded(date);
}

void Daily::UnitsChanged()
{
#ifndef REMOVE_FITNESS
    // Called from newprofile when height changed or when units changed metric / english
    // just as if weight changed. may make bmi visible.
    on_weightSpinBox_editingFinished();
#endif
    // The calendar's day hints show the weight in the chosen units: refresh every day that has a hint,
    // including the neighbouring months' days the grid shows.
    updateCalendarDays(ui->calendar->dateTextFormat().keys());
}

double  Daily::calculateBMI(double weight_kg, double height_cm) {
    double height = height_cm/100.0;
    double bmi    = weight_kg/(height * height);
    return bmi;
}


QString Daily::convertHtmlToPlainText( QString html) {
    QTextDocument doc;
    doc.setHtml(html);
    QString plain = doc.toPlainText();
    return plain.simplified();
}

void Daily::set_JournalZombie(QDate& date, int value) {
    Session *journal=GetJournalSession(date);
    if (!journal) {
        journal=CreateJournalSession(date);
    }
    if (!journal) { return; }
    if (value==0) {
        // should delete zombie entry here. if null.
        auto jit = journal->settings.find(Journal_ZombieMeter);
        if (jit != journal->settings.end()) {
           journal->settings.erase(jit);
        }
    } else {
        journal->settings[Journal_ZombieMeter]=value;
    }
    journal->SetChanged(true);
};


void Daily::set_JournalWeightValue(QDate& date, double kg) {
    Session *journal=GetJournalSession(date);
    if (!journal) {
        journal=CreateJournalSession(date);
    }
    if (!journal) { return; }

    if (journal->settings.contains(Journal_Weight)) {
        QVariant old = journal->settings[Journal_Weight];
        if (abs(old.toDouble() - kg) < zeroD  && kg > zeroD) {
            // No change to weight - skip
            return ;
        }
    } else if (kg < zeroD) {
        // Still zero - skip
        return ;
    }
    if (kg > zeroD) {
        journal->settings[Journal_Weight]=kg;
        // Save BMI alongside weight so the Overview BMI graph has data
        if (user_height_cm > zeroD) {
            journal->settings[Journal_BMI] = calculateBMI(kg, user_height_cm);
        }
    } else {
        // Weight now zero - remove weight and BMI from journal
        auto jit = journal->settings.find(Journal_Weight);
        if (jit != journal->settings.end()) {
            journal->settings.erase(jit);
        }
        auto bit = journal->settings.find(Journal_BMI);
        if (bit != journal->settings.end()) {
            journal->settings.erase(bit);
        }
    }
    journal->SetChanged(true);
    return ;
}

void Daily::set_JournalNotesHtml(QDate& date, QString html) {
    QString newHtmlPlaintText = convertHtmlToPlainText(html); // have a look as plaintext to see if really empty.
    bool newHtmlHasContent = !ui->JournalNotes->document()->isEmpty() ;
    Session* journal = GetJournalSession(date,false);
    QString prevHtml;
    if (journal) {
        auto jit = journal->settings.find(Journal_Notes);
        if (jit != journal->settings.end()) {
            prevHtml = journal->settings[Journal_Notes].toString();
        }
    }
    if (!newHtmlHasContent) {
        if (!journal)  {
            return ; //no action required
        }
        // removing previous notes.
        auto jit = journal->settings.find(Journal_Notes);
        if (jit != journal->settings.end()) {
            journal->settings.erase(jit);
            journal->SetChanged(true);
        }
        return;
    } else if (html == prevHtml) {
        return ; //no action required
    }
    if (!journal) {
        journal = GetJournalSession(date,true);
        if (!journal) {
            journal = CreateJournalSession(date);
        }
        if (!journal) { return; }
    }
    journal->settings[Journal_Notes] = html;
    journal->SetChanged(true);
}


void Daily::clearLastDay()
{
    lastcpapday=nullptr;
}

void Daily::clearJournalNotesEditor()
{
    ui->JournalNotes->clear();
}

void Daily::releaseDayData()
{
    // SessionBar keeps raw Session pointers and reads them when painting. The graph layers
    // read the day through m_day (gFlagsGroup's and gSessionBarLayer's own session lists are
    // only read while m_day is set), so clearing the day stops them too.
    sessionbar->clear();
    GraphView->setDay(nullptr);
    snapGV->setDay(nullptr);
    lastcpapday = nullptr;
}

/*! \brief Returns true if the journal session has no user-visible content.
 *  LastUpdated is intentionally excluded — it is not user-authored data.
 *  Bookmarks with an empty list count as absent. */
static bool isJournalSessionEmpty(Session *journal)
{
    static const QList<ChannelID> s_meaningfulKeys = {
        Journal_Notes, Journal_Weight, Journal_BMI, Journal_ZombieMeter
    };
    for (ChannelID key : s_meaningfulKeys) {
        if (journal->settings.contains(key)) return false;
    }
    if (journal->settings.contains(Bookmark_Start) &&
        !journal->settings[Bookmark_Start].toList().isEmpty()) {
        return false;
    }
    return true;
}

void Daily::deleteJournalSession(Session *journal, QDate date)
{
    qint64 rowId = journal->sessionRowId();

    Day *day = p_profile->GetDay(date, MT_JOURNAL);
    if (day) {
        day->removeSession(journal);
    }

    if (rowId > 0) {
        SessionRepository repo;
        if (!repo.remove(rowId)) {
            qWarning() << "Daily::deleteJournalSession(): Failed to remove session from database";
        }
    }

    delete journal;
}

void Daily::Unload(QDate date)
{
    if (!date.isValid()) {
        date = getDate();
        if (!date.isValid()) {
            graphView()->setDay(nullptr);
            return;
        }
    }

    // Update the journal notes
    set_JournalNotesHtml(date, ui->JournalNotes->toHtml());
    Session *journal = GetJournalSession(date);
    if (journal) {
        if (journal->IsChanged()) {
            if (isJournalSessionEmpty(journal)) {
                deleteJournalSession(journal, date);
            } else {
                journal->settings[LastUpdated] = QDateTime::currentDateTime();
                journal->StoreToDatabase();
                journal->SetChanged(false);
            }
        }
    }
    UpdateCalendarDay(date);
}

void Daily::on_JournalNotesItalic_clicked()
{
    QTextCursor cursor = ui->JournalNotes->textCursor();
    if (!cursor.hasSelection())
        cursor.select(QTextCursor::WordUnderCursor);

    QTextCharFormat format=cursor.charFormat();

    format.setFontItalic(!format.fontItalic());


    cursor.mergeCharFormat(format);

}

void Daily::on_JournalNotesBold_clicked()
{
    QTextCursor cursor = ui->JournalNotes->textCursor();
    if (!cursor.hasSelection())
        cursor.select(QTextCursor::WordUnderCursor);

    QTextCharFormat format=cursor.charFormat();

    int fw=format.fontWeight();
    if (fw!=QFont::Bold)
        format.setFontWeight(QFont::Bold);
    else
        format.setFontWeight(QFont::Normal);

    cursor.mergeCharFormat(format);

}

QVector<int> journalNotesFontSizeArray={10,15,20};

void Daily::on_JournalNotesFontsize_activated(int index)
{
    int fontsize = journalNotesFontSizeArray[index];

    QTextCursor cursor = ui->JournalNotes->textCursor();
    if (cursor.atEnd()  && cursor.atStart() ) {
        // set default font size
        QFont defaultFont = ui->JournalNotes->font();
        defaultFont.setPointSize(fontsize);
        defaultFont.setItalic(false);
        defaultFont.setBold(false);
        ui->JournalNotes->setFont(defaultFont);
    }

    if (!cursor.hasSelection())
        cursor.select(QTextCursor::WordUnderCursor);
    QTextCharFormat format=cursor.charFormat();

    QFont font=format.font();
    font.setPointSize(fontsize);
    format.setFont(font);

    cursor.mergeCharFormat(format);
}

void Daily::on_JournalNotesColour_clicked()
{
    QColor col=QColorDialog::getColor(COLOR_Black,this,tr("Pick a Color")); //,QColorDialog::NoButtons);
    if (!col.isValid()) return;

    QTextCursor cursor = ui->JournalNotes->textCursor();
    if (!cursor.hasSelection())
        cursor.select(QTextCursor::WordUnderCursor);

    QBrush b(col);
    QPalette newPalette = palette();
    newPalette.setColor(QPalette::ButtonText, col);
    ui->JournalNotesColour->setPalette(newPalette);

    QTextCharFormat format=cursor.charFormat();

    format.setForeground(b);

    cursor.setCharFormat(format);
}
Session * Daily::CreateJournalSession(QDate date)
{
    return GetOrCreateJournalSession(date);
}

Session * Daily::GetJournalSession(QDate date , bool create) // Get the first journal session
{
    Day *day=p_profile->GetDay(date, MT_JOURNAL);
    if (day) {
        Session * session = day->firstSession(MT_JOURNAL);
        if (!session && create) {
            session = CreateJournalSession(date);
        }
        return session;
    }
    return nullptr;
}


void Daily::redrawWithZoom()
{
    if (!GraphView || GraphView->isEmpty()) return;

    // Save current viewport from the first visible graph with a valid range
    qint64 savedMin = 0, savedMax = 0;
    for (int i = 0; i < GraphView->size(); ++i) {
        gGraph* g = (*GraphView)[i];
        if (g->visible() && g->min_x < g->max_x) {
            savedMin = g->min_x;
            savedMax = g->max_x;
            break;
        }
    }

    // Force each graph to recalculate its physical extent from the corrected session times,
    // then clamp the saved viewport to the new physical bounds
    qint64 newMin = 0, newMax = 0;
    for (int i = 0; i < GraphView->size(); ++i) {
        gGraph* g = (*GraphView)[i];
        qint64 gMin = g->MinX();
        qint64 gMax = g->MaxX();
        if (gMin < gMax) {
            if (newMin == 0 || gMin < newMin) newMin = gMin;
            if (gMax > newMax) newMax = gMax;
        }
    }

    if (newMin == 0 || newMax <= newMin) {
        GraphView->redraw();
        return;
    }

    if (savedMin == 0 || savedMax <= savedMin) {
        // No prior zoom — show full extent
        GraphView->SetXBounds(newMin, newMax);
        return;
    }

    // Clamp saved viewport to new physical extent
    qint64 clampedMin = qMax(savedMin, newMin);
    qint64 clampedMax = qMin(savedMax, newMax);
    if (clampedMax <= clampedMin) {
        clampedMin = newMin;
        clampedMax = newMax;
    }
    GraphView->SetXBounds(clampedMin, clampedMax);
}

void Daily::RedrawGraphs()
{
    // setting this here, because it needs to be done when preferences change
    if (p_profile->cpap->showLeakRedline()) {
        schema::channel[CPAP_Leak].setUpperThreshold(p_profile->cpap->leakRedline());
    } else {
        schema::channel[CPAP_Leak].setUpperThreshold(0); // switch it off
    }

    QFont appFont = QApplication::font();
    dateDisplay->setFont(appFont);

    GraphView->redraw();
}

void Daily::on_LineCursorUpdate(double time)
{
    if (time > 1) {
        // use local time since this string is displayed to the user
        QDateTime dt = QDateTime::fromMSecsSinceEpoch(time, QTimeZone::systemTimeZone());
        QString txt = QLocale().toString(dt, "MMM dd HH:mm:ss.zzz");
        dateDisplay->setText(txt);
    } else dateDisplay->setText(QString(GraphView->emptyText()));
}

void Daily::on_RangeUpdate(double minx, double /*maxx*/)
{
    if (minx > 1) {
        dateDisplay->setText(GraphView->getRangeString());
    } else {
        dateDisplay->setText(QString(GraphView->emptyText()));
    }
}


void Daily::on_treeWidget_itemClicked(QTreeWidgetItem *item, int )
{
    if (!item) return;
    QTreeWidgetItem* parent = item->parent();
    if (!parent) return;
    gGraph *g=GraphView->findGraph(STR_GRAPH_SleepFlags);
    if (!g) return;
    QDateTime d;
    if (!item->data(0,Qt::UserRole).isNull()) {
        int eventType = parent->type();
        qint64 time = item->data(0,Qt::UserRole).toLongLong();

        // for events display 3 minutes before and 20 seconds after
        // for start time skip abit before graph
        // for end   time skip abut after graph

        qint64 period = qint64(p_profile->general->eventWindowSize())*60000L;  // eventwindowsize units  minutes
        qint64 small  = period/10;

        qint64 start,end;
        if (eventType == eventTypeStart ) {
            start = time - small;
            end = time + period;
        } else {
            start = time - period;
            end   = time + small;
        }

        GraphView->SetXBounds(start,end);
    }
}

void Daily::on_treeWidget_itemSelectionChanged()
{
    if (ui->treeWidget->selectedItems().size()==0) return;
    QTreeWidgetItem *item=ui->treeWidget->selectedItems().at(0);
    if (!item) return;
    on_treeWidget_itemClicked(item, 0);
}

void Daily::on_JournalNotesUnderline_clicked()
{
    QTextCursor cursor = ui->JournalNotes->textCursor();
    if (!cursor.hasSelection())
        cursor.select(QTextCursor::WordUnderCursor);

    QTextCharFormat format=cursor.charFormat();

    format.setFontUnderline(!format.fontUnderline());

    cursor.mergeCharFormat(format);
}

void Daily::on_prevDayButton_clicked()
{
    if (previous_date.isValid()) {
         Unload(previous_date);
    }
    if (!p_profile->ExistsAndTrue("SkipEmptyDays")) {
        LoadDate(previous_date.addDays(-1));
    } else {
        QDate d=previous_date;
        for (int i=0;i<90;i++) {
            d=d.addDays(-1);
            if (p_profile->GetDay(d)) {
                LoadDate(d);
                break;
            }
        }
    }
}

bool Daily::eventFilter(QObject *object, QEvent *event)
{
    if (object == ui->JournalNotes && event->type() == QEvent::FocusOut) {
        // Trigger immediate save of journal when we focus out from it so we never
        // lose any journal entry text...
        if (previous_date.isValid()) {
            Unload(previous_date);
        }
    }
    return false;
}

void Daily::on_nextDayButton_clicked()
{
    if (previous_date.isValid()) {
         Unload(previous_date);
    }
    if (!p_profile->ExistsAndTrue("SkipEmptyDays")) {
        LoadDate(previous_date.addDays(1));
    } else {
        QDate d=previous_date;
        for (int i=0;i<90;i++) {
            d=d.addDays(1);
            if (p_profile->GetDay(d)) {
                LoadDate(d);
                break;
            }
        }
    }
}

void Daily::on_calButton_toggled(bool checked)
{
    bool b=checked;
    ui->calendarFrame->setVisible(b);
    AppSetting->setCalendarVisible(b);

    if (!b) {
        ui->calButton->setArrowType(Qt::DownArrow);
    } else {
        ui->calButton->setArrowType(Qt::UpArrow);
    }
}


void Daily::on_todayButton_clicked()
{
    if (previous_date.isValid()) {
         Unload(previous_date);
    }
        QDate lastcpap = p_profile->LastDay(MT_CPAP);
        QDate lastoxi = p_profile->LastDay(MT_OXIMETER);

        QDate d = qMax(lastcpap, lastoxi);
    LoadDate(d);
}

void Daily::on_evViewSlider_valueChanged(int value)
{
    ui->evViewLCD->display(value);
    p_profile->general->setEventWindowSize(value);

    int winsize=value*60;

    gGraph *g=GraphView->findGraph(STR_GRAPH_SleepFlags);
    if (!g) return;
    qint64 st=g->min_x;
    qint64 et=g->max_x;
    qint64 len=et-st;
    qint64 d=st+len/2.0;

    st=d-(winsize/2)*1000;
    et=d+(winsize/2)*1000;
    if (st<g->rmin_x) {
        st=g->rmin_x;
        et=st+winsize*1000;
    }
    if (et>g->rmax_x) {
        et=g->rmax_x;
        st=et-winsize*1000;
    }
    GraphView->SetXBounds(st,et);
}

static QRegularExpression emptyStr("^\\s*$") ;
void Daily::verifyBookMarkName(QTableWidgetItem* item) {
    if (!item) {
        return;
    }

    int column=item->column();
    if (column != 1) {
        int row=item->row();
        item = ui->bookmarkTable->item(row,1);
    }
    // item now points to bookmarks that has notes (text).
    if (item->text().contains(emptyStr) ) {
        bool ok=false;
        qint64 start=item->data(Qt::UserRole).toLongLong(&ok);
        QDateTime d=QDateTime::fromSecsSinceEpoch(start/1000L, QTimeZone::systemTimeZone());
        QString text = tr("Bookmark at %1").arg(d.time().toString("HH:mm:ss"));
        item->setText(text);
    }
}

void Daily::on_bookmarkTable_currentItemChanged(QTableWidgetItem *item, QTableWidgetItem *item2)
{
    if(!item) {
        // can get here at times.
        if(item2) {
            DEBUGCI QQ(row,item2->row()) QQ(col,item2->column()) O(item2->text());
        }
        return;
    }
    int row=item->row();
    qint64 st,et;

    qint64 drift = 0;
    Day * dday=p_profile->GetDay(previous_date,MT_CPAP);
    if (dday) {
        for (Session* s : dday->sessions)
            if (s->type() == MT_CPAP) { drift = s->correctionMs(); break; }
    }

    QTableWidgetItem *it=ui->bookmarkTable->item(row,1);
    bool ok;

    // Add drift back in (it was removed in addBookmark)
    st=it->data(Qt::UserRole).toLongLong(&ok) + drift;
    et=it->data(Qt::UserRole+1).toLongLong(&ok) + drift;
    qint64 st2=0,et2=0,st3,et3;
    Day * day=p_profile->GetGoodDay(previous_date,MT_CPAP);
    if (day) {
        st2=day->first();
        et2=day->last();
    }
    Day * oxi=p_profile->GetGoodDay(previous_date,MT_OXIMETER);
    if (oxi) {
        st3=oxi->first();
        et3=oxi->last();
    }
    if (oxi && day) {
        st2=qMin(st2,st3);
        et2=qMax(et2,et3);
    } else if (oxi) {
        st2=st3;
        et2=et3;
    } else if (!day) return;
    if ((et<st2) || (st>et2)) {
        mainwin->Notify(tr("This bookmark is in a currently disabled area.."));
        return;
    }

    if (st<st2) st=st2;
    if (et>et2) et=et2;
    GraphView->SetXBounds(st,et);
    GraphView->redraw();
}

void Daily::addBookmark(qint64 st, qint64 et, QString text)
{
    ui->bookmarkTable->blockSignals(true);
    QDateTime d=QDateTime::fromSecsSinceEpoch(st/1000L, QTimeZone::systemTimeZone());
    int row=ui->bookmarkTable->rowCount();
    ui->bookmarkTable->insertRow(row);
    QTableWidgetItem *tw=new QTableWidgetItem(text);
    QTableWidgetItem *dw=new QTableWidgetItem(d.time().toString("HH:mm:ss"));
    dw->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled);
    ui->bookmarkTable->setItem(row,0,dw);
    ui->bookmarkTable->setItem(row,1,tw);
    qint64 drift = 0;
    Day * day=p_profile->GetDay(previous_date,MT_CPAP);
    if (day) {
        for (Session* s : day->sessions)
            if (s->type() == MT_CPAP) { drift = s->correctionMs(); break; }
    }

    // Counter correction for storage, in case user changes it later on
    // This won't fix the text string names..

    tw->setData(Qt::UserRole,st-drift);
    tw->setData(Qt::UserRole+1,et-drift);

    ui->bookmarkTable->blockSignals(false);
    update_Bookmarks();
    mainwin->updateFavourites();

}

void Daily::on_addBookmarkButton_clicked()
{
    qint64 st,et;
    GraphView->GetXBounds(st,et);
    QDateTime d=QDateTime::fromSecsSinceEpoch(st/1000L, QTimeZone::systemTimeZone());
    addBookmark(st,et,"" );
}

void Daily::update_Bookmarks()
{
    QVariantList start;
    QVariantList end;
    QStringList notes;
    QTableWidgetItem *item;
    for (int row=0;row<ui->bookmarkTable->rowCount();row++) {
        item=ui->bookmarkTable->item(row,1);
        verifyBookMarkName(item);

        start.push_back(item->data(Qt::UserRole));
        end.push_back(item->data(Qt::UserRole+1));
        notes.push_back(item->text());
    }
    Session *journal=GetJournalSession(previous_date);
    if (!journal) {
        journal=CreateJournalSession(previous_date);
    }
    if (!journal) { return; }

    journal->settings[Bookmark_Start]=start;
    journal->settings[Bookmark_End]=end;
    journal->settings[Bookmark_Notes]=notes;
    journal->settings[LastUpdated]=QDateTime::currentDateTime();
    journal->SetChanged(true);
    BookmarksChanged=true;
    mainwin->updateFavourites();
    UpdateCalendarDay(previous_date);   // the day's style and hint show its bookmarks
}

void Daily::on_removeBookmarkButton_clicked()
{
    int row=ui->bookmarkTable->currentRow();
    if (row>=0) {
        ui->bookmarkTable->blockSignals(true);
        ui->bookmarkTable->removeRow(row);
        ui->bookmarkTable->blockSignals(false);
        update_Bookmarks();
    }
    mainwin->updateFavourites();
}

#ifndef REMOVE_FITNESS
void Daily::set_NotesUI(QString htmlNote) {
    ui->JournalNotes->setHtml(htmlNote);
};

void Daily::set_BmiUI(double user_weight_kg) {
    if ((user_height_cm>zeroD) && (user_weight_kg>zeroD)) {
        double bmi = calculateBMI(user_weight_kg, user_height_cm);
        QString label=QString("B.M.I   %1").arg(bmi,0,'f',2);
        ui->BMIlabel->setText(label);
        ui->BMIlabel->setVisible(true);
    } else {
        // BMI now zero - remove it
        // And make it invisible
        ui->BMIlabel->setVisible(false);
    }
    if (mainwin) {
        mainwin->updateOverview();
    }
};

void Daily::set_WeightUI(double kg) {
    ui->weightSpinBox->blockSignals(true);
    ui->weightSpinBox->setDecimals(WEIGHT_SPIN_BOX_DECIMALS);
    if (p_profile->general->unitSystem()==US_Metric) {
        ui->weightSpinBox->setSuffix(QString(" %1").arg(STR_UNIT_KG));
        ui->weightSpinBox->setValue(kg);
    } else {
        double lbs = kg * pounds_per_kg;
        ui->weightSpinBox->setSuffix(QString(" %1").arg(STR_UNIT_POUND));
        ui->weightSpinBox->setValue(lbs);
    }
    ui->weightSpinBox->blockSignals(false);
    set_BmiUI(kg);
};

void Daily::set_BookmarksUI( QVariantList& start , QVariantList& end , QStringList& notes, qint64 drift) {
    ui->bookmarkTable->blockSignals(true);
    ui->bookmarkTable->setRowCount(0);
    // Careful with drift here - apply to the label but not the
    // stored data (which will be saved if journal changes occur).
    bool ok;
    int qty = start.size();
    for (int i=0;i<qty;i++) {
        qint64 st=start.at(i).toLongLong(&ok);
        qint64 et=end.at(i).toLongLong(&ok);

        QDateTime d=QDateTime::fromSecsSinceEpoch((st+drift)/1000L);
        ui->bookmarkTable->insertRow(i);
        QTableWidgetItem *tw=new QTableWidgetItem(notes.at(i));
        QTableWidgetItem *dw=new QTableWidgetItem(d.time().toString("HH:mm:ss"));
        dw->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled);
        ui->bookmarkTable->setItem(i,0,dw);
        ui->bookmarkTable->setItem(i,1,tw);
        tw->setData(Qt::UserRole,st);
        tw->setData(Qt::UserRole+1,et);
        verifyBookMarkName(tw);
    }
    ui->bookmarkTable->blockSignals(false);
}

void Daily::on_bookmarkTable_itemChanged(QTableWidgetItem *)
{
    update_Bookmarks();
}

/*
Feelings (historically called "ZombieMeter" in code)

Values are stored in the database as an integer in the range 0–100, where 0 means "not set".
The UI offers two display modes, toggled by the /10 / /100 button:
  - /100 mode: spinbox shows 0–100, single-step 10
  - /10  mode: spinbox shows 0.0–10.0 (value/10), single-step 1
The slider always moves in the 0–100 range and maps directly to the stored value.

Historical note: OSCAR 1.x used a mixed encoding — old data used 0–10 (5 = not set),
newer 1.x data used value+20 (stored 20–120 for 0–100). Both encodings were normalised
to 0–100 during the 1.x → 2.0 import.
*/
void Daily::setup_ZombieUIWidgets(int zombieValue, bool zombieMode, bool setup) {
    ui->ZombieSpinBox->blockSignals(true);
    ui->ZombieSlider->blockSignals(true);
    // Feelings values are stored as 0–100 (0 = not set). Clamp any out-of-range value.
    int value100 = qBound(0, zombieValue, 100);

    ui->ZombieSlider->setValue(value100);

    // new setup spin box
    double spinValue = value100;
    if (setup) {
        ui->ZombieSpinBox->setMaximum(zombieMode ?  100 : 10);
        ui->ZombieSpinBox->setDecimals(zombieMode ?  0 : 1);
        ui->ZombieSpinBox->setSingleStep(zombieMode ?  10 : 1);
        ui->Units10_100->setText(zombieMode ? QStringLiteral("0 .. 100") : QStringLiteral("0 .. 10.0"));
    }
    ui->ZombieSpinBox->setValue(zombieMode ? spinValue  : (spinValue/10.0));

    ui->ZombieSlider->blockSignals(false);
    ui->ZombieSpinBox->blockSignals(false);
}

void Daily::set_ZombieUI(int zombieValue, bool init)
{
    setup_ZombieUIWidgets(zombieValue, p_profile->appearance->zombieMode(),init );
}

void Daily::on_ZombieSlider_valueChanged(int uiValue)
{
    // Slider position is the value (0–100 = not set to maximum).
    set_ZombieUI(uiValue);
    set_JournalZombie(previous_date, uiValue);
    mainwin->updateOverview();
    UpdateCalendarDay(previous_date);   // the day's hint shows the feeling
}

void Daily::on_ZombieSpinBox_valueChanged(double spinValue)
{
    if (ui->ZombieSpinBox->decimals() == 1) {
        spinValue *=10;
    }
    on_ZombieSlider_valueChanged(spinValue);
}

void Daily::on_Units10_100_clicked(){
    bool mode=p_profile->appearance->zombieMode();
    p_profile->appearance->setZombieMode(!mode);
    int uiValue = ui->ZombieSlider->value();
    set_ZombieUI(uiValue, true);
    // The calendar's day hints show feelings on the chosen scale: refresh every day that has a hint,
    // including the neighbouring months' days the grid shows.
    updateCalendarDays(ui->calendar->dateTextFormat().keys());
}

void Daily::on_weightSpinBox_valueChanged(double )
{
    on_weightSpinBox_editingFinished();
}

void Daily::on_weightSpinBox_editingFinished()
{
    user_height_cm = p_profile->user->height();
    double kg = ui->weightSpinBox->value();
    if (p_profile->general->unitSystem()==US_English) {
        double lbs = ui->weightSpinBox->value();
        kg =  lbs*kgs_per_pound; // convert pounds to kg.
    }
    if (kg < zeroD) kg = 0.0;
    set_JournalWeightValue(previous_date,kg) ;
    set_BmiUI(kg);
    gGraphView *gv=mainwin->getOverview()->graphView();
    gGraph *g;
    if (gv) {
        g=gv->findGraph(STR_GRAPH_Weight);
        if (g) g->setDay(nullptr);
    }
    if (gv) {
        g=gv->findGraph(STR_GRAPH_BMI);
        if (g) g->setDay(nullptr);
    }
    mainwin->updateOverview();
    UpdateCalendarDay(previous_date);   // the day's hint shows the weight
}
#endif

QString Daily::GetDetailsText()
{
    QTextDocument * doc = webView->document();
    QString content = doc->toHtml();

    return content;
}

void Daily::setGraphText () {
    int numOff = 0;
    int numTotal = 0;

    gGraph *g;
    for (int i=0;i<GraphView->size();i++) {
        g=(*GraphView)[i];
        if (!g->isEmpty()) {
            numTotal++;
            if (!g->visible()) {
                numOff++;
            }
        }
    }
    ui->graphCombo->setItemIcon(0, numOff ? *icon_warning : *icon_up_down);
    QString graphText;
    int lastIndex = ui->graphCombo->count()-1;  // account for hideshow button
    if (numOff == 0) {
        // all graphs are shown
        graphText = QObject::tr("%1 Graphs").arg(numTotal);
        ui->graphCombo->setItemText(lastIndex,STR_HIDE_ALL_GRAPHS);
    } else {
        // some graphs are hidden
        graphText = QObject::tr("%1 of %2 Graphs").arg(numTotal-numOff).arg(numTotal);
        if (numOff == numTotal) {
            // all graphs are hidden
            ui->graphCombo->setItemText(lastIndex,STR_SHOW_ALL_GRAPHS);
        }
    }
    ui->graphCombo->setItemText(0, graphText);
    ui->graphCombo->model()->setData(
        ui->graphCombo->model()->index(lastIndex, 0),
        numOff == 0 ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
}

void Daily::setFlagText () {
    int numOff = 0;
    int numTotal = 0;

    int lastIndex = ui->eventsCombo->count()-1;  // account for hideshow button
    for (int i=1; i < lastIndex; ++i) {
        numTotal++;
        ChannelID code = ui->eventsCombo->itemData(i, Qt::UserRole).toUInt();
        #if defined(STEADY_BREATHING)
        if (p_profile->cpap->steadyBreathing()!=SB_ACTIVE) {
            if (code == CPAP_SteadyBreathing || code == CPAP_SteadyBreathingFlag)  {
                continue;
            }
        }
        #endif
        schema::Channel * chan = &schema::channel[code];

        if (!chan->enabled())
            numOff++;
    }

    int numOn=numTotal;
    ui->eventsCombo->setItemIcon(0, numOff ? *icon_warning : *icon_up_down);
    QString flagsText;
    if (numOff == 0) {
        flagsText = (QObject::tr("%1 Event Types")+"       ").arg(numTotal);
        ui->eventsCombo->setItemText(lastIndex,STR_HIDE_ALL_EVENTS);
    } else {
        numOn-=numOff;
        flagsText = QObject::tr("%1 of %2 Event Types").arg(numOn).arg(numTotal);
        if (numOn==0) ui->eventsCombo->setItemText(lastIndex,STR_SHOW_ALL_EVENTS);
    }

    ui->eventsCombo->setItemText(0, flagsText);
    ui->eventsCombo->model()->setData(
        ui->eventsCombo->model()->index(lastIndex, 0),
        numOff == 0 ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
    sleepFlagsGroup->refreshConfiguration(sleepFlags); // need to know display changes before painting.
}

void Daily::showAllGraphs(bool show) {
    //Skip over first button - label for comboBox
    int lastIndex = ui->graphCombo->count()-1;
    for (int i=1;i<lastIndex;i++) {
        showGraph(i,show);
    }
    setGraphText();
    updateCube();
    GraphView->updateScale();
    GraphView->redraw();
}

void Daily::showGraph(int index,bool show, bool updateGraph) {
    ui->graphCombo->setItemData(index,show,Qt::UserRole);
    ui->graphCombo->setItemIcon(index, show ? *icon_on : *icon_off);
    ui->graphCombo->model()->setData(
        ui->graphCombo->model()->index(index, 0),
        show ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
    if (!updateGraph) return;
    QString graphName = ui->graphCombo->itemText(index);
    gGraph* graph=GraphView->findGraphTitle(graphName);
    if (graph) graph->setVisible(show);
}

void Daily::on_graphCombo_activated(int index)
{
    if (index<0) return;
    int lastIndex = ui->graphCombo->count()-1;
    if (index > 0) {
        bool nextOn =!ui->graphCombo->itemData(index,Qt::UserRole).toBool();
        if ( index == lastIndex ) {
            // user just pressed hide show button - toggle sates of the button and apply the new state
            showAllGraphs(nextOn);
            showGraph(index,nextOn,false);
        } else {
            showGraph(index,nextOn,true);
        }
        ui->graphCombo->showPopup();
    }
    ui->graphCombo->setCurrentIndex(0);
    setGraphText();
    updateCube();
    GraphView->updateScale();
    GraphView->redraw();
}

void Daily::updateCube()
{
    //brick..
    if ((GraphView->visibleGraphs()==0)) {
        if (ui->graphCombo->count() > 0) {
            GraphView->setEmptyText(STR_Empty_NoGraphs);
        } else {
            if (!p_profile->GetGoodDay(getDate(), MT_CPAP)
              && !p_profile->GetGoodDay(getDate(), MT_OXIMETER)
              && !p_profile->GetGoodDay(getDate(), MT_SLEEPSTAGE)
              && !p_profile->GetGoodDay(getDate(), MT_POSITION)) {
                GraphView->setEmptyText(STR_Empty_NoData);

            } else {
                if (GraphView->emptyText() != STR_Empty_Brick)
                    GraphView->setEmptyText(STR_Empty_SummaryOnly);
            }
        }
    }
}


void Daily::updateGraphCombo()
{
    ui->graphCombo->clear();
    gGraph *g;

    ui->graphCombo->addItem(*icon_up_down, "", true);   // text updated in setGRaphText

    for (int i=0;i<GraphView->size();i++) {
        g=(*GraphView)[i];
        if (g->isEmpty()) continue;

        bool vis = g->visible();
        ui->graphCombo->addItem(vis ? *icon_on : *icon_off, g->title(), vis);
        int idx = ui->graphCombo->count() - 1;
        ui->graphCombo->model()->setData(
            ui->graphCombo->model()->index(idx, 0),
            vis ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
    }
    ui->graphCombo->addItem(*icon_on,STR_HIDE_ALL_GRAPHS,true);
    ui->graphCombo->setCurrentIndex(0);
    setGraphText();
    updateCube();
}

void Daily::updateEventsCombo(Day* day) {

    quint32 chans = schema::SPAN | schema::FLAG | schema::MINOR_FLAG;
    if (p_profile->general->showUnknownFlags()) chans |= schema::UNKNOWN;

    QList<ChannelID> available;
    if (day) available.append(day->getSortedMachineChannels(chans));

    ui->eventsCombo->clear();
    ui->eventsCombo->addItem(*icon_up_down, "", 0);   // text updated in setflagText
    for (int i=0; i < available.size(); ++i) {
        ChannelID code = available.at(i);
        #if defined(STEADY_BREATHING)
        if (p_profile->cpap->steadyBreathing()!=SB_ACTIVE) {
            if (code == CPAP_SteadyBreathing || code == CPAP_SteadyBreathingFlag)  {
                continue;
            }
        }
        #endif
        int comboxBoxIndex = ui->eventsCombo->count();   // not i+1: channels may be skipped above
        schema::Channel & chan = schema::channel[code];
        bool on = chan.enabled();
        ui->eventsCombo->addItem(on ? *icon_on : *icon_off, chan.label(), code);
        ui->eventsCombo->setItemData(comboxBoxIndex, chan.fullname(), Qt::ToolTipRole);
        ui->eventsCombo->model()->setData(
            ui->eventsCombo->model()->index(comboxBoxIndex, 0),
            on ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
        dailySearchTab->updateEvents(code,chan.fullname());
    }
    ui->eventsCombo->addItem(*icon_on,"" , Qt::ToolTipRole);
    ui->eventsCombo->setCurrentIndex(0);
    setFlagText();
}

void Daily::showAllEvents(bool show) {
    // Mark all events as active
    int lastIndex = ui->eventsCombo->count()-1;  // account for hideshow button
    for (int i=1;i<lastIndex;i++) {
        // If disabled, emulate a click to enable the event
        ChannelID code = ui->eventsCombo->itemData(i, Qt::UserRole).toUInt();
        #if defined(STEADY_BREATHING)
        if (p_profile->cpap->steadyBreathing()!=SB_ACTIVE) {
            if (code == CPAP_SteadyBreathing || code == CPAP_SteadyBreathingFlag)  {
                continue;
            }
        }
        #endif
        schema::Channel * chan = &schema::channel[code];
        if (chan->enabled()!=show) {
            Daily::on_eventsCombo_activated(i);
        }
    }
    ui->eventsCombo->setItemData(lastIndex,show,Qt::UserRole);
    ui->eventsCombo->setCurrentIndex(0);
    setFlagText();
}

void Daily::on_eventsCombo_activated(int index)
{
    if (index<0)
        return;

    int lastIndex = ui->eventsCombo->count()-1;
    if (index > 0) {
        if ( index == lastIndex ) {
            bool nextOn =!ui->eventsCombo->itemData(index,Qt::UserRole).toBool();
            showAllEvents(nextOn);
        } else {
            ChannelID code = ui->eventsCombo->itemData(index, Qt::UserRole).toUInt();
            schema::Channel * chan = &schema::channel[code];

            bool b = !chan->enabled();
            chan->setEnabled(b);
            ui->eventsCombo->setItemIcon(index,b ? *icon_on : *icon_off);
            ui->eventsCombo->model()->setData(
                ui->eventsCombo->model()->index(index, 0),
                b ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
        }
        ui->eventsCombo->showPopup();
    }

    ui->eventsCombo->setCurrentIndex(0);
    setFlagText();
    GraphView->redraw();
}

void Daily::on_splitter_2_splitterMoved(int, int)
{
    int size = ui->splitter_2->sizes()[0];
    if (size == 0) return;
    AppSetting->setDailyPanelWidth(size);
}

void Daily::on_graphHelp_clicked() {
    if (!saveGraphLayoutSettings) {
        saveGraphLayoutSettings= new SaveGraphLayoutSettings("daily",this);
    }
    if (saveGraphLayoutSettings) {
        saveGraphLayoutSettings->hintHelp();
    }
}

void Daily::on_layout_clicked() {
    if (!saveGraphLayoutSettings) {
        saveGraphLayoutSettings= new SaveGraphLayoutSettings("daily",this);
    }
    if (saveGraphLayoutSettings) {
        saveGraphLayoutSettings->triggerLayout(GraphView);
    }
}
