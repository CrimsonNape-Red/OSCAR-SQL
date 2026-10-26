/* SleepLib Weinmann SOMNOsoft/Balance Loader Implementation
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 * Copyright (c) 2011-2018 Mark Watkins
 * Copyright (c) 2016-2017 "TBM" (Weinmann support for the SleepyHead fork this is based on)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QDomDocument>
#include <QDomElement>
#include <QDomNode>
#include <QtMath>
#include <cmath>

#include "SleepLib/session.h"
#include "weinmann_loader.h"

// Weinmann specific settings
ChannelID WM_HumidStatus, WM_HumidLevel, WM_HumidUsage, WM_SoftPAPLock, WM_SoftPAPLevel, WM_SoftStart,
    WM_PressureRise, WM_Masktest, WM_Autostart, WM_InfoMenu, WM_StoreDiary, WM_AutoStorage,
    WM_ManStorage, WM_UsedTime, WM_TherapyTime, WM_HumidifierTime, WM_Firmware;

// Weinmann specific event channels (the device scores these in 2 minute "epochs")
ChannelID WM_Artefact, WM_EpFlowLimit, WM_EpSnore, WM_EpMildObs, WM_EpSevereObs;

// Waveform channel: relative respiratory minute volume (100 = reference value of the device)
ChannelID WM_rRMV;

// Maps a raw event code of the day/week event tables to the channel it is stored in.
// Codes marked "verified" were matched event by event against the event lists shown by
// Weinmann's own software (WEINMANNsupport) for three consecutive nights.
// Everything else returns NoChannel and is deliberately not imported: the table also holds
// internal analysis markers (e.g. 0x12, 0x13, 0x28, 0x2a, 0x46) that Weinmann's software
// does not show as events.
static ChannelID wmEventChannel(int evcode)
{
    switch (evcode) {
    case 3:
        return WM_EpSevereObs; // verified: epoch with severe obstruction
    case 4:
        return WM_EpMildObs; // verified: epoch with mild obstruction
    case 8:
        return CPAP_FlowLimit; // verified: flow limitation
    case 10:
        return WM_EpFlowLimit; // verified: epoch with flattening
    case 9:
        return CPAP_RERA; // verified: RERA (obstruction by disturbance, flattening)
    case 11:
    case 12:
        return CPAP_RERA; // RERA as well (with snoring), counted like 0x09 by Weinmann's software (not verified)
    case 16:
        return CPAP_ObstructiveHypopnea; // verified
    case 17:
        return CPAP_CentralHypopnea; // verified
    case 34:
        return WM_EpSnore; // epoch with snoring (not present in the verification data)
    case 64:
        return CPAP_ClearAirway; // verified: central apnea
    case 65:
        return CPAP_Obstructive; // obstructive apnea (not present in the verification data)
    default:
        return NoChannel;
    }
}

// Creates the EventList for a channel on first use, so a session only carries channels the
// device actually reported.
static EventList *wmEvents(Session *sess, QHash<ChannelID, EventList *> &cache, ChannelID ch)
{
    EventList *el = cache.value(ch, nullptr);
    if (!el) {
        el = sess->AddEventList(ch, EVL_Event);
        cache.insert(ch, el);
    }
    return el;
}

struct WmEvent {
    qint64 end; // time stamp of the event (it is the end of the event)
    EventStoreType duration;
    ChannelID channel;
};

// Weinmann's software does not show central apneas and hypopneas that overlap
// an obstructive apnea, an obstructive hypopnea or a RERA, and RERAs that overlap an obstructive event.
// The events are in time order. Events that are suppressed themselves do not suppress others.
static QList<WmEvent> wmSuppress(const QList<WmEvent> &events)
{
    auto overlaps = [](const WmEvent &a, const WmEvent &b) {
        return (a.end > b.end - qint64(b.duration) * 1000) && (a.end - qint64(a.duration) * 1000 <= b.end);
    };
    auto suppressedBy = [&](const WmEvent &e, const QList<WmEvent> &candidates, bool withRera) {
        for (const WmEvent &c : candidates) {
            bool strong = (c.channel == CPAP_Obstructive) || (c.channel == CPAP_ObstructiveHypopnea) || (withRera && (c.channel == CPAP_RERA));
            if (strong && overlaps(c, e)) {
                return true;
            }
        }
        return false;
    };

    QList<WmEvent> kept;
    for (const WmEvent &e : events) {
        if ((e.channel == CPAP_RERA) && suppressedBy(e, events, false)) {
            continue;
        }
        kept.append(e);
    }
    QList<WmEvent> result;
    for (const WmEvent &e : kept) {
        bool central = (e.channel == CPAP_CentralHypopnea) || (e.channel == CPAP_ClearAirway);
        if (central && suppressedBy(e, kept, true)) {
            continue;
        }
        result.append(e);
    }
    return result;
}

// Reads the leading <desc>...</desc> XML block of the file at the current position. Both the
// index of WM_DATA.TDF and the settings block are followed directly by binary data.
static QByteArray wmReadXmlBlock(QFile &f)
{
    QByteArray xml;
    while (!f.atEnd()) {
        QByteArray line = f.readLine(250);
        xml += line;
        if (line.contains("</desc>")) {
            break;
        }
    }
    return xml;
}

void WeinmannLoader::reportProgress(int value)
{
    emit setProgressValue(value);
    QCoreApplication::processEvents();
}

Weinmann::Weinmann(Profile *profile, MachineID id)
    : CPAP(profile, id)
{
    ;
}

Weinmann::~Weinmann() = default;

WeinmannLoader::WeinmannLoader()
{
    m_type = MT_CPAP;
}

WeinmannLoader::~WeinmannLoader() = default;

QString WeinmannLoader::PresReliefLabel() { return QObject::tr("SoftPAP: "); }
ChannelID WeinmannLoader::PresReliefMode() { return WM_SoftPAPLock; }
ChannelID WeinmannLoader::PresReliefLevel() { return WM_SoftPAPLevel; }


bool WeinmannLoader::Detect(const QString &givenpath)
{
    QDir dir(givenpath);

    if (!dir.exists()) {
        return false;
    }

    // Check for the settings file inside the .. folder
    if (!dir.exists("WM_DATA.TDF")) {
        return false;
    }

    return true;
}


// Checks the offsets and counts of the index of WM_DATA.TDF against each other and the file size, so a damaged
// file is rejected instead of making the loader read or allocate nonsense.
static bool wmIndexValid(const QHash<QString, int> &index, qint64 fileSize)
{
    static const char *const weekKeys[] = {"DeviceConfigOffset", "YearComplianceOffset", "WeekComplianceOffset", "WCD_Pin_Offset", "WCD_Pex_Offset",
                                           "WCD_Snore_Offset", "WCD_Lf_Offset", "WCD_Events_Offset", "WCD_IO_Offset"};
    static const char *const dayKeys[] = {"DayComplianceOffset", "TID_Flow_Offset", "TID_Status_Offset", "TID_P_Offset", "TID_AMV_Offset", "TID_Events_Offset"};

    qint64 last = 0;
    for (const char *key : weekKeys) {
        qint64 offset = index.value(key, -1);
        if ((offset < last) || (offset > fileSize)) {
            return false;
        }
        last = offset;
    }

    // the day data is optional, but if the index mentions it, it must be complete
    bool hasDayData = false;
    for (const char *key : dayKeys) {
        hasDayData = hasDayData || (index.value(key, 0) != 0);
    }
    if (hasDayData) {
        for (const char *key : dayKeys) {
            qint64 offset = index.value(key, -1);
            if ((offset < last) || (offset > fileSize)) {
                return false;
            }
            last = offset;
        }
    }

    // year compliance: 20 bytes + 2 * 366 bytes
    if (index["WeekComplianceOffset"] - index["YearComplianceOffset"] < 752) {
        return false;
    }
    int weekCount = index.value("WeekComplianceCount", 0);
    int dayCount = index.value("DayComplianceCount", 0);
    if ((weekCount < 0) || (weekCount * 0x84 > index["WCD_Pin_Offset"] - index["WeekComplianceOffset"])) {
        return false;
    }
    return (dayCount >= 0) && (hasDayData ? (dayCount * 0xd6 <= index["TID_Flow_Offset"] - index["DayComplianceOffset"]) : (dayCount == 0));
}

int WeinmannLoader::ParseIndex(QFile &wmdata)
{
    index.clear();

    QDomDocument index_xml("weinmann");
    if (!index_xml.setContent(wmReadXmlBlock(wmdata))) {
        return 0;
    }
    QDomElement docElem = index_xml.documentElement();

    QDomNode n = docElem.firstChild();
    while (!n.isNull()) {
        QDomElement e = n.toElement();
        if (!e.isNull()) {
            bool ok;
            int val = e.attribute("val").toInt(&ok);
            if (ok) {
                index[e.attribute("name")] = val;
            }
        }
        n = n.nextSibling();
    }
    return index.size();
}

int WeinmannLoader::ParseSettings(QFile &wmdata)
{
    msettings.clear();

    QDomDocument settings_xml("weinmann");
    if (!settings_xml.setContent(wmReadXmlBlock(wmdata))) {
        return 0;
    }
    QDomElement docElem = settings_xml.documentElement();

    QDomNode n = docElem.firstChild();
    while (!n.isNull()) {
        QDomElement e = n.toElement();
        if (!e.isNull()) {
            msettings[e.attribute("name")] = e.attribute("val");
        }
        n = n.nextSibling();
    }
    return msettings.size();
}


const QString DayComplianceCount = "DayComplianceCount";
const QString CompOffset = "DayComplianceOffset";
const QString FlowOffset = "TID_Flow_Offset";
const QString StatusOffset = "TID_Status_Offset";
const QString PresOffset = "TID_P_Offset";
const QString AMVOffset = "TID_AMV_Offset";
const QString EventsOffset = "TID_Events_Offset";


MachineInfo WeinmannLoader::PeekInfo(const QString &path)
{
    //peek into WM_PARA.TPG
    QString tpath = path;
    tpath.replace("\\", "/");

    QFile wmdata(tpath + "/WM_PARA.TPG");
    if (!wmdata.open(QFile::ReadOnly)) {
        wmdata.setFileName(tpath + "/WM_DATA.TDF");
        if (!wmdata.open(QFile::ReadOnly)) {
            return MachineInfo();
        }
        if (ParseIndex(wmdata) <= 0 || !index.contains("DeviceConfigOffset") || !wmdata.seek(index["DeviceConfigOffset"])) {
            return MachineInfo();
        }
    }

    if (ParseSettings(wmdata) <= 0) {
        return MachineInfo();
    }

    MachineInfo info = newInfo();
    info.model = GetDeviceName(msettings["CDEVN00"].toInt());
    info.series = GetDeviceName(msettings["CTUBS00"].toInt());
    info.modelnumber = msettings["CDEVN00"];
    info.serial = msettings["CSN__00"];
    info.cap = msettings["CMODE00"] == "7" ? MODE_CPAP : MODE_APAP;

    return info;
}


// Flow (day data) and leak (week data) are stored compressed: one byte per sample, fine steps around
// the usual values and coarse steps at the extremes. The result is in 0.1 l/min.
static qint16 wmDecompressFlow(unsigned char value)
{
    if (value < 50) {
        return qint16(45 * value - 2478);
    }
    if (value < 200) {
        return qint16(10 * (value - 50) - 245);
    }
    return qint16(qRound(1250.0 * (value - 200) / 56.0) + 1261);
}

// Rounds half to even, like WEINMANNsupport, so the derived signals match its values exactly.
static int wmRound(double value)
{
    return int(std::nearbyint(value));
}

namespace {

// Flow (0.1 l/min) and pressure (0.1 hPa) of a point of the vent curve of the mask.
struct WmPoint {
    int flow;
    int pressure;
};

// Intentional (vent) flow of the mask as a function of the pressure. Through two points it is the parabola
// pressure = a * flow^2 + b * flow (flow = sqrt(factor * pressure + offset^2) - offset); if the two points do
// not allow that, it is the straight line flow = pressure / resistance.
struct WmVentCurve {
    float resistance = 0.0f;
    float factor = 0.0f;
    float offset = 0.0f;

    int flowAt(int pressure) const
    {
        if (resistance > 0.0f) {
            return wmRound(pressure / resistance);
        }
        return wmRound(std::sqrt(double(factor) * pressure + double(offset) * offset) - offset);
    }

    // first must have the lower pressure. With only one point (or a second point of lower flow) a second
    // point 20 l/min and 10 hPa higher is assumed.
    static WmVentCurve through(WmPoint first, const WmPoint *second)
    {
        WmPoint p1 = first;
        if (second && p1.flow >= second->flow) {
            p1 = *second;
            second = nullptr;
        }
        const WmPoint p2 = second ? *second : WmPoint{p1.flow + 200, p1.pressure + 100};
        const double r1 = double(p1.pressure) / p1.flow;
        const double r2 = double(p2.pressure) / p2.flow;
        const double dr = r2 - r1;

        WmVentCurve curve;
        if (dr < 0.001) {
            curve.resistance = float(r2);
        } else {
            curve.factor = float((p2.flow - p1.flow) / dr);
            curve.offset = float((p2.flow * r1 - p1.flow * r2) / (2.0 * dr));
        }
        return curve;
    }
};

// Typical vent curve (11 l/min at 4 hPa, 45 l/min at 30 hPa), used to rank the measured points and when a
// night has no quiet periods.
const WmPoint wmDefaultLow{110, 40};
const WmPoint wmDefaultHigh{450, 300};

// Baseline of the total flow that follows the slow changes (leak) but not the breaths: it moves slowly
// while the flow crosses it, and faster the longer the flow stays more than 40 l/min on one side.
class WmFlowBaseline
{
public:
    int next(int flow)
    {
        const int distance = wmRound(flow - m_base);
        if ((m_lastDistance < 0 && distance > 0) || (m_lastDistance > 0 && distance < 0) || qAbs(distance) < 400) {
            m_run = 0;
        }
        m_lastDistance = distance;
        m_run++;
        const double divisor = qMax(2.0, 10.0 * (15.0 - m_run / 4.0));
        m_base = float(m_base + (flow - m_base) / divisor);
        return wmRound(m_base);
    }

private:
    float m_base = 0.0f;
    int m_lastDistance = 0;
    int m_run = 0;
};

} // namespace

// Respiratory flow and unintentional leak of a detailed night, derived the way WEINMANNsupport derives them
// from the total flow (5 Hz) and the pressure (2 Hz) of the card. All values in 0.1 l/min.
//
// 1. The flow is interpolated to 10 Hz; a slow baseline of it (WmFlowBaseline, smoothed again by 1/32)
//    sampled at 2 Hz is the mean total flow (intentional vent flow + leak).
// 2. Respiratory flow = total flow - mean total flow.
// 3. 30 s windows (half overlapping) with little variation of mean flow and pressure give points of the
//    vent curve. Per 0.5 hPa class between 3 and 20 hPa the point with the lowest flow (relative to the
//    typical curve) is kept; the lowest two of them at least 1.5 hPa apart define the vent curve.
// 4. Leak = mean total flow - vent flow at the (smoothed) pressure, not below 0.
static void wmFlowAndLeak(const unsigned char *flowData, int flowCount, const unsigned char *pressureData, int pressureCount,
                          QVector<qint16> &respFlow, QVector<qint16> &leak)
{
    QVector<qint16> flow(flowCount);
    for (int i = 0; i < flowCount; i++) {
        flow[i] = wmDecompressFlow(flowData[i]);
    }
    QVector<qint16> flow10(2 * flowCount);
    for (int i = 0; i < flowCount; i++) {
        flow10[2 * i] = flow[i];
        flow10[2 * i + 1] = (i + 1 < flowCount) ? qint16(wmRound((flow[i] + flow[i + 1]) / 2.0)) : flow[i];
    }

    const int meanCount = qMax(pressureCount, flow10.size() / 5 + 1);
    QVector<qint16> meanFlow(meanCount, 0);
    WmFlowBaseline baseline;
    int smooth = 0;
    for (int k = 0; k < 100; k++) { // settle on the first value
        smooth = baseline.next(flow10[0]);
    }
    for (int i = 0; i < flow10.size() && i / 5 < meanCount; i++) {
        smooth = wmRound(smooth + (baseline.next(flow10[i]) - smooth) / 32.0);
        if (i % 5 == 0) {
            meanFlow[i / 5] = qint16(smooth);
        }
    }

    respFlow.resize(flowCount);
    for (int i = 0; i < flowCount; i++) {
        respFlow[i] = qint16(flow[i] - meanFlow[wmRound(2.0 * i / 5.0)]);
    }

    QVector<qint16> meanPressure(pressureCount);
    float p = pressureData[0];
    for (int i = 0; i < pressureCount; i++) {
        p += (pressureData[i] - p) / 16.0f;
        meanPressure[i] = qint16(wmRound(p));
    }

    const WmVentCurve typical = WmVentCurve::through(wmDefaultLow, &wmDefaultHigh);
    auto excess = [&typical](const WmPoint &pt) { return pt.flow - typical.flowAt(pt.pressure); };

    QMap<int, WmPoint> lowest; // per pressure class
    const int window = 60;
    for (int start = 0; start + window <= pressureCount; start += window / 2) {
        double sumFlow = 0.0, sumPressure = 0.0;
        for (int i = start; i < start + window; i++) {
            sumFlow += meanFlow[i];
            sumPressure += meanPressure[i];
        }
        const WmPoint pt{wmRound(sumFlow / window), wmRound(sumPressure / window)};
        double devFlow = 0.0, devPressure = 0.0;
        for (int i = start; i < start + window; i++) {
            devFlow += qAbs(meanFlow[i] - pt.flow);
            devPressure += qAbs(meanPressure[i] - pt.pressure);
        }
        const int pclass = wmRound(pt.pressure / 5.0);
        if (devFlow > 1200.0 || devPressure > 250.0 || pclass < 6 || pclass > 40 || pt.flow <= 0) {
            continue;
        }
        if (!lowest.contains(pclass) || excess(pt) < excess(lowest[pclass])) {
            lowest[pclass] = pt;
        }
    }

    WmVentCurve vent = typical;
    const WmPoint *first = nullptr;
    for (const WmPoint &pt : std::as_const(lowest)) {
        if (!first || excess(pt) < excess(*first)) {
            first = &pt;
        }
    }
    if (first) {
        const WmPoint *second = nullptr;
        for (const WmPoint &pt : std::as_const(lowest)) {
            if (qAbs(pt.pressure - first->pressure) >= 15 && (!second || excess(pt) < excess(*second))) {
                second = &pt;
            }
        }
        if (second && second->pressure < first->pressure) {
            vent = WmVentCurve::through(*second, first);
        } else {
            vent = WmVentCurve::through(*first, second);
        }
    }

    leak.resize(pressureCount);
    for (int i = 0; i < pressureCount; i++) {
        leak[i] = qint16(qMax(0, meanFlow[i] - vent.flowAt(meanPressure[i])));
    }
}


QString WeinmannLoader::GetDeviceName(const int id)
{
    switch (id) {
    case 0:
        return "SOMNOtron4";
    case 1:
        return "SOMNOsmart";
    case 2:
        return "respiCare";
    case 3:
        return "SOMNOcomfort";
    case 4:
        return "SOMNOventS";
    case 5:
        return "SOMNOventST";
    case 6:
        return "SOMNOtronE";
    case 7:
        return "SOMNOtitr - SW - E";
    case 8:
        return "SOMNOsmart2SW - E";
    case 10:
        return "VENTImotion";
    case 11:
        return "VENTIlogic";
    case 12:
        return "BilevelST22";
    case 15:
        return "VENTImotion2";
    case 19:
        return "SOMNOsmart2";
    case 20:
        return "VENTImotion2upgrade";
    case 29:
        return "SOMNOtitration";
    case 39:
        return "SOMNOset";
    case 49:
        return "SOMNOsoft";
    case 59:
        return "SOMNOcomfort2";
    case 69:
        return "SOMNOcomfort2e";
    case 79:
        return "SOMNOcompact";
    case 89:
        return "SOMNOcompactE";
    case 99:
        return "SOMNObalance";
    case 109:
        return "CPAP20";
    case 119:
        return "CPAP20e";
    case 129:
        return "SOMNObalanceE";
    case 149:
        return "SOMNOventCR";
    case 179:
        return "WEINMANNbalance";
    case 189:
        return "SOMNOventAutoS";
    case 199:
        return "SOMNOventAutoST";
    case 209:
        return "SOMNObalance";
    case 219:
        return "SOMNObalanceE";
    case 229:
        return "SOMNOsoft2";
    case 239:
        return "SOMNOsoft2e";
    case 249:
        return "WEINMANNsoft";
    case 524288:
        return "VENTIlogicLS";
    case 1048576:
        return "VENTIlogicPlus";
    default:
        return QObject::tr("unknown device");
    }
}

void WeinmannLoader::PopulateSettings(Session *sess)
{
    if (msettings.isEmpty())
        return;

    CPAPMode mode = MODE_UNKNOWN;
    if (sess->settings.contains(CPAP_Mode)) {
        mode = (CPAPMode)sess->settings[CPAP_Mode].toInt();
    } else {
        if (msettings["CMODE00"] == "7") {
            sess->settings[CPAP_Mode] = MODE_CPAP;
        } else if (msettings["CMODE00"] == "4") {
            sess->settings[CPAP_Mode] = MODE_APAP;
        } else {
            qDebug() << "WMLoader unknown Device-Mode - please report this";
            sess->settings[CPAP_Mode] = MODE_APAP;
        }
        mode = (CPAPMode)sess->settings[CPAP_Mode].toInt();
    }

    if (mode == MODE_CPAP) {
        if (!(sess->settings.contains(CPAP_Pressure))) {
            sess->settings[CPAP_Pressure] = msettings["CPAP_00"].toInt() / 10.0;
        }
    } else if (mode == MODE_APAP) {
        if (!(sess->settings.contains(CPAP_PressureMin)))
            sess->settings[CPAP_PressureMin] = msettings["LPL__00"].toInt() / 10.0;
        if (!(sess->settings.contains(CPAP_PressureMax)))
            sess->settings[CPAP_PressureMax] = msettings["UPL__00"].toInt() / 10.0;
        if (!(sess->settings.contains(WM_PressureRise)))
            sess->settings[WM_PressureRise] = msettings["PRISE00"].toInt() / 10.0;
    }

    sess->settings[WM_SoftStart] = msettings["SSTAR00"].toInt();
    if (msettings["SSTAR00"].toInt() > 0) {
        sess->settings[CPAP_RampTime] = msettings["SSTME00"].toInt();
        sess->settings[CPAP_RampPressure] = msettings["SSPRS00"].toInt() / 10.0;
    }
    sess->settings[WM_HumidStatus] = msettings["HUMID00"].toInt();
    if (msettings["HUMID00"].toInt() > 0) {
        sess->settings[WM_HumidLevel] = msettings["HUMST00"].toInt();
    }

    sess->settings[WM_SoftPAPLock] = msettings["PAPSP00"].toInt();
    sess->settings[WM_SoftPAPLevel] = msettings["PAPST00"].toInt();
    sess->settings[WM_Masktest] = msettings["MSKPR00"].toInt() / 10.0;
    sess->settings[WM_Autostart] = msettings["ASTRT00"].toInt();
    sess->settings[WM_InfoMenu] = msettings["ILOCK00"].toInt();
    sess->settings[WM_StoreDiary] = msettings["STRDI00"].toInt();
    sess->settings[WM_AutoStorage] = msettings["STRDA00"].toInt();
    sess->settings[WM_ManStorage] = msettings["STRDM00"].toInt();
    sess->settings[WM_UsedTime] = msettings["USETM00"].toInt() / 60.0;
    sess->settings[WM_TherapyTime] = msettings["WM_TherapyTime"].toInt() / 60.0;
    sess->settings[WM_HumidifierTime] = msettings["WM_HumidifierTime"].toInt() / 60.0;
    sess->settings[WM_Firmware] = msettings["CFWVE00"];
    QDateTime t1 = QDateTime::fromMSecsSinceEpoch(sess->realFirst());
    if (t1.time().hour() < 12)
        t1 = t1.addDays(-1); //12:00 session split!
    sess->settings[WM_HumidUsage] = mhumiuse[t1.toString("yyyy.MM.dd")];


    // Keys of the settings block (WM_PARA.TPG, or WM_DATA.TDF at DeviceConfigOffset):
    //   CDEVN00 device id           CTUBS00 matching (higher) device id    CMODE00 mode (4 = APAP, 7 = CPAP)
    //   CSN__00 serial number       CFWVE00 firmware version               DEVDA00/DEVTM00 date and time of the device
    //   LPL__00/UPL__00 lower/upper pressure limit, CPAP_00 CPAP pressure, PRISE00 pressure rise (per second)
    //   PAPST00/PAPSP00 softPAP level/lock     SSTAR00/SSTME00/SSPRS00 soft start on/time/pressure
    //   HUMID00/HUMST00 humidifier on/level    MSKPR00 mask test pressure    ASTRT00 automatic start
    //   ILOCK00 info menu lock      STRDI00 therapy diary          STRDA00/STRDM00 automatic/manual storage
    //                                                              (0 compliance, 1 compliance + efficacy, 3 signal data)
    //   USETM00 total operating time
    // The block also holds patient name, birth date etc. These are not read.
}

int WeinmannLoader::Open(const QString &dirpath)
{
    QString path(dirpath);
    path = path.replace("\\", "/");
    int imported = 0;


    QFile wmdata(path + "/WM_DATA.TDF");
    if (!wmdata.open(QFile::ReadOnly)) {
        return -1;
    }

    emit setProgressMax(100);
    reportProgress(0);

    if (ParseIndex(wmdata) <= 0)
        return -1;

    if (!wmIndexValid(index, wmdata.size())) {
        qWarning() << "WMLoader: the index of WM_DATA.TDF is damaged";
        return -1;
    }

    wmdata.seek(index["DeviceConfigOffset"]);
    if (ParseSettings(wmdata) <= 0)
        return -1;


    reportProgress(5);

    MachineInfo info = newInfo();
    info.model = GetDeviceName(msettings["CDEVN00"].toInt());
    info.series = GetDeviceName(msettings["CTUBS00"].toInt());
    info.modelnumber = msettings["CDEVN00"];
    info.serial = msettings["CSN__00"];
    info.cap = msettings["CMODE00"] == "7" ? MODE_CPAP : MODE_APAP; //7 CPAP //4 APAP ...

    Machine *mach = p_profile->CreateMachine(info);
    if (!mach) {
        return -1;
    }

    qint64 ignoreolder = p_profile->session->ignoreOlderSessionsDate().toSecsSinceEpoch();
    bool ignoreold = p_profile->session->ignoreOlderSessions();

    //////////////////////////////////////////////////////////////////////
    // Year / Compliance info ... use this for a compliance report?
    // it's the only place I found (so far) where usagetimes for the humidifier are stored.
    //
    // ydate = splitpoint for the yearchange.
    // ycompli = used length in tenth of hours - starts at 01/01 and ends at 12/31
    // humi = used length of the humidifier in tenth of hours .. same format!
    //////////////////////////////////////////////////////////////////////

    quint8 *yearco = new quint8[20];
    memset(yearco, 0, 20);
    wmdata.seek((int)index["YearComplianceOffset"]);
    wmdata.read((char *)yearco, 20);

    unsigned char *yp = yearco;
    QDate ydate = QDate(QString::asprintf("%02i%02i", yp[0], yp[1]).toInt(), (int)yp[2], (int)yp[3]);

    // [4]-[11] so far unknown

    msettings["WM_TherapyTime"] = QString::number(yp[12] | yp[13] << 8 | yp[14] << 16 | yp[15] << 24);
    msettings["WM_HumidifierTime"] = QString::number(yp[16] | yp[17] << 8 | yp[18] << 16 | yp[19] << 24);

    mhumiuse.clear();
    char *ycompli = new char[366];
    memset(ycompli, 0, 366);
    wmdata.read(ycompli, 366);
    char *humi = new char[366];
    memset(humi, 0, 366);
    wmdata.read(humi, 366);

    QDate dcur = QDate(ydate.year(), 1, 1);
    for (int i = 0; i < 366; i++) {
        if ((i == 59) && (dcur.daysInYear() == 365)) //non leap-Year ignore 02/29
            continue;
        mhumiuse[dcur.toString("yyyy.MM.dd")] = ((unsigned char)ycompli[i] == 0) ? 0.0 : ((unsigned char)humi[i] * 100.0) / (unsigned char)ycompli[i];
        if (dcur == ydate)
            dcur = dcur.addYears(-1);
        dcur = dcur.addDays(1);
    }

    //////////////////////////////////////////////////////////////////////
    // Read Week Information....
    //////////////////////////////////////////////////////////////////////
    reportProgress(10);


    int WeekComplianceOffset = index["WeekComplianceOffset"];
    int WCD_Pin_Offset = index["WCD_Pin_Offset"];
    int WCD_Pex_Offset = index["WCD_Pex_Offset"];
    int WCD_Snore_Offset = index["WCD_Snore_Offset"];
    int WCD_Lf_Offset = index["WCD_Lf_Offset"];
    int WCD_Events_Offset = index["WCD_Events_Offset"];
    int WCD_IO_Offset = index["WCD_IO_Offset"];
    int comp_start = index[CompOffset];

    int wccount = index["WeekComplianceCount"];

    int size = WCD_Pin_Offset - WeekComplianceOffset;

    char *weekco = new char[size];
    memset(weekco, 0, size);
    wmdata.seek(WeekComplianceOffset);
    wmdata.read(weekco, size);


    wmdata.seek(WCD_Pin_Offset);
    int wtsize = WCD_Pex_Offset - WCD_Pin_Offset;
    const int wpinSize = wtsize;
    char *wpin = new char[wtsize];
    memset(wpin, 0, wtsize);
    wmdata.read(wpin, wtsize);

    wmdata.seek(WCD_Pex_Offset);
    wtsize = WCD_Snore_Offset - WCD_Pex_Offset;
    const int wpexSize = wtsize;
    char *wpex = new char[wtsize];
    memset(wpex, 0, wtsize);
    wmdata.read(wpex, wtsize);

    // The snore bit field of the week data (WCD_Snore_Offset, 1 bit = 2 minutes) is not read:
    // the same information is in the events (epoch with snoring).


    wmdata.seek(WCD_Lf_Offset);
    wtsize = WCD_Events_Offset - WCD_Lf_Offset;
    const int wlfSize = wtsize;
    char *wlf = new char[wtsize];
    memset(wlf, 0, wtsize);
    wmdata.read(wlf, wtsize);

    wmdata.seek(WCD_Events_Offset);
    wtsize = WCD_IO_Offset - WCD_Events_Offset;
    const int wevtSize = wtsize;
    char *wevt = new char[wtsize];
    memset(wevt, 0, wtsize);
    wmdata.read(wevt, wtsize);

    wmdata.seek(WCD_IO_Offset);
    wtsize = ((comp_start == 0) || (index[DayComplianceCount] == 0)) ? (wmdata.size() - WCD_IO_Offset) : (comp_start - WCD_IO_Offset);
    const int wioSize = wtsize;
    char *wio = new char[wtsize];
    memset(wio, 0, wtsize);
    wmdata.read(wio, wtsize);
    quint64 wpinoffset = 0;
    quint64 wpexoffset = 0;
    quint64 wlfoffset = 0;

    EventDataType wsrate = 60000;
    unsigned char *p = (unsigned char *)weekco;
    unsigned char *pwio = (unsigned char *)wio;
    unsigned char *pwevt = (unsigned char *)wevt;
    for (int c = 0; c < wccount; c++) {
        if (isAborted()) {
            break;
        }
        int year = QString::asprintf("%02i%02i", p[0], p[1]).toInt();
        int month = p[2];
        int day = p[3];
        int hour = p[5];
        int minute = p[6];
        int second = p[7];
        bool dcdone = false;
        QDate date = QDate(year, month, day);

        if ((hour >= 0) && (hour < 12)) {
            date = date.addDays(1);
            dcdone = true;
        }
        QDateTime datetime = QDateTime(date, QTime(hour, minute, second));
        qint64 ts = datetime.toSecsSinceEpoch();


        // WCD_IO_Offset
        // WCD IO= 0x00=0  hour minute seconde - 2byte (little endian!) length in 2second-counts
        //io += 180 per day - each session == 6 byte

        // stores used length of data at 0x46, in 16bit integers, for IPAP, EPAP, snore, leak,
        // stores total length of data block at 0x66, in 16 bit integers for IPAP, EPAP, snore, leak

        // seems like after the 60 days the (last?) change in prescription / settings is stored
        // the last day before and the first day with the new pressure settings.

        int wnsess = p[68]; //number of sessions

        quint32 wpinLength = p[102] | p[103] << 8;

        quint32 wpexLength = p[104] | p[105] << 8;

        quint32 wlfLength = p[108] | p[109] << 8;

        quint32 wevtLength = p[110] | p[111] << 8; // in events - NOT bytes!

        // a damaged record must not make the loader read beyond the week data
        if ((wnsess > 30) || (wpinoffset + wpinLength > quint64(wpinSize)) || (wpexoffset + wpexLength > quint64(wpexSize)) ||
            (wlfoffset + wlfLength > quint64(wlfSize)) || (quint64(pwevt - (unsigned char *)wevt) + wevtLength * 6 > quint64(wevtSize)) ||
            (quint64(pwio - (unsigned char *)wio) + 0xb4 > quint64(wioSize))) {
            qWarning() << "WMLoader: damaged week record, ignoring the remaining week data";
            break;
        }

        if (ignoreold && (ts < ignoreolder)) {
            p += 0x84;
            pwio += 0xb4;
            wpinoffset += wpinLength;
            wpexoffset += wpexLength;
            wlfoffset += wlfLength;
            pwevt += (wevtLength * 6);
            continue;
        }

        if (mach->SessionExists(ts)) { // the first session of this day is known, so skip the entire day
            p += 0x84;
            pwio += 0xb4;
            wpinoffset += wpinLength;
            wpexoffset += wpexLength;
            wlfoffset += wlfLength;
            pwevt += (wevtLength * 6);
            continue;
        }

        unsigned char *tpwio = pwio; // tp... = temporary pointer into the week (w) data
        int twoffset = 0;
        unsigned char *tpwevt = pwevt;
        quint16 j = 0;

        for (int i = 0; i < wnsess; i++) {
            quint32 twdur = tpwio[4] | tpwio[5] << 8; // length in 2-second counts
            twdur *= 2;
            int thour = tpwio[1];
            int tmin = tpwio[2];
            int tsec = tpwio[3];

            if (!dcdone && (thour >= 0) && (thour < 12)) {
                date = date.addDays(1);
                dcdone = true;
            }

            if (quint32(twoffset) + twdur / 60 > qMin(qMin(wpinLength, wpexLength), wlfLength)) {
                break;
            }

            QDateTime twdate = QDateTime(date, QTime(thour, tmin, tsec));
            qint64 trs = twdate.toSecsSinceEpoch();

            Session *sess = new Session(mach, trs);
            sess->SetChanged(true);
            sess->really_set_first(trs * 1000);
            sess->really_set_last((trs + twdur) * 1000);
            qint64 ti = sess->realFirst();
            qint64 te = sess->realLast();
            PopulateSettings(sess);
            quint16 twsteps = qFloor(twdur / 60.0);

            EventList *wipap = sess->AddEventList(CPAP_Pressure, EVL_Waveform, 0.1f, 0.0, 0.0, 0.0, wsrate); // IPAP
            wipap->AddWaveform(ti, (unsigned char *)&wpin[wpinoffset + twoffset], twsteps, twsteps * wsrate);
            EventList *wepap = sess->AddEventList(CPAP_EPAP, EVL_Waveform, 0.1f, 0.0, 0.0, 0.0, wsrate);
            wepap->AddWaveform(ti, (unsigned char *)&wpex[wpexoffset + twoffset], twsteps, twsteps * wsrate);

            qint16 *wleakf = new qint16[twsteps];
            for (quint32 i = 0; i < twsteps; i++) {
                wleakf[i] = qMax<qint16>(0, wmDecompressFlow((unsigned char)wlf[wlfoffset + twoffset + i]));
            }


            EventList *wleak = sess->AddEventList(CPAP_Leak, EVL_Waveform, 0.1f, 0.0, 0.0, 0.0, wsrate);
            wleak->AddWaveform(ti, wleakf, twsteps, twsteps * wsrate);
            QHash<ChannelID, EventList *> evlists;

            if (wevtLength > 0) {
                QList<WmEvent> weekEvents;
                qint64 twet = 0;
                do {
                    QDateTime twed = QDateTime(date, QTime(tpwevt[1], tpwevt[2], tpwevt[3]));
                    if (!dcdone && (tpwevt[1] < 12)) {
                        twed = twed.addDays(1);
                    }
                    twet = twed.toMSecsSinceEpoch();
                    twet -= 2000;                                   // events carry the time they ended (the week data has a resolution of 2 seconds)
                    if ((tpwevt[0] == 0xff) && (tpwevt[1] == 0xff)) // we reached the end
                        break;


                    if (twet < (ti - 2000)) { // don't care about stuff before the session-start-point
                        j++;
                        tpwevt += 6;
                        continue;
                    }

                    if (twet <= (te - 2000)) {
                        qint16 evcode = tpwevt[0];
                        EventStoreType data = tpwevt[4] | tpwevt[5] << 8;
                        data = qFloor(data / 10.0); // rounded down: rounding up produced events out of order in the week data
                        ChannelID evch = wmEventChannel(evcode);
                        if (evch != NoChannel) {
                            weekEvents.append({twet, data, evch});
                        }
                        j++;
                        tpwevt += 6;
                    }
                    // Note: event codes of 0x80 and above in the week event table are most likely settings changes of the
                    // device (e.g. 0x80/0x81 pressure limits, 0x82 pressure rise, 0x87 humidifier level). They are logged
                    // when the device is switched on without a therapy session and are not imported.
                    else {
                        if (tpwevt[0] >= 0x80) {
                            if ((tpwevt[1] < 0x19) && (j < wevtLength)) {
                                j++;
                                tpwevt += 6;
                                twet = 0;
                            }
                        } // else - real event after the session end - it is either picked up by the next session
                        // or discarded as there are no more sessions for this day
                    }
                } while ((j < wevtLength) && (twet <= (te - 2000)));

                // Weinmann's software drops the RERA events of the week data (no RERA statistics for
                // those nights), so they are only imported from the day data.
                for (const WmEvent &e : wmSuppress(weekEvents)) {
                    if (e.channel != CPAP_RERA) {
                        wmEvents(sess, evlists, e.channel)->AddEvent(e.end, e.duration);
                    }
                }
            }

            twoffset += twsteps;
            tpwio += 0x06;

            sess->UpdateSummaries();
            addSession(sess);
            imported++;
            delete[] wleakf;
        }
        pwio += 0xb4;
        wpinoffset += wpinLength;
        wpexoffset += wpexLength;
        wlfoffset += wlfLength;
        pwevt += (wevtLength * 6);


        reportProgress(10 + (c * 40 / wccount));

        p += 0x84;
    }


    delete[] weekco;
    delete[] wpin;
    delete[] wpex;
    delete[] wlf;
    delete[] wevt;
    delete[] wio;

    //////////////////////////////////////////////////////////////////////
    // Read Day Information....
    //////////////////////////////////////////////////////////////////////
    reportProgress(50);

    int comp_end = index[FlowOffset];
    int comp_size = comp_end - comp_start;

    quint8 *comp = new quint8[comp_size];
    memset((char *)comp, 0, comp_size);

    wmdata.seek(comp_start);
    wmdata.read((char *)comp, comp_size);

    p = comp;


    float flow_sample_duration = 1000.0 / 5;
    float pressure_sample_duration = 1000.0 / 2;
    float rmv_sample_duration = 1000.0 * 2;

    const int flowstart = index[FlowOffset];
    const int flowsize = index[StatusOffset] - flowstart;
    const int st_start = index[StatusOffset];
    const int st_size = index[PresOffset] - st_start;
    const int pr_start = index[PresOffset];
    const int pr_size = index[AMVOffset] - pr_start;
    const int amv_start = index[AMVOffset];
    const int amv_size = index[EventsOffset] - amv_start;
    const int ev_start = index[EventsOffset];
    const int ev_size = wmdata.size() - ev_start;

    QList<Session *> replaced;
    int c = index[DayComplianceCount];
    for (int i = 0; i < c; i++) {
        int year = QString::asprintf("%02i%02i", p[0], p[1]).toInt();
        int month = p[2];
        int day = p[3];
        int hour = p[5];
        int minute = p[6];
        int second = p[7];

        QDateTime date = QDateTime(QDate(year, month, day), QTime(hour, minute, second));
        qint64 ts = date.toSecsSinceEpoch();

        // When the device overwrites the beginning of the oldest record, it counts the deleted samples, and the
        // remaining data starts ceil(deleted AMV count / 5) s later. Only the data is shifted: the session keeps
        // the time of the record as its ID, so it still matches the week session of that night.
        // Not verified: the count was 0 on all test cards.
        const quint32 deletedAmv = p[0x8c] | p[0x8d] << 8 | p[0x8e] << 16 | p[0x8f] << 24;
        const int startShift = qCeil(deletedAmv / 5.0);

        if (ignoreold && (ts < ignoreolder)) {
            p += 0xd6;
            continue;
        }

        // A night that is already known with detailed data is skipped. A night that is known only from
        // the week data of a newer card (cards imported in reverse order) is replaced below.
        Session *known = mach->SessionExists(ts);
        if (known && known->m_cnt.contains(CPAP_FlowRate)) {
            p += 0xd6;
            continue;
        }

        // Flow Waveform
        quint32 fs = p[8] | p[9] << 8 | p[10] << 16 | p[11] << 24;
        quint32 fl = p[0x44] | p[0x45] << 8 | p[0x46] << 16 | p[0x47] << 24;

        // Status
        quint32 ss = p[12] | p[13] << 8 | p[14] << 16 | p[15] << 24;
        quint32 sl = p[0x48] | p[0x49] << 8 | p[0x4a] << 16 | p[0x4b] << 24;

        // Pressure
        quint32 ps = p[16] | p[17] << 8 | p[18] << 16 | p[19] << 24;
        quint32 pl = p[0x4c] | p[0x4d] << 8 | p[0x4e] << 16 | p[0x4f] << 24;

        // Relative respiratory minute volume
        quint32 as = p[20] | p[21] << 8 | p[22] << 16 | p[23] << 24;
        quint32 al = p[0x50] | p[0x51] << 8 | p[0x52] << 16 | p[0x53] << 24;

        // Events
        quint32 es = p[24] | p[25] << 8 | p[26] << 16 | p[27] << 24;
        quint32 er = p[0x54] | p[0x55] << 8 | p[0x56] << 16 | p[0x57] << 24; // number of records

        // a damaged record must not make the loader read beyond the data blocks
        if (!date.isValid() || (quint64(fs) + fl > quint64(flowsize)) || (quint64(ss) + sl > quint64(st_size)) || (quint64(ps) + pl > quint64(pr_size)) ||
            (quint64(as) + al > quint64(amv_size)) || (quint64(es) + quint64(er) * 6 > quint64(ev_size)) || (qMin(pl / 2, fl / 5) < 2)) {
            qWarning() << "WMLoader: skipping a damaged session record";
            p += 0xd6;
            continue;
        }

        // the week data above may just have created a low resolution session for this very night
        if (new_sessions.contains(ts)) {
            delete new_sessions.take(ts);
        }
        if (known) {
            replaced.append(known);
        }
        Session *sess = new Session(mach, ts);

        sess->SetChanged(true);

        //pressure-settings
        quint16 p1 = p[0xbc] | p[0xbd] << 8; // LPL
        quint16 p2 = p[0xbe] | p[0xbf] << 8; // UPL
        int prise = p[0xc0];                 //Pressure Rise in 0.x cmH2O per second

        int pmode = p[0xc1]; //PAP-Mode

        quint32 dur = qMin(qFloor(pl / 2.0), qFloor(fl / 5.0));
        //cutting the last sec
        dur--;
        pl = qMin(pl, (dur * 2));
        fl = qMin(fl, (dur * 5));
        al = qMin(al, dur / 2 + 1);
        pl++;
        fl++;

        sess->really_set_first(qint64(ts + startShift) * 1000L);
        sess->really_set_last(qint64(ts + startShift + dur) * 1000L);

        if (pmode == 7) {
            sess->settings[CPAP_Mode] = MODE_CPAP;
            // p1 is NOT the CPAP-Pressure - it is alyways LPL!
        } else if (pmode == 4) {
            sess->settings[CPAP_Mode] = MODE_APAP;
            sess->settings[CPAP_PressureMin] = p1 / 1000.0;
            sess->settings[CPAP_PressureMax] = p2 / 1000.0;
            sess->settings[WM_PressureRise] = prise / 10.0;
        } else {
            qDebug() << "WMLoader unknown Device-Mode - please report this to the OSCAR developers";
            sess->settings[CPAP_Mode] = MODE_APAP;
            sess->settings[CPAP_PressureMin] = p1 / 1000.0;
            sess->settings[CPAP_PressureMax] = p2 / 1000.0;
            sess->settings[WM_PressureRise] = prise / 10.0;
        }
        PopulateSettings(sess);

        compinfo.append(CompInfo(sess, date.addSecs(startShift), fs, fl, ss, sl, ps, pl, as, al, es, er));


        p += 0xd6;
    }

    delete[] comp;

    reportProgress(55);

    //////////////////////////////////////////////////////////////////////
    // Read Flow Waveform....
    //////////////////////////////////////////////////////////////////////

    wmdata.seek(flowstart);

    char *data = new char[flowsize];
    memset(data, 0, flowsize);
    wmdata.read(data, flowsize);


    //////////////////////////////////////////////////////////////////////
    // Read Status.... seems to be the interpretation of the OPP-signal /-channel
    //////////////////////////////////////////////////////////////////////

    char *st = new char[st_size];
    memset(st, 0, st_size);

    wmdata.seek(st_start);
    wmdata.read(st, st_size);

    char *startf = new char[st_size];
    memset(startf, 0, st_size);
    char *stl = new char[st_size];
    memset(stl, 0, st_size);
    char *sts = new char[st_size];
    memset(sts, 0, st_size);

    for (int i = 0; i < st_size; i++) {
        startf[i] = (st[i] & 0x04) ? 1 : 0; // short artefacts (not the ones of 120 seconds and more)
        stl[i] = (st[i] & 0x40) ? 1 : 0;    //large leak
        sts[i] = (st[i] & 0x80) ? 1 : 0;    //snoring
    }

    //////////////////////////////////////////////////////////////////////
    // Read Mask Pressure....
    //////////////////////////////////////////////////////////////////////

    char *pres = new char[pr_size];
    memset(pres, 0, pr_size);

    wmdata.seek(pr_start);
    wmdata.read(pres, pr_size);

    //////////////////////////////////////////////////////////////////////
    // Read Relative Respiratory Minute Volume....
    //////////////////////////////////////////////////////////////////////

    char *amv = new char[amv_size];
    memset(amv, 0, amv_size);

    wmdata.seek(amv_start);
    wmdata.read(amv, amv_size);

    //////////////////////////////////////////////////////////////////////
    // Read Events....
    //////////////////////////////////////////////////////////////////////

    quint8 *ev = new quint8[ev_size];
    memset((char *)ev, 0, ev_size);

    wmdata.seek(ev_start);
    wmdata.read((char *)ev, ev_size);

    //////////////////////////////////////////////////////////////////////
    // Process sessions
    //////////////////////////////////////////////////////////////////////
    reportProgress(60);

    for (int i = 0; i < compinfo.size(); i++) {
        if (isAborted()) {
            for (int k = i; k < compinfo.size(); k++) {
                delete compinfo.at(k).session;
            }
            break;
        }
        const CompInfo &ci = compinfo.at(i);
        Session *sess = ci.session;

        qint64 ti = sess->realFirst();

        qDebug() << "new Session " << QDateTime::fromMSecsSinceEpoch(sess->realFirst()).toString("dd.MM. HH:mm:ss") << " - " << QDateTime::fromMSecsSinceEpoch(sess->realLast()).toString("dd.MM. HH:mm:ss");

        QVector<qint16> respFlow, leak;
        wmFlowAndLeak((const unsigned char *)&data[ci.flow_start], ci.flow_size, (const unsigned char *)&pres[ci.pres_start], ci.pres_size,
                      respFlow, leak);

        EventList *flow = sess->AddEventList(CPAP_FlowRate, EVL_Waveform, 0.1F, 0.0F, 0.0F, 0.0F, flow_sample_duration);
        flow->AddWaveform(ti, respFlow.data(), ci.flow_size, ci.flow_size * flow_sample_duration);


        // Stored as CPAP_Pressure (like the pressure of the week data) so the pressure graph and the
        // overview statistics (median, 95th percentile) work for these nights too.
        EventList *MPR = sess->AddEventList(CPAP_Pressure, EVL_Waveform, 0.1f, 0.0, 0.0, 0.0, pressure_sample_duration);
        MPR->AddWaveform(ti, (unsigned char *)&pres[ci.pres_start], ci.pres_size, ci.pres_size * pressure_sample_duration);


        // 100 is the reference value of the device, the range is 0 - 255
        EventList *RMV = sess->AddEventList(WM_rRMV, EVL_Waveform, 1.0f, 0.0, 0.0, 0.0, rmv_sample_duration);
        RMV->AddWaveform(ti, (unsigned char *)&amv[ci.amv_start], ci.amv_size, ci.amv_size * rmv_sample_duration);

        EventList *LE = sess->AddEventList(CPAP_Leak, EVL_Waveform, 0.1f, 0.0, 0.0, 0.0, pressure_sample_duration);
        LE->AddWaveform(ti, leak.data(), ci.pres_size, ci.pres_size * pressure_sample_duration);


        QHash<ChannelID, EventList *> evlists;

        // The event table of a day holds the respiratory events and the 2 minute "epochs" that the device
        // scores itself (wmEventChannel() lists the codes that are imported), plus a lot of internal
        // markers. Snoring, large leaks and short artefacts are not events there, they are bits of the
        // status signal.
        //
        // An epoch is a period of about 2 minutes. It is classified as an epoch with severe obstruction,
        // mild obstruction, flow limitation or snoring if the mean value of the event index during the
        // epoch exceeds a pressure dependent threshold, or if oH, oA or RERA occur.
        // Artefacts (coughing, changing position, swallowing, speaking) stop the event detection.
        //
        // A record is stamped 1 second after the end of the event. Like Weinmann's own software, only
        // events that start inside the session are imported.
        // Runs of set status bits become spans. A span ends at the first sample without the bit, or at
        // the end of the session (the status block may be a little longer than the shortened session).
        const quint32 stat_len = qMin(ci.stat_size, ci.pres_size);
        struct StatusSpan {
            qint64 end;
            EventStoreType duration;
        };
        auto findSpans = [&](const char *bits) {
            QList<StatusSpan> spans;
            qint64 first = -1;
            for (quint32 j = 0; j <= stat_len; j++) {
                if (j < stat_len && bits[ci.stat_start + j] != 0) {
                    if (first < 0) {
                        first = j;
                    }
                } else if (first >= 0) {
                    spans.append({ti + qint64(j) * 500, EventStoreType(qRound((j - first) * 0.5))});
                    first = -1;
                }
            }
            return spans;
        };

        for (const StatusSpan &span : findSpans(stl)) {
            wmEvents(sess, evlists, CPAP_LargeLeak)->AddEvent(span.end, span.duration);
        }
        for (const StatusSpan &span : findSpans(sts)) {
            wmEvents(sess, evlists, CPAP_VSnore)->AddEvent(span.end, span.duration);
        }

        struct tevt {
            quint64 end;
            EventStoreType data;
        };
        QList<tevt> artList;
        for (const StatusSpan &span : findSpans(startf)) {
            artList.append({quint64(span.end), span.duration});
        }


        QList<WmEvent> dayEvents;
        quint64 tt = ti;
        unsigned char *p = &ev[ci.event_start];
        for (quint32 j = 0; j < ci.event_recs; ++j) {
            QDate evdate = ci.time.date();
            QTime evtime(p[1], p[2], p[3]);
            evtime = evtime.addSecs(-1); // the record is stamped 1 second after the end of the event
            if (evtime < ci.time.time()) {
                evdate = evdate.addDays(1);
            }
            quint64 ts = QDateTime(evdate, evtime).toMSecsSinceEpoch();

            qint16 evcode = p[0];
            EventStoreType rdata = p[4] | p[5] << 8;
            EventStoreType data = qCeil(rdata / 10.0); // tenths of a second, rounded up like Weinmann's own software does

            // Like Weinmann's own software, ignore events that do not start inside the session
            qint64 evstart = qint64(ts) - qint64(data) * 1000;
            if ((evstart < qint64(ti)) || (evstart > qint64(ti) + qint64(ci.pres_size) * 500)) {
                p += 6;
                continue;
            }
            if (evcode == 100) {                            // invalid epoch, i.e. artefact
                while ((!artList.isEmpty()) && (tt < ts)) { //keep the artefacts sorted by time
                    tevt t2 = artList.first();
                    tt = t2.end;
                    if (tt < ts) {
                        wmEvents(sess, evlists, WM_Artefact)->AddEvent(t2.end, t2.data);
                        artList.removeFirst();
                    }
                }
                wmEvents(sess, evlists, WM_Artefact)->AddEvent(ts, data);
            } else {
                ChannelID evch = wmEventChannel(evcode);
                if (evch != NoChannel) {
                    dayEvents.append({qint64(ts), data, evch});
                }
            }
            p += 6;
        }

        for (const WmEvent &e : wmSuppress(dayEvents)) {
            wmEvents(sess, evlists, e.channel)->AddEvent(e.end, e.duration);
        }

        while (!artList.isEmpty()) {
            tevt t2 = artList.takeFirst();
            wmEvents(sess, evlists, WM_Artefact)->AddEvent(t2.end, t2.data);
        }


        sess->UpdateSummaries();
        addSession(sess);
        imported++;

        reportProgress(60 + int((i * 30) / compinfo.size()));
    }
    reportProgress(90);

    delete[] yearco;
    delete[] ycompli;
    delete[] humi;
    delete[] data;
    delete[] st;
    delete[] startf;
    delete[] stl;
    delete[] sts;
    delete[] pres;
    delete[] amv;
    delete[] ev;
    mhumiuse.clear();

    if (isAborted()) {
        // Nothing is saved, so the next import starts from scratch. Saving the week data alone would
        // block the detailed data of those nights later, because known nights are skipped.
        qDeleteAll(new_sessions);
        new_sessions.clear();
        compinfo.clear();
        return 0;
    }

    // week data sessions that get replaced by detailed data
    for (Session *old : replaced) {
        old->Destroy();
        delete old;
    }

    reportProgress(95);

    finishAddingSessions();
    mach->Save();
    reportProgress(100);

    compinfo.clear();

    return imported;
}

void WeinmannLoader::initChannels()
{
    using namespace schema;
    Channel *chan = nullptr;

    // Events
    schema::channel.add(GRP_CPAP, new Channel(WM_Artefact = 0xd123, SPAN, MT_CPAP, SESSION,
                                              "WM_Artefact", QObject::tr("Artefact"),
                                              QObject::tr("Falsification of a signal which stops event detection. e.g. change in position, coughing, speaking, swallowing etc."),
                                              //: Displayname on the events-graph - keep this short!
                                              QObject::tr("Art"), STR_UNIT_EventsPerHour, DEFAULT, QColor("#B2F0B2"))); //somewhat greenish

    //Epochs (Events)
    schema::channel.add(GRP_CPAP, new Channel(WM_EpFlowLimit = 0xd131, SPAN, MT_CPAP, SESSION,
                                              "WM_EpFlowLimit", QObject::tr("Epoch flow limitation"),
                                              QObject::tr("An epoch with flowlimitation."),
                                              //: Displayname for "epoch with flowlimitation" on the events-graph - keep this short!
                                              QObject::tr("eFL"), STR_UNIT_EventsPerHour, DEFAULT, QColor("#f0dc76"))); //brownish


    schema::channel.add(GRP_CPAP, new Channel(WM_EpSnore = 0xd132, SPAN, MT_CPAP, SESSION,
                                              "WM_EpSnore", QObject::tr("Epoch Snore"),
                                              QObject::tr("An epoch with snoring."),
                                              //: Displayname for "epoch with snoring" on the events-graph - keep this short!
                                              QObject::tr("eSN"), STR_UNIT_EventsPerHour, DEFAULT, QColor("#FFD7D7"))); //redish

    schema::channel.add(GRP_CPAP, new Channel(WM_EpMildObs = 0xd133, SPAN, MT_CPAP, SESSION,
                                              "WM_EpMildObs", QObject::tr("Epoch mild Obstruction"),
                                              QObject::tr("An epoch with mild obstruction."),
                                              //: Displayname for "epoch with mild obstruction" on the graph-legend - keep this short!
                                              QObject::tr("emO"), STR_UNIT_EventsPerHour, DEFAULT, QColor("#CCCCFF"))); // blueish

    schema::channel.add(GRP_CPAP, new Channel(WM_EpSevereObs = 0xd134, SPAN, MT_CPAP, SESSION,
                                              "WM_EpSevereObs", QObject::tr("Epoch severe Obstruction"),
                                              QObject::tr("An epoch with severe obstruction."),
                                              //: Displayname for "epoch with severe Obstruction" on the events-graph - keep this short!
                                              QObject::tr("esO"), STR_UNIT_EventsPerHour, DEFAULT, QColor("#FFA8D4"))); //pinkish?


    // Waveforms
    schema::channel.add(GRP_CPAP, new Channel(WM_rRMV = 0xd140, WAVEFORM, MT_CPAP, SESSION,
                                              "WM_rRMV", QObject::tr("rRMV"),
                                              QObject::tr("Relative respiratory minute volume (100 = reference value of the device)"),
                                              QObject::tr("rRMV"), STR_UNIT_Unknown, DEFAULT, QColor("#8E5BC4")));

    //Settings
    channel.add(GRP_CPAP, chan = new Channel(WM_HumidStatus = 0xd200, SETTING, MT_CPAP, SESSION,
                                             "WM_HumidStat", QObject::tr("Humid. Status"),
                                             QObject::tr("Humidifier Enabled Status"),
                                             QObject::tr("Humidifier Status"),
                                             "", LOOKUP));

    chan->addOption(0, STR_TR_Off);
    chan->addOption(1, STR_TR_On);

    channel.add(GRP_CPAP, chan = new Channel(WM_HumidLevel = 0xd201, SETTING, MT_CPAP, SESSION,
                                             "WM_HumidLevel", QObject::tr("Humid. Level"),
                                             QObject::tr("Humidity Level"),
                                             QObject::tr("Humidity Level"),
                                             "", LOOKUP));

    chan->addOption(0, STR_TR_Off);
    chan->addOption(1, "1");
    chan->addOption(2, "2");
    chan->addOption(3, "3");
    chan->addOption(4, "4");
    chan->addOption(5, "5");
    chan->addOption(6, "6");
    chan->addOption(7, "7");
    chan->addOption(8, "8");

    channel.add(GRP_CPAP, chan = new Channel(WM_HumidUsage = 0xd202, SETTING, MT_CPAP, SESSION,
                                             "WM_HumidUsage", QObject::tr("Humid. Usage"),
                                             QObject::tr("Percentage of time the humidifier was used (for the entire day)"),
                                             QObject::tr("Humidifier Usage"),
                                             STR_UNIT_Percentage, DOUBLE));

    channel.add(GRP_CPAP, chan = new Channel(WM_SoftPAPLock = 0xd205, SETTING, MT_CPAP, SESSION,
                                             "WM_SoftPAPLock", QObject::tr("SoftPAP Lock"),
                                             QObject::tr("Shows if the SoftPAP setting can be changed"),
                                             QObject::tr("SoftPAP Lock"),
                                             "", LOOKUP, Qt::green));

    chan->addOption(0, QObject::tr("Available"));
    chan->addOption(1, QObject::tr("Locked"));

    channel.add(GRP_CPAP, chan = new Channel(WM_SoftPAPLevel = 0xd206, SETTING, MT_CPAP, SESSION,
                                             "WM_SoftPAPLevel", QObject::tr("SoftPAP Level"),
                                             QObject::tr("Breathing Relief Level"),
                                             QObject::tr("SoftPAP Level"),
                                             "", LOOKUP, Qt::blue));

    chan->addOption(0, STR_TR_Off);
    chan->addOption(1, QObject::tr("Gentle"));
    chan->addOption(2, QObject::tr("Normal"));
    chan->addOption(3, QObject::tr("Breathing relief with inhalation assistance")); //which the somno-series doesn't have!

    channel.add(GRP_CPAP, chan = new Channel(WM_SoftStart = 0xd207, SETTING, MT_CPAP, SESSION,
                                             "WM_SoftStart", QObject::tr("Soft Start"),
                                             QObject::tr("Soft Start (Ramp) Status"),
                                             QObject::tr("Soft Start"),
                                             "", LOOKUP));

    chan->addOption(0, STR_TR_Off);
    chan->addOption(1, STR_TR_On);

    // Softstart-Settings in RampTime and RampPressure


    channel.add(GRP_CPAP, chan = new Channel(WM_PressureRise = 0xd210, SETTING, MT_CPAP, SESSION,
                                             "WM_PressureRise", QObject::tr("Pressure Rise"),
                                             QObject::tr("Speed of pressure increase"),
                                             QObject::tr("Pressure Rise"),
                                             QObject::tr("cmH2O/s"), DEFAULT));

    channel.add(GRP_CPAP, chan = new Channel(WM_Masktest = 0xd220, SETTING, MT_CPAP, SESSION,
                                             "WM_Masktest", QObject::tr("Masktest Pressure"),
                                             QObject::tr("Pressure for the Masktest at startup"),
                                             QObject::tr("Masktest Pressure"),
                                             STR_UNIT_CMH2O, DEFAULT));

    channel.add(GRP_CPAP, chan = new Channel(WM_Autostart = 0xd221, SETTING, MT_CPAP, SESSION,
                                             "WM_Autostart", QObject::tr("Automatic Start"),
                                             QObject::tr("Automatic Start"),
                                             QObject::tr("Automatic Start"),
                                             "", LOOKUP));

    chan->addOption(0, STR_TR_Off);
    chan->addOption(1, STR_TR_On);

    channel.add(GRP_CPAP, chan = new Channel(WM_InfoMenu = 0xd222, SETTING, MT_CPAP, SESSION,
                                             "WM_InfoMenu", QObject::tr("Patient Information Menu"),
                                             QObject::tr("Shows if the patient menu is available"),
                                             QObject::tr("Patient Information Menu"),
                                             "", LOOKUP));

    chan->addOption(0, QObject::tr("Available"));
    chan->addOption(1, QObject::tr("Locked"));

    channel.add(GRP_CPAP, chan = new Channel(WM_StoreDiary = 0xd223, SETTING, MT_CPAP, SESSION,
                                             "WM_StoreDiary", QObject::tr("Therapy Diary"),
                                             QObject::tr("Shows if the therapy diary is saved on the SD card"),
                                             QObject::tr("Therapy Diary"),
                                             "", LOOKUP));

    chan->addOption(0, STR_TR_Off);
    chan->addOption(1, STR_TR_On);

    channel.add(GRP_CPAP, chan = new Channel(WM_AutoStorage = 0xd230, SETTING, MT_CPAP, SESSION,
                                             "WM_AutoStorage", QObject::tr("Automatic Storage"),
                                             QObject::tr("Shows which information is automatically saved to the SD card after each session"),
                                             QObject::tr("Automatic Storage"),
                                             "", LOOKUP));

    chan->addOption(0, QObject::tr("Compliance"));
    chan->addOption(1, QObject::tr("Compliance + efficacy"));
    chan->addOption(3, QObject::tr("Detailed signal data"));

    channel.add(GRP_CPAP, chan = new Channel(WM_ManStorage = 0xd231, SETTING, MT_CPAP, SESSION,
                                             "WM_ManStorage", QObject::tr("Manual Storage"),
                                             QObject::tr("Shows which information is saved to the SD card by holding down the Info key"),
                                             QObject::tr("Manual Storage"),
                                             "", LOOKUP));

    chan->addOption(0, QObject::tr("Compliance"));
    chan->addOption(1, QObject::tr("Compliance + efficacy"));
    chan->addOption(3, QObject::tr("Detailed signal data"));

    channel.add(GRP_CPAP, chan = new Channel(WM_UsedTime = 0xd240, SETTING, MT_CPAP, SESSION,
                                             "WM_UsedTime", QObject::tr("Total usage time"),
                                             QObject::tr("Total usage time of this device by you (at import)"),
                                             QObject::tr("Total usage time"),
                                             STR_UNIT_Hours, DOUBLE));

    channel.add(GRP_CPAP, chan = new Channel(WM_TherapyTime = 0xd241, SETTING, MT_CPAP, SESSION,
                                             "WM_TherapyTime", QObject::tr("Total therapy time"),
                                             QObject::tr("Total therapy time of this device by you (at import)"),
                                             QObject::tr("Total therapy time"),
                                             STR_UNIT_Hours, DOUBLE));

    channel.add(GRP_CPAP, chan = new Channel(WM_HumidifierTime = 0xd242, SETTING, MT_CPAP, SESSION,
                                             "WM_HumidifierTime",
                                             //: Name
                                             QObject::tr("Total humidifier time"),
                                             //: Explanation
                                             QObject::tr("Total usage time of the humidifier by you (at import)"),
                                             //: Displayname
                                             QObject::tr("Total humidifier time"),
                                             STR_UNIT_Hours, DOUBLE));


    channel.add(GRP_CPAP, chan = new Channel(WM_Firmware = 0xd250, SETTING, MT_CPAP, SESSION,
                                             "WM_Firmware",
                                             QObject::tr("Firmware Version"),
                                             QObject::tr("Firmware Version"),
                                             QObject::tr("Firmware Version"),
                                             "", STRING));

    schema::channel[WM_Artefact].setOrder(103);
    schema::channel[WM_EpSnore].setOrder(101);
    schema::channel[WM_EpFlowLimit].setOrder(102);
    schema::channel[WM_EpSevereObs].setOrder(110);
    schema::channel[WM_EpMildObs].setOrder(111);
}

bool weinmann_initialized = false;
void WeinmannLoader::Register()
{
    if (weinmann_initialized) {
        return;
    }

    qDebug() << "Registering WeinmannLoader";
    RegisterLoader(new WeinmannLoader());
    weinmann_initialized = true;
}
