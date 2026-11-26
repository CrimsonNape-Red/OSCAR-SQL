/* Therapy Insights Calculations Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef INSIGHTS_H
#define INSIGHTS_H

#include <QDate>
#include <QList>
#include <QTime>
#include <QMap>
#include <QVector>

class Day;

//! \brief Calculations behind the Therapy Insights rows. UI-free; see
//! Notes/Design/2026-10-17-therapy-insights-prd.md for each definition.
namespace Insights {

//! \brief One night's scored events and usage hours.
struct NightCount {
    double events;
    double hours;
};

//! \brief Outcome of comparing a prescription period with the one before it.
enum class ChangeLabel { NoComparison, Within, Lower, Higher };

//! \brief Pooled AHI before and after a settings change, with a bootstrap interval.
struct RxComparison {
    bool valid = false;         //!< false when either period is too short
    double ahiBefore = 0;       //!< pooled: total events / total hours
    double ahiAfter = 0;
    double ciLow = 0;           //!< 95 % interval of ahiAfter - ahiBefore
    double ciHigh = 0;
    ChangeLabel label = ChangeLabel::NoComparison;
};

// Minimum nights per period before comparing. A fixed value for now: no preference exists
// (statistics.cpp has only the commented-out rxthresh = 5). Could become one.
const int kRxMinNights = 5;

// Smallest change in AHI (events/h) labelled Lower or Higher. With long periods the interval
// is narrow enough to call a tiny difference real; this keeps such rows "Within". A fixed
// value for now; could become a preference.
const double kMinAhiChange = 1.0;

//! \brief Compares the pooled AHI of \a after with \a before.
//!
//! The interval comes from 1,000 bootstrap resamples of nights with a fixed \a seed, so
//! output is deterministic and nights noisier than a Poisson count are handled. The label
//! is Lower or Higher only when the interval excludes 0 and the change is at least
//! kMinAhiChange; otherwise Within. No comparison when either period has fewer than
//! \a minNights nights.
RxComparison compareRxPeriods(const QVector<NightCount> &before,
                              const QVector<NightCount> &after,
                              int minNights = kRxMinNights, quint32 seed = 1);

// Covers device pressure quantization when comparing with the max setting. A fixed value
// for now; could become a preference if a device needs a different tolerance.
const double kAtMaxToleranceCmH2O = 0.2;

//! \brief Share of mask-on time spent at the device's maximum pressure setting.
struct TimeAtMax {
    bool valid = false;         //!< false unless some session was in an auto mode
    double pct = 0;             //!< % of mask-on time with pressure >= max setting - tolerance
};

//! \brief Time at max pressure over \a days, from the enabled CPAP sessions in APAP
//! (Pressure vs PressureMax) or auto-bilevel (IPAP vs IPAPHi) mode. Fixed-pressure modes
//! are skipped. Uses the stored summaries, so the events need not be loaded.
TimeAtMax timeAtMaxPressure(const QList<Day *> &days);

//! \brief Obstructive events and time at one pressure, in a 1 cmH2O bin.
struct PressureBin {
    int events = 0;
    double hours = 0;
};

//! \brief Obstructive events per hour by the pressure at their onset.
struct EventsAtPressure {
    bool valid = false;         //!< false unless some session was in an auto mode
    QMap<int, PressureBin> bins;    //!< keyed by pressure rounded to the nearest cmH2O
};

//! \brief Events at pressure over \a days, for sessions in an auto mode.
//!
//! Counts OA, H, OH and unclassified apneas at the pressure where they began: OSCAR stores
//! a flag at its end with the duration as its value, so onset = time - duration (a flag
//! with no duration is its own onset). Pressure is CPAP_Pressure for APAP and EPAP for
//! auto-bilevel. Loads each session's events if needed and releases them afterwards.
EventsAtPressure eventsAtPressure(const QList<Day *> &days);

// A bin needs this much time before its rate is ranked, so a pressure visited for a few
// minutes doesn't top the list. A fixed value for now; could become a preference.
const double kMinBinHours = 0.5;

//! \brief Pressures of the \a n bins with the most events per hour, highest first, among
//! bins with at least kMinBinHours of data.
QList<int> topPressureBins(const EventsAtPressure &eap, int n = 3);

// Data-quality thresholds. Fixed values for now; could become preferences.
const double kLargeLeakPct = 5.0;       //!< % of mask-on time in large leak
const int kMaxSessions = 3;             //!< more sessions than this is fragmented

//! \brief Reasons a night's AHI may be less trustworthy than it looks.
struct QualityFlags {
    bool leakKnown = false;     //!< the night has large-leak or leak data
    double largeLeakPct = 0;    //!< % of mask-on time in large leak, when leakKnown
    bool largeLeak = false;     //!< largeLeakPct > kLargeLeakPct
    bool shortNight = false;    //!< usage below the compliance-hours preference
    int sessions = 0;           //!< enabled CPAP sessions
    bool fragmented = false;    //!< sessions > kMaxSessions
    bool summaryOnly = false;   //!< some session has no detailed data

    bool any() const { return largeLeak || shortNight || fragmented || summaryOnly; }
};

//! \brief Data-quality flags for one night's enabled CPAP sessions.
//!
//! Large leak comes from LargeLeak spans when the device has ever reported them (OSCAR
//! also derives them from the leak redline at import), otherwise from the time Leak spends
//! above the redline. Each flag needs its data; a night without leak data has no leak flag.
QualityFlags qualityFlags(Day *day);

//! \brief The US Medicare adherence rule for the first 90 days of CPAP use.
struct MedicareCheck {
    bool valid = false;         //!< false without CPAP nights
    QDate firstNight;           //!< first CPAP night in the data, taken as first use
    bool complete = false;      //!< the data reaches the end of the 90 days
    bool met = false;
    int bestNights = 0;         //!< most nights of >= 4 h in any 30-night window
    QDate bestFirstNight;       //!< earliest window with bestNights, when met
};

//! \brief Checks for some window of 30 consecutive nights, starting within the first 90
//! days after first use, with >= 4 h of use on >= 21 nights. Always 4 h, whatever the
//! compliance-hours preference: it is a fixed rule. Nights without data count as unused.
MedicareCheck medicareFirst90Days(const QMap<QDate, Day *> &days);

//! \brief When the mask comes off, and how many sessions a night has.
struct UsagePattern {
    bool valid = false;         //!< false without CPAP nights
    QTime medianMaskOff;        //!< local clock time of the night's last CPAP session end
    int maskOffSpreadMinutes = 0;   //!< interquartile range of the mask-off time
    double medianSessions = 0;  //!< enabled CPAP sessions per night
};

//! \brief Mask-off time and sessions per night over \a days. Times are measured from the
//! day-split time, so nights around midnight compare correctly.
UsagePattern usagePattern(const QList<Day *> &days);

} // namespace Insights

#endif // INSIGHTS_H
