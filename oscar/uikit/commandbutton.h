/* Command button component
 *
 * A tile-style push button: a line icon beside a bold title and a one-line description.
 * Used for the Profile tab's actions; it looks like the Welcome page's action tiles but
 * is laid out sideways to fit a narrow column. It is a QPushButton, so it can replace a
 * plain button in a Qt Designer form (promoted widget) without changing its connections.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef UIKIT_COMMANDBUTTON_H
#define UIKIT_COMMANDBUTTON_H

#include <QPushButton>

#include "lineicons.h"

namespace UiKit {

/*! \class CommandButton
    \brief Icon beside title (the button's text) and subtitle; shows hover, pressed, focus
           and disabled states */
class CommandButton : public QPushButton
{
    Q_OBJECT

public:
    //! \brief How the button is drawn
    enum class Kind {
        Normal,     //!< Surface colour with a border
        Primary,    //!< The page's main action, filled with the accent colour
        Danger      //!< Destructive action, drawn in the "bad" colour
    };

    explicit CommandButton(QWidget *parent = nullptr);

    //! \brief Sets the icon, title, description and kind in one call
    void setup(LineIcon icon, const QString &title, const QString &subtitle, Kind kind = Kind::Normal);

    void setLineIcon(LineIcon icon);
    void setSubtitle(const QString &subtitle);
    void setKind(Kind kind);

    QString subtitle() const { return m_subtitle; }

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
    //! \brief Width available to the title and subtitle in a button \a width wide
    int textWidth(int width) const;

    LineIcon m_icon = LineIcon::None;
    QString m_subtitle;
    Kind m_kind = Kind::Normal;
    bool m_hover = false;
};

} // namespace UiKit

#endif // UIKIT_COMMANDBUTTON_H
