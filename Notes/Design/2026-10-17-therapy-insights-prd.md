# Therapy Insights — product requirements

**Date:** 2026-10-17
**Status:** Phase 1 (F0–F4-lite) implemented; Phases 2 and 3 proposed
**Base:** `master` at `1817a307`

---

## 1. Problem

OSCAR shows the user what the device scored: AHI, OAHI/CAHI, leak, pressure percentiles,
flow limitation (FL), CSR/PB time, and oximetry drops and pulse changes. It also derives
respiratory rate, tidal volume, Ti/Te, minute ventilation and steady breathing from the flow
waveform (`FlowParser` in `oscar/SleepLib/calcs.cpp`).

It doesn't answer the questions users actually bring to forums:

- Did my settings change make a real difference, or is it night-to-night noise?
- Is my APAP pressure range too narrow?
- Can I trust tonight's AHI? (large leak, short sessions, summary-only data)
- How much are my events affecting my oxygen and heart rate?
- Are my events positional, or REM-related?
- Are central apneas showing up because of my therapy?
- Am I meeting the compliance requirement?

Volunteers on the forums work these out by hand from OSCAR screenshots. Every feature in this
PRD uses data OSCAR already imports. **No new loaders and no schema migration.**

### 1.1 Why now (clinical context)

- **Beyond AHI.** Hypoxic burden (Azarbarzin et al., *Eur Heart J* 2019) and the pulse-rate
  response to events (Azarbarzin et al., *AJRCCM* 2021) predict cardiovascular outcomes better
  than AHI. OSCAR already has events, SpO2 and pulse on one timeline.
- **Endotyping and new therapies.** Drug therapy (tirzepatide for OSA with obesity; the
  AD109 combination in late-stage development), hypoglossal nerve stimulation and positional
  therapy fit different patients. Positional, REM-related and central-emergent patterns help a
  user and their clinician have that conversation.
- **Weight loss on GLP-1 drugs** often lowers pressure needs over months. OSCAR stores weight
  in the journal and pressure per night, but never shows them together.

### 1.2 Users

- **New users, first 90 days.** They need to know whether they're compliant, whether therapy
  is working, and what to ask their clinician.
- **Experienced users** (ApneaBoard, r/SleepApnea). They want the raw metrics, the method
  behind each one, and the ability to check it against the graphs.

---

## 2. Principles and non-negotiables

1. **Descriptive, not prescriptive.** OSCAR must never tell a user to change a setting.
   Software that recommends a treatment change falls under medical-device regulation
   (FDA SaMD in the US, EU MDR in Europe). Wording rules:
   - Allowed: "Pressure was at the device maximum for 18% of the night."
   - Allowed: "This pattern is worth discussing with your clinician."
   - Not allowed: "Raise your maximum pressure to 14."
   - Not allowed: "You have treatment-emergent central sleep apnea."

   Every insight surface carries the existing OSCAR disclaimer tone. Pick the label words
   carefully; translators and reviewers will copy them.
2. **Gate on the data, not the brand.** A metric appears only when the day has the channels
   it needs. Follow the evidence-based approach of `Machine::reportsHypopneaMechanism()` (see
   `Notes/Design/2026-09-14-oh-ch-capability-gating-design.md`). Never hard-code a list of
   loaders.
3. **Show the method.** Every new number needs a tooltip or help text that names its
   definition and window, e.g. "ODI 3%: drops of ≥3% from a 120 s rolling baseline, lasting
   ≥10 s".
4. **No schema change.** Compute on the fly from `Day`/`Session` data already in memory. If a
   calculation proves too slow over long ranges, write a separate spec for caching it in
   `daily_summaries`. Don't add it here. (A schema bump would also collide with any other
   in-flight migration.)
5. **Pure functions first.** All calculations live in new, UI-free files and take
   `Day*`/`Session*` or plain vectors. That makes them unit-testable with synthetic data and
   keeps edits to shared UI files to a few lines.

---

## 3. Implementation notes (found while building Phase 1)

- **Event timestamps.** OSCAR stores a flag event at its **end**, with the duration in the
  event's value (ResMed). The PRS1 loader stores OA, CA and H as instantaneous flags with no
  duration. So onset = time − duration works for every loader without naming one, and
  matches `SearchEvent()` in `calcs.cpp`. `MinutesAtPressure` bins events at the stored time,
  without the offset.
