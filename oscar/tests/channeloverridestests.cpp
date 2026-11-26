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
                                   LowerThreshold, UpperThreshold, ShowInOverview, Type };

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
// A NULL or unparsable stored colour (an invalid QColor) is not an override (review M4).
void ChannelOverridesTests::testClassifySkipsInvalidColour() {
    schema::Channel &c = schema::channel[CPAP_Obstructive];
    QVERIFY(c.defaults().color.name() != QColor().name());   // the default is not #000000
    ChannelData row = legacyOf(c);
    row.defaultColor = QColor();
    QVERIFY(classifyLegacy(c, row, [](const QString &) { return false; }).isEmpty());
}
// apply() sets the fields present and overridable for the channel, and nothing else.
void ChannelOverridesTests::testApplySetsOnlyOverridableFields() {
    schema::Channel &flag = schema::channel[CPAP_Obstructive];
    const EventDataType flagUpper = flag.upperThreshold();
    const QString flagName = flag.fullname();
    Values v;
    v.label = QString("Zz Label Zz");
    v.upperThreshold = 42.0;
    ChannelOverrides::apply(flag, v);
    QCOMPARE(flag.label(), QString("Zz Label Zz"));
    QCOMPARE(flag.upperThreshold(), flagUpper);       // no thresholds on a flag channel
    QCOMPARE(flag.fullname(), flagName);              // absent fields are left alone

    schema::Channel &setting = schema::channel[CPAP_PSMin];
    const QString settingLabel = setting.label();
    const QString settingColor = setting.defaultColor().name();
    Values s;
    s.label = QString("Zz Setting Zz");
    s.color = QColor("#123456");
    ChannelOverrides::apply(setting, s);
    QCOMPARE(setting.label(), settingLabel);
    QCOMPARE(setting.defaultColor().name(), settingColor);
}
// A changed threshold on a WAVEFORM channel is an override.
void ChannelOverridesTests::testDiffReportsWaveformThreshold() {
    schema::Channel &c = schema::channel[CPAP_FlowRate];
    QCOMPARE(c.type(), schema::WAVEFORM);
    c.setUpperThreshold(c.defaults().upperThreshold + 5);
    const Values v = diff(c);
    QVERIFY(v.upperThreshold.has_value());
    QCOMPARE(*v.upperThreshold, double(c.defaults().upperThreshold + 5));
}
// Thresholds that preferences drive are never overrides.
void ChannelOverridesTests::testDiffIgnoresPreferenceThresholds() {
    schema::Channel &leak = schema::channel[CPAP_Leak];
    QCOMPARE(leak.type(), schema::WAVEFORM);
    leak.setUpperThreshold(leak.defaults().upperThreshold + 5);
    QVERIFY(diff(leak).isEmpty());

    schema::Channel &pulse = schema::channel[OXI_Pulse];
    QCOMPARE(pulse.type(), schema::WAVEFORM);
    pulse.setLowerThreshold(pulse.defaults().lowerThreshold + 5);
    QVERIFY(diff(pulse).isEmpty());
}
// Enabled and ShowInOverview changes are overrides.
void ChannelOverridesTests::testDiffReportsEnabledAndOverview() {
    schema::Channel &c = schema::channel[CPAP_Obstructive];
    c.setEnabled(!c.defaults().enabled);
    c.setShowInOverview(!c.defaults().showInOverview);
    const Values v = diff(c);
    QCOMPARE(v.enabled.value(), !c.defaults().enabled);
    QCOMPARE(v.showInOverview.value(), !c.defaults().showInOverview);
    QVERIFY(!v.label && !v.fullname && !v.color);
}
// The Flag Type can be overridden on the three flag kinds and on nothing else (#430).
void ChannelOverridesTests::testFlagTypeOverridableOnFlagKindsOnly() {
    for (schema::ChanType t : { schema::FLAG, schema::MINOR_FLAG, schema::SPAN })
        QVERIFY(overridable(CPAP_Obstructive, t, Type));
    for (schema::ChanType t : { schema::UNKNOWN, schema::WAVEFORM, schema::DATA, schema::SETTING })
        QVERIFY(!overridable(CPAP_Obstructive, t, Type));
}
// A changed Flag Type is an override, and resetToDefaults() puts the code's type back.
void ChannelOverridesTests::testDiffReportsFlagTypeAndResetRestoresIt() {
    schema::Channel &c = schema::channel[CPAP_Obstructive];
    QCOMPARE(c.type(), schema::FLAG);
    c.setType(schema::SPAN);
    const Values v = diff(c);
    QCOMPARE(v.type.value(), schema::SPAN);
    QVERIFY(!v.label && !v.fullname && !v.color && !v.enabled && !v.showInOverview);
    c.resetToDefaults();
    QCOMPARE(c.type(), schema::FLAG);
    QVERIFY(diff(c).isEmpty());
}
// apply() takes a flag kind for a flag channel and ignores any other stored type.
void ChannelOverridesTests::testApplyAcceptsOnlyFlagKinds() {
    schema::Channel &flag = schema::channel[CPAP_Obstructive];
    Values v;
    v.type = schema::WAVEFORM;
    ChannelOverrides::apply(flag, v);
    QCOMPARE(flag.type(), schema::FLAG);
    v.type = schema::UNKNOWN;
    ChannelOverrides::apply(flag, v);
    QCOMPARE(flag.type(), schema::FLAG);
    v.type = static_cast<schema::ChanType>(12345);
    ChannelOverrides::apply(flag, v);
    QCOMPARE(flag.type(), schema::FLAG);
    v.type = schema::MINOR_FLAG;
    ChannelOverrides::apply(flag, v);
    QCOMPARE(flag.type(), schema::MINOR_FLAG);

    schema::Channel &wave = schema::channel[CPAP_FlowRate];
    Values w;
    w.type = schema::FLAG;
    ChannelOverrides::apply(wave, w);
    QCOMPARE(wave.type(), schema::WAVEFORM);
}
// The type an older build stored was never read back, so it is never an override.
void ChannelOverridesTests::testClassifyNeverTakesStoredType() {
    schema::Channel &c = schema::channel[CPAP_Obstructive];
    ChannelData row = legacyOf(c);
    row.type = schema::SPAN;
    QVERIFY(classifyLegacy(c, row, [](const QString &) { return false; }).isEmpty());
}
