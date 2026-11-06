/* Line icons
 *
 * Simple outline icons for the redesigned pages, drawn in code so they stay sharp at any
 * display scale and take their colour from the theme. Shapes follow the Welcome page
 * mockup's 24x24 icons.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef UIKIT_LINEICONS_H
#define UIKIT_LINEICONS_H

#include <QColor>
#include <QRectF>

class QPainter;

namespace UiKit {

//! \brief The available icons
enum class LineIcon { None, Import, Oximetry, Daily, Overview, Statistics, Insights,
                      ArrowUpRight, ArrowDownRight, Settings,
                      FolderOpen, Edit, UserAdd, Delete, Clear };

//! \brief Stroke width of the tile icons, on the 24x24 design grid
constexpr double LineIconStroke = 1.8;

/*! \brief Draws \a icon scaled into \a rect with a stroke in \a colour
    \param stroke Stroke width on the 24x24 design grid; small icons need a heavier stroke */
void paintLineIcon(QPainter &painter, LineIcon icon, const QRectF &rect, const QColor &colour,
                   double stroke = LineIconStroke);

} // namespace UiKit

#endif // UIKIT_LINEICONS_H
