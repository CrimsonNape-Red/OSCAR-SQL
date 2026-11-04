/* Theme module
 *
 * Central colours, sizes and fonts for the redesigned UI. Widgets ask the theme for every
 * colour and font instead of hard-coding them, so a dark colour set can be added later
 * without touching the widgets.
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
#include <QString>

/*! \namespace Theme
    \brief Named colour roles, size constants and font roles for the redesigned UI

    Only the light colour set exists for now. Values come from the Welcome page mockup
    (Notes/UI Redesign/Welcome_Concept.html). */
namespace Theme {

//! \brief What a colour is used for. Count must stay last.
enum class Role {
    Page,            //!< Page background
    Surface,         //!< Card and tile background
    Surface2,        //!< Inset boxes on a surface (comparisons, steps)
    Ink,             //!< Main text
    Ink2,            //!< Secondary text
    Ink3,            //!< Muted text, captions, reference lines
    Line,            //!< Borders and dividers
    Accent,          //!< OSCAR blue: primary actions
    AccentInk,       //!< Accent-coloured text on light backgrounds
    AccentSoft,      //!< Pale accent fills (icon boxes, tags, pressed buttons)
    OnAccent,        //!< Text on an Accent background
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

//! \brief Returns the colour for \a role in the current theme
QColor color(Role role);

//! \brief Returns \a role's colour for a Qt stylesheet: "#rrggbb", or "rgba(r, g, b, a)"
//!        with a 0-255 alpha when the colour is translucent
QString css(Role role);

//! \brief Which typeface a piece of text uses
enum class FontRole {
    Display,         //!< Headings and large numbers (Lexend)
    Body             //!< Everything else (Atkinson Hyperlegible)
};

/*! \brief Returns a font for \a role at \a pixelSize pixels and \a weight
    Falls back to the application font's family when the bundled fonts could not be
    registered. */
QFont font(FontRole role, qreal pixelSize, QFont::Weight weight = QFont::Normal);

//! \brief Registers the bundled fonts with Qt. Safe to call more than once.
void registerFonts();

constexpr int Radius = 12;        //!< Card and tile corner radius
constexpr int SmallRadius = 8;    //!< Corner radius of boxes inside cards
constexpr int PagePadding = 24;   //!< Space around the page content
constexpr int SectionGap = 22;    //!< Space between page sections
constexpr int CardGap = 14;       //!< Space between metric cards
constexpr int TileGap = 12;       //!< Space between action tiles
constexpr int CardPadding = 16;   //!< Space inside a card

} // namespace Theme

#endif // UIKIT_THEME_H
