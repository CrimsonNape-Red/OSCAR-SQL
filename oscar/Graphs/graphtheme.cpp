/* Graph theme
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "graphtheme.h"

#include <QGuiApplication>
#include <QHash>
#include <QPalette>
#include <cmath>

#include "uikit/theme.h"
#include "Graphs/glcommon.h"

namespace GraphTheme {

namespace {

int s_printDepth = 0;   //!< Number of live PrintScopes that are printing

const QColor kDarkBackground(0x25, 0x25, 0x25);    //!< What white becomes
const QColor kDarkInk(0xe6, 0xe6, 0xe6);           //!< What black becomes
constexpr double kMinContrast = 3.0;    //!< WCAG contrast a coloured line needs against the background
constexpr double kGreyChroma = 0.03;    //!< OKLab chroma below which a colour counts as black, white or grey
constexpr double kFillChroma = 0.7;     //!< Dark tints keep this share of their light tint's chroma

struct Lab { double L, a, b; };

double toLinear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
double toGamma(double c) { return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055; }

//! \brief sRGB to OKLab (Björn Ottosson's published matrices)
Lab toLab(const QColor &c)
{
    const double r = toLinear(c.redF()), g = toLinear(c.greenF()), b = toLinear(c.blueF());
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    return { 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
             1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
             0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s };
}

//! \brief OKLab to sRGB, clipped to the displayable range, with \a alpha
QColor fromLab(const Lab &lab, int alpha)
{
    const double l = std::pow(lab.L + 0.3963377774 * lab.a + 0.2158037573 * lab.b, 3);
    const double m = std::pow(lab.L - 0.1055613458 * lab.a - 0.0638541728 * lab.b, 3);
    const double s = std::pow(lab.L - 0.0894841775 * lab.a - 1.2914855480 * lab.b, 3);
    auto out = [](double v) { return qRound(qBound(0.0, toGamma(qBound(0.0, v, 1.0)), 1.0) * 255); };
    return QColor(out(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s),
                  out(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s),
                  out(-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s), alpha);
}

//! \brief WCAG relative luminance
double luminance(const QColor &c)
{
    return 0.2126 * toLinear(c.redF()) + 0.7152 * toLinear(c.greenF()) + 0.0722 * toLinear(c.blueF());
}

double contrast(const QColor &a, const QColor &b)
{
    const double la = luminance(a), lb = luminance(b);
    return (qMax(la, lb) + 0.05) / (qMin(la, lb) + 0.05);
}

//! \brief Maps lightness 1 (white) to the dark background and 0 (black) to the dark ink
Lab invert(Lab lab)
{
    static const double bgL = toLab(kDarkBackground).L;
    static const double inkL = toLab(kDarkInk).L;
    lab.L = bgL + (1.0 - lab.L) * (inkL - bgL);
    return lab;
}

QColor darkFill(const QColor &c)
{
    Lab lab = invert(toLab(c));
    lab.a *= kFillChroma;
    lab.b *= kFillChroma;
    return fromLab(lab, c.alpha());
}

QColor darkInk(const QColor &c)
{
    Lab lab = toLab(c);
    if (std::hypot(lab.a, lab.b) < kGreyChroma) {
        return fromLab(invert(lab), c.alpha());
    }
    // A colour: keep its hue and chroma, and lighten it only as far as it needs to show up
    QColor out = c;
    while (contrast(out, kDarkBackground) < kMinContrast && lab.L < 0.95) {
        lab.L = qMin(0.95, lab.L + 0.02);
        out = fromLab(lab, c.alpha());
    }
    return out;
}

//! \brief Looks \a c up in \a cache, computing it with \a fn the first time
QColor cached(QHash<QRgb, QRgb> &cache, const QColor &c, QColor (*fn)(const QColor &))
{
    const QRgb key = c.rgba();
    auto it = cache.constFind(key);
    if (it != cache.constEnd()) return QColor::fromRgba(it.value());
    const QColor result = fn(c);
    cache.insert(key, result.rgba());
    return result;
}

} // namespace

bool dark()
{
    return s_printDepth == 0 && Theme::isDark(QGuiApplication::palette());
}

QColor fill(const QColor &light)
{
    if (!light.isValid() || !dark()) return light;
    thread_local QHash<QRgb, QRgb> cache;
    return cached(cache, light, darkFill);
}

QColor ink(const QColor &light)
{
    if (!light.isValid() || !dark()) return light;
    thread_local QHash<QRgb, QRgb> cache;
    return cached(cache, light, darkInk);
}

QColor background()
{
    return dark() ? kDarkBackground : QColor(Qt::white);
}

QColor outline()
{
    return dark() ? QColor(0x58, 0x58, 0x58) : Theme::color(Theme::Role::Line);
}

QColor barShade(const QColor &color, float factor)
{
    if (!dark()) return brighten(color, factor);
    return color.darker(100 + int((factor - 1.0f) * 60));     // 2.0 -> 160%, 1.25 -> 115%
}

PrintScope::PrintScope(bool printing)
    : m_printing(printing)
{
    if (m_printing) ++s_printDepth;
}

PrintScope::~PrintScope()
{
    if (m_printing) --s_printDepth;
}

} // namespace GraphTheme
