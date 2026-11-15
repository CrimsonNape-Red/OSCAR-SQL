/* Callout
 *
 * A pale card with an icon, a bold title and a short text, for telling the user about the day
 * shown (no data, a device that records only compliance data, ...) or warning them.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef UIKIT_CALLOUT_H
#define UIKIT_CALLOUT_H

#include "panel.h"

class QLabel;

namespace UiKit {

/*! \class Callout
    \brief Information or warning card: icon, bold title, word-wrapped text

    Information uses the pale accent fill with the line colour as border; Warning uses the
    warning fill and border. Text is the theme's text colour on either. */
class Callout : public Panel
{
    Q_OBJECT

public:
    enum class Kind { Info, Warning };

    //! \param title Bold first line; may be empty
    //! \param text Wrapped beneath the title; may be empty
    Callout(Kind kind, const QString &title, const QString &text, QWidget *parent = nullptr);

    Kind kind() const { return m_kind; }
    QString title() const;
    QString text() const;

protected:
    void changeEvent(QEvent *event) override;

private:
    //! \brief Colours the labels from the theme
    void restyle();

    Kind m_kind;
    QLabel *m_title;
    QLabel *m_text;
};

} // namespace UiKit

#endif // UIKIT_CALLOUT_H
