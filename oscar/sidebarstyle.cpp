/* Right sidebar style
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "sidebarstyle.h"

#include "uikit/theme.h"

namespace SidebarStyle {

namespace {

constexpr int kModernMargin = 8;    //!< Space around the Modern cards
constexpr int kPanelPadding = 6;    //!< Space between a panel's outline and its text

} // namespace

Colours colours(const QPalette &palette)
{
    using Theme::Role;
    auto c = [&palette](Role role) { return Theme::color(role, palette); };

    Colours out;
    out.background = c(Role::Page);
    out.panel = c(Role::Surface);
    out.border = c(Role::Line);
    out.text = c(Role::Ink);
    out.muted = c(Role::Ink3);
    out.link = c(Role::AccentInk);
    return out;
}

int panelMargin(bool modern)
{
    return modern ? kModernMargin : 0;
}

QString styleSheet(const QPalette &palette, bool modern)
{
    const Colours c = colours(palette);
    const int radius = modern ? Theme::Radius : 0;
    const int smallRadius = modern ? Theme::SmallRadius : 0;

    // The tab bar has no rule here, so the tabs are the platform's own, like the main tab bar
    QString sheet;
    sheet += QString("QWidget#bookmarksPage, QWidget#recordsPage { background: %1; border: none; }"
                     "QFrame#frame_2 { background: transparent; border: none; }")
                 .arg(Theme::css(c.background));
    sheet += QString("QTextBrowser { background: %1; color: %2; border: 1px solid %3;"
                     " border-radius: %4px; padding: %5px; }")
                 .arg(Theme::css(c.panel), Theme::css(c.text), Theme::css(c.border))
                 .arg(radius).arg(kPanelPadding);
    sheet += QString("QLineEdit#filterBookmarks { background: %1; color: %2; border: 1px solid %3;"
                     " border-radius: %4px; padding: 3px 6px; }")
                 .arg(Theme::css(c.panel), Theme::css(c.text), Theme::css(c.border)).arg(smallRadius);
    sheet += QString("QLineEdit#filterBookmarks:focus { border: 1px solid %1; }")
                 .arg(Theme::css(Theme::color(Theme::Role::Accent, palette)));
    sheet += QString("QToolButton#filterBookmarksButton { background: transparent; border: none;"
                     " color: %1; padding: 0px 6px; }")
                 .arg(Theme::css(c.muted));
    sheet += QString("QToolButton#filterBookmarksButton:hover { color: %1; }")
                 .arg(Theme::css(Theme::color(Theme::Role::Bad, palette)));
    sheet += QString("QToolButton#filterBookmarksButton:pressed { color: %1; }").arg(Theme::css(c.text));
    return sheet;
}

QString document(const QString &body, const QPalette &palette, const QFont &font, const QString &extraCss)
{
    const Colours c = colours(palette);
    return QString("<html><head><style type='text/css'>"
                   "p,a,td,li,body { font-family: '%1'; font-size: %2px; color: %3; }"
                   "i { color: %4; }"
                   "a, a:link, a:visited { color: %5; text-decoration: none; }"
                   "%6</style></head><body>%7</body></html>")
        .arg(font.family())
        .arg(font.pointSize() + 2)
        .arg(Theme::css(c.text), Theme::css(c.muted), Theme::css(c.link), extraCss, body);
}

} // namespace SidebarStyle
