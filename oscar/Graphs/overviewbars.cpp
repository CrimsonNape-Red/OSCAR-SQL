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
constexpr float kDarkShade = 0.15f;     //!< Lightness (HSL) the far end of a bar's shading gains in dark mode
constexpr int kDarkOutline = 0xD0;      //!< Grey of a bar's outline in dark mode: light, but short of a glaring white
constexpr float kDarkGreyLift = 0.28f;  //!< Lightness added to a grey in dark mode; less the more colour there is
constexpr float kDarkWarmLift = 0.13f;  //!< Lightness added to orange in dark mode, where it would turn brown
constexpr float kWarmHue = 45.0f;       //!< Hue (degrees) that gets all of kDarkWarmLift
constexpr float kWarmSpread = 30.0f;    //!< Degrees either side of kWarmHue over which the lift fades out
constexpr double kMinOutlined = 4.0;    //!< Narrowest bar that gets an outline

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
    // Lightness is compressed, not capped, so that shades of one colour stay distinct. A grey
    // has no colour to set it off from the dark background, so it is lifted clear of it.
    const float grey = (1.0f - s) * (1.0f - s) * (1.0f - s);
    // Orange and yellow turn brown when darkened as far as the other colours, so they keep more light
    const float warm = h < 0.0f ? 0.0f : qMax(0.0f, 1.0f - qAbs(h * 360.0f - kWarmHue) / kWarmSpread) * s;
    const float lightness = qMin(1.0f, l * (kDarkLightness - kDarkCompress * l) + kDarkGreyLift * grey
                                       + kDarkWarmLift * warm);
    return QColor::fromHslF(h, s * kDarkSaturation, lightness, a);
}

QLinearGradient barGradient(const QColor &c, double left, double width, bool dark)
{
    if (!dark) return barGradient(c, left, width);
    const QColor base = darkBarColour(c);
    QLinearGradient g(left, 0, left + width, 0);
    g.setColorAt(0.0, base);
    float h, s, l, a;
    base.getHslF(&h, &s, &l, &a);
    g.setColorAt(1.0, QColor::fromHslF(h, s, qMin(1.0f, l + kDarkShade), a));
    return g;
}

QColor outlineColour(bool dark)
{
    return dark ? QColor(kDarkOutline, kDarkOutline, kDarkOutline) : QColor(Qt::black);
}

void paintSegment(QPainter &p, const QRectF &r, const QColor &c, bool top, bool shaded, bool closed)
{
    if (r.isEmpty()) return;
    const bool dark = GraphTheme::dark();
    // A thin line up the sides and across the top: black in light mode, as in OSCAR 2.1.0, and
    // a light grey in dark mode, where black would not show
    const bool outlined = r.width() >= kMinOutlined;
    QBrush brush;
    if (shaded) brush = QBrush(barGradient(c, r.left(), r.width(), dark));
    else if (dark) brush = QBrush(darkBarColour(c));
    else brush = QBrush(r.width() > 3.0 ? brighten(c, 1.25f) : c);

    double radius = (shaded && top) ? qMin(kCornerRadius, qMin(r.width() / 4.0, r.height() / 2.0)) : 0.0;
    if (radius < 1.0) radius = 0.0;
    if (!outlined && radius == 0.0) {
        p.fillRect(r, brush);
        return;
    }
    // The outline runs through pixel centres, so that it is one sharp pixel wide
    QRectF b = r;
    if (outlined) {
        b.setLeft(std::floor(r.left()) + 0.5);
        b.setRight(std::ceil(r.right()) - 0.5);
        b.setTop(qMin(std::floor(r.top()) + 0.5, r.bottom()));
        if (closed) b.setBottom(qMax(std::ceil(r.bottom()) - 0.5, b.top()));
    }
    // Flat top with slightly rounded corners (the top part only), straight sides, open bottom
    QPainterPath path;
    path.moveTo(b.left(), b.bottom());
    if (radius > 0.0) {
        path.lineTo(b.left(), b.top() + radius);
        path.arcTo(QRectF(b.left(), b.top(), 2 * radius, 2 * radius), 180, -90);
        path.lineTo(b.right() - radius, b.top());
        path.arcTo(QRectF(b.right() - 2 * radius, b.top(), 2 * radius, 2 * radius), 90, -90);
    } else {
        path.lineTo(b.left(), b.top());
        path.lineTo(b.right(), b.top());
    }
    path.lineTo(b.right(), b.bottom());
    QPainterPath filled = path;
    filled.closeSubpath();
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillPath(filled, brush);
    if (outlined) {
        p.setPen(QPen(outlineColour(dark), 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(closed ? filled : path);
    }
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
