/* Tooltip colors for the whole application
 *
 * Decides the colors of every tooltip, the standard Qt ones and the ones the graphs draw
 * themselves (gToolTip): OSCAR's black on yellow, or the system's when the user asks for that
 * in Preferences > Appearance.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef TOOLTIPCOLORS_H
#define TOOLTIPCOLORS_H

#include <QColor>

/*! \namespace ToolTipColors
    \brief The one place that knows what color a tooltip is.
    */
namespace ToolTipColors {

/*! \brief Remembers the operating system's dark tooltip colors, if it is in dark mode.
 *
 *  OSCAR runs in the Light color scheme, and once that is forced Qt no longer reports the system's
 *  dark mode. Call this after creating the QApplication and just before forcing the scheme.
 */
void captureSystemScheme();

/*! \brief Applies the tooltip colors to the application from the current setting and theme.
 *
 *  Safe to call again at any time, and needed after the setting, the widget style or the
 *  system theme changes (Qt resets the tooltip palette on a theme change). Until the preferences
 *  are loaded it applies OSCAR's own colors.
 */
void apply();

//! \brief The fill of a tooltip that the graphs draw. May be translucent.
QColor graphBackground();

//! \brief The text and border of a tooltip that the graphs draw.
QColor graphText();

} // namespace ToolTipColors

#endif // TOOLTIPCOLORS_H
