/* Tooltip colors for the whole application
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tooltipcolors.h"

#include <QApplication>
#include <QDebug>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QStyle>
#include <QStyleHints>
#include <QStyleOption>
#include <QToolTip>

#include "SleepLib/appsettings.h"

namespace {

// OSCAR's own tooltip colors. The graphs' tooltips let a little of the graph show through.
const QColor kOscarBackground(255, 255, 128);
const QColor kOscarText(Qt::black);
const int kOscarGraphAlpha = 230;

QPalette s_themePalette;        //!< Qt's tooltip palette for the current theme, without our colors
QPalette s_appliedPalette;      //!< What apply() last set, to tell our colors from a reset by Qt
bool s_paletteApplied = false;  //!< Whether s_appliedPalette has been set
QColor s_darkBackground;        //!< The system's tooltip fill in dark mode; invalid if not in dark mode
QColor s_darkText;              //!< The system's tooltip text in dark mode
QColor s_graphBackground = kOscarBackground;
QColor s_graphText = kOscarText;

//! \brief True while Qt reports the forced Light scheme. High contrast themes report no scheme at all.
bool lightSchemeInEffect()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Light;
#else
    return false;
#endif
}

/*! \brief The color the current widget style fills a standard tooltip with.
 *
 *  QToolTip::palette() doesn't answer this. While polishing a tooltip, the native Windows styles
 *  replace its palette with the application's, and windowsvista paints a themed panel regardless of
 *  the palette. So let the style paint a panel off screen, from the palette it would really use, and
 *  read the result.
 */
QColor nativePanelColor()
{
    QPalette palette = s_themePalette;
#ifdef Q_OS_WIN
    if (!AppSetting->useFusionTheme()) {
        palette = QApplication::palette();
    }
#endif
    QImage image(64, 32, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QStyleOptionFrame option;
    option.rect = image.rect();
    option.palette = palette;
    option.state = QStyle::State_Enabled;

    QPainter painter(&image);
    QApplication::style()->drawPrimitive(QStyle::PE_PanelTipLabel, &option, &painter, nullptr);
    painter.end();

    const QColor painted = image.pixelColor(image.rect().center());
    return painted.alpha() == 255 ? painted : palette.color(QPalette::Inactive, QPalette::ToolTipBase);
}

} // namespace

namespace ToolTipColors {

void captureSystemScheme()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark) {
        const QPalette palette = QToolTip::palette();
        s_darkBackground = palette.color(QPalette::Inactive, QPalette::ToolTipBase);
        s_darkText = palette.color(QPalette::Inactive, QPalette::ToolTipText);
    }
#endif
}

void apply()
{
    // A palette that isn't the one we set is one Qt has just reset to the theme's.
    const QPalette current = QToolTip::palette();
    if (!s_paletteApplied || current != s_appliedPalette) {
        s_themePalette = current;
    }

    const bool useSystem = AppSetting && AppSetting->useSystemTooltipColors();

    // The colors to force on every tooltip. Left invalid, the tooltips stay as the system draws them.
    // The system's dark colors must be forced because Qt only offers light ones in the Light scheme.
    QColor background;
    QColor text;
    if (!useSystem) {
        background = kOscarBackground;
        text = kOscarText;
    } else if (s_darkBackground.isValid() && lightSchemeInEffect()) {
        background = s_darkBackground;
        text = s_darkText;
    }
    const bool forced = background.isValid();

    QPalette palette = s_themePalette;
    if (forced) {
        palette.setColor(QPalette::ToolTipBase, background);
        palette.setColor(QPalette::ToolTipText, text);
    }
    QToolTip::setPalette(palette);
    s_appliedPalette = palette;
    s_paletteApplied = true;

#ifdef Q_OS_WIN
    // Only a style sheet rule changes a tooltip under the native Windows styles (see nativePanelColor()).
    // The palette is enough elsewhere, and is preferred, as an application style sheet changes how other
    // widgets are drawn (Fusion shades tabs and buttons differently under it).
    // Qt 6 on Windows 11 also pads combo box drop-down items too much; the first rule constrains that.
    QString styleSheet = QStringLiteral("QComboBox QAbstractItemView::item { padding: 0px 0px; margin: 0px; }");
    if (forced) {
        styleSheet += QStringLiteral("QToolTip { color: %1; background-color: %2; border: 1px solid %1; padding: 2px; }")
                          .arg(text.name(), background.name());
    }
    if (qApp->styleSheet() != styleSheet) {
        qApp->setStyleSheet(styleSheet);  // repolishes every widget, so only when it has changed
    }
#endif

    if (!useSystem) {
        s_graphBackground = kOscarBackground;
        s_graphBackground.setAlpha(kOscarGraphAlpha);
        s_graphText = kOscarText;
    } else if (forced) {
        s_graphBackground = background;
        s_graphText = text;
    } else {
        s_graphBackground = nativePanelColor();
        s_graphText = s_themePalette.color(QPalette::Inactive, QPalette::ToolTipText);
    }
    qDebug().noquote() << "Tooltip colors:" << (useSystem ? "system" : "OSCAR") << "background"
                       << s_graphBackground.name() << "text" << s_graphText.name();
}

QColor graphBackground()
{
    return s_graphBackground;
}

QColor graphText()
{
    return s_graphText;
}

} // namespace ToolTipColors
