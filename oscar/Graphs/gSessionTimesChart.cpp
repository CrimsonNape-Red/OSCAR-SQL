/* gSessionTimesChart Implementation
 *
 * Copyright (c) 2019-2025 The Oscar Team
 * Copyright (c) 2011-2018 Mark Watkins 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#define TEST_MACROS_ENABLEDoff
#include "test_macros.h"

#include <math.h>
#include <QLabel>
#include <QDateTime>
#include <QTimeZone>

#include "mainwindow.h"
#include "SleepLib/profiles.h"
#include "SleepLib/machine_common.h"
#include "gSessionTimesChart.h"

#include "gYAxis.h"
#include "graphtheme.h"
#include "overviewbars.h"

extern MainWindow * mainwin;

/// short SummaryCalcItem::midcalc;

void gSessionTimesChart::preCalc() {

    midcalc = p_profile->general->prefCalcMiddle();

    num_slices = 0;
    num_days = 0;
    total_length = 0;
    SummaryCalcItem  & calc = calcitems[0];

    calc.reset((idx_end - idx_start) * 6, midcalc);

    SummaryCalcItem  & calc1 = calcitems[1];

    calc1.reset(idx_end - idx_start, midcalc);

    SummaryCalcItem  & calc2 = calcitems[2];
    calc2.reset(idx_end - idx_start, midcalc);
}

void gSessionTimesChart::customCalc(Day *, QVector<SummaryChartSlice> & slices) {
    int size = slices.size();
    num_slices += size;

    SummaryCalcItem  & calc1 = calcitems[1];
    calc1.update(slices.size(), 1);

    EventDataType max = 0;

    for (auto & slice : slices) {
        slice.calc->update(slice.height, slice.height);

        max = qMax(slice.height, max);
    }
    SummaryCalcItem  & calc2 = calcitems[2];

    calc2.update(max, max);

    num_days++;
}

void gSessionTimesChart::afterDraw(QPainter & /*painter */, gGraph &graph, QRectF rect)
{
    if (totaldays == nousedays) return;

    SummaryCalcItem  & calc = calcitems[0]; // session length
    SummaryCalcItem  & calc1 = calcitems[1]; // number of sessions
    SummaryCalcItem  & calc2 = calcitems[2]; // number of sessions

    int midcalc = p_profile->general->prefCalcMiddle();

    float mid = 0, mid1 = 0, midlongest = 0;
    switch (midcalc) {
    case 0:
        if (calc.median_data.size() > 0) {
            mid = median(calc.median_data.begin(), calc.median_data.end());
            mid1 = median(calc1.median_data.begin(), calc1.median_data.end());
            midlongest = median(calc2.median_data.begin(), calc2.median_data.end());
        }
        break;
    case 1:
        mid = calc.wavg_sum / calc.divisor;
        mid1 = calc1.wavg_sum / calc1.divisor;
        midlongest = calc2.wavg_sum / calc2.divisor;
        break;
    case 2:
        mid = calc.avg_sum / calc.cnt;
        mid1 = calc1.avg_sum / calc1.cnt;
        midlongest = calc2.avg_sum / calc2.cnt;
        break;
    }



    QString txt = QObject::tr("Sessions: %1 / %2 / %3 Length: %4 / %5 / %6 Longest: %7 / %8 / %9")
            .arg(calc1.min, 0, 'f', 2).arg(mid1, 0, 'f', 2).arg(calc1.max, 0, 'f', 2)
            .arg(durationInHoursToHhMmSs(calc.min)).arg(durationInHoursToHhMmSs(mid)).arg(durationInHoursToHhMmSs(calc.max))
            .arg(durationInHoursToHhMmSs(calc2.min)).arg(durationInHoursToHhMmSs(midlongest)).arg(durationInHoursToHhMmSs(calc2.max));
    if (!graph.headerSummary(txt)) graph.renderText(txt, rect.left(), rect.top()-5*graph.printScaleY(), 0);
}

QList<DetailRow> gSessionTimesChart::detailRows(Day *, int idx)
{
    QList<DetailRow> rows;
    // This chart fills its day cache while drawing; a day that was clicked has been drawn
    if (!cache.contains(idx)) return rows;
    const QDateTime splitAt(dayindex.key(idx), split);
    for (const auto &s : cache[idx]) {
        if (s.height == 0) continue;
        const QDateTime start = splitAt.addSecs(qint64(s.value * 3600.0));
        const QDateTime end = start.addSecs(qint64(s.height * 3600.0));
        const bool maskOff = (s.color == QColor(Qt::black));
        rows.append({ durationInHoursToHhMmSs(s.height), maskOff ? QObject::tr("Mask off") : QObject::tr("Mask on"),
                      QObject::tr("%1 to %2").arg(QLocale().toString(start.time(), QLocale::ShortFormat),
                                                  QLocale().toString(end.time(), QLocale::ShortFormat)),
                      s.color, s.height });
    }
    return rows;
}

QString gSessionTimesChart::detailHeadline(Day *, int)
{
    return QObject::tr("Session Times");
}

QString gSessionTimesChart::detailSubtitle(Day *, int)
{
    return QString();
}

