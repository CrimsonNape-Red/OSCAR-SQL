/* Statistics page style
 *
 * The Statistics report is HTML built in fixed light colours, which printing uses as they
 * are (black on white). For the screen in dark mode those fills are swapped for theme
 * colours, so the page follows the system's light or dark palette like the redesigned
 * Welcome page and the Profile tab. Light mode keeps the report's own colours.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef STATISTICSSTYLE_H
#define STATISTICSSTYLE_H

#include <QColor>
#include <QPalette>
#include <QString>

namespace StatisticsStyle {

//! \brief The report's fills on screen for a palette
struct Colours {
    QColor page;        //!< Page, heading rows and plain data rows
    QColor subheading;  //!< Sub-heading rows (CPAP Usage, Therapy Efficacy, ...)
    QColor rowTint;     //!< The tinted data rows of the "alternating colours" preference
    QColor hover;       //!< Hover fills named in the report
};

//! \brief Returns the fills for \a palette: the report's own colours in light mode
Colours screenColours(const QPalette &palette);

/*! \brief Returns the report \a html as it should look on screen with \a palette.
    In light mode it is returned unchanged; in dark mode the report's light fills are
    replaced by screenColours(). The printed report never goes through this. */
QString screenHtml(const QString &html, const QPalette &palette);

//! \brief Returns the stylesheet of the records box (the Statistics side panel)
QString recordsBoxStyleSheet(const QPalette &palette);

} // namespace StatisticsStyle

#endif // STATISTICSSTYLE_H
