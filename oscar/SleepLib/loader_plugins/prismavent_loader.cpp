/* SleepLib Löwenstein prisma VENT Loader Implementation
 *
 * Imports SD cards written by Löwenstein (formerly Weinmann) prisma VENT home
 * ventilators. See prismavent_loader.h for the card layout and
 * Notes/loaders/PRISMA_VENT_CARD_ANALYSIS.md for the format analysis.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <functional>

#include "SleepLib/schema.h"
#include "SleepLib/importcontext.h"
#include "SleepLib/session.h"
#include "SleepLib/profiles.h"
#include "prismavent_loader.h"
#include "prisma_loader.h"      // WMEDFInfo: the signal files are Prisma-style 8/16-bit WMEDF

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../thirdparty/miniz.h"

//********************************************************************************************
/// IMPORTANT!!!
//********************************************************************************************
// Please INCREMENT the prismavent_data_version in prismavent_loader.h when making changes to
// this loader that change loader behaviour or modify channels.
//********************************************************************************************

ChannelID PrismaVent_Mode = 0, PrismaVent_Program = 0, PrismaVent_BackupRate = 0,
          PrismaVent_Ti = 0, PrismaVent_TiMin = 0, PrismaVent_TiMax = 0,
          PrismaVent_TriggerInsp = 0, PrismaVent_TriggerExp = 0,
          PrismaVent_InspRamp = 0, PrismaVent_ExpRamp = 0, PrismaVent_TriggerType = 0;
ChannelID PrismaVent_ePC = 0, PrismaVent_Disconnect = 0, PrismaVent_SEL = 0;

// Event channels registered by the Prisma loader. The prisma VENT writes the same
// respiratory event ids as the Prisma LINE, so the same channels are used rather
// than duplicated under new ids.
extern ChannelID Prisma_Artifact, Prisma_CriticalLeak, Prisma_DeepSleep, Prisma_TimedBreath;
extern ChannelID Prisma_eSO, Prisma_eMO, Prisma_eS, Prisma_eF;

static const char * DEVICE_FILE = "device.xml";
static const char * MARKER_FILE = "prismaVENT.sdpvdat";
static const char * CONFIG_FILE = "configuration.json";

//! TherapyMode values, each confirmed against prismaTS for the same card: S/T and
//! MPVp on a VENT50-C (MPVp was set in an unused program), autoST on a VENT40.
//! prismaTS's own numbered mode list puts T at 3 and MPVp at 7, so it is not the
//! device's numbering.
static const int PRISMAVENT_MODE_ST = 2;
static const int PRISMAVENT_MODE_AUTOST = 3;
static const int PRISMAVENT_MODE_MPVP = 8;

//! TriggerType values: prismaTS shows "Trigger: Manually" for 0 and "Trigger: auto"
//! for 1. Only a manual trigger uses the sensitivity levels.
static const int PRISMAVENT_TRIGGER_MANUAL = 0;
static const int PRISMAVENT_TRIGGER_AUTO = 1;

//! Respiratory event ids not in Prisma_Event_Type. Names are prismaTS's own
//! (EventsNames/RespEvent_<id> in its localization resources).
static const int PRISMAVENT_EVENT_EPOCH_PREMATURE_CYCLING = 6; //!< ePC, 2-min epoch, strength 1-3
static const int PRISMAVENT_EVENT_CLOSED_APNEA_HIGH_PRESSURE = 108; //!< hPrA
static const int PRISMAVENT_EVENT_HYPOPNEA_LEAKAGE = 113;   //!< unclassified, very high leakage
static const int PRISMAVENT_EVENT_DISCONNECTION = 171;
//! softSTART spans exactly the configured softSTART time, during which the
//! IPAP/EPAP targets ramp up; softSTOP is its counterpart at the end of therapy.
static const int PRISMAVENT_EVENT_SOFTSTART = 241;
static const int PRISMAVENT_EVENT_SOFTSTOP = 242;
static const int PRISMAVENT_EVENT_SUDDEN_DECLINE_OF_LEAKAGE = 330; //!< "SEL" in prismaTS reports

//********************************************************************************************
// Parsing helpers
//********************************************************************************************

namespace PrismaVentParsing {

bool parseLogTime(const QString & text, qint64 & msOut)
{
    static const QRegularExpression re(QStringLiteral("^\\+?(\\d+):(\\d{2}):(\\d{2})(?:\\.(\\d{1,3}))?$"));
    const QRegularExpressionMatch m = re.match(text.trimmed());
    if (!m.hasMatch()) {
        return false;
    }
    QString frac = m.captured(4);
    while (frac.size() < 3) frac += QLatin1Char('0');
    msOut = ((m.captured(1).toLongLong() * 60 + m.captured(2).toLongLong()) * 60
             + m.captured(3).toLongLong()) * 1000 + frac.toLongLong();
    return true;
}

bool ParameterLog::load(const QByteArray & parameterXml, const QByteArray & mapXml)
{
    QDomDocument mapDoc;
    if (!mapDoc.setContent(mapXml)) {
        qWarning() << "PrismaVent: parametersmap.xml is not valid XML";
        return false;
    }
    const QDomNodeList mapNodes = mapDoc.documentElement().elementsByTagName("parameter");
    for (int i = 0; i < mapNodes.count(); i++) {
        const QDomElement e = mapNodes.item(i).toElement();
        m_ids.insert(e.attribute("name"), e.attribute("id").toInt());
    }

    QDomDocument doc;
    if (!doc.setContent(parameterXml)) {
        qWarning() << "PrismaVent: parameter.xml is not valid XML";
        return false;
    }
    const QDomElement root = doc.documentElement();
    const QDate logDate = QDate::fromString(root.attribute("date"), Qt::ISODate);
    if (!logDate.isValid()) {
        qWarning() << "PrismaVent: parameter.xml has no date";
        return false;
    }
    // Same zone convention as the WMEDF start time, so the two can be compared.
    const qint64 midnight = QDateTime(logDate, QTime(0, 0), EDFInfo::localNoDST).toMSecsSinceEpoch();

    const QDomNodeList nodes = root.elementsByTagName("parameter");
    for (int i = 0; i < nodes.count(); i++) {
        const QDomElement e = nodes.item(i).toElement();
        qint64 offset;
        if (!parseLogTime(e.attribute("time"), offset)) {
            continue;
        }
        const int id = e.attribute("id").toInt();
        const int value = e.attribute("value").toInt();
        const QPair<qint64, int> entry(midnight + offset, value);
        if (e.hasAttribute("program")) {
            const int program = e.attribute("program").toInt();
            if (program >= 0 && program < 3) {
                m_program[program][id].append(entry);
            }
        } else {
            m_global[id].append(entry);
        }
    }

    // File order is time order, but a stable sort costs nothing and protects valueAt().
    auto byTime = [](const QPair<qint64, int> & a, const QPair<qint64, int> & b) { return a.first < b.first; };
    for (auto & timeline : m_global) std::stable_sort(timeline.begin(), timeline.end(), byTime);
    for (auto & program : m_program) {
        for (auto & timeline : program) std::stable_sort(timeline.begin(), timeline.end(), byTime);
    }
    return true;
}

int ParameterLog::valueAt(const Timeline & timeline, qint64 atMs, bool * ok)
{
    int value = 0;
    bool found = false;
    for (const auto & entry : timeline) {
        if (entry.first > atMs) break;
        value = entry.second;
        found = true;
    }
    if (ok) *ok = found;
    return value;
}

int ParameterLog::globalValue(const QString & name, qint64 atMs, bool * ok) const
{
    if (!m_ids.contains(name)) {
        if (ok) *ok = false;
        return 0;
    }
    return valueAt(m_global.value(m_ids.value(name)), atMs, ok);
}

int ParameterLog::programValue(const QString & name, int program, qint64 atMs, bool * ok) const
{
    if (!m_ids.contains(name) || program < 0 || program >= 3) {
        if (ok) *ok = false;
        return 0;
    }
    return valueAt(m_program[program].value(m_ids.value(name)), atMs, ok);
}

QList<RespEvent> parseRespEvents(const QByteArray & xml)
{
    QList<RespEvent> events;
    QDomDocument doc;
    if (!doc.setContent(xml)) {
        return events;
    }
    // Index into events of the begin still waiting for its end, per event id.
    QHash<int, QList<int>> open;
    const QDomNodeList nodes = doc.documentElement().elementsByTagName("RespEvent");
    for (int i = 0; i < nodes.count(); i++) {
        const QDomElement e = nodes.item(i).toElement();
        const int id = e.attribute("id").toInt();
        const qint64 ms = qint64(e.attribute("time").toDouble() * 1000.0);
        const QString kind = e.attribute("event");
        if (kind == QLatin1String("begin")) {
            RespEvent ev;
            ev.id = id;
            ev.beginMs = ms;
            ev.strength = e.attribute("Strength").toInt();
            open[id].append(events.size());
            events.append(ev);
        } else if (kind == QLatin1String("end")) {
            QList<int> & pending = open[id];
            if (pending.isEmpty()) {
                // Already under way when the file began: one card has six softSTART
                // ramps that end, without a begin, up to 1799 s into a session.
                RespEvent ev;
                ev.id = id;
                ev.beginMs = 0;
                ev.endMs = ms;
                ev.strength = e.attribute("Strength").toInt();
                events.append(ev);
                continue;
            }
            events[pending.takeFirst()].endMs = ms;
        }
    }
    return events;
}

} // namespace PrismaVentParsing

using namespace PrismaVentParsing;

//********************************************************************************************
// Archive access
//********************************************************************************************

/*! \class DayArchive
    \brief Read-only access to one day's zip archive held in memory. */
