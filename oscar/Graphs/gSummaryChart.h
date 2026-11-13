 /* gSessionTimesChart Header
 *
 * Copyright (c) 2019-2025 The Oscar Team
 * Copyright (C) 2011-2018 Mark Watkins 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */
#if 1
#ifndef GSUMMARYCHART_H
#define GSUMMARYCHART_H

#include "SleepLib/day.h"
#include "SleepLib/profiles.h"
#include "Graphs/gGraphView.h"
#include "SleepLib/appsettings.h"
#include "Graphs/summarylines.h"


struct TimeSpan
{
public:
    TimeSpan():begin(0), end(0) {}
    TimeSpan(float b, float e) : begin(b), end(e) {}
    TimeSpan(const TimeSpan & copy) {
        begin = copy.begin;
        end = copy.end;
    }
    ~TimeSpan() {}
    float begin;
    float end;
};

struct SummaryCalcItem {
    SummaryCalcItem() {
        code = 0;
        type = ST_CNT;
        color = Qt::black;
        wavg_sum = 0;
        avg_sum = 0;
        cnt = 0;
        divisor = 0;
        min = 0;
        max = 0;
    }

    SummaryCalcItem operator=(const SummaryCalcItem & copy) ;

    SummaryCalcItem(const SummaryCalcItem & copy) ;

    SummaryCalcItem(ChannelID code, SummaryType type, QColor color)
        :code(code), type(type), color(color) {
    }
    float mid()
    {
        float val = 0;
        switch (midcalc) {
        case 0:
            if (median_data.size() > 0)
                val = median(median_data.begin(), median_data.end());
            break;
        case 1:
            if (divisor > 0)
                val = wavg_sum / divisor;
            break;
        case 2:
            if (cnt > 0)
                val = avg_sum / cnt;
        }
        return val;
    }


    inline void update(float value, float weight) {
        if (midcalc == 0) {
            median_data.append(value);
        }

        avg_sum += value;
        cnt++;
        wavg_sum += value * weight;
        divisor += weight;
        min = qMin(min, value);
        max = qMax(max, value);
    }

    void reset(int reserve, short mc) {
        midcalc = mc;

        wavg_sum = 0;
        avg_sum = 0;
        divisor = 0;
        cnt = 0;
        min = 99999;
        max = -99999;
        median_data.clear();
        if (midcalc == 0) {
            median_data.reserve(reserve);
        }
    }
    ChannelID code;
    SummaryType type;
    QColor color;

    double wavg_sum;
    double divisor;
    double avg_sum;
    int cnt;
    EventDataType min;
    EventDataType max;
    static short midcalc;

    QList<float> median_data;

};

struct SummaryChartSlice {
    SummaryChartSlice() {
        calc = nullptr;
        height = 0;
        value = 0;
        name = "";
        color = Qt::black;
    }

    SummaryChartSlice(const SummaryChartSlice & copy) ;
    SummaryChartSlice& operator=(SummaryChartSlice & other) ;

    SummaryChartSlice& operator=(const SummaryChartSlice & other) ;

    SummaryChartSlice(SummaryCalcItem * calc, EventDataType value, EventDataType height, QString name, QColor color)
        :calc(calc), value(value), height(height), name(name), color(color) {
    }
    SummaryCalcItem * calc;
    EventDataType value;
    EventDataType height;
    QString name;
    QColor color;
//    QBrush brush;
};

/*! \struct DetailRow
    \brief One part of a day's bar, as shown in an Overview details popup */
struct DetailRow {
    QString value;      //!< The part's value with its unit, e.g. "2.30 /h"
    QString name;       //!< What the part is, e.g. "Hypopnea (H)"
    QString note;       //!< A short explanation or extra numbers; may be empty
    QColor color;       //!< The part's colour (light-mode; translated for dark mode when drawn)
    double size = 0;    //!< The part's height in chart units, for its swatch
};

class gSummaryChart : public QObject , public Layer
{
    Q_OBJECT;
public:
    gSummaryChart(QString label, MachineType machtype);
    gSummaryChart(ChannelID code, MachineType machtype);
    virtual ~gSummaryChart();

    //! \brief Renders the graph to the QPainter object
    virtual void paint(QPainter &, gGraph &, const QRegion &);

