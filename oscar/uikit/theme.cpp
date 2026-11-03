/* Theme module
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "theme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QHash>

namespace Theme {

namespace {

//! \brief The light colour set, indexed by Role. Values from the Welcome page mockup.
const QColor kLight[int(Role::Count)] = {
    QColor(0xf2, 0xf5, 0xf8),   // Page
    QColor(0xff, 0xff, 0xff),   // Surface
    QColor(0xea, 0xf0, 0xf6),   // Surface2
    QColor(0x13, 0x21, 0x30),   // Ink
    QColor(0x48, 0x5a, 0x6c),   // Ink2
    QColor(0x75, 0x85, 0x97),   // Ink3
    QColor(0xd9, 0xe2, 0xea),   // Line
    QColor(0x00, 0x7a, 0xcc),   // Accent (OSCAR logo blue)
    QColor(0x00, 0x59, 0x9a),   // AccentInk
    QColor(0xe2, 0xf0, 0xfa),   // AccentSoft
    QColor(0xff, 0xff, 0xff),   // OnAccent
    QColor(0x17, 0x84, 0x4a),   // Good
    QColor(0xe2, 0xf3, 0xe9),   // GoodSoft
    QColor(0xee, 0xf8, 0xf1),   // GoodTint
    QColor(0xbd, 0xe2, 0xca),   // GoodLine
    QColor(0xc4, 0x37, 0x2c),   // Bad
    QColor(0xfb, 0xe8, 0xe5),   // BadSoft
    QColor(0xfd, 0xf0, 0xee),   // BadTint
    QColor(0xf0, 0xc5, 0xbf),   // BadLine
    QColor(0x65, 0x73, 0x84),   // Neutral
    QColor(0xed, 0xf1, 0xf4),   // NeutralSoft
    QColor(0x8a, 0x5a, 0x00),   // Warn
    QColor(0xfd, 0xf3, 0xdc),   // WarnSoft
    QColor(0xf0, 0xd9, 0xa2),   // WarnLine
    QColor(0xf4, 0xf0, 0xfb),   // GroupBreath
    QColor(0xdc, 0xd2, 0xf0),   // GroupBreathLine
    QColor(0xea, 0xf3, 0xfb),   // GroupMask
    QColor(0xcb, 0xe0, 0xf2),   // GroupMaskLine
    QColor(0xfa, 0xf3, 0xe6),   // GroupUsage
    QColor(0xe9, 0xd8, 0xb3),   // GroupUsageLine
    QColor(19, 33, 48, 18),     // Shadow (7% opacity)
};

//! \brief Bundled font files and the family Qt registered for each, filled by registerFonts()
struct BundledFont {
    const char *path;
    FontRole role;
    QFont::Weight weight;
    QString family;
};

BundledFont kFonts[] = {
    { ":/fonts/Lexend-Regular.ttf", FontRole::Display, QFont::Normal, QString() },
    { ":/fonts/Lexend-Medium.ttf", FontRole::Display, QFont::Medium, QString() },
    { ":/fonts/AtkinsonHyperlegible-Regular.ttf", FontRole::Body, QFont::Normal, QString() },
    { ":/fonts/AtkinsonHyperlegible-Bold.ttf", FontRole::Body, QFont::Bold, QString() },
};

bool fontsRegistered = false;

} // namespace

QColor color(Role role)
{
    int i = int(role);
    if (i < 0 || i >= int(Role::Count)) return QColor();
    return kLight[i];
}

QString css(Role role)
{
    QColor c = color(role);
    if (c.alpha() == 255) return c.name();
    return QString("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

void registerFonts()
{
    if (fontsRegistered) return;
    fontsRegistered = true;
    for (BundledFont &f : kFonts) {
        int id = QFontDatabase::addApplicationFont(f.path);
        if (id < 0) continue;
        QStringList families = QFontDatabase::applicationFontFamilies(id);
        if (!families.isEmpty()) f.family = families.first();
    }
}

QFont font(FontRole role, qreal pixelSize, QFont::Weight weight)
{
    // Prefer the file registered for this exact weight (some platforms register Lexend
    // Medium as its own family), then any file of the role, then the application font.
    QString family;
    for (const BundledFont &f : kFonts) {
        if (f.role != role || f.family.isEmpty()) continue;
        if (f.weight == weight) { family = f.family; break; }
        if (family.isEmpty()) family = f.family;
    }

    QFont font = QApplication::font();
    if (!family.isEmpty()) font.setFamily(family);
    font.setPixelSize(qMax(1, qRound(pixelSize)));
    font.setWeight(weight);
    return font;
}

} // namespace Theme
