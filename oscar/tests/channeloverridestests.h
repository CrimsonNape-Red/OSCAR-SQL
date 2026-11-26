/* Channel Overrides Unit Tests
 *
 * Tests the schema default snapshots and the override rules in channel_overrides.h.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef CHANNELOVERRIDESTESTS_H
#define CHANNELOVERRIDESTESTS_H

#include "AutoTest.h"

#include "SleepLib/schema.h"

struct ChannelData;
class QCoreApplication;

//! \brief Tests for schema::Channel default snapshots and ChannelOverrides rules (no database).
class ChannelOverridesTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void testSettingChannelHasNothingOverridable();
    void testFlagChannelHasNoThresholds();
    void testPreferenceDrivenThresholds();
    void testSnapshotIsTakenOnce();
    void testDiffReportsOnlyWhatChanged();
    void testDiffIgnoresSettingChannel();
    void testEmptyStringIsAnOverride();
    void testClassifyDropsDefaultsAndShippedNames();
    void testClassifyKeepsRenameAndColour();
    void testClassifyIgnoresSettingChannel();
    void testClassifySkipsInvalidColour();
    void testApplySetsOnlyOverridableFields();
    void testDiffReportsWaveformThreshold();
    void testDiffIgnoresPreferenceThresholds();
    void testDiffReportsEnabledAndOverview();
    void testFlagTypeOverridableOnFlagKindsOnly();
    void testDiffReportsFlagTypeAndResetRestoresIt();
    void testApplyAcceptsOnlyFlagKinds();
    void testClassifyNeverTakesStoredType();
    void cleanupTestCase();

private:
    //! \brief Builds a ChannelData holding \p chan's code defaults.
    ChannelData legacyOf(schema::Channel &chan);

    QCoreApplication *m_app = nullptr;
};

DECLARE_TEST(ChannelOverridesTests)

#endif // CHANNELOVERRIDESTESTS_H
