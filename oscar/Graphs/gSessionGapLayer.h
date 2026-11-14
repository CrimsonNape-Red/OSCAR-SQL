/* gSessionGapLayer Header
 *
 * Marks the time between a night's CPAP sessions on the Modern Daily graphs with diagonal
 * hatching, so a stretch with the mask off is not mistaken for a stretch of flat data. Hatching
 * rather than a flat fill, so it is not confused with a Large Leak span (a flat pale grey fill).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef GSESSIONGAPLAYER_H
#define GSESSIONGAPLAYER_H

#include <QVector>

#include "Graphs/layer.h"

/*! \namespace SessionGaps
    \brief The stretches of a night that lie between its sessions */
namespace SessionGaps {

//! \brief A stretch of time, in milliseconds since the epoch; start before end
struct Span {
    qint64 start;
    qint64 end;
};

/*! \brief The spaces between \a sessions

    Sorts the spans, merges those that touch or overlap, and returns the spaces between the
    merged spans in time order. Nothing before the first or after the last, so one session (or
    none) gives no gaps. */
QVector<Span> gaps(QVector<Span> sessions);

} // namespace SessionGaps

/*! \class gSessionGapLayer
    \brief Hatches the gaps between the day's enabled CPAP sessions, under the graph's data

    Added first to a graph's layers so it draws beneath everything else. Follows zoom and pan
    (w.min_x / w.max_x). Draws nothing on printed graphs. */
class gSessionGapLayer : public Layer
{
public:
    gSessionGapLayer();
    ~gSessionGapLayer() override {}

    //! \brief Collects the day's enabled CPAP sessions and the gaps between them
    void SetDay(Day *d) override;

    //! \brief Hatches each gap within the visible range
    void paint(QPainter &painter, gGraph &w, const QRegion &region) override;

    Layer *Clone() override {
        gSessionGapLayer *l = new gSessionGapLayer();
        Layer::CloneInto(l);
        l->m_gaps = m_gaps;
        return l;
    }

protected:
    QVector<SessionGaps::Span> m_gaps;   //!< Gaps between the sessions, in display time
};

#endif // GSESSIONGAPLAYER_H
