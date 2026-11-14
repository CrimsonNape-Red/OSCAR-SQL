/* Collapsible card
 *
 * A card in theme colours with a header that opens and closes it. Used by the Modern Daily
 * page's Details tab for the sections the user can fold away.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef UIKIT_COLLAPSIBLECARD_H
#define UIKIT_COLLAPSIBLECARD_H

#include "panel.h"

class QVBoxLayout;

namespace UiKit {

/*! \class CollapsibleCard
    \brief A card whose header (title and chevron) opens and closes its contents

    The whole header is the click target. It takes keyboard focus, and Space or Enter toggle
    it. The card is as tall as its contents; closed, only the header shows. */
class CollapsibleCard : public Panel
{
    Q_OBJECT

public:
    explicit CollapsibleCard(const QString &title, QWidget *parent = nullptr);

    //! \brief Puts \a content under the header (the card takes ownership; replaces any earlier one)
    void setContent(QWidget *content);

    //! \brief Opens or closes the card without emitting toggled()
    void setOpen(bool open);
    bool isOpen() const { return m_open; }

    //! \brief The header button (for keyboard focus order and checks)
    QWidget *header() const;

    //! \brief Sets a tooltip on the header only
    void setHeaderToolTip(const QString &tip);

signals:
    //! \brief The user opened or closed the card
    void toggled(bool open);

private:
    class Header;
    Header *m_header;
    QVBoxLayout *m_layout;
    QWidget *m_content = nullptr;
    bool m_open = true;
};

} // namespace UiKit

#endif // UIKIT_COLLAPSIBLECARD_H
