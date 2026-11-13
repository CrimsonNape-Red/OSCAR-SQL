/* Overview GUI Implementation
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 * Copyright (c) 2011-2018 Mark Watkins 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#define TEST_MACROS_ENABLEDoff
#include <test_macros.h>

// Features enabled by conditional compilation.
#define ENABLE_GENERAL_MODIFICATION_OF_CALENDARS
#define ENABLE_START_END_WIDGET_DISPLAY_ACTUAL_GRAPH_RANGE

#include <QCalendarWidget>
#include <QTextCharFormat>
#include <QTimeZone>
#include <QDebug>
#include <QDateTimeEdit>
#include <QCalendarWidget>
#include <QFileDialog>
#include <QMessageBox>

#include "SleepLib/profiles.h"
#include "overview.h"
#include "overviewdetailspopup.h"

#include <QScopedValueRollback>
#include "ui_overview.h"
#include "combocheckdelegate.h"
#include "common_gui.h"
#include "Graphs/gXAxis.h"
#include "Graphs/gLineChart.h"
#include "Graphs/gYAxis.h"
#include "Graphs/gSessionTimesChart.h"
#include "Graphs/gPressureChart.h"
#include "Graphs/gAHIChart.h"
#include "Graphs/gUsageChart.h"
#include "Graphs/gTTIAChart.h"
#include "Graphs/summarylines.h"
#include "uikit/theme.h"
#include "overviewstats.h"
#include "overviewdateaxis.h"
#include "Graphs/graphtheme.h"
#include <QPainter>
#include <QTimeZone>
#include <QToolButton>
#include "cprogressbar.h"
#include "saveGraphLayoutSettings.h"

#include "mainwindow.h"

extern MainWindow *mainwin;


qint64 convertDateToTimeRtn(const QDate &date,int hours,int min,int sec) {
    // date.startOfDay was introduced in 5.14.  ubuntu 22.04 LTS used QT5.15
    #if QT_VERSION >= QT_VERSION_CHECK(5,15,0)
        return date.startOfDay().addSecs(((hours*60+min)*60)+sec).toMSecsSinceEpoch();
    #else
        // this version works in QT 5.12
        return QDateTime(date).addSecs(((hours*60+min)*60)+sec).toMSecsSinceEpoch();
    #endif
}

qint64 convertDateToStartTime(const QDate &date) {
    return convertDateToTimeRtn(date,0,10,0);
}
qint64 convertDateToEndTime(const QDate &date) {
    return convertDateToTimeRtn(date,23,0,0);
}
QDate convertTimeToDate(qint64 time) {
    return QDateTime::fromMSecsSinceEpoch(time).date();
}

Overview::Overview(QWidget *parent, gGraphView *shared) :
    QWidget(parent),
    ui(new Ui::Overview),
    m_shared(shared)
{
    chartsToBeMonitored.clear();
    chartsEmpty.clear();;
    ui->setupUi(this);

    // Set Date controls locale to 4 digit years
    QLocale locale = QLocale::system();
    shortformat = locale.dateFormat(QLocale::ShortFormat);

    if (!shortformat.toLower().contains("yyyy")) {
        shortformat.replace("yy", "yyyy");
    }

    ui->dateStart->setDisplayFormat(shortformat);
    ui->dateEnd->setDisplayFormat(shortformat);
    #if QT_VERSION < QT_VERSION_CHECK(6,7,0)
        ui->dateStart->setTimeSpec(Qt::UTC);
        ui->dateEnd->setTimeSpec(Qt::UTC);
    #else
        ui->dateStart->setTimeZone(QTimeZone("UTC"));
        ui->dateEnd->setTimeZone(QTimeZone("UTC"));
    #endif

    Qt::DayOfWeek dow = firstDayOfWeekFromLocale();

    ui->dateStart->calendarWidget()->setFirstDayOfWeek(dow);
    ui->dateEnd->calendarWidget()->setFirstDayOfWeek(dow);


    // Stop both calendar drop downs highlighting weekends in red
    QTextCharFormat format = ui->dateStart->calendarWidget()->weekdayTextFormat(Qt::Saturday);
    format.setForeground(QBrush(COLOR_Black, Qt::SolidPattern));
    ui->dateStart->calendarWidget()->setWeekdayTextFormat(Qt::Saturday, format);
    ui->dateStart->calendarWidget()->setWeekdayTextFormat(Qt::Sunday, format);
    ui->dateEnd->calendarWidget()->setWeekdayTextFormat(Qt::Saturday, format);
    ui->dateEnd->calendarWidget()->setWeekdayTextFormat(Qt::Sunday, format);
    samePage=true;

    // Connect the signals to update which days have CPAP data when the month is changed
    QVBoxLayout *framelayout = new QVBoxLayout;
    ui->graphArea->setLayout(framelayout);

    QFrame *border = new QFrame(ui->graphArea);

    framelayout->setContentsMargins(1, 1, 1, 1);
    // The redesigned page's graphs are cards on the page itself, with no frame around them all
    border->setFrameShape(AppSetting->useNewPages() ? QFrame::NoFrame : QFrame::StyledPanel);
    framelayout->addWidget(border,1);

    ///////////////////////////////////////////////////////////////////////////////
    // Create the horizontal layout to hold the GraphView object and it's scrollbar
    ///////////////////////////////////////////////////////////////////////////////
    layout = new QHBoxLayout(border);
    layout->setSpacing(0); // remove the ugly margins/spacing
    layout->setContentsMargins(0, 0, 0, 0);
    border->setLayout(layout);
    border->setAutoFillBackground(false);

    ///////////////////////////////////////////////////////////////////////////////
    // Create the GraphView Object
    ///////////////////////////////////////////////////////////////////////////////
    GraphView = new gGraphView(ui->graphArea, m_shared, this);
    // The redesigned page draws its graphs as cards; the original page keeps the 2.1 look
    GraphView->setLook(AppSetting->useNewPages() ? GraphLook::Card : GraphLook::Classic);
    GraphView->setSharedXAxis(AppSetting->useNewPages());
    GraphView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    GraphView->setEmptyText(STR_Empty_NoData);

    // Create the custom scrollbar and attach to GraphView
    scrollbar = new MyScrollBar(ui->graphArea);
    scrollbar->setOrientation(Qt::Vertical);
    scrollbar->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Expanding);
    scrollbar->setMaximumWidth(20);
    GraphView->setScrollBar(scrollbar);

    // Add the graphView and scrollbar to the layout.
    layout->addWidget(GraphView, 1);
    layout->addWidget(scrollbar, 0);
    layout->layout();

    ///////////////////////////////////////////////////////////////////////////////
    // Create date display at bottom of page
    ///////////////////////////////////////////////////////////////////////////////
    dateLabel = new MyLabel(this);
    dateLabel->setAlignment(Qt::AlignVCenter);
    dateLabel->setText(tr("[Date Widget]"));
    QFont font = dateLabel->font();
    font.setPointSizeF(font.pointSizeF()*1.3F);
    dateLabel->setFont(font);
    QPalette palette = dateLabel->palette();
    palette.setColor(QPalette::Base, Qt::blue);
    dateLabel->setPalette(palette);

    ui->dateLayout->addWidget(dateLabel,1);

    ///////////////////////////////////////////////////////////////////////////////
    // Rebuild contents
    ///////////////////////////////////////////////////////////////////////////////
    settingsLoaded=false;
    RebuildGraphs(false);

    ui->rangeCombo->setCurrentIndex(p_profile->general->lastOverviewRange());
    if (AppSetting->useNewPages()) buildRangeStrip();

    icon_on = new QIcon(":/icons/session-on.png");
    icon_off = new QIcon(":/icons/session-off.png");
    icon_up_down = new QIcon(":/icons/up-down.png");
    icon_warning = new QIcon(":/icons/warning.png");

    ui->graphCombo->view()->setItemDelegate(new ComboCheckDelegate(this));

    GraphView->resetLayout();
    GraphView->SaveDefaultSettings();
    GraphView->LoadSettings("Overview"); //no trans

    GraphView->setEmptyImage(QPixmap(":/icons/logo-md.png"));
	dateErrorDisplay = new DateErrorDisplay(this);

    connect(ui->dateStart->calendarWidget(), SIGNAL(currentPageChanged(int, int)), this, SLOT(dateStart_currentPageChanged(int, int)));
    connect(ui->dateEnd->calendarWidget(), SIGNAL(currentPageChanged(int, int)), this, SLOT(dateEnd_currentPageChanged(int, int)));
    connect(GraphView, SIGNAL(updateCurrentTime(double)), this, SLOT(on_LineCursorUpdate(double)));
    connect(GraphView, SIGNAL(updateRange(double,double)), this, SLOT(on_RangeUpdate(double,double)));
    connect(GraphView, SIGNAL(GraphsChanged()), this, SLOT(updateGraphCombo()));
    connect(GraphView, &gGraphView::summaryLinesChanged, this, &Overview::saveDottedLines);
    connect(GraphView, SIGNAL(XBoundsChanged(qint64 ,qint64)), this, SLOT(on_XBoundsChanged(qint64 ,qint64)));
    saveGraphLayoutSettings=nullptr;
}

Overview::~Overview()
{
    disconnect(GraphView, SIGNAL(XBoundsChanged(qint64 ,qint64)), this, SLOT(on_XBoundsChanged(qint64 ,qint64)));
    disconnect(GraphView, SIGNAL(GraphsChanged()), this, SLOT(updateGraphCombo()));
    disconnect(GraphView, SIGNAL(updateRange(double,double)), this, SLOT(on_RangeUpdate(double,double)));
    disconnect(GraphView, SIGNAL(updateCurrentTime(double)), this, SLOT(on_LineCursorUpdate(double)));
    disconnect(ui->dateEnd->calendarWidget(), SIGNAL(currentPageChanged(int, int)), this, SLOT(dateEnd_currentPageChanged(int, int)));
    disconnect(ui->dateStart->calendarWidget(), SIGNAL(currentPageChanged(int, int)), this, SLOT(dateStart_currentPageChanged(int, int)));
    disconnectgSummaryCharts() ;

    // Save graph orders and pin status, etc...
    GraphView->SaveSettings("Overview");//no trans

    delete ui;
	delete dateErrorDisplay;
    delete icon_on ;
    delete icon_off ;
    delete icon_up_down ;
    delete icon_warning ;
    if (saveGraphLayoutSettings!=nullptr) delete saveGraphLayoutSettings;
}

void Overview::ResetFont()
{
    QFont font = QApplication::font();
    font.setPointSizeF(font.pointSizeF()*1.3F);
    dateLabel->setFont(font);
}


void Overview::connectgSummaryCharts()
{
    for (auto it = chartsToBeMonitored.begin(); it != chartsToBeMonitored.end(); ++it) {
        gSummaryChart* sc =it.key();
        connect(sc, SIGNAL(summaryChartEmpty(gSummaryChart*,qint64,qint64,bool)), this, SLOT(on_summaryChartEmpty(gSummaryChart*,qint64,qint64,bool)));
    }
}

void Overview::disconnectgSummaryCharts()
{
    for (auto it = chartsToBeMonitored.begin(); it != chartsToBeMonitored.end(); ++it) {
        gSummaryChart* sc =it.key();
        disconnect(sc, SIGNAL(summaryChartEmpty(gSummaryChart*,qint64,qint64,bool)), this, SLOT(on_summaryChartEmpty(gSummaryChart*,qint64,qint64,bool)));
    }
    chartsToBeMonitored.clear();
    chartsEmpty.clear();;
}

void Overview::on_summaryChartEmpty(gSummaryChart*sc,qint64 firstI,qint64 lastI,bool empty)
{

    auto it = chartsToBeMonitored.find(sc);
    if (it==chartsToBeMonitored.end()) {
        return;
    }
    gGraph* graph=it.value();
    if (empty) {
        // on next range change allow empty flag to be recalculated
        chartsEmpty.insert(sc,graph);
    } else {
        // The chart has some entry with data.
        chartsEmpty.remove(sc);
        updateGraphCombo();
    }
    Q_UNUSED(firstI);
    Q_UNUSED(lastI);
};


// Create all the graphs for the Overview page
void Overview::CreateAllGraphs() {
    // The graphs are about to be replaced, and the popups belong to them
    closeDetailPopups();
    m_chartGraphs.clear();

    ///////////////////////////////////////////////////////////////////////////////
    // Add all the graphs
    // Process is to createGraph() to make the graph object, then add a layer
    // that provides the contents for that graph.
    ///////////////////////////////////////////////////////////////////////////////
    if (chartsToBeMonitored.size()>0) {
        disconnectgSummaryCharts();
    }
    chartsEmpty.clear();;
    chartsToBeMonitored.clear();;

    // Add graphs that are always included
    ChannelID ahicode = p_profile->general->calculateRDI() ? CPAP_RDI : CPAP_AHI;
    if (ahicode == CPAP_RDI) {
        AHI = createGraph("AHIBreakdown", STR_TR_RDI, tr("Respiratory\nDisturbance\nIndex"));
    } else {
        AHI = createGraph("AHIBreakdown", STR_TR_AHI, tr("Apnea\nHypopnea\nIndex"));
    }

    ahi = new gAHIChart();
    AHI->AddLayer(ahi);
    // Each graph's title strip takes its main channel's colour, or its bars' colour
    AHI->setTitleColor(schema::channel[ahicode].defaultColor());
    watchDetails(ahi, AHI);

    UC = createGraph(STR_GRAPH_Usage, tr("Usage"), tr("Usage\n(hours)"));
    uc = new gUsageChart();
    UC->AddLayer(uc);
    UC->setTitleColor(uc->mainColor());
    watchDetails(uc, UC);

    STG = createGraph("New Session", tr("Session Times"), tr("Session Times"),  YT_Time);
    stg = new gSessionTimesChart();
    STG->AddLayer(stg);
    STG->setTitleColor(stg->mainColor());
    watchDetails(stg, STG);

    PR = createGraph("Pressure Settings", STR_TR_Pressure, STR_TR_Pressure + "\n(" + STR_UNIT_CMH2O + ")");
    pres = new gPressureChart();
    PR->AddLayer(pres);
    PR->setTitleColor(schema::channel[CPAP_Pressure].defaultColor());
    watchDetails(pres, PR);

    TTIA = createGraph("TTIA", tr("Total Time in Apnea"), tr("Total Time in Apnea\n(Minutes)"));
    ttia = new gTTIAChart();
    TTIA->AddLayer(ttia);
    TTIA->setTitleColor(ttia->mainColor());
    watchDetails(ttia, TTIA);

    // Add graphs for all channels that have been marked in Preferences Dialog as wanting a graph
    QHash<ChannelID, schema::Channel *>::iterator chit;
    QHash<ChannelID, schema::Channel *>::iterator chit_end = schema::channel.channels.end();
    for (chit = schema::channel.channels.begin(); chit != chit_end; ++chit) {
        schema::Channel * chan = chit.value();

        if (chan->showInOverview()) {
            ChannelID code = chan->id();
            QString name = chan->fullname();

            #if defined(STEADY_BREATHING)
            if (p_profile->cpap->steadyBreathing()!=SB_ACTIVE ) {
                if (code == CPAP_SteadyBreathingFlag)  continue;
                if (code == CPAP_SteadyBreathing)  continue;
            }
            #endif

            if (name.length() > 16) name = chan->label();
            gGraph *G = createGraph(chan->code(), name, chan->description());
            G->setTitleColor(chan->defaultColor());
            gSummaryChart * sc = nullptr;
            if ((chan->type() == schema::FLAG) || (chan->type() == schema::MINOR_FLAG)) {
                sc = new gSummaryChart(chan->code(), chan->machtype()); // gts was MT_CPAP
                sc->addCalc(code, ST_CPH, schema::channel[code].defaultColor());
                G->AddLayer(sc);
                chartsToBeMonitored.insert(sc,G);
            } else if (chan->type() == schema::SPAN) {
                sc = new gSummaryChart(chan->code(), MT_CPAP);
                sc->addCalc(code, ST_SPH, schema::channel[code].defaultColor());
                G->AddLayer(sc);
                chartsToBeMonitored.insert(sc,G);
            } else if (chan->type() == schema::WAVEFORM) {
                if ((code==CPAP_AHI)||(code==CPAP_Pressure) ) {
                    DEBUGF O("SKIPPING") NAME(code) Q(code);
                    //skip if channel is for AHI.
                    continue;
                }
                sc= new gSummaryChart(code, chan->machtype());
                G->AddLayer(sc);
                chartsToBeMonitored.insert(sc,G);
            } else if (chan->type() == schema::UNKNOWN) {
                sc = new gSummaryChart(chan->code(), MT_CPAP);
                sc->addCalc(code, ST_CPH, schema::channel[code].defaultColor());
                G->AddLayer(sc);
                chartsToBeMonitored.insert(sc,G);
            }
            if (sc!= nullptr) {
                sc ->reCalculate();
                watchDetails(sc, G);
            }
        } // if showInOverview()
    } // for chit
    #ifndef REMOVE_FITNESS
    /* To enable these changes:   change the REMOTE_FITNESS define in appsettings.h */

    // Note The following do not use gSummaryChart. They use gOverviewGraph instead. and can not be monitored.
    WEIGHT = createGraph(STR_GRAPH_Weight, STR_TR_Weight, STR_TR_Weight, YT_Weight);
    weight = new gOverviewGraph("Weight", GT_LINE);
    weight->setMachineType(MT_JOURNAL);
    weight->addSlice(Journal_Weight, QColor("black"), ST_SETAVG);
    WEIGHT->AddLayer(weight);
    WEIGHT->setTitleColor(schema::channel[Journal_Weight].defaultColor());

    BMI = createGraph(STR_GRAPH_BMI, STR_TR_BMI, tr("Body\nMass\nIndex"));
    bmi = new gOverviewGraph("BMI", GT_LINE);
    bmi->setMachineType(MT_JOURNAL);
    bmi->addSlice(Journal_BMI, QColor("black"), ST_SETAVG);
    BMI->AddLayer(bmi);
    BMI->setTitleColor(schema::channel[Journal_BMI].defaultColor());

    ZOMBIE = createGraph(STR_GRAPH_Zombie, STR_TR_Zombie, tr("How you felt\n(1-10)"));
    zombie = new gOverviewGraph("Feeling", GT_LINE);
    zombie->setMachineType(MT_JOURNAL);
    zombie->addSlice(Journal_ZombieMeter, QColor("black"), ST_SETAVG);
    ZOMBIE->AddLayer(zombie);
    ZOMBIE->setTitleColor(schema::channel[Journal_ZombieMeter].defaultColor());
    #endif

    connectgSummaryCharts();
    loadDottedLines();
}

