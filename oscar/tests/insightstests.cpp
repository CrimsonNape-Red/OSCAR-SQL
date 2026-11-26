/* Therapy Insights Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
#include <memory>
#include <utility>

#include "insightstests.h"
#include "SleepLib/appsettings.h"
#include "SleepLib/day.h"
#include "SleepLib/insights.h"
#include "SleepLib/loader_plugins/resmed_loader.h"
#include "SleepLib/machine.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/progressdialog.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"
#include "database/database_manager.h"

#define TESTDATA_PATH "./testdata/"

namespace {

// One gain for every waveform: 0.01 resolves the scenario values, and a qint16 then holds up
// to 327.67. Give channels their own gain if one ever needs more range.
const EventDataType kGain = 0.01f;

//! \brief A scenario file built into OSCAR objects, as a loader would have imported it.
//! Owns everything it creates and points p_profile at its own throwaway Profile.
struct Scenario {
    QJsonObject json;
    QTemporaryDir profileDir;
    Profile *profile = nullptr;
    QHash<QString, Machine *> devices;      //!< keyed by the file's device id
    QSet<Machine *> endStamped;             //!< devices that stamp events at their end
    QMap<QDate, Day *> days;
    QList<Session *> rejected;              //!< sessions Day::addSession refused

    ~Scenario()
    {
        qDeleteAll(days);                   // a Day deletes its sessions
        qDeleteAll(rejected);
        qDeleteAll(devices);
        if (p_profile == profile) { p_profile = nullptr; }
        delete profile;
    }
};

//! \brief Value of a piecewise-linear [[t, v], ...] list at t seconds. Two points at the
//! same time are a step; the later one applies from that time on.
double valueAt(const QJsonArray &pts, double t)
{
    for (int i = 0; i + 1 < pts.size(); ++i) {
        const double t0 = pts[i][0].toDouble(), t1 = pts[i + 1][0].toDouble();
        if (t >= t0 && t < t1) {
            const double v0 = pts[i][1].toDouble(), v1 = pts[i + 1][1].toDouble();
            return v0 + (v1 - v0) * (t - t0) / (t1 - t0);
        }
    }
    return pts.last()[1].toDouble();
}

//! \brief Channel ID for an OSCAR channel code, or 0 when schema.cpp doesn't know it.
ChannelID channelId(const QString &code)
{
    return schema::channel.names.contains(code) ? schema::channel[code].id() : 0;
}

//! \brief Adds one session from a scenario file to \a sc. Fails the current test on bad input.
void addSession(Scenario &sc, const QDate &date, const QJsonObject &js, SessionID sid)
{
    const QString device = js["device"].toString();
    Machine *mach = sc.devices.value(device);
    QVERIFY2(mach, qPrintable("unknown device " + device));
    const bool endStamped = sc.endStamped.contains(mach);

    const qint64 start = QDateTime::fromString(js["start"].toString(), Qt::ISODate)
                             .toMSecsSinceEpoch();
    Session *sess = new Session(mach, sid);
    sess->really_set_first(start);
    sess->really_set_last(start + qint64(js["hours"].toDouble() * 3600000.0));

    const QJsonObject settings = js["settings"].toObject();
    for (auto it = settings.begin(); it != settings.end(); ++it) {
        const ChannelID id = channelId(it.key());
        QVERIFY2(id, qPrintable("unknown setting " + it.key()));
        if (id == CPAP_Mode) {
            static const QHash<QString, int> modes = {
                {"CPAP", MODE_CPAP}, {"APAP", MODE_APAP},
                {"BiLevelAuto", MODE_BILEVEL_AUTO_FIXED_PS}};
            QVERIFY2(modes.contains(it.value().toString()),
                     qPrintable("unknown PAPMode " + it.value().toString()));
            sess->settings[id] = modes[it.value().toString()];
        } else {
            sess->settings[id] = it.value().toDouble();
        }
    }

    // Each segment becomes its own EventList, as loaders do for a waveform with gaps.
    const QJsonObject channels = js["channels"].toObject();
    for (auto it = channels.begin(); it != channels.end(); ++it) {
        const ChannelID id = channelId(it.key());
        QVERIFY2(id, qPrintable("unknown channel " + it.key()));
        const QJsonObject ch = it.value().toObject();
        const double rate_s = ch["rate_s"].toDouble();
        for (const QJsonValue &seg : ch["segments"].toArray()) {
            const QJsonArray pts = seg.toArray();
            const double t0 = pts.first()[0].toDouble();
            const int recs = qRound((pts.last()[0].toDouble() - t0) / rate_s);
            QVector<qint16> raw(recs);
            for (int i = 0; i < recs; ++i) {
                raw[i] = qint16(qRound(valueAt(pts, t0 + i * rate_s) / kGain));
            }
            EventList *el = sess->AddEventList(id, EVL_Waveform, kGain, 0, 0, 0, rate_s * 1000);
            el->AddWaveform(start + qint64(t0 * 1000), raw.data(), recs,
                            qint64(recs * rate_s * 1000));
        }
    }

    // The file gives onsets; store them the way this device's loader does. End-stamped
    // (ResMed): time = end, value = duration. Start-stamped (PRS1): an instantaneous flag
    // with no duration, as prs1_loader.cpp stores OA, CA and H.
    const QJsonObject events = js["events"].toObject();
    for (auto it = events.begin(); it != events.end(); ++it) {
        const ChannelID id = channelId(it.key());
        QVERIFY2(id, qPrintable("unknown event " + it.key()));
        QList<QPair<qint64, int>> list;
        for (const QJsonValue &ev : it.value().toArray()) {
            const double onset = ev[0].toDouble(), dur = ev[1].toDouble();
            list.append(endStamped ? qMakePair(start + qint64((onset + dur) * 1000), qRound(dur))
                                   : qMakePair(start + qint64(onset * 1000), 0));
        }
        std::sort(list.begin(), list.end());
        EventList *el = sess->AddEventList(id, EVL_Event);
        for (const auto &ev : list) {
            el->AddEvent(ev.first, ev.second);
        }
    }

    sess->setEnabled(js["enabled"].toBool(true));
    sess->SetChanged(true);     // as loaders do; otherwise UpdateSummaries() drops the events
    sess->UpdateSummaries();
    mach->noteReportedChannels(sess);   // as Machine::AddSession() does

    Day *&day = sc.days[date];
    if (!day) {
        day = new Day();
        day->setDate(date);
    }
    day->addSession(sess);
    if (!day->sessions.contains(sess)) {
        sc.rejected.append(sess);
    }
}

//! \brief Builds \a sc from the scenario file at \a path. Fails the current test on bad input.
void loadScenario(Scenario &sc, const QString &path)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
    QJsonParseError err;
    sc.json = QJsonDocument::fromJson(file.readAll(), &err).object();
    QVERIFY2(err.error == QJsonParseError::NoError, qPrintable(path + ": " + err.errorString()));
    QCOMPARE(sc.json["format_version"].toInt(), 1);

    QVERIFY(sc.profileDir.isValid());
    sc.profile = new Profile(sc.profileDir.path(), false);
    p_profile = sc.profile;
    const QJsonObject prefs = sc.json["profile"].toObject();
    for (auto it = prefs.begin(); it != prefs.end(); ++it) {
        QVERIFY2(it.key() == "leak_redline", qPrintable("unknown profile key " + it.key()));
        p_profile->cpap->setLeakRedline(it.value().toDouble());
    }

    static const QHash<QString, MachineType> types = {
        {"CPAP", MT_CPAP}, {"OXIMETER", MT_OXIMETER},
        {"POSITION", MT_POSITION}, {"SLEEPSTAGE", MT_SLEEPSTAGE}};
    MachineID mid = 0;
    for (const QJsonValue &dv : sc.json["devices"].toArray()) {
        const QJsonObject d = dv.toObject();
        QVERIFY2(types.contains(d["type"].toString()),
                 qPrintable("unknown device type " + d["type"].toString()));
        MachineInfo info;
        info.type = types[d["type"].toString()];
        info.loadername = d["loader"].toString();
        info.serial = d["id"].toString();
        Machine *mach = new Machine(nullptr, ++mid);
        mach->setInfo(info);
        sc.devices.insert(d["id"].toString(), mach);
        if (d["event_time"].toString() == "end") { sc.endStamped.insert(mach); }
    }

    // "journal" (weights) isn't built yet; F11 is the first feature to read it.
    SessionID sid = 0;
    for (const QJsonValue &nv : sc.json["nights"].toArray()) {
        const QDate date = QDate::fromString(nv["date"].toString(), Qt::ISODate);
        for (const QJsonValue &sv : nv["sessions"].toArray()) {
            addSession(sc, date, sv.toObject(), ++sid);
            if (QTest::currentTestFailed()) { return; }
        }
    }
}

//! \brief Compares \a actual with \a want, within the file's tolerance[\a tolKey]
//! (default: equal to 4 decimal places).
void checkNear(const Scenario &sc, const QString &tolKey, double actual, double want,
               const QString &what)
{
    const double tol = sc.json["tolerance"][tolKey].toDouble(0.00005);
    QVERIFY2(qAbs(actual - want) <= tol,
             qPrintable(QString("%1: got %2, want %3 ± %4").arg(what).arg(actual).arg(want).arg(tol)));
}

//! \brief Compares \a actual with expected[\a key].
void checkValue(const Scenario &sc, const QString &key, double actual)
{
    checkNear(sc, key, actual, sc.json["expected"][key].toDouble(), key);
}

//! \brief A prescription period: its first night and the nights with CPAP usage.
struct RxPeriod {
    QDate start;
    QVector<Insights::NightCount> nights;
};

//! \brief Consecutive CPAP nights grouped by the same mode, pressure and relief strings
//! Statistics::updateRXChanges() compares.
QList<RxPeriod> rxPeriods(const QMap<QDate, Day *> &days)
{
    QList<RxPeriod> periods;
    QString current;
    for (auto it = days.cbegin(); it != days.cend(); ++it) {
        Day *day = it.value();
        const double hours = day->hours(MT_CPAP);
        if (!day->machine(MT_CPAP) || hours <= 0) { continue; }
        const QString key = day->getCPAPModeStr() + "|" + day->getPressureSettings() + "|"
                            + day->getPressureRelief();
        if (periods.isEmpty() || key != current) {
            periods.append({it.key(), {}});
            current = key;
        }
        periods.last().nights.append({day->count(AllAhiChannels), hours});
    }
    return periods;
}

//! \brief A ChangeLabel as the scenario files spell it.
QString labelName(Insights::ChangeLabel label)
{
    static const QHash<Insights::ChangeLabel, QString> names = {
        {Insights::ChangeLabel::NoComparison, "no comparison"},
        {Insights::ChangeLabel::Within, "within"},
        {Insights::ChangeLabel::Lower, "lower"},
        {Insights::ChangeLabel::Higher, "higher"}};
    return names[label];
}

//! \brief F1: settings-change comparison with noise bands.
void checkF1(const Scenario &sc)
{
    const QList<RxPeriod> periods = rxPeriods(sc.days);
    QCOMPARE(periods.size(), 2);
    const Insights::RxComparison c = Insights::compareRxPeriods(periods[0].nights,
                                                                periods[1].nights);
    QCOMPARE(labelName(c.label), sc.json["expected"]["label"].toString());
    QCOMPARE(c.valid, c.label != Insights::ChangeLabel::NoComparison);
    if (!c.valid) { return; }
    checkValue(sc, "ahi_before", c.ahiBefore);
    checkValue(sc, "ahi_after", c.ahiAfter);
    QVERIFY(c.ciLow <= c.ahiAfter - c.ahiBefore && c.ahiAfter - c.ahiBefore <= c.ciHigh);
}

//! \brief F2: APAP pressure-range hints.
void checkF2(const Scenario &sc)
{
    const QJsonObject exp = sc.json["expected"].toObject();
    const QList<Day *> days = sc.days.values();
    const Insights::TimeAtMax tam = Insights::timeAtMaxPressure(days);
    const Insights::EventsAtPressure eap = Insights::eventsAtPressure(days);
    if (exp.contains("time_at_max_valid")) {
        QCOMPARE(tam.valid, exp["time_at_max_valid"].toBool());
    }
    if (exp.contains("events_at_pressure_valid")) {
        QCOMPARE(eap.valid, exp["events_at_pressure_valid"].toBool());
    }
    if (exp.contains("time_at_max_pct")) {
        QVERIFY(tam.valid);
        checkValue(sc, "time_at_max_pct", tam.pct);
    }
    if (exp.contains("events_at_pressure_by_onset")) {
        QVERIFY(eap.valid);
        const QJsonObject bins = exp["events_at_pressure_by_onset"].toObject();
        QList<int> wantKeys;
        for (const QString &k : bins.keys()) { wantKeys << k.toInt(); }
        std::sort(wantKeys.begin(), wantKeys.end());
        QCOMPARE(eap.bins.keys(), wantKeys);
        for (auto it = bins.begin(); it != bins.end(); ++it) {
            const Insights::PressureBin &bin = eap.bins[it.key().toInt()];
            const QJsonObject want = it.value().toObject();
            const QString where = it.key() + " cmH2O ";
            QCOMPARE(bin.events, want["events"].toInt());
            checkNear(sc, "hours", bin.hours, want["hours"].toDouble(), where + "hours");
            checkNear(sc, "per_hour", bin.events / bin.hours, want["per_hour"].toDouble(),
                      where + "per_hour");
        }
    }
}

//! \brief Active quality flags, named as the scenario files name them.
QStringList flagNames(const Insights::QualityFlags &f)
{
    QStringList names;
    if (f.largeLeak) { names << "large_leak"; }
    if (f.shortNight) { names << "short_night"; }
    if (f.fragmented) { names << "fragmented"; }
    if (f.summaryOnly) { names << "summary_only"; }
    return names;
}

//! \brief F3: data-quality flags. "large_leak_source" isn't checked: OSCAR turns leak
//! above the redline into LargeLeak spans at import, so the source can't be told apart.
void checkF3(const Scenario &sc)
{
    const QJsonObject exp = sc.json["expected"].toObject();
    QCOMPARE(sc.days.size(), 1);
    Day *day = sc.days.first();
    const Insights::QualityFlags f = Insights::qualityFlags(day);
    QStringList want;
    for (const QJsonValue &v : exp["flags"].toArray()) { want << v.toString(); }
    QCOMPARE(flagNames(f), want);
    if (exp.contains("large_leak_pct")) {
        QVERIFY(f.leakKnown);
        checkValue(sc, "large_leak_pct", f.largeLeakPct);
    }
    if (exp.contains("sessions")) { QCOMPARE(f.sessions, exp["sessions"].toInt()); }
    if (exp.contains("hours")) { checkValue(sc, "hours", day->hours(MT_CPAP)); }
    if (exp.contains("ahi")) { checkValue(sc, "ahi", day->calcAHI()); }
}

//! \brief F4-lite: adherence patterns.
//! \brief Cases every feature must handle: no nights, and two CPAP devices in one night.
void checkAll(const Scenario &sc)
{
    const QJsonObject exp = sc.json["expected"].toObject();
    const QList<Day *> days = sc.days.values();
    if (exp.contains("all_valid")) {
        QVERIFY(!exp["all_valid"].toBool());
        QVERIFY(!Insights::timeAtMaxPressure(days).valid);
        QVERIFY(!Insights::eventsAtPressure(days).valid);
        QVERIFY(!Insights::medicareFirst90Days(sc.days).valid);
        QVERIFY(!Insights::usagePattern(days).valid);
        QVERIFY(!Insights::compareRxPeriods({}, {}).valid);
        return;
    }
    // A Day holds one device per type: OSCAR keeps the first device and refuses the other's.
    QCOMPARE(sc.rejected.size(), exp["rejected_sessions"].toInt());
    QCOMPARE(days.size(), 1);
    Day *day = days.first();
    const double tol = sc.json["tolerance"].toObject()["ahi"].toDouble(0.001);
    QVERIFY2(qAbs(day->hours(MT_CPAP) - exp["hours"].toDouble()) < 0.001,
             qPrintable(QString::number(day->hours(MT_CPAP))));
    QVERIFY2(qAbs(day->count(AllAhiChannels) / day->hours(MT_CPAP) - exp["ahi"].toDouble()) < tol,
             qPrintable(QString::number(day->count(AllAhiChannels) / day->hours(MT_CPAP))));
    QCOMPARE(Insights::usagePattern(days).medianSessions, 1.0);
}

void checkF4(const Scenario &sc)
{
    const QJsonObject exp = sc.json["expected"].toObject();
    const Insights::MedicareCheck m = Insights::medicareFirst90Days(sc.days);
    QVERIFY(m.valid && m.complete);
    QCOMPARE(m.met, exp["medicare_met"].toBool());
    QCOMPARE(m.bestNights, exp["best_window_nights_ge_4h"].toInt());
    QCOMPARE(m.bestFirstNight, QDate::fromString(exp["best_window_first_night"].toString(),
                                                 Qt::ISODate));

    // Every session starts at 23:00; most last 3.5 h, so the mask comes off at 02:30.
    const Insights::UsagePattern u = Insights::usagePattern(sc.days.values());
    QVERIFY(u.valid);
    QCOMPARE(u.medianMaskOff, QTime(2, 30));
    QCOMPARE(u.medianSessions, 1.0);

    // With an even count the median averages the middle pair: 02:30 and 05:00 give 03:45.
    Day *shortNight = nullptr;
    Day *longNight = nullptr;
    for (Day *day : sc.days) {
        const double h = day->hours(MT_CPAP);
        if (h < 4) { shortNight = day; }
        if (h > 5) { longNight = day; }
    }
    QVERIFY(shortNight && longNight);
    QCOMPARE(Insights::usagePattern({shortNight, longNight}).medianMaskOff, QTime(3, 45));
}

//! \brief One night's values for the real-data snapshot, as YAML.
QString nightYaml(Day *day)
{
    QString out;
    QTextStream ts(&out);
    ts << "date: " << day->date().toString(Qt::ISODate) << "\n"
       << "sessions: " << day->getSessions(MT_CPAP, true).size() << "\n"
       << "hours: " << QString::number(day->hours(MT_CPAP), 'f', 3) << "\n"
       << "ahi: " << QString::number(day->calcAHI(), 'f', 3) << "\n"
       << "events:\n";
    for (const char *code : {"Obstructive", "Hypopnea", "ClearAirway", "Apnea", "RERA"}) {
        ts << "  " << code << ": " << day->count(channelId(code)) << "\n";
    }
    const Insights::QualityFlags qf = Insights::qualityFlags(day);
    if (qf.leakKnown) {
        ts << "large_leak_pct: " << QString::number(qf.largeLeakPct, 'f', 2) << "\n";
    }
    ts << "flags: [" << flagNames(qf).join(", ") << "]\n";
    const Insights::TimeAtMax tam = Insights::timeAtMaxPressure({day});
    if (tam.valid) { ts << "time_at_max_pct: " << QString::number(tam.pct, 'f', 2) << "\n"; }
    const Insights::EventsAtPressure eap = Insights::eventsAtPressure({day});
    if (eap.valid) {
        ts << "events_at_pressure:\n";
        for (auto it = eap.bins.cbegin(); it != eap.bins.cend(); ++it) {
            ts << "  " << it.key() << ": {events: " << it->events
               << ", hours: " << QString::number(it->hours, 'f', 3) << "}\n";
        }
    }
    return out;
}

//! \brief Each settings period compared with the one before it, as YAML.
QString periodsYaml(const QList<RxPeriod> &periods)
{
    QString out;
    QTextStream ts(&out);
    for (int i = 0; i < periods.size(); ++i) {
        ts << "- start: " << periods[i].start.toString(Qt::ISODate) << "\n"
           << "  nights: " << periods[i].nights.size() << "\n";
        if (i == 0) { continue; }
        const Insights::RxComparison c = Insights::compareRxPeriods(periods[i - 1].nights,
                                                                    periods[i].nights);
        ts << "  label: " << labelName(c.label) << "\n";
        if (c.valid) {
            ts << "  ahi: [" << QString::number(c.ahiBefore, 'f', 3) << ", "
               << QString::number(c.ahiAfter, 'f', 3) << "]\n"
               << "  ci95: [" << QString::number(c.ciLow, 'f', 3) << ", "
               << QString::number(c.ciHigh, 'f', 3) << "]\n";
        }
    }
    return out;
}

} // namespace

void InsightsTests::initTestCase()
{
    // QApplication, not QCoreApplication: LoadMachineData() needs a ProgressDialog.
    static int argc = 1;
    static char appName[] = "test";
    static char *argv[] = { appName, nullptr };
    m_app = new QApplication(argc, argv);
    if (CPAP_Obstructive == 0) { schema::init(); }
    // The real-data profile is ResMed. Without its loader, Day::getCPAPModeStr() and
    // getPressureRelief() return an error string, so the settings periods merge. Another
    // suite used to register it first; OSCAR_TEST_SUITES can run this one alone.
    ResmedLoader::Register();
}

void InsightsTests::cleanupTestCase()
{
    delete m_app;
    m_app = nullptr;
}

// Pins the loader's conventions on known points of the scenario files.
void InsightsTests::testScenarioLoader()
{
    for (const QString &name : {QStringLiteral("f2_events_at_pressure_end_stamped"),
                                QStringLiteral("f2_events_at_pressure_start_stamped")}) {
        Scenario sc;
        loadScenario(sc, QFINDTESTDATA("insights_scenarios/" + name + ".json"));
        if (QTest::currentTestFailed()) { return; }
        Session *sess = sc.days.first()->sessions.first();
        const qint64 start = sess->realFirst();
        QCOMPARE(sess->settings[CPAP_Mode].toInt(), int(MODE_APAP));
        QCOMPARE(sess->settings[CPAP_PressureMax].toDouble(), 14.0);
        // First obstructive event: onset 1800 s, 20 s long.
        EventList *ev = sess->eventlist[CPAP_Obstructive].first();
        QCOMPARE(ev->count(), quint32(6));
        QCOMPARE(ev->time(0), start + (name.endsWith("end_stamped") ? 1820000 : 1800000));
        QCOMPARE(ev->data(0), EventDataType(name.endsWith("end_stamped") ? 20 : 0));
    }

    // A disabled session still loads; only its enabled flag differs.
    Scenario dis;
    loadScenario(dis, QFINDTESTDATA("insights_scenarios/f3_disabled_session_excluded.json"));
    if (QTest::currentTestFailed()) { return; }
    QCOMPARE(dis.days.first()->sessions.size(), 2);
    QVERIFY(dis.days.first()->sessions[0]->enabled());
    QVERIFY(!dis.days.first()->sessions[1]->enabled());
}

void InsightsTests::testScenario_data()
{
    QTest::addColumn<QString>("path");
    const QDir dir(QFINDTESTDATA("insights_scenarios"));
    const QStringList files = dir.entryList({"*.json"}, QDir::Files, QDir::Name);
    QVERIFY(!files.isEmpty());
    for (const QString &f : files) {
        QTest::newRow(qPrintable(QFileInfo(f).completeBaseName())) << dir.filePath(f);
    }
}

// Each feature adds a checker for its "expected" keys, with the file's "tolerance".
void InsightsTests::testScenario()
{
    QFETCH(QString, path);
    Scenario sc;
    loadScenario(sc, path);
    if (QTest::currentTestFailed()) { return; }
    QCOMPARE(sc.json["id"].toString(), QFileInfo(path).completeBaseName());
    const QString feature = sc.json["feature"].toString();
    if (feature == "F1") { return checkF1(sc); }
    if (feature == "F2") { return checkF2(sc); }
    if (feature == "F3") { return checkF3(sc); }
    if (feature == "F4-lite") { return checkF4(sc); }
    if (feature == "all") { return checkAll(sc); }
    QSKIP(qPrintable(feature + " is not implemented yet"));
}

// Opens a temporary copy of testdata/insights/real/ (see Tools/insights_testdata/README.md)
// and compares one YAML file per night with testdata/insights/output/. A missing file is
// written instead: review those nights against the Daily view before trusting them. Delete
// output/ to re-baseline after an intended change.
void InsightsTests::testRealDataSnapshot()
{
    const QString realDir = TESTDATA_PATH "insights/real/";
    if (!QFile::exists(realDir + "oscar.db")) {
        QSKIP("testdata/insights/real/ is absent");
    }

    // OSCAR writes to oscar.db on open, so work on a copy.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QVERIFY(QFile::copy(realDir + "oscar.db", tmp.filePath("oscar.db")));
    QVERIFY(QDir(tmp.path()).mkpath("Profiles/Test"));

    Profile *prevProfile = p_profile;
    Preferences *prevPref = p_pref;
    AppWideSetting *prevAppSetting = AppSetting;
    const QString prevAppData = GetAppData();
    SetAppData(tmp.path(), false);
    p_pref = new Preferences(QStringLiteral("Preferences"));
    p_pref->Open();
    AppSetting = new AppWideSetting(p_pref);
    std::unique_ptr<ProgressDialog> progress(new ProgressDialog(nullptr));

    p_profile = nullptr;
    auto restore = qScopeGuard([&] {
        Profiles::profiles.clear();
        delete p_profile;
        p_profile = prevProfile;
        delete AppSetting;
        AppSetting = prevAppSetting;
        delete p_pref;
        p_pref = prevPref;
        DatabaseManager::instance().close();
        SetAppData(prevAppData, false);
    });

    DatabaseManager &db = DatabaseManager::instance();
    QVERIFY(db.initialize(tmp.filePath("oscar.db")));
    if (db.pendingSchemaUpgradeFrom() > 0) {
        QVERIFY(db.upgradeSchema());
        QVERIFY(db.completeInitialization());
    }
    Profiles::Scan();
    p_profile = Profiles::Get("Test");
    QVERIFY(p_profile);
    p_profile->LoadMachineData(progress.get());
    QVERIFY(!p_profile->daylist.isEmpty());

    // The manifest's reference values come from the source database's summary tables.
    QFile mf(realDir + "manifest.json");
    QVERIFY(mf.open(QIODevice::ReadOnly));
    const QJsonObject manifest = QJsonDocument::fromJson(mf.readAll()).object();
    const QJsonObject cases = manifest["cases"].toObject();
    for (auto it = cases.begin(); it != cases.end(); ++it) {
        const QJsonObject c = it.value().toObject();
        Day *day = p_profile->GetDay(QDate::fromString(c["night"].toString(), Qt::ISODate),
                                     MT_CPAP);
        QVERIFY2(day, qPrintable(it.key()));
        QVERIFY2(qAbs(day->hours(MT_CPAP) - c["hours"].toDouble()) < 0.001, qPrintable(it.key()));
        QVERIFY2(qAbs(day->calcAHI() - c["ahi"].toDouble()) < 0.001, qPrintable(it.key()));
        QCOMPARE(day->getSessions(MT_CPAP, true).size(), c["sessions"].toInt());
        // Within a point: the reference comes from a different summary of the same data.
        QVERIFY2(qAbs(Insights::timeAtMaxPressure({day}).pct - c["time_at_max_pct"].toDouble())
                 < 1.0, qPrintable(it.key()));
        const Insights::QualityFlags qf = Insights::qualityFlags(day);
        QVERIFY2(qAbs(qf.largeLeakPct - c["large_leak_pct"].toDouble()) < 0.01, qPrintable(it.key()));
        QCOMPARE(qf.summaryOnly, c["summary_only_sessions"].toInt() > 0);
        const QJsonObject events = c["events"].toObject();
        for (auto ev = events.begin(); ev != events.end(); ++ev) {
            QVERIFY2(channelId(ev.key()), qPrintable("unknown event " + ev.key()));
            QCOMPARE(day->count(channelId(ev.key())), EventDataType(ev.value().toInt()));
        }
    }

    // Per night, plus one file for the settings-change comparisons (F1).
    QMap<QString, QString> snapshots;
    for (Day *day : std::as_const(p_profile->daylist)) {
        if (day->getSessions(MT_CPAP, true).isEmpty()) { continue; }
        snapshots[day->date().toString(Qt::ISODate) + ".yml"] = nightYaml(day);
    }
    snapshots["periods.yml"] = periodsYaml(rxPeriods(p_profile->daylist));

    // F4-lite over the whole profile.
    const QJsonObject mref = manifest["medicare_first_90_days"].toObject();
    const Insights::MedicareCheck med = Insights::medicareFirst90Days(p_profile->daylist);
    QCOMPARE(med.firstNight, QDate::fromString(mref["first_night"].toString(), Qt::ISODate));
    QCOMPARE(med.met, mref["met"].toBool());
    QCOMPARE(med.bestNights, mref["best_window_nights_ge_4h"].toInt());
    const Insights::UsagePattern usage = Insights::usagePattern(p_profile->daylist.values());
    snapshots["adherence.yml"] = QString("medicare_met: %1\nbest_nights: %2\nbest_first_night: %3\n"
                                         "median_mask_off: %4\nmask_off_iqr_min: %5\n"
                                         "median_sessions: %6\n")
            .arg(med.met).arg(med.bestNights).arg(med.bestFirstNight.toString(Qt::ISODate))
            .arg(usage.medianMaskOff.toString("HH:mm")).arg(usage.maskOffSpreadMinutes)
            .arg(usage.medianSessions);

    const QString outDir = TESTDATA_PATH "insights/output/";
    QVERIFY(QDir().mkpath(outDir));
    QStringList diffs;
    int written = 0;
    for (auto it = snapshots.cbegin(); it != snapshots.cend(); ++it) {
        const QString &yaml = it.value();
        QFile f(outDir + it.key());
        if (f.open(QIODevice::ReadOnly)) {
            if (QString::fromUtf8(f.readAll()) != yaml) { diffs << f.fileName(); }
        } else {
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(yaml.toUtf8());
            ++written;
        }
    }
    if (written) { qInfo() << "wrote" << written << "new snapshots to" << outDir; }
    QVERIFY2(diffs.isEmpty(), qPrintable(QString("%1 nights differ, e.g. %2")
                                         .arg(diffs.size()).arg(diffs.value(0))));
}
