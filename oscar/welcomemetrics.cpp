/* Welcome page metrics
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "welcomemetrics.h"

#include <QDateTime>
#include <QLocale>
#include <cmath>
#include <functional>
#include <limits>

#include "SleepLib/profiles.h"
#include "SleepLib/day.h"
#include "SleepLib/machine.h"
#include "SleepLib/schema.h"
#include "uikit/formatting.h"

extern EventDataType calcAHI(QDate start, QDate end);

namespace {

constexpr int kShortWindow = 7;     //!< Nights in the short comparison window
constexpr int kLongWindow = 30;     //!< Nights in the long comparison window and the sparkline
const double kNoData = std::numeric_limits<double>::quiet_NaN();

//! \brief A metric's value for one night, or NaN when the night can't provide it
using NightValue = std::function<double(Day *)>;

//! \brief A metric's nightly values over the sparkline window, with window averages
struct Series {
    QVector<double> values;     //!< night - 30 ... night
    double avg7 = 0, avg30 = 0;
    int nights7 = 0, nights30 = 0;
};

/*! \brief Collects \a value for every night from \a night - 30 to \a night

    The averages are the mean over the nights *before* \a night that have CPAP data, the same
    windows the original Welcome page uses for its 7-day figures. */
Series collect(QDate night, const NightValue &value)
{
    Series s;
    s.values.reserve(kLongWindow + 1);
    double sum7 = 0, sum30 = 0;
    for (int back = kLongWindow; back >= 0; --back) {
        Day *day = p_profile->GetGoodDay(night.addDays(-back), MT_CPAP);
        double v = day ? value(day) : kNoData;
        s.values.append(v);
        if (back == 0 || std::isnan(v)) continue;
        sum30 += v;
        s.nights30++;
        if (back <= kShortWindow) {
            sum7 += v;
            s.nights7++;
        }
    }
    s.avg7 = s.nights7 ? sum7 / s.nights7 : 0;
    s.avg30 = s.nights30 ? sum30 / s.nights30 : 0;
    return s;
}

//! \brief Copies \a s into \a m and sets the night's comparison value
void applySeries(MetricData &m, const Series &s)
{
    m.series = s.values;
    m.night = s.values.isEmpty() ? 0 : s.values.last();
    if (std::isnan(m.night)) m.night = 0;
    m.avg7 = s.avg7;
    m.avg30 = s.avg30;
    m.nights7 = s.nights7;
    m.nights30 = s.nights30;
}

//! \brief Hours of CPAP use on \a day, or NaN when there are none (avoids dividing by zero)
double usedHours(Day *day)
{
    double h = day->hours(MT_CPAP);
    return h > 0 ? h : kNoData;
}

QString number(double v, int decimals)
{
    return QString::number(v, 'f', decimals);
}

//! \brief Text shared with the original Welcome page, so existing translations apply
QString welcomeText(const char *source)
{
    return QCoreApplication::translate("Welcome", source);
}

bool rdiMode()
{
    return p_profile->general->calculateRDI();
}

//! \brief Every AHI event on \a day (plus RERA when the RDI preference is on)
double eventTotal(Day *day)
{
    double c = day->count(AllAhiChannels);
    if (rdiMode()) c += day->count(CPAP_RERA);
    return c;
}

//! \brief How the Pressure card describes one night
struct PressureInfo {
    bool valid = false;
    double compared = 0;    //!< The value averages compare: the pressure, or IPAP for bi-level
    QString value;          //!< e.g. "10.4" or "7.1–11.3"
    QString unit;
    QString sub;            //!< The original Welcome page's sentence for this mode
    bool bilevel = false;
};

/*! \brief Describes \a day's pressure with the same mode rules as Welcome::GenerateCPAPHTML()

    The sentences are the original page's, so they keep their translations. */
