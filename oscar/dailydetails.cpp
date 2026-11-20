/* Daily details figures
 *
 * The figures were moved here from the HTML builders in daily.cpp (getAHI, getSleepTime,
 * getCPAPInformation, getIndices, getStatisticsInfo, getOximeterInformation, getViatomSummary,
 * getMachineSettings, getSessionInformation), which now format their HTML from these values.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "dailydetails.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

#include "SleepLib/day.h"
#include "SleepLib/machine.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"
#include "SleepLib/loader_plugins/applehealth_loader.h"
#include "SleepLib/loader_plugins/viatom_loader.h"

extern ChannelID PRS1_PeakFlow;
extern ChannelID WM_rRMV;
extern ChannelID Prisma_ObstructLevel, Prisma_rMVFluctuation, Prisma_rRMV, Prisma_PressureMeasured, Prisma_FlowFull;

namespace DailyDetails {

Headline headline(Day *day)
{
    Headline h;
    if (!day) return h;
    const float hours = day->hours(MT_CPAP);
    Machine *cpap = day->machine(MT_CPAP);
    h.present = cpap && hours > 0;

    EventDataType ahi = day->count(AllAhiChannels);
    if (p_profile->general->calculateRDI()) ahi += day->count(CPAP_RERA);
    ahi /= hours;
    h.value = ahi;

    ChannelID ahichan = CPAP_AHI;
    h.name = STR_TR_AHI;
    if (p_profile->general->calculateRDI()) {
        ahichan = CPAP_RDI;
        h.name = STR_TR_RDI;
    }
    h.fullName = schema::channel[ahichan].fullname();

    // Obstructive and central breakdown of the same index, only for a device that scores
    // hypopneas by mechanism — for any other device OAHI would just restate AHI (GitLab #261).
    // The two always sum to AHI. RERA is excluded even in RDI mode, since an arousal is
    // neither obstructive nor central.
    if (hours > 0 && cpap && cpap->reportsHypopneaMechanism()) {
        h.hasShares = true;
        h.obstructive = day->calcOAHI();
        h.central = day->calcCAHI();
    }
    return h;
}

Usage usage(Day *day)
{
    Usage u;
    if (!day || (day->hours() < 0.0000001)) return u;
    u.present = true;
    u.seconds = qint64(day->total_time(MT_CPAP)) / 1000L;
    u.start = QDateTime::fromSecsSinceEpoch(day->first() / 1000L);
    u.end = QDateTime::fromSecsSinceEpoch(day->last() / 1000L);
    for (Session *sess : day->getSessions(MT_CPAP)) {
        if (sess->enabled()) ++u.cpapSessions;
    }
    return u;
}

Device device(Day *day)
{
    Device d;
    if (!day) return d;
    Machine *cpap = day->machine(MT_CPAP);
    if (!cpap) return d;
    const MachineInfo info = cpap->getInfo();
    d.present = true;
    d.brand = info.brand;
    d.model = info.model;
    d.tooltip = QCoreApplication::translate("Daily", "Model %1 - %2").arg(info.modelnumber).arg(info.serial);
    d.mode = day->getCPAPModeStr();
    d.pressure = day->getPressureSettings();
    d.settingsMissing = day->noSettings(cpap);
    return d;
}

//! \brief A row of the Events list that isn't a channel's index (Apple Watch figures, Unmasked Sleep)
static EventRow extraRow(const QString &name, const QString &rate, const QString &tooltip, const QColor &colour)
{
    EventRow row;
    row.name = name;
    row.rate = rate;
    row.tooltip = tooltip;
    row.colour = colour;
    return row;
}

QVector<EventRow> events(Day *day)
{
    QVector<EventRow> rows;
    if (!day) return rows;
    float hours=day->hours(MT_CPAP);

    quint32 zchans = schema::SPAN | schema::FLAG;
    bool show_minors = false; // true;
    if (p_profile->general->showUnknownFlags()) zchans |= schema::UNKNOWN;

    if (show_minors) zchans |= schema::MINOR_FLAG;
    QList<ChannelID> available = day->getSortedMachineChannels(zchans);

    EventDataType val;
    for (int i=0; i < available.size(); ++i) {
        ChannelID code = available.at(i);
        schema::Channel & chan = schema::channel[code];
        QString data;
        if (
            ( code == CPAP_UserFlag1 || code == CPAP_UserFlag2) &&
            ( ( !p_profile->cpap->userEventFlagging()) ||  (p_profile->cpap->clinicalMode()))  ){
            continue;
        }
        float channelHours = hours;
        if (chan.machtype() != MT_CPAP) {
            // Use device type hours (if available) rather than CPAP hours, since
            // might have Oximetry (for example) longer or shorter than CPAP
            channelHours = day->hours(chan.machtype());
            if (channelHours <= 0) {
                channelHours = hours;
            }
        }
        QString count;
        if (chan.type() == schema::SPAN) {
            val = (100.0 / channelHours)*(day->sum(code)/3600.0);
            data = QString("%1%").arg(val,0,'f',2);
        } else if (code == CPAP_VSnore2) {  // TODO: This should be generalized rather than special-casing a single channel here.
            val = day->sum(code) / channelHours;
            data = QString("%1").arg(val,0,'f',2);
        } else {
            const EventDataType events = day->count(code);
            val = events / channelHours;
            data = QString("%1").arg(val,0,'f',2);
            // Summary-only counts can be fractional
            count = (events == std::floor(events)) ? QString::number(qint64(events)) : QString::number(events, 'f', 1);
        }
        // TODO: percentage would be another useful option here for things like
        // percentage of patient-triggered breaths, which is much more useful
        // than the duration of timed breaths per hour.
        EventRow row;
        row.code = code;
        row.name = chan.fullname();
        row.tooltip = chan.description();
        row.count = count;
        row.rate = data;
        row.colour = chan.defaultColor();
        row.link = true;
        row.value = val;
        rows.append(row);
    }

    if (day->settingExists(AW_BreathingDisturbances)) {
        schema::Channel & chan = schema::channel[AW_BreathingDisturbances];
        rows.append(extraRow(QCoreApplication::translate("Daily", "Breathing Disturbances (Apple)"),
                             QString("%1").arg(day->settings_wavg(AW_BreathingDisturbances),0,'f',2),
                             chan.description(), chan.defaultColor()));
    }
    if (day->settingExists(AW_WristTemp)) {
        schema::Channel & chan = schema::channel[AW_WristTemp];
        rows.append(extraRow(QCoreApplication::translate("Daily", "Wrist Temperature"),
                             QString("%1 %2").arg(day->settings_wavg(AW_WristTemp),0,'f',1).arg(chan.units()),
                             chan.description(), chan.defaultColor()));
    }

    struct Interval {
        qint64 start;
        qint64 end;
    };
    QVector<Interval> watchIntervals;
    QVector<Interval> maskIntervals;
    qint64 totalWatchSleep = 0;
    bool hasWatchSleep = false;
    bool hasCpap = false;
    // Sessions from different machines carry different clock corrections; compare in display time.
    for (Session *s : day->sessions) {
        if (!s->enabled()) {
            continue;
        }
        if (s->type() == MT_SLEEPSTAGE) {
            // Other stage loaders (Zeo, Dreem) share the machine type but aren't the watch.
            if (s->machine()->loaderName() != applehealth_class_name) {
                continue;
            }
            hasWatchSleep = true;
            const qint64 correction = s->correctionMs();
            for (const SessionSlice &slice : s->m_slices) {
                if (slice.end > slice.start) {
                    watchIntervals.append({slice.start + correction, slice.end + correction});
                    totalWatchSleep += slice.end - slice.start;
                }
            }
        } else if (s->type() == MT_CPAP) {
            hasCpap = true;
            const qint64 correction = s->correctionMs();
            if (s->m_slices.isEmpty()) {
                // No slices means the whole session was mask-on, as Session::hours() assumes.
                if (s->realLast() > s->realFirst()) {
                    maskIntervals.append({s->realFirst() + correction, s->realLast() + correction});
                }
            } else {
                for (const SessionSlice &slice : s->m_slices) {
                    if (slice.status == MaskOn && slice.end > slice.start) {
                        maskIntervals.append({slice.start + correction, slice.end + correction});
                    }
                }
            }
        }
    }

    if (hasWatchSleep && hasCpap && totalWatchSleep > 0) {
        std::sort(maskIntervals.begin(), maskIntervals.end(), [](const Interval &a, const Interval &b) {
            return a.start < b.start || (a.start == b.start && a.end < b.end);
        });
        QVector<Interval> mergedMaskIntervals;
        for (const Interval &interval : maskIntervals) {
            if (mergedMaskIntervals.isEmpty() || interval.start > mergedMaskIntervals.last().end) {
                mergedMaskIntervals.append(interval);
            } else {
                mergedMaskIntervals.last().end = std::max(mergedMaskIntervals.last().end, interval.end);
            }
        }

        qint64 maskedWatchSleep = 0;
        for (const Interval &watch : watchIntervals) {
            for (const Interval &mask : mergedMaskIntervals) {
                if (mask.end <= watch.start) continue;
                if (mask.start >= watch.end) break;
                maskedWatchSleep += std::min(watch.end, mask.end) - std::max(watch.start, mask.start);
            }
        }

        const qint64 unmaskedSleep = totalWatchSleep - maskedWatchSleep;
        const qint64 totalMinutes = unmaskedSleep / 60000;
        const int percentage = static_cast<int>(std::lround(100.0 * unmaskedSleep / totalWatchSleep));
        const QString data = QString("%1:%2 (%3%)")
                .arg(totalMinutes / 60)
                .arg(totalMinutes % 60, 2, 10, QChar('0'))
                .arg(percentage);
        rows.append(extraRow(QCoreApplication::translate("Daily", "Unmasked Sleep"), data,
                             QCoreApplication::translate("Daily", "Time within the watch's sleep session spent without the CPAP mask on, and its share of that session"),
                             QColor("darkred")));
    }
    return rows;
}

Statistics statistics(Day *day, bool graphsEmpty)
{
    Statistics st;
    if (day == nullptr) return st;
    st.present = true;

    Machine *cpap = day->machine(MT_CPAP);

    int mididx=p_profile->general->prefCalcMiddle();
    SummaryType ST_mid = ST_AVG;

    if (mididx==0) ST_mid=ST_PERC;
    if (mididx==1) ST_mid=ST_WAVG;
    if (mididx==2) ST_mid=ST_AVG;

    float percentile=p_profile->general->prefCalcPercentile()/100.0;

    SummaryType ST_max=p_profile->general->prefCalcMax() ? ST_PERC : ST_MAX;
    const EventDataType maxperc=0.995F;

    if (ST_mid==ST_WAVG) st.midName=STR_TR_WAvg;
    else if (ST_mid==ST_AVG) st.midName=STR_TR_Avg;
    else if (ST_mid==ST_PERC) st.midName=STR_TR_Med;
    st.percName = QString("%1%2").arg(percentile*100,0,'f',0).arg(STR_UNIT_Percentage);
    st.maxName = ST_max == ST_MAX?STR_TR_Max:QString("99.5%");

    ChannelID chans[]={
        CPAP_Pressure,CPAP_PressureSet,CPAP_EPAP,CPAP_EPAPSet,CPAP_IPAP,CPAP_IPAPSet,CPAP_PS,CPAP_PTB,
        PRS1_PeakFlow,
        Prisma_ObstructLevel, Prisma_PressureMeasured, Prisma_rRMV, Prisma_rMVFluctuation, WM_rRMV,
        CPAP_MinuteVent, CPAP_RespRate, CPAP_RespEvent,CPAP_FLG,
        RMVENT_AlvMinVent, RMVENT_SpontCyc, RMVENT_SpontTrig,
        CPAP_Leak, CPAP_LeakTotal, CPAP_Snore,  CPAP_IE,  CPAP_Ti,CPAP_Te, CPAP_TgMV,
        CPAP_TidalVolume, OXI_Pulse, OXI_SPO2, POS_Inclination, POS_Orientation, POS_Movement
        #if defined(STEADY_BREATHING)
        , CPAP_SteadyBreathing
        #endif
    };
    int numchans=sizeof(chans)/sizeof(ChannelID);
    EventDataType tmp,med,perc,mx,mn;

    for (int i=0;i<numchans;i++) {

        ChannelID code=chans[i];

        if (!day->channelHasData(code))
            continue;

        QString tooltip=schema::channel[code].description();

        if (!schema::channel[code].units().isEmpty()) tooltip+=" ("+schema::channel[code].units()+")";

        if (ST_max == ST_MAX) {
            mx=day->Max(code);
        } else {
            mx=day->percentile(code,maxperc);
        }

        mn=day->Min(code);
        perc=day->percentile(code,percentile);

        if (ST_mid == ST_PERC) {
            med=day->percentile(code,0.5);
            tmp=day->wavg(code);
            if (tmp>0 || mx==0) {
                tooltip+=QString("<br/>"+STR_TR_WAvg+": %1").arg(tmp,0,'f',2);
            }
        } else if (ST_mid == ST_WAVG) {
            med=day->wavg(code);
            tmp=day->percentile(code,0.5);
            if (tmp>0 || mx==0) {
                tooltip+=QString("<br/>"+STR_TR_Median+": %1").arg(tmp,0,'f',2);
            }
        } else if (ST_mid == ST_AVG) {
            med=day->avg(code);
            tmp=day->percentile(code,0.5);
            if (tmp>0 || mx==0) {
                tooltip+=QString("<br/>"+STR_TR_Median+": %1").arg(tmp,0,'f',2);
            }
        }

        StatRow row;
        row.label = schema::channel[code].label();
        row.tooltip = tooltip;
        row.min = QString::number(mn,'f',2);
        row.mid = QString::number(med,'f',2);
        row.perc = QString::number(perc,'f',2);
        row.max = QString::number(mx,'f',2);
        st.rows.append(row);
    }

    if (graphsEmpty && ((st.rows.size()>0) || (cpap && day->summaryOnly()))) {
        st.summaryOnlyNote = true;
    } else if (cpap) {
        st.cpap = true;

        if ((cpap->loaderName() == STR_MACH_ResMed) || ((cpap->loaderName() == STR_MACH_PRS1) && (p_profile->cpap->resyncFromUserFlagging()))) {
            int ttia = day->sum(AllAhiChannels);
            int h = ttia / 3600;
            int m = ttia / 60 % 60;
            int s = ttia % 60;
            if (ttia > 0) {
                st.extras.append({QCoreApplication::translate("Daily", "Total time in apnea"),
                                  QString::asprintf("%02i:%02i:%02i",h,m,s)});
            }

        }
        float hours = day->hours(MT_CPAP);

        if (p_profile->cpap->showLeakRedline()) {
            float rlt = day->timeAboveThreshold(CPAP_Leak, p_profile->cpap->leakRedline()) / 60.0;
            float pc = (hours > 0) ? 100.0 / hours * rlt : 0;
            st.extras.append({QCoreApplication::translate("Daily", "Time over leak redline"),
                              QString("%1%").arg(pc, 0, 'f', 3)});
        }
        int l = day->sum(CPAP_Ramp);

        if (l > 0) {
            st.extras.append({QCoreApplication::translate("Daily", "Total ramp time"),
                              QString("%1:%2:%3").arg(l / 3600, 2, 10, QChar('0')).arg((l / 60) % 60, 2, 10, QChar('0')).arg(l % 60, 2, 10, QChar('0'))});
            float v = (hours - (float(l) / 3600.0));
            int q = v * 3600.0;
            st.extras.append({QCoreApplication::translate("Daily", "Time outside of ramp"),
                              QString("%1:%2:%3").arg(q / 3600, 2, 10, QChar('0')).arg((q / 60) % 60, 2, 10, QChar('0')).arg(q % 60, 2, 10, QChar('0'))});
        }
    }
    return st;
}

/*! \brief True if an enabled session recorded SpO2 or pulse samples.
 *
 *  Reads the summary counts, so it works whether or not the session's events are loaded.
 */
