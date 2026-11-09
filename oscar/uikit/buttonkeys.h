/* Button keys
 *
 * Which keys click the redesigned pages' buttons. Qt's buttons click on Space, but Enter
 * only clicks a dialog's default button; on a page of tiles users expect Enter to click
 * the focused one too.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef UIKIT_BUTTONKEYS_H
#define UIKIT_BUTTONKEYS_H

#include <QKeyEvent>

namespace UiKit {

/*! \brief True for Return or keypad Enter, not auto-repeated, so a held key clicks once.
    Space is left to QAbstractButton, which already clicks on it. */
inline bool isActivateKey(const QKeyEvent &event)
{
    return (event.key() == Qt::Key_Return || event.key() == Qt::Key_Enter) && !event.isAutoRepeat();
}

} // namespace UiKit

#endif // UIKIT_BUTTONKEYS_H
