/* Themed labels
 *
 * Helpers that create and colour plain text labels from theme roles, so pages built from
 * the redesign components never set label colours or fonts by hand.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef UIKIT_LABEL_H
#define UIKIT_LABEL_H

#include <QFont>
#include <QString>

#include "theme.h"

class QLabel;
class QWidget;

namespace UiKit {

//! \brief Creates a transparent label with \a font in the \a colour role; \a wrap enables word wrap
QLabel *makeLabel(const QString &text, const QFont &font, Theme::Role colour,
                  QWidget *parent = nullptr, bool wrap = false);

//! \brief Sets \a label's text colour to \a colour (keeps its background transparent)
void setLabelColour(QLabel *label, Theme::Role colour);

//! \brief Sets \a label's text colour to an explicit \a colour
void setLabelColour(QLabel *label, const QColor &colour);

//! \brief Creates a small rounded "chip" label: \a text in \a foreground on \a background
QLabel *makeChip(const QString &text, Theme::Role foreground, Theme::Role background,
                 QWidget *parent = nullptr);

//! \brief Creates a small square colour swatch for legends
QLabel *makeSwatch(Theme::Role colour, QWidget *parent = nullptr);

} // namespace UiKit

#endif // UIKIT_LABEL_H
