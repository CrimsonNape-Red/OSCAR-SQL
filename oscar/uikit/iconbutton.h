/* Icon button component
 *
 * A small square button showing only a line icon, such as the gear that opens a page's
 * settings. Always give it a tooltip, since it has no text.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef UIKIT_ICONBUTTON_H
#define UIKIT_ICONBUTTON_H

#include <QAbstractButton>

#include "lineicons.h"

namespace UiKit {

/*! \class IconButton
    \brief Square icon-only button; shows hover, pressed, focus and disabled states */
class IconButton : public QAbstractButton
{
    Q_OBJECT

public:
    /*! \param icon The icon to draw
        \param toolTip Shown on hover and used as the accessible name */
    IconButton(LineIcon icon, const QString &toolTip, QWidget *parent = nullptr);

    //! \brief For buttons created by a .ui form; call setLineIcon() and give it a tooltip
    explicit IconButton(QWidget *parent = nullptr);

    //! \brief Changes the icon
    void setLineIcon(LineIcon icon);

    /*! \brief Draws no box on hover or press, only a change of icon colour; for buttons that
        sit in a bar, such as the main tab bar */
    void setFrameless(bool frameless);

    //! \brief Sets the side of the square button; the icon keeps its size unless the button is too small for it
    void setSide(int side);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    //! \brief Enter clicks the button, as Space does
    void keyPressEvent(QKeyEvent *event) override;

private:
    LineIcon m_icon = LineIcon::None;
    bool m_hover = false;
    bool m_frameless = false;
    int m_side;                 //!< Side of the button
};

} // namespace UiKit

#endif // UIKIT_ICONBUTTON_H
