/* Graph card
 *
 * The geometry of the redesigned graph frame (GraphLook::Card): each graph is an outlined card
 * with a header along the top holding the title, units, summary text, dotted-line names and a
 * menu button. These are plain functions of the card's rectangle so that drawing (gGraph) and
 * mouse handling (gGraphView) agree, and so they can be unit tested.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef GRAPHCARD_H
#define GRAPHCARD_H

#include <QPoint>
#include <QRect>
#include <QString>
#include <functional>

/*! \namespace GraphCard
    \brief Sizes and hit tests for a graph drawn as a card (see GraphLook::Card) */
namespace GraphCard {

constexpr int Radius = 10;          //!< Corner radius of the card's outline
constexpr int Padding = 10;         //!< Space between the card's sides and its contents
constexpr int TopPadding = 6;       //!< Space between the card's top edge and the header
constexpr int HeaderHeight = 26;    //!< Height of the header band
constexpr int Gap = 8;              //!< Space between one card and the next
constexpr int MenuButton = 22;      //!< Width and height of the menu button

//! \brief The header band inside \a card: full width less the side padding
QRect headerRect(const QRect &card);

//! \brief The menu button, at the right end of the header and centred in its height
QRect menuButtonRect(const QRect &card);

/*! \brief True when \a p is on the card's header, where a drag re-orders the graph, a
    double-click pins it and a right-click opens its menu. The band runs the full width of the
    card, from its top edge to the bottom of the header. */
bool inHeader(const QRect &card, const QPoint &p);

/*! \brief \a text cut to fit \a available pixels, ending in an ellipsis when cut. Empty when
    there is no room for more than the ellipsis.
    \param width Measures a string in pixels (QFontMetrics::horizontalAdvance when drawing) */
QString elideSummary(const QString &text, const std::function<int(const QString &)> &width, int available);

/*! \brief What to show beside \a title for a graph whose axis caption is \a units

    Captions are often the title again with the unit in brackets on a second line
    ("Usage\n(hours)"). The header shows only what the title doesn't already say: "hours". */
QString headerUnits(const QString &title, const QString &units);

//! \brief The two parts of a header's free space: text at the left, a legend at the right
struct HeaderSplit {
    QRect text;     //!< Where the graph's summary or cursor text goes
    QRect legend;   //!< Where the plot and dotted-line legend goes (empty when there is none)
};

/*! \brief Divides \a headerText (see gGraph::headerTextRect()) between text and a legend

    The legend takes \a legendWidth pixels from the right. When that would leave the text less
    than \a minText pixels, the legend is narrowed to what remains, and to nothing when even the
    text's minimum doesn't fit. An empty \a headerText gives two empty rectangles. */
HeaderSplit splitHeader(const QRect &headerText, int legendWidth, int minText);

} // namespace GraphCard

#endif // GRAPHCARD_H
