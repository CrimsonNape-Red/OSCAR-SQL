/* Löwenstein prisma VENT Loader Unit Tests
 *
 * The parsing helpers are tested on synthetic input. The whole-card test reads a
 * real card from the folder named by the OSCAR_PRISMAVENT_CARD environment
 * variable and is skipped when it is unset, since sample cards carry device
 * serial numbers and cannot be committed. Its expected values are those of the
 * single VENT50-C night described in Notes/loaders/PRISMA_VENT_CARD_ANALYSIS.md.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QDir>
#include <QTemporaryDir>
#include <QTimeZone>

#include "prismaventtests.h"
#include "SleepLib/importcontext.h"
#include "SleepLib/machine.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"
#include "SleepLib/loader_plugins/edfparser.h"
#include "SleepLib/loader_plugins/prisma_loader.h"
#include "SleepLib/loader_plugins/prismavent_loader.h"

using namespace PrismaVentParsing;

extern ChannelID Prisma_Artifact;

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
        m_machineInfo = info;
        m_machine = new CPAP(m_profile, 1);
        m_machine->setInfo(info);
        return m_machine;
    }
    bool SessionExists(SessionID) override { return false; }
    bool AddSession(Session * session) override {
        sessions.append(session);
        return true;
    }
    QSet<QString> messages() const { return m_unexpectedMessages; }

    QList<Session *> sessions;

private:
    Profile * m_profile;
};

//! Number of events in the session's (single) list for \a code; 0 if absent.
int eventCount(Session * session, ChannelID code)
{
    if (!session->eventlist.contains(code)) return 0;
    int n = 0;
    for (EventList * el : session->eventlist[code]) n += int(el->count());
    return n;
}

} // namespace

void PrismaVentTests::initTestCase()
{
    if (CPAP_Obstructive == 0) {
        schema::init();
    }
    // The loaders register their own channels; the test needs both sets.
    if (PrismaVent_Mode == 0) {
        PrismaLoader().initChannels();
        PrismaVentLoader().initChannels();
    }
}

void PrismaVentTests::testParseLogTime()
{
    qint64 ms = 0;
    QVERIFY(parseLogTime("+0013:44:40.081", ms));
    QCOMPARE(ms, ((13LL * 60 + 44) * 60 + 40) * 1000 + 81);

    // Sessions running past midnight keep counting hours from the log's date.
    QVERIFY(parseLogTime("+0032:46:51.592", ms));
    QCOMPARE(ms, ((32LL * 60 + 46) * 60 + 51) * 1000 + 592);

    QVERIFY(!parseLogTime("13:44", ms));
    QVERIFY(!parseLogTime("", ms));
}

void PrismaVentTests::testParseRespEvents()
{
    const QByteArray xml =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<desc>\n"
        "<start time=\"+0022:42:01.154\"/>\n"
        "<RespEvent time = \"0\" id = \"241\" event = \"begin\" Strength = \"0\"/>\n"
        "<RespEvent time = \"17771\" id = \"141\" event = \"begin\" Strength = \"0\"/>\n"
        "<RespEvent time = \"17782\" id = \"141\" event = \"end\" Strength = \"0\"/>\n"
        "<RespEvent time = \"17706\" id = \"6\" event = \"begin\" Strength = \"1\"/>\n"
        "<RespEvent time = \"17829\" id = \"6\" event = \"end\" Strength = \"1\"/>\n"
        "<RespEvent time = \"20000\" id = \"111\" event = \"end\" Strength = \"0\"/>\n"
        "<stop time=\"+0008:46:51.592\"/>\n</desc>";

    const QList<RespEvent> events = parseRespEvents(xml);
    QCOMPARE(events.size(), 4);

    // A begin with no end stays open.
    QCOMPARE(events[0].id, 241);
    QCOMPARE(events[0].beginMs, 0LL);
    QCOMPARE(events[0].endMs, -1LL);

    QCOMPARE(events[1].id, 141);
    QCOMPARE(events[1].beginMs, 17771000LL);
    QCOMPARE(events[1].endMs, 17782000LL);

    // Out of time order across ids, as the device writes them.
    QCOMPARE(events[2].id, 6);
    QCOMPARE(events[2].beginMs, 17706000LL);
    QCOMPARE(events[2].endMs, 17829000LL);
    QCOMPARE(events[2].strength, 1);

    // An end with no begin was already under way when the session started.
    QCOMPARE(events[3].id, 111);
    QCOMPARE(events[3].beginMs, 0LL);
    QCOMPARE(events[3].endMs, 20000000LL);
}

void PrismaVentTests::testParameterLogFollowsActiveProgram()
{
    const QByteArray map =
        "<parameters version=\"6.3.0\" count=\"3\">"
        "<parameter name=\"ActiveProgram\" id=\"1\"/>"
        "<parameter name=\"IPAP\" id=\"69\"/>"
        "<parameter name=\"TherapyMode\" id=\"92\"/>"
        "</parameters>";
    const QByteArray log =
        "<log version=\"1\" date=\"2026-10-16\">"
        "<start time=\"+0013:44:40.081\"/>"
        "<parameter time=\"+0013:44:40.080\" id=\"1\" value=\"0\"/>"
        "<parameter time=\"+0013:44:40.080\" id=\"69\" value=\"1000\" program=\"0\"/>"
        "<parameter time=\"+0013:44:40.080\" id=\"69\" value=\"1200\" program=\"1\"/>"
        "<parameter time=\"+0013:45:56.040\" id=\"69\" value=\"1700\" program=\"0\"/>"
        "<parameter time=\"+0013:47:34.382\" id=\"1\" value=\"1\"/>"
        "<stop time=\"+0013:52:26.244\"/>"
        "</log>";

    ParameterLog params;
    QVERIFY(params.load(log, map));

    const qint64 midnight = QDateTime(QDate(2026, 10, 16), QTime(0, 0), EDFInfo::localNoDST).toMSecsSinceEpoch();
    auto at = [midnight](int h, int m, int s) { return midnight + ((h * 60LL + m) * 60 + s) * 1000; };

    bool ok = false;
    QCOMPARE(params.globalValue("ActiveProgram", at(13, 45, 0), &ok), 0);
    QVERIFY(ok);
    QCOMPARE(params.globalValue("ActiveProgram", at(13, 48, 0)), 1);

    // A later change to a program is seen only after it was made.
    QCOMPARE(params.programValue("IPAP", 0, at(13, 45, 0)), 1000);
    QCOMPARE(params.programValue("IPAP", 0, at(13, 46, 0)), 1700);
    QCOMPARE(params.programValue("IPAP", 1, at(13, 46, 0)), 1200);

    // Nothing before the first snapshot, and nothing for an unmapped name.
    params.programValue("IPAP", 0, at(13, 0, 0), &ok);
    QVERIFY(!ok);
    params.programValue("EPAP", 0, at(13, 46, 0), &ok);
    QVERIFY(!ok);
}

void PrismaVentTests::testSampleCard()
{
    const QString card = qEnvironmentVariable("OSCAR_PRISMAVENT_CARD");
    if (card.isEmpty() || !QDir(card).exists()) {
        QSKIP("OSCAR_PRISMAVENT_CARD is not set to a prisma VENT card folder");
    }

    QTemporaryDir profileDir;
    QVERIFY(profileDir.isValid());
    Profile profile(profileDir.path(), false);
    Profile * previousProfile = p_profile;
    p_profile = &profile;

    PrismaVentLoader loader;
    QVERIFY(loader.Detect(card));

    const MachineInfo info = loader.PeekInfo(card);
    QCOMPARE(info.loadername, prismavent_class_name);
    QCOMPARE(info.model, QStringLiteral("prisma VENT50-C"));
    QCOMPARE(info.modelnumber, QStringLiteral("5"));
    QVERIFY(!info.serial.isEmpty());

    // The card holds two device folders; only one has a day archive.
    const QString deviceDir = loader.selectDeviceDir(card);
    QCOMPARE(loader.findDeviceDirs(card).size(), 2);
    const QStringList zips = QDir(deviceDir).entryList(QStringList("*.zip"), QDir::Files);
    QCOMPARE(zips.size(), 1);

    CollectingContext ctx(&profile);
    loader.SetContext(&ctx);
    ctx.CreateMachineFromInfo(info);
    QCOMPARE(loader.importDayArchive(QDir(deviceDir).absoluteFilePath(zips.first())), 3);
    QCOMPARE(ctx.sessions.size(), 3);

    Session * night = nullptr;
    for (Session * s : ctx.sessions) {
        if (night == nullptr || s->length() > night->length()) night = s;
    }
    QVERIFY(night != nullptr);

    // 36261 one-second records from 22:42:01 local time.
    const QDateTime start = QDateTime::fromMSecsSinceEpoch(night->first(), EDFInfo::localNoDST);
    QCOMPARE(start.time(), QTime(22, 42, 1));
    QCOMPARE(night->length(), 36261LL * 1000);

    // Settings of program 1 (written as program 0) at the session start.
    QCOMPARE(night->settings[PrismaVent_Program].toInt(), 1);
    QCOMPARE(night->settings[PrismaVent_Mode].toInt(), 2);
    QCOMPARE(night->settings[CPAP_Mode].toInt(), int(MODE_BILEVEL_FIXED));
    QCOMPARE(night->settings[CPAP_IPAP].toDouble(), 17.0);
    QCOMPARE(night->settings[CPAP_EPAP].toDouble(), 9.0);
    QCOMPARE(night->settings[CPAP_PS].toDouble(), 8.0);
    QCOMPARE(night->settings[PrismaVent_BackupRate].toDouble(), 14.0);
    QCOMPARE(night->settings[PrismaVent_Ti].toDouble(), 1.4);
    QCOMPARE(night->settings[PrismaVent_TiMin].toDouble(), 0.6);
    QCOMPARE(night->settings[PrismaVent_TiMax].toDouble(), 1.8);
    QCOMPARE(night->settings[PrismaVent_TriggerInsp].toInt(), 3);
    QCOMPARE(night->settings[PrismaVent_TriggerExp].toInt(), 25);
    QCOMPARE(night->settings[CPAP_RampTime].toInt(), 10);
    QCOMPARE(night->settings[CPAP_RampPressure].toDouble(), 4.0);
    QCOMPARE(night->settings[PrismaVent_InspRamp].toInt(), 3);
    QCOMPARE(night->settings[PrismaVent_ExpRamp].toInt(), 2);

    // Waveforms: 10 Hz flow and pressure, 1 Hz trends; oximetry was all zeros.
    QVERIFY(night->eventlist.contains(CPAP_FlowRate));
    QCOMPARE(night->eventlist[CPAP_FlowRate].first()->count(), 362610U);
    QVERIFY(night->eventlist.contains(CPAP_MaskPressure));
    QCOMPARE(night->eventlist[CPAP_TidalVolume].first()->count(), 36261U);
    QVERIFY(night->eventlist.contains(CPAP_MinuteVent));
    QVERIFY(night->eventlist.contains(CPAP_RespRate));
    QVERIFY(night->eventlist.contains(CPAP_PTB));
    QVERIFY(night->eventlist.contains(CPAP_LeakTotal));
    QVERIFY(!night->eventlist.contains(OXI_SPO2));
    QVERIFY(!night->eventlist.contains(OXI_Pulse));

    // Events: one OA, five OH, one artefact, and the 10-minute softSTART ramp.
    QCOMPARE(eventCount(night, CPAP_Obstructive), 1);
    QCOMPARE(eventCount(night, CPAP_ObstructiveHypopnea), 5);
    QCOMPARE(eventCount(night, Prisma_Artifact), 1);
    QCOMPARE(eventCount(night, CPAP_Ramp), 1);
    EventList * ramp = night->eventlist[CPAP_Ramp].first();
    QCOMPARE(ramp->data(0), EventDataType(600));
    QCOMPARE(ramp->time(0), night->first() + 600000);
    QVERIFY(!night->eventlist.contains(CPAP_CentralHypopnea));

    // Three premature-cycling epochs, graded by strength.
    QCOMPARE(eventCount(night, PrismaVent_ePC), 3);
    QCOMPARE(night->eventlist[PrismaVent_ePC].first()->data2(0), EventDataType(1));

    // The short afternoon session holds the card's one sudden decline of leakage.
    int sel = 0;
    for (Session * s : ctx.sessions) sel += eventCount(s, PrismaVent_SEL);
    QCOMPARE(sel, 1);

    // Every event id on the card is known.
    QVERIFY2(ctx.messages().isEmpty(), qPrintable(QStringList(ctx.messages().values()).join("; ")));

    loader.SetContext(nullptr);
    p_profile = previousProfile;
}

void PrismaVentTests::testEveryCardSession()
{
    const QStringList cards = qEnvironmentVariable("OSCAR_PRISMAVENT_CARDS").split(';', Qt::SkipEmptyParts);
    if (cards.isEmpty()) {
        QSKIP("OSCAR_PRISMAVENT_CARDS is not set");
    }

    QTemporaryDir profileDir;
    QVERIFY(profileDir.isValid());
    Profile profile(profileDir.path(), false);
    Profile * previousProfile = p_profile;
    p_profile = &profile;

    for (const QString & card : cards) {
        PrismaVentLoader loader;
        QVERIFY2(loader.Detect(card), qPrintable(card));
        const QString deviceDir = loader.selectDeviceDir(card);
        const QStringList zips = QDir(deviceDir).entryList(QStringList("*.zip"), QDir::Files, QDir::Name);

        CollectingContext ctx(&profile);
        loader.SetContext(&ctx);
        ctx.CreateMachineFromInfo(loader.PeekInfo(card));

        int expected = 0;
        for (const QString & zip : zips) {
            const int before = ctx.sessions.size();
            const int added = loader.importDayArchive(QDir(deviceDir).absoluteFilePath(zip));
            QCOMPARE(ctx.sessions.size() - before, added);
            expected += added;
        }
        QVERIFY(expected > 0);

        // Every session has a flow waveform, sits inside its own time range, and
        // carries the active program's mode; the events stay within the session.
        QSet<SessionID> ids;
        for (Session * s : ctx.sessions) {
            QVERIFY2(!ids.contains(s->session()), "duplicate session id");
            ids.insert(s->session());
            QVERIFY(s->eventlist.contains(CPAP_FlowRate));
            QVERIFY(s->settings.contains(PrismaVent_Mode));
            QVERIFY(s->settings.contains(PrismaVent_Program));
            for (auto it = s->eventlist.begin(); it != s->eventlist.end(); ++it) {
                for (EventList * el : it.value()) {
                    if (el->count() == 0) continue;
                    QVERIFY2(el->first() >= s->first() && el->last() <= s->last(),
                             qPrintable(QString("channel 0x%1 outside session %2").arg(it.key(), 0, 16).arg(s->session())));
                }
            }
            if (s->eventlist.contains(CPAP_Ramp)) {
                for (EventList * el : s->eventlist[CPAP_Ramp]) {
                    for (quint32 i = 0; i < el->count(); i++) {
                        QVERIFY(el->data(i) <= s->settings[CPAP_RampTime].toDouble() * 60);
                    }
                }
            }
        }
        QVERIFY2(ctx.messages().isEmpty(), qPrintable(QStringList(ctx.messages().values()).join("; ")));
        qDebug() << "PrismaVent card imported" << ctx.sessions.size() << "sessions from" << zips.size() << "archives";
        loader.SetContext(nullptr);
    }
    p_profile = previousProfile;
}
