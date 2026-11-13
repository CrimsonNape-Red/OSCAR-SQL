/* Range figures
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "rangefigures.h"

#include "SleepLib/common.h"
#include "SleepLib/profiles.h"

namespace RangeFigures {

double ahi(const QDate &start, const QDate &end)
{
    if (!p_profile) return 0;
    const double hours = p_profile->calcHours(MT_CPAP, start, end);
    if (hours <= 0) return 0;
    double events = 0;
    for (const ChannelID code : ahiChannels) {
        events += p_profile->calcCount(code, MT_CPAP, start, end);
    }
    if (p_profile->general->calculateRDI()) {
        events += p_profile->calcCount(CPAP_RERA, MT_CPAP, start, end);
    }
    return events / hours;
}

int daysUsed(const QDate &start, const QDate &end)
{
    return p_profile ? p_profile->countDays(MT_CPAP, start, end) : 0;
}

double usageAverage(const QDate &start, const QDate &end)
{
    const int days = daysUsed(start, end);
    return days > 0 ? p_profile->calcHours(MT_CPAP, start, end) / days : 0;
}

double compliancePercent(const QDate &start, const QDate &end)
{
    const int days = daysUsed(start, end);
    return days > 0 ? 100.0 * p_profile->countCompliantDays(MT_CPAP, start, end) / days : 0;
}

double eventsPerHour(ChannelID code, MachineType mt, const QDate &start, const QDate &end)
{
    if (!p_profile) return 0;
    const double hours = p_profile->calcHours(mt, start, end);
    return hours > 0 ? p_profile->calcCount(code, mt, start, end) / hours : 0;
}

double middle(ChannelID code, MachineType mt, const QDate &start, const QDate &end)
{
    if (!p_profile) return 0;
    switch (p_profile->general->prefCalcMiddle()) {
    case 0: return p_profile->calcPercentile(code, 0.5f, mt, start, end);
    case 1: return p_profile->calcWavg(code, mt, start, end);
    default: return p_profile->calcAvg(code, mt, start, end);
    }
}

double percentile(ChannelID code, MachineType mt, const QDate &start, const QDate &end)
{
    if (!p_profile) return 0;
    return p_profile->calcPercentile(code, p_profile->general->prefCalcPercentile() / 100.0, mt, start, end);
}

QString middleLabel()
{
    if (!p_profile) return STR_TR_Median;
    switch (p_profile->general->prefCalcMiddle()) {
    case 0: return STR_TR_Median;
    case 1: return STR_TR_WAvg;
    default: return STR_TR_Average;
    }
}

QString percentileLabel()
{
    return QString("%1%").arg(p_profile ? p_profile->general->prefCalcPercentile() : 95.0, 0, 'f', 0);
}

ChannelID pressureChannel(const QDate &start, const QDate &end)
{
    if (!p_profile) return NoChannel;
    for (const ChannelID code : { CPAP_Pressure, CPAP_IPAP }) {
        if (p_profile->calcMax(code, MT_CPAP, start, end) > 0) return code;
    }
    return NoChannel;
}

} // namespace RangeFigures