static bool sessionHasOximetry(Session * sess)
{
    return sess->enabled()
           && (sess->m_cnt.value(OXI_SPO2, 0) > 0 || sess->m_cnt.value(OXI_Pulse, 0) > 0);
}

static TextRow textRow(const QString &label, const QString &value, TextRow::Style style)
{
    TextRow row;
    row.label = label;
    row.value = value;
    row.style = style;
    return row;
}

/*! \brief The summary figures a Viatom oximeter computes itself.
 *
 *  Counts, times and steps are summed over the day's enabled sessions from this oximeter;
 *  the O2 score is averaged over the sessions that have one, weighted by session length.
 *  Returns nothing when no session carries the figures (older models don't record them).
 */
static QVector<TextRow> viatomSummary(Day * day, Machine * oxi)
{
    double hours = 0, scoreWeighted = 0;
    qint64 drops3 = 0, drops4 = 0, below90 = 0, asleep = 0, steps = 0;
    bool found = false;
    for (auto & sess : day->sessions) {
        if (!sess->enabled() || sess->machine() != oxi) continue;
        if (!sess->settings.contains(Viatom_O2Score)) continue;
        found = true;
        // A session can carry other summary figures without a score; leave it out of the average
        double score = sess->settings[Viatom_O2Score].toDouble();
        if (score > 0) {
            double h = sess->hours();
            hours += h;
            scoreWeighted += score * h;
        }
        drops3 += sess->settings[Viatom_Drops3].toLongLong();
        drops4 += sess->settings[Viatom_Drops4].toLongLong();
        below90 += sess->settings[Viatom_TimeBelow90].toLongLong();
        asleep += sess->settings[Viatom_AsleepTime].toLongLong();
        steps += sess->settings[Viatom_Steps].toLongLong();
    }
    QVector<TextRow> rows;
    if (!found) {
        return rows;
    }

    auto hm = [](qint64 secs) {
        return QString("%1:%2").arg(secs / 3600).arg((secs % 3600) / 60, 2, 10, QChar('0'));
    };
    rows.append(textRow(QCoreApplication::translate("Daily", "Reported by the oximeter"), QString(), TextRow::Subheading));
    if (hours > 0) {
        rows.append(textRow(QCoreApplication::translate("Daily", "O2 Score"), QString("%1").arg(scoreWeighted / hours,0,'f',1), TextRow::Line));
    }
    rows.append(textRow(QCoreApplication::translate("Daily", "3% / 4% Drops"), QString("%1 / %2").arg(drops3).arg(drops4), TextRow::Line));
    rows.append(textRow(QCoreApplication::translate("Daily", "Time below 90%"), hm(below90), TextRow::Line));
    if (asleep > 0) {
        rows.append(textRow(QCoreApplication::translate("Daily", "Asleep"), hm(asleep), TextRow::Line));
    }
    if (steps > 0) {
        rows.append(textRow(QCoreApplication::translate("Daily", "Steps"), QString("%1").arg(steps), TextRow::Line));
    }
    return rows;
}

