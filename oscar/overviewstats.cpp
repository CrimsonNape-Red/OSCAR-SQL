/* Overview statistics row
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "overviewstats.h"
#include <QGuiApplication>

#include <QApplication>
#include <QPainter>
#include <cmath>

#include "Graphs/rangefigures.h"
#include "Graphs/graphtheme.h"
#include "SleepLib/common.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "uikit/formatting.h"
#include "uikit/theme.h"

namespace OverviewStats {

Range previousRange(const Range &shown)
{
    const qint64 days = shown.start.daysTo(shown.end) + 1;
    return { shown.start.addDays(-days), shown.start.addDays(-1) };
}

Verdict verdict(double now, double before, UiKit::Better better, int decimals)
{
    const double scale = std::pow(10.0, decimals);
    const double a = std::round(now * scale), b = std::round(before * scale);
    if (a == b || better == UiKit::Better::Neutral) return Verdict::Neutral;
    const bool lower = a < b;
    return (lower == (better == UiKit::Better::Lower)) ? Verdict::Good : Verdict::Bad;
}

namespace {

const QString kDash = QString(QChar(0x2014));

//! \brief An up or down arrow and the size of the change; empty when unchanged at \a decimals
QString changeText(double now, double before, int decimals)
{
    const double scale = std::pow(10.0, decimals);
    const double diff = (std::round(now * scale) - std::round(before * scale)) / scale;
    if (diff == 0) return QString(QChar(0x2013)) + " " + QObject::tr("no change");
    return QString(QChar(diff > 0 ? 0x25B2 : 0x25BC)) + " " + QString::number(std::abs(diff), 'f', decimals);
}

} // namespace

QVector<Stat> compute(const Range &shown)
{
    QVector<Stat> stats;
    if (!p_profile || !shown.start.isValid() || !shown.end.isValid()) return stats;

    const Range before = previousRange(shown);
    const int days = RangeFigures::daysUsed(shown.start, shown.end);
    const bool hasBefore = RangeFigures::daysUsed(before.start, before.end) > 0;

    // One figure: its value now and before, how to show it and which direction is better
    const auto add = [&](const QString &title, double now, double was, int decimals, const QString &unit,
                         UiKit::Better better, bool available = true, bool wasAvailable = true) {
        Stat stat;
        stat.title = title;
        stat.unit = unit;
        if (days == 0 || !available) {
            stat.value = kDash;
            stat.unit.clear();
        } else {
            stat.value = QString::number(now, 'f', decimals);
            if (hasBefore && wasAvailable) {
                stat.change = changeText(now, was, decimals);
                stat.verdict = verdict(now, was, better, decimals);
            }
        }
        stats.append(stat);
    };

    add(p_profile->general->calculateRDI() ? STR_TR_RDI : STR_TR_AHI,
        RangeFigures::ahi(shown.start, shown.end), RangeFigures::ahi(before.start, before.end),
        2, QObject::tr("events/hour"), UiKit::Better::Lower,
        RangeFigures::ahiAvailable(shown.start, shown.end), RangeFigures::ahiAvailable(before.start, before.end));

    add(QObject::tr("Average usage"),
        RangeFigures::usageAverage(shown.start, shown.end), RangeFigures::usageAverage(before.start, before.end),
        2, QObject::tr("hours/night"), UiKit::Better::Higher);

    // Every day of a period counts for compliance, so a previous period that starts before the
    // profile's first day would read low: it is not compared
    const QDate firstDay = p_profile->FirstDay(MT_CPAP);
    const int compliant = days > 0 ? p_profile->countCompliantDays(MT_CPAP, shown.start, shown.end) : 0;
    add(QObject::tr("Compliance"),
        RangeFigures::compliancePercent(shown.start, shown.end), RangeFigures::compliancePercent(before.start, before.end),
        1, QObject::tr("% (%1 of %2 days)").arg(compliant).arg(shown.start.daysTo(shown.end) + 1), UiKit::Better::Higher,
        true, firstDay.isValid() && before.start >= firstDay);

    add(QObject::tr("Leak rate (%1)").arg(RangeFigures::middleLabel()),
        RangeFigures::middle(CPAP_Leak, MT_CPAP, shown.start, shown.end),
        RangeFigures::middle(CPAP_Leak, MT_CPAP, before.start, before.end),
        2, schema::channel[CPAP_Leak].units(), UiKit::Better::Lower,
        RangeFigures::middleAvailable(CPAP_Leak, MT_CPAP, shown.start, shown.end),
        RangeFigures::middleAvailable(CPAP_Leak, MT_CPAP, before.start, before.end));

    // A percentile needs detailed data: a range of summary-only days has no figure, not a zero
    ChannelID pressure = RangeFigures::pressureChannel(shown.start, shown.end);
    if (pressure != NoChannel && !RangeFigures::hasDetail(pressure, MT_CPAP, shown.start, shown.end)) pressure = NoChannel;
    add(QObject::tr("Pressure (%1)").arg(RangeFigures::percentileLabel()),
        pressure == NoChannel ? 0 : RangeFigures::percentile(pressure, MT_CPAP, shown.start, shown.end),
        pressure == NoChannel ? 0 : RangeFigures::percentile(pressure, MT_CPAP, before.start, before.end),
        2, pressure == NoChannel ? QString() : schema::channel[pressure].units(), UiKit::Better::Neutral,
        pressure != NoChannel,
        pressure != NoChannel && RangeFigures::hasDetail(pressure, MT_CPAP, before.start, before.end));

    return stats;
}

} // namespace OverviewStats

namespace {
constexpr int kMargin = 8;      //!< Space around the row and between tiles
constexpr int kTileHeight = 58;
constexpr int kMinTileWidth = 150;
}

OverviewStatsRow::OverviewStatsRow(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_timer.setSingleShot(true);
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        // Not while the graphs are being dragged: the figures wait until the mouse is let go,
        // so each step of the drag redraws the graphs only
        if (QGuiApplication::mouseButtons() != Qt::NoButton) {
            m_timer.start();
            return;
        }
        recompute();
    });
}

void OverviewStatsRow::setRange(const QDate &start, const QDate &end)
{
    m_range = { start, end };
    m_timer.start();
}

void OverviewStatsRow::refresh()
{
    if (m_range.start.isValid()) m_timer.start();
}

//! \brief Works the figures out for the dates last given to setRange() and redraws the row
void OverviewStatsRow::recompute()
{
    m_stats = OverviewStats::compute(m_range);
    const qint64 days = m_range.start.isValid() ? m_range.start.daysTo(m_range.end) + 1 : 0;
    m_caption = days == 1 ? tr("vs previous day") : tr("vs previous %1 days").arg(days);
    update();
}

QSize OverviewStatsRow::sizeHint() const
{
    return QSize(5 * kMinTileWidth + 6 * kMargin, kTileHeight + kMargin);
}

QSize OverviewStatsRow::minimumSizeHint() const
{
    return QSize(3 * kMinTileWidth, kTileHeight + kMargin);
}

void OverviewStatsRow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (m_stats.isEmpty()) return;

    // As many tiles as fit; the rest are left off a narrow window
    const int count = qMax(1, qMin(int(m_stats.size()), (width() - kMargin) / (kMinTileWidth + kMargin)));
    const double tileWidth = double(width() - kMargin * (count + 1)) / count;

    QFont small = QApplication::font();
    small.setPointSizeF(small.pointSizeF() * 0.9);
    QFont big = QApplication::font();
    big.setPointSizeF(big.pointSizeF() * 1.6);
    big.setBold(true);

    for (int i = 0; i < count; ++i) {
        const OverviewStats::Stat &stat = m_stats.at(i);
        const QRectF tile(kMargin + i * (tileWidth + kMargin), kMargin / 2.0, tileWidth, kTileHeight);
        painter.setPen(QPen(GraphTheme::outline(), 1));
        painter.setBrush(Theme::color(Theme::Role::Surface));
        painter.drawRoundedRect(tile.adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);

        const QRectF inner = tile.adjusted(12, 6, -12, -6);
        painter.setFont(small);
        painter.setPen(Theme::color(Theme::Role::Ink3));
        painter.drawText(inner, Qt::AlignLeft | Qt::AlignTop, stat.title);

        // Value, then its unit on the same baseline
        painter.setFont(big);
        const QFontMetricsF bigMetrics(big);
        const double baseline = inner.bottom() - bigMetrics.descent() + 2;
        painter.setPen(Theme::color(Theme::Role::Ink));
        painter.drawText(QPointF(inner.left(), baseline), stat.value);
        double x = inner.left() + bigMetrics.horizontalAdvance(stat.value) + 5;

        painter.setFont(small);
        const QFontMetricsF smallMetrics(small);
        painter.setPen(Theme::color(Theme::Role::Ink2));
        const QString unit = smallMetrics.elidedText(stat.unit, Qt::ElideRight, inner.right() - x);
        painter.drawText(QPointF(x, baseline), unit);

        // The change, at the top right beside the title
        if (!stat.change.isEmpty()) {
            const Theme::Role role = stat.verdict == OverviewStats::Verdict::Good ? Theme::Role::Good
                                   : stat.verdict == OverviewStats::Verdict::Bad ? Theme::Role::Bad
                                   : Theme::Role::Neutral;
            const double titleWidth = smallMetrics.horizontalAdvance(stat.title) + 10;
            const QRectF room = inner.adjusted(titleWidth, 0, 0, 0);
            QString text = stat.change + " " + m_caption;
            if (smallMetrics.horizontalAdvance(text) > room.width()) text = stat.change;
            if (smallMetrics.horizontalAdvance(text) <= room.width()) {
                painter.setPen(Theme::color(role));
                painter.drawText(room, Qt::AlignRight | Qt::AlignTop, text);
            }
        }
    }
}
