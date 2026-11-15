/* Daily calendar
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "dailycalendar.h"

#include <QPainter>
#include <QTextCharFormat>

#include "Graphs/graphtheme.h"
#include "uikit/theme.h"

DailyCalendar::DailyCalendar(QWidget *parent)
    : QCalendarWidget(parent)
{
}

void DailyCalendar::setOutlineSelection(bool on)
{
    m_outline = on;
    update();
}

void DailyCalendar::paintCell(QPainter *painter, const QRect &rect, QDate date) const
{
    const bool selected = date == selectedDate();
    const bool today = date == QDate::currentDate();
    if (!m_outline || (!selected && !today)) {
        QCalendarWidget::paintCell(painter, rect, date);
        return;
    }

    painter->save();
    if (selected) {
        // Drawn as the other days are, from the weekday's format and the day's own, instead
        // of the filled selection
        QTextCharFormat format = weekdayTextFormat(Qt::DayOfWeek(date.dayOfWeek()));
        format.merge(dateTextFormat(date));
        const QBrush background = format.background();
        // The cells' background is the form's white, darkened in dark mode (Daily::applyTheme)
        painter->fillRect(rect, background.style() != Qt::NoBrush ? background : QBrush(GraphTheme::fill(Qt::white)));
        QFont font = format.font();
        font.setPointSizeF(this->font().pointSizeF());
        painter->setFont(font);
        const QBrush foreground = format.foreground();
        painter->setPen(foreground.style() != Qt::NoBrush ? foreground.color() : Theme::color(Theme::Role::Ink));
        painter->drawText(rect, Qt::AlignCenter, QString::number(date.day()));
    } else {
        QCalendarWidget::paintCell(painter, rect, date);
    }

    const qreal width = selected ? 2 : 1;
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(Theme::color(Theme::Role::Accent), width));
    painter->setBrush(Qt::NoBrush);
    const qreal inset = width / 2 + 1;
    painter->drawRoundedRect(QRectF(rect).adjusted(inset, inset, -inset, -inset), 4, 4);
    painter->restore();
}