Oximeter oximeter(Day *day)
{
    // Oximetry comes either from a dedicated oximeter (MT_OXIMETER) or from a CPAP that records
    // SpO2/pulse in its own sessions (ResMed or PRS1 with an attached oximeter, BMC, ...). The
    // dedicated oximeter wins when the day has one. All figures are summed over the chosen
    // machine's oximetry sessions only, so a CPAP's SpO2 Drop events are not folded into a
    // separate oximeter's rates, and the per-hour rates use only the hours that carried oximetry.
    Oximeter ox;
    if (!day) return ox;
    Machine * oxi = day->machine(MT_OXIMETER);
    bool cpapOximetry = false;
    if (!oxi || !day->hasEnabledSessions(MT_OXIMETER)) {
        // No dedicated oximeter: fall back to a CPAP that recorded oximetry itself
        oxi = nullptr;
        Machine * cpap = day->machine(MT_CPAP);
        for (auto & sess : day->sessions) {
            if (cpap && sess->machine() == cpap && sessionHasOximetry(sess)) {
                oxi = cpap;
                cpapOximetry = true;
                break;
            }
        }
    }
    if (!oxi) return ox;
    ox.present = true;
    if (cpapOximetry) {
        ox.heading = QCoreApplication::translate("Daily", "Recorded by %1 %2").arg(oxi->brand(), oxi->model());
    } else {
        ox.heading = oxi->brand()+" "+oxi->model();
    }
    if (oxi->loaderName() == applehealth_class_name) {
        // Per-machine, since day->count() would fold in a CPAP's SpO2/pulse from the same day
        EventDataType spo2Count = 0, pulseCount = 0;
        for (auto & sess : day->sessions) {
            if (!sess->enabled() || sess->machine() != oxi) continue;
            spo2Count += sess->count(OXI_SPO2);
            pulseCount += sess->count(OXI_Pulse);
        }
        ox.rows.append(textRow(QCoreApplication::translate("Daily", "SpO2 spot checks"), QString("%1").arg(int(spo2Count)), TextRow::Pair));
        ox.rows.append(textRow(QCoreApplication::translate("Daily", "Pulse readings"), QString("%1").arg(int(pulseCount)), TextRow::Pair));
        ox.note = QCoreApplication::translate("Daily", "Spot-check data; desaturation detection is not applicable");
    } else {
        // Totals over this machine's oximetry sessions only. For a CPAP, sessions without
        // the oximeter attached are left out so they don't dilute the per-hour rates.
        double hours = 0, dropCount = 0, dropSum = 0, pcCount = 0, pcSum = 0;
        double baseWeighted = 0, baseHours = 0;
        for (auto & sess : day->sessions) {
            if (!sess->enabled() || sess->machine() != oxi) continue;
            if (cpapOximetry && !sessionHasOximetry(sess)) continue;
            double h = sess->hours();
            hours += h;
            // Guarded like Day::count()/sum(): Session::count() caches a 0 in m_cnt for an
            // absent channel, which would then make channelHasData() report it present.
            if (sess->m_cnt.contains(OXI_SPO2Drop)) dropCount += sess->count(OXI_SPO2Drop);
            if (sess->m_sum.contains(OXI_SPO2Drop)) dropSum += sess->sum(OXI_SPO2Drop);
            if (sess->m_cnt.contains(OXI_PulseChange)) pcCount += sess->count(OXI_PulseChange);
            if (sess->m_sum.contains(OXI_PulseChange)) pcSum += sess->sum(OXI_PulseChange);
            // calcSPO2Drop() stores the baseline it used in the session settings
            auto base = sess->settings.constFind(OXI_SPO2Drop);
            if (base != sess->settings.constEnd()) {
                baseWeighted += base.value().toDouble() * h;
                baseHours += h;
            }
        }
        double pctDrop = (hours > 0) ? (100.0 / hours) * (dropSum / 3600.0) : 0;
        double rateDrop = (hours > 0) ? dropCount / hours : 0;
        double pctPC = (hours > 0) ? (100.0 / hours) * (pcSum / 3600.0) : 0;
        double ratePC = (hours > 0) ? pcCount / hours : 0;
        double baseline = (baseHours > 0) ? baseWeighted / baseHours : 0;

        // Include SpO2 and PC drops per hour of Oximetry data in case CPAP data is missing
        ox.rows.append(textRow(QCoreApplication::translate("Daily", "SpO2 Desaturations"),
                               QString("%1 (%2%) %3/h").arg(dropCount).arg(pctDrop,0,'f',2).arg(rateDrop,0,'f',2), TextRow::Line));
        ox.rows.append(textRow(QCoreApplication::translate("Daily", "Pulse Change events"),
                               QString("%1 (%2%) %3/h").arg(pcCount).arg(pctPC,0,'f',2).arg(ratePC,0,'f',2), TextRow::Line));
        ox.rows.append(textRow(QCoreApplication::translate("Daily", "SpO2 Baseline Used"),
                               QString("%1%").arg(baseline,0,'f',2), TextRow::Line)); // CHECKME: Should this value be wavg OXI_SPO2 isntead?

        if (oxi->loaderName() == viatom_class_name) {
            ox.rows += viatomSummary(day, oxi);
        }
    }
    return ox;
}

