/* Day labels for a date axis
 *
 * Works out which dates an Overview date axis labels and where, apart from drawing them, so
 * the axis under each graph and the one shared axis of the redesigned page follow one rule.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DAYLABELS_H
#define DAYLABELS_H

#include <QDate>
#include <QSet>
#include <QVector>

/*! \namespace DayLabels
    \brief Placement of the date labels along a day axis */
namespace DayLabels {

/*! \struct Label
    \brief One date label */
struct Label {
    QDate date;
    double x = 0;               //!< Left edge of the text, from the left of the plot
    bool bold = false;          //!< The day under the mouse, or one with a details popup open
    bool underlined = false;    //!< A day with a details popup open
};

/*! \brief Chooses the labels for \a days days starting at \a first
    \param barWidth   Width of one day
    \param labelWidth Width of a label's text
    \param gap        Least space between two labels
    \param hover      The day under the mouse (invalid if none); always labelled, in bold
    \param pinned     Days with a details popup open; labelled in bold and underlined unless
                      the hovered day's label would overlap
    \param fixedGrid  True: ordinary labels fall on the same dates however the axis is
                      scrolled. False: they are counted from \a first, as OSCAR 2.1 did.

    Ordinary labels never move to make room for a marked one: those that would overlap it are
    left out. Each label is centred under its day. */
QVector<Label> layout(const QDate &first, int days, double barWidth, double labelWidth, double gap,
                      const QDate &hover = QDate(), const QSet<QDate> &pinned = {}, bool fixedGrid = true);

} // namespace DayLabels

#endif // DAYLABELS_H
