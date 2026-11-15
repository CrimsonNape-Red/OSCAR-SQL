/* Daily shared time axis
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "dailytimeaxis.h"

#include <QPainter>

#include "Graphs/gGraph.h"
#include "Graphs/gGraphView.h"
#include "Graphs/gXAxis.h"
#include "Graphs/graphtheme.h"

DailyTimeAxis::DailyTimeAxis(gGraphView *view, QWidget *parent)
    : QWidget(parent), m_view(view), m_axis(new gXAxis())
{
    m_axis->setCacheEnabled(false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(view, &gGraphView::painted, this, &DailyTimeAxis::refresh);
}

DailyTimeAxis::~DailyTimeAxis() = default;

QSize DailyTimeAxis::sizeHint() const
{
    return QSize(200, m_axis->minimumHeight());
}

QSize DailyTimeAxis::minimumSizeHint() const
{
    return QSize(0, sizeHint().height());
}

DailyTimeAxis::State DailyTimeAxis::read() const
{
    State state;
    state.dark = GraphTheme::dark();
    if (!m_view) return state;
    for (int i = 0; i < m_view->size(); ++i) {
        gGraph *graph = (*m_view)[i];
        if (!graph || !graph->visible() || graph->isEmpty()) continue;
        const QRect plot = graph->plotRect();
        if (plot.width() <= 0) continue;
        bool shared = false;
        for (Layer *layer : graph->layers()) {
            if (graph->leavesAxisToPage(layer)) { shared = true; break; }
        }
        if (!shared) continue;
        state.graph = graph;
        state.day = graph->day();
        state.minx = graph->min_x;
        state.maxx = graph->max_x;
        state.left = mapFromGlobal(m_view->mapToGlobal(QPoint(plot.left(), 0))).x();
        state.width = plot.width();
        break;
    }
    if (state.maxx <= state.minx) state.graph = nullptr;
    return state;
}

void DailyTimeAxis::refresh()
{
    const State state = read();
    if (state == m_state) return;
    m_state = state;
    update();
}

void DailyTimeAxis::paintEvent(QPaintEvent *)
{
    // Read again rather than trust the pointer kept since the last refresh: the view can drop
    // its graphs in between
    const State state = read();
    if (!state.graph || !state.day) return;
    m_state = state;

    QPainter painter(this);
    painter.setFont(*defaultfont);
    m_axis->SetDay(state.day);
    m_axis->paint(painter, *state.graph, QRegion(state.left, 0, state.width, height()));
}