Settings settings(Day *day)
{
    Settings st;
    if (!day) return st;
    Machine * cpap = day->machine(MT_CPAP);
    if (!(cpap && day->hasEnabledSessions(MT_CPAP))) return st;
    st.present = true;
    CPAPLoader * loader = qobject_cast<CPAPLoader *>(cpap->loader());
    if (!loader) {
        st.loaderMissing = true;
        return st;
    }
    st.assumed = day->noSettings(cpap);

    QMap<QString, TextRow> other;
    Session * sess = day->firstSession(MT_CPAP);

    QHash<ChannelID, QVariant>::iterator it;
    QHash<ChannelID, QVariant>::iterator it_end;
    if (sess) {
        it_end = sess->settings.end();
        it = sess->settings.begin();
    }
    QMap<int, TextRow> first;

    ChannelID cpapmode = loader->CPAPModeChannel();
    schema::Channel & modeChan = schema::channel[cpapmode];
    first[cpapmode] = textRow(modeChan.label(), day->getCPAPModeStr(), TextRow::Pair);
    first[cpapmode].tooltip = modeChan.description();

    // The order in which to present pressure settings, which will appear first.
    QVector<ChannelID> first_channels = { cpapmode, CPAP_Pressure, CPAP_PressureMin, CPAP_PressureMax, CPAP_EPAP, CPAP_EPAPLo, CPAP_EPAPHi, CPAP_IPAP, CPAP_IPAPLo, CPAP_IPAPHi, CPAP_PS, CPAP_PSMin, CPAP_PSMax };

    if (sess) for (; it != it_end; ++it) {
        ChannelID code = it.key();

        // OXI_SPO2Drop is not a device setting: calcSPO2Drop() parks the calculated
        // SpO2 *baseline percentage* in session settings under that key (calcs.cpp).
        // Rendered here it picked up the flag channel's label and units and displayed
        // as "SD  96.00 Events/hr" — a percentage shown as an event rate.  The value is
        // presented properly as "SpO2 Baseline Used" in the Oximeter Information panel.
        if ((code <= 1) || (code == RMS9_MaskOnTime) || (code == CPAP_Mode) || (code == cpapmode)
            || (code == CPAP_SummaryOnly) || (code == OXI_SPO2Drop))
            continue;

        schema::Channel & chan = schema::channel[code];

        QString data;

        if (chan.datatype() == schema::LOOKUP) {
            int value = it.value().toInt();
            data = chan.option(value);
            if (data.isEmpty()) {
                data = QString().number(value) + " " + chan.units();;
            }
        } else if (chan.datatype() == schema::BOOL) {
            data = (it.value().toBool() ? STR_TR_Yes : STR_TR_No);
        } else if (chan.datatype() == schema::DOUBLE) {
            data = QString().number(it.value().toDouble(),'f',2) + " "+chan.units();
        } else if (chan.datatype() == schema::DEFAULT) {
            // Check for any options that override the default numeric display.
            int value = it.value().toInt();
            data = chan.option(value);
            if (data.isEmpty()) {
                data = QString().number(it.value().toDouble(),'f',2) + " "+chan.units();
            }
        } else {
            // INTEGER and anything else.  A registered option overrides the numeric
            // rendering, matching the DEFAULT branch above: an integer setting whose
            // zero means "disabled" (BMC ramp time, SleepStyle EPR level and humidity)
            // registers an option for it so it reads "Off" rather than "0 Minutes".
            // Channels with no options behave exactly as before.
            data = chan.option(it.value().toInt());
            if (data.isEmpty()) {
                data = it.value().toString() + " "+ chan.units();
            }
        }
        if (code ==0xe202)      // Format EPR relief correctly
            data = formatRelief(data);

        TextRow row = textRow(schema::channel[code].label(), data, TextRow::Pair);
        row.tooltip = schema::channel[code].description();
        if (first_channels.contains(code)) {
            first[code] = row;
        } else {
            other[schema::channel[code].label()] = row;
        }
    }

    // Put the pressure settings in order.
    for (auto & code : first_channels) {
        if (first.contains(code)) st.rows.append(first[code]);
    }

    // TODO: add logical (rather than alphabetical) ordering to this list, preferably driven by loader somehow
    for (QMap<QString,TextRow>::iterator it = other.begin(); it != other.end(); ++it) {
        st.rows.append(it.value());
    }
    return st;
}

