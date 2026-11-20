# Backups for ring-buffer cards (night archive): design

**Date:** 2026-11-19
**Status:** approved 2026-11-19, with the three proposals in section 9 accepted. Implemented
2026-11-20 on a feature branch for the two BMC loaders; see section 12, "As built", for where
the code differs from the sections before it.
**Plan:** `2026-11-19-ring-buffer-backup-archive-plan.md`
**Tracking:** GitLab #380
**First use:** the two BMC loaders (`bmc_loader.cpp`, `bmcg3x_loader.cpp`)

## 1. The problem

Some devices store detailed data in a fixed amount of card space and overwrite the oldest
data when it is full. For these cards a backup that mirrors the card has two faults:

- **(a) Cost.** Both BMC loaders delete the backup folder and copy the whole card on every
  import (`BmcLoader::Open()`, `BmcG3xLoader::Open()`). A full legacy card is about 480 MB
  and a full G3X card about 4 GB, so the copy dominates import time even when one night is
  new. This is what the reporter of #380 saw.
- **(b) Coverage.** The backup is only the last card image. "Rebuild from backup" can
  restore the nights still inside the ring (about two months of waveforms) and nothing
  older. Every earlier night that OSCAR imported with full waveforms comes back as
  events-only, or not at all. ResMed does not have this fault because its backup
  accumulates per-day files.

The wipe-and-replace was kept deliberately in #381: the ring files have fixed names, so
merging an old backup with a new card file-by-file would mix inconsistent contents. Any
fix therefore has to change what is stored, not just how it is copied.

## 2. Goals

- Import copies only what is new. Cost is proportional to the new nights, not the card.
- Rebuild from backup restores every night ever captured, with the same detail as when it
  was first imported, however many times the ring has wrapped since.
- One pattern that other ring-buffer loaders can adopt. The code that enumerates records
  is loader-specific; the storage layout, rules and helper are shared.
- The raw bytes are kept unmodified, so a later parser improvement applies to old nights
  on rebuild.

**Non-goals:** changing what is parsed or imported; a loader data-version bump (none is
needed); changing purge behaviour (purge does not touch backups today and still will
not); a retention limit; converting loaders other than the two BMC ones in this work.

**Would count as failure:** a night present in the old backup being lost in migration; an
archive that depends on card offsets staying meaningful after a wrap, reformat or clock
change; a rebuild that silently imports fewer nights than the archive holds.

## 3. How the BMC cards behave (from the code, notes and sample cards)

| | Legacy BMC | BMC G3X |
|---|---|---|
| Ring files | `.000`–`.029`, 16 MiB each on every full sample card; no geometry header is read, the wrap is found when the next file does not exist | `.000`–`.062`, 64 MiB each; file count read from the IDX header |
| Packet | 256 bytes per second | 2048 bytes per second |
| One 8 h night | about 7.4 MB | about 59 MB |
| Ring holds | about 65 nights | about 68 nights |
| Index files | `.USR` (sessions, events), `.idx` (512-byte entries with a start pointer into the ring) | `.idx` (2048-byte day records with waveform/event/log byte ranges), `.evt`, `.log`, `.set` |
| What the parser reads for a night | from the IDX start pointer (or a crumb), sequential packets whose timestamp is inside the USR session window, across files and across the wrap (`ReadWaveforms()`) | the byte range `[WaveStartOffset, WaveEndOffset)` unwrapped modulo the ring, trimmed to the surviving tail if partly overwritten (`ParseIdxRecords()`, `WaveformFileFor()`) |

Observed, not guaranteed by any specification:

- The index files only grow. On every wrapped sample card the IDX holds far more entries
  than the ring holds nights (the events-only import path exists for exactly those). No
  card has been seen where an index file was truncated or restarted.
- Ring files are created as the ring first fills, so a young card has fewer and the last
  one is short.
- Card file times are not trustworthy (one sample card is stamped 2097), so "changed
  since last import" cannot be decided from modification times.

## 4. Approaches considered

**A. Night archive (chosen).** Stop storing ring files. For each night, store the exact
packets the parser reads for that night, in order, as one file that is written once and
never overwritten. Keep the small index files as they are on the card. A manifest lists
the nights held. Nothing in the archive refers to a card offset, so nothing wraps.

