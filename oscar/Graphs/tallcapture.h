/* Tall capture
 *
 * The arithmetic of the full-height screenshot of the Daily page: how tall the graph area has
 * to be, the cap on the page's height, and the lengthening of the window grab that the tall
 * page is painted onto. These are plain functions, without widgets, so they can be unit tested.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef TALLCAPTURE_H
#define TALLCAPTURE_H

#include <QImage>
#include <QVector>

/*! \namespace TallCapture
    \brief Sizes and image handling for the full-height Daily screenshot
           (see Daily::grabTall() and MainWindow::grabClientAreaTall()) */
namespace TallCapture {

//! \brief The tallest page drawn, in device pixels; a QPixmap side may not exceed 32767
constexpr int MaxDevicePixels = 30000;

/*! \brief The height the graph area needs so the graph crossing the bottom of the left
           sidebar is drawn whole

    The graphs are stacked from the top as gGraphView::renderGraphs() stacks them. The first
    graph whose bottom edge reaches \a needed is the last one drawn, and the area ends at its
    bottom edge. A graph that would start at or below \a needed is not drawn. When the graphs
    end above \a needed, the area is \a needed tall and the rest of it is empty.

    \param heights The drawn height of each graph, in drawing order (pinned graphs first)
    \param gap The space after each graph
    \param needed The height at which the left sidebar is shown to its bottom
    \return The height of the graph area, never less than \a needed */
int graphAreaHeight(const QVector<float> &heights, float gap, int needed);

/*! \brief \a wanted limited to MaxDevicePixels at \a devicePixelRatio, and never less than
           \a onScreen (the height the page has on screen) */
int cappedPageHeight(int wanted, int onScreen, qreal devicePixelRatio);

/*! \brief \a image made \a rows taller by repeating the pixel row above \a y

    The rows above \a y stay where they are, row \a y - 1 is repeated \a rows times, and the
    rows from \a y on follow. Used to lengthen a grab of the window at the bottom edge of the
    Daily page, so the window's side borders and background run on beside the taller page.

    \param image The image to lengthen; its format and device pixel ratio are kept
    \param y The row to insert at, in device pixels; limited to 1 .. the image's height
    \param rows How many rows to insert, in device pixels; none are inserted when below 1 */
QImage insertRows(const QImage &image, int y, int rows);

} // namespace TallCapture

#endif // TALLCAPTURE_H