    //! \brief Called whenever data model changes underneath. Day object is not needed here, it's just here for Layer compatability.
    virtual void SetDay(Day *day = nullptr);

    //! \brief Returns true if no data was found for this day during SetDay
    virtual bool isEmpty() { return m_empty; }

    //! \brief Allows chart to recalculate empty flag.
    void reCalculate() {m_empty=false;};

    virtual void populate(Day *, int idx);

    //! \brief Override to setup custom stuff before main loop
    virtual void preCalc();

    //! \brief Override to call stuff in main loop
    virtual void customCalc(Day *, QVector<SummaryChartSlice> &);

    //! \brief Override to call stuff after draw is complete
    virtual void afterDraw(QPainter &, gGraph &, QRectF);

    //! \brief Return any extra data to show beneath the date in the hover over tooltip
    virtual QString tooltipData(Day *, int);

    //! \brief The rows of a details popup for \a day (index \a idx), top part of the bar first
    virtual QList<DetailRow> detailRows(Day *day, int idx);
    //! \brief The popup's headline after its date (e.g. "AHI 4.30"); empty to use the graph's title
    virtual QString detailHeadline(Day *day, int idx);
    //! \brief A line under the popup's header (e.g. hours of use); may be empty
    virtual QString detailSubtitle(Day *day, int idx);
    //! \brief The day at index \a idx in the chart's day list, or nullptr
    Day *dayAt(int idx) const { return (idx >= 0 && idx < daylist.size()) ? daylist.at(idx) : nullptr; }
    //! \brief The Details button as last drawn, in graph view coordinates; empty when hidden
    const QRectF &detailsButtonRect() const { return m_detailsRect; }

    virtual void dataChanged() {
        cache.clear();
        m_lineCacheKey.clear();
    }

    /*! \brief The dotted lines this chart offers and whether each is switched on. Empty before
        the first paint and for charts with none.

        Values are for the dates last painted. They take a pass over every session in the range
        to work out, so only the lines that are switched on have theirs filled in, unless
        \a allValues is true (for the menu, which shows every line's value). */
    QVector<SummaryLine> lines(bool allValues = false);
    //! \brief Switches the line with SummaryLineId \a id on or off
    void setLineEnabled(int id, bool on);
    //! \brief True once the user (or a saved preference) has chosen this chart's lines
    bool hasLineMask() const { return m_lineMaskSet; }
    //! \brief The chosen lines: bit n is set when the line with SummaryLineId n is on
    quint32 lineMask() const { return m_lineMask; }
    //! \brief Restores a saved choice of lines
    void setLineMask(quint32 mask) { m_lineMask = mask; m_lineMaskSet = true; }
    //! \brief First date drawn at the last paint
    QDate visibleStart() const { return m_visStart; }
    //! \brief Last date drawn at the last paint
    QDate visibleEnd() const { return m_visEnd; }

    virtual int addCalc(ChannelID code, SummaryType type, QColor color);
    //! \brief The colour of the chart's first calculation, which its bars use; invalid if none
    QColor mainColor() const { return calcitems.isEmpty() ? QColor() : calcitems.at(0).color; }
    virtual int addCalc(ChannelID code, SummaryType type);

    virtual Layer * Clone() {
        gSummaryChart * sc = new gSummaryChart(m_label, m_machtype);
        Layer::CloneInto(sc);
        CloneInto(sc);

        // copy this here, because only base summary charts need it
        sc->calcitems = calcitems;

        return sc;
    }

    void CloneInto(gSummaryChart * layer) {
        layer->m_empty = m_empty;
        layer->firstday = firstday;
        layer->lastday = lastday;
        layer->expected_slices = expected_slices;
        layer->nousedays = nousedays;
        layer->totaldays = totaldays;
        layer->peak_value = peak_value;
        layer->idx_start = idx_start;
        layer->idx_end = idx_end;
        layer->cache.clear();
        layer->dayindex = dayindex;
        layer->daylist = daylist;
        layer->m_lineMask = m_lineMask;
        layer->m_lineMaskSet = m_lineMaskSet;
    }
    //! \brief Colour of the Details button
    static inline const QColor DetailsAccent = QColor(0x6D, 0x4B, 0xC9);

signals:
    void summaryChartEmpty(gSummaryChart*,qint64,qint64,bool);
    //! \brief The Details button was clicked for \a date (index \a idx in the chart's day list)
    void detailsRequested(gSummaryChart *chart, QDate date, int idx);

protected:
    //! \brief Key was pressed that effects this layer
    virtual bool keyPressEvent(QKeyEvent *event, gGraph *graph);

