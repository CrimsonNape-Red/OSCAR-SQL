/* Redesigned Welcome page
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "welcomepage.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPixmap>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

#include "mainwindow.h"
#include "SleepLib/profiles.h"
#include "SleepLib/appsettings.h"
#include "SleepLib/schema.h"
#include "welcomemetrics.h"
#include "uikit/actiontile.h"
#include "uikit/comparison.h"
#include "uikit/eventbar.h"
#include "uikit/formatting.h"
#include "uikit/iconbutton.h"
#include "uikit/label.h"
#include "uikit/metriccard.h"
#include "uikit/panel.h"
#include "uikit/sectionheader.h"
#include "uikit/theme.h"

extern MainWindow *mainwin;

using namespace UiKit;
using Theme::Role;

namespace {

constexpr int kMaxContentWidth = 1400;   //!< Keeps lines readable on very wide windows

//! \brief Columns for a grid at \a width, from (minimum width, columns) steps, widest first
int columnsFor(int width, std::initializer_list<std::pair<int, int>> steps, int fallback)
{
    for (const auto &s : steps) {
        if (width >= s.first) return s.second;
    }
    return fallback;
}

//! \brief Puts \a widgets into \a grid, \a columns per row, with equal column widths
void placeInGrid(QGridLayout *grid, const QVector<QWidget *> &widgets, int columns)
{
    for (QWidget *w : widgets) grid->removeWidget(w);
    for (int c = 0; c < grid->columnCount(); ++c) grid->setColumnStretch(c, 0);
    for (int i = 0; i < widgets.size(); ++i) {
        grid->addWidget(widgets[i], i / columns, i % columns);
    }
    for (int c = 0; c < columns; ++c) grid->setColumnStretch(c, 1);
}

QString number(double v, int decimals)
{
    return QString::number(v, 'f', decimals);
}

//! \brief The original page's wording for how long ago \a daysAgo was
QString daysAgoText(int daysAgo)
{
    if (daysAgo == 0) return QCoreApplication::translate("Welcome", "today");
    if (daysAgo == 1) return QCoreApplication::translate("Welcome", "last night");
    if (daysAgo == 2) return QCoreApplication::translate("Welcome", "1 day ago");
    return QCoreApplication::translate("Welcome", "%2 days ago").arg(daysAgo - 1);
}

//! \brief Formats an average the same way the card formats its value
QString averageText(const MetricData &m, double avg, int nights)
{
    if (nights <= 0) return QString::fromUtf8("—");
    switch (m.id) {
    case MetricId::TimeUsed: return formatHoursMinutes(avg);
    case MetricId::LargeLeak: return number(avg, 1) + "%";
    case MetricId::Events:
    case MetricId::Sessions:
    case MetricId::Pressure: return number(avg, 1);
    default: return number(avg, 2);
    }
}

MetricCard::Tint groupTint(MetricGroup group)
{
    switch (group) {
    case MetricGroup::Breathing: return MetricCard::Tint::Breathing;
    case MetricGroup::Mask: return MetricCard::Tint::Mask;
    case MetricGroup::Usage: return MetricCard::Tint::Usage;
    }
    return MetricCard::Tint::None;
}

//! \brief A legend: swatch and text pairs in a row
QWidget *makeLegend(const QVector<QPair<Role, QString>> &entries, QWidget *parent)
{
    auto *legend = new QWidget(parent);
    auto *row = new QHBoxLayout(legend);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(14);
    for (const auto &e : entries) {
        auto *item = new QHBoxLayout();
        item->setSpacing(5);
        item->addWidget(makeSwatch(e.first, legend));
        item->addWidget(makeLabel(e.second, Theme::font(0.96), Role::Ink2, legend));
        row->addLayout(item);
    }
    row->addStretch(1);
    return legend;
}

} // namespace


WelcomePage::WelcomePage(QWidget *parent)
    : WelcomePageBase(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->installEventFilter(this);
    outer->addWidget(m_scroll);

    auto *page = new QWidget();
    page->setAutoFillBackground(true);
    QPalette pal = page->palette();
    pal.setColor(QPalette::Window, Theme::color(Role::Page));
    page->setPalette(pal);
    m_scroll->setWidget(page);

    // Centre a column of at most kMaxContentWidth
    auto *centre = new QHBoxLayout(page);
    centre->setContentsMargins(Theme::PagePadding, Theme::PagePadding, Theme::PagePadding, Theme::PagePadding + 8);
    auto *content = new QWidget(page);
    content->setMaximumWidth(kMaxContentWidth);
    centre->addStretch(0);
    centre->addWidget(content, 1);
    centre->addStretch(0);

    m_column = new QVBoxLayout(content);
    m_column->setContentsMargins(0, 0, 0, 0);
    m_column->setSpacing(Theme::SectionGap);
    m_column->addWidget(buildHeader());
    m_column->addWidget(buildTiles());
    m_column->addStretch(1);

    refreshPage();
}

QWidget *WelcomePage::buildHeader()
{
    auto *header = new QWidget();
    auto *row = new QHBoxLayout(header);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(18);

    auto *logo = new QLabel(header);
    QPixmap pix(":/icons/logo-lg.png");
    const qreal dpr = devicePixelRatioF();
    pix = pix.scaled(QSize(64, 64) * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pix.setDevicePixelRatio(dpr);
    logo->setPixmap(pix);
    logo->setFixedSize(64, 64);
    row->addWidget(logo, 0, Qt::AlignVCenter);

    auto *text = new QVBoxLayout();
    text->setSpacing(2);
    text->addWidget(makeLabel(tr("Welcome to the Open Source CPAP Analysis Reporter"),
                              Theme::font(1.9, QFont::DemiBold), Role::Ink, header, true));
    text->addWidget(makeLabel(tr("What would you like to do?"),
                              Theme::font(1.15), Role::Ink2, header));
    row->addLayout(text, 1);
    return header;
}

QWidget *WelcomePage::buildTiles()
{
    auto *box = new QWidget();
    m_tileGrid = new QGridLayout(box);
    m_tileGrid->setContentsMargins(0, 0, 0, 0);
    m_tileGrid->setSpacing(Theme::TileGap);

    auto *import = new ActionTile(LineIcon::Import, tr("Import CPAP data"), tr("Read your device's SD card"), box);
    import->setPrimary(true);
    connect(import, &QAbstractButton::clicked, this, [] { mainwin->JumpImport(); });

    auto *oxi = new ActionTile(LineIcon::Oximetry, tr("Oximetry wizard"), tr("Import pulse oximeter data"), box);
    connect(oxi, &QAbstractButton::clicked, this, [] { mainwin->JumpOxiWizard(); });

    m_dailyTile = new ActionTile(LineIcon::Daily, tr("Daily view"), tr("One night in detail"), box);
    connect(m_dailyTile, &QAbstractButton::clicked, this, [] {
        QTimer::singleShot(0, mainwin, [] { mainwin->JumpDaily(); });
    });

    m_overviewTile = new ActionTile(LineIcon::Overview, tr("Overview"), tr("Trends across nights"), box);
    connect(m_overviewTile, &QAbstractButton::clicked, this, [] {
        QTimer::singleShot(0, mainwin, [] { mainwin->JumpOverview(); });
    });

    m_statisticsTile = new ActionTile(LineIcon::Statistics, tr("Statistics"), tr("Summary tables"), box);
    connect(m_statisticsTile, &QAbstractButton::clicked, this, [] {
        QTimer::singleShot(0, mainwin, [] { mainwin->JumpStatistics(); });
    });

    auto *insights = new ActionTile(LineIcon::Insights, tr("Insights"), tr("Patterns and highlights"), box);
    insights->setTag(tr("Coming later"));
    insights->setEnabled(false);

    m_tiles = { import, oxi, m_dailyTile, m_overviewTile, m_statisticsTile, insights };
    return box;
}

QWidget *WelcomePage::buildCardWarning()
{
    auto *notice = new Panel(Role::WarnSoft, Role::WarnLine);
    auto *row = new QHBoxLayout(notice);
    row->setContentsMargins(14, 10, 14, 10);
    row->setSpacing(14);

    auto *icon = new QLabel(notice);
    QPixmap pix(":/icons/sdcard-lock.png");
    const qreal dpr = devicePixelRatioF();
    pix = pix.scaled(QSize(40, 40) * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pix.setDevicePixelRatio(dpr);
    icon->setPixmap(pix);
    icon->setFixedSize(40, 40);
    row->addWidget(icon, 0, Qt::AlignTop);

    auto *text = makeLabel(tr("Warning: ResMed S9 SD cards need to be locked before inserting them into "
                              "your computer. Some operating systems write index files to the card "
                              "without asking, which can make the card unreadable by your CPAP device."),
                           Theme::font(1.08), Role::Warn, notice, true);
    row->addWidget(text, 1);
    return notice;
}

QWidget *WelcomePage::buildNightStrip(const NightSummary &night)
{
    auto *strip = new Panel(Role::Surface, Role::Line);
    auto *row = new QHBoxLayout(strip);
    row->setContentsMargins(18, 14, 18, 14);
    row->setSpacing(18);

    auto *image = new QLabel(strip);
    QPixmap pix(night.pixmapPath);
    if (!pix.isNull()) {
        const qreal dpr = devicePixelRatioF();
        pix = pix.scaled(QSize(76, 76) * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        pix.setDevicePixelRatio(dpr);
        image->setPixmap(pix);
    }
    image->setFixedSize(76, 76);
    image->setAlignment(Qt::AlignCenter);
    row->addWidget(image);

    auto *text = new QVBoxLayout();
    text->setSpacing(2);
    text->addWidget(makeLabel(tr("Your most recent night on the %1").arg(night.deviceName),
                              Theme::font(1.45, QFont::DemiBold), Role::Ink, strip, true));
    text->addWidget(makeLabel(QLocale::system().toString(night.date, QLocale::LongFormat)
                                  + QString::fromUtf8(" · ") + daysAgoText(night.daysAgo),
                              Theme::font(1.15), Role::Ink2, strip, true));
    row->addLayout(text, 1);

    // Chip: the night's therapy mode
    Day *day = p_profile->GetDay(night.date, MT_CPAP);
    const QString mode = day ? day->getCPAPModeStr() : QString();
    if (!mode.isEmpty()) {
        row->addWidget(makeChip(tr("Mode: %1").arg(mode), Role::Ink2, Role::Surface2, strip));
    }
    return strip;
}

MetricCard *WelcomePage::buildMetricCard(const MetricData &m)
{
    auto *card = new MetricCard();
    card->setTitle(m.title);
    card->setValue(m.value, m.unit);
    card->setSubtitle(m.sub);

    Comparison c7 = compare(m.night, m.avg7, m.nights7, m.better);
    Comparison c30 = compare(m.night, m.avg30, m.nights30, m.better);
    card->setComparisons(tr("7-day avg"), averageText(m, m.avg7, m.nights7), c7,
                         tr("30-day avg"), averageText(m, m.avg30, m.nights30), c30);

    if (!m.events.isEmpty()) {
        auto *bar = new EventBar();
        bar->setParts(m.events);
        card->setExtraWidget(bar);
    }

    switch (AppSetting->welcomeCardColour()) {
    case WCC_Plain:
        break;
    case WCC_ByGroup:
        card->setTint(groupTint(m.group));
        break;
    case WCC_ByStatus:
        switch (statusTint(c7, c30)) {
        case StatusTint::Good: card->setTint(MetricCard::Tint::Good); break;
        case StatusTint::Bad: card->setTint(MetricCard::Tint::Bad); break;
        case StatusTint::None: break;
        }
        break;
    }
    return card;
}

MetricCard *WelcomePage::buildOximetryCard(const OximetrySummary &oxi)
{
    auto *card = new MetricCard();
    card->setTitle(WelcomeMetrics::cardTitle(MetricId::Oximetry));
    QStringList detail;
    if (oxi.hasSpo2) {
        card->setValue(number(oxi.spo2Middle, 1), tr("% SpO₂ (%1)").arg(oxi.middleLabel));
        detail << tr("Lowest SpO₂ %1%").arg(number(oxi.spo2Min, 0));
    } else {
        card->setValue(number(oxi.pulseMiddle, 0), schema::channel[OXI_Pulse].units());
    }
    if (oxi.hasSpo2 && oxi.hasPulse) {
        detail << tr("Pulse %1 %2").arg(number(oxi.pulseMiddle, 0), schema::channel[OXI_Pulse].units());
    }
    card->setSubtitle(detail.join(QString::fromUtf8(" · ")));
    card->setShowComparisons(false);
    if (AppSetting->welcomeCardColour() == WCC_ByGroup) {
        card->setTint(MetricCard::Tint::Breathing);
    }
    return card;
}

QWidget *WelcomePage::buildCardsSection(const NightSummary &night)
{
    auto *section = new QWidget();
    auto *v = new QVBoxLayout(section);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);

    auto *head = new QHBoxLayout();
    head->setSpacing(12);
    const QString headingDate = QLocale::system().toString(night.date, tr("MMMM d", "date format for the cards heading"));
    head->addWidget(new SectionHeader(tr("%1 compared with your averages").arg(headingDate), section));
    head->addStretch(1);
    head->addWidget(makeLegend({ { Role::Good, tr("Better than average") },
                                 { Role::Bad, tr("Worse than average") },
                                 { Role::Neutral, tr("Neither better nor worse") } }, section));
    auto *gear = new IconButton(LineIcon::Settings, tr("Choose which cards to show"), section);
    connect(gear, &QAbstractButton::clicked, this, &WelcomePage::chooseCards);
    head->addWidget(gear);
    v->addLayout(head);

    if (AppSetting->welcomeCardColour() == WCC_ByGroup) {
        v->addWidget(makeLegend({ { Role::GroupBreathLine, tr("Breathing") },
                                  { Role::GroupMaskLine, tr("Mask and pressure") },
                                  { Role::GroupUsageLine, tr("Usage") } }, section));
    }

    auto *grid = new QWidget(section);
    m_cardGrid = new QGridLayout(grid);
    m_cardGrid->setContentsMargins(0, 4, 0, 0);
    m_cardGrid->setSpacing(Theme::CardGap);
    m_cards.clear();
    const QSet<QString> hidden = WelcomeMetrics::hiddenCards();
    for (const MetricData &m : WelcomeMetrics::cards(night.date, hidden)) {
        m_cards.append(buildMetricCard(m));
    }
    if (!hidden.contains(WelcomeMetrics::cardKey(MetricId::Oximetry))) {
        OximetrySummary oxi = WelcomeMetrics::oximetry(night.date);
        if (oxi.present) m_cards.append(buildOximetryCard(oxi));
    }
    for (QWidget *c : m_cards) c->setParent(grid);
    v->addWidget(grid);
    if (m_cards.isEmpty()) {
        v->addWidget(makeLabel(tr("All cards are hidden. Use the gear button to choose cards to show."),
                               Theme::font(1.08), Role::Ink2, section, true));
    }
    return section;
}

void WelcomePage::chooseCards()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Welcome Page Cards"));
    auto *v = new QVBoxLayout(&dialog);
    v->addWidget(new QLabel(tr("Show these cards on the Welcome page:"), &dialog));

    const QSet<QString> hidden = WelcomeMetrics::hiddenCards();
    QVector<QPair<QString, QCheckBox *>> boxes;
    for (const CardInfo &card : WelcomeMetrics::catalog()) {
        auto *box = new QCheckBox(card.title, &dialog);
        box->setChecked(!hidden.contains(card.key));
        v->addWidget(box);
        boxes.append({ card.key, box });
    }
    v->addWidget(new QLabel(tr("Cards are only shown when your device records that information."), &dialog));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    v->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) return;

    // Keep keys this build doesn't know (e.g. from a newer version), so they stay hidden there
    QSet<QString> newHidden = hidden;
    for (const auto &b : boxes) {
        if (b.second->isChecked()) newHidden.remove(b.first);
        else newHidden.insert(b.first);
    }
    if (newHidden == hidden) return;
    WelcomeMetrics::setHiddenCards(newHidden);
    refreshPage();
}

QWidget *WelcomePage::buildFirstRun()
{
    auto *panel = new Panel(Role::Surface, Role::Line);
    panel->setShadow(true);
    auto *v = new QVBoxLayout(panel);
    v->setContentsMargins(22, 22, 22, 22);
    v->setSpacing(16);
    v->addWidget(makeLabel(tr("Let's get your first night into OSCAR"),
                           Theme::font(1.55, QFont::DemiBold), Role::Ink, panel, true));

    const QPair<QString, QString> steps[] = {
        { tr("Check your preferences"),
          tr("Some options in File › Preferences affect how data is imported. "
             "Some are set for you when a ResMed device is detected.") },
        { tr("Insert your SD card"),
          tr("Use a card reader, then choose Import CPAP data above. OSCAR finds the card for you.") },
        { tr("Give it a few minutes"),
          tr("The first import reads your whole history and can take a few minutes. Later imports are quick.") },
    };

    auto *grid = new QWidget(panel);
    m_stepGrid = new QGridLayout(grid);
    m_stepGrid->setContentsMargins(0, 0, 0, 0);
    m_stepGrid->setSpacing(Theme::CardGap);
    m_steps.clear();
    int n = 1;
    for (const auto &s : steps) {
        auto *step = new Panel(Role::Surface2, Role::Count, 10, grid);
        auto *sv = new QVBoxLayout(step);
        sv->setContentsMargins(14, 14, 14, 14);
        sv->setSpacing(6);
        auto *badge = new QLabel(QString::number(n++), step);
        badge->setFixedSize(26, 26);
        badge->setAlignment(Qt::AlignCenter);
        badge->setStyleSheet(QString("QLabel { color: %1; background: %2; border-radius: 13px; }")
                                 .arg(Theme::css(Role::OnAccent), Theme::css(Role::Accent)));
        badge->setFont(Theme::font(1.0));
        sv->addWidget(badge);
        sv->addWidget(makeLabel(s.first, Theme::font(1.15, QFont::DemiBold), Role::Ink, step, true));
        sv->addWidget(makeLabel(s.second, Theme::font(1.08), Role::Ink2, step, true));
        sv->addStretch(1);
        m_steps.append(step);
    }
    v->addWidget(grid);
    return panel;
}

QWidget *WelcomePage::buildNoCpapNote()
{
    auto *panel = new Panel(Role::Surface, Role::Line);
    auto *v = new QVBoxLayout(panel);
    v->setContentsMargins(18, 14, 18, 14);
    v->addWidget(makeLabel(QCoreApplication::translate("Welcome", "No CPAP data has been imported yet."),
                           Theme::font(1.15), Role::Ink2, panel, true));
    return panel;
}

void WelcomePage::refreshPage()
{
    if (!p_profile) return;

    // Tiles: same enabling rules as the original page
    const auto &cpaps = p_profile->GetMachines(MT_CPAP);
    bool noMachines = cpaps.isEmpty() && p_profile->GetMachines(MT_POSITION).isEmpty()
                      && p_profile->GetMachines(MT_OXIMETER).isEmpty()
                      && p_profile->GetMachines(MT_SLEEPSTAGE).isEmpty();
    bool showCardWarning = noMachines;
    for (Machine *mach : cpaps) {
        if (mach->brand().contains(STR_MACH_ResMed) && mach->series().contains("S9")) showCardWarning = true;
    }
    m_dailyTile->setEnabled(!noMachines);
    m_overviewTile->setEnabled(!noMachines);
    m_statisticsTile->setEnabled(!noMachines);
    if (mainwin) mainwin->EnableTabs(!noMachines);

    // Rebuild the data-dependent sections
    const int scroll = m_scroll->verticalScrollBar()->value();
    if (m_dynamic) {
        // deleteLater: the refresh may come from a button inside this section (the gear)
        m_column->removeWidget(m_dynamic);
        m_dynamic->hide();
        m_dynamic->deleteLater();
    }
    m_cardGrid = nullptr;
    m_stepGrid = nullptr;
    m_cards.clear();
    m_steps.clear();
    m_cardColumns = m_stepColumns = 0;

    m_dynamic = new QWidget();
    auto *v = new QVBoxLayout(m_dynamic);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(Theme::SectionGap);

    if (showCardWarning) v->addWidget(buildCardWarning());

    NightSummary night = WelcomeMetrics::lastNight();
    if (night.valid) {
        v->addWidget(buildNightStrip(night));
        v->addWidget(buildCardsSection(night));
    } else if (WelcomeMetrics::hasAnyData()) {
        v->addWidget(buildNoCpapNote());
    } else {
        v->addWidget(buildFirstRun());
    }

    // Above the trailing stretch
    m_column->insertWidget(m_column->count() - 1, m_dynamic);
    relayout(true);
    QTimer::singleShot(0, this, [this, scroll] { m_scroll->verticalScrollBar()->setValue(scroll); });
}

void WelcomePage::relayout(bool force)
{
    const int width = m_scroll->viewport()->width() - 2 * Theme::PagePadding;

    int tiles = columnsFor(width, { { 1050, 6 }, { 640, 3 } }, 2);
    if (force || tiles != m_tileColumns) {
        m_tileColumns = tiles;
        placeInGrid(m_tileGrid, m_tiles, tiles);
    }
    if (m_cardGrid) {
        int cards = columnsFor(width, { { 1000, 4 }, { 540, 2 } }, 1);
        if (force || cards != m_cardColumns) {
            m_cardColumns = cards;
            placeInGrid(m_cardGrid, m_cards, cards);
        }
    }
    if (m_stepGrid) {
        int steps = columnsFor(width, { { 700, 3 } }, 1);
        if (force || steps != m_stepColumns) {
            m_stepColumns = steps;
            placeInGrid(m_stepGrid, m_steps, steps);
        }
    }
}

bool WelcomePage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_scroll->viewport() && event->type() == QEvent::Resize) {
        relayout();
    }
    return WelcomePageBase::eventFilter(watched, event);
}
