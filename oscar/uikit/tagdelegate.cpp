/* Tag delegate
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tagdelegate.h"

#include <QApplication>
#include <QPainter>

#include "theme.h"

namespace UiKit {

namespace {
constexpr int kGap = 8;         //!< Cell text to tag
constexpr int kTagPadX = 7;     //!< Inside the tag, left and right

QFont tagFont()
{
    return Theme::font(0.85, QFont::Bold);
}

int tagWidth(const QString &tag)
{
    return QFontMetrics(tagFont()).horizontalAdvance(tag) + 2 * kTagPadX;
}
}

void TagDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                        const QModelIndex &index) const
{
    QStyledItemDelegate::paint(painter, option, index);
    const QString tag = index.data(TagRole).toString();
    if (tag.isEmpty()) return;

    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    const QWidget *widget = opt.widget;
    QStyle *style = widget ? widget->style() : QApplication::style();
    const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, widget);
    const int textEnd = textRect.left() + QFontMetrics(opt.font).horizontalAdvance(opt.text);

    const QFont font = tagFont();
    const int h = QFontMetrics(font).height() + 2;
    QRectF pill(textEnd + kGap, option.rect.center().y() - h / 2.0 + 0.5, tagWidth(tag), h);
    if (pill.right() > option.rect.right()) return;     // no room: leave the tag out

    const bool selected = opt.state & QStyle::State_Selected;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(Theme::color(selected ? Theme::Role::Surface : Theme::Role::AccentSoft));
    painter->drawRoundedRect(pill, h / 2.0, h / 2.0);
    painter->setFont(font);
    painter->setPen(Theme::color(Theme::Role::AccentInk));
    painter->drawText(pill, Qt::AlignCenter, tag);
    painter->restore();
}

QSize TagDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    const QString tag = index.data(TagRole).toString();
    if (!tag.isEmpty()) size.rwidth() += kGap + tagWidth(tag);
    return size;
}

} // namespace UiKit
