/* Event bar component
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "eventbar.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

#include "theme.h"

namespace UiKit {

namespace {
constexpr int kBarHeight = 10;      //!< Height of the proportional bar
constexpr int kBarGap = 2;          //!< Gap between bar segments
constexpr int kLegendTop = 8;       //!< Space between the bar and the legend
constexpr int kSwatch = 9;          //!< Legend swatch size
constexpr int kSwatchGap = 4;       //!< Swatch to text
constexpr int kEntryGap = 12;       //!< Between legend entries
constexpr int kRowGap = 2;          //!< Between wrapped legend rows

QFont legendFont()
{
    return Theme::font(0.96);
}

QString entryText(const EventBar::Part &part)
{
    return part.label + " " + QString::number(part.count);
}
}

EventBar::EventBar(QWidget *parent)
    : QWidget(parent)
{
    QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
}

void EventBar::setParts(const QVector<Part> &parts)
{
    m_parts.clear();
    for (const Part &p : parts) {
        if (p.count > 0) m_parts.append(p);
    }
    updateGeometry();
    update();
}

int EventBar::legendHeight(int width) const
{
    QFontMetrics fm(legendFont());
    int rows = m_parts.isEmpty() ? 0 : 1, x = 0;
    for (const Part &p : m_parts) {
        int w = kSwatch + kSwatchGap + fm.horizontalAdvance(entryText(p));
        if (x > 0 && x + w > width) {
            rows++;
            x = 0;
        }
        x += w + kEntryGap;
    }
    return rows * fm.height() + qMax(0, rows - 1) * kRowGap;
}

int EventBar::heightForWidth(int width) const
{
    return kBarHeight + kLegendTop + legendHeight(width);
}

QSize EventBar::sizeHint() const
{
    return QSize(200, heightForWidth(200));
}

QSize EventBar::minimumSizeHint() const
{
    return QSize(60, kBarHeight);
}

void EventBar::paintEvent(QPaintEvent *)
{
    if (m_parts.isEmpty()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Rounded bar, segments separated by small gaps
    int total = 0;
    for (const Part &part : m_parts) total += part.count;
    const double usable = width() - kBarGap * (m_parts.size() - 1);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, width(), kBarHeight), kBarHeight / 2.0, kBarHeight / 2.0);
    p.save();
    p.setClipPath(clip);
    double x = 0;
    for (const Part &part : m_parts) {
        double w = usable * part.count / total;
        p.fillRect(QRectF(x, 0, w, kBarHeight), part.colour);
        x += w + kBarGap;
    }
    p.restore();

    // Legend, wrapping onto more rows when narrow
    QFont font = legendFont();
    QFontMetrics fm(font);
    p.setFont(font);
    int lx = 0, ly = kBarHeight + kLegendTop;
    for (const Part &part : m_parts) {
        QString text = entryText(part);
        int w = kSwatch + kSwatchGap + fm.horizontalAdvance(text);
        if (lx > 0 && lx + w > width()) {
            lx = 0;
            ly += fm.height() + kRowGap;
        }
        QRectF swatch(lx, ly + (fm.height() - kSwatch) / 2.0, kSwatch, kSwatch);
        p.setPen(Qt::NoPen);
        p.setBrush(part.colour);
        p.drawRoundedRect(swatch, 2, 2);
        p.setPen(Theme::color(Theme::Role::Ink2));
        p.drawText(QPointF(lx + kSwatch + kSwatchGap, ly + fm.ascent()), text);
        lx += w + kEntryGap;
    }
}

} // namespace UiKit
