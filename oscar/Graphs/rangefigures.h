/* Range figures
 *
 * The figures the Overview page shows for a range of dates: in the statistics boxes at the
 * top and as dotted lines on the graphs. Both take their numbers from here, so a box and a
 * line that share a name always show the same value. Every figure is calculated once over
 * all the data in the range; none is an average of nightly figures.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef RANGEFIGURES_H
#define RANGEFIGURES_H

#include <QDate>
#include <QString>
#include "SleepLib/machine_common.h"

/*! \namespace RangeFigures
    \brief Figures for the current profile between two dates (both included) */
namespace RangeFigures {

//! \brief AHI (or RDI when the profile is set to show RDI): events divided by hours of use
double ahi(const QDate &start, const QDate &end);

//! \brief Hours of use divided by the number of days the device was used
double usageAverage(const QDate &start, const QDate &end);

//! \brief Days the device was used
int daysUsed(const QDate &start, const QDate &end);

//! \brief Percentage of the days used that met the compliance hours; 0 when no days
double compliancePercent(const QDate &start, const QDate &end);

//! \brief Events of \a code per hour of use
double eventsPerHour(ChannelID code, MachineType mt, const QDate &start, const QDate &end);

//! \brief The profile's "middle" figure for \a code: median, weighted average or average
double middle(ChannelID code, MachineType mt, const QDate &start, const QDate &end);

//! \brief The profile's upper percentile (usually 95%) of \a code
double percentile(ChannelID code, MachineType mt, const QDate &start, const QDate &end);

//! \brief Name of the middle calculation the profile uses: "Median", "W-Avg" or "Average"
QString middleLabel();

//! \brief Name of the upper percentile, e.g. "95%"
QString percentileLabel();

//! \brief The pressure channel with data in the range (Pressure, else IPAP), or NoChannel
ChannelID pressureChannel(const QDate &start, const QDate &end);

} // namespace RangeFigures

#endif // RANGEFIGURES_H
