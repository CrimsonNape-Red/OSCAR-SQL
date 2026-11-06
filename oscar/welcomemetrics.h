/* Welcome page metrics
 *
 * Gathers the numbers shown on the redesigned Welcome page: the most recent night, one
 * entry per metric card (the night's value and its 7- and 30-day averages) and the night's
 * oximetry summary. Every value comes from calculations OSCAR already uses in Daily,
 * Statistics and the original Welcome page.
 *
 * Also holds the catalog of cards and the profile's list of cards the user has hidden. To
 * add a card: add a MetricId, write its builder in welcomemetrics.cpp, and add one row to
 * the card table there. The card chooser and the stored list pick it up from the table.
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
#include <QSet>
#include <QString>
#include <QVector>

#include "uikit/comparison.h"
#include "uikit/eventbar.h"

//! \brief Which metric a card shows
enum class MetricId { TimeUsed, Ahi, Events, Pressure, Leak, LargeLeak, FlowLimit, Sessions, Oximetry };

//! \brief One card the page can show, as listed in the card chooser
struct CardInfo {
    MetricId id;
    QString key;        //!< Stable name stored in the profile; never translated or changed
    QString title;      //!< Translated name, as the card's title
};

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

    //! \brief Every card the page can show, in display order (the oximetry card last)
    static QVector<CardInfo> catalog();

    //! \brief The stored key of the card for \a id
    static QString cardKey(MetricId id);
    //! \brief The translated title of the card for \a id
    static QString cardTitle(MetricId id);

    /*! \brief The metric cards for \a night, in display order
        \param hidden Keys of cards the user has hidden; they are not computed
        Only metrics the device records get a card. The oximetry card comes from oximetry(). */
    static QVector<MetricData> cards(QDate night, const QSet<QString> &hidden = {});

    //! \brief Keys of the cards hidden in the current profile
    static QSet<QString> hiddenCards();
    //! \brief Stores \a hidden in the current profile and saves the profile's preferences
    static void setHiddenCards(const QSet<QString> &hidden);

    //! \brief Reads a stored list of card keys ("ahi,leak"); blanks and spaces are ignored
    static QSet<QString> parseCardKeys(const QString &text);
    //! \brief Stored form of \a keys: sorted and comma-separated
    static QString joinCardKeys(const QSet<QString> &keys);

    //! \brief The oximetry recorded on \a night
    static OximetrySummary oximetry(QDate night);
};

#endif // WELCOMEMETRICS_H
