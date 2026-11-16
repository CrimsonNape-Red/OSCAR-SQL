/* Right sidebar style Unit Tests
 *
 * Checks that the right sidebar (Bookmarks and Records) takes its colours from the palette
 * in light and dark mode, that every text colour is readable on the background it is
 * drawn on, and that the Classic and Modern pages get different looks.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "sidebarstyletests.h"
#include "sidebarstyle.h"
#include "uikit/theme.h"

#include <QFont>
#include <QPalette>
#include <cmath>

namespace {

QPalette makePalette(bool dark)
{
    QPalette p;
    p.setColor(QPalette::Window, dark ? QColor("#202020") : QColor("#f0f0f0"));
    p.setColor(QPalette::Base, dark ? QColor("#2b2b2b") : QColor("#ffffff"));
    p.setColor(QPalette::Text, dark ? QColor("#ffffff") : QColor("#000000"));
    p.setColor(QPalette::WindowText, dark ? QColor("#ffffff") : QColor("#000000"));
    p.setColor(QPalette::PlaceholderText, dark ? QColor("#a0a0a0") : QColor("#6a6a6a"));
    p.setColor(QPalette::Mid, dark ? QColor("#505050") : QColor("#a0a0a0"));
    p.setColor(QPalette::Highlight, QColor("#0078d4"));
    p.setColor(QPalette::HighlightedText, Qt::white);
    return p;
}

//! \brief WCAG relative luminance of \a c
double luminance(const QColor &c)
{
    auto channel = [](double v) {
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
}

//! \brief WCAG contrast ratio between \a a and \a b (1 = none, 21 = black on white)
double contrast(const QColor &a, const QColor &b)
{
    const double la = luminance(a);
    const double lb = luminance(b);
    return (qMax(la, lb) + 0.05) / (qMin(la, lb) + 0.05);
}

} // namespace

void SidebarStyleTests::testFollowsLightAndDarkPalettes()
{
    const SidebarStyle::Colours light = SidebarStyle::colours(makePalette(false));
    const SidebarStyle::Colours dark = SidebarStyle::colours(makePalette(true));
    QVERIFY(light.panel.lightness() > 200);
    QVERIFY(light.background.lightness() > 200);
    QVERIFY(dark.panel.lightness() < 80);
    QVERIFY(dark.background.lightness() < 80);
    QCOMPARE(light.panel, makePalette(false).color(QPalette::Base));
    QCOMPARE(dark.background, makePalette(true).color(QPalette::Window));
}

void SidebarStyleTests::testTextIsReadableOnItsBackground()
{
    // 4.5:1 is the WCAG AA level for body text; notes in italics and the placeholder are
    // muted on purpose and held to the 3:1 level for large or secondary text
    for (bool dark : { false, true }) {
        const SidebarStyle::Colours c = SidebarStyle::colours(makePalette(dark));
        const QByteArray where = dark ? "dark" : "light";
        QVERIFY2(contrast(c.text, c.panel) >= 4.5, where + " text");
        QVERIFY2(contrast(c.link, c.panel) >= 4.5, where + " link");
        QVERIFY2(contrast(c.muted, c.panel) >= 3.0, where + " muted");
    }
}

void SidebarStyleTests::testModernAndClassicDiffer()
{
    const QPalette p = makePalette(false);
    QVERIFY(SidebarStyle::styleSheet(p, true) != SidebarStyle::styleSheet(p, false));
    QVERIFY(SidebarStyle::panelMargin(true) > SidebarStyle::panelMargin(false));
    // Modern panels are rounded cards; Classic panels are square
    QVERIFY(SidebarStyle::styleSheet(p, true).contains(QString("border-radius: %1px").arg(Theme::Radius)));
    QVERIFY(!SidebarStyle::styleSheet(p, false).contains(QString("border-radius: %1px").arg(Theme::Radius)));
    // The tabs are left to the platform style in both looks, like the main tab bar
    QVERIFY(!SidebarStyle::styleSheet(p, true).contains("QTabBar"));
    QVERIFY(!SidebarStyle::styleSheet(p, false).contains("QTabBar"));
}

void SidebarStyleTests::testStyleSheetHasNoFixedColours()
{
    // The blues the sidebar was drawn in whatever the palette
    for (bool dark : { false, true }) {
        for (bool modern : { false, true }) {
            const QString sheet = SidebarStyle::styleSheet(makePalette(dark), modern);
            for (const char *fixed : { "163, 190, 255", "#a3beff", "#8181b1", "#8393c3", "220, 232, 255" }) {
                QVERIFY2(!sheet.contains(fixed, Qt::CaseInsensitive), fixed);
            }
        }
    }
}

void SidebarStyleTests::testDocumentWrapsBodyWithThemeColours()
{
    const QPalette dark = makePalette(true);
    const SidebarStyle::Colours c = SidebarStyle::colours(dark);
    QFont font("Arial");
    font.setPointSize(10);
    // A body with a percent sign and a place marker, as record lines have
    const QString body = "<b>Worst Large Leaks</b><br><a href='daily=2026-01-02'>Leak: 12.50% %1</a>";
    const QString html = SidebarStyle::document(body, dark, font, "body { line-height: 0.95; }");
    QVERIFY(html.contains(body));
    QVERIFY(html.contains("font-family: 'Arial'"));
    QVERIFY(html.contains("font-size: 12px"));
    QVERIFY(html.contains("color: " + Theme::css(c.text)));
    QVERIFY(html.contains("a, a:link, a:visited { color: " + Theme::css(c.link)));
    QVERIFY(html.contains("i { color: " + Theme::css(c.muted)));
    QVERIFY(html.contains("line-height: 0.95"));
    QVERIFY(!html.contains("color: white"));
}