- **Bilevel pressure channel.** `Day::getPressureChannelID()` prefers IPAP for bilevel modes.
  F2 bins events by EPAP, so it picks channels per mode itself. ResMed bilevels store IPAP
  as `CPAP_Pressure` ("Press.2s") with no `CPAP_IPAP` channel, so time at max falls back to
  `CPAP_Pressure` the same way `getPressureChannelID()` does.
- **Clock-drift correction.** `Session::SearchValue()` subtracts the device-time correction.
  Event and waveform times within one session are both raw device times, so F2 looks values
  up with a raw-time helper instead.
- **Summaries are enough for some metrics.** `Session::timeAboveThreshold()` returns minutes
  and uses the stored value histogram when it exists, so time at max pressure needs no
  waveform loading. Events at pressure does load events, one session at a time, and stays
  well inside the 50 ms-per-night budget (§5).
- **One device per type per night.** `Day::addSession()` refuses a second CPAP or oximeter
  on the same night, so `general_two_cpap_devices` and `f5_two_oximeters_one_night` can't be
  computed from a `Day` as they stand (§10).
- **Large leak.** At import, `flagLargeLeaks()` turns leak above the redline into LargeLeak
  spans when the device doesn't flag large leak itself (§10).

---

## 4. Conventions followed

- Every user-visible string goes through `tr()`. `Translations/*.ts` are left for the
  maintainers' batch `lupdate` run.
- `Htmldocs/release_notes.html` is left to the maintainers; each merge request carries a
  short user-facing summary they can paste in.
- Thresholds are named constants in `insights.h`, each with a comment saying it could become a
  preference. No preference UI is added.
- Edits to existing files are small additions; the logic lives in new files.
- One merge request per phase (§7).

---

## 5. Architecture

```
oscar/SleepLib/insights.h/.cpp   NEW  pure calculations, no Qt widgets, no p_profile writes
oscar/tests/insightstests.h/.cpp NEW  QTest, synthetic Sessions/EventLists, no testdata/ needed
oscar/insightspage.h/.cpp        NEW  the Insights tab (2.2 UI kit), opened from the Welcome
                                      page's Insights tile
oscar/statistics.h               EDIT settingsPeriods(): the "Changes to Device Settings" periods
oscar/mainwindow.cpp/.h          EDIT the tab, JumpInsights()
oscar/welcomepage.cpp/.h         EDIT the Insights tile opens the tab
oscar/oscar.pro                  EDIT add the new files
```

- **Where the results show (2026-11-24, agreed on !53):** Phase 1 shows on its own Insights
  page, not in Statistics or Daily. Statistics is regenerated whenever Daily opens a day, and
  events by pressure loads every session's events in its range, so it must never run from
  there. The page calculates only when it is shown, and again only after the data changes.
  Settings changes cover the whole history; pressure, data quality and usage cover the last
  90 days to the last CPAP night.

- `insights.h` exposes free functions in `namespace Insights` (this matches the free functions
  in `calcs.h`; there is one implementation, so no class hierarchy). Each function returns a
  small struct with a `bool valid` and the values, so callers can hide the row when data is
  missing.
- Read data only through existing APIs. On `Day`: `count`, `sum`, `cph`, `sph`,
  `calcAHI`/`calcOAHI`/`calcCAHI`, `calcPON`, `timeAboveThreshold`, `timeBelowThreshold`,
  `percentile`, `hours`, `summaryOnly`, `getCPAPMode`, `settings_max`/`settings_min`. On
  `Session`: `eventlist`, `channelDataExists`, `first`/`last`. On `EventList`: `count`,
  `time(i)`, `data(i)`, `raw(i)`, `rate`. Check the exact signatures in `day.h`, `session.h`
  and `event.h` before using them.
- **Event timestamp semantics.** See §3: onset = stored time − duration. Any feature that
  aligns events with pressure, SpO2 or pulse uses that rule, says so in its help text, and is
  tested with both end-stamped and instantaneous events.
