# Therapy Insights test data

Tools that build the test data described in
`Notes/Design/2026-10-17-therapy-insights-prd.md` §8. There are two kinds:

| | Real (anonymized) | Synthetic scenarios |
|---|---|---|
| Built by | `anonymize_oscar_db.py` | `make_scenarios.py` |
| Lives in | `testdata/insights/real/` (git-ignored, never committed) | `oscar/tests/insights_scenarios/*.json` (committed) |
| Proves | Insights hold up on messy real nights; catches regressions | Each definition gives the hand-computed answer |
| Expected values | `manifest.json`: reference values from the source summary tables, plus snapshots the test writes on first run | `expected` in each file, exact by construction |

Run both with `python3 -I` from the repository root.

## Real data: `anonymize_oscar_db.py`

```sh
python3 -I Tools/insights_testdata/anonymize_oscar_db.py \
    --src ~/Documents/OSCAR_Data/OSCAR20_Data --dst testdata/insights/real
```

The output folder is a complete OSCAR data folder with one profile, `Test`. It opens with
`OSCAR --datadir <abs path>/testdata/insights/real --profile Test`. Point OSCAR at a **copy**
of it: opening the folder writes to `oscar.db`.

What the script changes:

- **Timeline:** every timestamp moves back by a random whole number of weeks (52–260). The
  number is never written out. Weekdays and local time of night are kept. Event times inside
  the BLOBs are offsets from `event_lists.first_time`, so only columns change.
  ResMed-style `session_id`s, which are derived from the start time, move too.
- **Identity:**
  - name, DOB, contact details, doctor details, height and gender are replaced or blanked
  - the profile is renamed `Test`
  - serial numbers and `machine_id`s are replaced
  - file paths are blanked
  - audit timestamps (`created_at` and similar) are set to a constant
- **Size:** the per-breath and 25 Hz channels (flow, mask pressure, Ti/Te, respiratory rate,
  minute ventilation, tidal volume) are kept only on the case nights listed in the manifest.
  `--full` keeps them on every night. All event, pressure, leak and FLG data is kept.
- **Leak scan:** every identifying string from the source is searched for in the copy, in
  UTF-8 and UTF-16, across all text and BLOB columns except sensor samples. If any is found,
  the output is deleted and the script fails.

`manifest.json` lists:
- the case nights, under their shifted dates, with reference values
- the settings periods
- the US Medicare first-90-days result
- the nights that straddle a DST change

Reference values come from summary tables. Two schema details matter here:

- `session_channel_values.time_ms` actually holds **seconds**.
- A raw value multiplied by `session_channels.gain` gives cmH2O.

To share the real data with another developer, send `tar czf insights-real.tgz -C testdata
insights/real` privately. Don't attach it to an issue or merge request.

## Synthetic scenarios: `make_scenarios.py`

```sh
python3 -I Tools/insights_testdata/make_scenarios.py   # rewrites oscar/tests/insights_scenarios/
```

The output is deterministic: random series use fixed seeds, and a second run is
byte-identical. Commit the regenerated JSON together with any change to the script.

### Format (`format_version` 1)

```jsonc
{
  "format_version": 1,
  "id": "f5_odi",                       // = file name
  "feature": "F5",                      // PRD feature, or "all"
  "description": "...",
  "devices": [
    // event_time: how the device STORES events ("end" = ResMed, "start" = PRS1)
    {"id": "cpap1", "type": "CPAP", "loader": "ResMed", "event_time": "end"},
    // other types: OXIMETER, POSITION, SLEEPSTAGE
    {"id": "oxi1", "type": "OXIMETER", "loader": "Viatom"}
  ],
  "nights": [{
    "date": "2021-01-04",               // the OSCAR day (12:00 day split)
    "sessions": [{
      "device": "cpap1",
      "start": "2021-01-04T23:00:00",   // local time, no zone
      "hours": 8,
      "enabled": true,                  // optional, default true
      // OSCAR channel codes; PAPMode is "CPAP" | "APAP" | "BiLevelAuto"
      "settings": {"PAPMode": "APAP", "PressureMin": 6, "PressureMax": 14},
      "channels": {
        // Piecewise-linear [seconds from session start, value] points, sampled every
        // rate_s. Two points at the same time make an instant step. Gaps between
        // segments mean no samples.
        "SPO2": {"rate_s": 1, "segments": [[[0, 96], [600, 96], [610, 91], ...]]}
      },
      // Event ONSETS and durations in seconds. The builder converts them to the device's
      // event_time convention.
      "events": {"Obstructive": [[1800, 20]], "Hypopnea": [[3600, 25]]}
    }]
  }],
  "journal": [{"date": "2021-01-04", "weight_kg": 110}],    // optional
  "profile": {"leak_redline": 24},                          // optional preference overrides
  "orientation_convention": {"supine_deg": 0, "half_width_deg": 45},  // F6 only
  "expected": {"odi3": 0.625, "odi4": 0.375},
  "tolerance": {"odi4": 0.002}          // optional absolute tolerances; default exact to 4 dp
}
```

Channel codes match `schema.cpp`. `SleepStage` values are `SleepStageValue` negated
(-1 awake, -2 REM, -3 light, -4 deep), the same way loaders store them.

### Decisions the scenarios encode (confirm before implementing)

| Scenario | Assumption |
|---|---|
| `f2_auto_bilevel` | Events are binned by EPAP. Time at max compares IPAP with `IPAPHi`. |
| `f5_two_oximeters_one_night` | Use only the oximeter with the longest CPAP overlap. |
| `f6_*` | Somnopose's degree convention is unconfirmed: its app lets users adjust the angles, and the loader stores raw degrees. The scenarios declare 0° = supine, ±45°. Check against a real Somnopose export. |
| `f1_*` | Python and C++ bootstraps use different random generators, so only `label` and the pooled AHIs must match. Seeds are chosen so the label is well clear of the boundary. |
| `f4_medicare_*` | Windows are 30 consecutive nights, starting within the first 61 nights. `best_window_first_night` is the earliest window with the best count. |

A case from the PRD that has **no** scenario: "data starts after the first 90 days of use".
OSCAR can't tell first use from first imported night, so F4-lite (a) needs a decision first.
