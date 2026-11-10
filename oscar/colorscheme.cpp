/* Colour scheme
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "colorscheme.h"

#include <QApplication>
#include <QDebug>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

#include "SleepLib/appsettings.h"
#include "uikit/theme.h"

namespace ColorScheme {

namespace {

#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
//! \brief A dark palette for the Fusion style, used where Qt can't switch the scheme itself
QPalette darkPalette()
{
    const QColor window(0x32, 0x32, 0x32);
    const QColor base(0x1e, 0x1e, 0x1e);
    const QColor button(0x3c, 0x3c, 0x3c);
    const QColor text(0xf0, 0xf0, 0xf0);
    const QColor disabledText(0x80, 0x80, 0x80);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, QColor(0x2a, 0x2a, 0x2a));
    p.setColor(QPalette::ToolTipBase, button);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, QColor(0xa0, 0xa0, 0xa0));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, QColor(0xff, 0x60, 0x60));
    p.setColor(QPalette::Light, QColor(0x55, 0x55, 0x55));
    p.setColor(QPalette::Midlight, QColor(0x48, 0x48, 0x48));
    p.setColor(QPalette::Mid, QColor(0x5a, 0x5a, 0x5a));
    p.setColor(QPalette::Dark, QColor(0x23, 0x23, 0x23));
    p.setColor(QPalette::Shadow, QColor(0x0a, 0x0a, 0x0a));
    p.setColor(QPalette::Highlight, QColor(0x3d, 0x7f, 0xd6));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(0x6c, 0xb6, 0xff));
    p.setColor(QPalette::LinkVisited, QColor(0xb0, 0x8c, 0xff));
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    p.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x50, 0x50, 0x50));
    return p;
}
#endif

} // namespace

bool canSwitchLive()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    return true;
#else
    return false;
#endif
}

void apply()
{
    if (!AppSetting) return;
    const ColorSchemeChoice choice = AppSetting->colorScheme();

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    Qt::ColorScheme scheme = Qt::ColorScheme::Unknown;     // Unknown follows the system
    if (choice == CSC_Light) scheme = Qt::ColorScheme::Light;
    else if (choice == CSC_Dark) scheme = Qt::ColorScheme::Dark;
    QGuiApplication::styleHints()->setColorScheme(scheme);
    qDebug() << "Colour scheme: choice" << int(choice) << "in effect" << int(QGuiApplication::styleHints()->colorScheme());
#else
    // Startup only. Leave the system's own colours alone when they already match the choice.
    const bool systemDark = Theme::isDark(QApplication::palette());
    const bool wantDark = choice == CSC_Dark || (choice == CSC_System && systemDark);
    if (choice == CSC_System || wantDark == systemDark) {
        qDebug() << "Colour scheme: following the system, dark =" << systemDark;
        return;
    }
    // Native styles ignore much of the palette, so use Fusion, which draws with it throughout.
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QApplication::setPalette(wantDark ? darkPalette() : QApplication::style()->standardPalette());
    qDebug() << "Colour scheme: Fusion with a" << (wantDark ? "dark" : "light") << "palette";
#endif
}

} // namespace ColorScheme