void Overview::buildRangeStrip()
{
    // The selection bar goes above the graphs, with no frame of its own
    ui->verticalLayout->removeWidget(ui->frame);
    ui->verticalLayout->insertWidget(0, ui->frame);
    ui->frame->setFrameShape(QFrame::NoFrame);
    ui->horizontalLayout->setSpacing(8);
    ui->horizontalLayout->setContentsMargins(8, 6, 8, 2);

    // Under it, the figures for the dates on screen
    m_statsRow = new OverviewStatsRow(this);
    ui->verticalLayout->insertWidget(1, m_statsRow);

    // One date axis for all the graphs, fixed below them so it stays in view
    m_dateAxis = new OverviewDateAxis(GraphView, this);
    ui->verticalLayout->insertWidget(ui->verticalLayout->indexOf(ui->graphArea) + 1, m_dateAxis);

    // The strip stands in for the range drop-down, which stays (hidden) as the page's record of
    // the chosen range. The hovered date is shown in bold on the date axis instead of in a label.
    ui->label_3->hide();
    ui->rangeCombo->hide();
    ui->dateStartLabel->hide();
    ui->dateEndLabel->setText(QString(QChar(0x2013)));      // a dash between the two dates
    dateLabel->hide();
    ui->dateLayout->addStretch(1);

    m_rangeStrip = new QWidget(ui->frame);
    m_rangeStrip->setObjectName("rangeStrip");
    m_rangeStrip->setAttribute(Qt::WA_StyledBackground);
    QHBoxLayout *row = new QHBoxLayout(m_rangeStrip);
    row->setContentsMargins(3, 3, 3, 3);
    row->setSpacing(2);
    m_rangeButtons = new QButtonGroup(this);
    m_rangeButtons->setExclusive(true);
    // The drop-down's ranges by item number, less the two- and six-month ones
    const QVector<QPair<int, QString>> ranges = {
        { 0, tr("1 week") }, { 1, tr("2 weeks") }, { 2, tr("1 month") }, { 4, tr("3 months") },
        { 6, tr("1 year") }, { 7, tr("All") }, { 8, tr("Custom") }, { 9, tr("Snapshot") } };
    // The same ranges as named when the window is too narrow for the full names
    const QStringList shortNames = { tr("1W"), tr("2W"), tr("1M"), tr("3M"), tr("1Y"), tr("All"), tr("Custom"), tr("Snapshot") };
    int rangeNumber = -1;
    QFont bold = font();
    bold.setBold(true);
    const QFontMetrics boldMetrics(bold);
    for (const auto &range : ranges) {
        ++rangeNumber;
        if (range.first >= ui->rangeCombo->count()) continue;
        QToolButton *button = new QToolButton(m_rangeStrip);
        button->setText(range.second);
        button->setProperty("fullText", range.second);
        button->setProperty("shortText", shortNames.at(rangeNumber));
        button->setToolTip(range.first == 9 ? tr("Keep the dates now shown as the Custom range")
                                            : ui->rangeCombo->itemText(range.first));
        button->setCheckable(true);
        button->setFocusPolicy(Qt::TabFocus);
        // Wide enough for the bold text of the chosen range, so choosing one moves nothing
        button->setMinimumWidth(boldMetrics.horizontalAdvance(range.second) + 14);
        m_rangeButtons->addButton(button, range.first);
        row->addWidget(button);
    }
    ui->horizontalLayout->insertWidget(0, m_rangeStrip);

    // The number of days on screen, after the dates
    m_daysLabel = new QLabel(ui->frame);
    ui->horizontalLayout->insertWidget(ui->horizontalLayout->indexOf(ui->zoomButton), m_daysLabel);

    // At the right: the graphs drop-down, then Layout and Hint as small square buttons that
    // name themselves in a tooltip
    ui->horizontalLayout->removeWidget(ui->graphCombo);
    ui->horizontalLayout->insertWidget(ui->horizontalLayout->indexOf(ui->graphHelp), ui->graphCombo);
    ui->horizontalLayout->removeWidget(ui->graphHelp);
    ui->horizontalLayout->addWidget(ui->graphHelp);
    ui->graphHelp->setText(QStringLiteral("?"));
    ui->graphHelp->setIcon(QIcon());
    ui->layout->setText(QString());
    ui->layout->setIconSize(QSize(16, 16));
    for (QAbstractButton *button : std::initializer_list<QAbstractButton *>{ ui->zoomButton, ui->layout, ui->graphHelp }) {
        button->setFixedSize(30, 30);
    }

    connect(m_rangeButtons, &QButtonGroup::idClicked, this, [this](int index) {
        ui->rangeCombo->setCurrentIndex(index);
        on_rangeCombo_activated(index);
    });
    const auto sync = [this](int index) {
        if (QAbstractButton *button = m_rangeButtons->button(index)) {
            button->setChecked(true);
        } else if (QAbstractButton *checked = m_rangeButtons->checkedButton()) {
            // A range kept from the original page that has no button here
            m_rangeButtons->setExclusive(false);
            checked->setChecked(false);
            m_rangeButtons->setExclusive(true);
        }
    };
    connect(ui->rangeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, sync);
    sync(ui->rangeCombo->currentIndex());
    styleRangeStrip();
    // A steady width for the graphs drop-down, whose own changes with the text it shows, so
    // the widths measured for the bar hold
    ui->graphCombo->setMinimumWidth(170);
    ui->frame->installEventFilter(this);

    // The Tab key goes along the bar from left to right
    QList<QWidget *> tabOrder;
    for (QAbstractButton *button : m_rangeButtons->buttons()) tabOrder.append(button);
    tabOrder << ui->dateStart << ui->dateEnd << ui->zoomButton << ui->graphCombo << ui->layout << ui->graphHelp;
    for (int i = 1; i < tabOrder.size(); ++i) QWidget::setTabOrder(tabOrder.at(i - 1), tabOrder.at(i));
}

