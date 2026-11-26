/* Therapy Insights Unit Tests
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "tests/AutoTest.h"

class QApplication;

//! \brief Tests for the Therapy Insights calculations.
//!
//! testScenario runs every file in tests/insights_scenarios/ (format in
//! Tools/insights_testdata/README.md). testRealDataSnapshot runs over the local,
//! git-ignored testdata/insights/real/ folder and is skipped when it is absent.
class InsightsTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testScenarioLoader();
    void testScenario_data();
    void testScenario();
    void testRealDataSnapshot();

private:
    QApplication *m_app = nullptr;
};
DECLARE_TEST(InsightsTests)
