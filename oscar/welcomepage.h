/* Redesigned Welcome page
 *
 * The UI redesign pilot's Welcome page: action tiles, the most recent night, and metric
 * cards comparing that night with the 7- and 30-day averages.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef WELCOMEPAGE_H
#define WELCOMEPAGE_H

#include <QDate>
#include <QVector>

#include "welcomepagebase.h"

class QGridLayout;
class QScrollArea;
class QVBoxLayout;
struct MetricData;
struct OximetrySummary;
struct NightSummary;

namespace UiKit {
class ActionTile;
class MetricCard;
}

/*! \class WelcomePage
    \brief The redesigned Welcome page, shown when AppWideSetting::useNewWelcome() is set

    The header and tiles are built once. Everything that depends on the profile's data is
    rebuilt by refreshPage(). Tile, card and step grids re-flow with the page width. */
class WelcomePage : public WelcomePageBase
{
    Q_OBJECT

public:
    explicit WelcomePage(QWidget *parent = nullptr);

    //! \brief Rebuilds the page from the current profile's data
    void refreshPage() override;

protected:
    //! \brief Re-flows the grids when the scroll area's viewport changes width
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *buildHeader();
    QWidget *buildTiles();
    QWidget *buildCardWarning();
    QWidget *buildNightStrip(const NightSummary &night);
    QWidget *buildCardsSection(const NightSummary &night);
    QWidget *buildFirstRun();
    QWidget *buildNoCpapNote();
    UiKit::MetricCard *buildMetricCard(const MetricData &m);
    UiKit::MetricCard *buildOximetryCard(const OximetrySummary &oxi);

    //! \brief Re-flows the grids for the current width (only when a column count changes)
    void relayout(bool force = false);

    QScrollArea *m_scroll;
    QVBoxLayout *m_column;          //!< Page sections, top to bottom
    QWidget *m_dynamic = nullptr;   //!< Everything refreshPage() rebuilds

    QGridLayout *m_tileGrid;
    QVector<QWidget *> m_tiles;
    UiKit::ActionTile *m_dailyTile;
    UiKit::ActionTile *m_overviewTile;
    UiKit::ActionTile *m_statisticsTile;

    QGridLayout *m_cardGrid = nullptr;
    QVector<QWidget *> m_cards;
    QGridLayout *m_stepGrid = nullptr;
    QVector<QWidget *> m_steps;

    int m_tileColumns = 0;
    int m_cardColumns = 0;
    int m_stepColumns = 0;
};

#endif // WELCOMEPAGE_H
