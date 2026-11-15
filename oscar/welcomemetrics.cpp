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
constexpr int kLongWindow = 30;     //!< Nights in the long comparison window
const double kNoData = std::numeric_limits<double>::quiet_NaN();

//! \brief A metric's value for one night, or NaN when the night can't provide it
using NightValue = std::function<double(Day *)>;

//! \brief A metric's nightly values over the long window, with window averages
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

//! \brief What every card builder gets: the night and the comparison windows
struct CardContext {
    QDate night;            //!< The night shown
    Day *day = nullptr;     //!< Its CPAP day
    Machine *cpap = nullptr;
    QDate start7, start30;  //!< First nights of the 7- and 30-night windows
    QDate end;              //!< Last night of both windows (the night before)
    bool rdi = false;       //!< RDI preference on
};

/*! \brief Fills a card's data for one night
    \return false when the device doesn't record this metric, so the card isn't shown.
    The card's id and title are already set from the card table. */
using CardBuilder = bool (*)(const CardContext &ctx, MetricData &m);

bool buildTimeUsed(const CardContext &ctx, MetricData &m)
{
    m.group = MetricGroup::Usage;
    m.better = UiKit::Better::Higher;
    applySeries(m, collect(ctx.night, [](Day *d) { return usedHours(d); }));
    m.value = UiKit::formatHoursMinutes(m.night);
    qint64 first = ctx.day->first(MT_CPAP), last = ctx.day->last(MT_CPAP);
    if (first > 0 && last > first) {
        m.sub = timeOfDay(first) + QString::fromUtf8(" – ") + timeOfDay(last);
    }
    return true;
}

//! \brief AHI or RDI. Averages use calcAHI(), as the original page and Statistics do.
bool buildAhi(const CardContext &ctx, MetricData &m)
{
    const bool rdi = ctx.rdi;
    m.group = MetricGroup::Breathing;
    m.better = UiKit::Better::Lower;
    m.unit = WelcomeMetrics::tr("events/hr");
    m.sub = rdi ? WelcomeMetrics::tr("Respiratory Disturbance Index") : WelcomeMetrics::tr("Apnea-Hypopnea Index");
    Series s = collect(ctx.night, [rdi](Day *d) {
        if (std::isnan(usedHours(d))) return kNoData;
        return double(rdi ? d->calcRDI() : d->calcAHI());
    });
    if (s.nights7) s.avg7 = calcAHI(ctx.start7, ctx.end);
    if (s.nights30) s.avg30 = calcAHI(ctx.start30, ctx.end);
    applySeries(m, s);
    m.value = number(m.night, 2);
    return true;
}

//! \brief Event total, with the per-channel breakdown
bool buildEvents(const CardContext &ctx, MetricData &m)
{
    bool hasAhiChannel = false;
    for (ChannelID ch : ahiChannels) {
        if (ctx.cpap->hasChannel(ch)) { hasAhiChannel = true; break; }
    }
    if (!hasAhiChannel) return false;

    m.group = MetricGroup::Breathing;
    m.better = UiKit::Better::Lower;
    m.unit = WelcomeMetrics::tr("total");
    applySeries(m, collect(ctx.night, [](Day *d) { return eventTotal(d); }));
    m.value = QString::number(qRound(m.night));
    QVector<ChannelID> channels = ahiChannels;
    if (ctx.rdi) channels.append(CPAP_RERA);
    for (ChannelID ch : channels) {
        int c = qRound(ctx.day->count(ch));
        if (c > 0) {
            m.events.append({ schema::channel[ch].defaultColor(), c, schema::channel[ch].label() });
        }
    }
    return true;
}

bool buildPressure(const CardContext &ctx, MetricData &m)
{
    PressureInfo pressure = pressureInfo(ctx.day);
    if (!pressure.valid) return false;

    m.group = MetricGroup::Mask;
    m.better = UiKit::Better::Neutral;
    applySeries(m, collect(ctx.night, [](Day *d) {
        PressureInfo p = pressureInfo(d);
        return p.valid ? p.compared : kNoData;
    }));
    m.value = pressure.value;
    m.unit = pressure.unit;
    m.sub = pressure.sub;
    if (pressure.bilevel) m.sub += " " + WelcomeMetrics::tr("Averages compare IPAP.");
    return true;
}

