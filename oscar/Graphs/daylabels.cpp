/* Day labels for a date axis
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "daylabels.h"
#include <algorithm>

#include <cmath>

namespace DayLabels {

QVector<Label> layout(const QDate &first, int days, double barWidth, double labelWidth, double gap,
                      const QDate &hover, const QSet<QDate> &pinned, bool fixedGrid)
{
    QVector<Label> labels;
    if (!first.isValid() || days <= 0 || barWidth <= 0) return labels;

    const double spacing = labelWidth + gap;
    const double centring = barWidth / 2.0 - labelWidth / 2.0;
    const auto xOf = [&](int i) { return i * barWidth + centring; };
    const auto inRange = [&](const QDate &d) { return d.isValid() && d >= first && first.daysTo(d) < days; };

    // Marked days first: the hovered day, then pinned days clear of it
    QVector<double> marked;
    if (inRange(hover)) marked.append(xOf(int(first.daysTo(hover))));
    // In date order, so which of two close pinned days keeps its label does not depend on the
    // order the set happens to list them in
    QVector<QDate> pinnedDays(pinned.begin(), pinned.end());
    std::sort(pinnedDays.begin(), pinnedDays.end());
    QVector<Label> pins;
    for (const QDate &d : pinnedDays) {
        if (!inRange(d) || d == hover) continue;
        const double x = xOf(int(first.daysTo(d)));
        bool clear = true;
        for (double m : marked) {
            if (std::abs(m - x) < spacing) { clear = false; break; }
        }
        if (!clear) continue;
        pins.append({ d, x, true, true });
        marked.append(x);
    }
    const auto nearMarked = [&](double x) {
        for (double m : marked) {
            if (std::abs(m - x) < spacing) return true;
        }
        return false;
    };

    // Ordinary labels every `step` days
    const int step = int(std::floor(spacing / barWidth)) + 1;
    const qint64 base = fixedGrid ? first.toJulianDay() : 0;
    for (int i = 0; i < days; ++i) {
        const QDate d = first.addDays(i);
        if (d == hover) {
            labels.append({ d, xOf(i), true, pinned.contains(d) });
        } else if ((base + i) % step == 0 && !nearMarked(xOf(i))) {
            labels.append({ d, xOf(i), false, false });
        }
    }
    labels += pins;
    return labels;
}

} // namespace DayLabels
