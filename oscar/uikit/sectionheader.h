/* Section header component
 *
 * Small uppercase label that introduces a section of a redesigned page.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef UIKIT_SECTIONHEADER_H
#define UIKIT_SECTIONHEADER_H

#include <QLabel>

namespace UiKit {

/*! \class SectionHeader
    \brief Muted, bold, uppercase and letter-spaced label */
class SectionHeader : public QLabel
{
    Q_OBJECT

public:
    explicit SectionHeader(const QString &text = QString(), QWidget *parent = nullptr);

    //! \brief Sets the text; it is shown in upper case
    void setHeading(const QString &text);
};

} // namespace UiKit

#endif // UIKIT_SECTIONHEADER_H
