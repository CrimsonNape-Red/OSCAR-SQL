/* Formatting helpers
 *
 * Short duration formats used on the redesigned pages' cards.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef UIKIT_FORMATTING_H
#define UIKIT_FORMATTING_H

#include <QString>

namespace UiKit {

//! \brief Formats \a hours as "6h 18m" (minutes always two digits)
QString formatHoursMinutes(double hours);

//! \brief Formats \a seconds as "45s", "1m 12s", or "1h 05m" from an hour up
QString formatMinutesSeconds(double seconds);

} // namespace UiKit

#endif // UIKIT_FORMATTING_H
