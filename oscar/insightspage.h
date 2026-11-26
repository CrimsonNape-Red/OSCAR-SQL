/* Insights page
 *
 * Patterns across many nights, from the Therapy Insights calculations in
 * SleepLib/insights.h: settings changes, pressure, data quality and usage.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef INSIGHTSPAGE_H
#define INSIGHTSPAGE_H

#include <QScrollArea>
#include <QString>

/*! \class InsightsPage
    \brief The Insights tab, opened from the Welcome page's Insights tile

    The calculations run only when the page is shown, and again only after the profile's data
    or the relevant preferences change: events by pressure loads every session's events in
    the range, so it must not run whenever Daily or Statistics refreshes. */
class InsightsPage : public QScrollArea
{
    Q_OBJECT
public:
    explicit InsightsPage(QWidget *parent = nullptr);

    //! \brief Rebuilds the page now if it is on screen, otherwise the next time it is shown,
    //! e.g. after a colour scheme change
    void invalidate();
    //! \brief Recalculates now if the page is on screen and its data changed, e.g. after an import
    void refreshIfShown();

protected:
    void showEvent(QShowEvent *event) override;

private:
    //! \brief Builds the page's content from the open profile
    void rebuild();
    //! \brief A cheap summary of what the page depends on; the page is rebuilt when it changes
    QString dataKey() const;

    QString m_builtFor;     //!< dataKey() of the content shown; empty when out of date
};

#endif // INSIGHTSPAGE_H