**B. Unwrapped address space.** Keep the card's file naming but let the file number grow
without limit (`.063`, `.064`, …) and adjust the pointers, as suggested in #380. This
keeps the reading code closest to today's, but: G3X IDX offsets are 32-bit and the ring
already uses almost all of that range, so the adjusted pointers cannot live in the IDX
and need a side table anyway; the mapping from card offset to archive offset breaks when
a card is reformatted, replaced, or skipped for longer than the ring; and nights lost
between imports leave holes that the fixed-size files must still account for. Once the
side table exists this is approach A with the nights concatenated, which makes a single
night harder to verify, replace, compress or remove.

**C. Incremental mirror.** Keep mirroring the card but copy only ring files that changed.
Fixes (a) only, and has to detect change by content because file times are unreliable.
Rebuild coverage stays at one ring. Rejected as the answer, kept as an option in §9.

**D. Dated card snapshots.** Keep a full copy whenever the ring is about to overwrite
uncaptured data. Storage is several times A's and rebuild must reconcile overlapping
snapshots. Rejected.

A is chosen because the unit it stores is the unit the parser already works in, which
makes the archive independent of ring geometry and makes the same rules usable by other
loaders.

## 5. The pattern

Each loader sorts the card's files into three classes:

| Class | Meaning | Archive handling |
|---|---|---|
| **Index** | Small files that describe all nights and only grow | Latest copy kept verbatim, replaced only under the guard in §5.3 |
| **Ring** | Fixed-size storage that is overwritten | Never copied as files. Each record is extracted to its own segment |
| **Other** | Anything else on the card (firmware language packs, files OSCAR does not read) | Copied when absent or different in size; never deleted |

### 5.1 Layout in the device's Backup folder

```
Backup/
  archive.json                 manifest
  <base>.idx  <base>.USR  <base>.evt  <base>.log  <base>.set    index files, as on card
  Segments/
    <base>_20261114.seg.gz     one night's packets, wrap removed
    <base>_20261115.seg.gz
    <base>_20261115_2.seg.gz   second record with the same date (rare)
  Generations/
    20270301/                  an index set that a later card no longer covers (rare)
  <other card files>
```

A segment is nothing but the night's packets, back to back, in recording order. It has no
header, so it can be opened with the existing format grammars and so the manifest can be
rebuilt from the segments if it is lost. `<base>` is the card's own file base name.

### 5.2 Manifest

`archive.json`: a format version, the loader name, and one entry per segment:

| Field | Purpose |
|---|---|
| `key` | The loader's identity for the record: the IDX/USR date. Two captures with the same date are the same record when their time spans overlap (a late capture may hold only the tail); if they do not overlap they are separate records and the second gets `_2` |
| `file` | Segment file name |
| `bytes`, `packets` | Uncompressed size and packet count |
| `first`, `last` | First and last packet timestamps |
| `final` | False while this was the newest night on the card when captured (it may still grow) |
| `complete` | False when the capture is known to be only the tail of the night, because the ring had already overwritten its start |
| `rule` | The loader's capture-rule version when the segment was taken (rule 10) |
| `sha256` | Of the uncompressed content, for verification |

The manifest also records the first and last date covered by the index set it holds.

### 5.3 Rules

1. **Capture uses the parser's own range.** A segment is whatever the parser would read
   for that record on this card. There is no second implementation of "where does this
   night live", so capture and import cannot disagree.
2. **Segments are write-once.** A `final` segment is never replaced by a shorter one. A
   longer capture of the same record (the earlier one was taken while the night was in
   progress, or from a card where it was already partly overwritten) replaces it.
3. **The newest night is always re-captured**, because it may have been in progress. This
   matches the loaders, which already re-import the last imported day.
4. **Skip without reading.** A record is skipped when the manifest's entry for it (rule
   in §5.2) is `final` and `complete`. Otherwise it is skipped when the entry is `final`
   and has at least as many bytes as the card offers; a loader that cannot tell the size
   without reading (legacy BMC) reads the record and the archive keeps the longer one.
   Only tail captures and the newest night are ever read twice.
