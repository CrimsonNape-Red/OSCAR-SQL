/* CSV oximetry import
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef CSVOXIMETRY_H
#define CSVOXIMETRY_H

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

class Machine;
class Session;

/*! \brief Oximetry from a CSV file: a date and time, SpO2 and pulse on each row.
 *
 *  Each row carries its own time, so any sampling rate works, including an irregular
 *  one. See Notes/loaders/CSV_OXIMETRY.md for the format:
 *
 *      DateTime,SpO2,Pulse
 *      2026-11-02 02:24:12,97,58
 *
 *  - The header is optional; with one, the columns can be in any order and the date
 *    and time can also be two columns ("Date", "Time").
 *  - Comma, semicolon or tab between fields.
 *  - Date and time as yyyy-MM-dd HH:mm[:ss[.zzz]], a "T" allowed between them, with an
 *    optional offset (+01:00, Z); without one it is local time. O2 Insight Pro's
 *    "10:59:17PM Jul 15, 2026" is read too, so its CSV export imports as it is.
 *  - Fields in double quotes may hold the separator.
 *  - An empty field, 0, or a value out of range is no reading at that moment.
 *  - Rows may be out of order. A pause longer than the gap limit starts a new recording.
 */
namespace CsvOximetry {

extern const char *const LOADER_NAME;   //!< the machines' loader name, "CSVOximetry"

struct Sample
{
    QDateTime time;   //!< local time
    int spo2 = 0;     //!< 0: no reading
    int pulse = 0;    //!< 0: no reading
};

struct Recording
{
    QVector<Sample> samples;   //!< in time order
    QDateTime start() const { return samples.isEmpty() ? QDateTime() : samples.first().time; }
    QDateTime end() const { return samples.isEmpty() ? QDateTime() : samples.last().time; }
};

struct Result
{
    QList<Recording> recordings;
    QStringList problems;     //!< e.g. "line 214: date and time not understood" (the first few)
    int rows = 0;             //!< data rows read
    int badRows = 0;          //!< rows that couldn't be read
    QString error;            //!< set if the file can't be used at all
    bool ok() const { return error.isEmpty(); }
};

//! Reads a file; recordings are split where readings pause for more than gapSeconds.
Result read(const QString &path, int gapSeconds = 300);
//! The same from text (for the unit tests).
Result parse(const QString &text, int gapSeconds = 300);

/*! The machine for this import: the generic "Oximeter (CSV)" one if name is empty,
 *  otherwise one of that name, created the first time. */
Machine *machine(const QString &name);

/*! A session of mach holding rec's readings at their own times, with the summaries
 *  OSCAR shows; not yet added to the machine. Null if rec has no readings. */
Session *buildSession(Machine *mach, const Recording &rec);

} // namespace CsvOximetry

#endif // CSVOXIMETRY_H