- **Oximeter–CPAP clock alignment.** F4 and F5 need both on one timeline. OSCAR has
  device-time correction (`devicetimecorrectiondialog.*`, `driftanalysisdialog.*`,
  `Notes/Design/Clock Drift Plan.md`). Use the corrected times OSCAR already applies.
  Hide cross-device metrics when the day has no oximeter session overlapping a CPAP session by
  ≥ 1 h.
- **Performance:** Statistics covers ranges of up to years. Anything that walks waveforms
  (F4, F5) must work per day, reuse data already loaded, and record timing under
  `PERF_TIMER_SCOPE` (see `PERFORMANCE_INSTRUMENTATION_GUIDE.md`). Budget: < 50 ms per night on
  an M-series Mac. If it can't meet that, show it on Daily only and note it for the caching
  spec.

---

## 6. Existing settings to reuse

| Need | Existing source |
|---|---|
| Compliance hours threshold | `p_profile->cpap->complianceHours()` (used by `SC_DAYS_GE_COMPLIANCE_HOURS`) |
| Minimum days before comparing prescriptions | No preference exists (the "%1 days" message and its `rxthresh = 5` are commented out). F1 uses the constant `Insights::kRxMinNights` = 5; see §10. |
| Upper percentile | `p_profile->general->prefCalcPercentile()` |
| SpO2 drop / pulse change thresholds | `p_profile->oxi->spO2DropPercentage()`, `spO2DropDuration()`, `pulseChangeBPM()`, `pulseChangeDuration()` (`profiles.h` ~581). The new ODI 3%/4% use fixed standard definitions (F4) so they can be compared with published values. Explain the difference from the user-configurable `SPO2Drop` row in the help text. |
| Day split, session enable/disable | Already applied by `Day`; respect `Session::enabled()`. |

---

## 7. Features

Each feature lists the user value, definition, data gate, where it appears, acceptance
criteria and tests. Tick the boxes as you go.

**The scenario files are the acceptance tests.** A feature is done when every
`oscar/tests/insights_scenarios/<feature>_*.json` passes, the real-data snapshot looks right
(§8.2), and the build is warning-free. Where an "Accept" line below differs from a scenario
file, the scenario file wins: its numbers were verified independently. The test names below
are suggestions; the data-driven test in §8.1 replaces most of them.

### Phase 0 — test harness (first commits of MR 1)

#### F0. Insights test scaffolding — [x]
- [x] Test data: synthetic scenarios and the real-data anonymizer (§8.1, §8.2). Built
  2026-10-17. `.gitignore` ignores `testdata/` and allows `oscar/tests/insights_scenarios/*.json`.
- `oscar/tests/insightstests.{h,cpp}` with the scenario loader (§8.1), added to `oscar.pro`'s
  test block next to `tests/machinetests.*`.
- `testRealDataSnapshot` (§8.2), which `QSKIP`s when `testdata/insights/` is absent.
- **Accept:** `./test` passes with and without `testdata/insights/` present, and the
  pre-existing failure list is unchanged.

### Phase 1 — needs only CPAP data (MR 1)

#### F1. Settings-change comparison with noise bands — [x]
- **Value:** stops users chasing differences that are just night-to-night noise.
- **Definition:**
  - Periods are the prescription periods `Statistics::updateRXChanges()` already builds
    (`RXItem`). Read them; don't modify `updateRXChanges`.
  - For each period and the one before it, the pooled AHI is total events ÷ total hours.
  - The 95 % interval of the difference comes from a **bootstrap over nights**: 1,000
    resamples with a fixed seed, so output is deterministic. A bootstrap handles nights being
    noisier than a plain Poisson count (overdispersion).
  - Label: "lower" or "higher" only when the interval excludes 0 **and** the change is at
    least `Insights::kMinAhiChange` (1 event/h; added after review on !85, since long periods
    make the interval narrow enough to flag a tiny difference). Otherwise "within" (the page
    says "a change of less than 1 events/h" when the interval excludes 0), or "no
    comparison".
  - A period that starts on a different device is not compared: the page marks "Device
    changed", since devices score events differently.
  - No comparison when either period has fewer days than the minimum-days pref in §6.
