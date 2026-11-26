/* Time at Pressure Recalculation Unit Tests
 *
 * Tests the background recalculation of the Time at Pressure graph layer
 * (MinutesAtPressure / RecalcMAP): the totals themselves, that a calculation replaced by a
 * newer one never overwrites the newer totals, and that changing the day, cloning the layer
 * and destroying it are safe while a calculation is under way (#276).
 *
 * The layer is driven through startRecalc(), which needs no graph or graph view.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "minutesatpressuretests.h"

#include <QDateTime>
#include <QMutexLocker>
#include <QTemporaryDir>
#include <QThreadPool>

#include "Graphs/MinutesAtPressure.h"
#include "SleepLib/appsettings.h"
#include "SleepLib/common.h"
#include "SleepLib/machine.h"
#include "SleepLib/preferences.h"
#include "SleepLib/profiles.h"
#include "SleepLib/schema.h"
#include "SleepLib/session.h"
#include "database/database_manager.h"

namespace {

const QString kProfileName = QStringLiteral("MinutesAtPressureUnitTest");

// Pressure samples are stored as raw value * gain. The layer puts 10.0 cmH2O in bucket 50
// and 12.0 cmH2O in bucket 60 (five buckets per cmH2O).
constexpr EventDataType kGain = 0.1f;
constexpr EventStoreType kLowRaw = 100;
constexpr EventStoreType kHighRaw = 120;
constexpr int kLowBucket = 50;
constexpr int kHighBucket = 60;

constexpr int kShortSeconds = 15 * 60;
constexpr int kShortLowSeconds = 10 * 60;
constexpr int kLongSeconds = 8 * 3600;
constexpr int kLongLowSeconds = 2 * 3600;
constexpr qint64 kHourMs = 3600 * 1000LL;

// Generous: the jobs take milliseconds, this only bounds a hang.
constexpr int kWaitMs = 60000;

} // namespace

void MinutesAtPressureTests::initTestCase()
{
    // note: need dummy app for QtSql even though headless (each suite owns its own)
    static int argc = 1;
    static char appName[] = "test";
    static char *argv[] = { appName, nullptr };
    m_app = new QCoreApplication(argc, argv);

    if (DatabaseManager::instance().isOpen()) {
        DatabaseManager::instance().close();
    }

    m_tempDir = new QTemporaryDir(QDir::tempPath() + QStringLiteral("/oscar-minutesatpressuretests-XXXXXX"));
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
        MT_CPAP, 0, QStringLiteral("TimeAtPressureTestLoader"), QStringLiteral("Test"),
        QStringLiteral("Test CPAP"), QString(), QStringLiteral("time-at-pressure-cpap"),
        QStringLiteral("Test"), QDateTime::currentDateTime(), 1);
    m_machine = p_profile->CreateMachine(info);
    QVERIFY(m_machine != nullptr);

    const QDate shortDate(2026, 10, 1);
    const QDate longDate(2026, 10, 3);
    m_shortStart = QDateTime(shortDate, QTime(23, 0)).toMSecsSinceEpoch();
    m_longStart = QDateTime(longDate, QTime(23, 0)).toMSecsSinceEpoch();
    m_shortNight = addNight(shortDate, kShortSeconds, kShortLowSeconds);
    m_longNight = addNight(longDate, kLongSeconds, kLongLowSeconds);
    QVERIFY(m_shortNight != nullptr);
    QVERIFY(m_longNight != nullptr);
    QVERIFY(m_shortNight != m_longNight);
}

Day *MinutesAtPressureTests::addNight(const QDate &date, int seconds, int lowSeconds)
{
    const qint64 start = QDateTime(date, QTime(23, 0)).toMSecsSinceEpoch();
    Session *sess = new Session(m_machine, static_cast<SessionID>(start / 1000L));
    EventList *pressure = sess->AddEventList(CPAP_Pressure, EVL_Event, kGain);
    for (int i = 0; i < seconds; ++i) {
        pressure->AddEvent(start + i * 1000LL, (i < lowSeconds) ? kLowRaw : kHighRaw);
    }
    // Two flagged events, so the per-event part of the calculation runs as well
    EventList *oa = sess->AddEventList(CPAP_Obstructive, EVL_Event);
    oa->AddEvent(start + 5 * 60000LL, 12);
    oa->AddEvent(start + 12 * 60000LL, 15);
    sess->really_set_first(start);
    sess->really_set_last(start + (seconds - 1) * 1000LL);
    sess->SetChanged(true);
    sess->UpdateSummaries();
    if (!m_machine->AddSession(sess)) {
        return nullptr;
    }
    return p_profile->GetDay(date, MT_CPAP);
}

QVector<int> MinutesAtPressureTests::times(MinutesAtPressure &layer)
{
    QMutexLocker locker(&layer.timelock);
    return layer.ipap.times;
}

QVector<int> MinutesAtPressureTests::totalsFor(Day *day, qint64 minTime, qint64 maxTime)
{
    MinutesAtPressure layer;
    layer.SetDay(day);
    layer.startRecalc(minTime, maxTime, nullptr, true);
    return times(layer);
}

void MinutesAtPressureTests::startInBackground(MinutesAtPressure &layer, qint64 minTime, qint64 maxTime)
{
    layer.startRecalc(minTime, maxTime, nullptr, false);
}

void MinutesAtPressureTests::testSynchronousTotals()
{
    MinutesAtPressure layer;
    layer.SetDay(m_shortNight);
    QVERIFY(!layer.isEmpty());

    const qint64 end = m_shortStart + (kShortSeconds - 1) * 1000LL;
    layer.startRecalc(m_shortStart, end, nullptr, true);

    // The totals are in place when a synchronous calculation returns, and nothing is left running.
    QVERIFY(!layer.recalculating());
    QVERIFY(layer.initialized);
    const QVector<int> result = times(layer);
    QVERIFY(result.size() > kHighBucket);
    QCOMPARE(result[kLowBucket], kShortLowSeconds * 1000);
    QCOMPARE(result[kHighBucket], (kShortSeconds - kShortLowSeconds - 1) * 1000);
    for (int i = 0; i < result.size(); ++i) {
        if ((i != kLowBucket) && (i != kHighBucket)) QCOMPARE(result[i], 0);
    }
    QCOMPARE(layer.ipap.numEvents.value(CPAP_Obstructive), 2);
}

void MinutesAtPressureTests::testNewestRangeWins()
{
    MinutesAtPressure layer;
    layer.SetDay(m_longNight);

    // What dragging the time selection does: a new range before the last one has been totalled.
    for (int i = 0; i < 300; ++i) {
        startInBackground(layer, m_longStart, m_longStart + ((i % 7) + 1) * kHourMs);
    }
    const qint64 lastEnd = m_longStart + 3 * kHourMs;
    startInBackground(layer, m_longStart, lastEnd);

    QTRY_VERIFY_WITH_TIMEOUT(!layer.recalculating(), kWaitMs);

    // Whatever order the abandoned jobs ended in, the totals are those of the last range asked for.
    const QVector<int> expected = totalsFor(m_longNight, m_longStart, lastEnd);
    QCOMPARE(expected[kLowBucket], kLongLowSeconds * 1000);
    QCOMPARE(expected[kHighBucket], int(3 * kHourMs) - kLongLowSeconds * 1000);
    QCOMPARE(times(layer), expected);
}

void MinutesAtPressureTests::testSetDayStopsCalculation()
{
    MinutesAtPressure layer;
    const qint64 longEnd = m_longStart + (kLongSeconds - 1) * 1000LL;
    const qint64 shortEnd = m_shortStart + (kShortSeconds - 1) * 1000LL;

    for (int i = 0; i < 100; ++i) {
        layer.SetDay(m_longNight);
        startInBackground(layer, m_longStart, longEnd);
        startInBackground(layer, m_longStart, m_longStart + kHourMs);

        // Daily clears the day before its sessions are freed: nothing may be running after that.
        layer.SetDay(nullptr);
        QVERIFY(!layer.recalculating());
        QVERIFY(layer.isEmpty());
    }

    // The layer still works afterwards, and totals from the old day don't come back.
    layer.SetDay(m_shortNight);
    startInBackground(layer, m_shortStart, shortEnd);
    QTRY_VERIFY_WITH_TIMEOUT(!layer.recalculating(), kWaitMs);
    QCOMPARE(times(layer), totalsFor(m_shortNight, m_shortStart, shortEnd));
}

void MinutesAtPressureTests::testCloneHasNoCalculation()
{
    MinutesAtPressure layer;
    layer.SetDay(m_longNight);
    const qint64 end = m_longStart + (kLongSeconds - 1) * 1000LL;

    for (int i = 0; i < 50; ++i) {
        startInBackground(layer, m_longStart, end);

        // A pop-out graph copies the layer, possibly in the middle of a calculation.
        Layer *copy = layer.Clone();
        MinutesAtPressure *clone = dynamic_cast<MinutesAtPressure *>(copy);
        QVERIFY(clone != nullptr);
        QVERIFY(!clone->recalculating());

        // The copy calculates for itself and can be destroyed while the original carries on.
        clone->startRecalc(m_longStart, m_longStart + kHourMs, nullptr, true);
        QCOMPARE(times(*clone)[kLowBucket], int(kHourMs));
        delete copy;
    }

    QTRY_VERIFY_WITH_TIMEOUT(!layer.recalculating(), kWaitMs);
    QCOMPARE(times(layer), totalsFor(m_longNight, m_longStart, end));
}

void MinutesAtPressureTests::testDestroyWhileCalculating()
{
    const qint64 end = m_longStart + (kLongSeconds - 1) * 1000LL;

    for (int i = 0; i < 100; ++i) {
        MinutesAtPressure *layer = new MinutesAtPressure();
        layer->SetDay(m_longNight);
        startInBackground(*layer, m_longStart, end);
        startInBackground(*layer, m_longStart, m_longStart + kHourMs);
        // The destructor has to wait for both jobs: they write to the layer when they end.
        delete layer;
    }

    // No job is left behind in the pool to touch a layer that has gone.
    QVERIFY(QThreadPool::globalInstance()->waitForDone(kWaitMs));
}

void MinutesAtPressureTests::cleanupTestCase()
{
    QThreadPool::globalInstance()->waitForDone(kWaitMs);

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
