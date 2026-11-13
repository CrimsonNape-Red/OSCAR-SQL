/* Overview shared date axis
 *
 * The one date scale of the redesigned Overview page. It sits under the graph area, outside
 * the scrolling list of graphs, so it stays in view however far the graphs are scrolled. The
 * graphs then draw no date scale of their own (popped-out and printed graphs still do).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef OVERVIEWDATEAXIS_H
#define OVERVIEWDATEAXIS_H

#include <QDate>
#include <QPointer>
#include <QSet>
#include <QWidget>

class gGraphView;

/*! \class OverviewDateAxis
    \brief Draws the dates for the graphs of a gGraphView, lined up with their plots

    The day under the mouse (in any graph) is shown in bold; a day with a details popup open
    is bold and underlined. The other labels keep their places. */
class OverviewDateAxis : public QWidget
{
    Q_OBJECT
public:
    //! \param view The graphs this axis belongs to; the axis follows its gGraphView::painted() signal
    OverviewDateAxis(gGraphView *view, QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

public slots:
    //! \brief Reads the view's dates, plot position and marked days; repaints if any changed
    void refresh();

protected:
    //! \brief Draws the day labels for the state last read by refresh()
    void paintEvent(QPaintEvent *event) override;

private:
    //! \brief What the axis shows
    struct State {
        qint64 minx = 0, maxx = 0;      //!< Time range of the graphs
        int left = 0, width = 0;        //!< The plots' left edge (in this widget) and width; 0 = nothing to show
        QDate hover;
        QSet<QDate> pinned;
        bool operator==(const State &o) const {
            return minx == o.minx && maxx == o.maxx && left == o.left && width == o.width
                   && hover == o.hover && pinned == o.pinned;
        }
    };
    State read() const;

    QPointer<gGraphView> m_view;
    State m_state;
};

#endif // OVERVIEWDATEAXIS_H