5. **Index guard.** The index files are handled as one set. Compare the date range the
   card's set covers with the range of the set in the archive. If the card covers all of
   it, replace the set. If the card's range lies inside it (an older copy of the same
   card), keep the archive's set. Otherwise (the device restarted the files, the card was
   reformatted, or it is a different card) move the archive's set to
   `Generations/<yyyyMMdd>/` and install the card's; rebuild reads every generation. This
   turns the "index files only grow" observation into something checked.
6. **Crash safety.** Write a segment to a temporary name, rename it, then rewrite the
   manifest the same way. A segment without a manifest entry is adopted on the next run;
   an entry without a file is dropped.
7. **The archive is a union.** Importing an older card copy after a newer one adds
   whatever nights are missing. Order does not matter.
8. **Cancel and progress.** Capture reports per-night progress and checks for
   cancellation between blocks; a cancelled segment's temporary file is removed.
9. **Preference.** With "Create SD Card Backups during Import" off, nothing is written
   and an existing archive is left alone, as now.
10. **Capture-rule version.** Rule 1 means a fault in the parser's range logic (which
    packets belong to a night) is copied into the segment, and rule 4 would then never
    look at that night again. Each loader therefore has a capture-rule number, stored
    with every segment, that it raises when that logic changes. A segment taken under an
    older number is offered again while the card still holds the night, and is replaced
    by a new capture that is complete or longer. A new capture that is only a shorter
    tail does not replace it. This is not the loader's data version and forces no rebuild.

### 5.4 Shared helper

A new `SleepLib/card_archive.{h,cpp}` (`CardArchive`) owns everything that is not
format-specific: manifest load/save, key lookup, the skip and replace rules, atomic
segment writes, compression, index-file mirroring with the guard, copying "other" files,
and progress/cancel hooks. It has no knowledge of any card format and does not use the
profile, so it can be unit tested with synthetic records.

Each loader supplies, per record: the key, whether it is final, the size the card offers,
and a function that streams the record's bytes. On the read side the loader asks the
archive for a record's segment and gets a readable device or nothing.

## 6. Applying it to the BMC loaders

**Capture** (replaces the wipe-and-copy block in both `Open()` functions): after the
parser has read the index, walk every record the card describes, not only the ones being
imported, and offer each to the archive.

- *G3X:* the record is the IDX day record; the bytes are `[WaveStartOffset,
  WaveEndOffset)` after the existing unwrap and surviving-tail adjustment. A record with
  no surviving waveform produces no segment.
- *Legacy:* the record is the USR session with its linked IDX entry or crumb; the bytes
  are the packets `ReadWaveforms()` accepts for that session. The IDX entry's "next"
  pointer may give the end directly, but its meaning has not been verified against the
  sample cards; until it is, the existing timestamp scan defines the range.

**Reading in archive mode** (rebuild, or any import whose folder holds `archive.json`):
the parser builds its day list from the index files exactly as now. Waveforms then come
from the record's segment, read from offset 0 to the end; a record with no segment takes
the existing events-only path. The seams are narrow: in G3X every waveform read goes
through `WaveformFileFor()` and `ReadWaveformPacketTimestamp()`; in legacy through the
link's crumb (`Filepath`, `ByteOffset`) and the next-file step in `ReadWaveforms()`,
which must end at the segment's end instead of moving to another file.

**Detection:** the legacy `DirectoryHasBmcData()` requires a `.000` file and must also
accept a folder with `archive.json`. G3X already accepts a folder with only `.evt`.
`MainWindow::rebuildCPAPMachine()` calls `Detect()` on the backup folder before purging,
so a missed case would be reported rather than lose data.

**Migration:** an existing backup is a full card mirror. On the first import after the
change, capture runs once against that mirror, then against the card, and only after
both succeed are the mirror's ring files deleted. The mirror can hold nights the card has
since overwritten, so the order matters. This one import costs about what every import
costs today.

**Rebuild safeguards.** The archive can only restore what was captured. Two cases must
not pass silently:

- *Before a rebuild.* Nights whose waveforms were overwritten on the card before the
  archive existed are in OSCAR's database and nowhere else, and a rebuild purges the
  database. The rebuild confirmation (and the upgrade prompt in
  `Profile::DataFormatError()`) therefore states how many nights now have detailed graphs
  that the backup cannot restore, with the first and last date. While the Backup folder
  is still an old card mirror the count is not known cheaply, so a fixed caution is shown
  instead.
- *After a rebuild.* Nights whose segment failed its checksum, and segments that no index
  record claimed, are counted and reported to the user when the import ends, not only
  logged. An unreadable segment is then removed from the device's own archive so the next
  card import can store that night again if the card still holds it.

**Code outside the loaders:** one new `MachineLoader` virtual, `rebuildLossWarning()`,
whose default returns nothing, and its two call sites in the rebuild and upgrade prompts.
`Machine`, purge and profile backup keep treating the Backup folder as opaque.

## 7. Storage

| | Raw per year of 8 h nights | Compressed (gzip) |
|---|---|---|
| Legacy BMC | about 2.7 GB | about 1.0 GB |
| BMC G3X | about 21.5 GB | about 5.5–7.3 GB |

Compression ratios are from one ring file per platform (0.38 legacy; 0.26 and 0.34 for
two G3X-family cards), so treat them as indicative. Today's backup is capped at one card
(0.48 GB / 4 GB); the archive grows without limit, as ResMed's does, but G3X data is far
denser than ResMed's.

Decided: always gzip segments, without consulting the "Compress ResMed (EDF) backups"
preference (it is labelled ResMed-only and is off by default, and uncompressed G3X
segments have no user who benefits). A night is decompressed whole into memory when read.

## 8. Other ring-buffer loaders

Not designed here. From their code comments these are candidates, each to be confirmed
when its turn comes:

| Loader | Ring | Likely record |
|---|---|---|
| SEFAM | session folders whose number wraps; a session-archive ring inside the memory image | a session folder, keyed by its recorded start time; an archive block |
| Apex | fixed-size per-minute region and a fixed-length session table in one file | a session's per-minute run |
| Yuwell | numbered session files used as ring slots | a session file |
| IntelliPAP DV6 | data files read from a wrap point | a day's records |

SEFAM and Apex mix index and ring data in one file, so their "index" class may be empty
and everything is extracted as records. The helper allows that.

## 9. Decisions (made 2026-11-19)

1. **Compression:** segments are always gzipped; the existing preference is not consulted.
2. **Vendor-readable image:** not kept. The archive is no longer a card image, so vendor
   software cannot open the Backup folder. The alternative (also keeping an incrementally
   updated mirror of the current ring, approach C) was declined.
3. **Growth:** no retention limit in this work. The existing backup-size display will
   show the growth.

## 10. Verification

- Unit tests for `CardArchive` with synthetic records: skip, replace-if-longer, never
  shorten a final segment, crash between segment and manifest, index guard, union in
  either order.
- G3X: the sample collection has three card images from one device at increasing fill
  levels. Import them oldest to newest into a scratch profile, rebuild, and require the
  same sessions and waveform spans as the three direct imports combined. Repeat newest to
  oldest.
- Legacy: no such series exists. Derive an earlier card state from a wrapped sample (or
  obtain a second image) before relying on the legacy path.
- Migration: start from a backup made by the current build, import, and confirm no night
  in the old mirror is missing from the archive before the ring files are removed.
- Timing: second import of an unchanged card should spend no measurable time in backup.
- Safeguards: a rebuild of a device that has waveform nights older than its archive shows
  the count before purging; a rebuild with one deliberately damaged segment names that
  night afterwards, and the next card import restores it.

## 11. Left to the implementer

The exact `CardArchive` interface; whether segments are streamed or built in memory during
capture; how the parsers carry "this record's waveform source" (a per-record device is
suggested); wording of progress messages; and whether capture of already-imported nights
runs before or after the import tasks (after keeps the first screen update fast).

## 12. As built (2026-11-20)

Implemented on a feature branch; not yet merged. The sections above are the design as
approved. Where the code differs, this section is what is true.

### 12.1 Where things are