    //! \brief Mouse moved over this layers area (shows the hover-over tooltips here)
    virtual bool mouseMoveEvent(QMouseEvent *event, gGraph *graph);

    //! \brief Mouse Button was pressed over this area
    virtual bool mousePressEvent(QMouseEvent *event, gGraph *graph);

    //! \brief Mouse Button was released over this area. (jumps to daily view here)
    virtual bool mouseReleaseEvent(QMouseEvent *event, gGraph *graph);

    /*! \brief Marks the day under the mouse: a faint line through \a slot, and a Details button
        at the top of \a plot when the day has data. Nothing is drawn when printing. */
    void paintHoverMarker(QPainter &painter, gGraph &graph, const QRectF &plot, const QRectF &slot,
                          const QDate &date, int idx, bool hasData);
    //! \brief Called when no day is under the mouse: redraws once so the date label stops being bold
    void clearHover(gGraph &graph);
    //! \brief True for a plain left click on the Details button
    bool isDetailsClick(QMouseEvent *event) const;
    /*! \brief The lines this chart offers between \a start and \a end, each with its default
        on/off state. Values come from RangeFigures or the profile's range functions, calculated
        over the whole range: never an average of nightly figures.

        \param wanted Bit n set: work out the value of the line with SummaryLineId n. The other
               lines are returned with their names and defaults only, which costs nothing. */
    virtual QVector<SummaryLine> calcLines(const QDate &start, const QDate &end, quint32 wanted);
    //! \brief Card look: draws the enabled lines across \a plot and their legend in the header
    void paintLines(QPainter &painter, gGraph &graph, const QRectF &plot, float miny, float ymult);
    QDate m_visStart, m_visEnd;         //!< Dates drawn at the last paint
    QVector<SummaryLine> m_lineCache;   //!< calcLines() result for m_lineCacheKey
    QString m_lineCacheKey;             //!< Dates and preferences m_lineCache was calculated for
    quint32 m_lineHave = 0;             //!< Lines whose values m_lineCache holds (bits as lineMask())
    quint32 m_lineMask = 0;             //!< See lineMask()
    bool m_lineMaskSet = false;         //!< False while the lines' own defaults apply
    QRectF m_detailsRect;       //!< Details button as last drawn; empty when not shown

    /*! \brief Shows the hover tooltip \a text. With no Details button it goes beside the mouse,
        as in OSCAR 2.1; with one it goes beside the button, on the side with more room in
        \a plot, so the button is never covered. */
    void showHoverTip(gGraph &graph, const QString &text, const QPoint &mouse, const QRectF &plot);
    QDate m_detailsDate;        //!< Day the Details button belongs to
    int m_detailsIdx = -1;      //!< That day's index in daylist
    QDate m_lastHoverDate;      //!< Day under the mouse at the last paint, to tell when it changes
    //! \brief The day's cached parts, filled in first if the day has not been drawn yet
    const QVector<SummaryChartSlice> &slicesFor(Day *day, int idx);

    QString durationInHoursToHhMmSs(double duration);
    QString durationInMinutesToHhMmSs(double duration);
    QString durationInSecondsToHhMmSs(double duration);

    QString m_label;
    MachineType m_machtype;
    bool m_empty;
    bool m_emptyPrev;
    int hl_day;
    int tz_offset;
    float tz_hours;
    QDate firstday;
    QDate lastday;

    QMap<QDate, int> dayindex;
    QList<Day *> daylist;

    QHash<int, QVector<SummaryChartSlice> > cache;
    QVector<SummaryCalcItem> calcitems;

    int expected_slices;

    int nousedays;
    int totaldays;

    EventDataType peak_value;
    EventDataType min_value;

    int idx_start;
    int idx_end;

    short midcalc;
};



#endif // GSUMMARYCHART_H

#endif
