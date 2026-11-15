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
constexpr int kHeaderIcon = 20;     //!< Size of the bookmark and trophy icons in the headers

} // namespace

Colours colours(const QPalette &palette, bool modern)
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
    // Modern headers lie flat on the sidebar and only the open one is filled; Classic
    // headers are outlined bars, like the buttons and tabs of the Classic pages
    out.header = modern ? c(Role::Page) : c(Role::Surface2);
    out.headerText = c(Role::Ink2);
    out.headerOpen = c(Role::AccentSoft);
    out.headerOpenText = c(Role::Ink);  // accent-coloured text is too faint on the dark accent fill
    return out;
}

int panelMargin(bool modern)
{
    return modern ? kModernMargin : 0;
}

QString styleSheet(const QPalette &palette, bool modern)
{
    const Colours c = colours(palette, modern);
    const int radius = modern ? Theme::Radius : 0;
    const int smallRadius = modern ? Theme::SmallRadius : 0;
    const QString headerBorder = modern ? QStringLiteral("none")
                                        : QString("1px solid %1").arg(Theme::css(c.border));

    QString sheet;
    sheet += QString("QToolBox { background: %1; border: none; icon-size: %2px; }")
                 .arg(Theme::css(c.background)).arg(kHeaderIcon);
    sheet += QString("QToolBox::tab { background: %1; color: %2; border: %3; border-radius: %4px; }")
                 .arg(Theme::css(c.header), Theme::css(c.headerText), headerBorder).arg(smallRadius);
    sheet += QString("QToolBox::tab:selected { background: %1; color: %2; font: bold; }")
                 .arg(Theme::css(c.headerOpen), Theme::css(c.headerOpenText));
    // The pages sit in scroll areas the tool box makes; let the sidebar's background through
    sheet += QStringLiteral("QToolBox QScrollArea { background: transparent; border: none; }"
                            "QWidget#bookmarksPage, QWidget#recordsPage, QFrame#frame_2"
                            " { background: transparent; border: none; }");
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

QString document(const QString &body, const QPalette &palette, bool modern, const QFont &font,
                 const QString &extraCss)
{
    const Colours c = colours(palette, modern);
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
