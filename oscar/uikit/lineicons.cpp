/* Line icons
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "lineicons.h"

#include <QPainter>
#include <QPainterPath>

namespace UiKit {

namespace {

constexpr double kGrid = 24;        //!< Icons are designed on a 24x24 grid

void line(QPainterPath &path, double x1, double y1, double x2, double y2)
{
    path.moveTo(x1, y1);
    path.lineTo(x2, y2);
}

//! \brief SD card with a downward arrow
QPainterPath importIcon()
{
    QPainterPath p;
    p.moveTo(7, 3);
    p.lineTo(15, 3);
    p.lineTo(19, 7);
    p.lineTo(19, 21);
    p.lineTo(7, 21);
    p.arcTo(QRectF(5, 17, 4, 4), 270, -90);
    p.lineTo(5, 5);
    p.arcTo(QRectF(5, 3, 4, 4), 180, -90);
    p.closeSubpath();
    line(p, 9, 3, 9, 7);
    line(p, 12, 3, 12, 7);
    line(p, 15, 3, 15, 7);
    line(p, 12, 11, 12, 17);
    p.moveTo(9, 14);
    p.lineTo(12, 17);
    p.lineTo(15, 14);
    return p;
}

//! \brief Finger-clip oximeter with a pulse trace
QPainterPath oximetryIcon()
{
    QPainterPath p;
    p.addRoundedRect(QRectF(4, 7, 12, 10), 3, 3);
    p.moveTo(16, 10);
    p.lineTo(18, 10);
    p.arcTo(QRectF(16, 10, 4, 4), 90, -180);
    p.lineTo(16, 14);
    p.moveTo(7, 12);
    p.lineTo(8.5, 12);
    p.lineTo(9.5, 10);
    p.lineTo(11, 14);
    p.lineTo(12, 12);
    p.lineTo(13, 12);
    return p;
}

//! \brief Calendar page with a small trend line
QPainterPath dailyIcon()
{
    QPainterPath p;
    p.addRoundedRect(QRectF(3.5, 5, 17, 15), 2, 2);
    line(p, 3.5, 9.5, 20.5, 9.5);
    line(p, 8, 3, 8, 7);
    line(p, 16, 3, 16, 7);
    p.moveTo(7, 15);
    p.lineTo(9.5, 12.5);
    p.lineTo(11.5, 14.5);
    p.lineTo(17, 13);
    return p;
}

//! \brief Bar chart
QPainterPath overviewIcon()
{
    QPainterPath p;
    line(p, 4, 20, 4, 10);
    line(p, 9, 20, 9, 5);
    line(p, 14, 20, 14, 12);
    line(p, 19, 20, 19, 8);
    line(p, 3, 20, 21, 20);
    return p;
}

//! \brief Table
QPainterPath statisticsIcon()
{
    QPainterPath p;
    p.addRoundedRect(QRectF(3.5, 4, 17, 16), 2, 2);
    line(p, 3.5, 9, 20.5, 9);
    line(p, 3.5, 14.5, 20.5, 14.5);
    line(p, 10, 4, 10, 20);
    return p;
}

//! \brief Light bulb
QPainterPath insightsIcon()
{
    QPainterPath p;
    line(p, 9, 18, 15, 18);
    line(p, 10, 21, 14, 21);
    // Bulb: an arc of the circle centred on (12, 9) with radius 6, narrowing into the base
    const QRectF circle(6, 3, 12, 12);
    p.moveTo(12, 3);
    p.arcTo(circle, 90, 143.13);
    p.cubicTo(9.0, 14.3, 9.4, 15.0, 9.4, 15.8);
    p.lineTo(9.4, 16);
    p.lineTo(14.6, 16);
    p.lineTo(14.6, 15.8);
    p.cubicTo(14.6, 15.0, 15.0, 14.3, 15.6, 13.8);
    p.arcTo(circle, 306.87, 143.13);
    return p;
}

//! \brief Arrow pointing up and to the right (night above its average)
QPainterPath arrowUpRightIcon()
{
    QPainterPath p;
    line(p, 6, 18, 18, 6);
    p.moveTo(9, 6);
    p.lineTo(18, 6);
    p.lineTo(18, 15);
    return p;
}

//! \brief Arrow pointing down and to the right (night below its average)
QPainterPath arrowDownRightIcon()
{
    QPainterPath p;
    line(p, 6, 6, 18, 18);
    p.moveTo(18, 9);
    p.lineTo(18, 18);
    p.lineTo(9, 18);
    return p;
}

QPainterPath iconPath(LineIcon icon)
{
    switch (icon) {
    case LineIcon::ArrowUpRight: return arrowUpRightIcon();
    case LineIcon::ArrowDownRight: return arrowDownRightIcon();
    case LineIcon::Import: return importIcon();
    case LineIcon::Oximetry: return oximetryIcon();
    case LineIcon::Daily: return dailyIcon();
    case LineIcon::Overview: return overviewIcon();
    case LineIcon::Statistics: return statisticsIcon();
    case LineIcon::Insights: return insightsIcon();
    case LineIcon::None: break;
    }
    return QPainterPath();
}

} // namespace

void paintLineIcon(QPainter &painter, LineIcon icon, const QRectF &rect, const QColor &colour,
                   double stroke)
{
    if (icon == LineIcon::None) return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(rect.topLeft());
    painter.scale(rect.width() / kGrid, rect.height() / kGrid);
    QPen pen(colour, stroke);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(iconPath(icon));
    painter.restore();
}

} // namespace UiKit
