/* Flow Parser Unit Tests
 *
 * Tests the respiratory rate, tidal volume, minute ventilation and inspiratory and expiratory
 * times that OSCAR calculates from the flow waveform (FlowParser::calc(), MR !87): steady
 * breathing gives steady values at 5 Hz and 25 Hz, no respiratory rate is reported in the
 * first half minute of a flow block, an implausibly long inspiration gets no values, and the
 * respiratory rate and tidal volume lists hold no entries after their count.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "flowparsertests.h"

#include <QDateTime>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QtMath>

#include "SleepLib/appsettings.h"
#include "SleepLib/calcs.h"
#include "SleepLib/common.h"
#include "SleepLib/machine.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"
#include "database/database_manager.h"

namespace {

const QString kProfileName = QStringLiteral("FlowParserUnitTest");

// Flow is stored in 0.1 l/min. Each breath is a half sine of this peak flow in and out.
constexpr EventDataType kGain = 0.1f;
constexpr double kPeakFlow = 30.0; // l/min

// Steady breathing: 2 s in, 2 s out, 15 breaths a minute.
constexpr double kInspiration = 2.0;
constexpr double kExpiration = 2.0;
constexpr double kRate = 15.0;

// Tidal volume is stored in steps of 20 ml.
constexpr double kTidalVolumeStep = 20.0;

constexpr qint64 kSecondMs = 1000;
constexpr qint64 kMinuteMs = 60 * kSecondMs;

// Tidal volume of the inspiration half sine: peak flow (ml/s) * 2 * Ti / pi.
double expectedTidalVolume()
{
    return kPeakFlow * 1000.0 / 60.0 * 2.0 * kInspiration / M_PI;
}

EventList *firstList(Session *sess, ChannelID code)
{
    const QVector<EventList *> lists = sess->eventlist.value(code);
    return lists.isEmpty() ? nullptr : lists.first();
}

// The lists are written entry by entry; none may keep unused space at the end.
void verifyNoUnusedEntries(EventList *list)
{
    QCOMPARE(list->getData().size(), int(list->count()));
    QCOMPARE(list->getTime().size(), int(list->count()));
}

void addHz()
{
    QTest::addColumn<int>("hz");
    QTest::newRow("5 Hz") << 5;
    QTest::newRow("25 Hz") << 25;
}

} // namespace

void FlowParserTests::initTestCase()
{
    // note: need dummy app for QtSql even though headless (each suite owns its own)
    static int argc = 1;
    static char appName[] = "test";
    static char *argv[] = { appName, nullptr };
    m_app = new QCoreApplication(argc, argv);

    if (DatabaseManager::instance().isOpen()) {
        DatabaseManager::instance().close();
    }

    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-flowparsertests-XXXXXX"));
    QVERIFY(m_tempDir->isValid());

    m_previousAppData = GetAppData();
    SetAppData(m_tempDir->path());

    p_profile = nullptr;
    p_pref = new Preferences(QStringLiteral("Preferences"));
    p_pref->Open();
    AppSetting = new AppWideSetting(p_pref);
    QVERIFY(DatabaseManager::instance().initialize(m_tempDir->path() + QStringLiteral("/oscar.db")));

    schema::init();
    Profiles::Scan();

    const QString profileDir = m_tempDir->path() + QStringLiteral("/Profiles/") + kProfileName;
    p_profile = Profiles::Get(kProfileName);
    if (p_profile == nullptr) {
        p_profile = Profiles::Create(kProfileName, &profileDir);
    }
    QVERIFY(p_profile != nullptr);

    const MachineInfo info(
        MT_CPAP, 0, QStringLiteral("FlowParserTestLoader"), QStringLiteral("Test"),
        QStringLiteral("Test CPAP"), QString(), QStringLiteral("flow-parser-cpap"),
        QStringLiteral("Test"), QDateTime::currentDateTime(), 1);
    m_machine = p_profile->CreateMachine(info);
    QVERIFY(m_machine != nullptr);

    m_nextStartMs = QDateTime(QDate(2026, 10, 1), QTime(23, 0)).toMSecsSinceEpoch();
}

Session *FlowParserTests::makeSession(int hz, const QVector<QPair<double, double>> &breaths)
{
    double total = 0;
    for (const auto &b : breaths) {
        total += b.first + b.second;
    }

    // Sample in the middle of each interval, so that no sample falls exactly on a zero crossing.
    const int samples = int(total * hz);
    QVector<qint16> raw(samples);
    int b = 0;
    double breathStart = 0;
    for (int i = 0; i < samples; i++) {
        const double t = (i + 0.5) / hz;
        while (t >= breathStart + breaths[b].first + breaths[b].second) {
            breathStart += breaths[b].first + breaths[b].second;
            b++;
        }
        const double ti = breaths[b].first;
        const double te = breaths[b].second;
        const double dt = t - breathStart;
        const double flow = (dt < ti) ? kPeakFlow * qSin(M_PI * dt / ti) : -kPeakFlow * qSin(M_PI * (dt - ti) / te);
        raw[i] = qint16(qRound(flow / kGain));
    }

    const qint64 startMs = m_nextStartMs;
    m_nextStartMs += 24 * 60 * kMinuteMs;
    const qint64 durationMs = qint64(samples) * kSecondMs / hz;

    Session *sess = new Session(m_machine, static_cast<SessionID>(startMs / kSecondMs));
    EventList *flow = sess->AddEventList(CPAP_FlowRate, EVL_Waveform, kGain, 0.0, 0.0, 0.0, 1000.0 / hz);
    flow->AddWaveform(startMs, raw.data(), samples, durationMs);
    sess->really_set_first(startMs);
    sess->really_set_last(startMs + durationMs);
    return sess;
}

void FlowParserTests::testStartOfBlock_data()
{
    addHz();
}

// A flow block that starts with the shortest breath the parser accepts (1.2 s), then 30 minutes of steady
// breathing. Scaling that single breath to a minute used to give 50/min; now no rate is reported until half a
// minute of breaths is available, and the respiratory rate list has no entries after its count.
void FlowParserTests::testStartOfBlock()
{
    QFETCH(int, hz);

    QVector<QPair<double, double>> breaths;
    breaths.append(qMakePair(0.6, 0.6));
    for (int i = 0; i < 449; i++) {
        breaths.append(qMakePair(kInspiration, kExpiration));
    }
    QScopedPointer<Session> session(makeSession(hz, breaths));
    Session *sess = session.data();
    const qint64 start = firstList(sess, CPAP_FlowRate)->first();
    calcRespRate(sess);

    EventList *rr = firstList(sess, CPAP_RespRate);
    EventList *tv = firstList(sess, CPAP_TidalVolume);
    EventList *mv = firstList(sess, CPAP_MinuteVent);
    EventList *ti = firstList(sess, CPAP_Ti);
    EventList *te = firstList(sess, CPAP_Te);
    QVERIFY(rr && tv && mv && ti && te);
    QVERIFY(rr->count() > 400);

    verifyNoUnusedEntries(rr);
    verifyNoUnusedEntries(tv);

    // No rate in the first half minute, and the short first breath causes no spike
    QVERIFY(rr->time(0) - start >= kMinuteMs / 2);
    for (quint32 i = 0; i < rr->count(); i++) {
        QVERIFY2(rr->data(i) <= 17.0,
                 qPrintable(QStringLiteral("rate %1 at %2 s").arg(rr->data(i)).arg((rr->time(i) - start) / 1000)));
        if (rr->time(i) - start > 70 * kSecondMs) {
            QVERIFY(qAbs(rr->data(i) - kRate) <= 0.3);
        }
    }

    // Steady values once the first minute and the smoothing are past
    const double tvExpected = expectedTidalVolume();
    for (quint32 i = 0; i < tv->count(); i++) {
        if (tv->time(i) - start > kMinuteMs) {
            QVERIFY(qAbs(tv->data(i) - tvExpected) <= kTidalVolumeStep);
        }
    }
    for (quint32 i = 0; i < mv->count(); i++) {
        if (mv->time(i) - start > 70 * kSecondMs) {
            QVERIFY(qAbs(mv->data(i) - tvExpected / 1000.0 * kRate) <= 0.03 * tvExpected / 1000.0 * kRate);
        }
    }
    for (quint32 i = 0; i < ti->count(); i++) {
        if (ti->time(i) - start > kMinuteMs) {
            QVERIFY(qAbs(ti->data(i) - kInspiration) <= 0.05);
        }
    }
    for (quint32 i = 0; i < te->count(); i++) {
        if (te->time(i) - start > kMinuteMs) {
            QVERIFY(qAbs(te->data(i) - kExpiration) <= 0.05);
        }
    }

}

void FlowParserTests::testLongInspiration_data()
{
    addHz();
}

// Ten minutes of steady breathing, one "breath" whose inspiration lasts 15 s (as when the flow stays above the
// zero line while the baseline catches up with a change of leak), and ten more minutes. That breath gets no tidal
// volume, minute ventilation, inspiratory or expiratory time, and does not disturb the smoothed values after it.
void FlowParserTests::testLongInspiration()
{
    QFETCH(int, hz);

    const int steadyBreaths = 150;
    QVector<QPair<double, double>> breaths;
    for (int i = 0; i < steadyBreaths; i++) {
        breaths.append(qMakePair(kInspiration, kExpiration));
    }
    breaths.append(qMakePair(15.0, kExpiration));
    for (int i = 0; i < steadyBreaths; i++) {
        breaths.append(qMakePair(kInspiration, kExpiration));
    }
    QScopedPointer<Session> session(makeSession(hz, breaths));
    Session *sess = session.data();
    const qint64 start = firstList(sess, CPAP_FlowRate)->first();
    calcRespRate(sess);

    EventList *rr = firstList(sess, CPAP_RespRate);
    EventList *tv = firstList(sess, CPAP_TidalVolume);
    EventList *mv = firstList(sess, CPAP_MinuteVent);
    EventList *ti = firstList(sess, CPAP_Ti);
    EventList *te = firstList(sess, CPAP_Te);
    QVERIFY(rr && tv && mv && ti && te);
    QVERIFY(tv->count() > 250);

    verifyNoUnusedEntries(rr);
    verifyNoUnusedEntries(tv);

    // The same breaths are skipped for all of them
    QCOMPARE(ti->count(), tv->count());
    QCOMPARE(te->count(), tv->count());

    // Nothing is written for the long breath: its middle is 15 s after it starts at 10 minutes
    const qint64 longStart = start + steadyBreaths * qint64((kInspiration + kExpiration) * kSecondMs);
    for (quint32 i = 0; i < ti->count(); i++) {
        QVERIFY(ti->time(i) <= longStart || ti->time(i) >= longStart + 17 * kSecondMs);
    }

    // No value anywhere above the steady one
    const double tvExpected = expectedTidalVolume();
    for (quint32 i = 0; i < tv->count(); i++) {
        QVERIFY2(tv->data(i) <= tvExpected + kTidalVolumeStep,
                 qPrintable(QStringLiteral("tidal volume %1 ml").arg(tv->data(i))));
    }
    for (quint32 i = 0; i < mv->count(); i++) {
        QVERIFY2(mv->data(i) <= 1.03 * tvExpected / 1000.0 * kRate,
                 qPrintable(QStringLiteral("minute ventilation %1 l/min").arg(mv->data(i))));
    }
    for (quint32 i = 0; i < ti->count(); i++) {
        QVERIFY2(ti->data(i) <= kInspiration + 0.05, qPrintable(QStringLiteral("Ti %1 s").arg(ti->data(i))));
    }
    for (quint32 i = 0; i < te->count(); i++) {
        QVERIFY2(te->data(i) <= kExpiration + 0.05, qPrintable(QStringLiteral("Te %1 s").arg(te->data(i))));
    }

}

void FlowParserTests::cleanupTestCase()
{
    Profiles::profiles.clear();

    delete p_profile;
    p_profile = nullptr;
    delete AppSetting;
    AppSetting = nullptr;
    delete p_pref;
    p_pref = nullptr;

    DatabaseManager::instance().close();
    SetAppData(m_previousAppData);

    delete m_tempDir;
    m_tempDir = nullptr;
    delete m_app;
    m_app = nullptr;
}
