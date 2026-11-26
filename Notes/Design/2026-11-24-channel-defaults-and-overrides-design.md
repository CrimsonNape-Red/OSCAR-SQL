# Channel defaults and per-profile overrides (GitLab #418)

Status: implemented on branch `channel-overrides` (2026-11-25); pending the developer's hands-on verification and merge.

## Why

Every profile stores a full copy of every channel's names, colours, thresholds and flags
(about 307 rows per profile). On open, the stored copy replaces the program's values
(`Profile::loadChannelsFromDatabase()`, `oscar/SleepLib/profiles.cpp`). The copy does not
record which values the user changed, so a corrected name, a corrected translation or a
changed default colour never reaches an existing profile. A language switch works around
it for names by discarding the stored names, which also discards the user's own renames.

Storage is not the motive: the table is about 43 KB per profile. The motive is one source
of truth for defaults, and a store that holds only what the user chose.

## Goals

- A correction made in code or in a translation reaches every profile at the next start.
- A value the user changed survives updates and language switches.
- Existing profiles migrate with nothing lost and nothing asked.
- SQL scripts that read `channels` today keep working unchanged.
- Backups made by older versions still restore.

## Non-goals

- Colours per theme. One colour per channel stays; dark mode keeps converting it at draw
  time (`GraphTheme::ink()` / `fill()`, `oscar/Graphs/graphtheme.cpp`).
