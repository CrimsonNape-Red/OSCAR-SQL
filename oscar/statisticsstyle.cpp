/* Statistics page style
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "statisticsstyle.h"

#include "uikit/theme.h"

namespace StatisticsStyle {

namespace {

// The fills the report HTML is built with (statistics.cpp), which printing keeps
const QString kWhite = QStringLiteral("#ffffff");       // page, headings, plain rows
const QString kSubheading = QStringLiteral("#e0e0e0");
const QString kRowTint = QStringLiteral("#e8ffe8");     // "alternating colours" rows
const QString kEvenRow = QStringLiteral("#f8f8f8");     // tr.datarow:nth-child(even) rule
const QString kHoverLight = QStringLiteral("#eeeeee");
const QString kHoverDark = QStringLiteral("#dddddd");

constexpr double kSubheadingMix = 0.23;  //!< Text mixed into the page for dark sub-headings

QColor mix(const QColor &a, const QColor &b, double t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

} // namespace

Colours screenColours(const QPalette &palette)
{
    using Theme::Role;
    if (!Theme::isDark(palette)) {
        return { QColor(kWhite), QColor(kSubheading), QColor(kRowTint), QColor(kHoverLight) };
    }
    // Sub-headings a clear step lighter than the page; the tinted rows only a faint green,
    // so the sub-headings stay the stronger of the two
    const QColor page = Theme::color(Role::Surface, palette);
    return { page,
             mix(page, Theme::color(Role::Ink, palette), kSubheadingMix),
             Theme::color(Role::GoodTint, palette),
             Theme::color(Role::Surface2, palette) };
}

QString screenHtml(const QString &html, const QPalette &palette)
{
    if (!Theme::isDark(palette)) return html;
    const Colours c = screenColours(palette);
    QString out = html;
    const QList<QPair<QString, QColor>> swaps = {
        { kWhite, c.page }, { kSubheading, c.subheading }, { kRowTint, c.rowTint },
        { kEvenRow, c.page }, { kHoverLight, c.hover }, { kHoverDark, c.hover },
    };
    for (const auto &swap : swaps) {
        out.replace(swap.first, Theme::css(swap.second), Qt::CaseInsensitive);
    }
    return out;
}

} // namespace StatisticsStyle
