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

#ifndef FLOWPARSERTESTS_H
#define FLOWPARSERTESTS_H

#include "AutoTest.h"

class Machine;
class Session;
class QCoreApplication;
class QTemporaryDir;

class FlowParserTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testStartOfBlock_data();
    void testStartOfBlock();
    void testLongInspiration_data();
    void testLongInspiration();
    void cleanupTestCase();

private:
    //! \brief A session whose flow waveform is sampled at hz from the given breaths (inspiration, expiration in s)
    Session *makeSession(int hz, const QVector<QPair<double, double>> &breaths);

    QCoreApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QString m_previousAppData;
    Machine *m_machine = nullptr;
    qint64 m_nextStartMs = 0;
};

DECLARE_TEST(FlowParserTests)

#endif // FLOWPARSERTESTS_H
