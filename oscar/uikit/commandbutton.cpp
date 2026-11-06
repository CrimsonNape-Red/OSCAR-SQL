/* Command button component
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "commandbutton.h"

#include <QEnterEvent>
#include <QFontMetrics>
#include <QPainter>

#include "theme.h"

namespace UiKit {

namespace {
constexpr int kPadX = 14;           //!< Left and right padding
constexpr int kPadY = 12;           //!< Top and bottom padding
constexpr int kIconBox = 40;        //!< Rounded square behind the icon
constexpr int kIconBoxRadius = 10;
constexpr int kIconSize = 22;
constexpr int kIconGap = 12;        //!< Icon box to text
constexpr int kTitleGap = 2;        //!< Title to subtitle
constexpr int kTextFlags = Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap;
}

CommandButton::CommandButton(QWidget *parent)
    : QPushButton(parent)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);   // keyboard users get a focus ring; mouse clicks don't leave one
    setAttribute(Qt::WA_Hover);
    QSizePolicy sp(QSizePolicy::Preferred, QSizePolicy::Fixed);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
}

void CommandButton::setup(LineIcon icon, const QString &title, const QString &subtitle, Kind kind)
{
    setText(title);
    setAccessibleName(title);
    setLineIcon(icon);
    setSubtitle(subtitle);
    setKind(kind);
}

void CommandButton::setLineIcon(LineIcon icon)
{
    m_icon = icon;
    update();
}

void CommandButton::setSubtitle(const QString &subtitle)
{
    m_subtitle = subtitle;
    setAccessibleDescription(subtitle);
    updateGeometry();
    update();
}

void CommandButton::setKind(Kind kind)
{
    m_kind = kind;
    update();
}

QFont CommandButton::titleFont() const
{
    return Theme::font(1.15, QFont::DemiBold);
}

QFont CommandButton::subtitleFont() const
{
    return Theme::font(1.0);
}

int CommandButton::textWidth(int width) const
{
    return qMax(1, width - 2 * kPadX - kIconBox - kIconGap);
}

int CommandButton::heightForWidth(int width) const
{
    const int tw = textWidth(width);
    QFontMetrics tf(titleFont()), sf(subtitleFont());
    int textH = tf.boundingRect(QRect(0, 0, tw, 10000), kTextFlags, text()).height();
    if (!m_subtitle.isEmpty())
        textH += kTitleGap + sf.boundingRect(QRect(0, 0, tw, 10000), kTextFlags, m_subtitle).height();
    return 2 * kPadY + qMax(kIconBox, textH);
}

QSize CommandButton::sizeHint() const
{
    QFontMetrics tf(titleFont()), sf(subtitleFont());
    int w = 2 * kPadX + kIconBox + kIconGap
            + qMax(tf.horizontalAdvance(text()), sf.horizontalAdvance(m_subtitle));
    w = qBound(200, w, 360);
    return QSize(w, heightForWidth(w));
}

QSize CommandButton::minimumSizeHint() const
{
    return QSize(180, heightForWidth(180));
}

void CommandButton::enterEvent(QEnterEvent *event)
{
    m_hover = true;
    update();
    QPushButton::enterEvent(event);
}

void CommandButton::leaveEvent(QEvent *event)
{
    m_hover = false;
    update();
    QPushButton::leaveEvent(event);
}

void CommandButton::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::EnabledChange) {
        setCursor(isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
    } else if (event->type() == QEvent::FontChange) {
        updateGeometry();
    }
    QPushButton::changeEvent(event);
}

void CommandButton::paintEvent(QPaintEvent *)
{
    using Theme::Role;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // A disabled button is drawn plainly in muted colours rather than faded, so a disabled
    // primary button reads as unavailable instead of as a washed-out blue one.
    const bool enabled = isEnabled();
    const bool pressed = enabled && isDown();
    const bool hover = enabled && m_hover;
    const bool primary = enabled && m_kind == Kind::Primary;
    const bool danger = enabled && m_kind == Kind::Danger;
    if (pressed) p.translate(0, 1);    // pressed buttons sink by a pixel, like the tiles

    // Background and border
    QColor fill = primary ? Theme::color(Role::Accent) : Theme::color(Role::Surface);
    QColor border = primary ? Theme::color(Role::Accent)
                  : danger ? Theme::color(Role::BadLine) : Theme::color(Role::Line);
    if (pressed) {
        fill = primary ? fill.darker(115) : danger ? Theme::color(Role::BadTint)
                                                    : Theme::color(Role::Surface2);
    }
    if (hover) {
        border = primary ? Theme::color(Role::AccentInk)
               : danger ? Theme::color(Role::Bad) : Theme::color(Role::Accent);
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

    // Icon in its rounded square, centred vertically
    const QRectF iconBox(kPadX, (height() - kIconBox) / 2.0, kIconBox, kIconBox);
    QColor iconFill = !enabled ? Theme::color(Role::Surface2)
                    : primary ? QColor(255, 255, 255, 46)
                    : danger ? Theme::color(Role::BadSoft) : Theme::color(Role::AccentSoft);
    QColor iconInk = !enabled ? Theme::color(Role::Ink3)
                   : primary ? Theme::color(Role::OnAccent)
                   : danger ? Theme::color(Role::Bad) : Theme::color(Role::Accent);
    p.setPen(Qt::NoPen);
    p.setBrush(iconFill);
    p.drawRoundedRect(iconBox, kIconBoxRadius, kIconBoxRadius);
    const double inset = (kIconBox - kIconSize) / 2.0;
    paintLineIcon(p, m_icon, iconBox.adjusted(inset, inset, -inset, -inset), iconInk);

    // Title and subtitle, centred vertically as a block
    const int tw = textWidth(width());
    const int x = kPadX + kIconBox + kIconGap;
    QFont tf = titleFont(), sf = subtitleFont();
    QFontMetrics tfm(tf), sfm(sf);
    QRect titleRect = tfm.boundingRect(QRect(0, 0, tw, 10000), kTextFlags, text());
    QRect subRect = m_subtitle.isEmpty() ? QRect()
                  : sfm.boundingRect(QRect(0, 0, tw, 10000), kTextFlags, m_subtitle);
    const int blockH = titleRect.height() + (subRect.isNull() ? 0 : kTitleGap + subRect.height());
    const int top = (height() - blockH) / 2;

    p.setFont(tf);
    p.setPen(!enabled ? Theme::color(Role::Ink3)
             : primary ? Theme::color(Role::OnAccent)
             : danger ? Theme::color(Role::Bad) : Theme::color(Role::Ink));
    p.drawText(QRect(x, top, tw, titleRect.height()), kTextFlags, text());

    if (!m_subtitle.isEmpty()) {
        QColor subColour = !enabled ? Theme::color(Role::Ink3)
                         : primary ? Theme::color(Role::OnAccent) : Theme::color(Role::Ink2);
        if (primary) subColour.setAlphaF(0.88);
        p.setFont(sf);
        p.setPen(subColour);
        p.drawText(QRect(x, top + titleRect.height() + kTitleGap, tw, subRect.height()),
                   kTextFlags, m_subtitle);
    }
}

} // namespace UiKit
