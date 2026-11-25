/* Channel Overrides Unit Tests
 *
 * Tests the schema default snapshots and the override rules in channel_overrides.h.
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "channeloverridestests.h"

#include <QCoreApplication>

#include "SleepLib/channel_overrides.h"
#include "SleepLib/schema.h"
#include "database/channel_repository.h"

using namespace ChannelOverrides;

static const QList<Field> kAll = { Enabled, Color, Fullname, Label, Description,
                                   LowerThreshold, UpperThreshold, ShowInOverview };

void ChannelOverridesTests::initTestCase()
{
    static int argc = 1;
    static char appName[] = "test";
    static char *argv[] = { appName, nullptr };
    m_app = new QCoreApplication(argc, argv);

    schema::init();
    schema::snapshotDefaults();
}

void ChannelOverridesTests::cleanup()
{
    schema::resetChannelsToDefaults();
}

void ChannelOverridesTests::cleanupTestCase()
{
    schema::done();
    delete m_app;
    m_app = nullptr;
}

ChannelData ChannelOverridesTests::legacyOf(schema::Channel &chan)
{
    const schema::ChannelDefaults &d = chan.defaults();
    ChannelData row;
    row.channelId = chan.id();
    row.channelCode = chan.code();
    row.type = chan.type();
    row.enabled = d.enabled;
    row.defaultColor = d.color;
    row.fullname = d.fullname;
    row.label = d.label;
    row.description = d.description;
    row.lowerThreshold = d.lowerThreshold;
    row.lowerThresholdColor = d.lowerThresholdColor;
    row.upperThreshold = d.upperThreshold;
    row.upperThresholdColor = d.upperThresholdColor;
    row.showInOverview = d.showInOverview;
    return row;
}

void ChannelOverridesTests::testSettingChannelHasNothingOverridable() {
    QCOMPARE(schema::channel[CPAP_PSMin].type(), schema::SETTING);
    for (Field f : kAll) QVERIFY(!overridable(CPAP_PSMin, schema::SETTING, f));
}
void ChannelOverridesTests::testFlagChannelHasNoThresholds() {
    const auto t = schema::channel[CPAP_Obstructive].type();
    QVERIFY(overridable(CPAP_Obstructive, t, Fullname));
    QVERIFY(overridable(CPAP_Obstructive, t, Color));
    QVERIFY(overridable(CPAP_Obstructive, t, Enabled));
    QVERIFY(!overridable(CPAP_Obstructive, t, LowerThreshold));
    QVERIFY(!overridable(CPAP_Obstructive, t, UpperThreshold));
}
void ChannelOverridesTests::testPreferenceDrivenThresholds() {
    QVERIFY(!overridable(CPAP_Leak, schema::WAVEFORM, UpperThreshold));
    QVERIFY(overridable(CPAP_Leak, schema::WAVEFORM, LowerThreshold));
    QVERIFY(!overridable(OXI_Pulse, schema::WAVEFORM, LowerThreshold));
    QVERIFY(!overridable(OXI_Pulse, schema::WAVEFORM, UpperThreshold));
    QVERIFY(!overridable(OXI_SPO2, schema::WAVEFORM, LowerThreshold));
    QVERIFY(overridable(OXI_SPO2, schema::WAVEFORM, UpperThreshold));
}
void ChannelOverridesTests::testSnapshotIsTakenOnce() {
    schema::Channel &c = schema::channel[CPAP_Obstructive];
    const QString original = c.defaults().label;
    c.setLabel("Mine");
    c.snapshotDefaults();
    QCOMPARE(c.defaults().label, original);
    c.resetToDefaults();
    QCOMPARE(c.label(), original);
}
void ChannelOverridesTests::testDiffReportsOnlyWhatChanged() {
    schema::Channel &c = schema::channel[CPAP_Obstructive];
    QVERIFY(diff(c).isEmpty());
    c.setLabel("Mine");
    const Values v = diff(c);
    QCOMPARE(v.label.value(), QString("Mine"));
    QVERIFY(!v.fullname && !v.description && !v.color && !v.enabled && !v.showInOverview);
}
void ChannelOverridesTests::testDiffIgnoresSettingChannel() {
    schema::Channel &c = schema::channel[CPAP_PSMin];
    c.setLabel("Mine");
    c.setDefaultColor(QColor("#123456"));
    QVERIFY(diff(c).isEmpty());
}
void ChannelOverridesTests::testEmptyStringIsAnOverride() {
    schema::Channel &c = schema::channel[CPAP_Obstructive];
    c.setLabel(QString(""));
    QVERIFY(diff(c).label.has_value());
    QCOMPARE(diff(c).label.value(), QString(""));
}
void ChannelOverridesTests::testClassifyDropsDefaultsAndShippedNames() {
    schema::Channel &c = schema::channel[CPAP_Obstructive];
    ChannelData row = legacyOf(c);
    row.fullname = "An Old Shipped Name";
    row.type = schema::SETTING;                       // stored type is ignored
    const Values v = classifyLegacy(c, row, [](const QString &s) { return s == "An Old Shipped Name"; });
    QVERIFY(v.isEmpty());
}
void ChannelOverridesTests::testClassifyKeepsRenameAndColour() {
    schema::Channel &c = schema::channel[CPAP_Obstructive];
    ChannelData row = legacyOf(c);
    row.label = "MyOA";
    row.defaultColor = QColor("#123456");
    row.upperThreshold = 99;                          // not overridable on a flag
    const Values v = classifyLegacy(c, row, [](const QString &) { return false; });
    QCOMPARE(v.label.value(), QString("MyOA"));
    QCOMPARE(v.color.value().name(), QString("#123456"));
    QVERIFY(!v.upperThreshold && !v.fullname);
}
void ChannelOverridesTests::testClassifyIgnoresSettingChannel() {
    schema::Channel &c = schema::channel[CPAP_PSMin];
    ChannelData row = legacyOf(c);
    row.fullname = "Never Shipped";
    row.defaultColor = QColor("#123456");
    QVERIFY(classifyLegacy(c, row, [](const QString &) { return false; }).isEmpty());
}
