/* Löwenstein Prisma LINE Loader Unit Tests
 *
 * The settings mapping is tested on synthetic event and signal files. The whole-card test
 * reads a real prismaCR card from the folder named by the OSCAR_PRISMACR_CARD environment
 * variable and is skipped when it is unset, since sample cards carry device serial numbers
 * and cannot be committed.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QDir>
#include <QTemporaryDir>

#include "prismalinetests.h"
#include "SleepLib/appsettings.h"
#include "SleepLib/day.h"
#include "SleepLib/importcontext.h"
#include "SleepLib/machine.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"
#include "SleepLib/loader_plugins/prisma_loader.h"
#include "SleepLib/loader_plugins/prismavent_loader.h"

extern ChannelID Prisma_Mode, Prisma_BiSoft, Prisma_TubeType, Prisma_Warning,
                 Prisma_AutoF, Prisma_FMin, Prisma_TiT, Prisma_LeakAlarm, Prisma_HypopneaLeak;
extern ChannelID PrismaVent_Disconnect, PrismaVent_SEL;

namespace {

//! Collects sessions in memory instead of writing them to the database.
class CollectingContext : public ImportContext
{
public:
    explicit CollectingContext(Profile * profile) : m_profile(profile) {}
    ~CollectingContext() override {
        qDeleteAll(sessions);
        delete m_machine;
        m_machine = nullptr;
    }

    Machine * CreateMachineFromInfo(const MachineInfo & info) override {
        if (m_machine == nullptr) {
            m_machine = new CPAP(m_profile, 1);
        }
        m_machineInfo = info;
        m_machine->setInfo(info);
        return m_machine;
    }
    bool SessionExists(SessionID) override { return false; }
    bool AddSession(Session * session) override {
        sessions.append(session);
        return true;
    }

    QList<Session *> sessions;

private:
    Profile * m_profile;
};

//! Pads or truncates \a text to a fixed-width EDF header field.
QByteArray edfField(const QByteArray & text, int width)
{
    return text.leftJustified(width, ' ', true);
}

//! A one-signal WMEDF file: 10 one-second 16-bit "Pressure" records.
QByteArray syntheticSignalFile()
{
    QByteArray file;
    file += edfField("0", 8);
    file += edfField("", 80);
    file += edfField("", 80);
    file += edfField("02.11.2622.18.57", 16);
    file += edfField("512", 8);
    file += edfField("", 44);
    file += edfField("-1", 8);
    file += edfField("1", 8);
    file += edfField("1", 4);
    file += edfField("Pressure", 16);
    file += edfField("", 80);
    file += edfField("hPa", 8);
    file += edfField("-327.679", 8);
    file += edfField("327.6700", 8);
    file += edfField("-32768", 8);
    file += edfField("32767", 8);
    file += edfField("", 80);
    file += edfField("1", 8);
    file += edfField("#2", 32);
    for (int i = 0; i < 10; i++) {
        file += char(0xE8);   // 1000 (10 hPa), little-endian
        file += char(0x03);
    }
    return file;
}

//! A respiratory event as written on the card; times are in tenths of a second.
struct RespEventRow { int id; int endTime; int duration; };

//! Event file holding \a parameters as static (DeviceEventID 0) parameters, then \a events.
QByteArray syntheticEventFile(const QList<QPair<int, int>> & parameters, const QList<RespEventRow> & events = {})
{
    QByteArray xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<desc>\n";
    for (const auto & p : parameters) {
        xml += QString("<DeviceEvent  DeviceEventID=\"0\" Time=\"0\" ParameterID=\"%1\" NewValue=\"%2\"/>\n")
                   .arg(p.first).arg(p.second).toLatin1();
    }
    for (const auto & e : events) {
        xml += QString("<RespEvent  RespEventID=\"%1\" EndTime=\"%2\" Duration=\"%3\" Pressure=\"0\" Strength=\"0\"/>\n")
                   .arg(e.id).arg(e.endTime).arg(e.duration).toLatin1();
    }
    xml += "</desc>\n";
    return xml;
}

//! Event durations in seconds on \a channel, across all of the session's lists for it.
QList<int> eventDurations(Session * session, ChannelID channel)
{
    QList<int> durations;
    if (session->eventlist.contains(channel)) {
        for (EventList * list : session->eventlist[channel]) {
            for (quint32 i = 0; i < list->count(); i++) {
                durations.append(list->data(i));
            }
        }
    }
    return durations;
}

//! AcSV settings of the prismaCR sample, as reported by prismaTS.
QList<QPair<int, int>> acsvParameters()
{
    return {
        { PRISMA_LINE_MODE, PRISMA_MODE_ACSV },
        { PRISMA_LINE_EEPAP_MIN, 600 }, { PRISMA_LINE_EEPAP_MAX, 1000 },
        { PRISMA_LINE_PDIFF_NORM, 400 }, { PRISMA_LINE_PDIFF_MAX, 600 },
        { PRISMA_LINE_IPAP_MAX, 1350 }, { PRISMA_LINE_IPAP, 800 }, { PRISMA_LINE_EPAP, 400 },
        { PRISMA_LINE_EXTRA_OBSTRUCTION_PROTECTION, Prisma_TriLevel },
        { PRISMA_LINE_AUTOSTART, 1 }, { PRISMA_LINE_TUBE_TYPE, 22 },
        { PRISMA_LINE_SOFT_START_TIME, 30 }, { PRISMA_LINE_SOFT_START_PRESS, 500 },
        { PRISMA_LINE_AUTO_F, 0 }, { PRISMA_LINE_F_MIN, 14 }, { PRISMA_LINE_TI_T, 33 },
    };
}

//! Runs one import task on synthetic files; \a ctx owns the resulting session.
Session * importSynthetic(PrismaLoader & loader, CollectingContext & ctx, const QList<QPair<int, int>> & parameters,
                          const QList<RespEventRow> & events = {})
{
    MachineInfo info = loader.newInfo();
    info.serial = "1";
    ctx.CreateMachineFromInfo(info);
    PrismaImport task(&loader, info, 1 + ctx.sessions.size(), syntheticEventFile(parameters, events), syntheticSignalFile());
    task.run();
    return ctx.sessions.isEmpty() ? nullptr : ctx.sessions.last();
}

} // namespace

void PrismaLineTests::initTestCase()
{
    if (CPAP_Obstructive == 0) {
        schema::init();
    }
    // The loader also uses two channels that the prisma VENT loader registers.
    if (PrismaVent_SEL == 0) {
        if (Prisma_AutoF == 0) {
            PrismaLoader().initChannels();
        }
        PrismaVentLoader().initChannels();
    }
}

void PrismaLineTests::cleanupTestCase()
{
}

void PrismaLineTests::testModelTable()
{
    PrismaModelInfo models;
    QVERIFY(models.IsTested("13"));
    QCOMPARE(QString(models.Name("13")), QStringLiteral("prismaCR"));
}

void PrismaLineTests::testAcSVFixedRate()
{
    QTemporaryDir profileDir;
    QVERIFY(profileDir.isValid());
    Profile profile(profileDir.path(), false);
    Profile * previousProfile = p_profile;
    p_profile = &profile;
    {
        PrismaLoader loader;
        CollectingContext ctx(&profile);
        loader.SetContext(&ctx);
        Session * s = importSynthetic(loader, ctx, acsvParameters());
        QVERIFY(s != nullptr);

        QCOMPARE(s->settings[CPAP_Mode].toInt(), int(MODE_ASV_VARIABLE_EPAP));
        QCOMPARE(s->settings[Prisma_Mode].toInt(), int(PRISMA_COMBINED_MODE_ACSV));
        QCOMPARE(s->settings[CPAP_EPAPLo].toDouble(), 4.0);
        QCOMPARE(s->settings[CPAP_IPAPHi].toDouble(), 13.5);
        QCOMPARE(s->settings[CPAP_PSMin].toDouble(), 4.0);
        QCOMPARE(s->settings[CPAP_PSMax].toDouble(), 6.0);
        QCOMPARE(s->settings[CPAP_EEPAPLo].toDouble(), 6.0);
        QCOMPARE(s->settings[CPAP_EEPAPHi].toDouble(), 10.0);
        QCOMPARE(s->settings[Prisma_BiSoft].toInt(), int(Prisma_TriLevel));
        QCOMPARE(s->settings[Prisma_AutoF].toInt(), 0);
        QCOMPARE(s->settings[Prisma_FMin].toInt(), 14);
        QCOMPARE(s->settings[Prisma_TiT].toInt(), 33);
        QCOMPARE(s->settings[Prisma_TubeType].toDouble(), 2.2);

        // The fixed-EPAP settings and the "send sample data" warning are gone.
        QVERIFY(!s->settings.contains(CPAP_EPAP));
        QVERIFY(!s->settings.contains(CPAP_IPAP));
        QVERIFY(!s->settings.contains(Prisma_Warning));
        loader.SetContext(nullptr);
    }
    p_profile = previousProfile;
}

void PrismaLineTests::testAcSVAutoF()
{
    QTemporaryDir profileDir;
    QVERIFY(profileDir.isValid());
    Profile profile(profileDir.path(), false);
    Profile * previousProfile = p_profile;
    p_profile = &profile;
    {
        PrismaLoader loader;
        CollectingContext ctx(&profile);
        loader.SetContext(&ctx);

        QList<QPair<int, int>> parameters = acsvParameters();
        parameters.append(qMakePair(int(PRISMA_LINE_AUTO_F), 3));
        parameters.append(qMakePair(int(PRISMA_LINE_F_MIN), -1));
        parameters.append(qMakePair(int(PRISMA_LINE_TI_T), -1));
        Session * s = importSynthetic(loader, ctx, parameters);
        QVERIFY(s != nullptr);

        // Later parameters win, as they do in PrismaEventFile; -1 means "not applicable".
        QCOMPARE(s->settings[Prisma_AutoF].toInt(), 3);
        QVERIFY(!s->settings.contains(Prisma_FMin));
        QVERIFY(!s->settings.contains(Prisma_TiT));

        // prismaTS shows these two values as Off and On.
        QCOMPARE(schema::channel[Prisma_AutoF].option(0), STR_TR_Off);
        QCOMPARE(schema::channel[Prisma_AutoF].option(3), STR_TR_On);
        QVERIFY(STR_TR_Off != STR_TR_On);
        loader.SetContext(nullptr);
    }
    p_profile = previousProfile;
}

void PrismaLineTests::testTubeTypeInOtherModes()
{
    QTemporaryDir profileDir;
    QVERIFY(profileDir.isValid());
    Profile profile(profileDir.path(), false);
    Profile * previousProfile = p_profile;
    p_profile = &profile;
    {
        PrismaLoader loader;
        CollectingContext ctx(&profile);
        loader.SetContext(&ctx);

        const QList<int> modes = { PRISMA_MODE_CPAP, PRISMA_MODE_APAP, PRISMA_MODE_S, PRISMA_MODE_AUTO_S, PRISMA_MODE_AUTO_ST };
        for (int mode : modes) {
            Session * s = importSynthetic(loader, ctx, { { PRISMA_LINE_MODE, mode }, { PRISMA_LINE_TUBE_TYPE, 15 } });
            QVERIFY(s != nullptr);
            QVERIFY2(s->settings.contains(Prisma_TubeType), qPrintable(QString("mode %1").arg(mode)));
            QCOMPARE(s->settings[Prisma_TubeType].toDouble(), 1.5);
        }
        loader.SetContext(nullptr);
    }
    p_profile = previousProfile;
}

void PrismaLineTests::testEventChannels()
{
    QTemporaryDir profileDir;
    QVERIFY(profileDir.isValid());
    Profile profile(profileDir.path(), false);
    Profile * previousProfile = p_profile;
    p_profile = &profile;
    {
        PrismaLoader loader;
        CollectingContext ctx(&profile);
        loader.SetContext(&ctx);

        Session * s = importSynthetic(loader, ctx, acsvParameters(), {
            { PRISMA_EVENT_SOFTSTART, 18001, 18000 },
            { PRISMA_EVENT_SOFTSTART, 18002, 0 },       // no duration: not a span
            { PRISMA_EVENT_DISCONNECTION, 20000, 313 },
            { PRISMA_EVENT_DISCONNECTION, 20001, 0 },   // no duration: not a span
            { PRISMA_EVENT_DISCONNECTION, 900000, 500000 },   // over the 16-bit limit once in seconds
            { PRISMA_EVENT_SEL, 21000, 100 },
            { 304, 22000, 0 },                          // the alarm's start marker, not imported
            { PRISMA_EVENT_LEAKAGE_ALARM, 22890, 890 },
            { 306, 23000, 0 },                          // the disconnect alarm's markers duplicate 171
            { 307, 23313, 313 },
            { PRISMA_EVENT_HYPOPNEA_LEAKAGE, 24133, 133 },
        });
        QVERIFY(s != nullptr);

        QCOMPARE(eventDurations(s, CPAP_Ramp), QList<int>({ 1800 }));
        QCOMPARE(eventDurations(s, PrismaVent_Disconnect), QList<int>({ 31, 32767 }));
        QCOMPARE(eventDurations(s, PrismaVent_SEL), QList<int>({ 10 }));
        QCOMPARE(eventDurations(s, Prisma_LeakAlarm), QList<int>({ 89 }));
        QCOMPARE(eventDurations(s, Prisma_HypopneaLeak), QList<int>({ 13 }));

        // None of these count toward AHI.
        QVERIFY(eventDurations(s, CPAP_Hypopnea).isEmpty());
        QVERIFY(eventDurations(s, CPAP_ObstructiveHypopnea).isEmpty());
        QVERIFY(eventDurations(s, CPAP_CentralHypopnea).isEmpty());
        loader.SetContext(nullptr);
    }
    p_profile = previousProfile;
}

void PrismaLineTests::testPrismaCRCard()
{
    const QString card = qEnvironmentVariable("OSCAR_PRISMACR_CARD");
    if (card.isEmpty() || !QDir(card).exists()) {
        QSKIP("OSCAR_PRISMACR_CARD is not set to a prismaCR card folder");
    }

    QTemporaryDir profileDir;
    QVERIFY(profileDir.isValid());
    Profile profile(profileDir.path(), false);
    Profile * previousProfile = p_profile;
    p_profile = &profile;

    Preferences prefs("Preferences");
    Preferences * previousPref = p_pref;
    AppWideSetting * previousSetting = AppSetting;
    p_pref = &prefs;
    AppSetting = new AppWideSetting(p_pref);
    AppSetting->setMultithreading(false);   // the collecting context is not thread-safe
    {
        PrismaLoader loader;
        QVERIFY(loader.Detect(card));

        const MachineInfo info = loader.PeekInfo(card);
        QCOMPARE(info.modelnumber, QStringLiteral("13"));
        QCOMPARE(info.model, QStringLiteral("prismaCR"));
        QVERIFY(!info.serial.isEmpty());

        CollectingContext ctx(&profile);
        loader.SetContext(&ctx);
        QCOMPARE(loader.Open(card), 83);
        QCOMPARE(ctx.sessions.size(), 83);

        // Two sets of settings: the latest are the ones on the prismaTS report.
        int latest = 0;
        int earlier = 0;
        int softStarts = 0;
        int disconnections = 0;
        int sel = 0;
        int leakAlarms = 0;
        int leakHypopneas = 0;
        for (Session * s : ctx.sessions) {
            softStarts += eventDurations(s, CPAP_Ramp).size();
            disconnections += eventDurations(s, PrismaVent_Disconnect).size();
            sel += eventDurations(s, PrismaVent_SEL).size();
            leakAlarms += eventDurations(s, Prisma_LeakAlarm).size();
            leakHypopneas += eventDurations(s, Prisma_HypopneaLeak).size();
            QCOMPARE(s->settings[CPAP_Mode].toInt(), int(MODE_ASV_VARIABLE_EPAP));
            QCOMPARE(s->settings[Prisma_BiSoft].toInt(), int(Prisma_TriLevel));
            QCOMPARE(s->settings[Prisma_TubeType].toDouble(), 2.2);
            QVERIFY(!s->settings.contains(Prisma_Warning));
            if (s->settings[CPAP_EEPAPLo].toDouble() == 6.0) {
                latest++;
                QCOMPARE(s->settings[CPAP_EEPAPHi].toDouble(), 10.0);
                QCOMPARE(s->settings[CPAP_EPAPLo].toDouble(), 4.0);
                QCOMPARE(s->settings[CPAP_IPAPHi].toDouble(), 13.5);
                QCOMPARE(s->settings[CPAP_PSMin].toDouble(), 4.0);
                QCOMPARE(s->settings[CPAP_PSMax].toDouble(), 6.0);
                QCOMPARE(s->settings[Prisma_AutoF].toInt(), 0);
                QCOMPARE(s->settings[Prisma_FMin].toInt(), 14);
                QCOMPARE(s->settings[Prisma_TiT].toInt(), 33);

                // The Daily page's summary. ~Day() deletes its sessions, but the context owns this one.
                Day day;
                day.addSession(s);
                const QString pressures = day.getPressureSettings();
                day.removeSession(s);
                QCOMPARE(pressures, QString("Min EPAP 4.0 Max IPAP 13.5 PS 4.0-6.0 (%1)").arg(schema::channel[CPAP_Pressure].units()));
            } else {
                earlier++;
                QCOMPARE(s->settings[CPAP_EEPAPLo].toDouble(), 12.0);
                QCOMPARE(s->settings[CPAP_EEPAPHi].toDouble(), 20.0);
                QCOMPARE(s->settings[CPAP_IPAPHi].toDouble(), 26.5);
                QVERIFY(!s->settings.contains(Prisma_FMin));
            }
            QVERIFY(s->eventlist.contains(CPAP_FlowRate));
        }
        QCOMPARE(latest, 10);
        QCOMPARE(earlier, 73);

        // Counted directly from the card's event files; 28 of the 114 softSTART rows are under a second.
        QCOMPARE(softStarts, 86);
        QCOMPARE(disconnections, 9);
        QCOMPARE(sel, 215);
        QCOMPARE(leakAlarms, 10);
        QCOMPARE(leakHypopneas, 1);
        loader.SetContext(nullptr);
    }
    delete AppSetting;
    AppSetting = previousSetting;
    p_pref = previousPref;
    p_profile = previousProfile;
}
