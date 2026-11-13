/* Summary chart dotted lines
 *
 * The dotted reference lines an Overview graph can show across its bars (a percentile, the
 * middle value, a threshold...), and the text form in which the user's choice of lines is
 * kept in the profile preferences.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SUMMARYLINES_H
#define SUMMARYLINES_H

#include <QColor>
#include <QHash>
#include <QString>

//! \brief Which line this is on its graph; also its bit in the graph's saved mask
enum SummaryLineId { SL_Min, SL_Middle, SL_Perc, SL_Max, SL_Threshold, SL_Compliance };

/*! \struct SummaryLine
    \brief One dotted line a summary chart offers, with its value for the dates on screen */
struct SummaryLine {
    int id = SL_Middle;     //!< A SummaryLineId
    QString label;          //!< Name shown in the header and the menu, e.g. "95%"
    double value = 0;       //!< Where the line sits, in the graph's own units
    QColor color;           //!< Light-mode colour (translated for dark mode when drawn)
    bool enabled = false;   //!< Whether the line is drawn
};

/*! \namespace SummaryLines
    \brief Saving and loading which lines are switched on, per graph */
namespace SummaryLines {

/*! \brief The choices as text: "name=mask;name=mask", names in alphabetical order.
    Bit n of a mask is set when the line with SummaryLineId n is on. */
QString encode(const QHash<QString, quint32> &masks);

//! \brief The reverse of encode(). Entries that are not "name=number" are skipped.
QHash<QString, quint32> decode(const QString &text);

} // namespace SummaryLines

#endif // SUMMARYLINES_H
