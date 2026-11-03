/* Formatting helpers
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "formatting.h"

#include <QCoreApplication>
#include <QtMath>

namespace UiKit {

namespace {

QString hoursMinutes(int totalMinutes)
{
    return QCoreApplication::translate("UiKit", "%1h %2m")
        .arg(totalMinutes / 60).arg(totalMinutes % 60, 2, 10, QChar('0'));
}

} // namespace

QString formatHoursMinutes(double hours)
{
    return hoursMinutes(qMax(0, qRound(hours * 60.0)));
}

QString formatMinutesSeconds(double seconds)
{
    int s = qMax(0, qRound(seconds));
    if (s < 60) {
        return QCoreApplication::translate("UiKit", "%1s").arg(s);
    }
    if (s < 3600) {
        return QCoreApplication::translate("UiKit", "%1m %2s")
            .arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
    }
    return hoursMinutes(qRound(s / 60.0));
}

} // namespace UiKit
