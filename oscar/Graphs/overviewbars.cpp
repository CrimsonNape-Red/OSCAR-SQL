/* Overview bars
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "overviewbars.h"

#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace OverviewBars {

namespace {

constexpr double kGapShare = 0.3;       //!< Share of a day's slot left as a gap between days
constexpr double kMinGapSlot = 3.0;     //!< Narrowest slot that still gets a (1 px) gap
constexpr double kMinCylinder = 8.0;    //!< Narrowest bar drawn as a cylinder (as today's gradient bars)
constexpr double kCapHeight = 6.0;      //!< Height of a cylinder's rounded top

//! \brief Moves \a c a fraction \a t (0-1) of the way towards \a to
QColor mix(const QColor &c, const QColor &to, double t)
{
    auto ch = [t](int a, int b) { return qRound(a + (b - a) * t); };
    return QColor(ch(c.red(), to.red()), ch(c.green(), to.green()), ch(c.blue(), to.blue()), c.alpha());
}

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
    return { gap / 2.0, width, width >= kMinCylinder };
}

QLinearGradient cylinderGradient(const QColor &c, double left, double width)
{
    const QColor black(Qt::black), white(Qt::white);
    QLinearGradient g(left, 0, left + width, 0);
    g.setColorAt(0.0, mix(c, black, 0.40));
    g.setColorAt(0.28, mix(c, black, 0.08));
    g.setColorAt(0.45, mix(c, white, 0.22));
    g.setColorAt(0.62, mix(c, black, 0.08));
    g.setColorAt(1.0, mix(c, black, 0.45));
    return g;
}

void paintSegment(QPainter &p, const QRectF &r, const QColor &c, bool top, bool cylinder)
{
    if (r.isEmpty()) return;
    if (!cylinder) {
        p.fillRect(r, c);
        return;
    }
    const QBrush brush(cylinderGradient(c, r.left(), r.width()));
    const double cap = qMin(kCapHeight, r.height() / 2.0);
    if (!top || cap < 1.0) {
        p.fillRect(r, brush);
        return;
    }
    // Rounded top: an elliptical arc across the full width, straight sides and bottom
    QPainterPath path;
    path.moveTo(r.left(), r.bottom());
    path.lineTo(r.left(), r.top() + cap);
    path.arcTo(QRectF(r.left(), r.top(), r.width(), 2 * cap), 180, -180);
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
