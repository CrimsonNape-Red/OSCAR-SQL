/* Comparison helpers
 *
 * Compares a night's value with an average and decides whether the change is better,
 * worse, or too small to matter; combines two comparisons into a card tint.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef UIKIT_COMPARISON_H
#define UIKIT_COMPARISON_H

#include <QString>

namespace UiKit {

//! \brief Which direction of change is an improvement for a metric
enum class Better { Higher, Lower, Neutral };

//! \brief The meaning of a change
enum class Trend {
    Good,       //!< Changed in the better direction
    Bad,        //!< Changed in the worse direction
    Same,       //!< Within the no-change band
    Neutral,    //!< Changed, but direction carries no meaning for this metric
    None        //!< No comparison possible (no history, or a zero baseline)
};

//! \brief One night-versus-average comparison
struct Comparison {
    Trend trend = Trend::None;
    int percent = 0;    //!< Rounded percent change from the average; 0 when trend is None
};

//! \brief Rounded changes smaller than this many percent count as "same"
constexpr int NoChangeBand = 2;

/*! \brief Compares \a night with \a average
    \param nightsWithData How many nights the average was taken over; 0 means no history
    \param better Which direction is an improvement */
Comparison compare(double night, double average, int nightsWithData, Better better);

//! \brief Card tint from the two comparisons
enum class StatusTint { None, Good, Bad };

//! \brief Good or Bad only when both comparisons agree; otherwise None
StatusTint statusTint(const Comparison &c7, const Comparison &c30);

//! \brief Which way the night moved from the average, for the arrow beside it
enum class Direction { None, Up, Down };

//! \brief Up or Down for a real change (Good, Bad or Neutral); None for "same" or no comparison
Direction direction(const Comparison &c);

/*! \brief Text shown in place of an arrow: "same", "—" when there is no comparison, and
           empty for a real change (the arrow alone shows it) */
QString deltaText(const Comparison &c);

} // namespace UiKit

#endif // UIKIT_COMPARISON_H
