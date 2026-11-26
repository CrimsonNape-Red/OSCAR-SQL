/* Insights page
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "insightspage.h"

#include <cmath>

#include <QApplication>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>

#include "SleepLib/day.h"
#include "SleepLib/insights.h"
#include "SleepLib/profiles.h"
#include "statistics.h"
#include "uikit/callout.h"
#include "uikit/label.h"
#include "uikit/panel.h"
#include "uikit/sectionheader.h"
#include "uikit/theme.h"

using namespace UiKit;
using Role = Theme::Role;

namespace {

// Pressure, quality and usage cover this many days up to the last CPAP night. The settings
// table always covers the whole history.
const int kRangeDays = 90;

QLabel *body(const QString &text, QWidget *parent, Role role = Role::Ink, bool wrap = true)
{
    return makeLabel(text, Theme::font(1.0), role, parent, wrap);
}

QLabel *bold(const QString &text, QWidget *parent, Role role = Role::Ink)
{
    return makeLabel(text, Theme::font(1.0, QFont::DemiBold), role, parent);
}

Panel *card(const QString &title, QVBoxLayout **column)
{
    auto *c = new Panel(Role::Surface, Role::Line);
    c->setShadow(true);
    *column = new QVBoxLayout(c);
    (*column)->setContentsMargins(Theme::CardPadding, Theme::CardPadding - 2, Theme::CardPadding,
                                  Theme::CardPadding);
    (*column)->setSpacing(8);
    (*column)->addWidget(makeLabel(title, Theme::font(1.15, QFont::DemiBold), Role::Ink, c));
    return c;
}

QWidget *cardRow(QWidget *left, QWidget *right)
{
    auto *w = new QWidget();
    auto *row = new QHBoxLayout(w);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(Theme::CardGap);
    row->addWidget(left, 1);
    row->addWidget(right ? right : new QWidget(w), 1);
    return w;
}

QString num(double v, int decimals = 1)
{
    // Shows 0 instead of "-0.0" for a value that rounds to zero from below
    return QLocale().toString(qAbs(v) < 0.5 * std::pow(10.0, -decimals) ? 0.0 : v, 'f', decimals);
}

QString signedNum(double v)
{
    const QString s = num(v, 2);
    return (v > 0 && s != num(0, 2)) ? "+" + s : s;
}

QString nights(int n)
{
    return n == 1 ? InsightsPage::tr("1 night") : InsightsPage::tr("%1 nights").arg(n);
}

QString date(const QDate &d)
{
    return QLocale().toString(d, MedDateFormat);
}

//! \brief Whether Statistics treats \a a and \a b as one device when it splits the periods
bool sameDevice(Machine *a, Machine *b)
{
    if (!a || !b) return a == b;
    return AppSetting->combineSimilarMachines() ? a->brand() == b->brand() : a == b;
}

QString deviceName(Machine *m)
{
    return m ? (m->brand() + " " + m->model()).trimmed() : QString();
}

//! \brief One settings period, with the nights that count towards its AHI.
struct Period {
    RXItem rx;
    QVector<Insights::NightCount> nights;
    double ahi = 0;
};

QList<Period> settingsPeriods()
{
    const bool rdi = p_profile->general->calculateRDI();
    QList<Period> periods;
    const QMap<QDate, RXItem> items = Statistics().settingsPeriods();
    for (const RXItem &rx : items) {
        Period p{rx, {}, 0};
        double events = 0, hours = 0;
        for (Day *day : rx.dates) {
            // RXChanges.cache can hold dates whose day was since deleted or purged
            if (!day) continue;
            const double h = day->hours(MT_CPAP);
            if (h <= 0) continue;
            double e = day->count(AllAhiChannels);
            if (rdi) e += day->count(CPAP_RERA);
            p.nights.append({e, h});
            events += e;
            hours += h;
        }
        if (p.nights.isEmpty()) continue;
        p.ahi = events / hours;
        periods.append(p);
    }
    return periods;
}

QWidget *header(const QDate &first, const QDate &last)
{
    auto *w = new QWidget();
    auto *row = new QHBoxLayout(w);
    row->setContentsMargins(0, 0, 0, 0);
    auto *text = new QVBoxLayout();
    text->setSpacing(2);
    text->addWidget(makeLabel(InsightsPage::tr("Insights"), Theme::font(1.9, QFont::DemiBold),
                              Role::Ink, w));
    text->addWidget(makeLabel(InsightsPage::tr("Patterns across many nights. For one night, use Daily."),
                              Theme::font(1.15), Role::Ink2, w));
    row->addLayout(text, 1);
    if (first.isValid()) {
        row->addWidget(bold(InsightsPage::tr("Last %1 days: %2 – %3").arg(kRangeDays)
                            .arg(date(first), date(last)), w, Role::Ink2),
                       0, Qt::AlignRight | Qt::AlignBottom);
    }
    return w;
}

QWidget *settingsCard(const QList<Period> &periods, const QString &ahiText)
{
    QVBoxLayout *col;
    Panel *c = card(InsightsPage::tr("Settings changes"), &col);
    col->addWidget(body(InsightsPage::tr("Each row is a period on the same settings, newest first. Its %1 "
                                         "is compared with the period before, allowing for normal "
                                         "night-to-night variation.").arg(ahiText), c, Role::Ink2));

    auto *table = new QWidget(c);
    auto *g = new QGridLayout(table);
    g->setContentsMargins(0, 6, 0, 0);
    g->setHorizontalSpacing(18);
    g->setVerticalSpacing(4);
    const QStringList heads = { InsightsPage::tr("Period"), InsightsPage::tr("Nights"),
                                InsightsPage::tr("Mode"), InsightsPage::tr("Pressure"),
                                InsightsPage::tr("Relief"), ahiText };
    for (int i = 0; i < heads.size(); ++i) {
        g->addWidget(makeLabel(heads[i], Theme::font(0.95, QFont::DemiBold), Role::Ink2, table), 0, i);
    }

    int r = 1;
    for (int i = periods.size() - 1; i >= 0; --i) {
        const Period &p = periods[i];
        const Period *before = i > 0 ? &periods[i - 1] : nullptr;

        auto *line = new QFrame(table);
        line->setFixedHeight(1);
        line->setStyleSheet(QString("background: %1;").arg(Theme::css(Role::Line)));
        g->addWidget(line, r++, 0, 1, heads.size());

        g->addWidget(bold(date(p.rx.start) + " – " + date(p.rx.end), table), r, 0);
        g->addWidget(body(QString::number(p.nights.size()), table, Role::Ink, false), r, 1);
        const QStringList now = { p.rx.mode, p.rx.pressure, p.rx.relief };
        const QStringList then = before ? QStringList{ before->rx.mode, before->rx.pressure,
                                                       before->rx.relief } : now;
        for (int s = 0; s < now.size(); ++s) {
            QLabel *l = now[s] != then[s]
                        ? makeChip(now[s], Role::AccentInk, Role::AccentSoft, table)
                        : body(now[s], table, Role::Ink, false);
            if (now[s] != then[s]) l->setToolTip(InsightsPage::tr("Changed at the start of this period"));
            g->addWidget(l, r, 2 + s, Qt::AlignLeft | Qt::AlignVCenter);
        }
        g->addWidget(bold(num(p.ahi, 2), table), r, 5);
        ++r;

        // The verdict, then the numbers behind it
        QString tag, text, detail;
        Role fg = Role::Ink3, bg = Role::Surface2;
        if (!before) {
            text = InsightsPage::tr("First period: nothing to compare with.");
        } else if (!sameDevice(before->rx.machine, p.rx.machine)) {
            // Devices score events differently, so their AHIs aren't compared
            tag = InsightsPage::tr("Device changed");
            fg = Role::AccentInk; bg = Role::AccentSoft;
            text = InsightsPage::tr("New device: %1. Not compared with the previous device, which "
                                    "may score events differently.").arg(deviceName(p.rx.machine));
        } else {
            const Insights::RxComparison cmp = Insights::compareRxPeriods(before->nights, p.nights);
            const QString from = num(before->ahi, 2), to = num(p.ahi, 2);
            switch (cmp.label) {
            case Insights::ChangeLabel::NoComparison:
                tag = InsightsPage::tr("Too few nights");
                fg = Role::Warn; bg = Role::WarnSoft;
                text = InsightsPage::tr("Too few nights to compare: each period needs at least %1.")
                       .arg(Insights::kRxMinNights);
                break;
            case Insights::ChangeLabel::Lower:
                tag = InsightsPage::tr("Lower");
                fg = Role::Good; bg = Role::GoodSoft;
                text = InsightsPage::tr("%1 fell from %2 to %3, more than normal night-to-night variation.")
                       .arg(ahiText, from, to);
                break;
            case Insights::ChangeLabel::Higher:
                tag = InsightsPage::tr("Higher");
                fg = Role::Bad; bg = Role::BadSoft;
                text = InsightsPage::tr("%1 rose from %2 to %3, more than normal night-to-night variation.")
                       .arg(ahiText, from, to);
                break;
            case Insights::ChangeLabel::Within:
                fg = Role::Neutral; bg = Role::NeutralSoft;
                if (cmp.ciLow > 0 || cmp.ciHigh < 0) {
                    // Real, but smaller than the minimum change worth reporting
                    tag = InsightsPage::tr("Small change");
                    text = InsightsPage::tr("%1 %2 then %3: a change of less than %4 events/h.")
                           .arg(ahiText, from, to, num(Insights::kMinAhiChange));
                } else {
                    tag = InsightsPage::tr("No real change");
                    text = InsightsPage::tr("%1 %2 then %3: within normal night-to-night variation.")
                           .arg(ahiText, from, to);
                }
                break;
            }
            if (cmp.valid) {
                detail = InsightsPage::tr("Change %1 events/h; 95% range %2 to %3")
                         .arg(signedNum(cmp.ahiAfter - cmp.ahiBefore), signedNum(cmp.ciLow),
                              signedNum(cmp.ciHigh));
            }
        }
        auto *v = new QWidget(table);
        auto *vr = new QHBoxLayout(v);
        vr->setContentsMargins(0, 0, 0, 6);
        vr->setSpacing(8);
        if (!tag.isEmpty()) vr->addWidget(makeChip(tag, fg, bg, v), 0, Qt::AlignTop);
        auto *texts = new QVBoxLayout();
        texts->setSpacing(1);
        texts->addWidget(body(text, v, tag.isEmpty() ? Role::Ink3 : Role::Ink));
        if (!detail.isEmpty()) {
            QLabel *d = makeLabel(detail, Theme::font(0.95), Role::Ink3, v, true);
            d->setToolTip(InsightsPage::tr("The range comes from redrawing the nights of both periods "
                                           "1,000 times. If it includes 0, the change is within normal "
                                           "variation."));
            texts->addWidget(d);
        }
        vr->addLayout(texts, 1);
        g->addWidget(v, r++, 0, 1, heads.size());
    }
    g->setColumnStretch(0, 1);
    col->addWidget(table);
    return c;
}

QWidget *pressureCards(const QList<Day *> &days)
{
    const Insights::TimeAtMax tam = Insights::timeAtMaxPressure(days);
    if (!tam.valid) return nullptr;

    QVBoxLayout *col;
    Panel *a = card(InsightsPage::tr("Time at maximum pressure"), &col);
    auto *value = new QHBoxLayout();
    value->addWidget(makeLabel(num(tam.pct) + "%", Theme::font(2.2, QFont::DemiBold), Role::Ink, a));
    value->addWidget(body(InsightsPage::tr("of mask-on time"), a, Role::Ink2), 1, Qt::AlignBottom);
    col->addLayout(value);
    col->addWidget(body(InsightsPage::tr("Time within %1 %2 of the maximum pressure setting (IPAP for "
                                         "automatic bilevel).").arg(num(Insights::kAtMaxToleranceCmH2O))
                        .arg(STR_UNIT_CMH2O), a, Role::Ink2));
    col->addStretch(1);

    // Loads each session's events: the slow part of the page
    const Insights::EventsAtPressure eap = Insights::eventsAtPressure(days);
    const QList<int> top = Insights::topPressureBins(eap);
    Panel *b = card(InsightsPage::tr("Events by pressure"), &col);
    col->addWidget(body(InsightsPage::tr("Obstructive apneas and hypopneas per hour, by the pressure "
                                         "when each began (EPAP for automatic bilevel). The highest:"),
                        b, Role::Ink2));
    if (top.isEmpty()) {
        col->addWidget(body(InsightsPage::tr("Not enough time at any one pressure yet."), b, Role::Ink3));
    } else {
        auto *t = new QWidget(b);
        auto *g = new QGridLayout(t);
        g->setContentsMargins(0, 2, 0, 0);
        g->setHorizontalSpacing(24);
        g->setVerticalSpacing(3);
        const QStringList h = { InsightsPage::tr("Pressure"), InsightsPage::tr("Time"),
                                InsightsPage::tr("Events/h") };
        for (int i = 0; i < h.size(); ++i) {
            QLabel *l = makeLabel(h[i], Theme::font(0.95, QFont::DemiBold), Role::Ink2, t);
            l->setAlignment(i ? Qt::AlignRight : Qt::AlignLeft);
            g->addWidget(l, 0, i);
        }
        for (int r = 0; r < top.size(); ++r) {
            const Insights::PressureBin &bin = eap.bins[top[r]];
            const QStringList cells = { QString("%1 %2").arg(top[r]).arg(STR_UNIT_CMH2O),
                                        InsightsPage::tr("%1 h").arg(num(bin.hours, 0)),
                                        num(bin.events / bin.hours, 2) };
            for (int i = 0; i < cells.size(); ++i) {
                QLabel *l = body(cells[i], t, Role::Ink, false);
                l->setAlignment(i ? Qt::AlignRight : Qt::AlignLeft);
                g->addWidget(l, r + 1, i);
            }
        }
        g->setColumnStretch(0, 1);
        col->addWidget(t);
    }
    col->addWidget(makeLabel(InsightsPage::tr("Pressures used for less than %1 hours are left out.")
                             .arg(num(Insights::kMinBinHours)), Theme::font(0.95), Role::Ink3, b, true));
    col->addStretch(1);
    return cardRow(a, b);
}

QWidget *qualityCard(const QList<Day *> &days, const QString &ahiText)
{
    int leak = 0, shortNights = 0, fragmented = 0, summary = 0, any = 0;
    for (Day *day : days) {
        const Insights::QualityFlags f = Insights::qualityFlags(day);
        leak += f.largeLeak;
        shortNights += f.shortNight;
        fragmented += f.fragmented;
        summary += f.summaryOnly;
        any += f.any();
    }

    QVBoxLayout *col;
    Panel *c = card(InsightsPage::tr("Nights to read with care"), &col);
    col->addWidget(body(InsightsPage::tr("%1 of %2 nights have a reason their %3 may be less reliable "
                                         "than it looks.").arg(any).arg(days.size()).arg(ahiText),
                        c, Role::Ink2));
    auto *t = new QWidget(c);
    auto *g = new QGridLayout(t);
    g->setContentsMargins(0, 2, 0, 0);
    g->setVerticalSpacing(3);
    const QList<QPair<QString, int>> rows = {
        { InsightsPage::tr("Large leak over %1% of the night").arg(num(Insights::kLargeLeakPct, 0)), leak },
        { InsightsPage::tr("Used less than %1 hours").arg(num(p_profile->cpap->complianceHours())),
          shortNights },
        { InsightsPage::tr("More than %1 sessions").arg(Insights::kMaxSessions), fragmented },
        { InsightsPage::tr("Summary data only"), summary },
    };
    for (int r = 0; r < rows.size(); ++r) {
        g->addWidget(body(rows[r].first, t, Role::Ink, false), r, 0);
        QLabel *n = bold(nights(rows[r].second), t,
                         rows[r].second ? Role::Ink : Role::Ink3);
        n->setAlignment(Qt::AlignRight);
        g->addWidget(n, r, 1);
    }
    g->setColumnStretch(0, 1);
    col->addWidget(t);
    col->addStretch(1);
    return c;
}

QWidget *usageCard(const QList<Day *> &days)
{
    const Insights::UsagePattern usage = Insights::usagePattern(days);
    // A US rule, so only for profiles set to the United States
    const bool us = p_profile->user->country() == QLocale::territoryToString(QLocale::UnitedStates);
    Insights::MedicareCheck med = us ? Insights::medicareFirst90Days(p_profile->daylist)
                                     : Insights::MedicareCheck();
    // Only useful early on: hidden from a year after the first night
    if (med.valid && med.firstNight.addYears(1) < QDate::currentDate()) {
        med = Insights::MedicareCheck();
    }
    if (!usage.valid && !med.valid) return nullptr;

    QVBoxLayout *col;
    Panel *c = card(InsightsPage::tr("Usage pattern"), &col);
    if (usage.valid) {
        auto *g = new QGridLayout();
        g->setVerticalSpacing(3);
        const QList<QStringList> rows = {
            { InsightsPage::tr("Mask usually comes off"),
              QLocale().toString(usage.medianMaskOff, QLocale::ShortFormat) },
            { InsightsPage::tr("Spread, middle half of nights"),
              InsightsPage::tr("%1 min").arg(usage.maskOffSpreadMinutes) },
            { InsightsPage::tr("Sessions a night"), QLocale().toString(usage.medianSessions) },
        };
        for (int r = 0; r < rows.size(); ++r) {
            g->addWidget(body(rows[r][0], c, Role::Ink, false), r, 0);
            QLabel *v = bold(rows[r][1], c);
            v->setAlignment(Qt::AlignRight);
            g->addWidget(v, r, 1);
        }
        g->setColumnStretch(0, 1);
        col->addLayout(g);
    }
    if (med.valid) {
        col->addSpacing(4);
        col->addWidget(bold(InsightsPage::tr("US Medicare, first 90 days"), c));
        QString tag, text;
        Role fg, bg;
        if (med.met) {
            tag = InsightsPage::tr("Met");
            fg = Role::Good; bg = Role::GoodSoft;
            text = InsightsPage::tr("%1 of %2 nights with %3 h or more, from %4")
                   .arg(med.bestNights).arg(30).arg(4).arg(date(med.bestFirstNight));
        } else if (med.complete) {
            tag = InsightsPage::tr("Not met");
            fg = Role::Bad; bg = Role::BadSoft;
            text = InsightsPage::tr("At most %1 of %2 nights with %3 h or more")
                   .arg(med.bestNights).arg(30).arg(4);
        } else {
            tag = InsightsPage::tr("Not met yet");
            fg = Role::Warn; bg = Role::WarnSoft;
            text = InsightsPage::tr("%1 of %2 nights with %3 h or more so far")
                   .arg(med.bestNights).arg(30).arg(4);
        }
        auto *m = new QHBoxLayout();
        m->setSpacing(8);
        m->addWidget(makeChip(tag, fg, bg, c), 0, Qt::AlignTop);
        m->addWidget(body(text, c, Role::Ink2), 1);
        col->addLayout(m);
        col->addWidget(makeLabel(InsightsPage::tr("%1 or more hours on at least %2 nights in some %3 nights in a "
                                                  "row, within the first %4 days from the first night "
                                                  "OSCAR holds (%5).").arg(4).arg(21).arg(30).arg(90)
                                 .arg(date(med.firstNight)), Theme::font(0.95), Role::Ink3, c, true));
    }
    col->addStretch(1);
    return c;
}

} // namespace

InsightsPage::InsightsPage(QWidget *parent)
    : QScrollArea(parent)
{
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void InsightsPage::invalidate()
{
    m_builtFor.clear();
    if (isVisible()) rebuild();
}

void InsightsPage::refreshIfShown()
{
    if (isVisible() && m_builtFor != dataKey()) rebuild();
}

void InsightsPage::showEvent(QShowEvent *event)
{
    QScrollArea::showEvent(event);
    if (m_builtFor.isEmpty() || m_builtFor != dataKey()) rebuild();
}

QString InsightsPage::dataKey() const
{
    if (!p_profile) return "none";
    // Hours and scored events catch imports, purges, disabled sessions and re-flagged events
    double hours = 0, events = 0;
    for (Day *day : std::as_const(p_profile->daylist)) {
        hours += day->hours(MT_CPAP);
        events += day->count(AllAhiChannels) + day->count(CPAP_RERA);
    }
    return QString("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11")
           .arg(quintptr(p_profile)).arg(p_profile->daylist.size()).arg(hours, 0, 'f', 4)
           .arg(events, 0, 'f', 2).arg(p_profile->cpap->leakRedline())
           .arg(p_profile->general->calculateRDI()).arg(p_profile->cpap->complianceHours())
           .arg(p_profile->user->country()).arg(QLocale().name())
           .arg(AppSetting->combineSimilarMachines())   // how Statistics groups the periods
           .arg(p_profile->session->daySplitTime().toString());
}

void InsightsPage::rebuild()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_builtFor = dataKey();

    auto *page = new QWidget();
    page->setAutoFillBackground(true);
    QPalette pal = page->palette();
    pal.setColor(QPalette::Window, Theme::color(Role::Page));
    page->setPalette(pal);
    auto *column = new QVBoxLayout(page);
    column->setContentsMargins(Theme::PagePadding, Theme::PagePadding, Theme::PagePadding,
                               Theme::PagePadding + 8);
    column->setSpacing(Theme::SectionGap);

    const QDate last = p_profile ? p_profile->LastDay(MT_CPAP) : QDate();
    if (!last.isValid()) {
        column->addWidget(header(QDate(), QDate()));
        column->addWidget(new Callout(Callout::Kind::Info, QString(),
                                      tr("Insights needs CPAP data. Import a night to see it here.")));
    } else {
        const QString ahiText = p_profile->general->calculateRDI() ? STR_TR_RDI : STR_TR_AHI;
        const QDate first = last.addDays(1 - kRangeDays);
        QList<Day *> days;
        for (auto it = p_profile->daylist.lowerBound(first);
             it != p_profile->daylist.cend() && it.key() <= last; ++it) {
            if (it.value() && it.value()->hours(MT_CPAP) > 0) days.append(it.value());
        }

        column->addWidget(header(first, last));
        column->addWidget(new Callout(Callout::Kind::Info, QString(),
            tr("Insights describes what happened. It doesn't tell you what to change: talk to your "
               "clinician before changing settings.")));
        const QList<Period> periods = settingsPeriods();
        if (!periods.isEmpty()) column->addWidget(settingsCard(periods, ahiText));
        if (QWidget *pressure = pressureCards(days)) {
            column->addWidget(new SectionHeader(tr("Pressure (automatic modes), last %1 days")
                                                .arg(kRangeDays)));
            column->addWidget(pressure);
        }
        column->addWidget(new SectionHeader(tr("Data and habits, last %1 days").arg(kRangeDays)));
        column->addWidget(cardRow(qualityCard(days, ahiText), usageCard(days)));
    }
    column->addStretch(1);
    setWidget(page);    // deletes the previous content
    QApplication::restoreOverrideCursor();
}
