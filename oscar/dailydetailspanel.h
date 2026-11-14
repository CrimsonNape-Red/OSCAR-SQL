/* Daily details panel
 *
 * The Modern Daily page's Details tab: the day's figures as cards (the index with its recent
 * averages, the device, the events, the statistics, and cards the user can fold away), or a
 * callout for a day that is not an ordinary CPAP night. The tab scrolls as a whole; no card
 * scrolls by itself.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DAILYDETAILSPANEL_H
#define DAILYDETAILSPANEL_H

#include <QScrollArea>

#include "dailydetails.h"

class QVBoxLayout;
class Session;

/*! \class DailyDetailsPanel
    \brief Shows a DailyDetails::Page as cards in a scrolling column */
class DailyDetailsPanel : public QScrollArea
{
    Q_OBJECT
public:
    explicit DailyDetailsPanel(QWidget *parent = nullptr);

    /*! \brief Rebuilds the cards for \a page

        The scroll position is kept when \a page is for the same date as the one shown (a
        session switched off or on, a theme change); a new date starts at the top. */
    void setPage(const DailyDetails::Page &page);

    //! \brief Which collapsible cards are open (bits of DailyCard); applied at the next setPage()
    void setCardsOpen(int bits) { m_cardsOpen = bits; }
    int cardsOpen() const { return m_cardsOpen; }

    //! \brief The texts the cards show, one line each, for checks
    QStringList texts() const;

signals:
    //! \brief An event row was clicked: open that event in the Events tab
    void eventClicked(ChannelID code);
    //! \brief A session's switch was clicked
    void sessionToggled(Session *session);
    //! \brief The user opened or closed a card (\a card is a DailyCard)
    void cardToggled(int card, bool open);

private:
    //! \brief The AHI card: the index, its shares, the recent averages, hours, sessions, start and end
    QWidget *ahiCard(const DailyDetails::Page &page);
    //! \brief Hours, sessions, start and end, as one row
    QWidget *usageRow(const DailyDetails::Usage &usage, QWidget *parent);
    //! \brief The Device card (collapsible): brand and model, mode and pressures
    QWidget *deviceCard(const DailyDetails::Device &device);
    //! \brief One row per index, with a swatch of the channel's colour
    QWidget *eventsCard(const QVector<DailyDetails::EventRow> &events);
    //! \brief The Statistics table and the lines under it
    QWidget *statisticsCard(const DailyDetails::Statistics &statistics);

    //! \brief Wraps \a content in a collapsible card whose state is stored as \a card
    QWidget *collapsible(DailyCard card, const QString &title, QWidget *content);

    QWidget *m_body = nullptr;
    QVBoxLayout *m_column = nullptr;
    QDate m_date;
    int m_cardsOpen = 0;
};

#endif // DAILYDETAILSPANEL_H