| Piece | Location |
|---|---|
| Archive layout and rules | `SleepLib/card_archive.{h,cpp}` (`CardArchive`) |
| Parser side | `BmcDataParser` interface in `bmcDataParsing.h`; `BmcData` and `BmcG3xData` |
| Capture, conversion, read check, prompt wording | `SleepLib/loader_plugins/bmc_archive.{h,cpp}` |
| Import flow | `BmcLoader::importFromFolder()`, shared by both loaders; `createParser()` is the only per-loader difference |
| Rebuild prompts | `MachineLoader::rebuildLossWarning()` (default: nothing), called from `MainWindow::rebuildCPAPMachine()` and `Profile::DataFormatError()` |
| Tests | `tests/cardarchivetests.cpp`, `tests/bmcarchivetests.cpp` (synthetic cards built in a temporary folder) |

### 12.2 Differences from the design

- **Unchanged content is not rewritten.** When the archive already holds exactly the bytes
  on offer (same size and SHA-256), nothing is written; the entry only gains standing:
  final, complete, and the newer capture rule. This is what makes an import of an
  unchanged card cost seconds, since the newest night is always offered again.
- **What marks an unconverted backup is ring files without the manifest's `converted`
  flag.** Such a folder is a full card copy whose conversion has not finished, and is read
  as a card. On import its conversion is finished first; when every one of its nights is
  stored the flag is written, then the ring files are deleted, and only then is the card
  being imported captured. From the flag on the folder is an archive, so a deletion that
  is cut short cannot make a rebuild read a card copy with holes in it; the leftovers go
  on the next import. (The design deleted the ring files after both steps and keyed the
  conversion on the manifest's absence; a conversion cancelled part-way was then never
  resumed, and a rebuild in that state would have read a half-filled archive.) While a
  conversion cannot complete, new nights are not backed up; this is logged.
- **A replaced segment is written beside the old one.** The new data goes into the next
  free file name, the manifest is saved, and only then is the old file removed, so the
  manifest's entry and the file it names always agree. A night's file name can therefore
  end in `_2` without there being a second record for that date.
- **The conversion checks its work before deleting.** Every segment is read back and
  verified before the old copy's ring files go. One that does not read is set aside, the
  ring files stay, and the next import takes that night from them again.
- **An old copy that cannot be read as a card** (ring files left by a copy that was cut
  short) cannot be converted. Its ring files are moved to `Backup/Unconverted/`, the user
  is told once, and the backup carries on as a plain archive.
- **Index files of unknown range are retired, not overwritten.** If index files are in the
  folder but the manifest has no record of what they cover, they go to
  `Generations/unknown/` before the card's set is installed.
- **A backup step that fails is shown to the user**, outside batch mode, not only logged:
  the card goes on overwriting its oldest nights, so a backup that keeps failing has to
  be seen. Cancelling does not raise it.
- **A failed read is an error, not an empty night.** A ring file that exists but will not
  open (locked, not downloaded from a synced folder, on a failing disk), or a read that
  comes up short, marks the record `readFailed`. Capture counts it as failed and stores
  nothing for it, so a conversion that could not read the old copy does not delete it.
- **"Complete" is not final proof.** A G3X night is complete only if the ring files hold
  everything the index lists, so a cut-short copy of a card does not pass for the whole
  night; and a card known to offer more bytes than a stored segment is always read, even
  if that segment is marked final and complete. A record dated in the future does not
  make the real newest night final.
