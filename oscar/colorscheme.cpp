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
#include <QEvent>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

#include "SleepLib/appsettings.h"
#include "uikit/theme.h"

namespace ColorScheme {

namespace {

//! \brief True while OSCAR shows a different scheme from the system's; set by apply()
bool s_differsFromSystem = false;

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

/*! \brief Tells every widget that the theme changed, as Qt 6.8+ does when the scheme changes

    The pages rebuild in the new colours on QEvent::ThemeChange (see MainWindow::changeEvent()),
    which a style and palette change alone doesn't send. */
void sendThemeChange()
{
    const QWidgetList widgets = QApplication::allWidgets();
    for (QWidget *widget : widgets) {
        QEvent event(QEvent::ThemeChange);
        QApplication::sendEvent(widget, &event);
    }
}
#else
/*! \brief Gives light mode Fusion's standard light colours; dark mode keeps the desktop's own.

    For Linux, where Qt takes its colours from the desktop's GTK theme. Some light themes
    (e.g. Mint-L) give lists and panels the window's own colour and grey text, so OSCAR's cards
    and tables lose their edges. Called again whenever the scheme in effect changes. */
[[maybe_unused]] void applyLinuxLightPalette()
{
    QApplication::setPalette(QPalette());       // back to the desktop's colours, to judge and to keep when dark
    const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
    const bool dark = scheme == Qt::ColorScheme::Dark
                      || (scheme == Qt::ColorScheme::Unknown && Theme::isDark(QApplication::palette()));
    if (dark) return;
    QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion"));
    if (fusion) {
        QApplication::setPalette(fusion->standardPalette());
        delete fusion;
    }
}
#endif

} // namespace

bool canSwitchLive()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    return true;        // Qt changes the scheme itself
#elif defined(Q_OS_LINUX)
    return true;        // older Qt: apply() swaps the Fusion style and palette while OSCAR runs
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
    // Qt reports the system's own scheme only while no choice is forced: before the first
    // call here, and whenever the choice is "System".
    static Qt::ColorScheme systemScheme = QGuiApplication::styleHints()->colorScheme();
    QGuiApplication::styleHints()->setColorScheme(scheme);
    if (scheme == Qt::ColorScheme::Unknown) {
        systemScheme = QGuiApplication::styleHints()->colorScheme();
    }
    s_differsFromSystem = scheme != Qt::ColorScheme::Unknown
                          && systemScheme != Qt::ColorScheme::Unknown && scheme != systemScheme;
#ifdef Q_OS_LINUX
    static bool watching = false;
    if (!watching) {
        // Also follows the desktop when it changes between light and dark while OSCAR is open
        watching = true;
        QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                         qApp, [](Qt::ColorScheme) { applyLinuxLightPalette(); });
    }
    applyLinuxLightPalette();
#endif
    qDebug() << "Colour scheme: choice" << int(choice) << "in effect" << int(QGuiApplication::styleHints()->colorScheme());
#else
    // Qt can't change the scheme itself here, so OSCAR swaps the style and palette. The style
    // and colours the system gave at startup are kept, to judge "dark" by and to go back to.
    // Called again after startup only where canSwitchLive() says so (Linux).
    static bool captured = false;
    static QString systemStyle;
    static bool systemDark = false;
    static bool overridden = false;     // Fusion with OSCAR's own palette is in effect
    if (!captured) {
        captured = true;
        systemStyle = QApplication::style()->name();    // after the Fusion preference
        systemDark = Theme::isDark(QApplication::palette());
    }
    const bool wantDark = choice == CSC_Dark || (choice == CSC_System && systemDark);
    s_differsFromSystem = wantDark != systemDark;
    if (choice == CSC_System || wantDark == systemDark) {
        if (overridden) {
            overridden = false;
            QApplication::setStyle(QStyleFactory::create(systemStyle));
            QApplication::setPalette(QPalette());   // back to the system's colours
            sendThemeChange();
        }
        qDebug() << "Colour scheme: following the system, dark =" << systemDark;
        return;
    }
    // Native styles ignore much of the palette, so use Fusion, which draws with it throughout.
    const bool atStartup = !overridden && QApplication::allWidgets().isEmpty();
    overridden = true;
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QApplication::setPalette(wantDark ? darkPalette() : QApplication::style()->standardPalette());
    if (!atStartup) sendThemeChange();
    qDebug() << "Colour scheme: Fusion with a" << (wantDark ? "dark" : "light") << "palette";
#endif
}

bool nativeDialogsMatch()
{
#ifdef Q_OS_WIN
    return !s_differsFromSystem;
#else
    return true;
#endif
}

} // namespace ColorScheme
