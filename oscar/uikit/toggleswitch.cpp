/* Toggle switch
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "toggleswitch.h"

#include <QPainter>

#include "theme.h"

namespace UiKit {

ToggleSwitch::ToggleSwitch(QWidget *parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setFixedSize(sizeHint());
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF track = QRectF(rect()).adjusted(1, 1, -1, -1);
    const qreal radius = track.height() / 2;
    const bool on = isChecked();
    const qreal knob = track.height() - 4;

    if (on) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(Theme::color(Theme::Role::Accent));
    } else {
        painter.setPen(QPen(Theme::color(Theme::Role::BadLine), 1.2));
        painter.setBrush(Theme::color(Theme::Role::Surface));
    }
    painter.drawRoundedRect(track, radius, radius);

    const qreal x = on ? track.right() - 2 - knob : track.left() + 2;
    painter.setPen(Qt::NoPen);
    painter.setBrush(Theme::color(on ? Theme::Role::OnAccent : Theme::Role::BadLine));
    painter.drawEllipse(QRectF(x, track.top() + 2, knob, knob));

    if (hasFocus()) {
        painter.setPen(QPen(Theme::color(Theme::Role::Ink2), 1, Qt::DotLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius + 1, radius + 1);
    }
    if (!isEnabled()) {
        painter.fillRect(rect(), QColor(128, 128, 128, 60));
    }
}

} // namespace UiKit