class DayArchive
{
public:
    DayArchive() { memset(&m_zip, 0, sizeof(m_zip)); }
    ~DayArchive() { if (m_open) mz_zip_reader_end(&m_zip); }

    //! \brief Reads the whole archive into memory and opens it.
    bool open(const QString & path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        m_data = f.readAll();
        // The reader works directly on m_data's buffer, so m_data must outlive it.
        m_open = mz_zip_reader_init_mem(&m_zip, m_data.constData(), size_t(m_data.size()), 0);
        return m_open;
    }

    //! \brief Names of all members.
    QStringList names() {
        QStringList list;
        const int n = int(mz_zip_reader_get_num_files(&m_zip));
        for (int i = 0; i < n; i++) {
            mz_zip_archive_file_stat st;
            if (mz_zip_reader_file_stat(&m_zip, mz_uint(i), &st)) {
                list.append(QString::fromUtf8(st.m_filename));
            }
        }
        return list;
    }

    //! \brief Extracts a member, or returns an empty array if it is absent.
    QByteArray extract(const QString & name) {
        size_t size = 0;
        void * p = mz_zip_reader_extract_file_to_heap(&m_zip, name.toUtf8().constData(), &size, 0);
        if (p == nullptr) return QByteArray();
        QByteArray data(static_cast<const char *>(p), qsizetype(size));
        mz_free(p);
        return data;
    }

private:
    QByteArray m_data;
    mz_zip_archive m_zip;
    bool m_open = false;
};

