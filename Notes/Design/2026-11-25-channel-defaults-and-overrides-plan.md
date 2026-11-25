# Channel Defaults and Per-Profile Overrides Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Store channel defaults once for the whole database and, per profile, only what the user changed, so corrected names, translations and default colours reach existing profiles while renames survive (GitLab #418).

**Architecture:** `channels` (one full copy per profile) is replaced by a global `channel_defaults` table rewritten from code, a sparse `profile_channel_overrides` table, and a `channels` view that joins them under the old column names. Old rows are parked in `channels_legacy` by the SQL migration and converted for every profile once the channel registry is built, using a pure classifier and a bundled list of every channel name OSCAR has shipped.

**Tech Stack:** C++17, Qt 6 (QtSql, QtTest, QtWidgets), SQLite, Python 3 for the one-off tool.

**Spec:** `Notes/Design/2026-11-24-channel-defaults-and-overrides-design.md`. Read it first; this plan argues from it.

## Global Constraints

- C++17; compile clean on Qt 6.10.2 and stay compatible back to Qt 6.4; no `#if QT_VERSION` guards.
- Cross-platform only. Never touch `oscar/SleepLib/thirdparty`. Do not change any loader's data version.
- New modules: file header with a brief description and `Copyright (c) 2026 The OSCAR Team`; Doxygen comments in every `.h` and `.cpp`; add every new file to `oscar/oscar.pro` (sources, headers, and the `tests/` lists for tests).
- Schema policy: v19 → v20 ships a migration and loses no data. Schema upgrades stay user-gated (#293).
- `MIN_RESTORE_SCHEMA_VERSION` stays 12: backups from schema 12–19 must still restore.
- No dialog and no question during the upgrade or conversion.
- A user override applies in every language.
- `Notes/` is published: no serial numbers, contributor names or sample-folder names in any file.
- UI strings use US spelling to match the existing dialog ("Color", so "customized").
- Work on a branch named `channel-overrides`, not `master`: a half-finished v20 must never reach a nightly build.
- Commits: ask the developer before each commit; write the message to a temporary file and use `git commit -F`; one-line subject plus a 2–3 sentence summary in lines of at most 90 characters; no co-author line; check with `git log -1 --pretty=%B`. The last commit's body includes `Closes #418`.
- Unit tests: build with `CONFIG+=test` in a separate build folder and run `.\test.exe`. The exit code is the number of failures (0 = pass). Redirection captures no output; never `QSKIP` in `initTestCase()`. Tests reach private members through `friend class <TestClass>` under `UNITTEST_MODE`.

## Review Focus

1. **Two profiles opened in one run.** Profile A's overrides must not appear in profile B. Test in Task 3.
2. **`Profiles::Scan()` runs again while a profile is open** (after a restore or 1.x import). `channel_defaults` must be written from the pristine snapshot, not from the open profile's values. Test in Task 3.
3. **A stale `channels.dat` left in a profile folder by an older build.** It must not be imported over converted overrides. Test in Task 5.
4. **A row whose channel id is unknown to this build.** It is matched by `channel_code`; if that also fails it must survive a save and a conversion untouched. Tests in Tasks 2 and 5.
5. **Conversion run twice, or a user who clears a label to an empty string.** A second run changes nothing; an empty string is an override and reads back as empty, not as the default. Tests in Tasks 3 and 5.

---

## File Structure

| File | Responsibility |
|---|---|
| `oscar/SleepLib/schema.h`, `schema.cpp` (modify) | `Channel` keeps a snapshot of its code defaults |
| `oscar/SleepLib/channel_overrides.h/.cpp` (create) | Pure rules: which fields can be overrides, diff against defaults, classify an old row |
| `oscar/database/database_schema.h/.cpp` (modify) | v20 tables, view, migration |
| `oscar/database/channel_repository.h/.cpp` (rewrite) | SQL for defaults, overrides and legacy rows |
| `oscar/SleepLib/channel_store.h/.cpp` (create) | Glue between registry and database: sync defaults, convert legacy rows |
| `oscar/SleepLib/shipped_channel_names.h/.cpp` (create) | Lookup in the bundled list |
| `Tools/gen_shipped_channel_names.py` (create), `oscar/docs/shipped_channel_names.txt` (generated) | The list of names OSCAR has shipped |
| `oscar/SleepLib/profiles.h/.cpp`, `oscar/profileimporter.cpp` (modify) | Load, save, `channels.dat` import |
| `oscar/database/backup/profile_backup.cpp`, `profile_restore.cpp` (modify) | Backup and restore |
| `oscar/preferencesdialog.h/.cpp/.ui` (modify) | Markers, Reset Channel, filter |
| `oscar/tests/channeloverridestests.*`, `channelstoragetests.*`, `channelconversiontests.*`, `channelbackuptests.*` (create) | Tests |

---

### Task 1: Defaults snapshot and override rules

**Files:**
- Modify: `oscar/SleepLib/schema.h`, `oscar/SleepLib/schema.cpp`
- Create: `oscar/SleepLib/channel_overrides.h`, `oscar/SleepLib/channel_overrides.cpp`
- Test: `oscar/tests/channeloverridestests.h`, `oscar/tests/channeloverridestests.cpp`
- Modify: `oscar/oscar.pro`

**Interfaces:**
- Consumes: `ChannelData` from `oscar/database/channel_repository.h` (unchanged in this task). Forward-declare `struct ChannelData;` in `channel_overrides.h` and include the repository header only in the `.cpp`: from Task 2 the repository header includes `channel_overrides.h`.
- Produces, in `schema.h`:

```cpp
namespace schema {
struct ChannelDefaults {
    bool enabled = true;
    QColor color;
    QString fullname, label, description;
    EventDataType lowerThreshold = 0, upperThreshold = 0;
    QColor lowerThresholdColor, upperThresholdColor;
    bool showInOverview = false;
};
// members of class Channel:
bool hasDefaults() const;
const ChannelDefaults &defaults() const;
void snapshotDefaults();   // records the current values; does nothing if already recorded
void resetToDefaults();    // puts the recorded values back; does nothing if none recorded
// free functions:
void snapshotDefaults();           // every channel that has no snapshot yet
void resetChannelsToDefaults();    // every channel
}
```

- Produces, in `channel_overrides.h`:

```cpp
namespace ChannelOverrides {
enum Field { Enabled, Color, Fullname, Label, Description,
             LowerThreshold, UpperThreshold, ShowInOverview };
struct Values {
    std::optional<bool> enabled;
    std::optional<QColor> color;
    std::optional<QString> fullname, label, description;
    std::optional<double> lowerThreshold, upperThreshold;
    std::optional<bool> showInOverview;
    bool isEmpty() const;
};
using ShippedLookup = std::function<bool(const QString &)>;

bool overridable(ChannelID id, schema::ChanType type, Field field);
Values diffFrom(ChannelID id, schema::ChanType type,
                const schema::ChannelDefaults &defaults, const Values &current);
Values diff(const schema::Channel &chan);            // chan's current values against chan.defaults()
void apply(schema::Channel &chan, const Values &v);  // sets only present, overridable fields
Values classifyLegacy(const schema::Channel &chan, const ChannelData &legacy,
                      const ShippedLookup &wasShipped);
}
```

Rules the bodies must follow (from the spec, "What can be an override"):
- `DATA` and `SETTING`: no field is overridable.
- `WAVEFORM`: all eight fields.
- Every other type (`FLAG`, `MINOR_FLAG`, `SPAN`, `UNKNOWN`): all except the two thresholds.
- Never overridable, whatever the type: `CPAP_Leak` `UpperThreshold`; `OXI_Pulse` `LowerThreshold` and `UpperThreshold`; `OXI_SPO2` `LowerThreshold`.
- Colours compare by `QColor::name()` (`#RRGGBB`); thresholds are equal when they differ by less than `1e-4`.
- `classifyLegacy` uses `chan.type()`, never `legacy.type`. A name field becomes an override only if it differs from the default **and** `wasShipped(name)` is false. Other fields become overrides when they differ.

- [ ] **Step 1: Write the failing tests** in `channeloverridestests.cpp` (suite `ChannelOverridesTests`, `initTestCase()` creates a `QCoreApplication` as `machinepurgedatetests.cpp` does, then `schema::init(); schema::snapshotDefaults();`; `cleanup()` calls `schema::resetChannelsToDefaults()`):

```cpp
using namespace ChannelOverrides;
static const QList<Field> kAll = { Enabled, Color, Fullname, Label, Description,
                                   LowerThreshold, UpperThreshold, ShowInOverview };

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
// legacyOf(chan) builds a ChannelData holding chan's defaults.
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
```

- [ ] **Step 2: Build the tests and run `.\test.exe`.** Expected: the build fails (`channel_overrides.h` not found).
- [ ] **Step 3: Implement the `schema.h` additions.** `schema::resetChannels()` ends with `snapshotDefaults()`. The snapshot must be explicit, not taken in the constructor: loaders change defaults after construction (for example `prs1_loader.cpp:3082` `setEnabled(false)`, `schema.cpp:425` `setShowInOverview(true)`).
- [ ] **Step 4: Implement `channel_overrides.cpp`** to the rules above.
- [ ] **Step 5: Run `.\test.exe`.** Expected: exit code 0.
- [ ] **Step 6: Commit** (ask first): "Add channel default snapshots and override rules".

---

### Task 2: Schema v20 and the channel repository

**Files:**
- Modify: `oscar/database/database_schema.h`, `oscar/database/database_schema.cpp`
- Rewrite: `oscar/database/channel_repository.h`, `oscar/database/channel_repository.cpp`
- Test: `oscar/tests/channelstoragetests.h`, `oscar/tests/channelstoragetests.cpp`
- Modify: `oscar/oscar.pro`

**Interfaces:**
- Consumes: `ChannelOverrides::Values` (Task 1).
- Produces, in `database_schema.h`: `CURRENT_SCHEMA_VERSION = 20`; `createChannelDefaultsTable`, `createProfileChannelOverridesTable`, `createChannelsLegacyTable`, `createChannelsView`, `migrateV19ToV20` (all `static bool f(QSqlDatabase&)`).
- Produces, in `channel_repository.h` (`ChannelData` keeps its current fields and now means "a row of `channels_legacy`"):

```cpp
struct ChannelDefaultRow {
    ChannelID channelId = 0; QString channelCode; int type = 0;
    bool enabled = true; QColor defaultColor; QString fullname, label, description;
    double lowerThreshold = 0; QColor lowerThresholdColor;
    double upperThreshold = 0; QColor upperThresholdColor; bool showInOverview = false;
};
struct ChannelOverrideRow { ChannelID channelId = 0; QString channelCode; ChannelOverrides::Values values; };

class ChannelRepository {
public:
    ChannelRepository();
    explicit ChannelRepository(const QSqlDatabase &database);
    bool syncDefaults(const QList<ChannelDefaultRow> &rows);   // insert/update changed rows, delete absent ones
    QList<ChannelOverrideRow> findOverrides(qint64 profileId);
    bool replaceOverrides(qint64 profileId, const QList<ChannelOverrideRow> &rows,
                          const QSet<ChannelID> &knownIds, const QSet<QString> &knownCodes);
    QList<qint64> profilesWithLegacyRows();
    QList<ChannelData> findLegacy(qint64 profileId);
    bool insertLegacy(qint64 profileId, const QList<ChannelData> &rows);
    bool deleteLegacy(const QList<qint64> &rowIds);
    static QString colorToString(const QColor &color);
    static QColor stringToColor(const QString &colorStr);
};
```

`replaceOverrides` runs in one transaction: it deletes the profile's rows whose `channel_id` is in `knownIds` or whose `channel_code` is in `knownCodes`, then inserts every row whose `values` is not empty. Rows for channels unknown to the running build are therefore left alone. An absent `optional` is stored as SQL `NULL`; a present empty string is stored as `''`.

Exact SQL:

```sql
CREATE TABLE IF NOT EXISTS channel_defaults (
    channel_id INTEGER PRIMARY KEY,
    channel_code TEXT NOT NULL,
    type INTEGER,
    enabled INTEGER NOT NULL DEFAULT 1,
    default_color TEXT,
    fullname TEXT, label TEXT, description TEXT,
    lower_threshold REAL, lower_threshold_color TEXT,
    upper_threshold REAL, upper_threshold_color TEXT,
    show_in_overview INTEGER DEFAULT 0,
    updated_at TEXT DEFAULT CURRENT_TIMESTAMP);

CREATE TABLE IF NOT EXISTS profile_channel_overrides (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    profile_id INTEGER NOT NULL,
    channel_id INTEGER NOT NULL,
    channel_code TEXT NOT NULL,
    enabled INTEGER, default_color TEXT,
    fullname TEXT, label TEXT, description TEXT,
    lower_threshold REAL, upper_threshold REAL,
    show_in_overview INTEGER,
    updated_at TEXT DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (profile_id) REFERENCES profiles(id) ON DELETE CASCADE,
    UNIQUE(profile_id, channel_id));

CREATE VIEW IF NOT EXISTS channels AS
SELECT p.id AS profile_id, d.channel_id, d.channel_code, d.type,
       COALESCE(o.enabled, d.enabled) AS enabled,
       COALESCE(o.default_color, d.default_color) AS default_color,
       COALESCE(o.fullname, d.fullname) AS fullname,
       COALESCE(o.label, d.label) AS label,
       COALESCE(o.description, d.description) AS description,
       COALESCE(o.lower_threshold, d.lower_threshold) AS lower_threshold,
       d.lower_threshold_color,
       COALESCE(o.upper_threshold, d.upper_threshold) AS upper_threshold,
       d.upper_threshold_color,
       COALESCE(o.show_in_overview, d.show_in_overview) AS show_in_overview,
       (o.id IS NOT NULL) AS is_customized
FROM profiles p
CROSS JOIN channel_defaults d
LEFT JOIN profile_channel_overrides o
       ON o.profile_id = p.id AND o.channel_id = d.channel_id;
```

`channels_legacy` has exactly the v19 `channels` columns (`createChannelsTable()` today), under the new name, with index `idx_channels_legacy_profile ON channels_legacy(profile_id)`. Every `CREATE` here uses `IF NOT EXISTS`, so the migration can be run on a database that already has some of the objects. The migration test calls `migrateV19ToV20` directly; add `friend class ChannelStorageTests;` to `DatabaseSchema` under `UNITTEST_MODE` if it is private.

`migrateV19ToV20`, in one transaction and in this order: `DROP INDEX IF EXISTS` for `idx_channels_profile`, `idx_channels_code`, `idx_channels_lookup`; `ALTER TABLE channels RENAME TO channels_legacy`; create the legacy index, the two tables and the view; `setSchemaVersion(db, 20)`. Add `{ 19, &migrateV19ToV20 }` to the `steps` table in `upgradeSchema()`. In `createSchema()` replace the `createChannelsTable()` call with the four new creators (a fresh database also needs `channels_legacy`, as the target for restoring old backups). In `createIndexes()` replace the three `idx_channels_*` lines with the legacy index.

- [ ] **Step 1: Write the failing tests** (suite `ChannelStorageTests`; `initTestCase()` follows `machinepurgedatetests.cpp`: temporary app-data folder, `DatabaseManager::instance().initialize(...)`, `schema::init()`, `Profiles::Scan()`, create profile `ChannelStorageUnitTest`; helper `scalar(sql)` returns the first column of the first row):

```cpp
void ChannelStorageTests::testFreshSchemaIsV20() {
    QCOMPARE(scalar("SELECT type FROM sqlite_master WHERE name='channels'").toString(), QString("view"));
    for (const char *t : { "channel_defaults", "profile_channel_overrides", "channels_legacy" })
        QCOMPARE(scalar(QString("SELECT type FROM sqlite_master WHERE name='%1'").arg(t)).toString(), QString("table"));
}
void ChannelStorageTests::testMigrationParksOldRows() {
    // Put the database back into its v19 shape, with one old row.
    exec("DROP VIEW channels");
    exec("ALTER TABLE channels_legacy RENAME TO channels");
    exec("DROP TABLE channel_defaults");
    exec("DROP TABLE profile_channel_overrides");
    exec(QString("INSERT INTO channels (profile_id, channel_id, channel_code, label) VALUES (%1, 60001, 'TestChan', 'MyOA')").arg(m_profileId));
    QVERIFY(DatabaseSchema::migrateV19ToV20(db()));
    QCOMPARE(scalar("SELECT label FROM channels_legacy WHERE channel_id=60001").toString(), QString("MyOA"));
    QCOMPARE(scalar("SELECT type FROM sqlite_master WHERE name='channels'").toString(), QString("view"));
    QCOMPARE(scalar("SELECT COUNT(*) FROM profile_channel_overrides").toInt(), 0);
    exec("DELETE FROM channels_legacy");
}
void ChannelStorageTests::testViewPrefersOverride() {
    ChannelRepository repo;
    ChannelDefaultRow d; d.channelId = 60001; d.channelCode = "TestChan"; d.fullname = "Def Name"; d.label = "Def";
    QVERIFY(repo.syncDefaults({ d }));
    const QString where = QString(" FROM channels WHERE profile_id=%1 AND channel_id=60001").arg(m_profileId);
    QCOMPARE(scalar("SELECT label" + where).toString(), QString("Def"));
    QCOMPARE(scalar("SELECT is_customized" + where).toInt(), 0);
    ChannelOverrideRow o; o.channelId = 60001; o.channelCode = "TestChan"; o.values.label = QString("Mine");
    QVERIFY(repo.replaceOverrides(m_profileId, { o }, { 60001 }, { "TestChan" }));
    QCOMPARE(scalar("SELECT label" + where).toString(), QString("Mine"));
    QCOMPARE(scalar("SELECT fullname" + where).toString(), QString("Def Name"));
    QCOMPARE(scalar("SELECT is_customized" + where).toInt(), 1);
}
void ChannelStorageTests::testOverridesRoundTrip() {
    ChannelRepository repo;
    ChannelOverrideRow a; a.channelId = 60001; a.channelCode = "TestChan";
    a.values.label = QString(""); a.values.color = QColor("#123456"); a.values.enabled = false;
    ChannelOverrideRow empty; empty.channelId = 60002; empty.channelCode = "Other";
    QVERIFY(repo.replaceOverrides(m_profileId, { a, empty }, { 60001, 60002 }, { "TestChan", "Other" }));
    const auto rows = repo.findOverrides(m_profileId);
    QCOMPARE(rows.size(), 1);                                  // the empty row is not stored
    QCOMPARE(rows[0].values.label.value(), QString(""));       // empty string, not NULL
    QCOMPARE(rows[0].values.color.value().name(), QString("#123456"));
    QCOMPARE(rows[0].values.enabled.value(), false);
    QVERIFY(!rows[0].values.fullname.has_value());
}
void ChannelStorageTests::testReplaceKeepsUnknownChannelRows() {
    ChannelRepository repo;
    ChannelOverrideRow gone; gone.channelId = 61000; gone.channelCode = "NotInThisBuild"; gone.values.label = QString("Keep");
    QVERIFY(repo.replaceOverrides(m_profileId, { gone }, { 61000 }, { "NotInThisBuild" }));
    QVERIFY(repo.replaceOverrides(m_profileId, {}, { 60001 }, { "TestChan" }));   // 61000 is unknown now
    const auto rows = repo.findOverrides(m_profileId);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].channelId, ChannelID(61000));
}
void ChannelStorageTests::testSyncDefaultsUpdatesAndDeletes() {
    ChannelRepository repo;
    ChannelDefaultRow a; a.channelId = 60001; a.channelCode = "TestChan"; a.fullname = "English";
    ChannelDefaultRow b; b.channelId = 60002; b.channelCode = "Other";
    QVERIFY(repo.syncDefaults({ a, b }));
    a.fullname = "Deutsch";
    QVERIFY(repo.syncDefaults({ a }));
    QCOMPARE(scalar("SELECT fullname FROM channel_defaults WHERE channel_id=60001").toString(), QString("Deutsch"));
    QCOMPARE(scalar("SELECT COUNT(*) FROM channel_defaults WHERE channel_id=60002").toInt(), 0);
}
void ChannelStorageTests::testLegacyRowsAndProfileDelete() {
    ChannelRepository repo;
    ChannelData row; row.channelId = 60001; row.channelCode = "TestChan"; row.label = "Old";
    QVERIFY(repo.insertLegacy(m_profileId, { row }));
    QCOMPARE(repo.profilesWithLegacyRows(), QList<qint64>{ m_profileId });
    const auto rows = repo.findLegacy(m_profileId);
    QCOMPARE(rows.size(), 1);
    QVERIFY(repo.deleteLegacy({ rows[0].id }));
    QVERIFY(repo.profilesWithLegacyRows().isEmpty());
}
```

- [ ] **Step 2: Build and run.** Expected: build fails (`ChannelDefaultRow` unknown).
- [ ] **Step 3: Implement the schema changes** as specified above.
- [ ] **Step 4: Rewrite `ChannelRepository`.** `Profile` in `profiles.cpp` still calls the removed methods; to keep the tree compiling, do Task 3 Steps 3–4 in the same working session and commit Tasks 2 and 3 together.
- [ ] **Step 5: Run `.\test.exe` after Task 3 Step 5.** Expected: exit code 0.

---

### Task 3: Load and save through overrides; keep `channel_defaults` current

**Files:**
- Create: `oscar/SleepLib/channel_store.h`, `oscar/SleepLib/channel_store.cpp`
- Modify: `oscar/SleepLib/profiles.h`, `oscar/SleepLib/profiles.cpp` (`saveChannels`, `loadChannels`, remove `saveChannelsToDatabase`, `loadChannelsFromDatabase`, `initializeChannelsFromSchema`; `Profiles::Scan()`)
- Modify: `oscar/profileimporter.cpp:309-318`
- Test: `oscar/tests/channelstoragetests.cpp`
- Modify: `oscar/oscar.pro`

**Interfaces:**
- Consumes: Task 1 and Task 2 interfaces.
- Produces:

```cpp
namespace ChannelStore {
bool syncDefaults();   // snapshots new channels, then writes every channel's defaults() to channel_defaults
}
```

Behaviour:
- `Profiles::Scan()` calls `ChannelStore::syncDefaults()` first, when the database is open. It writes `chan->defaults()`, never the channel's current values.
- `Profile::loadChannels()`: `schema::resetChannelsToDefaults()`; for each row of `findOverrides(profileId)` find the channel by id, then by `channel_code`; `ChannelOverrides::apply`; finally `resetOxiChannelPref()`. A row matching neither is skipped. The `changing_language` check and the profile's `Language` preference are no longer used here.
- `Profile::saveChannels()`: one `ChannelOverrideRow` per channel from `ChannelOverrides::diff`, then `replaceOverrides` with the registry's ids and codes.
- Until Task 5, `migrateChannelsToDatabase(dir)` is `loadChannelsFromDat(dir)` followed by `saveChannels()`, and `loadChannels()` no longer calls it. `profileimporter.cpp` drops the `initializeChannelsFromSchema()` fallback: a profile with no customizations needs no rows.

- [ ] **Step 1: Add the failing tests** to `ChannelStorageTests` (add `friend class ChannelStorageTests;` to `Profile` under `UNITTEST_MODE` if `loadChannels()` is private; `overrideCount()` is `SELECT COUNT(*) FROM profile_channel_overrides WHERE profile_id = m_profileId`):

```cpp
void ChannelStorageTests::testUnchangedProfileStoresNothing() {
    schema::resetChannelsToDefaults();
    p_profile->saveChannels();
    QCOMPARE(overrideCount(), 0);
}
void ChannelStorageTests::testRenameSurvivesReload() {
    schema::Channel &c = schema::channel[CPAP_Obstructive];
    const QString defName = c.defaults().fullname;
    c.setLabel("MyOA");
    p_profile->saveChannels();
    QCOMPARE(overrideCount(), 1);
    schema::resetChannelsToDefaults();
    p_profile->loadChannels();
    QCOMPARE(c.label(), QString("MyOA"));
    QCOMPARE(c.fullname(), defName);
}
void ChannelStorageTests::testEmptyLabelSurvivesReload() {
    schema::Channel &c = schema::channel[CPAP_Obstructive];
    c.setLabel(QString(""));
    p_profile->saveChannels();
    schema::resetChannelsToDefaults();
    p_profile->loadChannels();
    QCOMPARE(c.label(), QString(""));
}
void ChannelStorageTests::testOverrideDroppedWhenBackToDefault() {
    schema::Channel &c = schema::channel[CPAP_Obstructive];
    c.setLabel("MyOA");            p_profile->saveChannels();
    c.setLabel(c.defaults().label); p_profile->saveChannels();
    QCOMPARE(overrideCount(), 0);
}
void ChannelStorageTests::testPreferenceThresholdAndSettingNotStored() {
    schema::channel[CPAP_Leak].setUpperThreshold(30);
    schema::channel[CPAP_PSMin].setLabel("Mine");
    p_profile->saveChannels();
    QCOMPARE(overrideCount(), 0);
}
void ChannelStorageTests::testSecondProfileDoesNotInheritOverrides() {      // Review Focus 1
    schema::Channel &c = schema::channel[CPAP_Obstructive];
    c.setLabel("MyOA");
    p_profile->saveChannels();
    Profile *other = Profiles::Create("ChannelStorageUnitTestB", &m_otherDir);
    other->loadChannels();
    QCOMPARE(c.label(), c.defaults().label);
    p_profile->loadChannels();
    QCOMPARE(c.label(), QString("MyOA"));
}
void ChannelStorageTests::testScanWritesPristineDefaults() {                // Review Focus 2
    schema::Channel &c = schema::channel[CPAP_Obstructive];
    c.setLabel("MyOA");                       // an open profile's override is in memory
    QVERIFY(ChannelStore::syncDefaults());
    QCOMPARE(scalar(QString("SELECT label FROM channel_defaults WHERE channel_id=%1").arg(CPAP_Obstructive)).toString(),
             c.defaults().label);
    QCOMPARE(scalar("SELECT COUNT(*) FROM channel_defaults").toInt(), int(schema::channel.channels.size()));
}
```

Each test starts from a clean state: `init()` calls `schema::resetChannelsToDefaults()` and deletes the test profiles' override rows.

- [ ] **Step 2: Build and run.** Expected: build fails (`ChannelStore` unknown).
- [ ] **Step 3: Implement `channel_store.cpp` and the `Profiles::Scan()` call.**
- [ ] **Step 4: Rewrite `Profile::loadChannels()` and `saveChannels()`**; remove the three obsolete methods and their declarations; update `profileimporter.cpp`.
- [ ] **Step 5: Run `.\test.exe`.** Expected: exit code 0, including every Task 2 test.
- [ ] **Step 6: Build OSCAR itself** (build-oscar skill) to confirm the application compiles.
- [ ] **Step 7: Commit** (ask first): "Store channel defaults once and per-profile overrides (schema v20)".

---

### Task 4: The list of names OSCAR has shipped

**Files:**
- Create: `Tools/gen_shipped_channel_names.py`
- Create (generated): `oscar/docs/shipped_channel_names.txt`
- Modify: `oscar/Resources.qrc` (add `docs/shipped_channel_names.txt`)
- Create: `oscar/SleepLib/shipped_channel_names.h`, `oscar/SleepLib/shipped_channel_names.cpp`
- Test: `oscar/tests/channelconversiontests.h`, `oscar/tests/channelconversiontests.cpp`
- Modify: `oscar/oscar.pro`

**Interfaces:**
- Consumes: a v20 database whose `channel_defaults` was written by a run of OSCAR set to English (Task 3 provides this).
- Produces: `namespace ShippedChannelNames { bool contains(const QString &name); }`, which loads `:/docs/shipped_channel_names.txt` on first use.

Tool: `python Tools/gen_shipped_channel_names.py --repo <repo> --db <oscar.db> --out oscar/docs/shipped_channel_names.txt [--report <source string>]`

- **Channel source strings** = every `fullname`, `label` and `description` in `channel_defaults` of `--db`, plus every string that has ever been a `tr("...")` or `QObject::tr("...")` literal (C escapes decoded) in `oscar/SleepLib/schema.cpp`, `oscar/SleepLib/common.cpp` or `oscar/SleepLib/loader_plugins/*.cpp` in any commit, including the literals still present at `HEAD`. (An earlier version of this plan left out literals still present at `HEAD`. That missed names such as "Large Leak", which was once a channel's full name and survives today as a literal used for something else; a dry run on real profiles found them kept as if they were user renames.) The scan must also follow those files at every earlier location in the repository's history (for example `SleepLib/...` and `sleepyhead/SleepLib/...` before the `oscar/` layout), and the translation scan must cover every location the `.ts` files have had; `git log -- <current path>` alone does not follow them.
- **Output** = those source strings, plus every `<translation>` any `Translations/*.ts` file has ever paired with one of them as `<source>`. Scan every commit that touched `Translations/*.ts` (`git log --format=%H -- Translations`), reading only the files that commit changed, plus `HEAD`.
- **Format:** UTF-8, one string per line, sorted, unique, no empty line; inside a string a backslash is written `\\` and a newline `\n`.
- **Guard:** if fewer than 80% of the `channel_defaults` names occur as a `<source>` in the `.ts` files at `HEAD`, exit with an error saying the database was not written by an English run.
- `--report S` prints every translation collected for source `S`, with its language file, and writes nothing.
- The tool reads the database read-only and writes only OSCAR's own strings; it must not copy anything else from the database.

- [ ] **Step 1: Write the tool.**
- [ ] **Step 2: Produce the input.** Run the Task 3 build with the language set to English on a copy of the test data folder, close it, and confirm `SELECT COUNT(*) FROM channel_defaults` is about 300.
- [ ] **Step 3: Run the tool.** Expected: it prints the number of source strings and output lines; the output is well under 1 MB. Report the two numbers to the developer.
- [ ] **Step 4: Pick a pinned example.** Run `--report "Flow Limit."` and choose one `Chinese.zh_CN.ts` translation that differs from the one at `HEAD` (#417 corrected it). If there is none, try `"Tidal Volume"`, then `"Minute Ventilation"`. Use it as `kRetiredTranslation` below.
- [ ] **Step 5: Write the failing tests** (suite `ChannelConversionTests`, same fixture as `ChannelStorageTests`, profile `ChannelConversionUnitTest`):

```cpp
void ChannelConversionTests::testShippedNamesLookup() {
    QVERIFY(ShippedChannelNames::contains(schema::channel[CPAP_Obstructive].defaults().fullname));
    QVERIFY(ShippedChannelNames::contains(kRetiredTranslation));
    QVERIFY(!ShippedChannelNames::contains(QStringLiteral("zz not a channel name zz")));
    QVERIFY(!ShippedChannelNames::contains(QString()));
}
```

- [ ] **Step 6: Build and run.** Expected: build fails (`ShippedChannelNames` unknown).
- [ ] **Step 7: Implement `shipped_channel_names.cpp`** and add the resource.
- [ ] **Step 8: Run `.\test.exe`.** Expected: exit code 0.
- [ ] **Step 9: Commit** (ask first): "Add the list of channel names OSCAR has shipped".

---

### Task 5: Convert old rows for every profile; route `channels.dat` through the same path

**Files:**
- Modify: `oscar/SleepLib/channel_store.h`, `oscar/SleepLib/channel_store.cpp`
- Modify: `oscar/SleepLib/profiles.h`, `oscar/SleepLib/profiles.cpp` (`Profiles::Scan()`, `loadChannels()`, `migrateChannelsToDatabase()`, replace `loadChannelsFromDat()`)
- Modify: `oscar/SleepLib/common.h` (new preference key)
- Test: `oscar/tests/channelconversiontests.cpp`

**Interfaces:**
- Consumes: `ChannelRepository` (Task 2), `ChannelOverrides::classifyLegacy` (Task 1), `ShippedChannelNames::contains` (Task 4).
- Produces:

```cpp
namespace ChannelStore {
// Converts the channels_legacy rows of every profile; returns the number of legacy rows
// that matched a channel and were removed (whether or not they produced an override).
int convertLegacyRows(const ChannelOverrides::ShippedLookup &wasShipped = &ShippedChannelNames::contains);
}
const QString STR_PREF_ChannelsDatImported = "ChannelsDatImported";   // common.h
static QList<ChannelData> Profile::readChannelsDat(const QString &dir);  // parses only; touches no channel
```

Behaviour:
- `Profiles::Scan()` calls `ChannelStore::convertLegacyRows()` right after `syncDefaults()`. `Scan()` already runs at startup (`main.cpp:1749`), after a 1.x import (`mainwindow.cpp:1640`) and after a restore (`mainwindow.cpp:5136`), so all three cases are covered.
- For each profile in `profilesWithLegacyRows()`, in one transaction per profile: find each row's channel by id, then by `channel_code`; classify it against `chan.defaults()`; write the non-empty results as override rows under the registry's id and code (an override row that already exists for that channel wins); delete the legacy rows that were matched; save preference `ChannelsDatImported = true` for the profile with `PreferencesRepository::savePreference`, in the category under which `Profile`'s own `(*this)[key]` values are stored (confirm in `PreferencesRepository::saveAllPreferences`).
- A row matching no channel stays in `channels_legacy` and is tried again on later runs.
- `readChannelsDat(dir)` is today's `loadChannelsFromDat()` parser returning rows (`channelId` = the stored id, `channelCode` = the stored name) without changing any channel. `loadChannelsFromDat()` and its language check are removed.
- `migrateChannelsToDatabase(dir)`: rows from `readChannelsDat`; if none, return false; `insertLegacy`; `ChannelStore::convertLegacyRows()`; return true.
- `loadChannels()` calls `migrateChannelsToDatabase()` (the profile's own data folder) before loading only when `ChannelsDatImported` is not true, and then sets it to true whether or not a file was found.

- [ ] **Step 1: Write the failing tests** (`legacyOf(chan)` as in Task 1; `kFake` treats exactly `"Old Shipped"` as shipped; `init()` resets channels and clears the test profiles' override and legacy rows):

```cpp
void ChannelConversionTests::testConvertsEveryProfile() {
    ChannelRepository repo;
    schema::Channel &oa = schema::channel[CPAP_Obstructive];
    schema::Channel &ps = schema::channel[CPAP_PSMin];
    ChannelData same = legacyOf(schema::channel[CPAP_Hypopnea]);
    ChannelData renamed = legacyOf(oa);  renamed.label = "MyOA"; renamed.defaultColor = QColor("#123456");
    ChannelData setting = legacyOf(ps);  setting.fullname = "Never Shipped"; setting.defaultColor = QColor("#123456");
    ChannelData stale = legacyOf(oa);    stale.fullname = "Old Shipped";
    QVERIFY(repo.insertLegacy(m_profileId, { same, renamed, setting }));
    QVERIFY(repo.insertLegacy(m_otherProfileId, { stale }));          // this profile is never opened

    QCOMPARE(ChannelStore::convertLegacyRows(kFake), 4);

    const auto a = repo.findOverrides(m_profileId);
    QCOMPARE(a.size(), 1);
    QCOMPARE(a[0].channelId, ChannelID(CPAP_Obstructive));
    QCOMPARE(a[0].values.label.value(), QString("MyOA"));
    QCOMPARE(a[0].values.color.value().name(), QString("#123456"));
    QVERIFY(!a[0].values.fullname.has_value());
    QVERIFY(repo.findOverrides(m_otherProfileId).isEmpty());
    QVERIFY(repo.profilesWithLegacyRows().isEmpty());
}
void ChannelConversionTests::testMatchesByCodeAndKeepsUnknownRows() {        // Review Focus 4
    ChannelRepository repo;
    schema::Channel &oa = schema::channel[CPAP_Obstructive];
    ChannelData moved = legacyOf(oa); moved.channelId = 61001; moved.label = "MyOA";   // id changed, code still matches
    ChannelData unknown; unknown.channelId = 61002; unknown.channelCode = "NotInThisBuild"; unknown.label = "Keep";
    QVERIFY(repo.insertLegacy(m_profileId, { moved, unknown }));
    QCOMPARE(ChannelStore::convertLegacyRows(kFake), 1);
    QCOMPARE(repo.findOverrides(m_profileId)[0].channelId, ChannelID(CPAP_Obstructive));
    const auto left = repo.findLegacy(m_profileId);
    QCOMPARE(left.size(), 1);
    QCOMPARE(left[0].channelCode, QString("NotInThisBuild"));
}
void ChannelConversionTests::testSecondRunChangesNothing() {                 // Review Focus 5
    ChannelRepository repo;
    ChannelData renamed = legacyOf(schema::channel[CPAP_Obstructive]); renamed.label = "MyOA";
    QVERIFY(repo.insertLegacy(m_profileId, { renamed }));
    QCOMPARE(ChannelStore::convertLegacyRows(kFake), 1);
    QCOMPARE(ChannelStore::convertLegacyRows(kFake), 0);
    QCOMPARE(repo.findOverrides(m_profileId).size(), 1);
}
void ChannelConversionTests::testScanConverts() {
    ChannelRepository repo;
    ChannelData renamed = legacyOf(schema::channel[CPAP_Obstructive]); renamed.label = "Zz Never Shipped Zz";
    QVERIFY(repo.insertLegacy(m_profileId, { renamed }));
    Profiles::Scan();
    p_profile = Profiles::Get(kProfileName);
    QVERIFY(repo.profilesWithLegacyRows().isEmpty());
    QCOMPARE(repo.findOverrides(m_profileId)[0].values.label.value(), QString("Zz Never Shipped Zz"));
}
void ChannelConversionTests::testChannelsDatGoesThroughClassifier() {
    schema::Channel &oa = schema::channel[CPAP_Obstructive];
    oa.setLabel("Zz Dat Label Zz");
    p_profile->saveChannelsToDat();               // the old writer, still compiled
    schema::resetChannelsToDefaults();
    QVERIFY(p_profile->migrateChannelsToDatabase(p_profile->Get("{DataFolder}")));
    const auto rows = ChannelRepository().findOverrides(m_profileId);
    QCOMPARE(rows.size(), 1);                     // every other channel was at its default
    QCOMPARE(rows[0].values.label.value(), QString("Zz Dat Label Zz"));
}
void ChannelConversionTests::testStaleChannelsDatIsNotReimported() {         // Review Focus 3
    schema::channel[CPAP_Obstructive].setLabel("Zz Stale Zz");
    p_profile->saveChannelsToDat();               // a file left behind by an older build
    schema::resetChannelsToDefaults();
    ChannelRepository repo;
    QVERIFY(repo.insertLegacy(m_profileId, { legacyOf(schema::channel[CPAP_Hypopnea]) }));
    ChannelStore::convertLegacyRows(kFake);       // marks the profile as imported
    Profiles::Scan();
    p_profile = Profiles::Get(kProfileName);
    p_profile->loadChannels();
    QCOMPARE(schema::channel[CPAP_Obstructive].label(), schema::channel[CPAP_Obstructive].defaults().label);
}
```

- [ ] **Step 2: Build and run.** Expected: build fails (`convertLegacyRows` unknown).
- [ ] **Step 3: Implement `convertLegacyRows` and the `Scan()` call.**
- [ ] **Step 4: Replace `loadChannelsFromDat()` with `readChannelsDat()`; rewrite `migrateChannelsToDatabase()`; gate the lazy import in `loadChannels()`.**
- [ ] **Step 5: Run `.\test.exe`.** Expected: exit code 0.
- [ ] **Step 6: Commit** (ask first): "Convert stored channel copies into overrides for every profile".

---

### Task 6: Backup and restore

**Files:**
- Modify: `oscar/database/backup/profile_backup.cpp:717-741`
- Modify: `oscar/database/backup/profile_restore.cpp` (`executeSqlFile()` around lines 907-940 and 1115-1120; `restoreOrder` at 1185)
- Test: `oscar/tests/channelbackuptests.h`, `oscar/tests/channelbackuptests.cpp`
- Modify: `oscar/oscar.pro`

**Interfaces:**
- Consumes: the v20 tables (Task 2); `Profiles::Scan()` converting legacy rows (Task 5).
- Produces: backups containing `profile_channel_overrides.sql` and `channels_legacy.sql`, and no `channels.sql`.

Behaviour:
- Backup exports `profile_channel_overrides` and `channels_legacy` for the profile, both with the `profile_id` placeholder exactly as `channels` is exported today. `channel_defaults` is not exported. The `channel_options` export drops its `WHERE` clause (all rows).
- Restore adds `profile_channel_overrides` and `channels_legacy` to `restoreOrder` after `channels`. In `executeSqlFile()`, a file named `channels.sql` (only backups from schema 12–19 have one) is inserted into `channels_legacy`: the existence check, the `PRAGMA table_info` column filter and the `INSERT` all use `channels_legacy`.
- If `validateRestore()` or the manifest compares per-table row counts, apply the same redirect there.
- Conversion needs no new call: `RestoreDialog` is followed by `Profiles::Scan()`.

- [ ] **Step 1: Write the failing test** (suite `ChannelBackupTests`, same fixture, profile `ChannelBackupUnitTest`):

```cpp
void ChannelBackupTests::testOverridesSurviveBackupAndRestore() {
    schema::channel[CPAP_Obstructive].setLabel("Zz Backup Label Zz");
    p_profile->saveChannels();

    ProfileBackup backup(m_profileId);
    backup.setOutputPath(m_tempDir->path());
    backup.setFilename("channelbackup");
    QVERIFY2(backup.createBackup(), "createBackup failed");
    const QString package = QDir(m_tempDir->path()).entryList({ "channelbackup*" }).value(0);
    QVERIFY(!package.isEmpty());

    ProfileRestore restore(m_tempDir->filePath(package));
    restore.setNewUsername("ChannelBackupRestored");
    QVERIFY(restore.validatePackage());
    QVERIFY(restore.checkCompatibility());
    QVERIFY2(restore.restoreProfile(), "restoreProfile failed");
    Profiles::Scan();

    const qint64 restoredId = ProfileRepository().findByUsername("ChannelBackupRestored").id;
    QVERIFY(restoredId > 0);
    const auto rows = ChannelRepository().findOverrides(restoredId);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].channelId, ChannelID(CPAP_Obstructive));
    QCOMPARE(rows[0].values.label.value(), QString("Zz Backup Label Zz"));
}
```

If `createBackup()` or `restoreProfile()` cannot run in the headless test build, stop and report what failed; do not drop the test silently.

- [ ] **Step 2: Build and run.** Expected: non-zero exit code; the restored profile has no override rows.
- [ ] **Step 3: Change the backup export.**
- [ ] **Step 4: Change `restoreOrder` and add the `channels.sql` redirect.**
- [ ] **Step 5: Run `.\test.exe`.** Expected: exit code 0.
- [ ] **Step 6: Commit** (ask first): "Back up and restore channel overrides; accept older channel backups".

Restoring a real pre-v20 backup is verified by hand in Task 8.

---

### Task 7: Preferences shows customized channels and resets one channel

**Files:**
- Modify: `oscar/preferencesdialog.h`, `oscar/preferencesdialog.cpp` (`InitChanInfo()`, `InitWaveInfo()`, `MySortFilterProxyModel`), `oscar/preferencesdialog.ui`

**Interfaces:**
- Consumes: `ChannelOverrides::diffFrom` and `schema::Channel::defaults()` (Task 1).
- Produces: nothing used by other tasks.

Behaviour, the same on the Events tab (`chanView`) and the Waveforms tab (`waveView`):
- **Marker.** A row is customized when `diffFrom(id, type, chan.defaults(), <the row's current cell values>)` is not empty. It is recomputed on the model's `itemChanged`, so it follows edits made in the dialog before OK. A customized row shows a small filled dot as the name item's `Qt::DecorationRole`. Each overridden text cell (name, label, details, lower and upper threshold) is bold. Use the font, not a colour or a style sheet. An overridden cell's tooltip gains a second line: `tr("Default: %1").arg(<default value>)`.
- **Reset Channel.** New buttons `resetOneChannel` (Events) and `resetOneWaveform` (Waveforms), text `tr("Reset Channel")`, placed left of the existing Reset Defaults buttons. Each is enabled only while the selected row is customized. Clicking sets that row's cells to the channel's defaults: checked state, name, colour, overview, label, details, and on Waveforms the two thresholds unless `overridable()` is false for them. The flag-type column is not touched. The button shows its pressed state and the marker clears at once. The same action is on the row's context menu.
- **Filter.** New check boxes `chanCustomizedOnly` and `waveCustomizedOnly`, text `tr("Show customized only")`, right of each search box. `MySortFilterProxyModel` gains `void setCustomizedOnly(bool)`; when set, `filterAcceptsRow` also requires the row's name item to carry the customized flag (store it in `Qt::UserRole + 1`). Group header rows stay visible while any child is accepted.
- Like every other edit on these tabs, a reset is applied by OK (`saveChanInfo()` / `saveWaveInfo()`) and discarded by Cancel. The existing Reset Defaults buttons are unchanged.

There is no widget test harness (the test build removes `gui`), so the row logic rests on `diffFrom`, tested in Task 1, and this task is verified by hand.

- [ ] **Step 1: Add the widgets to `preferencesdialog.ui`** and the proxy-model method.
- [ ] **Step 2: Implement the marker, tooltip, Reset Channel and filter** for both tabs with one shared helper each; do not duplicate the logic per tab.
- [ ] **Step 3: Build OSCAR** (build-oscar skill). Expected: clean build.
- [ ] **Step 4: Verify by hand on a test profile**, in light and in dark mode:
  - Rename one event's label and change another's colour; both rows gain a dot, the label is bold, its tooltip shows the default.
  - Tick "Show customized only": only those rows and their group headers remain.
  - Select the renamed row, click Reset Channel: label returns to the default, dot and bold disappear, the button disables.
  - Click Cancel, reopen: the rename is still there. Repeat, click OK, restart OSCAR: the rename is gone and the colour change remains.
  - Repeat the rename and reset on the Waveforms tab, including an upper threshold.
- [ ] **Step 5: Commit** (ask first): "Mark customized channels in Preferences and reset a single channel".

New `tr()` strings are picked up by the developer's next `lupdate` batch; do not run it.

---

### Task 8: Documentation and end-to-end verification

**Files:**
- Modify: `Notes/Database/DATABASE_SCHEMA_REFERENCE.md` (version table, sections 13 and "channels", index list, relationships)
- Modify: `UserResources/Data Access/OSCAR Data - Python Library for CPAP Data Access.md` only if it describes the `channels` table
- Modify: `Notes/Process/BUG_FIXES.md`
- Modify: `Notes/Design/2026-11-24-channel-defaults-and-overrides-design.md` (status line)

- [ ] **Step 1: Before switching builds, with the current `master` build,** make a backup of one profile from a copy of the test data folder. Keep the package for Step 5.
- [ ] **Step 2: Upgrade a copy.** Start the branch build on that copy and accept the schema upgrade. Expected: no dialog other than the upgrade prompt.
- [ ] **Step 3: Check the result with SQL** (read-only, OSCAR closed):

```sql
SELECT COUNT(*) FROM channels_legacy;                                  -- 0, or only rows for unknown channels
SELECT profile_id, COUNT(*) FROM profile_channel_overrides GROUP BY 1;
SELECT profile_id, channel_code, fullname, label, description
  FROM profile_channel_overrides
 WHERE fullname IS NOT NULL OR label IS NOT NULL OR description IS NOT NULL;
```

Show the third result to the developer. Every row must be a name the developer recognizes as their own rename; any row that is an old OSCAR default is a gap in the Task 4 list and must be fixed there before release.

- [ ] **Step 4: Run the shipped scripts unchanged against the upgraded copy:** `Notes/Database/QUERY_RECENT_SESSION_SETTINGS.sql`, the `channels` query in `Notes/Database/USEFUL_QUERIES.sql` (line 305), and `UserResources/Data Access/oscar_waveform_demo.py`. Expected: each runs without error and shows channel names, not codes.
- [ ] **Step 5: Restore the Step 1 backup** into the upgraded copy under a new name. Expected: it restores; `channels_legacy` is empty again afterwards; a customization known to be in that profile is present.
- [ ] **Step 6: Language switch.** Rename one event label, switch OSCAR to another language, restart. Expected: other channel names are in the new language and the renamed label is unchanged. Switch back.
- [ ] **Step 7: Update the schema reference**: v20 row in the version table; `channel_defaults`, `profile_channel_overrides`, `channels_legacy`; `channels` documented as a view with `is_customized`, and the note that the four preference-driven thresholds show the code default. Tell the developer the ER diagram (`Notes/Database/Database-ER-Diagram.png`) needs regenerating; do not edit the image.
- [ ] **Step 8: Log the fix** in `Notes/Process/BUG_FIXES.md` (#418), and change the design document's status line to "implemented".
- [ ] **Step 9: Tell the developer what is user-visible** for the release notes (the developer writes them): corrected channel names and translations now reach existing profiles; renames survive language changes; Preferences marks customized channels and can reset one channel.
- [ ] **Step 10: Commit** (ask first) with `Closes #418` in the body. After the push, ask whether to add the `fixed` label and the internal completion note, and whether to file the separate issue for the channel-type change that is saved but never loaded.
