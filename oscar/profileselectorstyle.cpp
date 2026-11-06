/* Profile tab style
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "profileselectorstyle.h"

#include "uikit/theme.h"

namespace ProfileSelectorStyle {

QString styleSheet(const QPalette &palette)
{
    using Theme::Role;
    auto c = [&palette](Role role) { return Theme::css(Theme::color(role, palette)); };
    const QString radius = QString::number(Theme::Radius);

    // The buttons are UiKit::CommandButtons, which paint themselves, so no QPushButton rules.
    // The table sits in a rounded card (#tableCard) a few pixels inside its border, because
    // a view's square corners would otherwise cover the card's rounded ones.
    return QString(
        "#tableCard { background: %1; border: 1px solid %4; border-radius: %8px; }"
        "QTableView#profileView { background: %1; color: %3; border: none; outline: 0;"
        "    gridline-color: %2; selection-background-color: %6; selection-color: %3; }"
        "QTableView#profileView::item { border-bottom: 1px solid %2; padding: 0px 4px; }"
        "QTableView#profileView::item:selected { background: %6; color: %3; }"
        "QTableView#profileView QHeaderView::section { background: %2; color: %9;"
        "    font-weight: 600; border: none; border-bottom: 1px solid %4; padding: 5px 4px; }"
        "QTableView#profileView QTableCornerButton::section { background: %2; border: none; }"
        "QLineEdit#profileFilter { background: %1; color: %3; border: 1px solid %4;"
        "    border-radius: 6px; padding: 4px 8px; }"
        "QLineEdit#profileFilter:focus { border-color: %5; }"
        "QFrame#frame_2 { background: transparent; border: none; }"
        "QGroupBox#profileInfoGroupBox { background: %1; color: %3; border: 1px solid %4;"
        "    border-radius: %8px; margin-top: 0px; padding: 32px 6px 6px 6px; }"
        "QGroupBox#profileInfoGroupBox::title { subcontrol-origin: padding;"
        "    subcontrol-position: top left; left: 14px; top: 12px; color: %3; }"
        "QLabel#profileInfoLabel, QLabel#diskSpaceInfo, QLabel#filterLabel { color: %9; }"
        "QLabel#OSCAR_Label { color: %3; }"
        "QLabel#versionLabel { color: %7; }")
        .arg(c(Role::Surface), c(Role::Surface2), c(Role::Ink), c(Role::Line), c(Role::Accent),
             c(Role::AccentSoft), c(Role::Ink3), radius, c(Role::Ink2));
}

} // namespace ProfileSelectorStyle
