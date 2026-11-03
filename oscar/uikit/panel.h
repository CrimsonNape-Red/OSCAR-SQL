/* Panel component
 *
 * A rounded, filled container with an optional border and drop shadow; the building block
 * for strips, notices and grouped content on the redesigned pages.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef UIKIT_PANEL_H
#define UIKIT_PANEL_H

#include <QFrame>

#include "theme.h"

namespace UiKit {

/*! \class Panel
    \brief Rounded rectangle in theme colours that holds other widgets */
class Panel : public QFrame
{
    Q_OBJECT

public:
    /*! \param fill Background role
        \param border Border role; Theme::Role::Count means no border
        \param radius Corner radius in pixels */
    explicit Panel(Theme::Role fill = Theme::Role::Surface, Theme::Role border = Theme::Role::Line,
                   int radius = Theme::Radius, QWidget *parent = nullptr);

    //! \brief Adds the card drop shadow
    void setShadow(bool shadow);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Theme::Role m_fill;
    Theme::Role m_border;
    int m_radius;
};

} // namespace UiKit

#endif // UIKIT_PANEL_H
