/* Daily calendar
 *
 * The Daily page's calendar. On the Modern page the selected day and today's date are marked
 * with an outline rather than a filled cell, so the colour, shading and lines that mark what
 * each day holds stay readable on them.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DAILYCALENDAR_H
#define DAILYCALENDAR_H

#include <QCalendarWidget>

/*! \class DailyCalendar
    \brief QCalendarWidget that can mark the selected day and today with outlines */
class DailyCalendar : public QCalendarWidget
{
    Q_OBJECT
public:
    explicit DailyCalendar(QWidget *parent = nullptr);

    /*! \brief Off (the default): paints exactly as QCalendarWidget. On: the selected day and
        today's date are drawn from their text formats as the other days are, with a 2 px
        accent outline on the selected day and a 1 px one on today's date. */
    void setOutlineSelection(bool on);
    bool outlineSelection() const { return m_outline; }

protected:
    void paintCell(QPainter *painter, const QRect &rect, QDate date) const override;

private:
    bool m_outline = false;
};

#endif // DAILYCALENDAR_H
