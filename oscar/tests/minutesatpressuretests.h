/* Time at Pressure Recalculation Unit Tests
 *
 * Tests the background recalculation of the Time at Pressure graph layer
 * (MinutesAtPressure / RecalcMAP): the totals themselves, that a calculation replaced by a
 * newer one never overwrites the newer totals, and that changing the day, cloning the layer
 * and destroying it are safe while a calculation is under way (#276).
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef MINUTESATPRESSURETESTS_H
#define MINUTESATPRESSURETESTS_H

#include <QVector>

#include "AutoTest.h"

class Day;
class Machine;
class MinutesAtPressure;
class QCoreApplication;
class QTemporaryDir;

class MinutesAtPressureTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testSynchronousTotals();
    void testNewestRangeWins();
    void testSetDayStopsCalculation();
    void testCloneHasNoCalculation();
    void testDestroyWhileCalculating();
    void cleanupTestCase();

private:
    //! \brief Add a CPAP session of one pressure sample per second to the test device.
    //! The first \a lowSeconds samples are 10 cmH2O, the rest 12 cmH2O.
    Day *addNight(const QDate &date, int seconds, int lowSeconds);

    //! \brief Total a time range on the calling thread and return the time per pressure bucket.
    QVector<int> totalsFor(Day *day, qint64 minTime, qint64 maxTime);

    //! \brief Copy of the layer's time per pressure bucket, taken under its lock.
    QVector<int> times(MinutesAtPressure &layer);

    //! \brief Start a background calculation on the layer, with no view to redraw.
    void startInBackground(MinutesAtPressure &layer, qint64 minTime, qint64 maxTime);

    QCoreApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QString m_previousAppData;
    Machine *m_machine = nullptr;
    Day *m_shortNight = nullptr;    //!< 15 minutes: 10 at 10 cmH2O, then 5 at 12 cmH2O
    Day *m_longNight = nullptr;     //!< 8 hours, so a calculation takes long enough to interrupt
    qint64 m_shortStart = 0;
    qint64 m_longStart = 0;
};

DECLARE_TEST(MinutesAtPressureTests)

#endif // MINUTESATPRESSURETESTS_H
