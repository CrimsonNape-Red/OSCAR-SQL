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
  `profiles.cpp`. Which pages ask for these while a profile opens has not been traced, so the
  saving is unknown until it is.
- **Batching is not an obvious win for the histograms.** Fetching them with one query per
  device instead of one per session was slower in a direct SQLite test (3.0 s against 1.6 s).
  The cost is the row volume, not the number of queries.
- **The smaller tables** (channels, settings, summary, slices, session lookup: about 1.4 s
  together) are where per-query overhead matters: reusing prepared statements, one query per
  device, and passing on the row `findByMachine` already has. Not measured.

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
