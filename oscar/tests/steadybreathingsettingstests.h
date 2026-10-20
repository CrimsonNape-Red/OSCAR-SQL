/* Steady Breathing Settings Unit Tests
 *
 * Tests that the Steady Breathing settings belong to each profile, and that the old
 * app-wide values are copied into a profile once, the first time it loads.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef STEADYBREATHINGSETTINGSTESTS_H
#define STEADYBREATHINGSETTINGSTESTS_H

#include "AutoTest.h"

class Profile;
class QCoreApplication;
class QTemporaryDir;

class SteadyBreathingSettingsTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testAppWideValueCopiedOnce();
    void testProfilesAreIndependent();
    void cleanupTestCase();

private:
    QCoreApplication *m_app = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QString m_previousAppData;
    Profile *m_profileA = nullptr;
    Profile *m_profileB = nullptr;
};

DECLARE_TEST(SteadyBreathingSettingsTests)

#endif // STEADYBREATHINGSETTINGSTESTS_H