- **An archive whose manifest is lost or unreadable is still an archive.** Segments mark
  the folder as one; the manifest is rebuilt from them when the folder is opened (in
  memory only, for an archive that is not the device's own). An archive written by a
  later format is not detected as importable at all, so a rebuild stops at "OSCAR does
  not recognize the backup data" before it purges anything.
- **Cancel** is noticed during a night's read, not only between nights: the abort check
  the readers poll every few megabytes also processes pending events.
- **Legacy BMC reads two nights on an unchanged card, not one:** the oldest surviving
  night, which may be a tail that an older card copy could complete, and the in-progress
  session, whose size is not known without reading. Both are small.
- **Generations.** An index set that a later card no longer covers moves to
  `Generations/<yyyyMMdd>/` (named for the last date it covered), not to dated file
  names. On import from an archive each set has its own parser, and a date is imported
  from the first set that lists it.
- **Damaged segment during an import from the archive.** The night is imported exactly as
  a night whose waveform the ring had overwritten (events only). Parsers never change the
  archive; after the import the loader tells the user and, in the device's own archive,
  moves a damaged segment to `Segments/Damaged/` (nothing is deleted). "Damaged" means the
  file opened and does not hold what its entry describes. A segment that merely could not
  be opened (locked by another program, not downloaded from a synced folder) is reported
  and left exactly where it is, since during a rebuild the archive may be the only copy. Recovery is two steps, and the message says so: import a
  card that still holds the night (this restores the segment), then rebuild again. An
  import only re-reads the newest day, so importing the card by itself does not bring the
  graphs back.
- **Rebuild prompt.** "Has detailed graphs" is a stored flow sample count above zero
  (`Day::count(CPAP_FlowRate) > 0`). The presence of a flow summary row proves nothing:
  an events-only legacy session has one, with a count of zero. The upgrade prompt runs
  before a device's sessions are loaded, so there the text comes from the archive alone
  (the date from which it holds detailed data). The computed warning is also logged.
- **Other card files.** Folders an operating system keeps on a removable drive
  ("System Volume Information", "$RECYCLE.BIN") are not copied.
- **Segment writes** use `QSaveFile` (temporary file renamed over the target) instead of a
  hand-made `.tmp` file.
- **Legacy waveform scan** stops after one lap of the ring, and ignores a trailing
  part-packet. Before, a card whose whole ring is one short recording had no stop
  condition. The scan now decodes only each packet's timestamp and fully decodes the
  packets it accepts.
- **G3X leak-field probe** reads through the same spans as the import, so it stops at the
  end of the night's range and can cross ring files.

### 12.3 Measured on sample cards

All with a MinGW / Qt 6.10.2 build, batch imports into throwaway data folders.

| Check | Result |
|---|---|
| G3X, three images of one device at increasing fill, oldest first | 19, then 15, then 77 nights stored; 111 segments, 1.39 GB for a 3.9 GB card; 244 sessions, all with waveform |
| Same three images, newest first | identical archive (same checksums) and identical sessions |
| Unchanged card imported again | whole run 4 s (G3X), 6 s (legacy); nothing written |
| Fresh data folder importing only the Backup folder | identical sessions (id, start, end, waveform seconds), G3X and legacy |
| Wrapped legacy card | 199 nights indexed, 100 with waveform stored (1 as a tail); 181 MB for a 480 MB card |
| Wrapped G3X card (full ring) | 75 nights stored (1 as a tail); 1.19 GB for a 4.1 GB card |
| Old full-card backup, G3X and legacy | converted in place, ring files removed, result identical to a direct import |
| Real rebuild with one segment damaged | night restored events-only and named in a message; card import then a second rebuild gives sessions identical to the original import |
| "Create SD Card Backups during Import" off | nothing written |

Across these cards the whole Backup folder came to roughly 0.3–0.4 of the card's size.

### 12.4 Not verified

- Cancel during "Backing up new nights..." in the running application. It is covered by
  unit tests only.
- Two images of one legacy device taken at different dates. The sample collection has no
  such pair.
- How the extra paragraph looks in the rebuild dialog, and the upgrade prompt in the
  running application. The wording was read from the log.
- The one-lap bound on the legacy scan against a real card that needs it.
- Read failures (a ring file that will not open, a short read) outside unit tests.
- Two cards used alternately in one device. The index guard compares date ranges, so one
  card's index can replace the other's; the other card's nights are then reported at a
  rebuild as having no matching index record and are not restored, though their segments
  stay on disk. Left as it is by decision (seen once in OSCAR's history).
- macOS and Linux.

### 12.5 When to raise a capture-rule constant

`kBmcLegacyCaptureRule` and `kBmcG3xCaptureRule` are stored with every segment. Raise the
loader's constant in the same change as any fix to which packets belong to a night: the
crumb search, the session window, wrap handling, the IDX range or its unwrapping, the
surviving-tail search. Nights archived under the old number are then read again from any
card that still holds them; a night whose bytes come out the same is not rewritten, only
marked. It is not the loader's data version and forces no rebuild.