//! \brief Leak rate. Averages use the weighted average, as the original page does.
bool buildLeak(const CardContext &ctx, MetricData &m)
{
    if (!ctx.cpap->hasChannel(CPAP_Leak)) return false;

    m.group = MetricGroup::Mask;
    m.better = UiKit::Better::Lower;
    m.unit = schema::channel[CPAP_Leak].units();
    Series s = collect(ctx.night, [](Day *d) { return double(d->wavg(CPAP_Leak)); });
    if (s.nights7) s.avg7 = p_profile->calcWavg(CPAP_Leak, MT_CPAP, ctx.start7, ctx.end);
    if (s.nights30) s.avg30 = p_profile->calcWavg(CPAP_Leak, MT_CPAP, ctx.start30, ctx.end);
    applySeries(m, s);
    m.value = number(m.night, 2);
    m.sub = WelcomeMetrics::tr("Average · %1 %2").arg(Day::calcPercentileLabel(CPAP_Leak))
                .arg(number(ctx.day->calcPercentile(CPAP_Leak), 1));
    return true;
}

//! \brief Large leaks, compared as percent of the night (as Statistics reports them)
bool buildLargeLeak(const CardContext &ctx, MetricData &m)
{
    if (!ctx.cpap->hasChannel(CPAP_LargeLeak)) return false;

    m.group = MetricGroup::Mask;
    m.better = UiKit::Better::Lower;
    applySeries(m, collect(ctx.night, [](Day *d) {
        return std::isnan(usedHours(d)) ? kNoData : double(d->calcPON(CPAP_LargeLeak));
    }));
    m.value = UiKit::formatMinutesSeconds(ctx.day->sum(CPAP_LargeLeak));
    int episodes = qRound(ctx.day->count(CPAP_LargeLeak));
    m.sub = WelcomeMetrics::tr("%n episode(s)", "", episodes) + QString::fromUtf8(" · ")
            + WelcomeMetrics::tr("%1% of the night").arg(number(m.night, 1));
    return true;
}

//! \brief Flow limitation, as Statistics reports it
bool buildFlowLimit(const CardContext &ctx, MetricData &m)
{
    const bool flEvents = ctx.cpap->hasChannel(CPAP_FlowLimit);
    if (!flEvents && !ctx.cpap->hasChannel(CPAP_FLG)) return false;

    m.group = MetricGroup::Breathing;
    m.better = UiKit::Better::Lower;
    applySeries(m, collect(ctx.night, [flEvents](Day *d) { return flowLimitation(d, flEvents); }));
    m.value = number(m.night, 2);
    if (flEvents) {
        m.unit = WelcomeMetrics::tr("events/hr");
        m.sub = WelcomeMetrics::tr("Flow limitation events per hour");
    } else {
        m.sub = WelcomeMetrics::tr("%1 of the flow limitation signal").arg(Day::calcPercentileLabel(CPAP_FLG));
    }
    return true;
}

bool buildSessions(const CardContext &ctx, MetricData &m)
{
    m.group = MetricGroup::Usage;
    m.better = UiKit::Better::Neutral;
    m.unit = WelcomeMetrics::tr("mask on/off");
    applySeries(m, collect(ctx.night, [](Day *d) { return double(d->getSessions(MT_CPAP).size()); }));
    m.value = QString::number(qRound(m.night));
    double longest = 0;
    for (Session *sess : ctx.day->getSessions(MT_CPAP)) {
        longest = qMax(longest, double(sess->hours()));
    }
    if (longest > 0) m.sub = WelcomeMetrics::tr("Longest %1").arg(UiKit::formatHoursMinutes(longest));
    return true;
}

/*! \brief One row of the card table
    \a key is stored in the profile, so never change an existing key. */
struct CardDef {
    MetricId id;
    const char *key;
    QString (*title)();
    CardBuilder build;      //!< nullptr for the oximetry card, which WelcomeMetrics::oximetry() fills
};

/*! \brief Every card the page can show, in display order

    Adding a card: add a MetricId, write a CardBuilder above, and add a row here. The card
    chooser lists it and new cards are shown until the user hides them. */
