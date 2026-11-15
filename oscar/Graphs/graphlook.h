/* Graph look
 *
 * The two ways a gGraphView can frame its graphs. Classic is the look every page had up to
 * OSCAR 2.1: a rotated title in a column at the left and graphs separated by a thin splitter.
 * Card is the redesigned look: each graph is an outlined card with its title and a menu button
 * in a header along the top.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef GRAPHLOOK_H
#define GRAPHLOOK_H

/*! \enum GraphLook
    \brief How a gGraphView frames its graphs */
enum class GraphLook {
    Classic,    //!< Rotated title column at the left, as in OSCAR 2.1
    Card        //!< Outlined card with a header along the top
};

#endif // GRAPHLOOK_H