- The Device Settings Changes cache (#258). Separate store, separate fix.
- Making a channel type change in Preferences › Events persist. Today it is saved and never
  loaded back (read from code, not tested). To be filed as its own issue; this design keeps
  the current behaviour.
- Editing option labels (mode names). They stay code-defined (#257, #333).

## Anti-goals

- Silently dropping a real user rename.
- Asking the user anything during the upgrade.
- Leaving the corrections already made (#417) stuck in existing profiles.

## Constraints

- Schema policy: every schema change ships a migration; no data loss. Schema upgrades are
  user-gated (#293). Current schema is v19; this is v20.
- Restore accepts backups from schema 12 up to current (`MIN_RESTORE_SCHEMA_VERSION`).
- The SQL migration runs before the channel registry and translations exist, so it cannot
  itself compare stored values with code defaults.
- C++17, Qt 6, cross-platform. `Notes/` is published: no identifiers.

## Approaches considered

1. **Treat a version change like a language change.** Discard stored names at every update.
   Rejected: destroys renames at every update, including nightly builds.
2. **Keep the per-profile table, add `user_fullname` / `user_label` / `user_description`.**
   Small migration, no script impact. Rejected by the developer as a half measure: colours,
   thresholds and flags would stay stale, and the per-profile duplication would remain.
3. **Global defaults plus per-profile overrides (chosen).** Fixes the whole class of
   problem. Costs a real migration and work in backup/restore.

## Design

### Tables

- **`channel_defaults`** (new, global): one row per channel, primary key `channel_id`.
  Holds `channel_code`, `type`, and the code defaults: `enabled`, `default_color`,
  `fullname`, `label`, `description`, both thresholds and their colours,
  `show_in_overview`. Names are in the current OSCAR language. OSCAR rewrites any row that
  differs from the registry after the registry is built (schema init plus every loader's
  `initChannels()`), so it is always current for the running build and language.
- **`profile_channel_overrides`** (new, per profile): `profile_id`, `channel_id`,
  `channel_code`, and one nullable column per overridable field: `enabled`,
  `default_color`, `fullname`, `label`, `description`, the two thresholds and
  `show_in_overview`. NULL means "use the default". A row exists only if at least one field
  is set. `UNIQUE(profile_id, channel_id)`, cascade delete with the profile.
  `channel_code` keeps today's fallback: if a channel's id is unknown to the running build,
  the row is matched by code instead (`profiles.cpp:2776-2782`). The threshold colours have
  no column: nothing in the UI can change them, so they always come from code.
- **`channels`** becomes a **view** over profiles × `channel_defaults` left-joined to the
  overrides, returning `COALESCE(override, default)` under today's column names, including
  `profile_id`. Existing scripts (`Notes/Database/*.sql`, `UserResources/Data Access/*.py`,
  users' own) keep working unchanged. Builder's call whether to add a column that tells
  scripts a field is customised. *(My call, not discussed with the developer: making
  `channels` the view rather than adding a differently named view. It removes all script
  breakage. OSCAR itself never writes through the view.)*
- **`channels_legacy`** (new, transitional): the old per-profile rows, held until each
  profile is converted (below).
- `channel_options` is unchanged.

### Load and save

- `schema::Channel` keeps a snapshot of its code defaults, taken after the registry is
  built and again after `schema::resetChannels()`.
- Load applies only the profile's non-NULL override fields over the registry.
- Save writes, per channel, only the fields that differ from the snapshot; a channel with
  no differences has no row. An override that has become equal to the default is dropped.
- Both follow the rule in "What can be an override" below.
- A language switch changes the defaults only. Overrides always apply, in every language
  (developer's decision). The `changing_language` logic and its use of the profile's
  `Language` preference go away for channels.
- Reset Defaults keeps its current behaviour. There are two buttons, one per Preferences
  tab, each resetting only its own tab's channels in memory
  (`preferencesdialog.cpp:1385-1392`, `1533-1540`); nothing is written until the profile is
  saved. The save rule above then removes the override rows that no longer differ.

### What can be an override

A field is an override candidate only where the UI can change that field on that kind of
channel. This rule applies to saving and to the conversion of old rows.

- **DATA and SETTING channels: nothing.** No editor reaches them (Preferences › Events
  skips DATA, SETTING and WAVEFORM; Preferences › Waveforms lists WAVEFORM only; the Daily
  events selector covers span and flag channels only). They never get an override row.
- **Thresholds: WAVEFORM channels only**, the only place they can be edited.
- **Thresholds driven by a preference are never overrides:** the upper threshold of
  `CPAP_Leak` (leak redline preference, `daily.cpp:619-621`) and the thresholds of
  `OXI_Pulse` and `OXI_SPO2` (oximetry preferences, `Profile::resetOxiChannelPref()`).
  The preference is the source of truth and is re-applied after every load. Consequence:
  for these four values the `channels` view shows the code default, where scripts today see
  the preference value; the real value is in `profile_preferences`. *(My call.)*

### Migration v19 → v20 (SQL step)

Rename the `channels` table to `channels_legacy`, create the two new tables and the view,
rebuild indexes. No values are interpreted here.

### Conversion of old rows (once the channel registry is built)

The conversion needs only the registry and the current translation, both of which are
global, so it does not wait for a profile to be opened. It runs for **every** profile that
has rows in `channels_legacy`: at the first start after the upgrade, and immediately after
a restore or a 1.x import. This matters because Backup and Share can act on profiles that
are not open; a profile left unconverted would be backed up without its customisations.
Converted rows are deleted, so their presence is the one-time gate. For each stored field:

| Stored value | Result |
|---|---|
| Field that cannot be an override for this channel (previous section) | Not an override |
| Equals today's default | Not an override |
| A name that OSCAR has shipped before (see next section) | Not an override; the user gets the current name |
| Any other name | Kept as an override: a real rename |
| Colour, threshold, enabled or overview flag that differs | Kept as an override |
| `type` | Ignored (see non-goals) |
| Row whose channel is unknown by id and by code | Left in `channels_legacy` and tried again at later starts. Today such a row is skipped at load but kept in the table, so this preserves it |

No dialog (developer's decision). The same conversion is used for:

- restoring a backup made at schema 12–19: restore loads its `channels` rows into
  `channels_legacy` for the new profile, then converts them;
- importing an OSCAR 1.x profile's `channels.dat` (`migrateChannelsToDatabase()`,
  `profileimporter.cpp`): its values go through the same classification instead of being
  saved wholesale.

Known limit: a default colour or threshold that changed in code between the saving build
and this one is indistinguishable from a user's choice and is kept as an override. Such
changes have been rare; Preferences can reset it.

### The list of names OSCAR has shipped

A developer tool, `Tools/gen_shipped_channel_names.py`, builds the list from the git
history. Its source strings are the current channel defaults (full names, labels and
descriptions, in English) plus every `tr()` literal the channel-defining files
(`schema.cpp`, `common.cpp`, the loader plugins) have ever held, at every location those
files have had, including literals still present. Its translations are every translation
any `Translations/*.ts` file has ever paired with one of those source strings. The result
is bound into the application as a resource.

- It is frozen at the v20 release: `channels_legacy`, old backups and `channels.dat` can
  only hold names written by pre-v20 builds, so it never needs regenerating afterwards.
- As built: 24,914 lines, about 0.9 MB (UTF-8, one name per line), matched in any
  language. Before converting anything OSCAR checks that the loaded list holds at least
  20,000 names and four fixed sentinels (English and Chinese); if not, nothing is converted
  and the old rows stay parked for a later start.
- To cover test builds between releases, the tool should scan every commit that touched
  the translation files or the channel definitions, not only tags.
- Consequences of a miss: a name absent from the list is kept as an override (stale but
  visible and resettable). A user rename that happens to equal a shipped name is treated
  as an old default. Both are accepted.

### Backup and restore

- Backup exports `profile_channel_overrides` for the profile. `channel_defaults` is not
  exported; it is rebuilt from code. The `channel_options` export currently selects through
  the per-profile `channels` table and needs a new selection (simplest: all rows).
- Restore of a v20 backup inserts overrides directly. Restore of an older backup goes
  through `channels_legacy` as above.

### Preferences (separable; accepted by the developer 2026-11-25)

With the upgrade dialog gone, this is how a user notices and clears a stale or unwanted
override. It applies to the Events and Waveforms tabs, whose layout is otherwise unchanged,
and can ship after the storage change.

- **Marker:** a small dot before the channel name on any row with at least one override
  (this covers colour and checkbox changes). Overridden text cells (name, label, details,
  thresholds) are bold; bold rather than a colour so it reads the same in light and dark
  mode. The tooltip of an overridden cell shows the default value.
- **Reset Channel** button beside the existing Reset Defaults button, enabled only when the
  selected row has an override; it resets that one channel. The same action is on the
  row's right-click menu.
- **Show customised only** checkbox beside the search box.
- A reset shows at once and, like every other edit on these tabs, is saved with OK and
  discarded with Cancel.
- Not included: resetting a single field of a channel.

### Dark mode

No interaction with storage. Colours, default or user-chosen, are stored once as picked and
translated at draw time. Two existing behaviours stay as they are: the Preferences swatch
shows the stored colour rather than its dark-mode rendering, and a pale colour picked while
in dark mode is not darkened for light mode.

## Left to the builder

- Column types and index names; the exact view SQL; whether the view synthesises an `id`.
- Where the defaults snapshot lives and when `channel_defaults` is refreshed.
- The tool's implementation and the resource format.
- How restore redirects an old backup's `channels` rows.
- Tolerance when comparing thresholds; colour comparison by `#RRGGBB` string.

## Verification

- Unit tests for the field classifier (each row of the table above).
- v19 → v20 on a copy of a multi-profile database: unrenamed profiles end with no name
  overrides; a deliberate rename, colour and threshold change survive.
- A name corrected in code or translation appears in an existing profile at next start.
- Language switch: defaults change, a rename stays.
- Reset Defaults clears overrides.
- Backup and restore at v20; restore of a pre-v20 backup; 1.x profile import.
- The shipped SQL and Python scripts run unchanged against the view.
- Update `Notes/Database/DATABASE_SCHEMA_REFERENCE.md` and the ER diagram.
