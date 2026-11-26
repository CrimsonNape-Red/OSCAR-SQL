/* Minutes At Pressure Graph Header
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 * Copyright (C) 2011-2018 Mark Watkins 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef MINUTESATPRESSURE_H
#define MINUTESATPRESSURE_H

#include <atomic>

#include <QMutex>
#include <QPen>
#include <QRunnable>
#include <QWaitCondition>
#include "Graphs/layer.h"
#include "SleepLib/appsettings.h"
#include "SleepLib/day.h"
#include "SleepLib/schema.h"

class gGraphView;
class MinutesAtPressure;
struct PressureInfo
{
public:
    PressureInfo();
    PressureInfo(ChannelID code, qint64 minTime, qint64 maxTime) ;
    PressureInfo(PressureInfo &copy) = default;
    ~PressureInfo() {} ;

    void AddChannel(ChannelID c);
    void AddChannels(QList<ChannelID> & chans);
    void finishCalcs();
    void setMachineTimes(EventDataType min,EventDataType max);

    ChannelID code;
    schema::Channel chan;
    qint64 minTime, maxTime;
    QVector<int> times;
    int peaktime, peakevents;

    QHash<ChannelID, QVector<int> > events;
    QHash<ChannelID, int> numEvents;
    QList<ChannelID> chans;
    QVector<EventList*> eventLists;

    void updateBucketsPerPressure(Session* sess);
    int bucketsPerPressure = 1;
    int numberXaxisDivisions =10;

    EventDataType rawToPressure ( EventStoreType raw,EventDataType gain);
    EventStoreType rawToBucketId ( EventStoreType raw,EventDataType gain);
    EventDataType minpressure = 0.0;
    EventDataType maxpressure = 0.0;
    qint64 totalDuration = 0;

    EventDataType machinePressureMin = 0.0;
    EventDataType machinePressureMax = 0.0;

    int firstPlotBucket =0;
    int lastPlotBucket =0;

private:
    void init();
};


/*! \class RecalcMAP
    \brief One calculation of the Time at Pressure totals for a day and a time range.

    Normally runs on a thread pool thread; for a printed report it runs on the calling thread.
    The totals are built in the job's own PressureInfo objects and handed to the layer in one
    step at the end, so a job that is replaced by a newer one can simply be told to quit():
    it stops at its next check and hands over nothing.

    A background job belongs to the thread pool, which deletes it when run() returns. The
    layer may use its pointer only under MinutesAtPressure::m_jobLock, see there.
  */
class RecalcMAP:public QRunnable
{
    friend class MinutesAtPressure;
public:
    /*! \brief Describe a calculation. Call on the GUI thread.
        \param map      layer that receives the totals
        \param day      day to total; must stay valid until the job has ended
        \param minTime  start of the time range, device time in ms
        \param maxTime  end of the time range, device time in ms
        \param view     graph view to redraw when new totals are ready, or nullptr for none
      */
    RecalcMAP(MinutesAtPressure * map, Day * day, qint64 minTime, qint64 maxTime, gGraphView * view)
        : map(map), m_quit(false), m_day(day), m_view(view), minTime(minTime), maxTime(maxTime) {}
    virtual ~RecalcMAP();

    //! \brief Thread pool entry point: calculate, ask for a redraw, then tell the layer this job has ended.
    virtual void run();

    //! \brief Ask the calculation to stop. Returns at once; safe to call from any thread.
    void quit();


protected:
    MinutesAtPressure * map;
    //! Set by quit(), on the GUI thread, and read by the calculation on its own thread.
    std::atomic<bool> m_quit;

private:
    /*! \brief Total the day and hand the result to the layer.
        \return true if the layer now holds this job's totals, false if the job was told to
                quit or there was no day
      */
    bool calculate();

    Day * m_day;            //!< Day being totalled
    gGraphView * m_view;    //!< View to redraw afterwards, may be nullptr
    qint64 minTime, maxTime;
    ChannelID chanId;       // required for debug.