QVector<SessionGroup> sessions(Day *day)
{
    QVector<SessionGroup> groups;
    if (!day) return groups;

    // Day::machines is a QMap, so the groups come out in MachineType order on every launch
    // (CPAP, oximetry, sleep stage, position).
    for (auto mi = day->machines.begin(), mach_end = day->machines.end(); mi != mach_end; ++mi) {
        if (mi.key() == MT_JOURNAL) continue;
        SessionGroup group;
        group.type = mi.key();
        switch (mi.key()) {
            case MT_CPAP: group.heading = QCoreApplication::translate("Daily", "CPAP Sessions"); break;
            case MT_OXIMETER: group.heading = QCoreApplication::translate("Daily", "Oximetry Sessions"); break;
            case MT_SLEEPSTAGE: group.heading = QCoreApplication::translate("Daily", "Sleep Stage Sessions"); break;
            case MT_POSITION: group.heading = QCoreApplication::translate("Daily", "Position Sensor Sessions"); break;
            default: group.heading = QCoreApplication::translate("Daily", "Unknown Session"); break;
        }

        QList<Session *> sesslist = day->getSessions(mi.key(), true);

        for (QList<Session *>::iterator s=sesslist.begin(); s != sesslist.end(); ++s) {
            QDateTime fd=QDateTime::fromSecsSinceEpoch((*s)->first()/1000L);
            QDateTime ld=QDateTime::fromSecsSinceEpoch((*s)->last()/1000L);
            int len=(*s)->length()/1000L;
            int h=len/3600;
            int m=(len/60) % 60;
            int s1=len % 60;

            SessionRow row;
            row.session = *s;
            row.name = QCoreApplication::translate("Daily", "%1 Session #%2").arg((*s)->machine()->loaderName()).arg((*s)->session(),8,10,QChar('0'));
            row.duration = QCoreApplication::translate("Daily", "%1h %2m %3s").arg(h,2,10,QChar('0')).arg(m,2,10,QChar('0')).arg(s1,2,10,QChar('0'));
            row.on = (*s)->enabled();
            row.date = fd.date().toString(QLocale::system().dateFormat(QLocale::ShortFormat));
            row.start = fd.toString("HH:mm:ss");
            row.end = ld.toString("HH:mm:ss");
            group.rows.append(row);
        }
        groups.append(group);
    }
    return groups;
}

double pieTotal(const QVector<EventRow> &events)
{
    // The channels the event breakdown pie shows (see Daily::Daily), as Daily::Load added them
    static const QList<ChannelID> pie = {
        CPAP_Obstructive, CPAP_Hypopnea, CPAP_AllApnea, CPAP_ObstructiveHypopnea, CPAP_CentralHypopnea,
        CPAP_ClearAirway, CPAP_Apnea, CPAP_RERA, CPAP_FlowLimit, CPAP_SensAwake };
    EventDataType total = 0;
    for (const EventRow &row : events) {
        if (row.link && pie.contains(row.code)) total += EventDataType(row.value);
    }
    return total;
}

DayKind classify(bool hasCpap, double cpapHours, int sessionCount, bool brick, bool hasOximeterOrPosition)
{
    if (!hasCpap) {
        return hasOximeterOrPosition ? DayKind::NonCpapOnly : DayKind::NoData;
    }
    if (cpapHours > 0) {
        return brick ? DayKind::ComplianceOnly : DayKind::Cpap;
    }
    return sessionCount > 0 ? DayKind::SessionsOff : DayKind::ZeroHours;
}

} // namespace DailyDetails
