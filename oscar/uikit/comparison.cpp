/* Comparison helpers
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "comparison.h"

#include <QCoreApplication>
#include <QtMath>
#include <cstdlib>

namespace UiKit {

Comparison compare(double night, double average, int nightsWithData, Better better)
{
    Comparison c;
    if (nightsWithData <= 0) return c;

    // A zero baseline has no percent change: equal is "same", anything else can't be compared
    if (average <= 0) {
        if (night <= 0) c.trend = Trend::Same;
        return c;
    }

    c.percent = qRound((night - average) / average * 100.0);
    if (std::abs(c.percent) < NoChangeBand) {
        c.trend = Trend::Same;
    } else if (better == Better::Neutral) {
        c.trend = Trend::Neutral;
    } else {
        bool higher = c.percent > 0;
        c.trend = (higher == (better == Better::Higher)) ? Trend::Good : Trend::Bad;
    }
    return c;
}

StatusTint statusTint(const Comparison &c7, const Comparison &c30)
{
    if (c7.trend == Trend::Good && c30.trend == Trend::Good) return StatusTint::Good;
    if (c7.trend == Trend::Bad && c30.trend == Trend::Bad) return StatusTint::Bad;
    return StatusTint::None;
}

Direction direction(const Comparison &c)
{
    if (c.trend == Trend::None || c.trend == Trend::Same) return Direction::None;
    return c.percent > 0 ? Direction::Up : Direction::Down;
}

QString deltaText(const Comparison &c)
{
    switch (c.trend) {
    case Trend::None:
        return QString::fromUtf8("—");
    case Trend::Same:
        return QCoreApplication::translate("UiKit", "same");
    default:
        return QString();
    }
}

} // namespace UiKit