- **Gate:** CPAP days with event data.
- **Surface:** Insights page, "Settings changes": one row per "Changes to Device Settings"
  period, newest first, with the changed setting marked, a verdict and the interval.
- **Accept:** synthetic data with identical distributions → label is "within variation".
  Halving the rate across 30 + 30 nights → "lower". A period of 3 nights → no comparison.
- **Test:** `InsightsTests::testRxComparisonNoise`, `testRxComparisonRealDrop`,
  `testRxComparisonTooFewNights`.

#### F2. APAP pressure-range hints — [x]
- **Value:** shows when the maximum is too low (time pinned at max) or the minimum is too low
  (obstructive events beginning near min). Forum volunteers currently work this out by hand.
- **Definition:**
  - (a) % of mask-on time with pressure ≥ the session's max setting − 0.2 cmH2O. The
    tolerance covers device quantization; keep it a named constant. Use
    `Day::timeAboveThreshold` and check whether it returns minutes or hours.
  - (b) Obstructive events (OA, H, OH, UA, and A where unclassified) per hour, by pressure at
    event onset, in 1 cmH2O bins. Normalize by minutes spent in each bin, so a pressure the
    machine rarely visits doesn't look alarming.
  - Pressure channel: `Day::getPressureChannelID()` (EPAP for bilevel).
- **Gate:** APAP or auto-bilevel modes only (`getCPAPMode()`). Hide in fixed CPAP mode,
  where (a) is meaningless.
- **Surface:** Insights page, "Pressure": % time at max pressure and the top-3 bins for
  events at pressure, over the page's 90 days.
- **Accept:** a synthetic session with pressure pinned at max for half the night → 50 % ± 1.
  Fixed CPAP mode → row hidden. Event-offset handling is verified against a ResMed-style
  end-timestamped event.
- **Test:** `testTimeAtMaxPressure`, `testEventsAtPressureNormalized`,
  `testEventOnsetOffset`, `testHiddenForFixedCpap`.

#### F3. Data-quality flags on AHI — [x]
- **Value:** a low AHI on a night with 40 minutes of large leak shouldn't look green. ResMed
  doesn't score events during large leak.
- **Definition:** one or more flags per night:
  - large leak > 5 % of mask-on time (`calcPON(CPAP_LargeLeak)`; for devices with no
    LargeLeak channel, leak above the redline)
  - total usage < compliance hours
  - more than 3 sessions (fragmentation)
  - summary-only data (`summaryOnly()`)

  Keep the thresholds as named constants that could become preferences.
- **Gate:** each flag only where its channel exists.
- **Surface:** Insights page, "Nights to read with care": nights with each flag over the
  page's 90 days.
- **Accept:** each flag fires on its synthetic case and stays silent on a clean night.
- **Test:** `testQualityFlags*`.

#### F4-lite. Adherence patterns — [x]
- **Definition:**
  - (a) **US Medicare first-90-days check:** whether some window of 30 consecutive days
    within the first 90 days after first use has ≥ 4 h on ≥ 70 % of the nights (21 of 30).
    Show the best window, and its dates if met. This must use 4 h regardless of the user's
    compliance-hours pref, since it's a fixed rule. Label it as the US Medicare criterion.
  - (b) Median mask-off clock time and its spread.
  - (c) Median sessions per night.
- **Gate:** CPAP. (a) appears only when first use falls inside the profile's data.
- **Surface:** Insights page, "Usage pattern". (a) only for profiles whose country is the
  United States, and only until a year after the first night (agreed after review on !85).
- **Accept:** `f4_medicare_just_met` (21 good nights, one exactly 4.0 h) and
  `f4_medicare_just_missed` (20).
- **Test:** `testMedicareWindowMet`, `testMedicareWindowNotMet`.

### Phase 2 — needs an oximeter, position or sleep-stage data (MR 2)

