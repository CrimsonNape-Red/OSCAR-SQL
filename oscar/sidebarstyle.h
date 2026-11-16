/* Right sidebar style
 *
 * Colours and style sheets for the right sidebar (the Bookmarks and Records panels, on
 * tabs). All colours come from the theme, so the sidebar follows the light or dark palette.
 * The panels are flat boxes with the Classic pages and rounded cards with the Modern pages;
 * the tabs are the platform's own, like the tabs of the main window.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SIDEBARSTYLE_H
#define SIDEBARSTYLE_H

#include <QColor>
#include <QFont>
#include <QPalette>
#include <QString>

namespace SidebarStyle {

//! \brief The sidebar's colours for a palette
struct Colours {
    QColor background;      //!< Behind the tabs and panels
    QColor panel;           //!< Bookmarks and Records text areas, and the filter box
    QColor border;          //!< Outline of the panels and the filter box
    QColor text;            //!< Body text and headings
    QColor muted;           //!< Notes in italics, placeholder text, the clear button
    QColor link;            //!< Dates that open a day or a range
};

//! \brief Returns the sidebar's colours for \a palette
Colours colours(const QPalette &palette);

/*! \brief Returns the style sheet for the sidebar's QTabWidget and everything inside it.
    Child widgets are styled from here by type and object name, so they need no style
    sheets of their own. The tabs themselves are left to the platform style. */
QString styleSheet(const QPalette &palette, bool modern);

//! \brief Returns the space in pixels around a panel's content: the cards of the Modern
//!        pages stand clear of the sidebar's edge, the Classic boxes fill it
int panelMargin(bool modern);

/*! \brief Returns a complete HTML document for a sidebar text area.
    \a body is the content between the body tags, built without colours; text, link and
    note colours are added here for \a palette. \a font is the application font.
    \a extraCss is appended to the style block. */
QString document(const QString &body, const QPalette &palette, const QFont &font,
                 const QString &extraCss = QString());

} // namespace SidebarStyle

#endif // SIDEBARSTYLE_H
