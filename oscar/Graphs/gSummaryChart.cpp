/* gSummaryChart Implementation
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
#include <functional>

#include "mainwindow.h"
#include "SleepLib/profiles.h"
#include "SleepLib/machine_common.h"
#include "gSummaryChart.h"

#include "gYAxis.h"
#include "graphtheme.h"
#include "overviewbars.h"
#include "rangefigures.h"

extern MainWindow * mainwin;

short SummaryCalcItem::midcalc;

gSummaryChart::gSummaryChart(QString label, MachineType machtype)
    :Layer(NoChannel), m_label(label), m_machtype(machtype)
{
    m_layertype = LT_Overview;
    QDateTime d1 = QDateTime::currentDateTime();
    QDateTime d2 = d1;
    d1.setTimeZone(QTimeZone("UTC"));  // CHECK: Does this deal with DST?
    tz_offset = d2.secsTo(d1);
    tz_hours = tz_offset / 3600.0;
    expected_slices = 5;

    idx_end = 0;
    idx_start = 0;
}

gSummaryChart::gSummaryChart(ChannelID code, MachineType machtype)
    :Layer(code), m_machtype(machtype)
{
    m_layertype = LT_Overview;
    QDateTime d1 = QDateTime::currentDateTime();
    QDateTime d2 = d1;
    d1.setTimeZone(QTimeZone("UTC"));  // CHECK: Does this deal with DST?
    tz_offset = d2.secsTo(d1);
    tz_hours = tz_offset / 3600.0;
    expected_slices = 5;

    addCalc(code, ST_MIN, brighten(schema::channel[code].defaultColor() ,0.60f));
    addCalc(code, ST_MID, brighten(schema::channel[code].defaultColor() ,1.20f));
    addCalc(code, ST_90P, brighten(schema::channel[code].defaultColor() ,1.70f));
    addCalc(code, ST_MAX, brighten(schema::channel[code].defaultColor() ,2.30f));

    idx_end = 0;
    idx_start = 0;
}

gSummaryChart::~gSummaryChart()
{
}

QVector<SummaryLine> gSummaryChart::lines(bool allValues)
{
    if (!m_visStart.isValid() || !p_profile) return {};
    const QString key = QString("%1|%2|%3|%4|%5").arg(m_visStart.toJulianDay()).arg(m_visEnd.toJulianDay())
            .arg(p_profile->general->prefCalcMiddle()).arg(p_profile->general->prefCalcPercentile())
            .arg(p_profile->general->calculateRDI());
    if (key != m_lineCacheKey) {
        m_lineCache = calcLines(m_visStart, m_visEnd, 0);       // names and defaults only
        m_lineHave = 0;
        m_lineCacheKey = key;
    }
    const auto isOn = [this](const SummaryLine &line) {
        return m_lineMaskSet ? (m_lineMask & (1u << line.id)) != 0 : line.enabled;
    };
    // This runs at every paint, and a value is a pass over the range's sessions, so only the
    // values needed are worked out, once for each set of dates
    quint32 wanted = 0;
    for (const SummaryLine &line : m_lineCache) {
        if (allValues || isOn(line)) wanted |= 1u << line.id;
    }
    if (wanted & ~m_lineHave) {
        m_lineHave |= wanted;
        m_lineCache = calcLines(m_visStart, m_visEnd, m_lineHave);
    }
    QVector<SummaryLine> out = m_lineCache;
    for (SummaryLine &line : out) line.enabled = isOn(line);
    return out;
}

void gSummaryChart::setLineEnabled(int id, bool on)
{
    quint32 mask = 0;
    const QVector<SummaryLine> current = lines();
    for (const SummaryLine &line : current) {
        if (line.enabled) mask |= 1u << line.id;
    }
    if (on) mask |= 1u << id; else mask &= ~(1u << id);
    m_lineMask = mask;
    m_lineMaskSet = true;
}

QVector<SummaryLine> gSummaryChart::calcLines(const QDate &start, const QDate &end, quint32 wanted)
{
    QVector<SummaryLine> out;
    if (calcitems.isEmpty()) return out;
    const SummaryCalcItem &first = calcitems.at(0);
    const ChannelID code = first.code;
    if (code == NoChannel) return out;
    const auto want = [wanted](int id) { return (wanted & (1u << id)) != 0; };

    if (first.type == ST_CPH) {
        // An event graph: the events per hour over the whole range
        SummaryLine line{ SL_Middle, QObject::tr("Average"), 0, first.color.darker(150), false };
        if (want(SL_Middle)) {
            line.available = p_profile->calcHours(m_machtype, start, end) > 0;
            if (line.available) line.value = RangeFigures::eventsPerHour(code, m_machtype, start, end);
        }
        out.append(line);
        return out;
    }
    if (first.type != ST_MIN) return out;

    // A waveform graph offers the lines its Daily graph does. A line whose figure cannot be
    // worked out from the range's data (a percentile of summary-only days) is not available.
    schema::Channel &chan = schema::channel[code];
    const auto add = [&](int id, ChannelCalcType type, bool hasFigure, const std::function<double()> &value) {
        SummaryLine line{ id, chan.calc[type].label(), 0, chan.calc[type].color, chan.calc[type].enabled };
        if (want(id)) {
            line.available = hasFigure;
            if (hasFigure) line.value = value();
        }
        out.append(line);
    };
    const bool anyStat = want(SL_Max) || want(SL_Perc) || want(SL_Middle) || want(SL_Min);
    const bool detail = anyStat && RangeFigures::hasDetail(code, m_machtype, start, end);
    const bool data = detail || (anyStat && RangeFigures::hasData(code, m_machtype, start, end));
    const bool middle = p_profile->general->prefCalcMiddle() == 0 ? detail : data;
    add(SL_Max, Calc_Max, data, [&] { return p_profile->calcMax(code, m_machtype, start, end); });
    add(SL_Perc, Calc_Perc, detail, [&] { return RangeFigures::percentile(code, m_machtype, start, end); });
    add(SL_Middle, Calc_Middle, middle, [&] { return RangeFigures::middle(code, m_machtype, start, end); });
    if ((code != CPAP_Snore) && (code != CPAP_FlowLimit) && (code != CPAP_RDI) && (code != CPAP_AHI)) {
        add(SL_Min, Calc_Min, data, [&] { return p_profile->calcMin(code, m_machtype, start, end); });
    }
    if (code == CPAP_Leak) {
        add(SL_Threshold, Calc_UpperThresh, true, [] { return p_profile->cpap->leakRedline(); });
    }
    return out;
}

void gSummaryChart::paintLines(QPainter &painter, gGraph &graph, const QRectF &plot, float miny, float ymult)
{
    const QRect header = graph.headerTextRect();
    if (header.isEmpty() || graph.printing()) return;       // Card look on screen only

    const QVector<SummaryLine> all = lines();
    const QFontMetrics fm(*defaultfont);
    const int sample = 16;      // length of the legend's piece of line
    int right = header.right();

    painter.save();
    painter.setFont(*defaultfont);
    for (const SummaryLine &line : all) {
        if (!line.enabled || !line.available) continue;
        QColor color = GraphTheme::ink(line.color);
        color.setAlpha(200);
        const QPen pen(QBrush(color), 2, Qt::DotLine);      // as the Daily graphs draw theirs

        const double y = plot.bottom() - (line.value - miny) * ymult;
        if (y >= plot.top() && y <= plot.bottom()) {
            painter.setPen(pen);
            painter.drawLine(QLineF(plot.left(), y, plot.right(), y));
        }

        // Legend, filled in from the right; lines that don't fit are still drawn on the plot
        const QString text = QString("%1 %2").arg(line.label).arg(line.value, 0, 'f', 2);
        const int textWidth = fm.horizontalAdvance(text);
        const int width = sample + 4 + textWidth + 12;
        if (right - width < header.left() + 40) continue;
        painter.setPen(GraphTheme::ink(QColor(60, 60, 60)));
        painter.drawText(QRect(right - textWidth, header.top(), textWidth, header.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, text);
        painter.setPen(pen);
        const double sampleY = header.center().y() + 1.5;
        painter.drawLine(QLineF(right - textWidth - 4 - sample, sampleY, right - textWidth - 4, sampleY));
        right -= width;
    }
    painter.restore();
    graph.reserveHeaderRight(header.right() - right);
}

void gSummaryChart::SetDay(Day *unused_day)
{
    cache.clear();
    m_lineCacheKey.clear();

    Q_UNUSED(unused_day)
    Layer::SetDay(nullptr);

    if (m_machtype != MT_CPAP) {
        // Channels' machine types are not terribly reliable: oximetry channels can be reported by a CPAP,
        // and position channels can be reported by an oximeter. So look for any days with data.
        firstday = p_profile->FirstDay();
        lastday = p_profile->LastDay();
    } else {
        // But CPAP channels (like pressure settings) can only be reported by a CPAP.
        firstday = p_profile->FirstDay(m_machtype);
        lastday = p_profile->LastDay(m_machtype);
    }

    dayindex.clear();
    daylist.clear();

    if (!firstday.isValid() || !lastday.isValid()) return;
    QDate date = firstday;
    int idx = 0;
    do {
        auto di = p_profile->daylist.find(date);
        Day * day = nullptr;
        if (di != p_profile->daylist.end()) {
            day = di.value();
        }
        daylist.append(day);
        dayindex[date] = idx;
        idx++;
        date = date.addDays(1);
    } while (date <= lastday);

    m_minx = QDateTime(firstday, QTime(0,0,0), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
    m_maxx = QDateTime(lastday, QTime(23,59,59), QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
    m_miny = 0;
    m_maxy = 20;

    m_empty = false;
    m_emptyPrev = true;

}


int gSummaryChart::addCalc(ChannelID code, SummaryType type, QColor color)
{
    calcitems.append(SummaryCalcItem(code, type, color));
    return calcitems.size() - 1;  // return the index of the newly appended calc
}

int gSummaryChart::addCalc(ChannelID code, SummaryType type)
{
    return addCalc(code, type, schema::channel[code].defaultColor());
}


bool gSummaryChart::keyPressEvent(QKeyEvent *event, gGraph *graph)
{
    Q_UNUSED(event)
    Q_UNUSED(graph)
    return false;
}

bool gSummaryChart::mouseMoveEvent(QMouseEvent *event, gGraph *graph)
{
    Q_UNUSED(event)
    Q_UNUSED(graph)
    return false;
}

bool gSummaryChart::mousePressEvent(QMouseEvent *event, gGraph *graph)
{
    Q_UNUSED(graph)
    return isDetailsClick(event);
}

bool gSummaryChart::isDetailsClick(QMouseEvent *event) const
{
    return event->button() == Qt::LeftButton
           && !(event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier))
           && m_detailsRect.contains(event->position());
}

void gSummaryChart::showHoverTip(gGraph &graph, const QString &text, const QPoint &mouse, const QRectF &plot)
{
    if (!m_detailsRect.isValid()) {
        graph.ToolTip(text, mouse.x() + 20, mouse.y(), TT_AlignBottomLeft);
        return;
    }
    // The tooltip's bottom edge follows the mouse, but stays below the top of the button
    const int y = qMax(mouse.y(), int(m_detailsRect.bottom()) + 4);
    if (m_detailsRect.center().x() > plot.center().x()) {
        graph.ToolTip(text, int(m_detailsRect.left()) - 6, y, TT_AlignBottomRight);
    } else {
        graph.ToolTip(text, int(m_detailsRect.right()) + 6, y, TT_AlignBottomLeft);
    }
}

bool gSummaryChart::hoverEmptyDay(QPainter &painter, gGraph &graph, const QRectF &plot, const QPoint &mouse,
                                  const QDate &first, double slotWidth)
{
    if (graph.printing() || slotWidth <= 0 || !plot.contains(mouse)) return false;
    const int slotNumber = int((mouse.x() - plot.left()) / slotWidth);
    const QRectF slot(plot.left() + slotNumber * slotWidth, plot.top(), slotWidth, plot.height());
    const QDate date = first.addDays(slotNumber);
    paintHoverMarker(painter, graph, plot, slot, date, -1, false);
    showHoverTip(graph, date.toString(QLocale::system().dateFormat(QLocale::ShortFormat)), mouse, plot);
    return true;
}

void gSummaryChart::paintFrame(QPainter &painter, gGraph &graph, const QRectF &plot)
{
    if (graph.graphView()->cardLook() && !graph.printing()) {
        painter.setPen(GraphTheme::axis());
        painter.drawLine(plot.topLeft(), plot.bottomLeft());
        painter.drawLine(plot.bottomLeft(), plot.bottomRight());
    } else {
        painter.setPen(GraphTheme::ink(Qt::black));
        painter.drawRect(plot);
    }
}

void gSummaryChart::clearHover(gGraph &graph)
{
    // As with a new day under the mouse, the date axis needs one more frame to drop the bold label
    if (m_lastHoverDate.isValid()) {
        m_lastHoverDate = QDate();
        graph.graphView()->timedRedraw(0);
    }
}

void gSummaryChart::paintHoverMarker(QPainter &painter, gGraph &graph, const QRectF &plot, const QRectF &slot,
                                     const QDate &date, int idx, bool hasData)
{
    m_detailsRect = QRectF();
    if (graph.printing()) return;
    // The date axis is drawn before the bars, so a new day under the mouse needs one more frame
    // for its label to turn bold
    if (date != m_lastHoverDate) {
        m_lastHoverDate = date;
        graph.graphView()->timedRedraw(0);
    }
    graph.setHoverDate(date);

    // A faint line through the middle of the day
    const double x = std::floor(slot.center().x()) + 0.5;
    painter.setPen(QPen(GraphTheme::ink(QColor(0, 0, 0, 110)), 1));
    painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    if (!hasData) return;
    // No button on a popped-out or cloned graph: the popup it opens belongs to the Overview page
    if (graph.isSnapshot() || graph.graphView()->isPopout()) return;

    // The Details button at the top of the day
    const QString text = tr("Details");
    QFont font = *defaultfont;
    font.setBold(true);
    const QFontMetricsF fm(font);
    const double h = fm.height() + 6;
    const double w = fm.horizontalAdvance(text) + 30;
    QRectF r(x - w / 2, plot.top() + 2, w, h);
    if (r.left() < plot.left()) r.moveLeft(plot.left());
    if (r.right() > plot.right()) r.moveRight(plot.right());

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(DetailsAccent);
    painter.drawRoundedRect(r, h / 2, h / 2);
    painter.setPen(QPen(Qt::white, 1.6, Qt::SolidLine, Qt::RoundCap));
    const double ix = r.left() + 10, iy = r.center().y();
    painter.drawLine(QPointF(ix, iy - 3.5), QPointF(ix + 8, iy - 3.5));
    painter.drawLine(QPointF(ix, iy), QPointF(ix + 8, iy));
    painter.drawLine(QPointF(ix, iy + 3.5), QPointF(ix + 5, iy + 3.5));
    painter.setFont(font);
    painter.drawText(QRectF(ix + 12, r.top(), r.right() - ix - 12, r.height()), Qt::AlignVCenter | Qt::AlignLeft, text);
    painter.restore();

    m_detailsRect = r;
    m_detailsDate = date;
    m_detailsIdx = idx;
}

bool gSummaryChart::mouseReleaseEvent(QMouseEvent *event, gGraph *graph)
{
    if (isDetailsClick(event)) {
        emit detailsRequested(this, m_detailsDate, m_detailsIdx);
        return true;
    }
    if (!(event->modifiers() & Qt::ShiftModifier)) return false;

    #if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        float x = event->x();
    #else   
        float x = event->position().x();
    #endif  
    x -= m_rect.left();
    
    EventDataType miny;
    EventDataType maxy;

    graph->roundY(miny, maxy);

    QDate date = QDateTime::fromMSecsSinceEpoch(m_minx, QTimeZone::systemTimeZone()).date();

    int days = ceil(double(m_maxx - m_minx) / 86400000.0);

    float barw = float(m_rect.width()) / float(days);

    float idx = x/barw;

    date = date.addDays(idx);

    auto it = dayindex.find(date);
    if (it != dayindex.end()) {
        Day * day = daylist.at(it.value());
        if (day) {
            mainwin->getDaily()->LoadDate(date);
            mainwin->JumpDaily();
        }
    }

    return true;
}

void gSummaryChart::preCalc()
{
    midcalc = p_profile->general->prefCalcMiddle();

    for (auto & calc : calcitems) {
        calc.reset(idx_end - idx_start, midcalc);
    }
}

void gSummaryChart::customCalc(Day *day, QVector<SummaryChartSlice> & slices)
{
    int size = slices.size();
    if (size != calcitems.size()) {
        return;
    }
    float hour = day->hours(m_machtype);

    for (int i=0; i < size; ++i) {
        const SummaryChartSlice & slice = slices.at(i);
        SummaryCalcItem & calc = calcitems[i];

        calc.update(slice.value, hour);
     }
}

void gSummaryChart::afterDraw(QPainter &painter, gGraph &graph, QRectF rect)
{
    if (totaldays == nousedays) return;

    if (calcitems.size() == 0) return;

    QStringList strlist;
    QString txt;

    int midcalc = p_profile->general->prefCalcMiddle();
    QString midstr;
    if (midcalc == 0) {
        midstr = QObject::tr("Med.");
    } else if (midcalc == 1) {
        midstr = QObject::tr("W-Avg");
    } else {
        midstr = QObject::tr("Avg");
    }


    float perc = p_profile->general->prefCalcPercentile();
    QString percstr = QString("%1%").arg(perc, 0, 'f',0);

    schema::Channel & chan = schema::channel[calcitems.at(0).code];

    for (auto & calc : calcitems) {

        if (calcitems.size() == 1) {
            float val = calc.min;
            if (val < 99998)
                strlist.append(QObject::tr("Min: %1").arg(val,0,'f',2));
        }

        float mid = 0;
        switch (midcalc) {
        case 0:
            if (calc.median_data.size() > 0) {
                mid = median(calc.median_data.begin(), calc.median_data.end());
            }
            break;
        case 1:
            mid = calc.wavg_sum / calc.divisor;
            break;
        case 2:
            mid = calc.avg_sum / calc.cnt;
            break;
        }

        float val = 0;
        switch (calc.type) {
        case ST_CPH:
            val = mid;
            txt = midstr+": ";
            break;
        case ST_SPH:
            val = mid;
            txt = midstr+": ";
            break;
        case ST_MIN:
            val = calc.min;
            if (val >= 99998) continue;
            txt = QObject::tr("Min: ");
            break;
        case ST_MAX:
            val = calc.max;
            if (val <= -99998) continue;
            txt = QObject::tr("Max: ");
            break;
        case ST_SETMIN:
            val = calc.min;
            if (val >= 99998) continue;
            txt = QObject::tr("Min: ");
            break;
        case ST_SETMAX:
            val = calc.max;
            if (val <= -99998) continue;
            txt = QObject::tr("Max: ");
            break;
        case ST_MID:
            val = mid;
            txt = QString("%1: ").arg(midstr);
            break;
        case ST_90P:
            val = mid;
            txt = QString("%1: ").arg(percstr);
            break;
        default:
            val = mid;
            txt = QString("???: ");
            break;
        }
        strlist.append(QString("%1%2").arg(txt).arg(val,0,'f',2));
        if (calcitems.size() == 1) {
            val = calc.max;
            if (val > -99998)
                strlist.append(QObject::tr("Max: %1").arg(val,0,'f',2));
        }
    }

    QString str;
    if (totaldays > 1) {
        str = QObject::tr("%1 (%2 days): ").arg(chan.fullname()).arg(totaldays);
    } else {
        str = QObject::tr("%1 (%2 day): ").arg(chan.fullname()).arg(totaldays);
    }
    str += " "+strlist.join(", ");
    if (graph.headerSummary(str)) return;

    QRectF rec(rect.left(), rect.top(), 0,0);
    painter.setFont(*defaultfont);
    rec = painter.boundingRect(rec, Qt::AlignTop, str);
    rec.moveBottom(rect.top()-3*graph.printScaleY());
    painter.drawText(rec, Qt::AlignTop, str);



}

const QVector<SummaryChartSlice> &gSummaryChart::slicesFor(Day *day, int idx)
{
    if (!cache.contains(idx)) {
        day->OpenSummary();
        populate(day, idx);
    }
    return cache[idx];
}

QList<DetailRow> gSummaryChart::detailRows(Day *day, int idx)
{
    QList<DetailRow> rows;
    const auto &slices = slicesFor(day, idx);
    for (int i = slices.size() - 1; i >= 0; --i) {
        const auto &s = slices.at(i);
        if (s.value == 0) continue;     // only the parts that have something to show
        rows.append({ QString::number(s.value, 'f', 2), s.name.trimmed(), QString(), s.color, s.height });
    }
    return rows;
}

QString gSummaryChart::detailHeadline(Day *, int)
{
    return QString();
}

QString gSummaryChart::detailSubtitle(Day *, int)
{
    return QString();
}

QString gSummaryChart::tooltipData(Day *, int idx)
{
    QString txt;
    const auto & slices = cache[idx];
    int i = slices.size();
    while (i > 0) {
        i--;
        txt += QString("\n%1: %2").arg(slices[i].name).arg(float(slices[i].value), 0, 'f', 2);
    }
    return txt;
}

void gSummaryChart::populate(Day * day, int idx)
{

    bool good = false;
    for (const auto & item : calcitems) {
        if (day->hasData(item.code, item.type)) {
            good = true;
            break;
        }
    }
    if (!good) return;

    auto & slices = cache[idx];

    float hours = day->hours(m_machtype);
    if ((hours==0) && (m_machtype != MT_CPAP)) hours = day->hours();
    float base = 0;

    for (auto & item : calcitems) {
        ChannelID code = item.code;
        schema::Channel & chan = schema::channel[code];
        float value = 0;
        QString name;
        QColor color;
        switch (item.type) {
        // Per-hour values are undefined on a day with no mask time. Omitting the
        // slice also keeps the day out of customCalc()'s statistics (GitLab #290).
        case ST_CPH:
            if (hours <= 0) break;
            value = day->count(code) / hours;
            name = chan.label();
            color = item.color;
            slices.append(SummaryChartSlice(&item, value, value, name, color));
            break;
        case ST_SPH:
            if (hours <= 0) break;
            value = (100.0 / hours) * (day->sum(code) / 3600.0);
            name = QObject::tr("% in %1").arg(chan.label());
            color = item.color;
            slices.append(SummaryChartSlice(&item, value, value, name, color));
            break;
        case ST_HOURS:
            value = hours;
            name = QObject::tr("Hours");
            color = COLOR_LightBlue;
            slices.append(SummaryChartSlice(&item, value, hours, name, color));
            break;
        case ST_MIN:
            value = day->Min(code);
            name = QObject::tr("Min %1").arg(chan.label());
            color = item.color;
            slices.append(SummaryChartSlice(&item, value, value - base, name, color));
            base = value;
            break;
        case ST_MID:
            value = day->calcMiddle(code);
            name = day->calcMiddleLabel(code);
            color = item.color;
            slices.append(SummaryChartSlice(&item, value, value - base, name, color));
            base = value;
            break;
        case ST_90P:
            value = day->calcPercentile(code);
            name = day->calcPercentileLabel(code);
            color = item.color;
            slices.append(SummaryChartSlice(&item, value, value - base, name, color));
            base = value;
            break;
        case ST_MAX:
            value = day->calcMax(code);
            name = day->calcMaxLabel(code);
            color = item.color;
            slices.append(SummaryChartSlice(&item, value, value - base, name, color));
            base = value;
            break;
        default:
            break;
        }
    }
}

void gSummaryChart::paint(QPainter &painter, gGraph &graph, const QRegion &region)
{
    QRectF rect = region.boundingRect();

    rect.translate(0.0f, 0.001f);

    paintFrame(painter, graph, rect);

    rect.moveBottom(rect.bottom()+1);

    m_minx = graph.min_x;
    m_maxx = graph.max_x;

    QDateTime date2 = QDateTime::fromMSecsSinceEpoch(m_minx, QTimeZone::systemTimeZone());
    QDateTime enddate2 = QDateTime::fromMSecsSinceEpoch(m_maxx, QTimeZone::systemTimeZone());

    QDate date = date2.date();
    QDate enddate = enddate2.date();

    int days = ceil(double(m_maxx - m_minx) / 86400000.0);
    m_visStart = date;
    m_visEnd = date.addDays(qMax(days, 1) - 1);

    // Card look, and nothing drawn under the mouse: the day still shows its date
    const QDate chartStart = date;
    const auto emptyHover = [&]() {
        if (!graph.graphView()->cardLook() || graph.printing()) return false;
        return hoverEmptyDay(painter, graph, rect, graph.graphView()->currentMousePos(), chartStart,
                             double(rect.width()) / qMax(days, 1));
    };

    auto it = dayindex.find(date);
    idx_start = 0;
    if (it == dayindex.end()) {
        it = dayindex.begin();
    } else {
        idx_start = it.value();
    }

    int idx = idx_start;

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
        graph.setHoverDate(QDate());
        if (!emptyHover()) clearHover(graph);
        return;
    }

    auto ite = dayindex.find(enddate);
    idx_end = daylist.size()-1;
    if (ite != dayindex.end()) {
        idx_end = ite.value();
    }

    // Nothing is under the mouse on a printed page
    QPoint mouse = graph.printing() ? QPoint(-1, -1) : graph.graphView()->currentMousePos();

    nousedays = 0;
    totaldays = 0;

    QRectF hl_rect;
    QDate hl_date;
    Day * hl_day = nullptr;
    int hl_idx = -1;
    bool hl = false;

    if ((daylist.size() == 0) || (it == dayindex.end())) {
        graph.setHoverDate(QDate());
        if (!emptyHover()) clearHover(graph);
        return;
    }


//            dc++;
    float barw = float(rect.width()) / float(days);
    // The original (2.1) look has touching, outlined bars, a yellow and red hover highlight and a
    // numbers tooltip. The card look has a gap beside each bar, a hover line and a Details button.
    const bool classic = !graph.graphView()->cardLook() || graph.printing();   // print is always the 2.1 look
    const OverviewBars::BarGeometry geo = classic ? OverviewBars::classicBarGeometry(barw)
                                                  : OverviewBars::barGeometry(barw);
    const bool flatOnly = graph.printing() && AppSetting->monochromePrinting();
    graph.setHoverDate(QDate());
    QVector<QRectF> outlines;       // classic look only
    if (classic) outlines.reserve((idx_end - idx) * expected_slices);


    // Virtual call to setup any custom graph stuff
    preCalc();

    float lastx1 = rect.left();
    lastx1 += numDaysOffset * barw;
    float right_edge = (rect.left()+rect.width()+1);


    /////////////////////////////////////////////////////////////////////
    /// Calculate Graph Peaks
    /////////////////////////////////////////////////////////////////////
    peak_value = 0;
    for (int i=idx; i <= idx_end; ++i, lastx1 += barw) {
        Day * day = daylist.at(i);

        if ((lastx1 + barw) > right_edge)
            break;

        if (!day) {
            continue;
        }

        day->OpenSummary();

        auto cit = cache.find(i);

        if (cit == cache.end()) {
            populate(day, i);
            cit = cache.find(i);
        }

        if (cit != cache.end()) {
            float base = 0, val;
            for (const auto & slice : cit.value()) {
                val = slice.height;
                base += val;
            }
            peak_value = qMax(peak_value, base);
        }
    }
    m_miny = 0;
    m_maxy = ceil(peak_value);

    /////////////////////////////////////////////////////////////////////
    /// Y-Axis scaling
    /////////////////////////////////////////////////////////////////////

    EventDataType miny;
    EventDataType maxy;

    graph.roundY(miny, maxy);
    float ymult = float(rect.height()) / (maxy-miny);

    lastx1 = rect.left();
    lastx1 += numDaysOffset * barw;

    /////////////////////////////////////////////////////////////////////
    /// Main drawing loop
    /////////////////////////////////////////////////////////////////////
    do {
        Day * day = daylist.at(idx);

        if ((lastx1 + barw) > right_edge)
            break;

        totaldays++;

        if (!day)
        {
            lastx1 += barw;
            it++;
            nousedays++;
            continue;
        }


        float x1 = lastx1 + barw;

        day->OpenSummary();

        bool hlday = false;
        QRectF rec2(lastx1, rect.top(), barw, rect.height());
        if (rec2.contains(mouse)) {
            hl_rect = rec2;
            hl_day = day;
            hl_date = it.key();
            hl_idx = idx;

            hl = true;
            hlday = true;
        }

        auto cit = cache.find(idx);

        if (cit == cache.end()) {
            populate(day, idx);
            cit = cache.find(idx);
        }

        float lastval = 0, val, y1,y2;
        if (cit != cache.end()) {
            /////////////////////////////////////////////////////////////////////////////////////
            /// Draw pressure settings
            /////////////////////////////////////////////////////////////////////////////////////
            QVector<SummaryChartSlice> & list = cit.value();
            customCalc(day, list);

            // The topmost part with any height gets the rounded top corners
            int topSlice = -1;
            for (int s = 0; s < list.size(); ++s) {
                if (list.at(s).height > 0) topSlice = s;
            }

            for (int s = 0; s < list.size(); ++s) {
                const auto & slice = list.at(s);
                val = slice.height;
                y1 = ((lastval-miny) * ymult);
                y2 = (val * ymult);
                QColor color = GraphTheme::ink(slice.color);

                const QRectF rec = QRectF(lastx1 + geo.offset, rect.bottom() - y1 - y2, geo.width, y2).intersected(rect);
                if (classic) {
                    // The part of the bar under the mouse turns yellow
                    if (hlday && rec.contains(mouse.x(), mouse.y())) color = Qt::yellow;
                    OverviewBars::paintClassicSegment(painter, rec, color, barw);
                    if (barw > 3) outlines.append(rec);
                } else {
                    OverviewBars::paintSegment(painter, rec, color, s == topSlice, geo.shaded && !flatOnly);
                }

                lastval += val;
            }
        }

        lastx1 = x1;
        it++;
    } while (++idx <= idx_end);

    paintLines(painter, graph, rect, miny, ymult);

    m_detailsRect = QRectF();
    if (classic) {
        painter.setPen(QPen(GraphTheme::ink(Qt::black), 1));
        painter.drawRects(outlines);
        // The day under the mouse is washed red
        if (hl) painter.fillRect(hl_rect, QBrush(QColor(255, 0, 0, 64)));
    } else if (hl) {
        paintHoverMarker(painter, graph, rect, hl_rect, hl_date, hl_idx, hl_day != nullptr);
    } else if (!emptyHover()) {
        clearHover(graph);
    }
    if (hl) {
        // In both looks the day's date and numbers are shown in a tooltip
        QString txt = hl_date.toString(QLocale::system().dateFormat(QLocale::ShortFormat)) + " ";
        if (hl_day) txt += tooltipData(hl_day, hl_idx);
        showHoverTip(graph, txt, mouse, rect);
    }
    try {
        afterDraw(painter, graph, rect);
    } catch(...) {
        qDebug() << "Bad median call in" << m_label;
    }


    // This could be turning off graphs prematurely..
    if (cache.size() == 0) {
        m_empty = true;
        m_emptyPrev = true;
        graph.graphView()->updateScale();
        emit summaryChartEmpty(this,m_minx,m_maxx,true);
    } else if (m_emptyPrev) {
        m_emptyPrev = false;
        emit summaryChartEmpty(this,m_minx,m_maxx,false);
    }

}

QString gSummaryChart::durationInHoursToHhMmSs(double duration) {
    return durationInSecondsToHhMmSs(duration * 3600);
}

QString gSummaryChart::durationInMinutesToHhMmSs(double duration) {
    return durationInSecondsToHhMmSs(duration * 60);
}

QString gSummaryChart::durationInSecondsToHhMmSs(double duration) {
    // ensure that a negative duration is supported (could potentially occur when start and end occur in different timezones without compensation)
    double duration_abs = abs(duration);
    int seconds_abs = static_cast<int>(0.5 + duration_abs);
    int daily_hours_abs = seconds_abs / 3600;
    QString result;
    if (daily_hours_abs < 24) {
        result = QTime(0,0,0,0).addSecs(seconds_abs).toString("hh:mm:ss");
    } else {
        result = QString::number(daily_hours_abs + seconds_abs % 86400 / 3600) + ":" + QTime(0, 0, 0, 0).addSecs(seconds_abs).toString("mm:ss");
    }

    if (duration == duration_abs) {
        return result;
    } else {
        return "-" + result;
    }
}





    SummaryCalcItem SummaryCalcItem::operator=(const SummaryCalcItem & copy) {
        code = copy.code;
        type = copy.type;
        color = copy.color;

        wavg_sum = 0;
        avg_sum = 0;
        cnt = 0;
        divisor = 0;
        min = 0;
        max = 0;
        midcalc = p_profile->general->prefCalcMiddle();
        return *this;
    }

    SummaryCalcItem::SummaryCalcItem(const SummaryCalcItem & copy) {
        code = copy.code;
        type = copy.type;
        color = copy.color;

        wavg_sum = 0;
        avg_sum = 0;
        cnt = 0;
        divisor = 0;
        min = 0;
        max = 0;
        midcalc = p_profile->general->prefCalcMiddle();
    }


    SummaryChartSlice::SummaryChartSlice(const SummaryChartSlice & copy) {
        calc = copy.calc;
        value = copy.value;
        height = copy.height;
        name = copy.name;
        color = copy.color;
    }
    
    SummaryChartSlice& SummaryChartSlice::operator=(SummaryChartSlice & other) {
        if (this != &other) {  // Self-assignment check
            calc = other.calc;
            value = other.value;
            height = other.height;
            name = other.name;
            color = other.color;
        }
        return *this;
    }

    SummaryChartSlice& SummaryChartSlice::operator=(const SummaryChartSlice & other) {
        if (this != &other) {  // Self-assignment check
            calc = other.calc;
            value = other.value;
            height = other.height;
            name = other.name;
            color = other.color;
        }
        return *this;
    }