//! Matches a day archive name, capturing its date: <counter>_<yyyy-mm-dd>.zip
static const QRegularExpression & dayArchivePattern()
{
    static const QRegularExpression re(QStringLiteral("^\\d+_(\\d{4}-\\d{2}-\\d{2})\\.zip$"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

//! Day archives in \a deviceDir, oldest first.
static QStringList dayArchives(const QString & deviceDir)
{
    QStringList result;
    const QStringList files = QDir(deviceDir).entryList(QStringList("*.zip"), QDir::Files, QDir::Name);
    for (const QString & f : files) {
        if (dayArchivePattern().match(f).hasMatch()) result.append(f);
    }
    // The leading counter can restart, so order by the date and then by name.
    std::stable_sort(result.begin(), result.end(), [](const QString & a, const QString & b) {
        return dayArchivePattern().match(a).captured(1) < dayArchivePattern().match(b).captured(1);
    });
    return result;
}

//********************************************************************************************
// Loader
//********************************************************************************************

PrismaVentLoader::PrismaVentLoader()
{
    m_type = MT_CPAP;
}

PrismaVentLoader::~PrismaVentLoader()
{
}

//! True if \a dirPath holds a prisma VENT device.xml next to one of its companion files.
static bool isDeviceDir(const QString & dirPath)
{
    const QDir dir(dirPath);
    if (!dir.exists(DEVICE_FILE)) return false;
    if (!dir.exists(MARKER_FILE) && !dir.exists(CONFIG_FILE)) return false;

    // device.xml is under 1 KB; checking its root element rules out other devices'
    // files of the same name.
    QFile f(dir.absoluteFilePath(DEVICE_FILE));
    if (!f.open(QIODevice::ReadOnly)) return false;
    return f.read(4096).contains("<Device_settings>");
}

QStringList PrismaVentLoader::findDeviceDirs(const QString & path)
{
    QStringList found;
    if (path.isEmpty()) return found;
    if (isDeviceDir(path)) {
        found.append(QDir(path).absolutePath());
        return found;
    }
    const QDir dir(path);
    const QStringList children = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString & child : children) {
        const QString childPath = dir.absoluteFilePath(child);
        if (isDeviceDir(childPath)) {
            found.append(childPath);
        } else if (child.startsWith(QLatin1String("WM"), Qt::CaseInsensitive)) {
            const QDir wmDir(childPath);
            const QStringList grandchildren = wmDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QString & g : grandchildren) {
                const QString gPath = wmDir.absoluteFilePath(g);
                if (isDeviceDir(gPath)) found.append(gPath);
            }
        }
    }
    return found;
}

QString PrismaVentLoader::selectDeviceDir(const QString & path)
{
    const QStringList dirs = findDeviceDirs(path);
    if (dirs.isEmpty()) return QString();

    QString best = dirs.first();
    QString bestDate;
    for (const QString & d : dirs) {
        const QStringList zips = dayArchives(d);
        if (zips.isEmpty()) continue;
        const QString newest = dayArchivePattern().match(zips.last()).captured(1);
        if (newest > bestDate) {
            bestDate = newest;
            best = d;
        }
    }
    if (dirs.size() > 1) {
        qDebug() << "PrismaVent:" << dirs.size() << "device folders found; importing" << QDir(best).dirName();
    }
    return best;
}

bool PrismaVentLoader::Detect(const QString & path)
{
    return !findDeviceDirs(path).isEmpty();
}

MachineInfo PrismaVentLoader::readDeviceInfo(const QString & deviceDir)
{
    QFile f(QDir(deviceDir).absoluteFilePath(DEVICE_FILE));
    if (!f.open(QIODevice::ReadOnly)) {
        return MachineInfo();
    }
    QDomDocument doc;
    if (!doc.setContent(f.readAll())) {
        qWarning() << "PrismaVent: device.xml is not valid XML";
        return MachineInfo();
    }
    const QDomElement root = doc.documentElement();
    auto text = [&root](const char * tag) { return root.firstChildElement(tag).text().trimmed(); };

    MachineInfo info = newInfo();
    info.serial = text("Device_sn");
    if (info.serial.isEmpty()) {
        qWarning() << "PrismaVent: device.xml has no serial number";
        return MachineInfo();
    }
    // Device_type is the numeric product type (1 on the VENT40, 5 on the VENT50-C)
    // and Device_type_str its short name ("V40", "V50-C"). Names are those prismaTS
    // gives each product type under the Löwenstein branding.
    info.modelnumber = text("Device_type");
    static const QHash<QString, QString> productNames = {
        { "0", "prisma VENT30" },   { "1", "prisma VENT40" },   { "2", "prisma VENT50" },
        { "3", "prisma VENT30-C" }, { "5", "prisma VENT50-C" }, { "6", "prisma VENT50-C" },
        { "7", "prisma VENT40-C" },
    };
    const QString typeName = text("Device_type_str");
    if (productNames.contains(info.modelnumber)) {
        info.model = productNames.value(info.modelnumber);
    } else if (!typeName.isEmpty()) {
        info.model = QStringLiteral("prisma VENT ") + typeName;
    }
    const QString firmware = text("Device_fw");
    if (!firmware.isEmpty()) {
        // Key spelled as in prisma_loader.cpp and bmcg3x_loader.cpp.
        info.properties["firmware"] = firmware;
    }
    const QString wmNumber = text("WM_number");
    if (!wmNumber.isEmpty()) {
        info.properties["WM_number"] = wmNumber;
    }
    return info;
}

MachineInfo PrismaVentLoader::PeekInfo(const QString & path)
{
    const QString deviceDir = selectDeviceDir(path);
    if (deviceDir.isEmpty()) return MachineInfo();
    return readDeviceInfo(deviceDir);
}

void PrismaVentLoader::backupDeviceDir(const QString & deviceDir, const MachineInfo & info)
{
    const QDir src(deviceDir);
    // Named from device.xml rather than the parent folder, which is not WM...
    // when the user imports an SN folder copied elsewhere; findDeviceDirs()
    // only looks inside WM... folders.
    QString wmName = QFileInfo(src.absolutePath()).dir().dirName();
    if (info.properties.contains("WM_number")) {
        wmName = QStringLiteral("WM") + info.properties.value("WM_number");
    } else if (!wmName.startsWith(QLatin1String("WM"), Qt::CaseInsensitive)) {
        wmName = QStringLiteral("WM");
    }
    const QDir dst(QDir(context()->GetBackupPath()).absoluteFilePath(wmName + "/" + src.dirName()));

    // Rebuild opens the backup itself: never copy a folder onto itself.
    if (src == dst || !p_profile->session->backupCardData()) {
        return;
    }

    emit updateMessage(QObject::tr("Backing Up Files..."));
    QCoreApplication::processEvents();

    // Walk the device folder, skipping logs/: those are the device's own
    // diagnostic logs, several MB per device and of no use to a rebuild.
    std::function<void(const QString &, const QString &)> copyTree =
        [&](const QString & from, const QString & to) {
        QDir().mkpath(to);
        const QDir fromDir(from);
        for (const QFileInfo & fi : fromDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString target = QDir(to).absoluteFilePath(fi.fileName());
            if (fi.isDir()) {
                if (fi.fileName().compare(QLatin1String("logs"), Qt::CaseInsensitive) != 0) {
                    copyTree(fi.absoluteFilePath(), target);
                }
                continue;
            }
            const QFileInfo existing(target);
            if (existing.exists() && existing.size() == fi.size()) continue;
            if (existing.exists()) QFile::remove(target);
            if (!QFile::copy(fi.absoluteFilePath(), target)) {
                qWarning() << "PrismaVent: could not back up" << fi.fileName();
            }
        }
    };
    copyTree(src.absolutePath(), dst.absolutePath());
}

int PrismaVentLoader::Open(const QString & path)
{
    if (m_ctx == nullptr) {
        qWarning() << "PrismaVentLoader::Open() called without a valid m_ctx object present";
        return 0;
    }

    const QString deviceDir = selectDeviceDir(path);
    if (deviceDir.isEmpty()) {
        return -1;
    }
    MachineInfo info = readDeviceInfo(deviceDir);
    if (info.type == MT_UNKNOWN) {
        emit deviceIsUnsupported(info);
        return -1;
    }
    m_ctx->CreateMachineFromInfo(info);

    // The VENT50-C (Device_type 5 or 6) and VENT40 (1) have been checked against
    // prismaTS; any other prisma VENT model is untested.
    static const QSet<QString> checkedTypes = { "1", "5", "6" };
    if (!checkedTypes.contains(info.modelnumber)) {
        emit deviceIsUntested(info);
    }

    m_abort = false;
    m_reportedEventIds.clear();
    m_reportedModes.clear();
    emit setProgressValue(0);
    emit updateMessage(QObject::tr("Getting Ready..."));
    QCoreApplication::processEvents();

    backupDeviceDir(deviceDir, info);

    const QStringList zips = dayArchives(deviceDir);
    qDebug() << "PrismaVent: opening" << deviceDir << "with" << zips.size() << "day archives";

    emit updateMessage(QObject::tr("Importing Sessions..."));
    emit setProgressMax(zips.size());
    QCoreApplication::processEvents();

    int imported = 0;
    int progress = 0;
    for (const QString & zip : zips) {
        if (isAborted()) break;
        imported += importDayArchive(QDir(deviceDir).absoluteFilePath(zip));
        emit setProgressValue(++progress);
        QCoreApplication::processEvents();
    }

    m_ctx->FlushUnexpectedMessages();
    finishAddingSessions();
    qDebug() << "PrismaVent: imported" << imported << "sessions";
    return imported;
}

int PrismaVentLoader::importDayArchive(const QString & zipPath)
{
    DayArchive archive;
    if (!archive.open(zipPath)) {
        qWarning() << "PrismaVent: cannot open" << QFileInfo(zipPath).fileName();
        return 0;
    }

    ParameterLog params;
    const bool haveParams = params.load(archive.extract("parameter.xml"), archive.extract("parametersmap.xml"));
    if (!haveParams) {
        qWarning() << "PrismaVent: no usable parameter log in" << QFileInfo(zipPath).fileName();
    }

    // Sessions are numbered within the day: NNNN.wmedf with event_NNNN.xml.
    static const QRegularExpression signalName(QStringLiteral("^(\\d+)\\.wmedf$"), QRegularExpression::CaseInsensitiveOption);
    const QStringList names = archive.names();
    int added = 0;
    for (const QString & name : names) {
        if (isAborted()) break;
        const QRegularExpressionMatch m = signalName.match(name);
        if (!m.hasMatch()) continue;

        const QByteArray signalData = archive.extract(name);
        const QByteArray eventData = archive.extract(QStringLiteral("event_%1.xml").arg(m.captured(1)));
        Session * session = buildSession(signalData, eventData, haveParams ? &params : nullptr);
        if (session == nullptr) continue;
        if (m_ctx->AddSession(session)) {
            added++;
        }
        QCoreApplication::processEvents();
    }
    return added;
}

//! Adds one WMEDF signal to the session as a waveform.
//! \return false if the file has no signal labelled \a label.
static bool addWaveform(Session * session, WMEDFInfo & wmedf, qint64 startMs,
                        ChannelID code, const QString & label, bool skipIfAllZero = false)
{
    EDFSignal * es = wmedf.lookupLabel(label);
    if (es == nullptr || es->dataArray == nullptr) return false;

    const long recs = es->sampleCnt * wmedf.GetNumDataRecords();
    if (recs <= 0) return false;
    if (skipIfAllZero && std::all_of(es->dataArray, es->dataArray + recs, [](qint16 v) { return v == 0; })) {
        return false;
    }
    const qint64 duration = qint64(wmedf.GetNumDataRecords() * wmedf.GetDuration() * 1000.0);
    const double rate = double(duration) / double(recs);

    // The parser leaves offset at 0; every signal imported here has its physical
    // and digital ranges both anchored at zero, so gain alone is correct.
    EventList * el = session->AddEventList(code, EVL_Waveform, es->gain, es->offset, 0, 0, rate);
    el->setDimension(es->physical_dimension);
    el->AddWaveform(startMs, es->dataArray, int(recs), duration);
    session->setPhysMin(code, es->physical_minimum);
    session->setPhysMax(code, es->physical_maximum);
    return true;
}

Session * PrismaVentLoader::buildSession(const QByteArray & signalData, const QByteArray & eventData,
                                         const ParameterLog * params)
{
    WMEDFInfo wmedf;
    if (!wmedf.Open(signalData) || !wmedf.Parse()) {
        qWarning() << "PrismaVent: signal file unreadable";
        return nullptr;
    }
    const QDateTime start = wmedf.edfHdr.startdate_orig;
    if (!start.isValid()) {
        qWarning() << "PrismaVent: signal file has no start time";
        return nullptr;
    }
    const qint64 startMs = start.toMSecsSinceEpoch();
    const qint64 durationMs = qint64(wmedf.GetNumDataRecords() * wmedf.GetDuration() * 1000.0);
    if (durationMs <= 0) {
        return nullptr;
    }

    // The start time is unique per device and stable across re-exports of the
    // same day, which the per-day session counter in the file name is not.
    const SessionID sid = SessionID(start.toSecsSinceEpoch());
    if (m_ctx->SessionExists(sid)) {
        return nullptr;
    }
    if (m_ctx->ShouldIgnoreOldSessions() && start < m_ctx->IgnoreSessionsOlderThan()) {
        return nullptr;
    }

    Session * session = m_ctx->CreateSession(sid);
    session->really_set_first(startMs);
    session->really_set_last(startMs + durationMs);

    if (params != nullptr) {
        applySettings(session, *params, startMs);
    }

    // Waveforms. Pressures are in hPa, which OSCAR treats as cmH2O (1 hPa = 1.02 cmH2O),
    // as the Prisma loader does.
    addWaveform(session, wmedf, startMs, CPAP_MaskPressure, "Airway Pressure");
    addWaveform(session, wmedf, startMs, CPAP_FlowRate, "Patient Flow");
    addWaveform(session, wmedf, startMs, CPAP_Leak, "Leakage Flow");
    addWaveform(session, wmedf, startMs, CPAP_LeakTotal, "TotalLeakage");
    addWaveform(session, wmedf, startMs, CPAP_RespRate, "Frequency");
    addWaveform(session, wmedf, startMs, CPAP_TidalVolume, "Tidal Volume");
    addWaveform(session, wmedf, startMs, CPAP_MinuteVent, "Minute Volume");
    addWaveform(session, wmedf, startMs, CPAP_PTB, "Part Spont Insp.");
    addWaveform(session, wmedf, startMs, CPAP_IPAP, "IPAPsoll");
    addWaveform(session, wmedf, startMs, CPAP_EPAP, "EPAPsoll");
    // The oximetry signals are written as zeros when no sensor is connected.
    addWaveform(session, wmedf, startMs, OXI_SPO2, "SpO2 Value", true);
    addWaveform(session, wmedf, startMs, OXI_Pulse, "Pulse Rate", true);

    // Respiratory events. The id space is shared with the Prisma LINE (see
    // Prisma_Event_Type); names were checked against prismaTS's event dictionary.
    // Unclassified apneas and hypopneas go to the generic channels, as the Prisma
    // loader does for its unclassified apneas.
    static const QHash<int, ChannelID *> eventChannels = {
        { PRISMA_EVENT_OBSTRUCTIVE_APNEA,       &CPAP_Obstructive },
        { PRISMA_EVENT_CENTRAL_APNEA,           &CPAP_ClearAirway },
        { PRISMA_EVENT_APNEA_LEAKAGE,           &CPAP_Apnea },
        { PRISMA_EVENT_APNEA_HIGH_PRESSURE,     &CPAP_Apnea },
        { PRISMA_EVENT_APNEA_MOVEMENT,          &CPAP_Apnea },
        { PRISMAVENT_EVENT_CLOSED_APNEA_HIGH_PRESSURE, &CPAP_Apnea },
        { PRISMA_EVENT_OBSTRUCTIVE_HYPOPNEA,    &CPAP_ObstructiveHypopnea },
        { PRISMA_EVENT_CENTRAL_HYPOPNEA,        &CPAP_CentralHypopnea },
        { PRISMAVENT_EVENT_HYPOPNEA_LEAKAGE,    &CPAP_Hypopnea },
        { PRISMA_EVENT_RERA,                    &CPAP_RERA },
        { PRISMA_EVENT_SNORE,                   &CPAP_VSnore },
        { PRISMA_EVENT_ARTIFACT,                &Prisma_Artifact },
        { PRISMA_EVENT_FLOW_LIMITATION,         &CPAP_FlowLimit },
        { PRISMA_EVENT_CRITICAL_LEAKAGE,        &Prisma_CriticalLeak },
        { PRISMA_EVENT_CS_RESPIRATION,          &CPAP_CSR },
        { PRISMA_EVENT_TIMED_BREATH,            &Prisma_TimedBreath },
        { PRISMA_EVENT_EPOCH_DEEPSLEEP,         &Prisma_DeepSleep },
        { PRISMA_EVENT_EPOCH_SEVERE_OBSTRUCTION, &Prisma_eSO },
        { PRISMA_EVENT_EPOCH_MILD_OBSTRUCTION,  &Prisma_eMO },
        { PRISMA_EVENT_EPOCH_FLOW_LIMITATION,   &Prisma_eF },
        { PRISMA_EVENT_EPOCH_SNORE,             &Prisma_eS },
        { PRISMAVENT_EVENT_EPOCH_PREMATURE_CYCLING, &PrismaVent_ePC },
        { PRISMAVENT_EVENT_DISCONNECTION,       &PrismaVent_Disconnect },
        { PRISMAVENT_EVENT_SUDDEN_DECLINE_OF_LEAKAGE, &PrismaVent_SEL },
        { PRISMAVENT_EVENT_SOFTSTART,           &CPAP_Ramp },
        { PRISMAVENT_EVENT_SOFTSTOP,            &CPAP_Ramp },
    };

    // Collect (end time, duration, strength) per channel first: the file is not
    // in time order across event types, and an EventList must be.
    struct Flag { qint64 endMs; qint64 durationSec; int strength; };
    QHash<ChannelID, QList<Flag>> flags;
    const QList<RespEvent> events = parseRespEvents(eventData);
    for (const RespEvent & ev : events) {
        if (!eventChannels.contains(ev.id)) {
            if (!m_reportedEventIds.contains(ev.id)) {
                m_reportedEventIds.insert(ev.id);
                const QString message = QStringLiteral("Unknown prisma VENT respiratory event id %1").arg(ev.id);
                qWarning() << "PrismaVent:" << message;
                m_ctx->LogUnexpectedMessage(message);
            }
            continue;
        }
        // An event still open when the file ends runs to the end of the session,
        // except softSTART, which cannot outlast its configured time: one card
        // left it open through a six-hour session.
        qint64 openEndMs = durationMs;
        if (ev.id == PRISMAVENT_EVENT_SOFTSTART && session->settings.contains(CPAP_RampTime)) {
            openEndMs = ev.beginMs + session->settings[CPAP_RampTime].toLongLong() * 60000;
        }
        const qint64 endMs = qBound(ev.beginMs, ev.endMs < 0 ? openEndMs : ev.endMs, durationMs);
        // EventStoreType is 16-bit; no real event approaches the cap.
        const qint64 durationSec = qMin<qint64>((endMs - ev.beginMs) / 1000, 32767);
        flags[*eventChannels.value(ev.id)].append({ startMs + endMs, durationSec, ev.strength });
    }
    for (auto it = flags.begin(); it != flags.end(); ++it) {
        auto & list = it.value();
        std::stable_sort(list.begin(), list.end(), [](const Flag & a, const Flag & b) { return a.endMs < b.endMs; });
        EventList * el = session->AddEventList(it.key(), EVL_Event, 1.0, 0.0, 0.0, 0.0, 0.0, true);
        for (const Flag & f : list) {
            el->AddEvent(f.endMs, EventStoreType(f.durationSec), EventStoreType(f.strength));
        }
    }

    // The main event channels keep an (empty) list so they appear in the events
    // list from the first import - except OH and CH, whose presence is read as
    // "this device splits hypopneas by mechanism" (GitLab #261).
    for (ChannelID code : { CPAP_Obstructive, CPAP_ClearAirway, CPAP_Apnea, CPAP_RERA,
                            CPAP_VSnore, CPAP_FlowLimit, CPAP_CSR }) {
        if (!session->eventlist.contains(code)) {
            session->AddEventList(code, EVL_Event);
        }
    }

    session->SetChanged(true);
    return session;
}

void PrismaVentLoader::applySettings(Session * session, const ParameterLog & params, qint64 startMs)
{
    // The WMEDF start is truncated to the second; ventilation starts within it.
    const qint64 at = startMs + 999;

    bool ok = false;
    const int program = params.globalValue("ActiveProgram", at, &ok);
    if (!ok) {
        qWarning() << "PrismaVent: no ActiveProgram at session" << session->session();
        return;
    }
    session->settings[PrismaVent_Program] = program + 1;

    auto value = [&](const char * name, bool * found = nullptr) {
        return params.programValue(name, program, at, found);
    };

    bool haveMode = false;
    const int mode = value("TherapyMode", &haveMode);
    if (!haveMode) {
        return;
    }
    session->settings[PrismaVent_Mode] = mode;

    // Settings shared by the spontaneous/timed modes. Each matched prismaTS's
    // settings table for the same session, in S/T and autoST (F min, Ti min/max,
    // Trigger, Trigger IN/EX, RAMP IN/EX, softSTART max).
    auto applyCommon = [&]() {
        session->settings[PrismaVent_BackupRate] = value("Frequency") / 10.0;
        session->settings[PrismaVent_TiMin] = value("Ti_min") / 1000.0;
        session->settings[PrismaVent_TiMax] = value("Ti_max") / 1000.0;
        // prismaTS lists Trigger IN/EX only for a manual trigger.
        bool haveTrigger = false;
        const int triggerType = value("TriggerType", &haveTrigger);
        if (haveTrigger) {
            session->settings[PrismaVent_TriggerType] = triggerType;
        }
        if (!haveTrigger || triggerType == PRISMAVENT_TRIGGER_MANUAL) {
            session->settings[PrismaVent_TriggerInsp] = value("TriggerSensitivityInspiration");
            session->settings[PrismaVent_TriggerExp] = value("TriggerSensitivityExspiration");
        }
        session->settings[PrismaVent_InspRamp] = value("InspirationRamp");
        session->settings[PrismaVent_ExpRamp] = value("ExpirationRamp");
        session->settings[CPAP_RampTime] = value("sSTART_T");
    };

    if (mode == PRISMAVENT_MODE_ST) {
        // IPAP, EPAP, Ti timed and softSTART pressure also matched the report.
        const double ipap = value("IPAP") / 100.0;
        const double epap = value("EPAP") / 100.0;
        session->settings[CPAP_Mode] = (int)MODE_BILEVEL_FIXED;
        session->settings[CPAP_EPAP] = epap;
        session->settings[CPAP_IPAP] = ipap;
        session->settings[CPAP_PS] = ipap - epap;
        applyCommon();
        // Ti_timed, not Ti: prismaTS lists "Ti timed" for S/T; Ti belongs to
        // modes where every breath is timed (it shows for the MPVp program).
        session->settings[PrismaVent_Ti] = value("Ti_timed") / 1000.0;
        session->settings[CPAP_RampPressure] = value("sSTART_EPAP") / 100.0;
    } else if (mode == PRISMAVENT_MODE_AUTOST) {
        // prismaTS lists EPAPmin, EPAPmax and delta-Pinsp for this mode, not IPAP
        // or EPAP. On the VENT40 card the EPAPsoll targets stay between EPAPmin
        // and EPAPmax and IPAPsoll sits delta-Pinsp above them. softSTART ramps
        // EPAP up from sSTART_EPAPmin, not sSTART_EPAP. prismaTS shows "Ti timed:
        // auto" (Ti_timed = 0), so no Ti is reported.
        const double epapMin = value("AutoEPAPmin") / 100.0;
        const double epapMax = value("AutoEPAPmax") / 100.0;
        const double ps = value("AutoEPAPdeltaPinsp") / 100.0;
        session->settings[CPAP_Mode] = (int)MODE_BILEVEL_AUTO_FIXED_PS;
        session->settings[CPAP_EPAPLo] = epapMin;
        session->settings[CPAP_EPAPHi] = epapMax;
        session->settings[CPAP_IPAPLo] = epapMin + ps;
        session->settings[CPAP_IPAPHi] = epapMax + ps;
        session->settings[CPAP_PS] = ps;
        applyCommon();
        session->settings[CPAP_RampPressure] = value("sSTART_EPAPmin") / 100.0;
    } else {
        // Which parameters apply differs by mode (volume modes, CPAP, high flow),
        // so nothing is published until a sample of the mode has been checked.
        session->settings[CPAP_Mode] = (int)MODE_UNKNOWN;
        if (!m_reportedModes.contains(mode)) {
            m_reportedModes.insert(mode);
            const QString message = QStringLiteral("Unknown prisma VENT therapy mode %1").arg(mode);
            qWarning() << "PrismaVent:" << message;
            m_ctx->LogUnexpectedMessage(message);
        }
    }
}

//********************************************************************************************

using namespace schema;

void PrismaVentLoader::initChannels()
{
    Channel * chan = nullptr;

    // Daily shows the mode through this channel's options, so every value needs one.
    // Values are numbered until each is matched to the vendor's name. 2, 3 and 8
    // are named from prismaTS for the same cards (see the analysis notes).
    channel.add(GRP_CPAP, chan = new Channel(PrismaVent_Mode = 0xe580, SETTING, MT_CPAP, SESSION,
        "PrismaVent_Mode", QObject::tr("Mode"),
        QObject::tr("Ventilation mode"),
        QObject::tr("Mode"),
        "", LOOKUP, Qt::green));
    for (int mode = 0; mode < 16; mode++) {
        chan->addOption(mode, QObject::tr("Mode %1").arg(mode));
    }
    chan->addOption(PRISMAVENT_MODE_ST, QObject::tr("S/T"));
    chan->addOption(PRISMAVENT_MODE_AUTOST, QObject::tr("autoS/T"));
    chan->addOption(PRISMAVENT_MODE_MPVP, QObject::tr("MPVp"));

    channel.add(GRP_CPAP, new Channel(PrismaVent_Program = 0xe581, SETTING, MT_CPAP, SESSION,
        "PrismaVent_Program", QObject::tr("Therapy program"),
        QObject::tr("Therapy program in use"),
        QObject::tr("Program"),
        "", INTEGER, Qt::green));

    channel.add(GRP_CPAP, new Channel(PrismaVent_BackupRate = 0xe582, SETTING, MT_CPAP, SESSION,
        "PrismaVent_BackupRate", QObject::tr("Backup rate"),
        QObject::tr("Breath rate below which the device delivers timed breaths"),
        QObject::tr("Backup rate"),
        STR_UNIT_BreathsPerMinute, DEFAULT, Qt::green));

    channel.add(GRP_CPAP, new Channel(PrismaVent_Ti = 0xe583, SETTING, MT_CPAP, SESSION,
        "PrismaVent_Ti", QObject::tr("Inspiration time"),
        QObject::tr("Inspiration time of a timed breath"),
        QObject::tr("Ti"),
        STR_UNIT_Seconds, DEFAULT, Qt::green));

    channel.add(GRP_CPAP, new Channel(PrismaVent_TiMin = 0xe584, SETTING, MT_CPAP, SESSION,
        "PrismaVent_TiMin", QObject::tr("Minimum inspiration time"),
        QObject::tr("Shortest inspiration the device allows"),
        QObject::tr("Ti min"),
        STR_UNIT_Seconds, DEFAULT, Qt::green));

    channel.add(GRP_CPAP, new Channel(PrismaVent_TiMax = 0xe585, SETTING, MT_CPAP, SESSION,
        "PrismaVent_TiMax", QObject::tr("Maximum inspiration time"),
        QObject::tr("Longest inspiration the device allows"),
        QObject::tr("Ti max"),
        STR_UNIT_Seconds, DEFAULT, Qt::green));

    channel.add(GRP_CPAP, new Channel(PrismaVent_TriggerInsp = 0xe586, SETTING, MT_CPAP, SESSION,
        "PrismaVent_TriggerInsp", QObject::tr("Inspiratory trigger"),
        QObject::tr("Inspiratory trigger sensitivity level"),
        QObject::tr("Insp. trigger"),
        "", INTEGER, Qt::green));

    channel.add(GRP_CPAP, chan = new Channel(PrismaVent_TriggerType = 0xe58d, SETTING, MT_CPAP, SESSION,
        "PrismaVent_TriggerType", QObject::tr("Trigger"),
        QObject::tr("Whether the breath triggers are set manually or automatically"),
        QObject::tr("Trigger"),
        "", LOOKUP, Qt::green));
    chan->addOption(PRISMAVENT_TRIGGER_MANUAL, QObject::tr("Manual"));
    chan->addOption(PRISMAVENT_TRIGGER_AUTO, QObject::tr("Auto"));

    channel.add(GRP_CPAP, new Channel(PrismaVent_TriggerExp = 0xe587, SETTING, MT_CPAP, SESSION,
        "PrismaVent_TriggerExp", QObject::tr("Expiratory trigger"),
        QObject::tr("Expiratory trigger, as a percentage of peak inspiratory flow"),
        QObject::tr("Exp. trigger"),
        STR_UNIT_Percentage, INTEGER, Qt::green));

    channel.add(GRP_CPAP, new Channel(PrismaVent_InspRamp = 0xe588, SETTING, MT_CPAP, SESSION,
        "PrismaVent_InspRamp", QObject::tr("Inspiratory rise"),
        QObject::tr("Speed of the pressure rise at the start of inspiration"),
        QObject::tr("Insp. rise"),
        "", INTEGER, Qt::green));

    // Event channels for the ids the Prisma loader has no channel for. Names and
    // abbreviations follow prismaTS.
    channel.add(GRP_CPAP, chan = new Channel(PrismaVent_ePC = 0xe58a, SPAN, MT_CPAP, SESSION,
        "PrismaVent_ePC", QObject::tr("ePC"),
        QObject::tr("Epoch (2 mins) with premature cycling"),
        QObject::tr("ePC"),
        STR_UNIT_Percentage, DEFAULT, QColor("#9AB8BF")));
    chan->setEnabled(false);    // like the Prisma loader's other epoch channels

    channel.add(GRP_CPAP, new Channel(PrismaVent_Disconnect = 0xe58b, SPAN, MT_CPAP, SESSION,
        "PrismaVent_Disconnect", QObject::tr("Disconnection"),
        QObject::tr("The breathing circuit was disconnected"),
        QObject::tr("DC"),
        STR_UNIT_EventsPerHour, DEFAULT, QColor("darkgoldenrod")));

    channel.add(GRP_CPAP, new Channel(PrismaVent_SEL = 0xe58c, SPAN, MT_CPAP, SESSION,
        "PrismaVent_SEL", QObject::tr("Sudden decline of leakage"),
        QObject::tr("Leakage dropped suddenly, as when a mouth leak closes"),
        QObject::tr("SEL"),
        STR_UNIT_EventsPerHour, DEFAULT, QColor("lightblue")));

    channel.add(GRP_CPAP, new Channel(PrismaVent_ExpRamp = 0xe589, SETTING, MT_CPAP, SESSION,
        "PrismaVent_ExpRamp", QObject::tr("Expiratory fall"),
        QObject::tr("Speed of the pressure fall at the start of expiration"),
        QObject::tr("Exp. fall"),
        "", INTEGER, Qt::green));
}

bool PrismaVentLoader::initialized = false;

void PrismaVentLoader::Register()
{
    if (initialized) { return; }

    qDebug() << "Registering PrismaVentLoader";
    RegisterLoader(new PrismaVentLoader());
    initialized = true;
}
