/* Welcome page base class
 *
 * Shared interface of the original and the redesigned Welcome pages, so MainWindow can
 * hold either one in its Welcome tab.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef WELCOMEPAGEBASE_H
#define WELCOMEPAGEBASE_H

#include <QWidget>

/*! \class WelcomePageBase
    \brief A page that can sit in MainWindow's Welcome tab

    MainWindow creates the page chosen in Preferences (see AppWideSetting::useNewWelcome())
    and calls refreshPage() whenever the profile's data changes. */
class WelcomePageBase : public QWidget
{
    Q_OBJECT

public:
    using QWidget::QWidget;

    //! \brief Rebuilds the page from the current profile's data
    virtual void refreshPage() = 0;
};

#endif // WELCOMEPAGEBASE_H
