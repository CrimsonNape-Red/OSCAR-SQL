# Night Archive for Ring-Buffer Card Backups: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the BMC loaders' wipe-and-copy card backup with a night archive, so an
import copies only new nights and "rebuild from backup" restores every night ever captured.

**Architecture:** A format-agnostic `CardArchive` class owns the Backup folder layout
(manifest, gzipped per-night segments, index-file sets). Each BMC parser gains a small
archive interface: it can list its records, hand over a record's raw packets, and read a
record's packets from an archive instead of the ring files. One shared capture routine
drives both, and the two `Open()` functions call it.

**Tech Stack:** C++17, Qt 6 (QtCore: `QJsonDocument`, `QSaveFile`, `QCryptographicHash`),
existing `gCompress()`/`gUncompress()` from `SleepLib/common.h`, QtTest.

**Spec:** `Notes/Design/2026-11-19-ring-buffer-backup-archive-design.md` (GitLab #380)

## Global Constraints

- C++17; must compile clean on Qt 6.10.2 and stay compatible back to Qt 6.4. No
  `#if QT_VERSION` guards. No platform-specific APIs.
- New files start with a brief module description and "Copyright (c) 2026 The OSCAR Team",
  are documented in Doxygen style, and are added to `oscar/oscar.pro` (sources, headers,
  and the `test { }` block for tests).
- Scope is the two BMC loaders only. No other loader's behaviour may change. The one
  shared change is a new `MachineLoader` virtual whose default returns an empty string.
  Never touch `oscar/SleepLib/thirdparty`.
- Do not change `bmc_data_version` or any loader version number.
- Segments are always gzipped. The "Compress ResMed (EDF) backups" preference is not read.
- With "Create SD Card Backups during Import" off (`backupCardData()` false) nothing is
  written to the Backup folder and an existing archive is left alone.
- Cancel must take effect within a few seconds: every loop over packets or records checks
  the abort callback at least once per 4 MiB read.
- `Notes/` is published: no serial numbers, contributor names or sample-folder names in
  any Notes file, code comment, test or commit message.
- Commits: ask the user before each commit. One-line subject plus a 2–3 sentence summary
  in lines of at most 90 characters, written to a file and committed with
  `git commit -F`; no Co-Authored-By line. Only the final commit carries `Closes #380`.
- Unit tests: build in a separate folder with `qmake <repo>\oscar\oscar.pro CONFIG+=test`
  then `jom` (MSVC) or `mingw32-make` (MinGW). Run `.\test.exe` (never bare `test.exe`).
  The exit code is the number of failed tests; stdout is not captured reliably, so the
  exit code is the result. Tests reach private members through
  `friend class <TestClass>` under `#ifdef UNITTEST_MODE`.
- Application build: use the `build-oscar` skill.

## Review Focus

1. **Backup folder cannot be written** (disk full, read-only): the import still succeeds,
   the old ring mirror is not deleted, and the failure is logged. Test in Task 5.
2. **Corrupt or truncated segment at rebuild:** that night imports events-only with a
   warning instead of crashing, and the next card import replaces the bad segment.
   Tests in Tasks 1, 3 and 4.
3. **Unreadable or newer-version `archive.json`:** never treated as an empty archive and
   overwritten. Unreadable → rebuilt from the segments; newer version → archive is
   read-only for this build. Tests in Tasks 1 and 2.
4. **Import pointed at a copy of an archive that is not the device's own Backup folder:**
   the nights import, the source is not modified, and the device's own archive gains
   them. Test in Task 5.
5. **Cancel during capture:** no temporary file is left, the manifest lists only whole
   segments, and the next import finishes the job. Test in Task 5.

## File Structure

| File | Responsibility |
|---|---|
| `oscar/SleepLib/card_archive.h/.cpp` (new) | Archive layout, manifest, store/skip/replace rules, index sets, other files. Knows no card format, does not use the profile |
| `oscar/SleepLib/loader_plugins/bmc_archive.h/.cpp` (new) | `BmcCaptureToArchive()`: drives a `BmcDataParser` into a `CardArchive` |
| `oscar/SleepLib/loader_plugins/bmcDataParsing.h/.cpp` | `BmcDataParser` archive interface; legacy implementation |
| `oscar/SleepLib/loader_plugins/bmcG3xDataParsing.h/.cpp` | G3X implementation |
| `oscar/SleepLib/loader_plugins/bmc_loader.cpp`, `bmcg3x_loader.cpp` | `Open()`: archive-mode import, capture, migration |
| `oscar/SleepLib/machine_loader.h` | New virtual `rebuildLossWarning()` (default: empty) |
| `oscar/mainwindow.cpp`, `oscar/SleepLib/profiles.cpp` | Show that warning in the rebuild and upgrade prompts |
| `oscar/tests/cardarchivetests.h/.cpp`, `bmcarchivetests.h/.cpp` (new) | Unit tests |

---

### Task 1: `CardArchive` segments and manifest

**Files:**
- Create: `oscar/SleepLib/card_archive.h`, `oscar/SleepLib/card_archive.cpp`
- Create: `oscar/tests/cardarchivetests.h`, `oscar/tests/cardarchivetests.cpp`
- Modify: `oscar/oscar.pro` (SOURCES, HEADERS, and both lists in the `test { }` block)

**Interfaces:**
- Consumes: `gCompress()`, `gUncompress()` (`SleepLib/common.h`).
- Produces:

```cpp
//! What a loader offers for one record (one night).
struct CardArchiveRecord {
    QDate     date;             //!< the index's date for the record
    QDateTime first;            //!< first packet time; invalid if unknown before reading
    QDateTime last;             //!< last packet time; invalid if unknown before reading
    qint64    bytes = -1;       //!< uncompressed size on offer; -1 if unknown before reading
    bool      final = false;    //!< not the newest night on the card
    bool      complete = true;  //!< false if known to be only the tail of the night
    int       rule = 0;         //!< the loader's capture-rule version (spec rule 10)
};

//! One manifest row.
struct CardArchiveEntry {
    QString file; QDate date; QDateTime first; QDateTime last;
    qint64 bytes = 0; int packets = 0; bool final = false; bool complete = true;
    int rule = 0;               //!< capture-rule version the segment was taken under
    QByteArray sha256;          //!< of the uncompressed content
};

class CardArchive {
public:
    enum class StoreResult { Stored, Replaced, KeptExisting, Failed };
    static constexpr int kFormatVersion = 1;
    static QString manifestName();    // "archive.json"
    static QString segmentDirName();  // "Segments"

    explicit CardArchive(const QString &dir);
    static bool isArchive(const QString &dir);            // manifest file exists
    void setIdentity(const QString &loaderName, const QString &baseName); // new archives only
    QString baseName() const;
    bool load();                  // false if the manifest is missing, unreadable or newer
    bool isWritable() const;      // false after a newer-version manifest or setReadOnly(true)
    void setReadOnly(bool on);    // for an archive that is not the device's own Backup folder
    bool save();                  // QSaveFile; no-op returning false when !isWritable()
    QList<CardArchiveEntry> entries() const;               // sorted by date, then first
    const CardArchiveEntry *find(const CardArchiveRecord &rec) const;
    //! True if any entry's [first, last] overlaps [\a from, \a to].
    bool coversSpan(const QDateTime &from, const QDateTime &to) const;
    bool wants(const CardArchiveRecord &rec) const;
    StoreResult store(const CardArchiveRecord &rec, const QByteArray &packets, int packetSize);
    QByteArray read(const CardArchiveEntry &entry, bool *ok = nullptr) const;
    void drop(const CardArchiveEntry &entry);              // remove entry and file, save;
                                                           // no-op when !isWritable()
};
```

Decisions the signatures do not carry:

- `find()`: an entry matches when its date equals `rec.date` and either `rec.first` is
  invalid or the spans `[first, last]` overlap.
- `wants()`: true if no match; true if `entry.rule < rec.rule`; false if the match is
  `final && complete`; if `rec.bytes` is -1, true; otherwise false when
  `rec.bytes < entry.bytes`, false when equal and the entry is `final`, true in every
  other case.
- `store()`: rejects (`Failed`) empty data, a size that is not a multiple of
  `packetSize`, an invalid `rec.date`/`first`/`last`, and `!isWritable()`. With a match,
  returns `KeptExisting` when `packets.size() < entry.bytes`, or when the sizes are equal
  and the entry is `final`; otherwise replaces. Exception for an older rule
  (`entry.rule < rec.rule`): a capture with `rec.complete` replaces whatever its size,
  because the newer rule's full range is the correct one; one that is not complete
  replaces only if longer, and a kept entry keeps its old `rule` so it is offered again.
  Without a match, stores.
- Segment name: `<baseName>_<yyyyMMdd>.seg.gz`; if that name belongs to a different
  (non-overlapping) record, `_2`, `_3`, … before `.seg.gz`.
- Write order: gzip to `<name>.tmp` in `Segments/`, rename over the final name, then
  `save()`. On any failure remove the `.tmp` and leave the manifest as it was.
- `read()`: `ok` is false and the result empty when the file is missing, does not
  decompress, or its SHA-256 or size disagrees with the entry.
- Manifest JSON: `{"version":1,"loader":…,"base":…,"index":{…},"segments":[{file,date,
  first,last,bytes,packets,final,complete,rule,sha256}]}`; dates ISO, hash hex. Unknown keys
  are preserved on save. `load()` drops entries whose file is missing.

- [ ] **Step 1: Write the failing tests** in `cardarchivetests.cpp` (class
  `CardArchiveTests`, each on a fresh `QTemporaryDir`). Helper
  `QByteArray packets(int count, char fill)` returns `count * 256` bytes.

```cpp
void testStoreAndReadBack();      // store 100 packets -> Stored; entries().size()==1;
                                  // file "base_20261114.seg.gz" exists; read() == data, ok
void testReloadKeepsEntries();    // second CardArchive on same dir: load() true, same entry
void testFinalCompleteIsSkipped();// after storing final+complete: wants(same)==false,
                                  // even with rec.bytes larger
void testLongerReplacesShorter(); // non-final 100 packets, then 150 -> Replaced; read()==150
void testNeverShortened();        // final 150 packets (complete=false), then 100 ->
                                  // KeptExisting; read() still 150
void testTailThenFull();          // stored: final, complete=false, span 02:00-06:00,
                                  // 100 packets. Offered: same date, span 22:00 (day
                                  // before)-06:00, 200 packets, complete=true ->
                                  // wants()==true, Replaced, entry.complete==true
void testSameDateNoOverlap();     // two records same date, spans 01:00-02:00 and
                                  // 05:00-06:00 -> two entries, second file ends "_2.seg.gz"
void testRejectsBadInput();       // empty data, 257 bytes, invalid date -> Failed, no files
void testCorruptSegment();        // overwrite the .seg.gz with garbage: read() empty,
                                  // ok==false; drop() removes entry; wants()==true again
void testNewerVersionReadOnly();  // write manifest with "version":2: load()==false,
                                  // isWritable()==false, store()==Failed, file unchanged
void testSetReadOnly();           // setReadOnly(true): store()==Failed, drop() and save()
                                  // change nothing on disk; read() still works
void testNewerRuleRecaptured();   // stored final+complete, rule 1, 150 packets. Offer
                                  // rule 2: wants()==true. store(rule 2, complete,
                                  // 120 packets) -> Replaced, entry.rule==2, bytes 120*256
void testNewerRuleTailKept();     // same start; store(rule 2, complete=false, 80 packets)
                                  // -> KeptExisting, entry.rule still 1, wants() still true
void testCoversSpan();            // entry 22:00-06:00: 05:00-07:00 true, 06:30-07:00 false
void testNoTmpLeftBehind();       // after every store above, Segments/ has no "*.tmp"
```

- [ ] **Step 2: Build the test target and run `.\test.exe`.** Expected: build fails
  (`card_archive.h` not found).
- [ ] **Step 3: Implement `CardArchive`** as specified above and add the files to
  `oscar.pro`.
- [ ] **Step 4: Run `.\test.exe`.** Expected: exit code 0.
- [ ] **Step 5: Commit** (ask first): "CardArchive: per-night segment store for card backups".

---

### Task 2: `CardArchive` recovery, index sets and other files

**Files:**
- Modify: `oscar/SleepLib/card_archive.h/.cpp`, `oscar/tests/cardarchivetests.h/.cpp`

**Interfaces:**
- Produces (added to `CardArchive`):

```cpp
enum class IndexAction { Replace, Keep, Retire };
static QString generationDirName();   // "Generations"

//! Packet decoder supplied by a loader: first/last packet times of a segment's content.
using Describe = std::function<bool(const QByteArray &packets, QDateTime &first, QDateTime &last)>;

//! Add manifest entries for segment files that have none (also rebuilds a lost or
//! unreadable manifest). Adopted entries are final=true, complete=false, rule=0. Returns the count.
int adoptOrphans(int packetSize, const Describe &describe);

IndexAction indexAction(const QDate &cardFirst, const QDate &cardLast) const;
//! Apply indexAction(): copy \a names from \a srcDir (Replace), do nothing (Keep), or move
//! the current set to Generations/<yyyyMMdd>[_n]/ and then copy (Retire). Records the range.
bool installIndexSet(const QString &srcDir, const QStringList &names,
                     const QDate &first, const QDate &last);
QStringList generationDirs() const;   // absolute paths, oldest first
//! Copy every file under \a srcDir for which \a skip(relativePath) is false and that is
//! absent or differs in size. Never deletes. Recursive.
bool mirrorOtherFiles(const QString &srcDir, const std::function<bool(const QString &)> &skip);
```

Decisions:

- `indexAction()`: `Replace` when the archive has no index range, or
  `cardFirst <= indexFirst && cardLast >= indexLast`; `Keep` when
  `cardFirst >= indexFirst && cardLast <= indexLast`; otherwise `Retire`.
- The generation folder is named for the retired set's last date; `_2`, `_3` on collision.
- `installIndexSet()` and `mirrorOtherFiles()` are no-ops for file copying when `srcDir`
  is the archive folder itself (migration); `installIndexSet()` still records the range.
- Index files are copied to `<name>.tmp` and renamed, so a failed copy leaves the old set.
- `adoptOrphans()` takes the date from the file name; a segment that does not decompress
  or that `describe` rejects is left on disk and not adopted.
- `load()` on an unreadable manifest returns false but leaves `isWritable()` true, so the
  caller can `adoptOrphans()` and `save()`. Only a newer version makes it read-only.

- [ ] **Step 1: Write the failing tests.**

```cpp
void testIndexActionTable();   // none recorded -> Replace; card 1 Jan-30 Jun vs index
                               // 1 Jan-31 May -> Replace; card 1 Jan-30 Apr -> Keep;
                               // card 1 Jun-30 Jun -> Retire; card 1 Dec(prev)-30 Apr -> Retire
void testInstallReplace();     // newer file content replaces older; range updated
void testInstallKeep();        // older card: archive file content unchanged
void testInstallRetire();      // old files now in Generations/20260531/, new at top level,
                               // generationDirs().size()==1
void testAdoptOrphans();       // store 2 segments, delete archive.json: load()==false,
                               // adoptOrphans()==2, entries match dates, final, !complete
void testAdoptAfterGarbageManifest(); // archive.json = "not json": same result as above
void testOrphanBadGzipIgnored();      // extra garbage *.seg.gz: not adopted, file kept
void testMirrorOtherFiles();   // copies new + size-changed files, honours skip, keeps
                               // a file that exists only in the archive
```

- [ ] **Step 2: Run `.\test.exe`.** Expected: build fails (members not declared).
- [ ] **Step 3: Implement the additions.**
- [ ] **Step 4: Run `.\test.exe`.** Expected: exit code 0.
- [ ] **Step 5: Commit** (ask first): "CardArchive: index sets, manifest recovery, other files".

---

### Task 3: Legacy BMC parser archive interface

**Files:**
- Modify: `oscar/SleepLib/loader_plugins/bmcDataParsing.h` (classes `BmcDataParser`,
  `BmcData`), `bmcDataParsing.cpp` (`DirectoryHasBmcData`, `ReadData`, `ReadWaveforms`)
- Create: `oscar/tests/bmcarchivetests.h/.cpp`; modify `oscar/oscar.pro`

**Interfaces:**
- Consumes: `CardArchive`, `CardArchiveRecord`, `CardArchiveEntry` (Tasks 1–2).
- Produces (pure virtuals on `BmcDataParser`, implemented here for `BmcData`):

```cpp
struct BmcArchiveRecord { CardArchiveRecord info; int handle = -1; }; // handle: parser-private

virtual void SetArchive(CardArchive *archive) = 0;  //!< before ReadData(); non-null = archive mode
virtual QList<BmcArchiveRecord> ArchiveRecords() = 0;                 //!< after ReadData()
//! The record's packets, in order, wrap removed. Fills first/last/bytes/complete in
//! \a rec.info. Empty if there are none or \a aborted returned true.
virtual QByteArray ReadArchiveRecordBytes(BmcArchiveRecord &rec,
                                          const std::function<bool()> &aborted) = 0;
virtual int PacketSize() const = 0;                                   //!< 256 legacy, 2048 G3X
virtual bool DescribePackets(const QByteArray &packets, QDateTime &first, QDateTime &last) const = 0;
virtual QString BaseName() const = 0;                                 //!< card file base name
virtual QStringList IndexFileNames() const = 0;                       //!< present index-class files
virtual bool IndexDateRange(QDate *first, QDate *last) const = 0;
virtual bool IsRingFileName(const QString &fileName) const = 0;
//! Archive mode: dates whose segment is in the manifest but failed read() during this
//! import. Filled as nights are read; guarded by a mutex (import tasks may be threaded).
virtual QList<QDate> UnreadableNights() const = 0;
```

Capture-rule constants, set as `info.rule` on every record in card mode:
`constexpr int kBmcLegacyCaptureRule = 1;` (`bmcDataParsing.h`) and
`constexpr int kBmcG3xCaptureRule = 1;` (`bmcG3xDataParsing.h`), each with a comment
saying it is raised when the logic that decides which packets belong to a night changes,
and that it is not the loader's data version. In archive mode `info.rule` comes from the
manifest.

Legacy decisions:

- **One reader for import and capture.** Split `ReadWaveforms()` into
  `QByteArray ReadWaveformBytes(BmcDataLink &link, const std::function<bool()> &aborted)`
  (today's scan, returning the accepted 256-byte packets concatenated) and a decode of
  that buffer into `QList<BmcWaveformPacket>`. `ReadWaveforms()` becomes decode of
  `ReadWaveformBytes()`. `ReadArchiveRecordBytes()` returns the same buffer.
- **Records:** one per `SessionLinks` entry. `info.date` = USR session date; `first`,
  `last`, `bytes` unknown until read; `final` = a later USR session exists; `handle` =
  index into `SessionLinks`.
- **`complete`** is set after reading: false when the first accepted packet is the oldest
  packet on the card (its timestamp is not later than `WaveformCrumbs.first().Timestamp`
  once the crumbs are sorted), true otherwise.
- **Archive mode:** `ReadData()` skips `BuildWaveformCrumbs()` and the `.nnn` probing in
  `FindValidSessions()` (every USR session still gets a link, with no crumb).
  `ReadWaveformBytes()` returns `archive->read()` of the entry for the session's date
  whose span overlaps the USR session window; on `ok == false` it logs a `qWarning`,
  adds the date to `UnreadableNights()` and returns empty, which selects the existing
  events-only path. A parser never modifies the archive; the loader drops unreadable
  entries after the import (Task 6). `ArchiveRecords()` in archive mode fills `first/last/bytes/complete`
  from the manifest.
- `IndexFileNames()`: the `.USR`, `.idx`, `.evt`, `.log`, `.set` files that exist beside
  the USR file. `IsRingFileName()`: base name plus `.` and exactly three digits.
  `IndexDateRange()`: earliest and latest USR session date.
- `DirectoryHasBmcData()`: the `.000` requirement becomes "`.000` exists or
  `CardArchive::isArchive(path)`".
- `BmcData` gets `friend class BmcArchiveTests;` under `UNITTEST_MODE`.

- [ ] **Step 1: Write the failing tests** (`BmcArchiveTests`). Build a synthetic legacy
  card in a `QTemporaryDir` with a helper `makeLegacyCard(dir, nights)`: a `.USR` with
  one historic record per night (reuse the record builder pattern in
  `tests/bmcusrsessionstests.cpp`), an `.idx` with one 512-byte entry per night pointing
  at its first packet, and `.000` holding 60 packets per night (magic at 0x00, timestamp
  at 0xF8–0xFE, one per second from 23:00:00), three nights 2026-03-10/11/12.

```cpp
void testLegacyRecordsListed();   // ArchiveRecords().size()==3 (+1 if the in-progress
                                  // session is linked); first two final, last not
void testLegacyBytesMatchImport();// ReadArchiveRecordBytes(night 2).size()==60*256 and
                                  // decoding it gives the same timestamps as
                                  // ReadDateSession(night 2).Waveforms
void testLegacyDescribePackets(); // first==23:00:00, last==23:00:59 on the night's date
void testLegacyArchiveRoundTrip();// store all three in a CardArchive, copy index files,
                                  // delete .000; DirectoryHasBmcData()==true; new BmcData
                                  // with SetArchive: ReadDateSession(night 2) has the same
                                  // 60 packet timestamps and same Sessions count as from card
void testLegacyMissingSegment();  // archive without night 1: ReadDateSession(night 1)
                                  // returns no waveforms and does not throw
void testLegacyCorruptSegment();  // garbage in night 2's file: no waveforms, no throw;
                                  // UnreadableNights()=={night 2}; archive unchanged.
                                  // (The missing night of the previous test is not listed.)
void testLegacyRecordRule();      // card mode: every record's info.rule==kBmcLegacyCaptureRule
void testLegacyFileClasses();     // IsRingFileName("X.000") true, ("X.USR") false,
                                  // ("X.0000") false; IndexFileNames() lists USR and idx
```

- [ ] **Step 2: Run `.\test.exe`.** Expected: build fails (interface not declared).
- [ ] **Step 3: Implement** the interface on `BmcDataParser` and `BmcData`. Give
  `BmcG3xData` temporary overrides that return empty/false so the tree compiles.
- [ ] **Step 4: Run `.\test.exe`.** Expected: exit code 0, including the existing
  `BmcUsrSessionsTests`, `BmcSessionEndsTests` and `BmcPressureWaveTests`.
- [ ] **Step 5: Commit** (ask first): "Legacy BMC parser: read and list nights for the night archive".

---

### Task 4: G3X parser archive interface

**Files:**
- Modify: `oscar/SleepLib/loader_plugins/bmcG3xDataParsing.h/.cpp`
  (`ParseIdxRecords`, `ReadDateSession` waveform loop near line 1500,
  `ResolveLeakFieldOffset`, `HasG3xDataFiles`), `oscar/tests/bmcarchivetests.h/.cpp`

**Interfaces:**
- Consumes: Task 3's `BmcDataParser` interface.
- Produces: the same interface implemented for `BmcG3xData`, plus private

```cpp
struct WaveSpan { std::unique_ptr<QIODevice> device; qint64 start = 0; qint64 end = 0; };
//! The devices and byte ranges holding \a entry's packets, in order. Card mode: one span
//! per ring file touched. Archive mode: one QBuffer over the decompressed segment.
std::vector<WaveSpan> OpenWaveSpans(const G3xDayEntry &entry) const;
```

G3X decisions:

- **One reader for import and capture.** The packet loop in `ReadDateSession()` and the
  probe in `ResolveLeakFieldOffset()` iterate `OpenWaveSpans()` instead of opening ring
  files themselves. `ReadArchiveRecordBytes()` concatenates the same spans.
- Card-mode spans start at the record's start rounded down to a 2048-byte packet boundary
  within its file, exactly as the current loop's `cursor` does, so a segment is
  packet-aligned.
- **Records:** one per `dayEntries` item with `WaveLength > 0`. `first` =
  `StartTimestamp`, `last` = `EndTimestamp` minus one second, `bytes` = span total,
  `final` = a later day entry exists, `complete` = false when `ParseIdxRecords()` trimmed
  the record to its surviving tail (add `bool WaveTailOnly` to `G3xDayEntry`).
- **Archive mode:** `ParseIdxRecords()` does not probe ring files. For each IDX record it
  looks up the archive entry for the record's date (`find()` with only the date set;
  with several entries for one date, the longest); if found, `WaveStartOffset = 0`,
  `WaveEndOffset = WaveLength = entry.bytes`, timestamps from the entry
  (`EndTimestamp = last + 1 s`); if not, the record takes the existing no-waveform path.
  `WaveformFileFor()` and `ReadVirtualWaveformBytes()` must not be reached in archive
  mode (`Q_ASSERT`). A segment that fails `read()` is handled as in Task 3.
- `IndexFileNames()`: `.idx`, `.evt`, `.log`, `.set` that exist. `IsRingFileName()` as
  legacy. `IndexDateRange()`: earliest and latest IDX record date, including records
  without waveform.
- `HasG3xDataFiles()`: also true when the folder is an archive.
- `friend class BmcArchiveTests;` under `UNITTEST_MODE`.

- [ ] **Step 1: Write the failing tests.** Helper `makeG3xCard(dir, nights, fileCount)`:
  an `.idx` with the `"BMC G/E/P INDEX"` header, ring geometry at 0xD4/0xD8 (64 MiB,
  `fileCount`), one 2048-byte day record per night with waveform start/end/length; an
  empty `.evt`; ring files holding 30 packets per night (magic `AD AA`, timestamp at
  0x04–0x09). Use sparse files: write only the packets, at their offsets.

```cpp
void testG3xRecordsListed();     // 3 nights -> 3 records, bytes==30*2048, last not final
void testG3xBytesMatchImport();  // record bytes' packet timestamps == ReadDateSession's
void testG3xWrappedNight();      // fileCount 2, night starting 10 packets before the end of
                                 // .001 and ending 20 packets into .000: bytes==30*2048,
                                 // timestamps strictly increasing
void testG3xTailOnly();          // overwrite the first 12 packets of night 1 with packets
                                 // dated 3 months later: record bytes==18*2048,
                                 // complete==false
void testG3xArchiveRoundTrip();  // capture all, copy index files, remove ring files:
                                 // DirectoryHasBmcG3xData()==true; archive-mode
                                 // ReadDateSession() gives identical waveform timestamps
void testG3xMissingAndCorruptSegment(); // both -> no waveform packets, no throw;
                                 // UnreadableNights() lists only the corrupt night
void testG3xRecordRule();        // card mode: every record's info.rule==kBmcG3xCaptureRule
void testG3xNoRingAccessInArchiveMode();// archive-mode parse + read of all nights succeeds
                                 // with no ring files present (would assert otherwise)
```

- [ ] **Step 2: Run `.\test.exe`.** Expected: the new G3X tests fail (stub overrides).
- [ ] **Step 3: Implement** on `BmcG3xData`, replacing the Task 3 stubs.
- [ ] **Step 4: Run `.\test.exe`.** Expected: exit code 0.
- [ ] **Step 5: Commit** (ask first): "BMC G3X parser: read and list nights for the night archive".

---

### Task 5: Shared capture routine

**Files:**
- Create: `oscar/SleepLib/loader_plugins/bmc_archive.h/.cpp`; modify `oscar/oscar.pro`,
  `oscar/tests/bmcarchivetests.h/.cpp`

**Interfaces:**
- Consumes: `CardArchive` (Tasks 1–2), `BmcDataParser` archive interface (Tasks 3–4).
- Produces:

```cpp
struct BmcCaptureStats {
    int offered = 0, skipped = 0, stored = 0, replaced = 0, failed = 0;
    bool aborted = false;
    bool ok() const { return failed == 0 && !aborted; }
};

//! Bring \a archive up to date with everything \a parser (already ReadData()) describes in
//! \a sourceDir: index set, other files, then every record the archive wants.
BmcCaptureStats BmcCaptureToArchive(BmcDataParser &parser, const QString &sourceDir,
                                    CardArchive &archive, const QString &loaderName,
                                    const std::function<void(int done, int total)> &progress,
                                    const std::function<bool()> &aborted);

//! Delete the ring files of an old full-card mirror in \a dir. Returns the number removed.
int BmcRemoveRingFiles(const BmcDataParser &parser, const QString &dir);

struct BmcArchiveReadReport {
    QList<QDate> unreadable;   //!< segment present in the manifest but failed read()
    QList<QDate> unclaimed;    //!< segment whose date no parser's index describes
    bool clean() const { return unreadable.isEmpty() && unclaimed.isEmpty(); }
};
//! After an archive-mode import: what the archive held that was not imported in full.
//! \a parsers are the main parser and one per index generation, all on \a archive.
BmcArchiveReadReport BmcArchiveReadCheck(const QList<BmcDataParser *> &parsers,
                                         const CardArchive &archive);
```

Decisions:

- Order: `archive.load()`; if `isWritable()`, `adoptOrphans()` with the parser's
  `PacketSize()`/`DescribePackets()` (always, so a segment left by a crash between the
  rename and the manifest write is picked up, and an unreadable manifest is rebuilt);
  `setIdentity()` if new; `installIndexSet()`;
  `mirrorOtherFiles()` skipping index names, ring names, `archive.json`, `Segments/` and
  `Generations/`; then records in date order.
- `progress(done, total)` counts only records `wants()` accepted, so an unchanged card
  reports `total == 0` and reads no ring data.
- A record whose read comes back empty is counted `skipped`, not `failed`.
- If `!archive.isWritable()`: return immediately with `failed = 1`, having written nothing.
- Works unchanged when `parser` is itself in archive mode (copying one archive into
  another).

- [ ] **Step 1: Write the failing tests** (reuse the Task 3/4 card builders; run each for
  both parsers where it says "both").

```cpp
void testCaptureFresh();        // both: 3 nights -> stored==3, index files and an extra
                                // "lang.BIN" copied, no ring file in the archive
void testCaptureAgainReadsNothing(); // both: second run on same card: stored==0 and
                                // replaced<=1 (only the newest night), progress total<=1
void testCaptureNewNight();     // both: card gains night 4 -> exactly night 3 replaced or
                                // finalised and night 4 stored; nights 1-2 untouched
                                // (same sha256 and file time)
void testUnionEitherOrder();    // G3X: cards A (nights 1-3) and B (nights 3-5, night 1-2
                                // overwritten). A then B, and B then A, both give 5
                                // entries with identical sha256 sets
void testMigrationSource();     // legacy: sourceDir == archive dir holding a full mirror:
                                // stored==3; then BmcRemoveRingFiles()==1; index intact
void testArchiveToArchive();    // legacy: capture card->X; archive-mode parser on X
                                // captured into Y: Y has the same 3 sha256; X unchanged
void testAbortMidway();         // aborted() true after first record: stats.aborted,
                                // entries()==1, no "*.tmp" anywhere; rerun completes to 3
void testUnwritableArchive();   // archive.json with "version":2 in target: failed==1,
                                // ok()==false, target folder byte-for-byte unchanged
void testWriteFailure();        // replace Segments with a regular file before capture:
                                // failed==3, ok()==false, no entries, no crash
void testRuleBumpRecaptures();  // legacy: capture with rule 1; parser subclass reporting
                                // rule 2 on the same card: stored+replaced==3, every
                                // entry.rule==2; a third run reads nothing but the newest
void testReadCheck();           // both: archive of 3 nights, one segment corrupted, one
                                // extra segment stored for a date outside the index:
                                // after reading all nights, unreadable=={corrupt date},
                                // unclaimed=={extra date}; untouched archive -> clean()
```

- [ ] **Step 2: Run `.\test.exe`.** Expected: build fails (`bmc_archive.h` not found).
- [ ] **Step 3: Implement `bmc_archive.cpp`.**
- [ ] **Step 4: Run `.\test.exe`.** Expected: exit code 0.
- [ ] **Step 5: Commit** (ask first): "BMC: capture new nights from a card into the night archive".

---

### Task 6: Wire both loaders' `Open()`

**Files:**
- Modify: `oscar/SleepLib/loader_plugins/bmc_loader.cpp` (`BmcLoader::Open`, the
  "Create backup" region near line 1043), `bmcg3x_loader.cpp` (`BmcG3xLoader::Open`, the
  backup block near line 167), and their headers if a helper is declared.

**Interfaces:**
- Consumes: `BmcCaptureToArchive()`, `BmcRemoveRingFiles()`, `CardArchive`, the parser
  interface.
- Produces: no new public interface. Put the logic both loaders share in one function in
  `bmc_archive.cpp` if it needs no `MachineLoader` members beyond the two callbacks.

`Open(dirpath)` behaviour, identical in both loaders:

1. `sourceIsArchive = CardArchive::isArchive(dirpath)`; `sourceIsBackup` = `QDir(dirpath)
   == QDir(mach->getBackupPath())` (compare `QDir`s, not strings).
2. If `sourceIsArchive`: load a `CardArchive` on `dirpath`, call `parser->SetArchive()`
   before `ReadData()`. Then, for each `generationDirs()` entry, create a second parser on
   that folder with the same archive. Queue import tasks from the main parser first, then
   from each generation only for dates not already queued. All parsers stay alive until
   `runTasks()` returns. When the source is not the device's own Backup folder, call
   `setReadOnly(true)` on its archive so nothing in it can be changed.
3. Remove the existing wipe-and-copy block. Import tasks run as now.
4. After `runTasks()`, if `!isAborted()`, `backupCardData()` is true and `!sourceIsBackup`:
   - target = `CardArchive(mach->getBackupPath())`.
   - **Migration:** if the Backup folder is not an archive but this loader's `Detect()`
     accepts it, parse it with a second parser and `BmcCaptureToArchive()` it into the
     target (source folder == target folder).
   - Capture the import source into the target.
   - Only if a migration ran and both captures returned `ok()`:
     `BmcRemoveRingFiles()` on the Backup folder.
   - Messages: `emit updateMessage(QObject::tr("Backing up new nights..."))`; drive
     `setProgressMax/Value` from the progress callback; pass `[this]{ return isAborted(); }`
     with `QCoreApplication::processEvents()` in the progress callback so Cancel is seen.
   - Any `!ok()` result is logged with `qWarning` (counts from the stats) and does not
     change `Open()`'s return value.
5. If `sourceIsBackup` and the folder is still an old mirror (no `archive.json`), import
   from it exactly as today and write nothing; it is converted at the next card import.
6. After an archive-mode import (step 2), run `BmcArchiveReadCheck()` over all the
   parsers. If it is not `clean()`:
   - `qWarning` the dates.
   - If the source archive is writable (the device's own Backup folder), `drop()` each
     unreadable entry so the next card import can store that night again.
   - Unless `mainwin->isBatchMode()`, show one `QMessageBox::warning` after the import
     tasks finish, titled `tr("BMC Backup")`, with whichever of these apply, each followed
     by the dates (at most ten, then `tr("and %n more", "", count)`):
     `tr("%n night(s) in the backup could not be read and were restored without detailed graphs. Import the SD card again to restore any that it still holds.", "", count)`
     and
     `tr("%n night(s) in the backup have no matching index record and were not imported.", "", count)`.

- [ ] **Step 1: Implement the `Open()` changes in both loaders.**
- [ ] **Step 2: Run `.\test.exe`.** Expected: exit code 0 (no regression).
- [ ] **Step 3: Build the application** with the `build-oscar` skill. Expected: no warnings
  from the changed files.
- [ ] **Step 4: Verify on real cards** in the scratch data folder (a new profile; never a
  real one). Record the session count and total waveform hours for the device after each
  step from the database (queries in `Notes/database/USEFUL_QUERIES.sql`).
  1. G3X series (three images of one device at increasing fill): import oldest, middle,
     newest. Expected: after each import the Backup folder holds `archive.json`,
     `Segments/` with one file per waveform night seen so far, the index files, and no
     `.0nn` files.
  2. Import the newest image again. Expected: the backup step shows no progress bar
     activity beyond one night and takes under 5 seconds.
  3. Rebuild the device's data from backup (OSCAR's rebuild command for that device).
     Expected: the same session count and
     waveform hours as before the rebuild.
  4. New profile, import newest → middle → oldest, rebuild. Expected: same totals as 3.
  5. Migration: with the build from before this work, import a wrapped legacy card and a
     wrapped G3X card into a new profile; switch to the new build and import each again.
     Expected: `Segments/` holds every waveform night the old mirror held, ring files are
     gone, and a rebuild gives the same totals as before.
  6. Start a first import of a full G3X card and press Cancel during "Backing up new
     nights...". Expected: it stops within a few seconds; no `.tmp` in `Segments/`;
     importing again completes the archive.
  7. Turn "Create SD Card Backups during Import" off and import. Expected: Backup folder
     unchanged.
  8. Legacy: the sample collection has no series of images from one legacy device, and
     deriving an earlier card state by editing a wrapped image is not trustworthy. Ask the
     user for a second image of a legacy device taken at a different date. Until that is
     run, report the legacy two-image check as not done.
- [ ] **Step 5: Commit** (ask first): "BMC loaders: back up new nights only; rebuild restores all captured nights".

---

### Task 7: Warn before a rebuild that cannot restore everything

**Files:**
- Modify: `oscar/SleepLib/machine_loader.h` (new virtual), `oscar/mainwindow.cpp`
  (`MainWindow::rebuildCPAPMachine`, the `else if (backups)` question),
  `oscar/SleepLib/profiles.cpp` (`Profile::DataFormatError`, the `if (backups)` text),
  `oscar/SleepLib/loader_plugins/bmc_loader.h/.cpp`, `bmcg3x_loader.h/.cpp`,
  `bmc_archive.h/.cpp`, `oscar/tests/bmcarchivetests.h/.cpp`

**Interfaces:**
- Consumes: `CardArchive::coversSpan()` (Task 1), `CardArchive::isArchive()`.
- Produces:

```cpp
// machine_loader.h, class MachineLoader
//! \brief Text for the rebuild prompts when rebuilding \a mach from its backup would lose
//! detail the database now holds. Empty (the default) when there is nothing to say.
virtual QString rebuildLossWarning(Machine *mach) { Q_UNUSED(mach); return QString(); }

// bmc_archive.h
//! One night's waveform extent as the database holds it.
struct BmcStoredNight { QDate day; QDateTime first; QDateTime last; };
//! The \a nights that no segment in \a archive overlaps.
QList<BmcStoredNight> BmcNightsNotInArchive(const QList<BmcStoredNight> &nights,
                                            const CardArchive &archive);
//! Shared body of both loaders' rebuildLossWarning().
QString BmcRebuildLossWarning(Machine *mach, MachineLoader *loader);
```

Decisions:

- `BmcRebuildLossWarning()`:
  - Backup folder is an archive: collect a `BmcStoredNight` for every day of `mach` that
    has flow waveform data, using the day's first and last session times; call
    `BmcNightsNotInArchive()`. Empty result: empty string. Otherwise
    `tr("%n night(s) now have detailed graphs that this backup cannot restore (%1 to %2). After rebuilding they will show events and statistics only.", "", count)`
    with the first and last date formatted through
    `QLocale().toString(date, QLocale::ShortFormat)`.
  - Backup folder is an old card mirror (`loader->Detect()` accepts it, no
    `archive.json`): the fixed text
    `tr("This backup holds only the most recent contents of the SD card. Older nights that now have detailed graphs will show events and statistics only after rebuilding.")`.
  - No backup, or not recognised: empty string (the existing prompts already cover it).
- "Has flow waveform data" is intended to be `day->channelHasData(CPAP_FlowRate)`, which
  reads the stored summaries without loading events. This has not been verified for BMC
  sessions loaded from the database: Step 1 checks it, and if it is not reliable the
  implementer stops and reports instead of substituting another test.
- Both prompts append the warning as its own paragraph (`<p>...</p>` in
  `DataFormatError` and in HTML-formatted questions; a blank line then the text in the
  plain-text question in `rebuildCPAPMachine`). They call
  `loader->rebuildLossWarning(mach)` before the purge and only when `loader` is non-null.
  Nothing else in either prompt changes.

- [ ] **Step 1: Verify the waveform test.** In the scratch profile holding a BMC device
  with both waveform nights and events-only nights (from Task 6's checks), log
  `day->channelHasData(CPAP_FlowRate)` for each day after a fresh program start and
  compare with the Daily page. Expected: true exactly for the nights that show a flow
  graph. Remove the logging. If it does not hold, stop and report.
- [ ] **Step 2: Write the failing tests.**

```cpp
void testNightsNotInArchive();   // archive spans: night A 22:00-06:00, night B 23:00-05:00.
                                 // stored nights A, B, and C (no segment) -> result=={C};
                                 // a stored night overlapping A by one minute is covered
void testNightsNotInArchiveEmpty(); // empty archive -> all nights returned; no nights -> empty
```

- [ ] **Step 3: Run `.\test.exe`.** Expected: build fails (`BmcNightsNotInArchive` not declared).
- [ ] **Step 4: Implement** the virtual, the two functions, both loader overrides and the
  two prompt changes.
- [ ] **Step 5: Run `.\test.exe`** (expected: exit code 0) and build the application.
- [ ] **Step 6: Verify in the application** with the scratch profile:
  1. Device whose archive covers every waveform night: the rebuild prompt is unchanged.
  2. Delete two segment files and their manifest entries by hand, restart, choose
     rebuild. Expected: the prompt reports 2 nights with the right dates. Answer No.
     Expected: nothing is purged.
  3. Restore the two segments, then overwrite one segment file with garbage and rebuild.
     Expected: after the import a message names that night; it shows events only. Import
     the card image that holds that night. Expected: its graphs return and the segment
     is back in `Segments/`.
  4. A profile whose Backup folder is still an old mirror: the prompt shows the fixed
     caution.
  5. Rebuild a non-BMC device in any test profile. Expected: its prompt is unchanged.
- [ ] **Step 7: Commit** (ask first): "Rebuild: say which BMC nights the backup cannot restore".

---

### Task 8: Documentation

**Files:**
- Modify: `Notes/Design/2026-11-19-ring-buffer-backup-archive-design.md` (status, and an
  "As built" section listing any deviation from the design)
- Modify: `Notes/Formats/BMC/Luna/BMC_Legacy_File_Formats.md` §3.5 and
  `Notes/Formats/BMC/G3X/BMC_G3X_IDX_File_Layout.md` "Waveform Files": one paragraph each on
  how OSCAR's Backup folder stores these files and where the design is.
- Modify: `Notes/Process/BUG_FIXES.md`: an entry for #380 in the file's existing
  format (symptom, root cause, fix, non-obvious points: backup is no longer a card image;
  first import after upgrade converts the old mirror; no loader version change; nights
  overwritten before the archive existed cannot be restored and the rebuild prompt says
  so; when to raise a capture-rule constant).
- Do not edit `Htmldocs/release_notes.html`; ask the user whether they want to update it.

- [ ] **Step 1: Write the three documentation changes.** Check them for serials, names
  and sample-folder names before saving.
- [ ] **Step 2: Commit** (ask first), with `Closes #380` in the body. After committing run
  `git log -1 --pretty=%B` and confirm the message is clean. Do not push without asking.
