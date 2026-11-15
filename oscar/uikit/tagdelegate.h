/* Tag delegate
 *
 * Item delegate that draws a small rounded tag (e.g. "Open") after a cell's text, using the
 * theme's accent colours. The tag text comes from the cell's TagRole data; cells without it
 * are drawn as usual.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef UIKIT_TAGDELEGATE_H
#define UIKIT_TAGDELEGATE_H

#include <QStyledItemDelegate>

namespace UiKit {

/*! \class TagDelegate
    \brief Draws the cell normally, then the TagRole text as a pill after the cell's text */
class TagDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    //! \brief Model role holding the tag text; an empty or missing value draws no tag
    static constexpr int TagRole = Qt::UserRole + 20;

    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

} // namespace UiKit

#endif // UIKIT_TAGDELEGATE_H