PressureInfo pressureInfo(Day *day)
{
    PressureInfo p;
    CPAPMode mode = (CPAPMode)(int)day->settings_max(CPAP_Mode);
    ChannelID pressChanID = day->getPressureChannelID();
    if (pressChanID == NoChannel) return p;
    double perc = p_profile->general->prefCalcPercentile();

    ChannelID epapDataChanID = CPAP_EPAP;
    if (day->channelHasData(CPAP_EPAPSet)) epapDataChanID = CPAP_EPAPSet;
    if (day->channelHasData(CPAP_EEPAP)) epapDataChanID = CPAP_EEPAP;

    const QString units = schema::channel[pressChanID].units();
    p.unit = units;
    p.valid = true;

    auto range = [](double lo, double hi) { return number(lo, 1) + QString::fromUtf8("–") + number(hi, 1); };

    if (mode == MODE_CPAP) {
        // DreamStation ventilators report EPAP/IPAP data, but the setting is Pressure
        double pressure = day->settings_max(CPAP_Pressure);
        p.compared = pressure;
        p.value = number(pressure, 1);
        p.sub = welcomeText("Your CPAP device used a constant %1 %2 of air").arg(pressure).arg(units);
    } else if (mode == MODE_APAP) {
        double pressure = day->percentile(pressChanID, perc / 100.0);
        p.compared = pressure;
        p.value = number(pressure, 1);
        p.sub = welcomeText("Your pressure was under %1 %2 for %3% of the time.")
                    .arg(pressure).arg(units).arg(perc);
    } else if (mode == MODE_BILEVEL_FIXED) {
        double epap = day->settings_min(CPAP_EPAP);
        double ipap = day->settings_max(CPAP_IPAP);
        p.bilevel = true;
        p.compared = ipap;
        p.unit = schema::channel[CPAP_IPAP].units();
        p.value = range(epap, ipap);
        p.sub = welcomeText("Your device used a constant %1-%2 %3 of air.")
                    .arg(day->validPressure(epap)).arg(day->validPressure(ipap)).arg(p.unit);
    } else if (mode == MODE_BILEVEL_AUTO_FIXED_PS) {
        double ipap = day->percentile(pressChanID, perc / 100.0);
        double epap = day->percentile(epapDataChanID, perc / 100.0);
        p.bilevel = true;
        p.compared = ipap;
        p.value = range(epap, ipap);
        p.sub = welcomeText("Your device was under %1-%2 %3 for %4% of the time.")
                    .arg(epap).arg(ipap).arg(units).arg(perc);
    } else if (mode == MODE_ASV) {
        double ipap = day->percentile(pressChanID, perc / 100.0);
        double epap = qRound(10.0 * day->settings_wavg(CPAP_EPAP)) / 10.0;
        p.bilevel = true;
        p.compared = ipap;
        p.value = range(epap, ipap);
        p.sub = welcomeText("Your EPAP pressure fixed at %1 %2.")
                    .arg(epap).arg(schema::channel[epapDataChanID].units())
                + " " + welcomeText("Your IPAP pressure was under %1 %2 for %3% of the time.")
                    .arg(ipap).arg(units).arg(perc);
    } else if (mode == MODE_AVAPS) {
        double ipap = day->percentile(pressChanID, perc / 100.0);
        p.bilevel = true;
        p.compared = ipap;
        // iVAPS: EPAP is fixed when AutoEPAP is off, or a min/max range when it is on.
        if (day->settingExists(CPAP_EPAPHi)) {
            double epaplo = qRound(10.0 * day->settings_min(CPAP_EPAPLo)) / 10.0;
            double epaphi = qRound(10.0 * day->settings_max(CPAP_EPAPHi)) / 10.0;
            p.value = range(epaplo, ipap);
            p.sub = welcomeText("Your EPAP pressure ranged from %1 to %2 %3.")
                        .arg(epaplo).arg(epaphi).arg(schema::channel[epapDataChanID].units());
        } else {
            double epap = qRound(10.0 * day->settings_wavg(CPAP_EPAP)) / 10.0;
            p.value = range(epap, ipap);
            p.sub = welcomeText("Your EPAP pressure fixed at %1 %2.")
                        .arg(epap).arg(schema::channel[epapDataChanID].units());
        }
        p.sub += " " + welcomeText("Your IPAP pressure was under %1 %2 for %3% of the time.")
                           .arg(ipap).arg(units).arg(perc);
    } else if (mode == MODE_ASV_VARIABLE_EPAP || mode == MODE_BILEVEL_AUTO_VARIABLE_PS) {
        double ipap = day->percentile(pressChanID, perc / 100.0);
        double epap = day->percentile(epapDataChanID, perc / 100.0);
        p.bilevel = true;
        p.compared = ipap;
        p.value = range(epap, ipap);
        p.sub = welcomeText("Your EPAP pressure was under %1 %2 for %3% of the time.")
                    .arg(epap).arg(schema::channel[epapDataChanID].units()).arg(perc)
                + " " + welcomeText("Your IPAP pressure was under %1 %2 for %3% of the time.")
                    .arg(ipap).arg(units).arg(perc);
    } else if (mode == MODE_TRILEVEL_AUTO_VARIABLE_PDIFF) {
        double ipap = day->percentile(pressChanID, perc / 100.0);
        double eepap = day->percentile(epapDataChanID, perc / 100.0);
        p.bilevel = true;
        p.compared = ipap;
        p.value = range(eepap, ipap);
        p.sub = welcomeText("Your EEPAP pressure was under %1 %2 for %3% of the time.")
                    .arg(eepap).arg(schema::channel[epapDataChanID].units()).arg(perc)
                + " " + welcomeText("Your IPAP pressure was under %1 %2 for %3% of the time.")
                    .arg(ipap).arg(units).arg(perc);
    } else {
        p.valid = false;
    }
    return p;
}

