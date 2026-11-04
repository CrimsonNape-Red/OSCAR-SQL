/* Action tile component
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "actiontile.h"

#include <QEnterEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

#include "theme.h"

namespace UiKit {

namespace {
constexpr int kPadding = 14;        //!< Inside the tile
constexpr int kIconBox = 40;        //!< Rounded square behind the icon
constexpr int kIconBoxRadius = 10;
constexpr int kIconSize = 22;
constexpr int kIconGap = 10;        //!< Icon box to title
constexpr int kTitleGap = 2;        //!< Title to subtitle
constexpr int kTagGap = 8;          //!< Icon box to tag
constexpr double kDisabledOpacity = 0.45;
constexpr int kTextFlags = Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap;
}

ActionTile::ActionTile(LineIcon icon, const QString &title, const QString &subtitle, QWidget *parent)
    : QAbstractButton(parent), m_icon(icon), m_subtitle(subtitle)
{
    setText(title);
    setAccessibleName(title);
    setAccessibleDescription(subtitle);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);   // keyboard users get a focus ring; mouse clicks don't leave one
    setAttribute(Qt::WA_Hover);
    QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
}

void ActionTile::setPrimary(bool primary)
{
    m_primary = primary;
    update();
}

void ActionTile::setTag(const QString &tag)
{
    m_tag = tag;
    update();
}

QFont ActionTile::titleFont() const
{
    return Theme::font(1.15, QFont::DemiBold);
}

QFont ActionTile::subtitleFont() const
{
    return Theme::font(1.0);
}

int ActionTile::heightForWidth(int width) const
{
    int textWidth = qMax(1, width - 2 * kPadding);
    QFontMetrics tf(titleFont()), sf(subtitleFont());
    int titleH = tf.boundingRect(QRect(0, 0, textWidth, 10000), kTextFlags, text()).height();
    int subH = sf.boundingRect(QRect(0, 0, textWidth, 10000), kTextFlags, m_subtitle).height();
    return kPadding + kIconBox + kIconGap + titleH + kTitleGap + subH + kPadding;
}

QSize ActionTile::sizeHint() const
{
    return QSize(170, heightForWidth(170));
}

QSize ActionTile::minimumSizeHint() const
{
    return QSize(120, heightForWidth(120));
}

void ActionTile::enterEvent(QEnterEvent *event)
{
    m_hover = true;
    update();
    QAbstractButton::enterEvent(event);
}

void ActionTile::leaveEvent(QEvent *event)
{
    m_hover = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void ActionTile::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::EnabledChange) {
        setCursor(isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
    QAbstractButton::changeEvent(event);
}

void ActionTile::paintEvent(QPaintEvent *)
{
    using Theme::Role;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const bool enabled = isEnabled();
    const bool pressed = enabled && isDown();
    const bool hover = enabled && m_hover;
    if (!enabled) p.setOpacity(kDisabledOpacity);
    if (pressed) p.translate(0, 1);    // pressed tiles sink by a pixel, as in the mockup

    // Background and border
    QColor fill = m_primary ? Theme::color(Role::Accent) : Theme::color(Role::Surface);
    QColor border = m_primary ? Theme::color(Role::Accent) : Theme::color(Role::Line);
    if (pressed) {
        fill = m_primary ? fill.darker(115) : Theme::color(Role::Surface2);
    }
    if (hover) {
        border = m_primary ? Theme::color(Role::AccentInk) : Theme::color(Role::Accent);
    }
    QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    if (pressed) box.adjust(0, 0, 0, -1);
    p.setPen(QPen(border, 1));
    p.setBrush(fill);
    p.drawRoundedRect(box, Theme::Radius, Theme::Radius);

    if (hasFocus() && focusPolicy() != Qt::NoFocus) {
        p.setPen(QPen(Theme::color(Role::AccentInk), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(box.adjusted(2, 2, -2, -2), Theme::Radius - 2, Theme::Radius - 2);
    }

    // Icon in its rounded square
    const QRectF iconBox(kPadding, kPadding, kIconBox, kIconBox);
    QColor iconFill = m_primary ? QColor(255, 255, 255, 46) : Theme::color(Role::AccentSoft);
    p.setPen(Qt::NoPen);
    p.setBrush(iconFill);
    p.drawRoundedRect(iconBox, kIconBoxRadius, kIconBoxRadius);
    const double inset = (kIconBox - kIconSize) / 2.0;
    paintLineIcon(p, m_icon, iconBox.adjusted(inset, inset, -inset, -inset),
                  m_primary ? Theme::color(Role::OnAccent) : Theme::color(Role::Accent));

    // Optional tag beside the icon
    if (!m_tag.isEmpty()) {
        QFont tagFont = Theme::font(0.85, QFont::Bold);
        QFontMetrics fm(tagFont);
        QRectF tag(iconBox.right() + kTagGap, iconBox.center().y() - (fm.height() + 2) / 2.0,
                   fm.horizontalAdvance(m_tag) + 14, fm.height() + 2);
        p.setBrush(Theme::color(Role::AccentSoft));
        p.drawRoundedRect(tag, tag.height() / 2, tag.height() / 2);
        p.setFont(tagFont);
        p.setPen(Theme::color(Role::AccentInk));
        p.drawText(tag, Qt::AlignCenter, m_tag);
    }

    // Title and subtitle
    const int textWidth = width() - 2 * kPadding;
    QFont tf = titleFont();
    QFontMetrics tfm(tf);
    QRect titleRect = tfm.boundingRect(QRect(kPadding, kPadding + kIconBox + kIconGap, textWidth, 10000),
                                       kTextFlags, text());
    p.setFont(tf);
    p.setPen(m_primary ? Theme::color(Role::OnAccent) : Theme::color(Role::Ink));
    p.drawText(titleRect, kTextFlags, text());

    QFont sf = subtitleFont();
    QFontMetrics sfm(sf);
    QRect subRect(kPadding, titleRect.bottom() + 1 + kTitleGap, textWidth, 10000);
    p.setFont(sf);
    QColor subColour = m_primary ? Theme::color(Role::OnAccent) : Theme::color(Role::Ink2);
    if (m_primary) subColour.setAlphaF(0.85);
    p.setPen(subColour);
    p.drawText(subRect, kTextFlags, m_subtitle);
}

} // namespace UiKit
