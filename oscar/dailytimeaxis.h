/* Daily shared time axis
 *
 * The one time scale of the Modern Daily page. It sits under the graph area, outside the
 * scrolling list of graphs, so it stays in view however far the graphs are scrolled. The
 * graphs that follow zoom then draw no time scale of their own (Event Flags, Time at Pressure,
 * popped-out and printed graphs still do).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DAILYTIMEAXIS_H
#define DAILYTIMEAXIS_H

#include <QPointer>
#include <QWidget>

#include <memory>

class gGraph;
class gGraphView;
class gXAxis;
class Day;

/*! \class DailyTimeAxis
    \brief Draws the time of day for the graphs of a gGraphView, lined up with their plots

    It draws with a gXAxis of its own (its cache off), for the first visible graph that leaves
    its time scale to the page (gGraph::leavesAxisToPage()), across that graph's plot. So the
    ticks and labels are the ones that graph would have drawn, and follow zoom and pan. Nothing
    is drawn while no such graph is shown. */
class DailyTimeAxis : public QWidget
{
    Q_OBJECT
public:
    //! \param view The graphs this axis belongs to; the axis follows its gGraphView::painted() signal
    DailyTimeAxis(gGraphView *view, QWidget *parent = nullptr);
    ~DailyTimeAxis() override;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    //! \brief The plot's left edge and width in this widget's coordinates, as last drawn (for checks)
    QPair<int, int> plotSpan() const { return {m_state.left, m_state.width}; }

public slots:
    //! \brief Reads the view's range and plot position; repaints if anything changed
    void refresh();

protected:
    //! \brief Draws the time scale for the state last read by refresh()
    void paintEvent(QPaintEvent *event) override;

private:
    //! \brief What the axis shows
    struct State {
        gGraph *graph = nullptr;        //!< The graph whose scale is drawn; null = nothing to show
        Day *day = nullptr;             //!< That graph's day (the labels count from its start)
        qint64 minx = 0, maxx = 0;      //!< Time range shown
        int left = 0, width = 0;        //!< The plot's left edge (in this widget) and width
        bool dark = false;              //!< Drawn in dark-mode colours
        bool operator==(const State &o) const {
            return graph == o.graph && day == o.day && minx == o.minx && maxx == o.maxx
                   && left == o.left && width == o.width && dark == o.dark;
        }
    };
    State read() const;

    QPointer<gGraphView> m_view;
    std::unique_ptr<gXAxis> m_axis;
    State m_state;
};

#endif // DAILYTIMEAXIS_H