//! \brief Flow limitation as Statistics reports it: events per hour, or the FLG percentile
double flowLimitation(Day *day, bool useEvents)
{
    if (useEvents) {
        return std::isnan(usedHours(day)) ? kNoData : day->calcIdx(CPAP_FlowLimit);
    }
    return day->calcPercentile(CPAP_FLG);
}

QString timeOfDay(qint64 ms)
{
    return QLocale::system().toString(QDateTime::fromMSecsSinceEpoch(ms).time(), QLocale::ShortFormat);
}

} // namespace


bool WelcomeMetrics::hasAnyData()
{
    if (!p_profile) return false;
    for (MachineType type : { MT_CPAP, MT_OXIMETER }) {
        for (Machine *mach : p_profile->GetMachines(type)) {
            if (mach->day.size() > 0) return true;
        }
    }
    return false;
}

NightSummary WelcomeMetrics::lastNight()
{
    NightSummary n;
    if (!p_profile) return n;
    QDate date = p_profile->LastDay(MT_CPAP);
    if (!date.isValid()) return n;
    Day *day = p_profile->GetDay(date, MT_CPAP);
    Machine *cpap = day ? day->machine(MT_CPAP) : nullptr;
    if (!cpap) return n;

    n.valid = true;
    n.date = date;
    n.deviceName = cpap->brand() + " " + cpap->model();
    n.pixmapPath = cpap->getPixmapPath();
    n.daysAgo = date.daysTo(QDate::currentDate());
    return n;
}

