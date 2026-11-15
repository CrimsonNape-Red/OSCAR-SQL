/* Overview shared date axis
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "overviewdateaxis.h"

#include <QDateTime>
#include <QPainter>
#include <QTimeZone>
#include <cmath>

#include "Graphs/daylabels.h"
#include "Graphs/gGraph.h"
#include "Graphs/gGraphView.h"
#include "Graphs/gXAxis.h"
#include "uikit/theme.h"

namespace {
constexpr int kTopGap = 2;                      //!< Space above the text
constexpr int kUnderline = 2;                   //!< Thickness of a pinned day's underline
const QColor kPinColour(0x6D, 0x4B, 0xC9);      //!< The Details button's colour
const QString kWidestLabel = QStringLiteral("Mjj 00");
}

OverviewDateAxis::OverviewDateAxis(gGraphView *view, QWidget *parent)
    : QWidget(parent), m_view(view)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(view, &gGraphView::painted, this, &OverviewDateAxis::refresh);
}

QSize OverviewDateAxis::sizeHint() const
{
    return QSize(200, QFontMetrics(*defaultfont).height() + kTopGap + kUnderline + 2);
}

QSize OverviewDateAxis::minimumSizeHint() const
{
    return QSize(0, sizeHint().height());
}

OverviewDateAxis::State OverviewDateAxis::read() const
{
    State state;
    if (!m_view) return state;
    for (int i = 0; i < m_view->size(); ++i) {
        gGraph *graph = (*m_view)[i];
        if (!graph || !graph->visible() || graph->isEmpty()) continue;
        const QRect plot = graph->plotRect();
        if (plot.width() <= 0) continue;
        if (state.width == 0) {
            state.left = mapFromGlobal(m_view->mapToGlobal(QPoint(plot.left(), 0))).x();
            state.width = plot.width();
        }
        // A graph scrolled out of view is not repainted, so it can still hold an old hover date
        if (graph->hoverDate().isValid() && graph->rect().contains(m_view->currentMousePos())) state.hover = graph->hoverDate();
        state.pinned += graph->pinnedDates();
    }
    m_view->GetXBounds(state.minx, state.maxx);
    if (state.maxx <= state.minx) state.width = 0;
    return state;
}

void OverviewDateAxis::refresh()
{
    const State state = read();
    if (state == m_state) return;
    m_state = state;
    update();
}

void OverviewDateAxis::paintEvent(QPaintEvent *)
{
    if (m_state.width <= 0) return;

    // The same days and day width the graphs use (see gSummaryChart::paint)
    const QDate first = QDateTime::fromMSecsSinceEpoch(m_state.minx, QTimeZone::systemTimeZone()).date();
    const int days = int(std::ceil(double(m_state.maxx - m_state.minx) / 86400000.0));
    if (days <= 0) return;
    const double barWidth = double(m_state.width) / days;

    QFont plain = *defaultfont;
    QFont bold = plain;
    bold.setBold(true);
    const QFontMetricsF metrics(plain);
    const double textWidth = QFontMetricsF(bold).horizontalAdvance(kWidestLabel);
    const auto labels = DayLabels::layout(first, days, barWidth, textWidth, 8, m_state.hover, m_state.pinned);

    QPainter painter(this);
    const double baseline = kTopGap + metrics.ascent();
    for (const DayLabels::Label &label : labels) {
        const QString text = gXAxisDay::labelText(label.date);
        painter.setFont(label.bold ? bold : plain);
        const double w = painter.fontMetrics().horizontalAdvance(text);
        // Centred under its day, kept inside the widget
        double x = m_state.left + label.x + (textWidth - w) / 2;
        x = qBound(0.0, x, qMax(0.0, width() - w));
        painter.setPen(Theme::color(label.bold ? Theme::Role::Ink : Theme::Role::Ink2));
        painter.drawText(QPointF(x, baseline), text);
        if (label.underlined) {
            painter.fillRect(QRectF(x, baseline + 2, w, kUnderline), kPinColour);
        }
    }
}