void gSessionTimesChart::paint(QPainter &painter, gGraph &graph, const QRegion &region)
{
    QRectF rect = region.boundingRect();

    paintFrame(painter, graph, rect);

    m_minx = graph.min_x;
    m_maxx = graph.max_x;

    QDateTime date2 = QDateTime::fromMSecsSinceEpoch(m_minx, QTimeZone::systemTimeZone());
    QDateTime enddate2 = QDateTime::fromMSecsSinceEpoch(m_maxx, QTimeZone::systemTimeZone());

    QDate date = date2.date();
    QDate enddate = enddate2.date();

    int days = ceil(double(m_maxx - m_minx) / 86400000.0);

    float barw = float(rect.width()) / float(days);
    // The original (2.1) look and the card look differ as in gSummaryChart::paint()
    const bool classic = !graph.graphView()->cardLook() || graph.printing();   // print is always the 2.1 look
    const OverviewBars::BarGeometry geo = classic ? OverviewBars::classicBarGeometry(barw)
                                                  : OverviewBars::barGeometry(barw);
    QVector<QRectF> outlines;       // classic look only
    const bool flatOnly = graph.printing() && AppSetting->monochromePrinting();
    graph.setHoverDate(QDate());
    int hlIdx = -1;
    bool tipShown = false;      // a session under the mouse has named itself

    // Card look, and nothing drawn under the mouse: the day still shows its date
    const QDate chartStart = date;
    const auto emptyHover = [&]() {
        if (classic) return false;
        return hoverEmptyDay(painter, graph, rect, graph.graphView()->currentMousePos(), chartStart, days);
    };
    // No day of this graph is under the mouse: the day pointed at in another graph is still marked
    const auto notHoveredHere = [&]() { notHovered(painter, graph, rect, days); };

    QDateTime splittime;

    auto it = dayindex.find(date);
    int idx=0;

    if (it == dayindex.end()) {
        it = dayindex.begin();
    } else {
        idx = it.value();
    }

    // Determine how many days after the first day of the chart that this data is to begin
    int numDaysOffset = 0;
    if (firstday > date) {  // date = beginning date of chart; firstday = beginning date of data
        numDaysOffset = date.daysTo(firstday);
    }

    // Determine how many days before the last day of the chart that this data is to end
    int numDaysAfter = 0;
    if (enddate > lastday) {  // enddate = last date of chart; lastday = last date of data
        numDaysAfter = lastday.daysTo(enddate);
    }
    if (numDaysAfter > days) {      // Nothing to do if this data is off the left edge of the chart
        if (!emptyHover()) notHoveredHere();
        return;
    }

    float lastx1 = rect.left();
    lastx1 += numDaysOffset * barw;

    auto ite = dayindex.find(enddate);
    int idx_end = daylist.size()-1;
    if (ite != dayindex.end()) {
        idx_end = ite.value();
    }

    // Nothing is under the mouse on a printed page
    QPoint mouse = graph.printing() ? QPoint(-1, -1) : graph.graphView()->currentMousePos();
    // The day slot under the mouse, counted from the left of the plot; -1 when the mouse is not
    // over this plot (as in gSummaryChart::paint())
    const int mouseSlot = rect.contains(mouse) ? OverviewBars::daySlot(rect.left(), rect.width(), days, mouse.x()) : -1;

    if (daylist.size() == 0) {
        if (!emptyHover()) notHoveredHere();
        return;
    }

    auto it2 = it;

    /////////////////////////////////////////////////////////////////////
    /// Calculate Graph Peaks
    /////////////////////////////////////////////////////////////////////
    peak_value = 0;
    min_value = 999;
    auto it_end = dayindex.end();

    float right_edge = (rect.left()+rect.width()+1);
    for (int i=idx; (i <= idx_end) && (it2 != it_end); ++i, ++it2, lastx1 += barw) {
        Day * day = daylist.at(i);

        if ((lastx1 + barw) > right_edge)
            break;

        if (!day) {
            continue;
        }

        auto cit = cache.find(i);

        if (cit == cache.end()) {
            day->OpenSummary();
            date = it2.key();
            splittime = QDateTime(date, split);

            QVector<SummaryChartSlice> & slices = cache[i];

            bool haveoxi = day->hasMachine(MT_OXIMETER);

            QColor goodcolor = haveoxi ? QColor(128,255,196) : QColor(64,128,255);

            QString datestr = date.toString(QLocale::system().dateFormat(QLocale::ShortFormat));

            for (const auto & sess : day->sessions) {
                if (!sess->enabled() || (sess->type() != m_machtype)) continue;

                // Look at mask on/off slices...
                if (sess->m_slices.size() > 0) {
                    // segments
                    for (const auto & slice : sess->m_slices) {
                        QDateTime st = QDateTime::fromMSecsSinceEpoch(slice.start, QTimeZone::systemTimeZone());

                        float s1 = float(splittime.secsTo(st)) / 3600.0;

                        float s2 = double(slice.end - slice.start) / 3600000.0;
                        float s2_display = double(slice.end - slice.start) / 1000.0;

                        QColor col = (slice.status == MaskOn) ? goodcolor : Qt::black;
                        QString txt = QObject::tr("%1\nLength: %3\nStart: %2\n").arg(datestr).arg(st.time().toString("hh:mm:ss")).arg(durationInSecondsToHhMmSs(s2_display));

                        txt += (slice.status == MaskOn) ? QObject::tr("Mask On") : QObject::tr("Mask Off");
                        slices.append(SummaryChartSlice(&calcitems[0], s1, s2, txt, col));
                    }
                } else {
                    // otherwise just show session duration
                    qint64 sf = sess->first();
                    QDateTime st = QDateTime::fromMSecsSinceEpoch(sf, QTimeZone::systemTimeZone());
                    float s1 = float(splittime.secsTo(st)) / 3600.0;

                    float s2 = sess->hours();

                    QString txt = QObject::tr("%1\nLength: %3\nStart: %2").arg(datestr).arg(st.time().toString("hh:mm:ss")).arg(durationInHoursToHhMmSs(s2));

                    slices.append(SummaryChartSlice(&calcitems[0], s1, s2, txt, goodcolor));
                }
            }

            cit = cache.find(i);
        }

        if (cit != cache.end()) {
            float peak = 0, base = 999;

            for (const auto & slice : cit.value()) {
                float s1 = slice.value;
                float s2 = slice.height;

                peak = qMax(peak, s1+s2);
                base = qMin(base, s1);
            }
            peak_value = qMax(peak_value, peak);
            min_value = qMin(min_value, base);

        }
    }
    m_miny = (min_value < 999) ? floor(min_value) : 0;
    m_maxy = ceil(peak_value);

    /////////////////////////////////////////////////////////////////////
    /// Y-Axis scaling
    /////////////////////////////////////////////////////////////////////

    EventDataType miny;
    EventDataType maxy;

    graph.roundY(miny, maxy);
    float ymult = float(rect.height()) / (maxy-miny);


    preCalc();
    totaldays = 0;
    nousedays = 0;

    lastx1 = rect.left();
    lastx1 += numDaysOffset * barw;

    /////////////////////////////////////////////////////////////////////
    /// Main Loop scaling
    /////////////////////////////////////////////////////////////////////
    do {
        Day * day = daylist.at(idx);

        if ((lastx1 + barw) > right_edge)
            break;

        totaldays++;

        if (!day)
        {
            lastx1 += barw;
            nousedays++;
         //   it++;
            continue;
        }

        auto cit = cache.find(idx);

        float x1 = lastx1 + barw;


        QRectF rec2(lastx1, rect.top(), barw, rect.height());
        if (mouseSlot >= 0 && qRound((lastx1 - rect.left()) / barw) == mouseSlot) {
            hlIdx = idx;
            if (classic) painter.fillRect(rec2, QBrush(QColor(255, 0, 0, 64)));
        }

        if (cit != cache.end()) {
            QVector<SummaryChartSlice> & slices = cit.value();

            customCalc(day, slices);

            for (const auto & slice : slices) {
                float s1 = slice.value - miny;
                float s2 = slice.height;

                float y1 = (s1 * ymult);
                float y2 = (s2 * ymult);

                QColor col = GraphTheme::ink(slice.color);

                const QRectF rec = QRectF(lastx1 + geo.offset, rect.bottom() - y1 - y2, geo.width, y2).intersected(rect);
                if (classic) {
                    // The session under the mouse turns yellow and names itself in a tooltip
                    if (rec.contains(mouse)) {
                        col = Qt::yellow;
                        graph.ToolTip(slice.name, mouse.x() + 20, mouse.y(), TT_AlignBottomLeft);
                    }
                    OverviewBars::paintClassicSegment(painter, rec, col, barw);
                    if (barw > 3) outlines.append(rec);
                } else {
                    // Each session is a piece of its own, with its own rounded top; the one
                    // under the mouse names itself in a tooltip
                    if (rec.contains(mouse)) {
                        showHoverTip(graph, slice.name, mouse, rect);
                        tipShown = true;
                    }
                    OverviewBars::paintSegment(painter, rec, col, true, geo.shaded && !flatOnly, true);
                }

            }
        }


        lastx1 = x1;
    } while (++idx <= idx_end);

    m_detailsRect = QRectF();
    if (classic) {
        painter.setPen(QPen(GraphTheme::ink(Qt::black), 1));
        painter.drawRects(outlines);
        // The day under the mouse was washed red above; in the other graphs the line cursor marks it
        if (hlIdx < 0) paintDayCursor(painter, graph, rect, days);
    } else if (hlIdx >= 0) {
        const QDate hlDate = dayindex.key(hlIdx);
        paintHoverMarker(painter, graph, rect, days, mouseSlot, hlDate, hlIdx, true);
        // Between the day's sessions: the date alone, not the last session pointed at
        if (!tipShown) showHoverTip(graph, hlDate.toString(QLocale::system().dateFormat(QLocale::ShortFormat)), mouse, rect);
    } else if (!emptyHover()) {
        notHoveredHere();
    }
    afterDraw(painter, graph, rect);
}

