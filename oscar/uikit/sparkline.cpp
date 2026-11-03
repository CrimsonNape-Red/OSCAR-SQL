/* Sparkline component
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "sparkline.h"

#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <limits>

#include "theme.h"

namespace UiKit {

namespace {
constexpr int kHeight = 38;         //!< Fixed sparkline height, as the mockup
constexpr double kPad = 4;          //!< Space above and below the line
constexpr double kDotRadius = 4.5;  //!< Last-value dot, including its outline

//! \brief Vertical scale: smallest and largest non-NaN value and the reference
void valueRange(const QVector<double> &values, double reference, double &lo, double &hi)
{
    lo = hi = reference;
    for (double v : values) {
        if (std::isnan(v)) continue;
        lo = qMin(lo, v);
        hi = qMax(hi, v);
    }
    if (hi - lo < 1e-9) {
        hi += 1;
        lo -= 1;
    }
}

double yFor(double v, double lo, double hi, double height, double pad)
{
    return pad + (height - 2 * pad) * (1 - (v - lo) / (hi - lo));
}
}

Sparkline::Sparkline(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void Sparkline::setData(const QVector<double> &values, double reference, const QColor &dotColour)
{
    m_values = values;
    m_reference = reference;
    m_dot = dotColour;
    update();
}

QVector<QPointF> Sparkline::mapPoints(const QVector<double> &values, double reference,
                                      QSizeF size, double pad)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    QVector<QPointF> points;
    points.reserve(values.size());
    if (values.isEmpty()) return points;

    double lo, hi;
    valueRange(values, reference, lo, hi);

    const double step = values.size() > 1 ? size.width() / (values.size() - 1) : 0;
    for (int i = 0; i < values.size(); ++i) {
        double v = values[i];
        if (std::isnan(v)) {
            points.append(QPointF(nan, nan));
        } else {
            points.append(QPointF(i * step, yFor(v, lo, hi, size.height(), pad)));
        }
    }
    return points;
}

QSize Sparkline::sizeHint() const
{
    return QSize(200, kHeight);
}

QSize Sparkline::minimumSizeHint() const
{
    return QSize(60, kHeight);
}

void Sparkline::paintEvent(QPaintEvent *)
{
    if (m_values.isEmpty()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Keep the dot inside the widget at the right edge
    QSizeF box(width() - kDotRadius, height());
    QVector<QPointF> pts = mapPoints(m_values, m_reference, box, kPad);

    // Contiguous runs of nights with data; gaps break both the area and the line
    QVector<QVector<QPointF>> runs;
    QVector<QPointF> run;
    for (const QPointF &pt : pts) {
        if (std::isnan(pt.y())) {
            if (!run.isEmpty()) runs.append(run);
            run.clear();
        } else {
            run.append(pt);
        }
    }
    if (!run.isEmpty()) runs.append(run);

    for (const QVector<QPointF> &r : runs) {
        if (r.size() < 2) continue;
        QPainterPath area;
        area.moveTo(r.first().x(), box.height());
        for (const QPointF &pt : r) area.lineTo(pt);
        area.lineTo(r.last().x(), box.height());
        area.closeSubpath();
        p.fillPath(area, Theme::color(Theme::Role::AccentSoft));
    }

    // Dashed reference line (the 30-day average)
    double lo, hi;
    valueRange(m_values, m_reference, lo, hi);
    const double refY = yFor(m_reference, lo, hi, box.height(), kPad);
    QPen dash(Theme::color(Theme::Role::Ink3), 1, Qt::CustomDashLine);
    dash.setDashPattern({ 3, 3 });
    p.setPen(dash);
    p.drawLine(QPointF(0, refY), QPointF(box.width(), refY));

    QPen line(Theme::color(Theme::Role::Accent), 1.6);
    line.setJoinStyle(Qt::RoundJoin);
    line.setCapStyle(Qt::RoundCap);
    p.setPen(line);
    for (const QVector<QPointF> &r : runs) {
        if (r.size() >= 2) p.drawPolyline(r.constData(), r.size());
    }

    // Dot on the last value, outlined in the surface colour
    const QPointF last = pts.last();
    if (!std::isnan(last.y())) {
        p.setPen(QPen(Theme::color(Theme::Role::Surface), 2));
        p.setBrush(m_dot.isValid() ? m_dot : Theme::color(Theme::Role::Accent));
        p.drawEllipse(last, kDotRadius - 1, kDotRadius - 1);
    }
}

} // namespace UiKit
