/* Overview statistics row
 *
 * The row of figures at the top of the redesigned Overview page: AHI, usage, compliance,
 * leak and pressure for the dates on screen, each compared with the same number of days
 * just before. The numbers come from RangeFigures, which the graphs' dotted lines also
 * use, so a box and a line that share a name show the same value.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef OVERVIEWSTATS_H
#define OVERVIEWSTATS_H

#include <QDate>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include "uikit/comparison.h"

/*! \namespace OverviewStats
    \brief What the statistics row shows, apart from how it is drawn */
namespace OverviewStats {

//! \brief A run of dates, both ends included
struct Range {
    QDate start;
    QDate end;
};

//! \brief The same number of days as \a shown, ending the day before it starts
Range previousRange(const Range &shown);

//! \brief Whether a figure got better or worse
enum class Verdict { Good, Bad, Neutral };

/*! \brief Compares a figure with the one before
    \param decimals Figures equal when rounded to this many places count as unchanged
    \return Neutral when unchanged or when \a better is UiKit::Better::Neutral */
Verdict verdict(double now, double before, UiKit::Better better, int decimals = 2);

/*! \struct Stat
    \brief One figure as shown in the row */
struct Stat {
    QString title;      //!< e.g. "AHI"
    QString value;      //!< The figure, or a dash when the range has no data
    QString unit;       //!< Shown small after the value; may be empty
    QString change;     //!< e.g. an arrow and "0.14"; empty when there is nothing to compare with
    Verdict verdict = Verdict::Neutral;     //!< Colour of the change text
};

//! \brief The five figures of the current profile for \a shown
QVector<Stat> compute(const Range &shown);

} // namespace OverviewStats

/*! \class OverviewStatsRow
    \brief Draws the figures as a row of tiles; display only */
class OverviewStatsRow : public QWidget
{
    Q_OBJECT
public:
    explicit OverviewStatsRow(QWidget *parent = nullptr);

    /*! \brief Sets the dates the figures are for. They are recalculated a moment later, so
        that dragging or zooming the graphs stays smooth. */
    void setRange(const QDate &start, const QDate &end);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void recompute();

    OverviewStats::Range m_range;
    QVector<OverviewStats::Stat> m_stats;
    QString m_caption;      //!< "vs previous N days", shown after each change
    QTimer m_timer;
};

#endif // OVERVIEWSTATS_H