void Overview::setStripCompact(bool compact)
{
    if (!m_rangeButtons || compact == m_stripCompact) return;
    m_stripCompact = compact;
    QFont bold = font();
    bold.setBold(true);
    const QFontMetrics metrics(bold);
    for (QAbstractButton *button : m_rangeButtons->buttons()) {
        const QString text = button->property(compact ? "shortText" : "fullText").toString();
        button->setText(text);
        button->setMinimumWidth(metrics.horizontalAdvance(text) + (compact ? 12 : 14));
    }
    if (m_daysLabel) m_daysLabel->setVisible(!compact);
    // The bar's layout keeps the strip's old size until told the strip has changed
    m_rangeStrip->layout()->invalidate();
    m_rangeStrip->updateGeometry();
}

bool Overview::eventFilter(QObject *watched, QEvent *event)
{
    const bool sized = event->type() == QEvent::Resize || event->type() == QEvent::Show;
    // Only once the bar is on screen: before that its buttons do not have their final font and
    // style, and the widths measured below would be wrong
    if (watched == ui->frame && sized && m_rangeStrip && ui->frame->isVisible()) {
        if (m_stripFullWidth == 0) {
            // The width the full names need, and the width the short ones need, which becomes
            // the narrowest the selection bar can be
            for (QWidget *child : ui->frame->findChildren<QWidget *>()) child->ensurePolished();
            setStripCompact(false);
            ui->horizontalLayout->invalidate();
            m_stripFullWidth = ui->horizontalLayout->minimumSize().width();
            setStripCompact(true);
            ui->horizontalLayout->invalidate();
            ui->frame->setMinimumWidth(ui->horizontalLayout->minimumSize().width());
        }
        setStripCompact(ui->frame->width() < m_stripFullWidth);
    }
    return QWidget::eventFilter(watched, event);
}

