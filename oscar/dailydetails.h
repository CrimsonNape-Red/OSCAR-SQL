/* Daily details figures
 *
 * The figures of the Daily page's Details tab, as values. The Classic page formats them as
 * HTML and the Modern page shows them as cards; both read them from here, so the two looks
 * always show the same digits. Every text is already translated and formatted as the Classic
 * page shows it.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DAILYDETAILS_H
#define DAILYDETAILS_H

#include <QColor>
#include <QDateTime>
#include <QPair>
#include <QString>
#include <QVector>

#include "SleepLib/machine_common.h"

class Day;
class Session;

/*! \brief The Modern Details tab's cards that can be closed; their open state is one bit each
    in AppSettings::dailyCardsOpen() */
enum DailyCard { DC_Device, DC_EventBreakdown, DC_Oximeter, DC_DeviceSettings, DC_SessionInfo };

/*! \namespace DailyDetails
    \brief Collects what the Daily page's Details tab shows for a day */
namespace DailyDetails {

//! \brief True when \a card is open in the stored \a bits
inline bool cardOpen(int bits, DailyCard card) { return (bits >> card) & 1; }

//! \brief \a bits with \a card open or closed, the other cards unchanged
inline int setCardOpen(int bits, DailyCard card, bool open)
{
    return open ? (bits | (1 << card)) : (bits & ~(1 << card));
}

//! \brief The kinds of day the Details tab draws differently
enum class DayKind {
    NoData,         //!< Nothing recorded on the day
    Cpap,           //!< An ordinary night with CPAP data
    ComplianceOnly, //!< A device that records usage only
    SessionsOff,    //!< CPAP sessions exist but all are switched off
    ZeroHours,      //!< CPAP data adding up to no time at all
    NonCpapOnly     //!< Oximetry or other non-CPAP data without CPAP data
};

/*! \brief The kind of day, from what Daily::Load knows about it
    \param hasCpap The day has a CPAP device
    \param cpapHours Hours of enabled CPAP sessions
    \param sessionCount Sessions of the day, switched off ones included
    \param brick The device records only compliance data
    \param hasOximeterOrPosition The day has an oximeter or a position sensor */
DayKind classify(bool hasCpap, double cpapHours, int sessionCount, bool brick, bool hasOximeterOrPosition);

//! \brief The night's index (AHI, or RDI by preference)
struct Headline {
    bool present = false;       //!< The day has CPAP hours
    QString name;               //!< "AHI" or "RDI"
    QString fullName;           //!< The index channel's full name
    double value = 0;           //!< Events per hour
    bool hasShares = false;     //!< The device scores hypopneas by mechanism
    double obstructive = 0;     //!< Obstructive share (OAHI)
    double central = 0;         //!< Central share (CAHI)
};

//! \brief Hours of use and the night's start and end
struct Usage {
    bool present = false;       //!< The day has any hours
    QDateTime start, end;       //!< First and last time of the day's data
    qint64 seconds = 0;         //!< Total CPAP time, whole seconds
    int cpapSessions = 0;       //!< Enabled CPAP sessions
};

//! \brief The day's CPAP device and its mode and pressures
struct Device {
    bool present = false;       //!< The day has a CPAP device
    QString brand, model;
    QString tooltip;            //!< "Model <number> - <serial>"
    QString mode;               //!< Day::getCPAPModeStr()
    QString pressure;           //!< Day::getPressureSettings() (may hold HTML)
    bool settingsMissing = false; //!< The day's settings are missing; yesterday's are shown
};

//! \brief One index of the Events list
struct EventRow {
    ChannelID code = 0;         //!< The channel, or 0 for a row that isn't one (Unmasked Sleep)
    QString name, tooltip;      //!< Full name; description (plain text)
    QString count;              //!< Number of events; empty for span channels and extra rows
    QString rate;               //!< Per hour, or a percentage of time for span channels
    QColor colour;              //!< The channel's colour (shown as a swatch)
    bool link = false;          //!< Clicking it opens the event in the Events tab
    double value = 0;           //!< The rate as a number (used for the event breakdown)
};

//! \brief One channel of the Statistics table
struct StatRow {
    QString label, tooltip;     //!< The tooltip may hold "<br/>"
    QString min, mid, perc, max;
};

//! \brief The Statistics table and the lines under it
struct Statistics {
    bool present = false;
    QString midName, percName, maxName;     //!< Column headings
    QVector<StatRow> rows;
    bool cpap = false;                      //!< The day has a CPAP device (the lines below the table)
    QVector<QPair<QString, QString>> extras; //!< Time in apnea, over the leak redline, ramp times
    bool summaryOnlyNote = false;           //!< Show the "only summary data" note
};

//! \brief A line of text in the Oximeter or Device Settings sections
struct TextRow {
    enum Style {
        Pair,       //!< Label and value in two columns
        Line,       //!< "Label: value" on one centred line
        Subheading  //!< The label alone, in italics
    };
    QString label, tooltip, value;
    Style style = Pair;
};

//! \brief The Oximeter Information section
struct Oximeter {
    bool present = false;
    QString heading;            //!< The oximeter, or "Recorded by" the CPAP that recorded it
    QVector<TextRow> rows;
    QString note;               //!< A closing note in italics (spot-check data)
};

//! \brief The Device Settings section
struct Settings {
    bool present = false;       //!< The day has enabled CPAP sessions
    bool loaderMissing = false; //!< The device's loader is not available: an error heading only
    bool assumed = false;       //!< Settings are carried over from earlier days
    QVector<TextRow> rows;      //!< Mode and pressures first, then the rest by label
};

//! \brief One session of the Session Information section
struct SessionRow {
    Session *session = nullptr;
    QString name;               //!< "<loader> Session #<id>"
    QString date, start, end, duration;
    bool on = true;             //!< The session is switched on
};

//! \brief The sessions of one kind of device
struct SessionGroup {
    MachineType type = MT_UNKNOWN;
    QString heading;            //!< "CPAP Sessions", "Oximetry Sessions", ...
    QVector<SessionRow> rows;
};

//! \brief The index and its obstructive and central shares
Headline headline(Day *day);
//! \brief Hours, sessions, start and end
Usage usage(Day *day);
//! \brief The CPAP device, its mode and its pressures
Device device(Day *day);
//! \brief The indices listed for the day, in the Classic order
QVector<EventRow> events(Day *day);
//! \brief The Statistics table; \a graphsEmpty is true when the day has no graph data
Statistics statistics(Day *day, bool graphsEmpty);
//! \brief The Oximeter Information section
Oximeter oximeter(Day *day);
//! \brief The Device Settings section
Settings settings(Day *day);
//! \brief The day's sessions grouped by kind of device
QVector<SessionGroup> sessions(Day *day);
//! \brief The total of the rates the event breakdown pie shows (above 0 means there is a pie)
double pieTotal(const QVector<EventRow> &events);

} // namespace DailyDetails

#endif // DAILYDETAILS_H
