/* Section header component
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "sectionheader.h"

#include "label.h"
#include "theme.h"

namespace UiKit {

SectionHeader::SectionHeader(const QString &text, QWidget *parent)
    : QLabel(parent)
{
    QFont font = Theme::font(0.92, QFont::Bold);
    font.setLetterSpacing(QFont::PercentageSpacing, 108);
    setFont(font);
    setTextFormat(Qt::PlainText);
    setLabelColour(this, Theme::Role::Ink3);
    setHeading(text);
}

void SectionHeader::setHeading(const QString &text)
{
    setText(text.toUpper());
}

} // namespace UiKit
