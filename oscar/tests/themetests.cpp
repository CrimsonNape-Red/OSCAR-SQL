/* Theme module Unit Tests
 *
 * Checks that the theme takes its base colours from the palette, uses the Oximetry
 * Wizard's status colours for light and dark palettes, defines every colour role, and
 * sizes fonts relative to the application font.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "themetests.h"
#include "uikit/theme.h"

#include <QPalette>

using Theme::Role;

namespace {

//! \brief A palette with distinct values for every role the theme reads
QPalette makePalette(bool dark)
{
    QPalette p;
    p.setColor(QPalette::Window, dark ? QColor("#202326") : QColor("#e8e8e8"));
    p.setColor(QPalette::Base, dark ? QColor("#1a1c1e") : QColor("#fcfcfc"));
    p.setColor(QPalette::Text, dark ? QColor("#eeeeee") : QColor("#101010"));
    p.setColor(QPalette::WindowText, dark ? QColor("#e0e0e0") : QColor("#202020"));
    p.setColor(QPalette::PlaceholderText, dark ? QColor("#909090") : QColor("#707070"));
    p.setColor(QPalette::Mid, dark ? QColor("#505050") : QColor("#a0a0a0"));
    p.setColor(QPalette::Highlight, QColor("#3366cc"));
    p.setColor(QPalette::HighlightedText, QColor("#fefefe"));
    return p;
}

} // namespace

void ThemeTests::testFollowsPalette()
{
    QPalette p = makePalette(false);
    QCOMPARE(Theme::color(Role::Page, p), p.color(QPalette::Window));
    QCOMPARE(Theme::color(Role::Surface, p), p.color(QPalette::Base));
    QCOMPARE(Theme::color(Role::Ink, p), p.color(QPalette::Text));
    QCOMPARE(Theme::color(Role::Ink3, p), p.color(QPalette::PlaceholderText));
    QCOMPARE(Theme::color(Role::Line, p), p.color(QPalette::Mid));
    QCOMPARE(Theme::color(Role::Accent, p), p.color(QPalette::Highlight));
    QCOMPARE(Theme::color(Role::OnAccent, p), p.color(QPalette::HighlightedText));
}

void ThemeTests::testWizardStatusColours()
{
    QPalette light = makePalette(false);
    QPalette dark = makePalette(true);
    QVERIFY(!Theme::isDark(light));
    QVERIFY(Theme::isDark(dark));
    QCOMPARE(Theme::color(Role::Good, light).name(), QString("#1b6b43"));
    QCOMPARE(Theme::color(Role::Warn, light).name(), QString("#9a4a00"));
    QCOMPARE(Theme::color(Role::Good, dark).name(), QString("#6fcf97"));
    QCOMPARE(Theme::color(Role::Warn, dark).name(), QString("#f2b36b"));
}

void ThemeTests::testEveryRoleDefined()
{
    for (bool dark : { false, true }) {
        QPalette p = makePalette(dark);
        for (int r = 0; r < int(Role::Count); ++r) {
            QVERIFY2(Theme::color(Role(r), p).isValid(),
                     qPrintable(QString("role %1, dark %2").arg(r).arg(dark)));
        }
    }
}

void ThemeTests::testTintsSitBetweenSurfaceAndColour()
{
    // Tints are the status colour mixed into the card surface, so they follow the palette:
    // pale on a light surface, dark on a dark one.
    for (bool dark : { false, true }) {
        QPalette p = makePalette(dark);
        int surface = Theme::color(Role::Surface, p).lightness();
        int good = Theme::color(Role::Good, p).lightness();
        int tint = Theme::color(Role::GoodTint, p).lightness();
        QVERIFY2(tint != surface, dark ? "dark" : "light");
        QVERIFY2((tint - surface) * (good - surface) > 0, dark ? "dark" : "light");
        QVERIFY2(qAbs(tint - surface) < qAbs(good - surface), dark ? "dark" : "light");
    }
}

void ThemeTests::testCss()
{
    QCOMPARE(Theme::css(QColor(0x00, 0x7a, 0xcc)), QString("#007acc"));
    QCOMPARE(Theme::css(QColor(0, 0, 0, 18)), QString("rgba(0, 0, 0, 18)"));
}

void ThemeTests::testFontScalesApplicationFont()
{
    QFont base("Arial");
    base.setPointSizeF(10);
    QFont f = Theme::font(1.5, QFont::Normal, base);
    QCOMPARE(f.family(), QString("Arial"));
    QCOMPARE(f.pointSizeF(), 15.0);
    QCOMPARE(Theme::font(1.0, QFont::DemiBold, base).weight(), QFont::DemiBold);

    // A bold application font stays at least bold
    base.setBold(true);
    QCOMPARE(Theme::font(1.0, QFont::Normal, base).weight(), QFont::Bold);

    // A font sized in pixels is scaled in pixels
    QFont px("Arial");
    px.setPixelSize(16);
    QCOMPARE(Theme::font(0.5, QFont::Normal, px).pixelSize(), 8);
}

void ThemeTests::testRadiusMatchesWizard()
{
    QCOMPARE(Theme::Radius, 10);
}
