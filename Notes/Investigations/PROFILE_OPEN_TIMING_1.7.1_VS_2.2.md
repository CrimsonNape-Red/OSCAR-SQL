# Profile open time: OSCAR 1.7.1 vs 2.2.0-alpha-1

Measured 2026-11-21 on Windows 11 (Core i7-1355U, 32 GB RAM, NVMe SSD).

**Result:** the development build opens a large profile about 7× faster when the data is not in
the Windows file cache, and about 2 s slower when it is.

## What was compared

| | 1.7.1 | Development |
|---|---|---|
| Build | 1.7.1 release, Qt 5.15.2, MinGW | 2.2.0-alpha-1, master `97919310`, Qt 6.12.0, MSVC |
| Storage | profile folder: 10.1 GB, 30,941 files | SQLite database: 19.7 GB, 8 profiles |
| Start page | Welcome | Welcome (Modern UI) |

The same profile was opened in both: 3,091 CPAP sessions from four ResMed devices (2018–2022),
plus oximeter and journal data. CPAP session counts per device are identical in the two stores.

## Method

`profile_open_timing.ps1` (this folder) starts each build with `--profile "<name>"` and times,
from launch, until the main window title shows the profile **and** the GUI thread is idle. The
title is set near the end of `MainWindow::OpenProfile` in both versions. The builds ran
alternately, five rounds. The millisecond stamps in each run's `debug.txt` give the split
between program start-up and the profile open, and the phases inside it.

## Results

| | 1.7.1 | Development |
|---|---|---|
| First open | 61.3 s | 8.4 s |
| Warm open, median of 4 | 3.1 s | 5.2 s |
| Warm open, range | 2.9–3.4 s | 5.1–5.4 s |
| Warm: program start-up before the profile opens | 1.4 s | 1.6 s |
| Warm: profile open itself | 1.7 s | 3.6 s |
| CPU time, warm | 2.1 s | 4.3 s |
| CPU time, first open | 4.5 s | 5.8 s |
| Working set after open | 413 MB | 483 MB |
| Private memory after open | 378 MB | 407 MB |
| Close | 1.7 s | 0.5 s |

Neither version used more than 0.03 s of CPU in the 3 s after the GUI went idle, so neither
defers work to a background thread after opening.

### Where the time goes

| Phase (one warm run) | 1.7.1 | Development |
|---|---|---|
| "Opening profile" → "Finishing profile" (load machine and session data) | 0.2 s | 3.0 s |
| "Finishing profile" → GUI idle (build Welcome, Daily, Overview, Statistics) | 1.4 s | 0.6 s |

- **1.7.1, first open:** loading takes 0.4 s, because only each device's `Summaries.xml.gz` is
  read. The rest is the pages reading one summary file per session from disk: 31 s in
  `SummaryInfo::update` for the Daily graphs and 27 s for the Overview graphs, at 4.5 s of CPU.
  Once those files are cached the same two steps take about 0.55 s each.
- **Development, warm:** `Machine::LoadSessionsFromDatabase` accounts for nearly all of the
  3.0 s (the 1,185-session device alone takes about 1.1 s). On the first open the same phase
  took 6.1 s.

A trial run on a second, smaller profile (3.5 GB in 1.7.1) showed the same first-open pattern:
47.1 s for 1.7.1 against 5.2 s for the development build.

## Why the development build spends 3 s loading sessions

**Cause:** opening a profile eagerly reads every session's per-channel value histograms, 2.1
million rows for this profile, and that alone is about half of the 3 s. It is not a missing
index (every query uses one, checked with `EXPLAIN QUERY PLAN`) and not disk I/O (the data was
cached).

`Machine::LoadSessionsFromDatabase` calls `Session::LoadFromDatabase` once per session, which
runs six queries: 3,206 sessions, 19,236 queries. Each step was timed with temporary
`QElapsedTimer` accumulators in the app (one warm run, 3.15 s in the loop; the timers were
removed afterwards):