//! \brief Two stacked boxes, for the button that saves and restores the graph layout
static QIcon layoutIcon(const QColor &colour, qreal scale)
{
    QPixmap pixmap(QSize(16, 16) * scale);
    pixmap.setDevicePixelRatio(scale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(colour, 1.5));
    painter.drawRoundedRect(QRectF(2, 2.25, 12, 4.5), 1, 1);
    painter.drawRoundedRect(QRectF(2, 9.25, 12, 4.5), 1, 1);
    return QIcon(pixmap);
}

void Overview::styleRangeStrip()
{
    if (!m_rangeStrip) return;
    // The strip is a trough a little darker (lighter in dark mode) than the page; the chosen
    // range is a raised pill in it
    const QColor page = Theme::color(Theme::Role::Page);
    const QColor trough = Theme::isDark(palette()) ? page.lighter(135) : page.darker(107);
    m_rangeStrip->setStyleSheet(QString(
        "#rangeStrip { background: %1; border-radius: 9px; }"
        "QToolButton { border: none; border-radius: 7px; padding: 4px 6px; background: transparent; color: %2; }"
        "QToolButton:hover:!checked { color: %3; }"
        "QToolButton:checked { background: %4; color: %3; font-weight: bold; }")
        .arg(Theme::css(trough), Theme::css(Theme::Role::Ink2), Theme::css(Theme::Role::Ink),
             Theme::css(Theme::Role::Surface)));

    const QString box = QString(
        "QPushButton, QToolButton { border: 1px solid %1; border-radius: 8px; background: %2; color: %3;"
        " font-weight: bold; padding: 0; }"
        "QPushButton:hover, QToolButton:hover { border-color: %4; }"
        "QPushButton:pressed, QToolButton:pressed { background: %5; }")
        .arg(Theme::css(GraphTheme::outline()), Theme::css(Theme::Role::Surface), Theme::css(Theme::Role::Ink),
             Theme::css(Theme::Role::Accent), Theme::css(Theme::Role::AccentSoft));
    ui->zoomButton->setStyleSheet(box);
    ui->layout->setStyleSheet(box);
    ui->graphHelp->setStyleSheet(box);
    ui->layout->setIcon(layoutIcon(Theme::color(Theme::Role::Ink), devicePixelRatioF()));
    if (m_daysLabel) m_daysLabel->setStyleSheet(QString("color: %1;").arg(Theme::css(Theme::Role::Ink2)));
}

