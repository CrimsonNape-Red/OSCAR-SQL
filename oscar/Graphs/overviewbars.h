/* Overview bars
 *
 * How the Overview page's daily bars are placed and shaded: each day has a slot, the bar
 * fills most of it and the rest is a gap between days; wide bars are shaded from
 * left to right, narrow ones are plain fills.
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
#include <QtGlobal>

class QPainter;

/*! \namespace OverviewBars
    \brief Placement and shading of the Overview page's daily bars */
namespace OverviewBars {

/*! \struct BarGeometry
    \brief Where a bar sits inside its day's slot */
struct BarGeometry {
    double offset;      //!< Space between the start of the slot and the bar
    double width;       //!< Width of the bar
    bool shaded;        //!< Wide enough to shade left to right; otherwise a plain fill
};

/*! \brief Places a bar in a day's slot \a slotWidth pixels wide

    The gap between days is about 15% of the slot and shrinks with it, staying at least one
    pixel while the slot is 3 pixels or more; below that the bars touch. */
BarGeometry barGeometry(double slotWidth);

/*! \brief The shading for \a c across a bar from \a left, \a width wide

    As in OSCAR 2.1.0: the channel colour at the left edge, brightening to twice as bright
    (see brighten()) at the right edge. */
QLinearGradient barGradient(const QColor &c, double left, double width);

/*! \brief The same shading for dark mode when \a dark is true: from darkBarColour(\a c) to a
    slightly lighter shade of it, as bars brightening towards white glare on a dark background */
QLinearGradient barGradient(const QColor &c, double left, double width, bool dark);

/*! \brief A bar colour for dark mode: \a c with the same hue, less saturated, and no lighter
    than a medium shade. Pale colours, made for a white background, are brought down the most.
    Greys are lifted clear of the background, and orange keeps enough light to stay amber. */
QColor darkBarColour(const QColor &c);

/*! \brief The colour of the thin line round a bar: black in light mode, and in dark mode a very
    light grey, which shows on the dark background without the glare of white. */
QColor outlineColour(bool dark);

/*! \brief Draws one part of a bar in \a r

    In light mode a bar at least 4 pixels wide has a one-pixel black line up its sides and
    across its top (as in OSCAR 2.1.0); in dark mode, where it would not show, there is none.

    \param top The top part of the bar, whose top corners are rounded when \a shaded is true
    \param closed The outline also runs along the bottom: for a piece that stands on nothing
           (a session in Session Times). Otherwise the bottom is left to the part below, or the axis.
    \param shaded Shaded left to right; false for a plain fill, which is 25% brighter than
           \a c when the bar is more than 3 pixels wide (as in OSCAR 2.1.0) */
void paintSegment(QPainter &p, const QRectF &r, const QColor &c, bool top, bool shaded, bool closed = false);

/*! \brief Places a bar the way OSCAR 2.1 did: it fills the whole slot, so bars touch

    \a shaded is true once the slot is wider than 8 pixels. */
BarGeometry classicBarGeometry(double slotWidth);

/*! \brief Draws one part of a bar as OSCAR 2.1 did, with a square top

    Bars wider than 8 pixels are shaded from \a c at the left to GraphTheme::barShade(c, 2.0) at
    the right; bars of more than 3 pixels are a plain fill of barShade(c, 1.25); narrower bars are
    filled with \a c.
    \param barWidth The width of the whole bar (the day's slot), which picks the fill */
void paintClassicSegment(QPainter &p, const QRectF &r, const QColor &c, double barWidth);

//! \brief White or black, whichever reads better on \a background
QColor contrastingText(const QColor &background);

/*! \brief The day slot at \a x in a plot showing \a days days

    The graph under the mouse works out its day with this, and the other graphs get to the same
    slot through daySlotAtTime(), so that they all mark the same day.
    \param plotLeft Left edge of the plot
    \param plotWidth Width of the plot
    \param days Number of day slots across the plot
    \returns The slot, counted from 0 at the left edge; -1 when \a x is outside the plot */
int daySlot(double plotLeft, double plotWidth, int days, double x);

/*! \brief The day slot holding \a time, the view's line-cursor time (gGraphView::currentTime()),
    in a plot that shows \a minx to \a maxx

    The cursor time is what a graph made of the mouse position (gGraph::mouseMoveEvent()). It is
    turned back into that position, to the pixel, so the slot is the one daySlot() gives the graph
    under the mouse. Unlike the mouse position it is kept when the mouse stops or leaves.
    \returns The slot as daySlot(); -1 when \a time is not between \a minx and \a maxx */
int daySlotAtTime(double plotLeft, double plotWidth, int days, qint64 minx, qint64 maxx, double time);

/*! \brief Where the vertical line that marks day \a slot is drawn: the middle of the slot, moved
    onto a pixel centre so that a one-pixel line stays sharp. Parameters as for daySlot(). */
double daySlotLineX(double plotLeft, double plotWidth, int days, int slot);

/*! \brief Colour of the line that marks the day under the mouse in every graph when the line
    cursor is on: the green of the Daily page's line cursor */
QColor dayCursorColour();

} // namespace OverviewBars

#endif // OVERVIEWBARS_H