const QVector<CardDef> &cardTable()
{
    static const QVector<CardDef> table = {
        { MetricId::TimeUsed, "time_used", [] { return WelcomeMetrics::tr("Time used"); }, buildTimeUsed },
        { MetricId::Ahi, "ahi", [] { return (p_profile && rdiMode()) ? STR_TR_RDI : STR_TR_AHI; }, buildAhi },
        { MetricId::Events, "events", [] { return WelcomeMetrics::tr("Events"); }, buildEvents },
        { MetricId::Pressure, "pressure", [] { return WelcomeMetrics::tr("Pressure"); }, buildPressure },
        { MetricId::Leak, "leak", [] { return WelcomeMetrics::tr("Leak rate"); }, buildLeak },
        { MetricId::LargeLeak, "large_leak", [] { return WelcomeMetrics::tr("Large leaks"); }, buildLargeLeak },
        { MetricId::FlowLimit, "flow_limit", [] { return WelcomeMetrics::tr("Flow limitation"); }, buildFlowLimit },
        { MetricId::Sessions, "sessions", [] { return WelcomeMetrics::tr("Sessions"); }, buildSessions },
        { MetricId::Oximetry, "oximetry", [] { return WelcomeMetrics::tr("Oximetry"); }, nullptr },
    };
    return table;
}

//! \brief Profile preference holding the hidden cards; also listed in PreferencesRepository::saveUserSettings()
const QString kHiddenCardsPref = QStringLiteral("WelcomeHiddenCards");

} // namespace


QVector<CardInfo> WelcomeMetrics::catalog()
{
    QVector<CardInfo> list;
    for (const CardDef &def : cardTable()) {
        list.append({ def.id, QString::fromLatin1(def.key), def.title() });
    }
    return list;
}

QString WelcomeMetrics::cardKey(MetricId id)
{
    for (const CardDef &def : cardTable()) {
        if (def.id == id) return QString::fromLatin1(def.key);
    }
    return QString();
}

QString WelcomeMetrics::cardTitle(MetricId id)
{
    for (const CardDef &def : cardTable()) {
        if (def.id == id) return def.title();
    }
    return QString();
}

QSet<QString> WelcomeMetrics::hiddenCards()
{
    if (!p_profile || !p_profile->general) return {};
    Preferences *pref = p_profile->general->m_pref;
    if (!pref->contains(kHiddenCardsPref)) return {};
    return parseCardKeys((*pref)[kHiddenCardsPref].toString());
}

void WelcomeMetrics::setHiddenCards(const QSet<QString> &hidden)
{
    if (!p_profile || !p_profile->general) return;
    p_profile->general->setPref(kHiddenCardsPref, joinCardKeys(hidden));
    p_profile->saveExtendedDataToDatabase();
}

QSet<QString> WelcomeMetrics::parseCardKeys(const QString &text)
{
    QSet<QString> keys;
    for (const QString &part : text.split(',', Qt::SkipEmptyParts)) {
        const QString key = part.trimmed();
        if (!key.isEmpty()) keys.insert(key);
    }
    return keys;
}

QString WelcomeMetrics::joinCardKeys(const QSet<QString> &keys)
{
    QStringList list(keys.begin(), keys.end());
    list.sort();
    return list.join(',');
}

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

QVector<MetricData> WelcomeMetrics::cards(QDate night, const QSet<QString> &hidden)
{
    QVector<MetricData> cards;
    if (!p_profile) return cards;
    CardContext ctx;
    ctx.night = night;
    ctx.day = p_profile->GetDay(night, MT_CPAP);
    ctx.cpap = ctx.day ? ctx.day->machine(MT_CPAP) : nullptr;
    if (!ctx.cpap) return cards;
    ctx.start7 = night.addDays(-kShortWindow);
    ctx.start30 = night.addDays(-kLongWindow);
    ctx.end = night.addDays(-1);
    ctx.rdi = rdiMode();

    for (const CardDef &def : cardTable()) {
        if (!def.build || hidden.contains(QString::fromLatin1(def.key))) continue;
        MetricData m;
        m.id = def.id;
        m.title = def.title();
        if (def.build(ctx, m)) cards.append(m);
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
