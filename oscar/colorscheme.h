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

#include <functional>

class QObject;

/*! \namespace ColorScheme
    \brief Applies the user's light/dark choice (AppSetting->colorScheme()) to the application

    Qt 6.8 and later can change the colour scheme while OSCAR runs, so the choice takes effect at
    once. Older Qt cannot, so there the choice is applied with the Fusion style and a light or dark
    palette: on Linux this is done again at once when the choice changes, elsewhere only at
    startup, and changing it means restarting OSCAR. */
namespace ColorScheme {

//! \brief True when the colour scheme can change while OSCAR runs (Qt 6.8+, or Linux with older Qt)
bool canSwitchLive();

/*! \brief Applies the saved choice to the application
 *
 *  Call once at startup after the preferences are loaded and the widget style is set, and again
 *  after the choice changes (only when canSwitchLive() is true). Pages and graphs see the result
 *  as a palette or theme change event.
 */
void apply();

/*! \brief True when dialogs drawn by the operating system appear in the scheme OSCAR is showing
 *
 *  Windows draws its own file dialog in the system's light or dark mode whatever scheme Qt has
 *  been given, so this is false there while OSCAR's choice differs from the system's; callers
 *  then use Qt's own dialog (see nativeDialogOption()). The system's mode is read at startup
 *  and whenever the choice is "System", so a change made in Windows while OSCAR runs with a
 *  forced choice is not seen until then. Always true on macOS, where Qt passes the choice on
 *  to the system, and on Linux, where the native dialogs have not been checked.
 */
bool nativeDialogsMatch();

/*! \brief Applies the choice again once the main window is on screen, then calls \a done
 *
 *  For macOS (GitLab #397). When OSCAR's choice differs from macOS's own scheme, the scheme
 *  apply() sets at startup, before any window is shown, reaches Qt's palette but not the parts
 *  the macOS style draws with native controls (tab bars, frames): those are drawn in macOS's
 *  scheme. A change made while the window is on screen reaches both. So this goes back to
 *  macOS's scheme and, a moment later, to the chosen one, as the user would with the moon/sun
 *  button.
 *
 *  Elsewhere, and when the choice doesn't differ from macOS's scheme, \a done is called at
 *  once and nothing else happens.
 *
 *  While this is being tested on Macs, the environment variable OSCAR_SCHEME_REAPPLY picks the
 *  method: "pause" (the default, described above), "sync" (the same two steps with no pause
 *  between them), "same" (only the chosen scheme, applied once more) or "off".
 *
 *  \param context  Object the delayed step belongs to; \a done is not called if it is destroyed first
 *  \param done     Called when the scheme has settled
 */
void reapplyAfterShow(QObject *context, const std::function<void()> &done);

} // namespace ColorScheme

#endif // COLORSCHEME_H
