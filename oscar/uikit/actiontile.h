/* Action tile component
 *
 * A large clickable tile with a line icon, a title and a short description; used for the
 * Welcome page's main actions.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef UIKIT_ACTIONTILE_H
#define UIKIT_ACTIONTILE_H

#include <QAbstractButton>

#include "lineicons.h"

namespace UiKit {

/*! \class ActionTile
    \brief Icon above title and subtitle; shows hover, pressed, focus and disabled states */
class ActionTile : public QAbstractButton
{
    Q_OBJECT

public:
    ActionTile(LineIcon icon, const QString &title, const QString &subtitle, QWidget *parent = nullptr);

    //! \brief Highlights the tile as the page's main action (filled with the accent colour)
    void setPrimary(bool primary);

    //! \brief Shows a small pill next to the icon, e.g. "Coming later"; empty hides it
    void setTag(const QString &tag);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    QFont titleFont() const;
    QFont subtitleFont() const;

    LineIcon m_icon;
    QString m_subtitle;
    QString m_tag;
    bool m_primary = false;
    bool m_hover = false;
};

} // namespace UiKit

#endif // UIKIT_ACTIONTILE_H
