/* gSessionGapLayer Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "gSessionGapLayer.h"

#include <QPainter>
#include <algorithm>

#include "gGraph.h"
#include "SleepLib/day.h"
#include "SleepLib/session.h"
#include "uikit/theme.h"
#include "Graphs/graphtheme.h"

namespace SessionGaps {

QVector<Span> gaps(QVector<Span> sessions)
{
    QVector<Span> out;
    if (sessions.size() < 2) return out;
    std::sort(sessions.begin(), sessions.end(), [](const Span &a, const Span &b) {
        return a.start < b.start || (a.start == b.start && a.end < b.end);
    });
    qint64 reach = sessions.first().end;      // end of the merged span so far
    for (int i = 1; i < sessions.size(); ++i) {
        const Span &s = sessions[i];
        if (s.start > reach) {
            out.append({reach, s.start});
        }
        reach = std::max(reach, s.end);
    }
    return out;
}

} // namespace SessionGaps

gSessionGapLayer::gSessionGapLayer()
    : Layer(NoChannel)
{
}

void gSessionGapLayer::SetDay(Day *d)
{
    Layer::SetDay(d);
    m_gaps.clear();
    if (!d) return;
    QVector<SessionGaps::Span> spans;
    for (Session *sess : d->getSessions(MT_CPAP)) {
        if (sess->enabled() && sess->last() > sess->first()) {
            spans.append({sess->first(), sess->last()});
        }
    }
    m_gaps = SessionGaps::gaps(spans);
}

void gSessionGapLayer::paint(QPainter &painter, gGraph &w, const QRegion &region)
{
    if (!m_visible || !m_day || m_gaps.isEmpty() || w.printing()) return;

    const QRect bounds = region.boundingRect();
    // A zoom-blocked graph (Event Flags) always shows the whole day
    const double minx = w.blockZoom() ? w.rmin_x : w.min_x;
    const double maxx = w.blockZoom() ? w.rmax_x : w.max_x;
    if (maxx <= minx || bounds.width() <= 0) return;
    const double xmult = bounds.width() / (maxx - minx);

    painter.save();
    painter.setClipRect(bounds);
    painter.setPen(Qt::NoPen);
    // The theme's line colour; in dark mode that is too close to the graph background for a
    // one-pixel hatch to show, so the muted text colour, softened, is used there instead
    // Kept faint: the gap should show without drawing the eye
    QColor hatch = Theme::color(Theme::Role::Line);
    hatch.setAlpha(150);
    if (GraphTheme::dark()) {
        hatch = Theme::color(Theme::Role::Ink3);
        hatch.setAlpha(80);
    }
    painter.setBrush(QBrush(hatch, Qt::BDiagPattern));
    for (const SessionGaps::Span &gap : m_gaps) {
        if (gap.end <= minx || gap.start >= maxx) continue;
        const double x1 = bounds.left() + (std::max<double>(gap.start, minx) - minx) * xmult;
        const double x2 = bounds.left() + (std::min<double>(gap.end, maxx) - minx) * xmult;
        if (x2 - x1 < 1) continue;
        painter.drawRect(QRectF(x1, bounds.top(), x2 - x1, bounds.height()));
    }
    painter.restore();
}