| Step in `Session::LoadFromDatabase` | Time | Rows read |
|---|---|---|
| Value/time histograms (`session_channel_values`) | 1,533 ms | 2,112,086 |
| Channel statistics (`session_channels`) | 545 ms | 59,163 |
| Settings (`session_settings`) | 377 ms | 49,766 |
| Summary row (`session_summaries`) | 276 ms | 3,206 |
| Session row lookup (`sessions`) | 150 ms | 3,206 |
| Slices (`session_slices`) | 86 ms | 0 |
| `AddSession` + the initial session list | 88 ms | — |

- **Histograms:** about 660 rows per session, mostly Te, Ti, Pressure, EPAP, RespRate and
  MinuteVent (74–153 distinct values per session each). Most of the 1.5 s is fetching the
  rows; inserting them into the nested hashes is about 0.2 s. The per-row timers add some
  overhead of their own to these two figures.
- **Fixed cost per query:** the slices query returns nothing for this profile yet costs 27 µs
  a call, and each call prepares its statement afresh. Across 19,236 queries that is roughly
  0.5 s, spread through the other rows of the table.
- **Redundant lookup:** the session row lookup re-reads a row that `findByMachine` fetched a
  moment earlier.
- **Why 1.7.1 is faster warm:** it reads only each device's `Summaries.xml.gz` at open and
  loads a session's histograms when a day needs them. That lazy loading is the same work that
  cost it 58 s on the uncached open.

### Candidates, not yet verified

- **Defer the histograms.** They feed `Day::percentile`, `Day::hasData` /
  `Day::channelHasData`, the time-above/below-threshold figures and the range percentiles in
  `profiles.cpp`. The next two sections trace which pages ask for them while a profile opens
  and measure a per-channel deferred load with a prototype.
- **Batching is not an obvious win for the histograms.** Fetching them with one query per
  device instead of one per session was slower in a direct SQLite test (3.0 s against 1.6 s).
  The cost is the row volume, not the number of queries.
- **The smaller tables** (channels, settings, summary, slices, session lookup: about 1.4 s
  together) are where per-query overhead matters: reusing prepared statements, one query per
  device, and passing on the row `findByMachine` already has. Not measured.

## Which pages read the histograms at open

**Result:** four things read them while a profile opens, and the Statistics report is by far
the biggest. Together they read only a small part of the 2.1 million rows that are loaded.

Every place that reads `m_valuesummary` / `m_timesummary` was given a temporary counter, tagged
by `OpenProfile` stage and then by tab. The profile was opened and each tab visited in turn
(counters removed afterwards). Of 3,206 sessions:

| When | Who reads | Sessions / days touched | Channels |
|---|---|---|---|
| Creating the Welcome page | `Day::percentile` via `welcomemetrics.cpp` | 76 / 31 | Leak, Pressure, EPAP, FLG |
| Daily page loading its last day | `Day::percentile` for the details panel | 2 / 1 | 48 |
| Statistics report, generated from `Daily::Load` | `Profile::calcPercentile` and time over leak redline (`Statistics`, `SC_MEDIAN` / `SC_90P` / above-threshold rows) | 1,693 / 648 | Leak, Pressure, FLG |
| Just after open, in the event loop | Overview stats row (`overviewstats.cpp`), shown range plus the range before it | 299 / 117 | Leak, Pressure |
| Creating the Overview page and its graphs | nothing | — | — |

`Day::channelHasData` also checks whether a histogram exists while the Welcome page (the same
31 days) and the Daily page (7 days) are created.

After open, by tab:

- **Overview:** the pressure chart reads IPAP and EPAP for the 59 visible days (169 sessions)
  when the tab is first shown.
- **Daily:** only "is there data" checks for the shown day.
- **Statistics:** nothing; the report was already built during open.

What this means for deferring the load:

- **Statistics sets the cost.** The test profile uses a custom report range of 659 days, which
  is why the report reaches 648 days of data. In standard mode it covers at most the last year.
