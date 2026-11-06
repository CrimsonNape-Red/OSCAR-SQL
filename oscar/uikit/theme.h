/* Theme module
 *
 * Central colours, sizes and fonts for the redesigned UI. Widgets ask the theme for every
 * colour and font instead of hard-coding them. Colours come from the application palette
 * and fonts from the application font, so the redesigned pages follow the system theme
 * (including dark palettes) and the font chosen in Preferences, like the Oximetry Wizard.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef UIKIT_THEME_H
#define UIKIT_THEME_H

#include <QColor>
#include <QFont>
#include <QPalette>
#include <QString>

/*! \namespace Theme
    \brief Named colour roles, size constants and fonts for the redesigned UI

    Base colours (page, surface, text, borders, accent) are read from the palette. Status
    colours are fixed per light or dark palette, using the Oximetry Wizard's values where it
    has them; their soft fills, tints and borders are mixed into the palette's surface. */
namespace Theme {

//! \brief What a colour is used for. Count must stay last.
enum class Role {
    Page,            //!< Page background (palette Window)
    Surface,         //!< Card and tile background (palette Base)
    Surface2,        //!< Inset boxes on a surface (comparisons, steps)
    Ink,             //!< Main text (palette Text)
    Ink2,            //!< Secondary text
    Ink3,            //!< Muted text, captions, reference lines (palette PlaceholderText)
    Line,            //!< Borders and dividers (palette Mid)
    Accent,          //!< Primary actions (palette Highlight)
    AccentInk,       //!< Accent-coloured text on a surface
    AccentSoft,      //!< Pale accent fills (icon boxes, tags, pressed buttons)
    OnAccent,        //!< Text on an Accent background (palette HighlightedText)
    Good,            //!< Better than the averages
    GoodSoft,
    GoodTint,        //!< Card background when the night is better
    GoodLine,        //!< Card border when the night is better
    Bad,             //!< Worse than the averages
    BadSoft,
    BadTint,         //!< Card background when the night is worse
    BadLine,         //!< Card border when the night is worse
    Neutral,         //!< Changes that are neither better nor worse
    NeutralSoft,
    Warn,            //!< Warning text
    WarnSoft,        //!< Warning background
    WarnLine,        //!< Warning border
    GroupBreath,     //!< "By group" tint: breathing metrics
    GroupBreathLine,
    GroupMask,       //!< "By group" tint: mask and pressure metrics
    GroupMaskLine,
    GroupUsage,      //!< "By group" tint: usage metrics
    GroupUsageLine,
    Shadow,          //!< Card drop shadow
    Count
};

//! \brief Returns true when \a palette is dark (its Base colour has lightness below 128),
//!        the same test the Oximetry Wizard uses
bool isDark(const QPalette &palette);

//! \brief Returns the colour for \a role derived from \a palette
QColor color(Role role, const QPalette &palette);

//! \brief Returns the colour for \a role derived from the application palette
QColor color(Role role);

//! \brief Returns \a c for a Qt stylesheet: "#rrggbb", or "rgba(r, g, b, a)" with a 0-255
//!        alpha when the colour is translucent
QString css(const QColor &c);

//! \brief Returns \a role's colour (from the application palette) for a Qt stylesheet
QString css(Role role);

/*! \brief Returns \a base scaled by \a scale, at least as heavy as \a weight
    Point-sized fonts are scaled in points and pixel-sized fonts in pixels. The family,
    style and any heavier weight of \a base are kept. */
QFont font(qreal scale, QFont::Weight weight, const QFont &base);

/*! \brief Returns the application font scaled by \a scale, at least as heavy as \a weight
    Text sizes are multiples of the application font, so the size chosen in Preferences
    applies to the redesigned pages too. 1.0 is body text. */
QFont font(qreal scale, QFont::Weight weight = QFont::Normal);

constexpr int Radius = 10;        //!< Card and tile corner radius (as the Oximetry Wizard)
constexpr int SmallRadius = 8;    //!< Corner radius of boxes inside cards
constexpr int PagePadding = 24;   //!< Space around the page content
constexpr int SectionGap = 22;    //!< Space between page sections
constexpr int CardGap = 14;       //!< Space between metric cards
constexpr int TileGap = 12;       //!< Space between action tiles
constexpr int CardPadding = 16;   //!< Space inside a card

} // namespace Theme

#endif // UIKIT_THEME_H
