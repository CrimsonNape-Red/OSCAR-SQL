/* Overview bars
 *
 * How the Overview page's daily bars are placed and shaded: each day has a slot, the bar
 * fills most of it and the rest is a gap between days; wide bars are drawn as shaded
 * cylinders, narrow ones as plain fills.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef OVERVIEWBARS_H
#define OVERVIEWBARS_H

#include <QColor>
#include <QLinearGradient>
#include <QRectF>

class QPainter;

/*! \namespace OverviewBars
    \brief Placement and shading of the Overview page's daily bars */
namespace OverviewBars {

/*! \struct BarGeometry
    \brief Where a bar sits inside its day's slot */
struct BarGeometry {
    double offset;      //!< Space between the start of the slot and the bar
    double width;       //!< Width of the bar
    bool cylinder;      //!< Wide enough to draw as a shaded cylinder; otherwise a plain fill
};

/*! \brief Places a bar in a day's slot \a slotWidth pixels wide

    The gap between days is about a third of the slot and shrinks with it, staying at least one
    pixel while the slot is 3 pixels or more; below that the bars touch. */
BarGeometry barGeometry(double slotWidth);

/*! \brief The cylinder shading for \a c across a bar from \a left, \a width wide

    Darker at both edges, with a soft highlight just left of the middle. */
QLinearGradient cylinderGradient(const QColor &c, double left, double width);

/*! \brief Draws one part of a bar in \a r

    \param top The top part of the bar, which gets a rounded cap when \a cylinder is true
    \param cylinder Shaded cylinder; false for a plain fill */
void paintSegment(QPainter &p, const QRectF &r, const QColor &c, bool top, bool cylinder);

//! \brief White or black, whichever reads better on \a background
QColor contrastingText(const QColor &background);

} // namespace OverviewBars

#endif // OVERVIEWBARS_H
