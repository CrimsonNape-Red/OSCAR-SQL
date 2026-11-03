/* Welcome page metrics
 *
 * Gathers the numbers shown on the redesigned Welcome page: the most recent night, one
 * entry per metric card (the night's value, 7- and 30-day averages and a 30-night series)
 * and the night's oximetry summary. Every value comes from calculations OSCAR already
 * uses in Daily, Statistics and the original Welcome page.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef WELCOMEMETRICS_H
#define WELCOMEMETRICS_H

#include <QCoreApplication>
#include <QDate>
#include <QString>
#include <QVector>

#include "uikit/comparison.h"
#include "uikit/eventbar.h"

//! \brief Which metric a card shows
enum class MetricId { TimeUsed, Ahi, Events, Pressure, Leak, LargeLeak, FlowLimit, Sessions };

//! \brief The kind of number, used by the "By group" card colouring
enum class MetricGroup { Usage, Breathing, Mask };

//! \brief Everything one metric card shows
struct MetricData {
    MetricId id = MetricId::TimeUsed;
    MetricGroup group = MetricGroup::Usage;
    UiKit::Better better = UiKit::Better::Neutral;
    QString title;              //!< Card title, e.g. "AHI"
    QString value;              //!< The night's value as displayed
    QString unit;               //!< Unit shown after the value; may be empty
    QString sub;                //!< Line of detail under the value
    double night = 0;           //!< The night's value used for comparisons
    double avg7 = 0;            //!< Average over the 7 nights before
    double avg30 = 0;           //!< Average over the 30 nights before
    int nights7 = 0;            //!< Nights with CPAP data in the 7-night window
    int nights30 = 0;           //!< Nights with CPAP data in the 30-night window
    QVector<double> series;     //!< 31 values, night - 30 ... night; NaN where there is no data
    QVector<UiKit::EventBar::Part> events;   //!< Events card only: the breakdown
};

//! \brief The night the page describes
struct NightSummary {
    bool valid = false;         //!< False when the profile has no CPAP data
    QDate date;                 //!< The OSCAR day (noon to noon)
    QString deviceName;         //!< Brand and model
    QString pixmapPath;         //!< Device image
    int daysAgo = 0;            //!< Calendar days between the night's date and today
};

//! \brief The night's oximetry, from an oximeter or a CPAP device's own SpO2/pulse channels
struct OximetrySummary {
    bool present = false;       //!< False when the night has no oximetry
    double spo2Middle = 0;      //!< SpO2 average, as Daily's middle column
    double spo2Min = 0;         //!< Lowest SpO2
    double pulseMiddle = 0;     //!< Pulse average, as Daily's middle column
    bool hasSpo2 = false;
    bool hasPulse = false;
    QString middleLabel;        //!< Label of the middle calculation, e.g. "Avg" or "Med."
};

/*! \class WelcomeMetrics
    \brief Builds the Welcome page's data from the current profile (p_profile) */
class WelcomeMetrics
{
    Q_DECLARE_TR_FUNCTIONS(WelcomeMetrics)

public:
    //! \brief True when the profile holds any CPAP or oximetry data
    static bool hasAnyData();

    //! \brief The profile's last CPAP night, as the original Welcome page shows it
    static NightSummary lastNight();

    //! \brief The cards for \a night, only for metrics the device records, in display order
    static QVector<MetricData> cards(QDate night);

    //! \brief The oximetry recorded on \a night
    static OximetrySummary oximetry(QDate night);
};

#endif // WELCOMEMETRICS_H