- **Rows actually needed at open are few.** The Statistics range holds 1,036,143 histogram rows
  (49% of the profile's) across all channels, but only 150,229 (7%) for the three channels the
  report reads.
- **So the grain matters.** Loading a whole session's histograms on first touch would still
  read about half the rows for this profile; loading per channel would read under a tenth.
- **Statistics is rebuilt on every `Daily::Load`** (there is a FIXME on it), so a deferred load
  must keep what it reads or each date change would pay again.

Not measured: the union of sessions across the four readers. The percentages are row counts
from the database, not timings; the next section has the timings.

## What a per-channel deferred load saves

**Result:** about 1.3 s of the 3.9 s profile open on the test profile (roughly a third), and
about 80 MB of memory.

Measured with a throwaway prototype, not kept in the tree: one exe that loads histograms either
eagerly (today's behaviour) or per session and channel on first read, switched by an
environment variable. `Session::LoadFromDatabase` skips the histogram query, and every reader
listed above first asks the session to fetch that one channel. Six alternating warm runs each,
medians:

| | Eager (today) | Deferred per channel | Change |
|---|---|---|---|
| Launch to GUI idle | 5.60 s | 4.40 s | −1.2 s |
| Profile open itself | 3.91 s | 2.64 s | −1.3 s |
| Loading sessions | 3.28 s | 1.69 s | −1.6 s |
| Building the pages | 0.65 s | 0.92 s | +0.3 s |
| CPU time | 4.6 s | 3.4 s | −1.2 s |
| Working set | 484 MB | 406 MB | −78 MB |
| Private memory | 407 MB | 328 MB | −79 MB |

- **What moved to the pages:** the deferred run made 5,576 small queries for 152,413 rows (7%
  of the 2.1 million), taking about 230 ms. Nearly all of that is the Statistics report.
- **Results match:** the Statistics report and records box were byte-identical in the two
  modes. The Welcome, Daily and Overview output was not compared.
- **What is left:** loading sessions still takes 1.7 s, which is the other five queries per
  session.

Limits of the prototype, which a real implementation would have to deal with:

- **Presence checks load the data.** `Day::hasData`, `Day::channelHasData` and
  `RangeFigures::hasDetail` fetch a channel's histogram just to answer yes or no.
- **Store path untested.** `Session::StoreToDatabase` replaces the session's channel rows, so
  the prototype loads every remaining histogram first. No session was re-stored in these runs.
- **Clearing paths not handled.** The two places that clear a session's histograms along with
  its other statistics (one in `mainwindow.cpp`, one in `session_split.cpp`) were left alone.
- **Later tab visits not timed.** The Overview tab's first show would add a few hundred small
  queries.
- **One profile, with a 659-day Statistics range.** A standard one-year report reads less.

## Where the rest of the row-reading time goes

**Result:** date parsing. Converting each row's `created_at` text into a `QDateTime` takes
about 0.59 s of the 1.4 s spent on the five non-histogram queries, and nothing in the code
reads the result.

Each step inside the five per-session queries was timed with temporary accumulators (one warm
open, milliseconds; timers removed afterwards):

| Query | Total | Date parsing | Prepare | Execute | Row fetch | Other conversions | Rest |
|---|---|---|---|---|---|---|---|
| Channel statistics (59,163 rows) | 525 | 272 | 50 | 75 | 66 | 23 | 39 |
| Settings (49,766 rows) | 372 | 236 | 24 | 36 | 42 | 10 | 24 |
| Summary row | 273 | 44 | 70 | 86 | 1 | 31 | 41 |
| Session row lookup | 147 | 35 | 37 | 51 | 1 | 2 | 21 |
| Slices (0 rows) | 81 | 0 | 35 | 37 | 0 | 0 | 9 |
| **All five** | **1,398** | **587** | **216** | **285** | **110** | **66** | **134** |

- **Date parsing:** every row's `created_at` (and `updated_at` where present) goes through
  `query.value(n).toDateTime()`, at about 4.6 µs per conversion. The values are stored as text
  (`YYYY-MM-DD HH:MM:SS`). A search of the code found only assignments to the
  `createdAt` / `updatedAt` fields of the session structs (`SessionData`, `SessionChannelData`,
  `SessionSettingData`, `SessionSummaryData`), no reads. Why Qt takes 4.6 µs per conversion
  was not looked into.
- **Confirmed by switching it off:** skipping those conversions in the same exe cut session
  loading from 3.02 s to 2.36 s (−0.66 s).
- **Prepare:** each query prepares its statement afresh, about 13 µs each, 0.22 s over 16,030
  queries.
- **Forward-only queries:** `setForwardOnly(true)` trimmed the execute step by about 0.06 s;
  with date parsing also off, loading came to 2.27 s. On its own it was within run-to-run
  noise.
- **Not the cause:** filling the session's hashes from the rows takes only 26 ms, and reading
  the summary columns by name only 31 ms.

With date parsing gone the app reads the channel rows in about 236 ms, against 175 ms for a
bare SQLite client, so the gap noted earlier is essentially explained.

Deferring the histograms (about −1.6 s on loading) and dropping the date parsing (−0.66 s)
should stack, taking the 3.0 s load to roughly 1 s. That is arithmetic; the two were not run
together.

## Would a cache file like 1.7.1's help?

**Not in the 1.7.1 form.** What makes 1.7.1 quick at open is how little its cache file holds,
not the file itself, and the database already gives the same thing faster.

- **What 1.7.1's file is.** `Summaries.xml.gz` is only a session index: id, start, end,
  enabled, an events flag, and the lists of channel and setting ids (`Machine::LoadSummary` /
  `SaveSummaryCache`). It holds no statistics, settings values or histograms. Those live in one
  summary file per session and are read later, when a page needs that day. That later reading
  is the 58 s it paid on the uncached open.
- **The 2.x equivalent already exists.** The same index is the `sessions` table; reading it for
  all 3,206 sessions took 34 ms. A 1.7.1-style file would duplicate that and save nothing.
- **A fuller cache is a different idea.** A file holding everything `Session::LoadFromDatabase`
  reads could plausibly cut the remaining 1.7 s of session loading. That is not measured, and
  it means two copies of the truth: the file would need invalidating on import, purge, session
  enable/disable, recalculation, restore, schema migration, and any edit made to the database
  outside OSCAR. `daily_summaries` is this kind of cached rollup and has already produced
  staleness bugs (GitLab #239, #286).

Suggested order instead:

1. **Defer the histograms.** The measured win: about 1.3 s and 80 MB, with no second store.
2. **Cut the cost of the other five queries.** Stop parsing `created_at` / `updated_at` on
   load (measured: −0.66 s), then look at the per-query prepare and execute cost (about 0.5 s
   across the five queries). See the previous section.
3. **Consider loading per-session detail lazily**, which is the part of 1.7.1's design that
   actually made it fast. It needs the same kind of trace as the histograms got, since
   Statistics and Overview may ask for most of it at open anyway.

One case where a cache file might genuinely win is a cold start: a small file reads
sequentially, while the database pages are scattered through a 19.7 GB file. That is untested;
the first-open figure here (8.4 s) was not a controlled cold start.

## Caveats

- **"First open" is not a controlled cold start.** There was no reboot or cache flush. The 1.7.1
  profile had not been read recently; the development database is in daily use, so its 8.4 s may
  be partly cached. A controlled figure needs a reboot before each run.
- One machine, one profile, one device family (ResMed). Spinning disks and network or cloud
  folders would stretch the 1.7.1 first-open figure far more than the development one, but that
  was not measured.
- The two builds differ in Qt version and compiler as well as in data layer.
- Both versions log the "can't add session … already contains a different machine of the same
  MachineType" message for this profile while loading (two CPAP devices on one day).