#### F5. Oximetry burden — [ ]
- **Definitions:**
  - **ODI 3 % / ODI 4 %:** drops ≥ 3 % (4 %) below a baseline lasting ≥ 10 s, per hour of
    valid oximetry. Baseline = mean SpO2 of the preceding 120 s. Ignore samples outside
    50–100 % and gaps.
  - **T90 / T88:** % of valid oximetry time below 90 % (88 %), via `Day::timeBelowThreshold`.
  - **Hypoxic burden (approximation):** for each respiratory event, the area between the
    pre-event baseline (max SpO2 in the 100 s before event end) and the SpO2 curve, from event
    start to event end + 60 s. Sum it and divide by hours of overlap, giving %·min/h. Say in
    the help text that this is a fixed-window approximation of Azarbarzin 2019's
    ensemble-averaged window, not the validated method. The values are comparable over time
    for one user, not against published cut-offs.
  - **Pulse-rate response (ΔHR):** mean over events of (max pulse from event end to + 30 s)
    − (mean pulse in the 30 s before event start). Same caveat (Azarbarzin 2021).
  - **Desat-confirmed hypopnea index:** hypopneas (H, OH, CH) followed within 30 s of their
    end by a ≥ 3 % (and separately ≥ 4 %) ODI drop, per hour. Explain that devices score
    hypopneas from flow alone, whereas a PSG requires a desaturation or an arousal.
- **Gate:**
  - ODI and T90 need SpO2.
  - Burden, ΔHR and desat-confirmed index need SpO2 (+ pulse for ΔHR) overlapping CPAP for
    ≥ 1 h.
  - Never mix data from two oximeters on one night.
- **Surface:** a section on the Insights page (see "Where the results show" in §5).
- **Accept:** the seven `f5_*` scenarios.
- **Test:** one test per definition, plus a gap/invalid-sample test.

#### F6. Positional AHI — [ ]
- **Data reality:** only `somnopose_loader.cpp` writes `POS_Orientation`/`POS_Inclination`
  today. Viatom writes `POS_Movement` only. The feature is narrow, but cheap to build.
- **Definition:** supine = orientation within ±45° of the supine value. The loader stores
  Somnopose's raw degrees, and the app lets users change its angles, so the convention is
  unconfirmed. Implement it as a named constant, use 0° to match the
  `f6_*` scenarios' declared convention, and list it as an open question (§10). Report AHI supine, AHI
  non-supine and % of time supine. Show nothing when supine or non-supine time is < 30 min.
- **Gate:** `MT_POSITION` data overlapping CPAP for ≥ 1 h.
- **Accept / Test:** synthetic orientation series plus events → expected split.

#### F7. REM vs NREM AHI (sleep stages) — [ ]
- **Data reality:** Zeo, Dreem and Apple Health import `MT_SLEEPSTAGE` data
  (`SleepStageValue` in `machine_common.h`).
- **Definition:** AHI during REM vs NREM epochs and the ratio. A REM/NREM ratio ≥ 2 with
  NREM AHI < 5 is a recognised REM-related OSA pattern. **Describe the numbers only**; don't
  name a diagnosis.
- **Gate:** stage data overlapping CPAP for ≥ 1 h, with ≥ 15 min of REM.
- **Accept / Test:** synthetic stages plus events → expected per-stage AHI.

#### F8. Hourly event profile — [ ]
- **Definition:** events per hour, bucketed by hours since the night's first mask-on (or by
  thirds of the night), across all nights in the range. Each bucket shows its count of
  contributing nights.
- **Gate:** CPAP.
- **Surface:** a 1-row-per-bucket table on the Insights page. A graph comes later (§9).

### Phase 3 — patterns across time (MR 3)

#### F9. Central emergence watch — [ ]
- **Value:** central apneas that rise after a pressure increase, or in the first weeks of
  therapy, change what the clinician does, and users miss them.
- **Definition:**
  - CAHI per week since first use.
  - Per prescription period (F1), flag when CAHI ≥ 5 **and** central events are > 50 % of all
    events while OAHI < 5. These are the ICSD-3 numeric thresholds, but report them only as
    "central events were the majority this period".
  - Separately, flag a period whose CAHI interval (F1 method) is higher than the previous
    period's, after a pressure increase.
- **Gate:** CPAP with CA scoring. Use CH as well where `reportsHypopneaMechanism()`.
- **Wording:** "Central events made up 62 % of events this period (CAHI 6.1). Worth
  mentioning to your clinician." Never use "TECSA" or "complex apnea" in the UI.

