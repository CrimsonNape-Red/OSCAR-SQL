/* Right sidebar style
 *
 * Colours and style sheets for the right sidebar (the Bookmarks and Records panels). All
 * colours come from the theme, so the sidebar follows the light or dark palette, and the
 * panels are drawn as flat boxes with the Classic pages and as rounded cards with the
 * Modern pages.
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

//! \brief The sidebar's colours for a palette and page look
struct Colours {
    QColor background;      //!< Behind the panel headers and panels
    QColor panel;           //!< Bookmarks and Records text areas, and the filter box
    QColor border;          //!< Outline of the panels, filter box and Classic headers
    QColor text;            //!< Body text and headings
    QColor muted;           //!< Notes in italics, placeholder text, the clear button
    QColor link;            //!< Dates that open a day or a range
    QColor header;          //!< Panel header that is not open
    QColor headerText;
    QColor headerOpen;      //!< Header of the open panel
    QColor headerOpenText;
};

//! \brief Returns the sidebar's colours for \a palette, with the Modern or Classic pages
Colours colours(const QPalette &palette, bool modern);

/*! \brief Returns the style sheet for the sidebar's QToolBox and everything inside it.
    Child widgets are styled from here by type and object name, so they need no style
    sheets of their own. */
QString styleSheet(const QPalette &palette, bool modern);

//! \brief Returns the space in pixels around a panel's content: the cards of the Modern
//!        pages stand clear of the sidebar's edge, the Classic boxes fill it
int panelMargin(bool modern);

/*! \brief Returns a complete HTML document for a sidebar text area.
    \a body is the content between the body tags, built without colours; text, link and
    note colours are added here for \a palette. \a font is the application font.
    \a extraCss is appended to the style block. */
QString document(const QString &body, const QPalette &palette, bool modern, const QFont &font,
                 const QString &extraCss = QString());

} // namespace SidebarStyle

#endif // SIDEBARSTYLE_H
