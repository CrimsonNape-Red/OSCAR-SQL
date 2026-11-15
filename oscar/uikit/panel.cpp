/* Panel component
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "panel.h"

#include <QGraphicsDropShadowEffect>
#include <QPainter>

namespace UiKit {

Panel::Panel(Theme::Role fill, Theme::Role border, int radius, QWidget *parent)
    : QFrame(parent), m_fill(fill), m_border(border), m_radius(radius)
{
    setFrameShape(QFrame::NoFrame);
}

void Panel::setShadow(bool shadow)
{
    if (!shadow) {
        setGraphicsEffect(nullptr);
        return;
    }
    auto *effect = new QGraphicsDropShadowEffect(this);
    effect->setBlurRadius(18);
    effect->setOffset(0, 6);
    effect->setColor(Theme::color(Theme::Role::Shadow));
    setGraphicsEffect(effect);
}

void Panel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (m_border == Theme::Role::Count) {
        p.setPen(Qt::NoPen);
    } else {
        p.setPen(QPen(Theme::color(m_border), 1));
    }
    p.setBrush(Theme::color(m_fill));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), m_radius, m_radius);
}

} // namespace UiKit