QVector<MetricData> WelcomeMetrics::cards(QDate night)
{
    QVector<MetricData> cards;
    if (!p_profile) return cards;
    Day *day = p_profile->GetDay(night, MT_CPAP);
    Machine *cpap = day ? day->machine(MT_CPAP) : nullptr;
    if (!cpap) return cards;

    const QDate start7 = night.addDays(-kShortWindow), start30 = night.addDays(-kLongWindow);
    const QDate end = night.addDays(-1);
    const bool rdi = rdiMode();

    // Time used
    {
        MetricData m;
        m.id = MetricId::TimeUsed;
        m.group = MetricGroup::Usage;
        m.better = UiKit::Better::Higher;
        m.title = tr("Time used");
        applySeries(m, collect(night, [](Day *d) { return usedHours(d); }));
        m.value = UiKit::formatHoursMinutes(m.night);
        qint64 first = day->first(MT_CPAP), last = day->last(MT_CPAP);
        if (first > 0 && last > first) {
            m.sub = timeOfDay(first) + QString::fromUtf8(" – ") + timeOfDay(last);
        }
        cards.append(m);
    }

    // AHI or RDI. Averages use calcAHI(), as the original page and Statistics do.
    {
        MetricData m;
        m.id = MetricId::Ahi;
        m.group = MetricGroup::Breathing;
        m.better = UiKit::Better::Lower;
        m.title = rdi ? STR_TR_RDI : STR_TR_AHI;
        m.unit = tr("events/hr");
        m.sub = rdi ? tr("Respiratory Disturbance Index") : tr("Apnea-Hypopnea Index");
        Series s = collect(night, [rdi](Day *d) {
            if (std::isnan(usedHours(d))) return kNoData;
            return double(rdi ? d->calcRDI() : d->calcAHI());
        });
        if (s.nights7) s.avg7 = calcAHI(start7, end);
        if (s.nights30) s.avg30 = calcAHI(start30, end);
        applySeries(m, s);
        m.value = number(m.night, 2);
        cards.append(m);
    }

    // Events, with the per-channel breakdown
    bool hasAhiChannel = false;
    for (ChannelID ch : ahiChannels) {
        if (cpap->hasChannel(ch)) { hasAhiChannel = true; break; }
    }
    if (hasAhiChannel) {
        MetricData m;
        m.id = MetricId::Events;
        m.group = MetricGroup::Breathing;
        m.better = UiKit::Better::Lower;
        m.title = tr("Events");
        m.unit = tr("total");
        applySeries(m, collect(night, [](Day *d) { return eventTotal(d); }));
        m.value = QString::number(qRound(m.night));
        QVector<ChannelID> channels = ahiChannels;
        if (rdi) channels.append(CPAP_RERA);
        for (ChannelID ch : channels) {
            int c = qRound(day->count(ch));
            if (c > 0) {
                m.events.append({ schema::channel[ch].defaultColor(), c, schema::channel[ch].label() });
            }
        }
        cards.append(m);
    }

    // Pressure
    PressureInfo pressure = pressureInfo(day);
    if (pressure.valid) {
        MetricData m;
        m.id = MetricId::Pressure;
        m.group = MetricGroup::Mask;
        m.better = UiKit::Better::Neutral;
        m.title = tr("Pressure");
        applySeries(m, collect(night, [](Day *d) {
            PressureInfo p = pressureInfo(d);
            return p.valid ? p.compared : kNoData;
        }));
        m.value = pressure.value;
        m.unit = pressure.unit;
        m.sub = pressure.sub;
        if (pressure.bilevel) m.sub += " " + tr("Averages compare IPAP.");
        cards.append(m);
    }

    // Leak rate. Averages use the weighted average, as the original page does.
    if (cpap->hasChannel(CPAP_Leak)) {
        MetricData m;
        m.id = MetricId::Leak;
        m.group = MetricGroup::Mask;
        m.better = UiKit::Better::Lower;
        m.title = tr("Leak rate");
        m.unit = schema::channel[CPAP_Leak].units();
        Series s = collect(night, [](Day *d) { return double(d->wavg(CPAP_Leak)); });
        if (s.nights7) s.avg7 = p_profile->calcWavg(CPAP_Leak, MT_CPAP, start7, end);
        if (s.nights30) s.avg30 = p_profile->calcWavg(CPAP_Leak, MT_CPAP, start30, end);
        applySeries(m, s);
        m.value = number(m.night, 2);
        m.sub = tr("Average · %1 %2").arg(Day::calcPercentileLabel(CPAP_Leak))
                    .arg(number(day->calcPercentile(CPAP_Leak), 1));
        cards.append(m);
    }

    // Large leaks, compared as percent of the night (as Statistics reports them)
    if (cpap->hasChannel(CPAP_LargeLeak)) {
        MetricData m;
        m.id = MetricId::LargeLeak;
        m.group = MetricGroup::Mask;
        m.better = UiKit::Better::Lower;
        m.title = tr("Large leaks");
        applySeries(m, collect(night, [](Day *d) {
            return std::isnan(usedHours(d)) ? kNoData : double(d->calcPON(CPAP_LargeLeak));
        }));
        m.value = UiKit::formatMinutesSeconds(day->sum(CPAP_LargeLeak));
        int episodes = qRound(day->count(CPAP_LargeLeak));
        m.sub = tr("%n episode(s)", "", episodes) + QString::fromUtf8(" · ")
                + tr("%1% of the night").arg(number(m.night, 1));
        cards.append(m);
    }

    // Flow limitation, as Statistics reports it
    bool flEvents = cpap->hasChannel(CPAP_FlowLimit);
    if (flEvents || cpap->hasChannel(CPAP_FLG)) {
        MetricData m;
        m.id = MetricId::FlowLimit;
        m.group = MetricGroup::Breathing;
        m.better = UiKit::Better::Lower;
        m.title = tr("Flow limitation");
        applySeries(m, collect(night, [flEvents](Day *d) { return flowLimitation(d, flEvents); }));
        m.value = number(m.night, 2);
        if (flEvents) {
            m.unit = tr("events/hr");
            m.sub = tr("Flow limitation events per hour");
        } else {
            m.sub = tr("%1 of the flow limitation signal").arg(Day::calcPercentileLabel(CPAP_FLG));
        }
        cards.append(m);
    }

    // Sessions
    {
        MetricData m;
        m.id = MetricId::Sessions;
        m.group = MetricGroup::Usage;
        m.better = UiKit::Better::Neutral;
        m.title = tr("Sessions");
        m.unit = tr("mask on/off");
        applySeries(m, collect(night, [](Day *d) { return double(d->getSessions(MT_CPAP).size()); }));
        m.value = QString::number(qRound(m.night));
        double longest = 0;
        for (Session *sess : day->getSessions(MT_CPAP)) {
            longest = qMax(longest, double(sess->hours()));
        }
        if (longest > 0) m.sub = tr("Longest %1").arg(UiKit::formatHoursMinutes(longest));
        cards.append(m);
    }

    return cards;
}

OximetrySummary WelcomeMetrics::oximetry(QDate night)
{
    OximetrySummary o;
    if (!p_profile) return o;
    // Same test as Daily's calendar: an oximeter day, or a CPAP day with its own SpO2/pulse
    Day *day = p_profile->GetDay(night, MT_OXIMETER);
    if (!day) day = p_profile->GetDay(night, MT_CPAP);
    if (!day) return o;

    o.hasSpo2 = day->channelHasData(OXI_SPO2);
    o.hasPulse = day->channelHasData(OXI_Pulse);
    o.present = o.hasSpo2 || o.hasPulse;
    if (!o.present) return o;

    if (o.hasSpo2) {
        o.spo2Middle = day->calcMiddle(OXI_SPO2);
        o.spo2Min = day->Min(OXI_SPO2);
    }
    if (o.hasPulse) {
        o.pulseMiddle = day->calcMiddle(OXI_Pulse);
    }
    o.middleLabel = Day::calcMiddleLabel(OXI_SPO2);
    return o;
}
