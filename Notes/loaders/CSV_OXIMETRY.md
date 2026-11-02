# CSV oximetry import

For oximeters OSCAR has no reader for: a CSV file with the date and time, SpO2 and pulse
on each row. In the Oximetry Wizard choose **Other oximeter (CSV)**, then **From data
files**. The method page shows the accepted format.

Code: `SleepLib/loader_plugins/csvoximetry.*` (reader, machine, session), the wizard's
`OximeterImport::importCsvFiles()`. Tests: `tests/csvoximetrytests.cpp`.

## Format

```
DateTime,SpO2,Pulse
2026-11-02 02:24:12,97,58
2026-11-02 02:24:16,96,59
2026-11-02 02:24:20,,
2026-11-02 02:24:24,95,61
```

- **One reading per row.** Each row carries its own time, so any sampling rate works,
  including an irregular one.
- **Date and time:** `yyyy-MM-dd HH:mm`, with optional `:ss` and `.zzz`; a `T` or a space
  between date and time. Local time, unless an offset is given (`+01:00`, `-0500`, `Z`),
  in which case it is converted to local time.
- **Header:** optional. Without one, the columns are date and time, SpO2, pulse. With
  one, columns are found by name, in any order:
  - date and time: a name containing both "date" and "time" (`DateTime`), or one column
    named with "date" or "time" (`Timestamp`), or two columns, `Date` and `Time`;
  - SpO2: "spo2", "sat", "oxygen" or "o2";
  - pulse: "pulse", "heart", "bpm", or `PR` / `HR`.
  Other columns are ignored.
- **Separators:** comma, semicolon or tab, whichever the first line has most of. Fields
  in double quotes are fine; a decimal comma is fine for values.
- **Missing readings:** an empty field, `0`, text such as `--`, or a value out of range
  (SpO2 outside 50–100, pulse outside 20–250; 255 is a common "no reading" marker) is no reading at that moment.
- **Order:** rows may be out of order; they are sorted. For a time that appears twice,
  the last row wins.
- **Recordings:** a pause of more than 5 minutes starts a new recording, so one file can
  hold several nights. A recording without a single reading is dropped.
- A byte-order mark and Windows line endings are fine.

## Known exports

**Wellue O2 Insight Pro**, *Export as CSV* (checked on 2026-11-02 with a SleepU recording,
`SleepU_20260715225917.csv`, exported on Windows 10): imports as it is.

```
Time,SpO2(%),Pulse Rate(bpm),Motion,SpO2 Reminder,PR Reminder,
"10:59:17PM Jul 15, 2026",97,84,0,0,0,
...
"08:20:01AM Jul 16, 2026",255,65535,14,0,0,
```

- The time is quoted, 12-hour, with an English month and a comma inside
  (`hh:mm:ssAP MMM d, yyyy`); the reader handles quoted fields and this layout.
- One row per 4 seconds for the SleepU; a trailing comma on every line.
- No reading: SpO2 255, pulse 65535 (both out of range, so ignored).
- Motion and the two reminder columns are ignored.
- The 9.3-hour recording above imported as one session; its averages (SpO2 96.6%,
  pulse 65.8, minimum SpO2 92) agree with the file.

## Errors

A row whose date and time can't be read is skipped and reported by line number in the
summary (the first ten; then a count). A file is refused if more than half its rows
can't be read, if a header doesn't name the three columns, or if it has no readings.

## In OSCAR

- Each recording is a session at its own start time (session ID = start, UTC seconds),
  with SpO2 and pulse stored at their own timestamps (no resampling), and the usual
  SpO2-drop and pulse-change calculations and summaries.
- There is no CPAP-matching step: the times come from the file.
- A recording already in OSCAR (same start) is skipped.
- Machine: loader name `CSVOximetry`. With no name given, one generic machine, "Oximeter
  (CSV)", holds all CSV imports. With a name (the wizard's optional field, remembered for
  next time), each name is its own machine, so two oximeters stay apart. A new machine
  gets a fresh ID: `Profile::CreateMachine()` would otherwise reuse the ID of an earlier
  CSV machine's data folder and merge them.
