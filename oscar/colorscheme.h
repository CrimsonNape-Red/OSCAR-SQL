/* Colour scheme
 *
 * Makes the whole application light or dark as chosen in Preferences > Appearance (or with the
 * moon/sun button on the main tab bar), or lets it follow the operating system.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef COLORSCHEME_H
#define COLORSCHEME_H

/*! \namespace ColorScheme
    \brief Applies the user's light/dark choice (AppSetting->colorScheme()) to the application

    Qt 6.8 and later can change the colour scheme while OSCAR runs, so the choice takes effect at
    once. Older Qt cannot, so there the choice is applied once at startup, with the Fusion style
    and a light or dark palette, and changing it means restarting OSCAR. */
namespace ColorScheme {

//! \brief True when this build's Qt can change the colour scheme while OSCAR runs (Qt 6.8+)
bool canSwitchLive();

/*! \brief Applies the saved choice to the application
 *
 *  Call once at startup after the preferences are loaded and the widget style is set, and again
 *  after the choice changes (only when canSwitchLive() is true). Pages and graphs see the result
 *  as a palette or theme change event.
 */
void apply();

} // namespace ColorScheme

#endif // COLORSCHEME_H