void Overview::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange && m_rangeStrip) {
        // A new font size changes the widths the selection bar needs: measure them again
        m_stripFullWidth = 0;
        ui->frame->setMinimumWidth(0);
        QEvent resize(QEvent::Resize);
        eventFilter(ui->frame, &resize);
    }
    if (event->type() == QEvent::PaletteChange) {
        styleRangeStrip();
        if (m_dateAxis) m_dateAxis->update();
    }
}

//! \brief Profile preference holding the dotted lines; also listed in PreferencesRepository::saveUserSettings()
static const QString kDottedLinesPref = QStringLiteral("OverviewDottedLines");

void Overview::loadDottedLines()
{
    if (!p_profile || !p_profile->general) return;
    Preferences *pref = p_profile->general->m_pref;
    if (!pref->contains(kDottedLinesPref)) return;
    const QHash<QString, quint32> masks = SummaryLines::decode((*pref)[kDottedLinesPref].toString());
    for (int i = 0; i < GraphView->size(); ++i) {
        gGraph *graph = (*GraphView)[i];
        gSummaryChart *chart = dynamic_cast<gSummaryChart *>(GraphView->findLayer(graph, LT_Overview));
        if (chart && masks.contains(graph->name())) chart->setLineMask(masks.value(graph->name()));
    }
}

void Overview::saveDottedLines()
{
    if (!p_profile || !p_profile->general) return;
    QHash<QString, quint32> masks;
    for (int i = 0; i < GraphView->size(); ++i) {
        gGraph *graph = (*GraphView)[i];
        if (graph->isSnapshot()) continue;
        gSummaryChart *chart = dynamic_cast<gSummaryChart *>(GraphView->findLayer(graph, LT_Overview));
        if (chart && chart->hasLineMask()) masks.insert(graph->name(), chart->lineMask());
    }
    p_profile->general->setPref(kDottedLinesPref, SummaryLines::encode(masks));
    p_profile->saveExtendedDataToDatabase();
}


// Recalculates Overview chart info
void Overview::RebuildGraphs(bool reset)
{
    qint64 minx, maxx;
    if (reset) {
        GraphView->GetXBounds(minx, maxx);
    }
    if (settingsLoaded) GraphView->SaveSettings("Overview");
    settingsLoaded=false;
    minRangeStartDate=p_profile->LastDay(MT_CPAP);
    maxRangeEndDate=minRangeStartDate.addDays(-1);      // force a range change;
    disconnectgSummaryCharts() ;
    GraphView->trashGraphs(true);       // Remove all existing graphs
    CreateAllGraphs();

    if (reset) {
        GraphView->resetLayout();
        GraphView->setDay(nullptr);
        SetXBounds(minx, maxx, 0, false);
        GraphView->resetLayout();
        updateGraphCombo();
    }

    // Load after resetLayout so user-adjusted heights are not overwritten by defaults.
    GraphView->LoadSettings("Overview");
    settingsLoaded = true;
    if (m_statsRow) m_statsRow->refresh();      // preferences the figures depend on may have changed
}

// Create an overview graph, adding it to the overview gGraphView object
// param QString name  The title of the graph
// param QString units The units of measurements to show in the popup
gGraph *Overview::createGraph(QString code, QString name, QString units, YTickerType yttype)
{
    int default_height = AppSetting->graphHeight();
    gGraph *g = new gGraph(code, GraphView, name, units, default_height, 0);

    gYAxis *yt;

    switch (yttype) {
    case YT_Time:
        yt = new gYAxisTime(false); // Time scale false=24hourFormat
        break;

    case YT_Weight:
        yt = new gYAxisWeight(p_profile->general->unitSystem());
        break;

    default:
        yt = new gYAxis(); // Plain numeric scale
        break;
    }

    g->AddLayer(yt, LayerLeft, gYAxis::Margin);
    gXAxisDay *x = new gXAxisDay();
    g->AddLayer(x, LayerBottom, 0, gXAxisDay::Margin);
    g->AddLayer(new gXGrid());
    return g;
}

void Overview::watchDetails(gSummaryChart *chart, gGraph *graph)
{
    m_chartGraphs.insert(chart, graph);
    connect(chart, &gSummaryChart::detailsRequested, this, &Overview::openDetails, Qt::UniqueConnection);
}

