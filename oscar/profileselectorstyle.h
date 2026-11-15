/* Profile tab style
 *
 * Builds the Profile tab's stylesheet from the theme, so the page follows the system's
 * light or dark palette like the redesigned Welcome page and the Oximetry Wizard.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef PROFILESELECTORSTYLE_H
#define PROFILESELECTORSTYLE_H

#include <QPalette>
#include <QString>

namespace ProfileSelectorStyle {

//! \brief Returns the Profile tab's stylesheet with colours derived from \a palette
QString styleSheet(const QPalette &palette);

} // namespace ProfileSelectorStyle

#endif // PROFILESELECTORSTYLE_H
