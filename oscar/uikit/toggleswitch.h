/* Toggle switch
 *
 * A small on/off switch, used on the Modern Daily page to switch a session on or off.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef UIKIT_TOGGLESWITCH_H
#define UIKIT_TOGGLESWITCH_H

#include <QAbstractButton>

namespace UiKit {

/*! \class ToggleSwitch
    \brief A checkable 28x16 switch: accent track when on, outlined in the "worse" line colour when off

    Space toggles it when it has focus, as with any button. Its size is fixed. */
class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT

public:
    explicit ToggleSwitch(QWidget *parent = nullptr);

    QSize sizeHint() const override { return QSize(28, 16); }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *event) override;
};

} // namespace UiKit

#endif // UIKIT_TOGGLESWITCH_H
