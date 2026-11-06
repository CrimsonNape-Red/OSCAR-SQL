/* Profile page style Unit Tests
 *
 * Checks that the Profile tab's stylesheet is built from the palette (so it follows light
 * and dark themes) and that the icons its buttons use are drawn.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "profilepagetests.h"
#include "profileselectorstyle.h"
#include "uikit/commandbutton.h"
#include "uikit/lineicons.h"
#include "uikit/theme.h"

#include <QImage>
#include <QPainter>
#include <QPalette>

using Theme::Role;

namespace {

QPalette makePalette(bool dark)
{
    QPalette p;
    p.setColor(QPalette::Window, dark ? QColor("#202020") : QColor("#f0f0f0"));
    p.setColor(QPalette::Base, dark ? QColor("#2b2b2b") : QColor("#ffffff"));
    p.setColor(QPalette::Text, dark ? QColor("#ffffff") : QColor("#000000"));
    p.setColor(QPalette::PlaceholderText, dark ? QColor("#a0a0a0") : QColor("#6e6e6e"));
    p.setColor(QPalette::Mid, dark ? QColor("#555555") : QColor("#a0a0a0"));
    p.setColor(QPalette::Highlight, QColor("#0078d4"));
    p.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    return p;
}

} // namespace

void ProfilePageTests::testStyleSheetUsesPalette()
{
    for (bool dark : { false, true }) {
        QPalette p = makePalette(dark);
        QString sheet = ProfileSelectorStyle::styleSheet(p);
        for (Role r : { Role::Surface, Role::Ink, Role::Line, Role::AccentSoft }) {
            QString c = Theme::css(Theme::color(r, p));
            QVERIFY2(sheet.contains(c), qPrintable(QString("role %1 colour %2 missing, dark %3")
                                                       .arg(int(r)).arg(c).arg(dark)));
        }
    }
    QVERIFY(ProfileSelectorStyle::styleSheet(makePalette(false))
            != ProfileSelectorStyle::styleSheet(makePalette(true)));
}

void ProfilePageTests::testStyleSheetHasNoFixedColours()
{
    QString sheet = ProfileSelectorStyle::styleSheet(makePalette(true)).toLower();
    for (const char *fixed : { "white", "black", "lightgrey", "#3a7fc2", "#f0f0f0", "#adadad" }) {
        QVERIFY2(!sheet.contains(fixed), fixed);
    }
}

void ProfilePageTests::testNewIconsDraw()
{
    using UiKit::LineIcon;
    for (LineIcon icon : { LineIcon::FolderOpen, LineIcon::Edit, LineIcon::UserAdd,
                           LineIcon::Delete, LineIcon::Clear }) {
        QImage img(48, 48, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        {
            QPainter p(&img);
            UiKit::paintLineIcon(p, icon, QRectF(0, 0, 48, 48), Qt::black);
        }
        int inked = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (qAlpha(img.pixel(x, y)) > 64) ++inked;
        QVERIFY2(inked > 60, qPrintable(QString("icon %1 drew %2 pixels").arg(int(icon)).arg(inked)));
    }
}

void ProfilePageTests::testTableHeaderIsShaded()
{
    QPalette palette;
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::Text, Qt::black);
    const QString shade = Theme::css(Theme::color(Theme::Role::Surface2, palette));
    const QString sheet = ProfileSelectorStyle::styleSheet(palette);
    QVERIFY(sheet.contains("QHeaderView::section { background: " + shade));
    QVERIFY(sheet.contains("QTableCornerButton::section { background: " + shade));
}

void ProfilePageTests::testCommandButtonWidthLetsSubtitleWrap()
{
    using UiKit::CommandButton;
    const int chrome = 62;  // padding, icon box and the gap beside it
    // A long subtitle gets a little over half its length, so it wraps onto about two lines
    QCOMPARE(CommandButton::preferredWidth(105, 267), chrome + 147);
    // The title is never wrapped just to narrow the button
    QCOMPARE(CommandButton::preferredWidth(200, 100), chrome + 200);
    // Limits for very short text and very long translations
    QCOMPARE(CommandButton::preferredWidth(10, 10), 160);
    QCOMPARE(CommandButton::preferredWidth(500, 100), 360);
}