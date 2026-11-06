/* Theme module
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "theme.h"

#include <QApplication>
#include <QGuiApplication>

namespace Theme {

namespace {

//! \brief Returns \a a moved a fraction \a t (0-1) of the way towards \a b
QColor mix(const QColor &a, const QColor &b, qreal t)
{
    auto ch = [t](int x, int y) { return qRound(x + (y - x) * t); };
    return QColor(ch(a.red(), b.red()), ch(a.green(), b.green()), ch(a.blue(), b.blue()));
}

//! \brief A colour with one value for light palettes and one for dark palettes
struct Pair {
    QColor light;
    QColor dark;
    QColor pick(bool dark) const { return dark ? this->dark : light; }
};

// Status colours. Good and Warn are the Oximetry Wizard's values.
const Pair kGood { QColor(0x1b, 0x6b, 0x43), QColor(0x6f, 0xcf, 0x97) };
const Pair kWarn { QColor(0x9a, 0x4a, 0x00), QColor(0xf2, 0xb3, 0x6b) };
const Pair kBad { QColor(0xc4, 0x37, 0x2c), QColor(0xf2, 0x8b, 0x82) };

// "By group" hues, mixed into the surface for card tints and borders
const QColor kBreath(0x7a, 0x5b, 0xc7);
const QColor kMask(0x2f, 0x80, 0xc8);
const QColor kUsage(0xb5, 0x82, 0x1f);

// How far each derived colour moves from the surface towards its source colour
constexpr qreal kSoft = 0.14;   // icon boxes, chips
constexpr qreal kTint = 0.07;   // card backgrounds
constexpr qreal kLine = 0.30;   // card borders

} // namespace

bool isDark(const QPalette &palette)
{
    return palette.color(QPalette::Base).lightness() < 128;
}

QColor color(Role role, const QPalette &p)
{
    const bool dark = isDark(p);
    const QColor base = p.color(QPalette::Base);
    const QColor text = p.color(QPalette::Text);
    const QColor accent = p.color(QPalette::Highlight);
    const QColor good = kGood.pick(dark);
    const QColor bad = kBad.pick(dark);
    const QColor warn = kWarn.pick(dark);
    const QColor neutral = mix(text, base, 0.38);

    switch (role) {
    case Role::Page:            return p.color(QPalette::Window);
    case Role::Surface:         return base;
    case Role::Surface2:        return mix(base, text, dark ? 0.08 : 0.045);
    case Role::Ink:             return text;
    case Role::Ink2:            return mix(text, base, 0.28);
    case Role::Ink3:            return p.color(QPalette::PlaceholderText);
    case Role::Line:            return p.color(QPalette::Mid);
    case Role::Accent:          return accent;
    case Role::AccentInk:       return dark ? accent.lighter(140) : accent.darker(130);
    case Role::AccentSoft:      return mix(base, accent, dark ? 0.22 : 0.12);
    case Role::OnAccent:        return p.color(QPalette::HighlightedText);
    case Role::Good:            return good;
    case Role::GoodSoft:        return mix(base, good, kSoft);
    case Role::GoodTint:        return mix(base, good, kTint);
    case Role::GoodLine:        return mix(base, good, kLine);
    case Role::Bad:             return bad;
    case Role::BadSoft:         return mix(base, bad, kSoft);
    case Role::BadTint:         return mix(base, bad, kTint);
    case Role::BadLine:         return mix(base, bad, kLine);
    case Role::Neutral:         return neutral;
    case Role::NeutralSoft:     return mix(base, text, 0.06);
    case Role::Warn:            return warn;
    case Role::WarnSoft:        return mix(base, warn, 0.12);
    case Role::WarnLine:        return mix(base, warn, 0.35);
    case Role::GroupBreath:     return mix(base, kBreath, kTint);
    case Role::GroupBreathLine: return mix(base, kBreath, kLine);
    case Role::GroupMask:       return mix(base, kMask, kTint);
    case Role::GroupMaskLine:   return mix(base, kMask, kLine);
    case Role::GroupUsage:      return mix(base, kUsage, kTint);
    case Role::GroupUsageLine:  return mix(base, kUsage, kLine);
    case Role::Shadow:          return QColor(0, 0, 0, dark ? 90 : 20);
    case Role::Count:           break;
    }
    return QColor();
}

QColor color(Role role)
{
    return color(role, QGuiApplication::palette());
}

QString css(const QColor &c)
{
    if (c.alpha() == 255) return c.name();
    return QString("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

QString css(Role role)
{
    return css(color(role));
}

QFont font(qreal scale, QFont::Weight weight, const QFont &base)
{
    QFont f = base;
    if (base.pointSizeF() > 0) {
        f.setPointSizeF(base.pointSizeF() * scale);
    } else {
        f.setPixelSize(qMax(1, qRound(base.pixelSize() * scale)));
    }
    f.setWeight(qMax(base.weight(), weight));
    return f;
}

QFont font(qreal scale, QFont::Weight weight)
{
    return font(scale, weight, QApplication::font());
}

} // namespace Theme