void Overview::openDetails(gSummaryChart *chart, QDate date, int idx)
{
    QPointer<gGraph> graph = m_chartGraphs.value(chart);
    Day *day = chart->dayAt(idx);
    if (!graph || !day) return;

    QString headline = chart->detailHeadline(day, idx);
    if (headline.isEmpty()) headline = graph->title();
    const QString title = QLocale().toString(date, "ddd ") + QLocale().toString(date, QLocale::ShortFormat)
                          + QStringLiteral("  \u00b7  ") + headline;
    auto *popup = new OverviewDetailsPopup(this, title, chart->detailSubtitle(day, idx), graph->titleColor(),
                                           chart->detailRows(day, idx));

    // Below and to the right of the Details button, stepped clear of any popup already there
    QPoint pos = GraphView->mapTo(this, chart->detailsButtonRect().bottomLeft().toPoint()) + QPoint(0, 8);
    for (bool moved = true; moved;) {
        moved = false;
        for (const auto &other : std::as_const(m_detailPopups)) {
            if (other && (other->pos() - pos).manhattanLength() < 20) {
                pos += QPoint(24, 24);
                moved = true;
            }
        }
    }
    pos.setX(qBound(0, pos.x(), qMax(0, width() - popup->width())));
    pos.setY(qBound(0, pos.y(), qMax(0, height() - popup->height())));
    popup->move(pos);
    popup->show();
    popup->raise();
    popup->setFocus();
    m_detailPopups.append(popup);

    // The day's date stays marked on its graph while its popup is open
    graph->setPinnedDates(graph->pinnedDates() + QSet<QDate>{date});
    QPointer<OverviewDetailsPopup> guard(popup);
    connect(popup, &OverviewDetailsPopup::closed, this, [this, graph, date, guard]() {
        m_detailPopups.removeAll(guard);
        if (!graph) return;
        bool stillOpen = false;     // another popup for the same day on the same graph
        for (const auto &other : std::as_const(m_detailPopups)) {
            if (other && other->property("graph").value<QObject *>() == graph.data()
                && other->property("date").toDate() == date) stillOpen = true;
        }
        if (!stillOpen) {
            QSet<QDate> pins = graph->pinnedDates();
            pins.remove(date);
            graph->setPinnedDates(pins);
            GraphView->redraw();
        }
    });
    popup->setProperty("graph", QVariant::fromValue<QObject *>(graph.data()));
    popup->setProperty("date", date);
    GraphView->redraw();
}

void Overview::closeDetailPopups()
{
    const auto popups = m_detailPopups;
    m_detailPopups.clear();
    for (const auto &popup : popups) {
        if (popup) popup->close();
    }
    for (auto it = m_chartGraphs.begin(); it != m_chartGraphs.end(); ++it) {
        if (it.value()) it.value()->setPinnedDates({});
    }
}

void Overview::hideEvent(QHideEvent *event)
{
    // Leaving the tab closes the popups; minimising OSCAR (a spontaneous hide) keeps them
    if (!event->spontaneous()) closeDetailPopups();
    QWidget::hideEvent(event);
}

void Overview::on_LineCursorUpdate(double time)
{
    if (time > 1) {
        // even though the generated string is displayed to the user
        // no time zone conversion is neccessary, so pass UTC
        // to prevent QT from automatically converting to local time
        QDateTime dt = QDateTime::fromMSecsSinceEpoch(time, QTimeZone::systemTimeZone()/*, Qt::UTC*/);
        QString txt = QLocale().toString(dt, "dd MMM yyyy (dddd)");
        dateLabel->setText(txt);
    } else dateLabel->setText(QString(GraphView->emptyText()));
}

void Overview::on_RangeUpdate(double minx, double /* maxx */)
{
    if (minx > 1) {
        dateLabel->setText(GraphView->getRangeInDaysString());
    } else {
        dateLabel->setText(QString(GraphView->emptyText()));
    }
}

void Overview::ReloadGraphs()
{
    GraphView->setDay(nullptr);
    updateCube();

    // With no data there are no figures or dates to show, so their rows give up their space
    const bool hasData = p_profile && p_profile->FirstDay().isValid();
    if (m_statsRow) {
        m_statsRow->setVisible(hasData);
        // The dates may be the same as before, but the data or a preference may not be
        m_statsRow->refresh();
    }
    if (m_dateAxis) m_dateAxis->setVisible(hasData);

    // A reload keeps the same range (it follows a light/dark switch, a journal edit or an
    // import), so the details popups stay open
    const QScopedValueRollback<bool> keep(m_keepDetailPopups, true);
    on_rangeCombo_activated(ui->rangeCombo->currentIndex());
}

