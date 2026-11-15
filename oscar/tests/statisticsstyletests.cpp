/* Statistics page style Unit Tests
 *
 * Checks that the Statistics report keeps its own colours in light mode and that in dark
 * mode its light fills are replaced by theme colours, with sub-headings standing out more
 * than the tinted rows.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "statisticsstyletests.h"
#include "statisticsstyle.h"
#include "uikit/theme.h"

#include <QPalette>

namespace {

QPalette makePalette(const QColor &base, const QColor &text, const QColor &window)
{
    QPalette p;
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Highlight, QColor("#0078d4"));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, QColor(text.red() / 2 + base.red() / 2,
                                                 text.green() / 2 + base.green() / 2,
                                                 text.blue() / 2 + base.blue() / 2));
    p.setColor(QPalette::Mid, QColor("#a0a0a0"));
    return p;
}

QPalette lightPalette() { return makePalette(Qt::white, Qt::black, QColor("#f0f0f0")); }
QPalette darkPalette() { return makePalette(QColor("#2b2b2b"), Qt::white, QColor("#202020")); }

// The fills the report HTML uses (see statistics.cpp), in the forms it writes them
const QString kReport =
    "<tr bgcolor='#ffffff'><th>CPAP Statistics</th></tr>"
    "<tr bgcolor='#e0e0e0'><td>CPAP Usage</td></tr>"
    "<tr class=datarow bgcolor='#e8ffe8'><td>Days Not Used</td></tr>"
    "<tr class=datarow bgcolor='#FFFFFF'><td>Used Days</td></tr>"
    "<style>tr.datarow:nth-child(even) { background-color: #f8f8f8; }</style>"
    "<td onmouseover='ChangeColor(this, \"#eeeeee\");' onmouseout='ChangeColor(this, \"#ffffff\");'>"
    "<tr onmouseover='ChangeColor(this, \"#dddddd\");'>";

} // namespace

void StatisticsStyleTests::testLightModeKeepsReportColours()
{
    QCOMPARE(StatisticsStyle::screenHtml(kReport, lightPalette()), kReport);
    QCOMPARE(StatisticsStyle::recordsBoxStyleSheet(lightPalette()),
             QString("background: rgb(163, 190, 255)"));
}

void StatisticsStyleTests::testDarkModeReplacesLightFills()
{
    const QPalette dark = darkPalette();
    const QString html = StatisticsStyle::screenHtml(kReport, dark);
    for (const char *light : { "#ffffff", "#e0e0e0", "#e8ffe8", "#f8f8f8", "#eeeeee", "#dddddd" }) {
        QVERIFY2(!html.contains(light, Qt::CaseInsensitive), light);
    }
    const StatisticsStyle::Colours c = StatisticsStyle::screenColours(dark);
    QVERIFY(html.contains("bgcolor='" + Theme::css(c.page) + "'"));
    QVERIFY(html.contains("bgcolor='" + Theme::css(c.subheading) + "'"));
    QVERIFY(html.contains("bgcolor='" + Theme::css(c.rowTint) + "'"));
    QCOMPARE(c.page, Theme::color(Theme::Role::Surface, dark));
    QVERIFY(html.contains("<td>CPAP Usage</td>"));   // only colours change
}

void StatisticsStyleTests::testDarkSubheadingOutranksTintedRows()
{
    const StatisticsStyle::Colours c = StatisticsStyle::screenColours(darkPalette());
    const int tintStep = c.rowTint.lightness() - c.page.lightness();
    const int subStep = c.subheading.lightness() - c.page.lightness();
    QVERIFY2(tintStep > 0, "tinted rows must differ from the page");
    QVERIFY2(subStep >= 4 * tintStep, qPrintable(QString("sub-heading step %1, tint step %2")
                                                     .arg(subStep).arg(tintStep)));
    QVERIFY(c.subheading.lightness() < 128);    // white text stays readable
}

void StatisticsStyleTests::testDarkRecordsBoxIsDark()
{
    const QPalette dark = darkPalette();
    const QString sheet = StatisticsStyle::recordsBoxStyleSheet(dark);
    QVERIFY(!sheet.contains("163, 190, 255"));
    QVERIFY(sheet.contains("color: " + Theme::css(Theme::color(Theme::Role::Ink2, dark))));
    const int at = sheet.indexOf("background: ") + int(strlen("background: "));
    const QColor background(sheet.mid(at, 7));
    QVERIFY2(background.isValid() && background.lightness() < 100, qPrintable(sheet));
}
