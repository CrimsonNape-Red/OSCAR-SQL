/* Graph theme
 *
 * Adapts the graphs' colours, which were all chosen for a white background, to dark mode.
 * In light mode, and whenever a graph is rendered for printing, every colour is returned
 * unchanged, so printed reports and the light-mode screen look exactly as before.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef GRAPHTHEME_H
#define GRAPHTHEME_H

#include <QColor>

/*! \namespace GraphTheme
    \brief Light-mode graph colours, translated for dark mode

    Two translations, both computed in the OKLab colour space so hues stay recognisable:
    - fill() for backgrounds, bands and highlight areas: lightness is inverted, so white becomes
      the dark graph background and pale tints become dark tints.
    - ink() for lines, markers and text: black and greys are inverted like fills (black text
      becomes light text); coloured lines keep their colour and are only lightened, just enough to
      stand out from the dark background. A user's channel colour from Preferences is treated the
      same way, so it is still recognisably their colour.
    */
namespace GraphTheme {

//! \brief True while graphs should be drawn in dark colours: dark palette, and not printing
bool dark();

//! \brief The light-mode colour for a background or band, translated for the current mode
QColor fill(const QColor &light);

//! \brief The light-mode colour for a line, marker or text, translated for the current mode
QColor ink(const QColor &light);

//! \brief The graphs' background colour in the current mode (white in light mode)
QColor background();

/*! \brief The outline of a graph card or statistics tile. In dark mode the palette's border
    colour can be as dark as the card itself, so a grey that shows on it is used instead. */
QColor outline();

/*! \brief The far end of a bar's shading: brighten(\a color, \a factor) in light mode, and a
    correspondingly darker shade in dark mode, where bars fading to white would glare */
QColor barShade(const QColor &color, float factor);

/*! \class PrintScope
    \brief Keeps graphs in their light colours while alive, if \a printing is true

    Printed reports go on white paper, so they keep the light-mode colours in dark mode too. */
class PrintScope
{
public:
    explicit PrintScope(bool printing);
    ~PrintScope();
    PrintScope(const PrintScope &) = delete;
    PrintScope &operator=(const PrintScope &) = delete;
private:
    bool m_printing;
};

} // namespace GraphTheme

#endif // GRAPHTHEME_H
