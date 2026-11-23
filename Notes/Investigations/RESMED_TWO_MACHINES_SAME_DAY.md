# ResMed: two machines on the same day

Investigated 2026-11-22 on master `ab5559e5`. Related issue: GitLab #377 (closed, no action).

OSCAR shows sessions from only one CPAP machine per day (`Day::addSession()`, `day.cpp:123-131`).
Someone who uses one ResMed machine for part of the night and another for the rest sees only one
of them. This note records a way round it that needs no change to OSCAR, and what was tested.

## Summary

Copy the second machine's DATALOG files into a copy of the first machine's card, and import that
merged card. OSCAR imports both machines' sessions into the same day, as sessions of the first
machine. Nothing else on the card is changed: not `STR.edf`, not the Identification file, not the
serial number, not the `.crc` files.

Changing only the serial number, so that two cards look like one machine, does not work reliably
(see below).

## How to merge

Call the machine whose card is kept "A" and the other one "B".

1. Copy A's card to a folder on disk. Do not change the card itself.
2. Copy B's DATALOG files for the shared nights into the copy's `DATALOG` folder, in the same
   layout the copy already uses. A night runs from noon to noon, so files stamped before noon
   belong to the previous night.
3. If those nights are already in OSCAR, purge the CPAP data of the earliest of them. On a
   build without the fix for GitLab #415, import in the same OSCAR session, without restarting.
4. Import the merged folder.

## Why it works

- The serial number is checked only in the `STR.edf` header (`fetchSTRandVerify()`,
  `resmed_loader.cpp:1011`). DATALOG files are not checked.
- DATALOG files that match no mask-on/off pair in the day's STR record become sessions of their
  own, built from the file durations (`ResDayTask::run()`, `resmed_loader.cpp:3013-3023`).
- Every session belongs to machine A, so `Day::addSession()` has nothing to refuse.

## Test

Two series 10 machines used by one person, an AirCurve 10 VAuto (A) and an AirSense 10 AutoSet
(B), with four nights where B ran in the evening and A after midnight. Cards were built for those
nights: A alone, B alone, and A plus B's DATALOG files. Each was imported with batch `--import`
into its own database, and the databases were compared.

| Check | Result |
|---|---|
| A's 6 sessions, merged card vs. A alone | Identical: times, events, settings, AHI |
| B's 4 sessions, merged card vs. B alone | Same times, events, event-list counts and AHI |
| Events from one machine's EVE files in the other's sessions | None |
| Daily summaries for the four nights | 2 or 3 sessions per night, hours and AHI combined |
| A imported first, then the merged card, no purge | 0 new sessions |
| A imported first, purge date set to the first shared night, then the merged card | Same result as the fresh merged import; only the four shared nights were re-imported (10 sessions of 823) |

## Re-importing days that are already in OSCAR

An import normally starts at the machine's last imported day (`resmed_loader.cpp:620`), so older
nights on the card are skipped. Purging a day sets the machine's purge date
(`MainWindow::purgeDayData()`, `mainwindow.cpp:3213-3215`), and the next import then starts at
the earlier of the two dates.

Starting earlier does not re-import everything since. Each day from the start date is checked
(`checkSummaryDay()`, `resmed_loader.cpp:1021`): a day whose stored session count already matches
what the card implies is skipped. A day is re-imported when it has no sessions (the purged day),
or when the card implies more sessions than are stored. The second case is what picks up the
other shared nights of a merged card, so purging the earliest shared night is enough. The files
from the start date on are still listed and offered to the backup, which costs time only.

In the test the purge date was set directly in the database, not with the purge command.

### The purge date was not saved (GitLab #415)

On the build tested, the purge date lived only in memory: nothing wrote it to the database for a
machine that already had a row. Confirmed in the GUI on 2026-11-22: purge a day that is not the
last day, close and restart OSCAR, import, and the purged day is not re-imported. That is
GitLab #415. On a build without its fix, purge and import in the same session (step 3 above).

Recovery, also confirmed in the GUI: purge an earlier day and import without restarting. The
import then starts at that earlier day, and the day lost before comes back with it, because it
has no sessions.

## Limits

- **Settings are machine A's.** B's sessions take A's STR record for the day. In the test,
  humidifier level, tube temperature and mask type were wrong for B's sessions; mode and pressure
  settings were right only because both machines were set the same. A STR day record holds one
  set of settings, so getting this right would mean editing `STR.edf`.
- **B's sessions are filed under machine A** and show A's model.
- **B's session IDs differ by under a minute** from an import of B alone: they come from the file
  start time instead of the STR mask-on minute.
- **A's backup folder receives B's files.** A rebuild from that backup was not tried.

## Why changing only the serial number is not enough

From reading the code, not tested. Giving card B machine A's serial (Identification file and
`STR.edf` header) and importing the two cards in turn gets past `Day::addSession()`, but:

- `checkSummaryDay()` skips a day whose stored session count already matches or exceeds what the
  new card implies (`resmed_loader.cpp:1087-1104`). One session from each machine is 1 >= 1, so
  the second machine's session is never imported.
- The two cards' `STR.edf` files then compete. They are keyed by start date; for a day covered by
  both, the file that starts later supplies the day's record (`resmed_loader.cpp:1544`).
- The backup's `STR.edf` and the machine's model flip between the two cards on each import.

## Not tested

- Machines from different families (S9 or AirSense 11 together with a series 10).
- Sessions of the two machines that overlap in time.
- How the merged nights look in Daily and Overview; only the database was checked.
- Other manufacturers' loaders.