    PressureInfo * ipap_info;
    void updateTimes(PressureInfo & info);
    void updateEvents(Session*sess,PressureInfo & info);
    void updateTimesValues(qint64 d1,qint64 d2, int key,PressureInfo & info);
    void updateEventsChannel(Session * sess,ChannelID id, QVector<int> &background, PressureInfo & info );
    void updateFlagData(int &currentLoc, int & currentEL,int& currentData,qint64 eventTime, QVector<int> &dataArray, PressureInfo & info ) ;
    void updateSpanData(int &currentLoc, int & currentEL,int& currentData,qint64 startSpan, qint64 eventTime , QVector<int> &dataArray, PressureInfo & info ) ;

};



class MinutesAtPressure:public Layer
{
    friend class RecalcMAP;
#ifdef UNITTEST_MODE
    friend class MinutesAtPressureTests;
#endif
public:
    MinutesAtPressure();
    //! \brief Stops any background calculation and waits for it to end.
    virtual ~MinutesAtPressure();

    /*! \brief Start totalling the time range the graph's view is showing.

        For a printed report the totals are ready when this returns. Otherwise the work is
        done in the background and the view is redrawn when it finishes; a calculation that
        is still under way for an earlier range is abandoned.
      */
    virtual void recalculate(gGraph * graph);

    //! \brief Change the day. No background calculation is reading the old day once this returns.
    virtual void SetDay(Day *d);

    //! \brief True while a background calculation is queued or running.
    bool recalculating();

    virtual bool isEmpty();
    virtual int minimumHeight();

    //! Draw filled rectangles behind Event Flag's, and an outlines around them all, Calls the individual paint for each gFlagLine
    virtual void paint(QPainter &painter, gGraph &w, const QRegion &region);

    bool mouseMoveEvent(QMouseEvent *event, gGraph *graph);
    bool mousePressEvent(QMouseEvent *event, gGraph *graph);
    bool mouseReleaseEvent(QMouseEvent *event, gGraph *graph);

    virtual Layer * Clone() {
        MinutesAtPressure * map = new MinutesAtPressure();
        Layer::CloneInto(map);
        CloneInto(map);
        return map;
    }


protected:

    int numCloned =0;
    //! \brief Copy this layer's state. The copy starts with no calculation of its own.
    void CloneInto(MinutesAtPressure * layer) {
        timelock.lock();
        layer->m_empty = m_empty;
        layer->m_minimum_height = m_minimum_height;
        layer->m_lastminx = m_lastminx;
        layer->m_lastmaxx = m_lastmaxx;
        layer->ipap = ipap;
        layer->epap = epap;
        layer->initialized = initialized;
        layer->numCloned=numCloned+1;

        timelock.unlock();
        layer->m_enabled = m_enabled;
    }
    bool isCLoned() {return numCloned!=0;};

    /*! \brief Abandon any calculation under way and start one for the layer's day.
        \param minTime      start of the time range, device time in ms
        \param maxTime      end of the time range, device time in ms
        \param view         graph view to redraw when the totals are ready, or nullptr
        \param synchronous  calculate on the calling thread; the totals are in place on return
      */
    void startRecalc(qint64 minTime, qint64 maxTime, gGraphView * view, bool synchronous);

    /*! \brief Tell every background calculation to stop.
        \param wait  also block until they have all ended. They stop at the next sample they
                     look at, so the wait is short.
      */
    void cancelRecalc(bool wait);

    //! \brief Called by a background job, on its own thread, as the last thing it does.
    void recalcEnded(RecalcMAP * job);

    //! \brief Time range the graph's view is showing, converted to device time.
    void selectionRange(gGraph * graph, qint64 & minTime, qint64 & maxTime);

    /*! Guards m_remap and m_jobsRunning. A job cannot end without taking this lock in
        recalcEnded(), so while it is held a non-null m_remap points to a live job. */
    QMutex m_jobLock;
    QWaitCondition m_jobsDone;      //!< Woken each time a background job ends
    RecalcMAP * m_remap;            //!< Newest background job; nullptr once it has ended or been told to quit
    int m_jobsRunning = 0;          //!< Background jobs started and not yet ended

    //! Guards ipap, epap and initialized, which a background job writes when it finishes.
    QMutex timelock;
    bool initialized=false;         //!< True once a calculation has delivered totals
    bool m_empty;
    int m_minimum_height;
    //QAtomicInteger<int> m_recalcCount;

private:

    PressureInfo epap, ipap;
    void setEnabled(gGraph &graph);
    QHash<ChannelID, bool > m_enabled;
    qint64 m_lastminx;
    qint64 m_lastmaxx;
    QPoint last_mouse=QPoint(0,0);

    //EventDataType m_last_height=0;     // re-calculate only when needed.
    //int m_last_peaktime=0;      // re-calculate only when needed.

    bool isEnabled(ChannelID id) ;
    QString topBarLabel;

};


class MapPainter
{
public:
    // environment - set in constructor
    QPainter& painter;
    gGraph& graph;
    QRectF& drawingRect;
    QRectF& boundingRect;
    EventDataType lineThickness;

    MapPainter(
                QPainter& painter,
                gGraph& graph,
                QRectF& drawingRect ,
                QRectF& boundingRect ) :
                painter(painter),
                graph(graph),
                drawingRect(drawingRect) ,
                boundingRect(boundingRect)  {
        lineThickness= AppSetting->lineThickness();
    };

    // mouse related
    int mouseOverKey;
    bool graphSelected;
    void setMouse(int mouseOverKey,bool graphSelected) {
        this->mouseOverKey=mouseOverKey;
        this->graphSelected=graphSelected;
    };

    // for all graphs horizonatal
    int startGraphBucket;
    int endGraphBucket;
    EventDataType pixelsPerBucket;
    EventDataType minpressure;
    int bucketsPerPressure;
    void setHorizontal( EventDataType minpressure, EventDataType maxpressure,EventDataType  pixelsPerBucket , int bucketsPerPressure, int catmullRomSplineNumberOfPoints) {
        this->startGraphBucket = minpressure*bucketsPerPressure;
        this->endGraphBucket = maxpressure*bucketsPerPressure;
        this->pixelsPerBucket = pixelsPerBucket;
        this->minpressure = minpressure;
        this->bucketsPerPressure = bucketsPerPressure;
        initCatmullRomSpline(pixelsPerBucket,catmullRomSplineNumberOfPoints);
    };

    void drawEvent();
    void drawSpanEvents();
    void drawEventTick();     


    //  Pen type for drawing - per graph
    QPen linePen;
    QPen pointSelectionPen;
    QPen pointEnhancePen;
    QPen tickPen;
    QPen tickEnhancePen;
    QPen tickEnhanceTransparentPen;

    //EventDataType bottom,top,height,left,right;

    schema::Channel* channel;
    schema::ChanType chanType;
    EventDataType yPixelsPerUnit;
    QVector<int> dataArray;
    int startBucket;
    int endBucket;
    void setChannelInfo(ChannelID id, QVector<int> dataArray, EventDataType yPixelsPerUnit ,int ,int) ;

    void initCatmullRomSpline(EventDataType pixelsPerBucket,int numberOfPoints);
    EventDataType catmullRomSplineIncrement, catmullRomSplineInterval;
    int catmullRomSplineNumberOfPoints;
    EventDataType catmullRomSplineXstep;    // based on pixelsPerBucket.

    void setPenColorAlpha(ChannelID channelId ,int opacity) ;
    void setPenColorAlpha(ChannelID channelId ) ;
    void drawPlot();
    EventDataType dataToYaxis(int value) ;
    EventDataType verifyYaxis(EventDataType value);
    void initCatmullRomSpline(int numberOfPoints);
    void drawPoint(bool fill,int xp, int yp);
    EventDataType drawSegment ( int i , EventDataType fromx,EventDataType fromy) ;

    EventDataType yPixelsPerMsec ;       
    EventDataType yPixelsPerEvent;
    EventDataType pixelsPerPressure;

    EventDataType  yPixelsPerStep ;
    EventDataType  yMinutesPerStep;
    EventDataType  peakMinutes;

    int singleCharWidth, textHeight;
    void calculatePeakY(int peaktime );
    int drawYaxis(int peaktime);

    void drawEventYaxis(EventDataType peakEvents,int widest_YAxis);
    void drawXaxis(int numberXaxisDivisions , int startGraphBucket , int endGraphBucket);

    void drawMetaData(QPoint& last_mouse , QString& topBarLabel,PressureInfo& ipap , PressureInfo& epap ,QHash<ChannelID, bool >& enabled , EventDataType  minpressure , EventDataType  maxpressure);


};

#endif // MINUTESATPRESSURE_H