void Overview::setGraphText () {
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
    int lastIndex = ui->graphCombo->count()-1;

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

void Overview::updateGraphCombo()
{
    ui->graphCombo->clear();
    gGraph *g;

    ui->graphCombo->addItem(*icon_up_down, "", true);
    for (int i = 0; i < GraphView->size(); i++) {
        g = (*GraphView)[i];

        if (g->isEmpty()) { continue; }

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

void Overview::RedrawGraphs()
{
    GraphView->redraw();
}

// Updates calendar format and profile data.
void Overview::UpdateCalendarDay(QDateEdit *dateedit, QDate date,bool startDateWidget)
{
    QCalendarWidget *calendar = dateedit->calendarWidget();
    bool hascpap = p_profile->FindDay(date, MT_CPAP) != nullptr;
    bool hasoxi = p_profile->FindDay(date, MT_OXIMETER) != nullptr;

    QTextCharFormat normal;
    normal.setForeground(QBrush(Qt::blue, Qt::SolidPattern));
    normal.setFontWeight(QFont::Bold);
    if (startDateWidget) {
        // reduce font size if range can not be reached
        if (date > uiEndDate) {
            normal.setFontWeight(QFont::Light);
        }
    } else if (date < uiStartDate) {
        // reduce font size if range can not be reached
        normal.setFontWeight(QFont::Light);
    }

    if (hascpap) {
        if (hasoxi) {
            normal.setForeground(QBrush(Qt::red, Qt::SolidPattern));
            calendar->setDateTextFormat(date, normal);
        } else {
            normal.setForeground(QBrush(Qt::blue, Qt::SolidPattern));
            calendar->setDateTextFormat(date, normal);
        }
    } else if (p_profile->GetDay(date)) {
        calendar->setDateTextFormat(date, normal);
    } else {
        // is invalid.
        normal.setForeground(QBrush(Qt::gray, Qt::SolidPattern));
        normal.setFontWeight(QFont::Light);
        calendar->setDateTextFormat(date, normal);
    }

    calendar->setHorizontalHeaderFormat(QCalendarWidget::ShortDayNames);
}

void Overview::SetXBounds(qint64 start, qint64 end, short group , bool refresh )
{
     GraphView->SetXBounds( start , end ,group,refresh);
}

void Overview::SetXBounds(QDate & start, QDate&  end, short group , bool refresh ) {
    SetXBounds(convertDateToStartTime(start),convertDateToEndTime(end),group,refresh);
}


void Overview::on_XBoundsChanged(qint64 start,qint64 end)
{
    displayStartDate = convertTimeToDate(start);
    displayEndDate = convertTimeToDate(end);

    if (m_statsRow) {
        // The dates the graphs draw for these bounds (as gSummaryChart::paint works them out),
        // so the figures and the graphs' dotted lines cover the same days
        const QDate first = QDateTime::fromMSecsSinceEpoch(start, QTimeZone::systemTimeZone()).date();
        const int days = qMax(1, int(std::ceil(double(end - start) / 86400000.0)));
        m_statsRow->setRange(first, first.addDays(days - 1));
        if (m_daysLabel) m_daysLabel->setText(days == 1 ? tr("1 night") : tr("%1 nights").arg(days));
    }

    bool largerRange=false;
    if (displayStartDate>maxRangeEndDate || minRangeStartDate>displayEndDate) {
        // have non-overlaping ranges
        // Only occurs when custom mode is switched to/from a latest mode. custom mode to/from last week.
        // All other displays expand the existing range.
        // reset all empty flags to not empty
        largerRange=true;
        chartsEmpty = QHash<gSummaryChart*, gGraph*>( chartsToBeMonitored );
        minRangeStartDate = displayStartDate;
        maxRangeEndDate   = displayEndDate;
    } else {
        // new range overlaps with old range
        if (displayStartDate<minRangeStartDate) {
            largerRange=true;
            minRangeStartDate = displayStartDate;
        }
        if (displayEndDate>maxRangeEndDate) {
            largerRange=true;
            maxRangeEndDate   = displayEndDate;
        }
    }

    if (largerRange) {
        for (auto it= chartsEmpty.begin();it!=chartsEmpty.end();it++) {
            gSummaryChart* sc = it.key();
            bool empty=sc->isEmpty();
            if (empty) {
                sc ->reCalculate();
                GraphView->updateScale();
                GraphView->redraw();
                GraphView->timedRedraw(150);
            }
        }
        chartsEmpty.clear();
        updateGraphCombo();
    }
    #if defined(ENABLE_START_END_WIDGET_DISPLAY_ACTUAL_GRAPH_RANGE)
    setRange(displayStartDate,displayEndDate, false);
    #endif
}

void Overview::dateStart_currentPageChanged(int year, int month)
{
    QDate d(year, month, 1);
    int dom = d.daysInMonth();

    for (int i = 1; i <= dom; i++) {
        d = QDate(year, month, i);
        UpdateCalendarDay(ui->dateStart, d,true /*startWidget*/);
    }
}

void Overview::dateEnd_currentPageChanged(int year, int month)
{
    QDate d(year, month, 1);
    int dom = d.daysInMonth();
    for (int i = 1; i <= dom; i++) {
        d = QDate(year, month, i);
        UpdateCalendarDay(ui->dateEnd, d,false /* not startWidget*/);
    }
}


void Overview::on_dateEnd_dateChanged(const QDate &date)
{
    if (!m_keepDetailPopups) closeDetailPopups();
	if (date<uiStartDate) {
		dateErrorDisplay->error(false,date);
		return;
	}
    QDate d2(date);
    if (customMode) {
        p_profile->general->setCustomOverviewRangeEnd(d2);
    }
    setRange(uiStartDate,d2, true);
    if ( (uiStartDate.month() ==uiEndDate.month()) && (uiStartDate.year() ==uiEndDate.year()) ) {
        dateEnd_currentPageChanged(uiEndDate.year(),uiEndDate.month());
    }
}

void Overview::on_dateStart_dateChanged(const QDate &date)
{
    if (!m_keepDetailPopups) closeDetailPopups();
	if (date>uiEndDate) {
		// change date back to last date.
		dateErrorDisplay->error(true,date);
		return;
    }
    QDate d1(date);
    if (customMode) {
        p_profile->general->setCustomOverviewRangeStart(d1);
    }
    setRange(d1, uiEndDate, true);
    if ( (uiStartDate.month() ==uiEndDate.month()) && (uiStartDate.year() ==uiEndDate.year()) ) {
        dateStart_currentPageChanged(uiStartDate.year(),uiStartDate.month());
    }
}

// Zoom to 100% button clicked or called back from 100% zoom in popup menu
void Overview::on_zoomButton_clicked()
{
    // the Current behaviour is to zoom back to the last range created by on_rangeCombo_activation
    // This change preserves OSCAR behaviour
    on_rangeCombo_activated(p_profile->general->lastOverviewRange());  // type of range in last use
}


void Overview::ResetGraphLayout()
{
    GraphView->resetLayout();
}

void Overview::ResetGraphOrder(int type)
{
    Q_UNUSED(type)
    GraphView->resetGraphOrder(false);
    ResetGraphLayout();
}

// Process new range selection from combo button
void Overview::on_rangeCombo_activated(int index)
{
    if (!m_keepDetailPopups) closeDetailPopups();
    // Exclude Journal in calculating the last day
    QDate end = p_profile->LastDay(MT_CPAP);
    end = max(end, p_profile->LastDay(MT_OXIMETER));
    end = max(end, p_profile->LastDay(MT_POSITION));
    end = max(end, p_profile->LastDay(MT_SLEEPSTAGE));
    QDate start;


    if (index == 0) {
        start = end.addDays(-6);
    } else if (index == 1) {
        start = end.addDays(-13);
    } else if (index == 2) {
        start = end.addMonths(-1).addDays(1);
    } else if (index == 3) {
        start = end.addMonths(-2).addDays(1);
    } else if (index == 4) {
        start = end.addMonths(-3).addDays(1);
    } else if (index == 5) {
        start = end.addMonths(-6).addDays(1);
    } else if (index == 6) {
        start = end.addYears(-1).addDays(1);
    } else if (index == 7) { // Everything
        start = p_profile->FirstDay();
    } else if (index == 8 || index == 9) { // Custom
        // Validate save Overview Custom Range for first access.
        if (!p_profile->general->customOverviewRangeStart().isValid()
            || (!p_profile->general->customOverviewRangeEnd().isValid() )
            || (index==9 /* New Coustom mode - to reset custom range to displayed date range*/)
        ) {
            // Reset Custom Range to current range displayed
            // on first initialization of this version of OSCAR
            // or on new custom Mode to reset range.
            qint64 istart,iend;
            GraphView->GetXBounds(istart , iend);
            start = QDateTime::fromMSecsSinceEpoch( istart ).date();
            end =   QDateTime::fromMSecsSinceEpoch( iend ).date();
            p_profile->general->setCustomOverviewRangeStart(start);
            p_profile->general->setCustomOverviewRangeEnd(end);
            index=8;
            ui->rangeCombo->setCurrentIndex(index);
        } else {
            // have a change in RangeCombo selection. Use last saved values.
            start = p_profile->general->customOverviewRangeStart() ;
            end   = p_profile->general->customOverviewRangeEnd() ;
        }
    }

    if (start < p_profile->FirstDay()) { start = p_profile->FirstDay(); }

    customMode = (index == 8) ;
    #if !defined(ENABLE_GENERAL_MODIFICATION_OF_CALENDARS)
    ui->dateStartLabel->setEnabled(customMode);
    ui->dateEndLabel->setEnabled(customMode);
    ui->dateEnd->setEnabled(customMode);
    ui->dateStart->setEnabled(customMode);
    #endif


    p_profile->general->setLastOverviewRange(index);  // type of range in last use

    // Ensure that all summary files are available and update version numbers if required
    int size = start.daysTo(end);
    QDate dateback = end;
    CProgressBar progress(QObject::tr("Loading summaries"), mainwin, size);
    for (int i=1; i < size; ++i) {
        progress.add(1);
        auto di = p_profile->daylist.find(dateback);
        dateback = dateback.addDays(-1);
        if (di == p_profile->daylist.end())  // Check for no Day entry
           continue;
        Day * day = di.value();
        if (!day)
            continue;
        if (day->size() <= 0)
            continue;
        day->OpenSummary();     // This can be slow if summary needs to be updated to new version
    }
    progress.close();

    setRange(start, end);
}


DateErrorDisplay::DateErrorDisplay (Overview* overview)
	:	m_overview(overview)
{
	m_visible=false;
	m_timer = new QTimer();
	connect(m_timer, SIGNAL(timeout()),this, SLOT(timerDone()));
};

DateErrorDisplay::~DateErrorDisplay() {
	disconnect(m_timer, SIGNAL(timeout()),this, SLOT(timerDone()));
	delete m_timer;
};

void DateErrorDisplay::cancel() {
	m_visible=false;
	m_timer->stop();
};

void DateErrorDisplay::timerDone() {
	m_visible=false;
	m_overview->resetUiDates();
	m_overview->graphView()->m_parent_tooltip->cancel();
};

int Overview::calculatePixels(bool startDate,ToolTipAlignment& align) {
	// Center error message over start and end dates combined.
	// Other allignement were tested but this is the best for this problem.
	Q_UNUSED(startDate);
	int space=4;
	align=TT_AlignCenter;
	return ((4*space) + ui->label_3->width() + ui->rangeCombo->width() + ui->dateStartLabel->width()  +ui->dateStart->width() );
}

void DateErrorDisplay::error(bool startDate,const QDate& dateEntered) {
	m_startDate =m_overview->uiStartDate;
	m_endDate =m_overview->uiEndDate;
	ToolTipAlignment align=TT_AlignCenter;


	QString dateFormatted =dateEntered.toString(m_overview->shortformat);

	QString txt = QString(tr("ERROR\nThe start date MUST be before the end date"));
	txt.append("\n");
	if (startDate) {
		txt.append(tr("The entered start date %1 is after the end date %2").arg(dateFormatted).arg(m_endDate.toString(m_overview->shortformat)));
		txt.append(tr("\nHint: Change the end date first"));
	} else {
		txt.append(tr("The entered end date %1 ").arg(dateFormatted));
		txt.append(tr("is before the start date %1").arg(m_startDate.toString(m_overview->shortformat)));
		txt.append(tr("\nHint: Change the start date first"));
	}

	gGraphView* gv=m_overview->graphView();

	int pixelsFromLeft = m_overview->calculatePixels(startDate,align);
	int pixelsAboveBottom = 0;
	int warningDurationMS =4000;
	int resetUiDatesTimerDelayMS =1000;
	QFont* font=defaultfont;

	gv->m_parent_tooltip->display(gv,txt,pixelsAboveBottom,pixelsFromLeft, align, warningDurationMS , font);

	m_timer->setInterval( resetUiDatesTimerDelayMS );
	m_timer->setSingleShot(true);
	m_timer->start();

};

void Overview::resetUiDates() {
    ui->dateStart->blockSignals(true);
    ui->dateStart->setMinimumDate(p_profile->FirstDay());  // first and last dates for ANY machine type
    ui->dateStart->setMaximumDate(p_profile->LastDay());
    ui->dateStart->setDate(uiStartDate);
    ui->dateStart->blockSignals(false);

    ui->dateEnd->blockSignals(true);
    ui->dateEnd->setMinimumDate(p_profile->FirstDay());  // first and last dates for ANY machine type
    ui->dateEnd->setMaximumDate(p_profile->LastDay());
    ui->dateEnd->setDate(uiEndDate);
    ui->dateEnd->blockSignals(false);
}

// Saves dates in UI, clicks zoom button, and updates combo box
// 1. Updates the dates in the start / end date boxs
// 2. optionally also changes display range for graphs.
void Overview::setRange(QDate& start, QDate& end, bool updateGraphs/*zoom*/)
{

	if (start>end) {
		// this is an ERROR and shold NEVER occur.
		return;
	}

    // first setting of the date (setDate) will cause pageChanged to be executed.
    // The pageChanged processing requires access to the other ui's date.
    // so save them to memory before the first call.
    uiStartDate =start;
    uiEndDate = end;

    bool nextSamePage= (start.year() == end.year() && start.month() == end.month()) ;
    if (samePage>0 ||nextSamePage) {
        // The widgets do not signal pageChanged on opening - since the page hasn't changed.
        // however the highlighting may need to be changed.
        // The following forces a page change signal when calendar widget is opened.
        ui->dateStart->calendarWidget()->setCurrentPage(1970,1);
        ui->dateEnd->calendarWidget()->setCurrentPage(1970,1);
        samePage=nextSamePage;
    }

	resetUiDates();
    if (updateGraphs) SetXBounds(uiStartDate,uiEndDate);
    updateGraphCombo();
}

void Overview::showGraph(int index,bool show, bool updateGraph) {
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

void Overview::showAllGraphs(bool show) {
    //Skip over first button - label for comboBox
    int lastIndex = ui->graphCombo->count()-1;
    for (int i=1;i<lastIndex;i++) {
        showGraph(i,show,true);
    }
}


void Overview::on_graphCombo_activated(int index)
{
   if (index<0) return;
    if (index > 0) {
        bool nextOn =!ui->graphCombo->itemData(index,Qt::UserRole).toBool();
        int lastIndex = ui->graphCombo->count()-1;
        if ( index == lastIndex ) {
            // user just pressed hide show button - toggle states of the button and apply the new state
            showAllGraphs(nextOn);
            showGraph(index,nextOn,false);
        } else {
            showGraph(index,nextOn,true);
        }
        ui->graphCombo->showPopup();
    }

    ui->graphCombo->setCurrentIndex(0);
    updateCube();
    setGraphText();
    GraphView->updateScale();
    GraphView->redraw();
}
void Overview::updateCube()
{
    if ((GraphView->visibleGraphs() == 0)) {
        if (ui->graphCombo->count() > 0) {
            GraphView->setEmptyText(STR_Empty_NoGraphs);
        } else {
            GraphView->setEmptyText(STR_Empty_NoData);
        }
    }
}

void Overview::on_graphHelp_clicked() {
    if (!saveGraphLayoutSettings) {
        saveGraphLayoutSettings= new SaveGraphLayoutSettings("overview",this);
    }
    if (saveGraphLayoutSettings) {
        saveGraphLayoutSettings->hintHelp();
    }
}

void Overview::on_layout_clicked() {
    if (!saveGraphLayoutSettings) {
        saveGraphLayoutSettings= new SaveGraphLayoutSettings("overview",this);
    }
    if (saveGraphLayoutSettings) {
        saveGraphLayoutSettings->triggerLayout(GraphView);
    }
}



