/* Icon button component
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "iconbutton.h"

#include <QEnterEvent>
#include <QKeyEvent>
#include <QPainter>

#include "buttonkeys.h"
#include "theme.h"

namespace UiKit {

namespace {
constexpr int kSize = 30;           //!< Side of the button
constexpr int kIconSize = 18;       //!< Side of the icon inside it
constexpr double kDisabledOpacity = 0.45;
}

IconButton::IconButton(LineIcon icon, const QString &toolTip, QWidget *parent)
    : IconButton(parent)
{
    m_icon = icon;
    setToolTip(toolTip);
    setAccessibleName(toolTip);
}

IconButton::IconButton(QWidget *parent)
    : QAbstractButton(parent)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);   // keyboard users get a focus ring; mouse clicks don't leave one
    setAttribute(Qt::WA_Hover);
    setSide(kSize);
}

void IconButton::setFrameless(bool frameless)
{
    m_frameless = frameless;
    update();
}

void IconButton::setSide(int side)
{
    m_side = side;
    setFixedSize(side, side);
    updateGeometry();
}

void IconButton::setLineIcon(LineIcon icon)
{
    m_icon = icon;
    update();
}

QSize IconButton::sizeHint() const
{
    return QSize(m_side, m_side);
}

void IconButton::enterEvent(QEnterEvent *event)
{
    m_hover = true;
    update();
    QAbstractButton::enterEvent(event);
}

void IconButton::leaveEvent(QEvent *event)
{
    m_hover = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void IconButton::keyPressEvent(QKeyEvent *event)
{
    if (isActivateKey(*event)) {
        animateClick();     // shows the pressed state briefly, then clicks
        return;
    }
    QAbstractButton::keyPressEvent(event);
}

void IconButton::paintEvent(QPaintEvent *)
{
    using Theme::Role;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const bool enabled = isEnabled();
    const bool pressed = enabled && isDown();
    const bool hover = enabled && m_hover;
    if (!enabled) p.setOpacity(kDisabledOpacity);

    // Background only on hover or press, so the button stays quiet beside a legend
    QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    if ((pressed || hover) && !m_frameless) {
        p.setPen(QPen(Theme::color(hover ? Role::Accent : Role::Line), 1));
        p.setBrush(Theme::color(pressed ? Role::AccentSoft : Role::Surface));
        p.drawRoundedRect(box, Theme::SmallRadius, Theme::SmallRadius);
    }
    if (hasFocus() && focusPolicy() != Qt::NoFocus) {
        p.setPen(QPen(Theme::color(Role::AccentInk), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(box.adjusted(1.5, 1.5, -1.5, -1.5), Theme::SmallRadius - 1, Theme::SmallRadius - 1);
    }

    const double iconSide = qMin(double(kIconSize), m_side - 4.0);    // a small button keeps a full-size icon
    const double inset = (m_side - iconSide) / 2.0;
    QRectF iconRect = QRectF(rect()).adjusted(inset, inset, -inset, -inset);
    if (pressed) iconRect.translate(0, 1);     // the icon sinks a pixel while pressed
    paintLineIcon(p, m_icon, iconRect, Theme::color(hover || pressed ? Role::AccentInk : Role::Ink2));
}

} // namespace UiKit
