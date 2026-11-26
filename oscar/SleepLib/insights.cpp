/* Therapy Insights Calculations
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QDateTime>
#include <QRandomGenerator>

#include <algorithm>

#include "insights.h"
#include "day.h"
#include "machine.h"
#include "performance_timer.h"
#include "profiles.h"
#include "session.h"

namespace Insights {

namespace {

//! \brief Events / hours over \a nights; 0 when there are no hours.
double pooledAhi(const QVector<NightCount> &nights)
{
    double events = 0, hours = 0;
    for (const NightCount &n : nights) {
        events += n.events;
        hours += n.hours;
    }
    return hours > 0 ? events / hours : 0;
}

//! \brief Pooled AHI of a same-size resample of \a nights, drawn with replacement.
double resampledAhi(const QVector<NightCount> &nights, QRandomGenerator &rng)
{
    double events = 0, hours = 0;
    for (int i = 0; i < nights.size(); ++i) {
        const NightCount &n = nights[rng.bounded(int(nights.size()))];
        events += n.events;
        hours += n.hours;
    }
    return hours > 0 ? events / hours : 0;
}

//! \brief Pressure channels for an auto mode; all 0 for fixed-pressure modes.
struct AutoChannels {
    ChannelID binBy = 0;        //!< pressure that events are binned by
    ChannelID atMax = 0;        //!< pressure compared with the max setting
    ChannelID maxSetting = 0;
};

AutoChannels autoChannels(Session *sess)
{
    switch (sess->settings.value(CPAP_Mode).toInt()) {
    case MODE_APAP:
        return {CPAP_Pressure, CPAP_Pressure, CPAP_PressureMax};
    case MODE_BILEVEL_AUTO_FIXED_PS:
    case MODE_BILEVEL_AUTO_VARIABLE_PS:
        return {CPAP_EPAP, CPAP_IPAP, CPAP_IPAPHi};
    default:
        return {};
    }
}

//! \brief Value of \a code at raw device time \a t: the sample or step covering it.
//! Returns false when no list of \a code has an entry covering \a t.
bool valueAt(Session *sess, ChannelID code, qint64 t, EventDataType &value)
{
    // A session can hold several lists for one channel (one per data chunk). Use the first
    // whose time span contains t.
    for (EventList *el : sess->eventlist.value(code)) {
        if (el->count() == 0 || t < el->first() || t >= el->last()) { continue; }
        int i;
        if (el->type() == EVL_Waveform) {
            // Evenly spaced samples, rate() ms apart: the index follows from the elapsed time.
            if (el->rate() <= 0) { continue; }
            i = qMin(int((t - el->first()) / el->rate()), int(el->count()) - 1);
        } else {
            // One entry per change, each holding until the next: take the last change <= t.
            i = 0;
            while (i + 1 < int(el->count()) && el->time(i + 1) <= t) { ++i; }
        }
        // Don't rely on first()/last() alone: the chosen entry must itself start by t.
        if (el->time(i) > t) { continue; }
        value = el->data(i);
        return true;
    }
    return false;
}

} // namespace

TimeAtMax timeAtMaxPressure(const QList<Day *> &days)
{
    PERF_TIMER_SCOPE("Insights::timeAtMaxPressure()");
    double minutesAtMax = 0, minutes = 0;
    for (Day *day : days) {
        for (Session *sess : day->getSessions(MT_CPAP)) {
            const AutoChannels ch = autoChannels(sess);
            if (!ch.atMax || !sess->settings.contains(ch.maxSetting)) { continue; }
            // ResMed stores a bilevel's IPAP as CPAP_Pressure; Day::getPressureChannelID()
            // falls back the same way.
            const ChannelID atMax = sess->channelExists(ch.atMax) ? ch.atMax : CPAP_Pressure;
            if (!sess->channelExists(atMax)) { continue; }
            const double max = sess->settings.value(ch.maxSetting).toDouble();
            minutesAtMax += sess->timeAboveThreshold(atMax, max - kAtMaxToleranceCmH2O);
            minutes += sess->hours() * 60;
        }
    }
    TimeAtMax r;
    r.valid = minutes > 0;
    r.pct = r.valid ? 100 * minutesAtMax / minutes : 0;
    return r;
}

EventsAtPressure eventsAtPressure(const QList<Day *> &days)
{
    PERF_TIMER_SCOPE("Insights::eventsAtPressure()");
    static const ChannelID obstructive[] = {CPAP_Obstructive, CPAP_Hypopnea,
                                            CPAP_ObstructiveHypopnea, CPAP_Apnea};
    EventsAtPressure r;
    for (Day *day : days) {
        for (Session *sess : day->getSessions(MT_CPAP)) {
            const AutoChannels ch = autoChannels(sess);
            if (!ch.binBy) { continue; }
            const bool loaded = sess->eventsLoaded();
            if (!loaded) { sess->OpenEvents(); }

            // Time in each bin: every sample or step lasts until the next one.
            for (EventList *el : sess->eventlist.value(ch.binBy)) {
                for (quint32 i = 0; i < el->count(); ++i) {
                    const qint64 end = i + 1 < el->count() ? el->time(i + 1) : el->last();
                    r.bins[qRound(el->data(i))].hours += (end - el->time(i)) / 3600000.0;
                    r.valid = true;
                }
            }
            for (ChannelID code : obstructive) {
                for (EventList *el : sess->eventlist.value(code)) {
                    for (quint32 i = 0; i < el->count(); ++i) {
                        const qint64 onset = el->time(i) - qint64(el->data(i) * 1000);
                        EventDataType p;
                        if (valueAt(sess, ch.binBy, onset, p)) { r.bins[qRound(p)].events++; }
                    }
                }
            }
            if (!loaded) { sess->TrashEvents(); }
        }
    }
    return r;
}

QualityFlags qualityFlags(Day *day)
{
    QualityFlags f;
    const QList<Session *> sessions = day->getSessions(MT_CPAP);
    if (sessions.isEmpty()) { return f; }
    const double hours = day->hours(MT_CPAP);
    f.sessions = sessions.size();
    f.fragmented = f.sessions > kMaxSessions;
    f.shortNight = hours < p_profile->cpap->complianceHours();
    // Any session without detailed data leaves the night's event-based figures incomplete.
    f.summaryOnly = std::any_of(sessions.cbegin(), sessions.cend(),
                                [](Session *s) { return s->summaryOnly(); });

    // Two sources for large leak, best first:
    // 1. The machine flags large leak itself (Philips, IntelliPAP): use its share of the night.
    // 2. Otherwise (ResMed and others), time with the leak rate above the leak redline set in
    //    Preferences; a redline of 0 means none is set.
    // With neither, leakKnown stays false and the night isn't flagged for leak.
    Machine *cpap = day->machine(MT_CPAP);
    const double redline = p_profile->cpap->leakRedline();
    if (hours > 0) {
        if (cpap && cpap->hasReportedEvents(CPAP_LargeLeak)) {
            f.leakKnown = true;
            f.largeLeakPct = day->calcPON(CPAP_LargeLeak);
        } else if (redline > 0 && day->channelHasData(CPAP_Leak)) {
            f.leakKnown = true;
            f.largeLeakPct = 100 * day->timeAboveThreshold(CPAP_Leak, redline) / (hours * 60);
        }
    }
    f.largeLeak = f.leakKnown && f.largeLeakPct > kLargeLeakPct;
    return f;
}

MedicareCheck medicareFirst90Days(const QMap<QDate, Day *> &days)
{
    MedicareCheck r;
    QMap<QDate, EventDataType> hours;
    for (auto it = days.cbegin(); it != days.cend(); ++it) {
        const EventDataType h = it.value()->hours(MT_CPAP);
        if (h > 0) { hours[it.key()] = h; }
    }
    if (hours.isEmpty()) { return r; }
    r.valid = true;
    r.firstNight = hours.firstKey();
    r.complete = hours.lastKey() >= r.firstNight.addDays(89);

    // A night of exactly 4 h counts. Day::hours() is whole milliseconds / 3,600,000, so 4 h
    // is exactly 4.0f and needs no tolerance.
    // Slide a 30-night window over the first 90 days (day 0 = first CPAP night; 61 windows,
    // starting on days 0 to 60). Days without CPAP count as missed. Keep the window with the
    // most nights of 4 h or more; on a tie, the earliest.
    for (int start = 0; start + 30 <= 90; ++start) {
        int n = 0;
        for (int i = 0; i < 30; ++i) {
            n += hours.value(r.firstNight.addDays(start + i)) >= 4.0f;
        }
        if (n > r.bestNights) {
            r.bestNights = n;
            r.bestFirstNight = r.firstNight.addDays(start);
        }
    }
    r.met = r.bestNights >= 21;
    if (!r.met) { r.bestFirstNight = QDate(); }
    return r;
}

UsagePattern usagePattern(const QList<Day *> &days)
{
    const QTime split = p_profile->session->daySplitTime();
    QVector<int> offsets;       // minutes after the day split
    QVector<int> sessions;
    for (Day *day : days) {
        const qint64 last = day->last(MT_CPAP);
        if (!last || day->hours(MT_CPAP) <= 0) { continue; }
        const QTime t = QDateTime::fromMSecsSinceEpoch(last).time();
        offsets << (split.secsTo(t) / 60 + 1440) % 1440;
        sessions << day->getSessions(MT_CPAP).size();
    }
    // Each offset is the mask-off time in minutes after the day split, wrapped into 0..1439,
    // so 01:30 and 23:30 sit 2 h apart rather than 22 h. Clock times can then be sorted like
    // plain numbers, and the median converts back to a clock time at the end.
    UsagePattern r;
    if (offsets.isEmpty()) { return r; }
    std::sort(offsets.begin(), offsets.end());
    std::sort(sessions.begin(), sessions.end());
    const int n = offsets.size();
    // An even count averages the middle pair.
    auto median = [](const QVector<int> &v) {
        const int m = v.size();
        return m % 2 ? v[m / 2] : (v[m / 2 - 1] + v[m / 2]) / 2.0;
    };
    r.valid = true;
    r.medianMaskOff = split.addSecs(qRound(median(offsets) * 60));
    // Spread = interquartile range: the middle half of nights fall within this many minutes.
    // Quartiles are taken by rank without interpolation, which is close enough for a summary.
    r.maskOffSpreadMinutes = offsets[3 * n / 4] - offsets[n / 4];
    r.medianSessions = median(sessions);
    return r;
}

QList<int> topPressureBins(const EventsAtPressure &eap, int n)
{
    QList<int> keys;
    for (auto it = eap.bins.cbegin(); it != eap.bins.cend(); ++it) {
        if (it->hours >= kMinBinHours && it->events > 0) { keys << it.key(); }
    }
    std::stable_sort(keys.begin(), keys.end(), [&eap](int a, int b) {
        return eap.bins[a].events / eap.bins[a].hours > eap.bins[b].events / eap.bins[b].hours;
    });
    return keys.mid(0, n);
}

RxComparison compareRxPeriods(const QVector<NightCount> &before,
                              const QVector<NightCount> &after, int minNights, quint32 seed)
{
    RxComparison r;
    if (before.size() < minNights || after.size() < minNights) { return r; }
    r.ahiBefore = pooledAhi(before);
    r.ahiAfter = pooledAhi(after);

    // The AHIs above use every night. To tell a real change from ordinary night-to-night ups
    // and downs, rebuild each period 1,000 times from randomly drawn nights (a night can be
    // drawn more than once) and take the middle 95% of the differences as the interval.
    // Nightly AHI isn't bell-shaped, so this is safer than a textbook formula. The fixed seed
    // keeps the report the same on every run.
    const int reps = 1000;
    QRandomGenerator rng(seed);
    QVector<double> diffs(reps);
    for (double &d : diffs) { d = resampledAhi(after, rng) - resampledAhi(before, rng); }
    std::sort(diffs.begin(), diffs.end());
    r.ciLow = diffs[reps * 25 / 1000];
    r.ciHigh = diffs[reps * 975 / 1000 - 1];

    r.valid = true;
    r.label = r.ciHigh < 0 ? ChangeLabel::Lower
            : r.ciLow > 0  ? ChangeLabel::Higher
                           : ChangeLabel::Within;
    return r;
}

} // namespace Insights