#### F10. Flow-limitation burden — [ ]
- **Definition:** % of mask-on time with the FL graph channel (`FLG`) ≥ 0.3, and the median
  per-night value, via `timeAboveThreshold`. Keep the threshold a named constant.
- **Gate:** the `FLG` waveform exists (mostly ResMed).

#### F11. Weight vs pressure — [ ]
- **Definition:** pair `Journal_Weight` (journal entries) with each night's upper-percentile
  pressure. Show the last value and the change per 30 days for both, over the range. No
  correlation coefficient: with so few journal points it would mislead.
- **Gate:** ≥ 4 weight entries in the range, and APAP.

#### F12. Welcome-page summary — [ ] *(optional, last)*
- **Definition:** three rule-based lines on the Welcome page from F1–F3 and F9, e.g.
  "Since your settings change on 3 Oct, AHI is within night-to-night variation."
- **Gate:** only once F1–F3 have shipped. Keep the `welcome.cpp` change to one new function
  and one call.

---

## 8. Test plan

Testing has two layers. Synthetic scenarios prove each definition is correct. Real data proves
it holds up on messy nights and catches regressions. **Real CPAP data is never committed:**
`Notes/` and the repo are public, and SD-card files carry serial numbers, dates and health
data.

### 8.1 Synthetic scenarios (committed, run everywhere) — part of every feature's MR

- **New file `oscar/tests/insightstests.{h,cpp}`**, registered with `DECLARE_TEST` (see
  `machinetests.h`). Build `Session`/`EventList` objects in code. Don't depend on
  `testdata/`, which isn't in the repo.
- **The scenarios already exist** (built 2026-10-17): `Tools/insights_testdata/make_scenarios.py`
  has a function per feature and writes `oscar/tests/insights_scenarios/`. Only the
  implemented features' files are committed (Phase 1 and the cross-feature `general_*`: 17
  files); each later feature adds its function's output to `main()` in its own MR.
  `Tools/insights_testdata/README.md` documents the format and the decisions the scenarios
  encode. Each expected value was checked by an independent 1 Hz sampling script.
  `insightstests.cpp` needs one small loader that turns a scenario file into
  `Session`/`EventList` objects. Locate the files with `QFINDTESTDATA`. The loader must:
  - sample each piecewise-linear channel at `rate_s`, treating gaps between segments as no
    data
  - store events at onset or onset + duration, according to the device's `event_time`
  - apply the `expected` values with each file's `tolerance`

  Loop over the files with one data-driven QTest (`_data()` rows keyed by scenario `id`), so
  adding a scenario needs no new C++.
- **One small test per definition branch** (listed per feature). Use fixed bootstrap seeds so
  results are deterministic.
- **Scenarios that real data rarely covers, so synthetic tests must:**

  | Scenario | Features |
  |---|---|
  | Identical vs. genuinely halved AHI across a settings change; a period that's too short | F1 |
  | Pressure pinned at max; fixed-CPAP mode; bilevel (EPAP channel) | F2 |
  | ResMed-style events timestamped at their end vs. start-timestamped | F2, F5 |
  | Large leak > 5 %, short night, > 3 sessions, summary-only | F3 |
  | 90-day series that just meets / just misses 21-of-30 | F4-lite |
  | SpO2 dips of known depth, gaps, invalid samples, oximeter not overlapping CPAP | F5 |
  | Supine/non-supine split; < 30 min in one position | F6 |
  | REM/NREM epochs with known event placement; < 15 min REM | F7 |
  | Centrals becoming the majority after a pressure increase | F9 |

### 8.2 Real-data regression (local only, never committed) — Phase 0 deliverable

The existing tests read `./testdata/` (`#define TESTDATA_PATH "./testdata/"`, e.g.
`prs1tests.cpp`), which developers obtain separately. Follow that pattern, with one fix:
**`QSKIP` when the folder is missing** instead of failing. Missing data is the cause of most
of the 7 pre-existing failures.

- **Input:** `testdata/insights/real/`, an anonymized OSCAR data folder (profile `Test`)
  built by `Tools/insights_testdata/anonymize_oscar_db.py`. The folder's `manifest.json` lists
  the case nights (shifted dates) with reference values, the settings periods, the Medicare
  result and the DST nights; read it to learn what the local data covers. The test must open a
  **temporary copy**, because OSCAR writes to `oscar.db` on open.
