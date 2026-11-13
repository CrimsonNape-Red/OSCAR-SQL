/* Overview bars
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "overviewbars.h"
#include "glcommon.h"
#include "graphtheme.h"

#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace OverviewBars {

namespace {

constexpr double kGapShare = 0.15;      //!< Share of a day's slot left as a gap between days
constexpr double kMinGapSlot = 3.0;     //!< Narrowest slot that still gets a (1 px) gap
constexpr double kMinShaded = 8.0;      //!< Narrowest bar shaded left to right (as in OSCAR 2.1.0)
constexpr double kCornerRadius = 3.0;   //!< Radius of the two top corners of a shaded bar
constexpr float kDarkLightness = 0.88f; //!< Share of a dark colour's lightness kept in dark mode (HSL)
constexpr float kDarkCompress = 0.40f;  //!< How much more a light colour loses: white ends at 0.48
constexpr float kDarkSaturation = 0.8f; //!< Share of a bar colour's saturation kept in dark mode
constexpr int kDarkShade = 122;         //!< Far end of a bar's shading in dark mode (QColor::lighter)

double toLinear(double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); }

double luminance(const QColor &c)
{
    return 0.2126 * toLinear(c.redF()) + 0.7152 * toLinear(c.greenF()) + 0.0722 * toLinear(c.blueF());
}

} // namespace

BarGeometry barGeometry(double slotWidth)
{
    const double gap = slotWidth >= kMinGapSlot ? qMax(1.0, double(qRound(slotWidth * kGapShare))) : 0.0;
    const double width = slotWidth - gap;
    return { gap / 2.0, width, width >= kMinShaded };
}

BarGeometry classicBarGeometry(double slotWidth)
{
    return { 0.0, slotWidth, slotWidth > kMinShaded };
}

void paintClassicSegment(QPainter &p, const QRectF &r, const QColor &c, double barWidth)
{
    if (r.isEmpty()) return;
    if (barWidth <= 3.0) {
        p.fillRect(r, c);
    } else if (barWidth > kMinShaded) {
        QLinearGradient g(r.left(), 0, r.left() + barWidth, 0);
        g.setColorAt(0.0, c);
        g.setColorAt(1.0, GraphTheme::barShade(c, 2.0f));
        p.fillRect(r, QBrush(g));
    } else {
        p.fillRect(r, GraphTheme::barShade(c, 1.25f));
    }
}

QLinearGradient barGradient(const QColor &c, double left, double width)
{
    QLinearGradient g(left, 0, left + width, 0);
    g.setColorAt(0.0, c);
    g.setColorAt(1.0, brighten(c, 2.0f));
    return g;
}

QColor darkBarColour(const QColor &c)
{
    float h, s, l, a;
    c.getHslF(&h, &s, &l, &a);
    // Lightness is compressed, not capped, so that shades of one colour stay distinct
    return QColor::fromHslF(h, s * kDarkSaturation, l * (kDarkLightness - kDarkCompress * l), a);
}

QLinearGradient barGradient(const QColor &c, double left, double width, bool dark)
{
    if (!dark) return barGradient(c, left, width);
    const QColor base = darkBarColour(c);
    QLinearGradient g(left, 0, left + width, 0);
    g.setColorAt(0.0, base);
    g.setColorAt(1.0, base.lighter(kDarkShade));
    return g;
}

void paintSegment(QPainter &p, const QRectF &r, const QColor &c, bool top, bool shaded)
{
    if (r.isEmpty()) return;
    const bool dark = GraphTheme::dark();
    if (!shaded) {
        if (dark) p.fillRect(r, darkBarColour(c));
        else p.fillRect(r, r.width() > 3.0 ? brighten(c, 1.25f) : c);
        return;
    }
    const QBrush brush(barGradient(c, r.left(), r.width(), dark));
    const double radius = qMin(kCornerRadius, qMin(r.width() / 4.0, r.height() / 2.0));
    if (!top || radius < 1.0) {
        p.fillRect(r, brush);
        return;
    }
    // Flat top with slightly rounded corners, straight sides and bottom
    QPainterPath path;
    path.moveTo(r.left(), r.bottom());
    path.lineTo(r.left(), r.top() + radius);
    path.arcTo(QRectF(r.left(), r.top(), 2 * radius, 2 * radius), 180, -90);
    path.lineTo(r.right() - radius, r.top());
    path.arcTo(QRectF(r.right() - 2 * radius, r.top(), 2 * radius, 2 * radius), 90, -90);
    path.lineTo(r.right(), r.bottom());
    path.closeSubpath();
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.fillPath(path, brush);
    p.restore();
}

QColor contrastingText(const QColor &background)
{
    const double l = luminance(background);
    const double onWhite = 1.05 / (l + 0.05);
    const double onBlack = (l + 0.05) / 0.05;
    return onWhite >= onBlack ? QColor(Qt::white) : QColor(Qt::black);
}

} // namespace OverviewBars
