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