- **Schema detail:** `session_channel_values.time_ms` holds **seconds** at each raw value, not
  milliseconds. Multiply the value by `session_channels.gain` to get physical units.
- **Test:** `InsightsTests::testRealDataSnapshot`. It runs every insight over every day and
  writes one YAML file per night to `testdata/insights/output/`, copying the prs1 tests'
  `output/` YAML pattern. On later runs it compares against the stored files and fails on any
  difference.
- **First run:** the developer reviews a handful of nights by hand against the Daily graphs
  (large-leak night, settings-change week, an oximetry night). Only then are the snapshots
  accepted as the baseline. Snapshots catch regressions; they don't prove correctness.
- **Coverage note:** before relying on it, list in the MR description which scenarios in
  §8.1's table the local data actually covers (device and mode, oximetry yes/no, number of
  settings changes, any large-leak nights). Don't include dates or serials.

### 8.3 Manual and regression runs
- **Manual check** for each feature on real data:
  - a ResMed card (25 Hz flow, FLG, LargeLeak)
  - a PRS1 card (5 Hz flow, OH/CH on DreamStation 2)
  - one night with oximetry

  Compare each new number with the graphs on Daily. Record anomalies in the MR description,
  without identifiers.
- **Regression:** the full `./test` run. The failures that already occur on `master` (7 on
  macOS, from the missing `testdata/` folder and `/private` temp paths) must be unchanged.

---

## 9. Out of scope (each needs its own spec)

- Caching insight results in `daily_summaries` (needs a schema migration).
- New graphs, e.g. events-at-pressure across days, or weight vs pressure. These touch
  `Graphs/*`, which other in-flight work edits heavily.
- Preference UI for the threshold constants in `insights.h`.
- Printed-report and CSV-export output for insights.
- **Research tier** — each needs PSG validation before any user sees it:
  - flow-shape flow-limitation scoring from the raw waveform
  - loop-gain estimation from periodic breathing
  - arousal estimation from recovery breaths and pulse spikes
- Apple Health HRV/temperature correlations (the data is noisy and sparse; revisit after F11
  ships).
- LLM- or cloud-generated summaries (privacy; and against principle 1 in §2).

---

## 10. Open questions for the maintainers (raise in MR 1's description)

1. ~~Should insights live in a new "Therapy Insights" section on Statistics?~~ Settled on !53
   (2026-11-24): on their own Insights page, opened from the Welcome page.
2. Is the Medicare criterion (F4-lite a) acceptable in an international app, if it is labelled
   as US-specific?
3. Would they prefer insight thresholds become preferences now, rather than in a follow-up?
4. Is a separate "first 90 days" view wanted? This PRD deliberately doesn't build one.
5. The scenario files encode several assumptions; see the decision table in
   `Tools/insights_testdata/README.md`:
   - auto-bilevel time-at-max uses IPAP
   - with two oximeters on one night, use the one with the longest overlap
   - the Somnopose supine convention
   - how first use is determined for the Medicare check

   Confirm these, or change both the scenario and the code.
6. F1 needs a minimum number of nights before comparing two settings periods. §6 assumed a
   preference for it, but none exists: the "Efficacy highlighting ignores prescription
   settings with less than %1 days" message in `statistics.cpp` is commented out, along with
   its `rxthresh = 5`. F1 uses a constant of 5 (`Insights::kRxMinNights`). Should it become a
   preference?
7. F3's large-leak source can't be reported. At import, `flagLargeLeaks()` turns leak above
   the redline into LargeLeak spans for devices that don't flag large leak, so afterwards
   device-flagged and redline-derived large leak look the same. F3 uses LargeLeak when the
   device has reported it, otherwise time above the redline, and doesn't check the
   `large_leak_source` value in `f3_redline_without_large_leak_channel`. Is that acceptable?
8. A `Day` holds one device per type, so a second CPAP or oximeter on the same night is
   dropped (§3). Should insights combine devices of one type (`general_two_cpap_devices`
   expects AHI over both CPAPs; `f5_two_oximeters_one_night` picks the longer overlap), or
   follow OSCAR and use one?
