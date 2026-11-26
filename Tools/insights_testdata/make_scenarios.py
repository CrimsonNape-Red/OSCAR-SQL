#!/usr/bin/env python3
"""Generate the synthetic Therapy Insights test scenarios.

Copyright (c) 2026 The OSCAR Team
This file is subject to the terms and conditions of the GNU General Public License.
See the file COPYING in the main directory of the source code for more details.

Each scenario covers a test case that real data rarely or never contains (see
Notes/Design/2026-10-17-therapy-insights-prd.md section 8.1). Signals are built from simple
shapes so every expected value can be checked by hand; the few random series use fixed
seeds, and their expected values are computed from the generated events themselves.
The JSON format is documented in Tools/insights_testdata/README.md.

Usage:  python3 -I make_scenarios.py [--out oscar/tests/insights_scenarios]
"""

import argparse
import json
import random
from datetime import date, datetime, timedelta
from pathlib import Path

BASE = date(2021, 1, 4)  # a Monday; all synthetic data lives in 2021
FORMAT_VERSION = 1
RESMED = {"id": "cpap1", "type": "CPAP", "loader": "ResMed", "event_time": "end"}
PRS1 = {"id": "cpap2", "type": "CPAP", "loader": "PRS1", "event_time": "start"}
OXI = {"id": "oxi1", "type": "OXIMETER", "loader": "Viatom"}
POS = {"id": "pos1", "type": "POSITION", "loader": "Somnopose"}
STAGES = {"id": "stage1", "type": "SLEEPSTAGE", "loader": "Dreem"}
APAP = {"PAPMode": "APAP", "PressureMin": 6, "PressureMax": 14}

# SleepStageValue (machine_common.h), stored negated in SLEEP_Stage.
AWAKE, REM, LIGHT, DEEP = -1, -2, -3, -4


def night_start(n):
    return datetime.combine(BASE + timedelta(days=n), datetime.min.time()) + timedelta(hours=23)


def session(device, start, hours, settings=None, channels=None, events=None, **flags):
    s = {"device": device["id"], "start": start.isoformat(), "hours": hours}
    if settings:
        s["settings"] = settings
    if channels:
        s["channels"] = channels
    if events:
        s["events"] = events
    s.update(flags)
    return s


def flat(value, hours, rate_s=2):
    """A channel holding one value for the whole session."""
    return {"rate_s": rate_s, "segments": [[[0, value], [hours * 3600, value]]]}


def steps(rate_s, *runs):
    """Piecewise-constant channel from (seconds, value) runs, back to back from t=0."""
    pts, t = [], 0
    for secs, value in runs:
        pts += [[t, value], [t + secs, value]]
        t += secs
    return {"rate_s": rate_s, "segments": [pts]}


def dip(t, base, depth, fall=10, hold=15, rise=10):
    """Flat-bottomed desaturation starting at t: the shape the ODI definition expects."""
    return [[t, base], [t + fall, base - depth], [t + fall + hold, base - depth],
            [t + fall + hold + rise, base]]


def trace(base, hours, shapes, rate_s=1):
    """Baseline signal with the given shapes spliced in (shapes must not overlap)."""
    pts = [[0, base]]
    for shape in sorted(shapes):
        pts += shape
    pts.append([hours * 3600, base])
    return {"rate_s": rate_s, "segments": [pts]}


def scenario(sid, feature, description, devices, nights, expected, **extra):
    return {"format_version": FORMAT_VERSION, "id": sid, "feature": feature,
            "description": description, "devices": devices, "nights": nights,
            "expected": expected, **extra}


def night(n, *sessions):
    return {"date": (BASE + timedelta(days=n)).isoformat(), "sessions": list(sessions)}


def random_events(rng, hours, per_hour, kinds):
    """Poisson-ish event count spread uniformly over the session; returns {kind: [[t, dur]]}."""
    count = sum(1 for _ in range(int(hours * 3600 / 30)) if rng.random() < per_hour / 120)
    out = {}
    for _ in range(count):
        kind = rng.choice(kinds)
        out.setdefault(kind, []).append([rng.randint(60, int(hours * 3600) - 60),
                                         rng.randint(10, 40)])
    return {k: sorted(v) for k, v in out.items()}


def series(rng, first_night, count, hours, per_hour, kinds, settings, device=RESMED):
    return [night(first_night + i, session(device, night_start(first_night + i), hours,
                                           settings, {"Pressure": flat(10, hours)},
                                           random_events(rng, hours, per_hour, kinds)))
            for i in range(count)]


def pooled(nights, kinds):
    events = sum(len(s.get("events", {}).get(k, [])) for n in nights for s in n["sessions"]
                 for k in kinds)
    hours = sum(s["hours"] for n in nights for s in n["sessions"])
    return events, hours


def bootstrap_diff(a, b, kinds, seed=1, reps=1000):
    """95 % interval of pooled-AHI(b) - pooled-AHI(a), resampling nights."""
    rng = random.Random(seed)
    diffs = []
    for _ in range(reps):
        ra = [rng.choice(a) for _ in a]
        rb = [rng.choice(b) for _ in b]
        ea, ha = pooled(ra, kinds)
        eb, hb = pooled(rb, kinds)
        diffs.append(eb / hb - ea / ha)
    diffs.sort()
    return diffs[int(0.025 * reps)], diffs[int(0.975 * reps) - 1]


def robust_series(label, mean_a, mean_b, kinds, n_a=30, n_b=30, hours=7):
    """Find the first seed whose bootstrap result sits well inside the wanted label."""
    for seed in range(1, 200):
        rng = random.Random(seed)
        a = series(rng, 0, n_a, hours, mean_a, kinds, APAP)
        b = series(rng, n_a, n_b, hours, mean_b, kinds, {**APAP, "PressureMin": 8})
        lo, hi = bootstrap_diff(a, b, kinds)
        ea, ha = pooled(a, kinds)
        eb, hb = pooled(b, kinds)
        point, margin = eb / hb - ea / ha, 0.25 * (hi - lo)
        ok = {"within": lo + margin < 0 < hi - margin and abs(point) < margin,
              "lower": hi < -margin, "higher": lo > margin}[label]
        if ok:
            return a, b, {"ahi_before": round(ea / ha, 4), "ahi_after": round(eb / hb, 4),
                          "label": label, "oracle_ci95_of_difference": [round(lo, 3),
                                                                        round(hi, 3)]}
    raise RuntimeError(f"no robust seed for {label}")


AHI_KINDS = ["Obstructive", "Hypopnea", "ClearAirway", "Apnea"]


def f1():
    out = []
    for sid, label, ma, mb in (("f1_real_drop", "lower", 6, 2),
                               ("f1_real_rise", "higher", 2, 6),
                               ("f1_no_change", "within", 3, 3)):
        a, b, exp = robust_series(label, ma, mb, ["Obstructive", "Hypopnea"])
        out.append(scenario(sid, "F1", f"30 + 30 nights, AHI ~{ma} then ~{mb} after a "
                            "PressureMin change. Bootstrap seeds differ between Python and C++, "
                            "so only the label and pooled AHIs must match; the oracle interval "
                            "is a guide.", [RESMED], a + b, exp))
    rng = random.Random(7)
    a = series(rng, 0, 30, 7, 3, ["Obstructive"], APAP)
    b = series(rng, 30, 3, 7, 3, ["Obstructive"], {**APAP, "PressureMin": 8})
    out.append(scenario("f1_too_few_nights", "F1", "Second period has 3 nights, below any "
                        "sensible minimum-days preference.", [RESMED], a + b,
                        {"label": "no comparison"}))
    return out


def f2():
    out = []
    out.append(scenario(
        "f2_pinned_at_max_half_night", "F2", "APAP 6-14: 10 cmH2O for 4 h, then pinned at 14.",
        [RESMED], [night(0, session(RESMED, night_start(0), 8, APAP,
                                    {"Pressure": steps(2, (14400, 10), (14400, 14))}))],
        {"time_at_max_pct": 50.0}, tolerance={"time_at_max_pct": 0.5}))

    # Pressure 8 for 2 h, 10 for 2 h, 12 for 4 h. Two events start at 8 but end at 10, so
    # binning by end time instead of onset gives a different answer.
    pressure = steps(2, (7200, 8), (7200, 10), (14400, 12))
    events = {"Obstructive": [[1800, 20], [3600, 20], [7185, 20], [7190, 20],
                              [9000, 15], [12000, 15]],
              "Hypopnea": [[15000 + i * 1500, 20] for i in range(8)]}
    expected = {"events_at_pressure_by_onset": {
        "8": {"events": 4, "hours": 2.0, "per_hour": 2.0},
        "10": {"events": 2, "hours": 2.0, "per_hour": 1.0},
        "12": {"events": 8, "hours": 4.0, "per_hour": 2.0}}}
    for dev, name in ((RESMED, "end"), (PRS1, "start")):
        out.append(scenario(
            f"f2_events_at_pressure_{name}_stamped", "F2",
            f"Same events, device stores them at their {name}. Onsets are given in the file; "
            "the builder converts them to the device's convention. Bins are 1 cmH2O.",
            [dev], [night(0, session(dev, night_start(0), 8, APAP, {"Pressure": pressure},
                                     events))], expected))
    out.append(scenario(
        "f2_fixed_cpap_hidden", "F2", "Fixed CPAP: time at max is meaningless and hidden.",
        [RESMED], [night(0, session(RESMED, night_start(0), 7, {"PAPMode": "CPAP",
                                                                 "Pressure": 10},
                                    {"Pressure": flat(10, 7)}))],
        {"time_at_max_valid": False, "events_at_pressure_valid": False}))
    bilevel = {"PAPMode": "BiLevelAuto", "EPAPLo": 6, "IPAPHi": 16, "PS": 4}
    out.append(scenario(
        "f2_auto_bilevel", "F2", "Auto-bilevel: events are binned by EPAP; time at max "
        "compares IPAP with IPAPHi (PRD decision to confirm).",
        [RESMED], [night(0, session(RESMED, night_start(0), 8, bilevel,
                                    {"EPAP": steps(2, (21600, 9), (7200, 12)),
                                     "IPAP": steps(2, (21600, 13), (7200, 16))},
                                    {"Obstructive": [[3600, 20], [7200, 20], [25000, 20]]}))],
        {"time_at_max_pct": 25.0,
         "events_at_pressure_by_onset": {"9": {"events": 2, "hours": 6.0,
                                               "per_hour": 0.3333},
                                         "12": {"events": 1, "hours": 2.0, "per_hour": 0.5}}},
        tolerance={"time_at_max_pct": 0.5, "per_hour": 0.001}))
    return out


def f3():
    leak = steps(2, (10800, 10), (1800, 30), (9000, 10))
    return [
        scenario("f3_redline_without_large_leak_channel", "F3",
                 "Device with no LargeLeak channel: leak above the 24 L/min redline for 30 min "
                 "of 6 h.", [PRS1], [night(0, session(PRS1, night_start(0), 6, APAP,
                                                      {"Leak": leak}))],
                 {"large_leak_pct": 8.333, "large_leak_source": "redline",
                  "flags": ["large_leak"]}, profile={"leak_redline": 24},
                 tolerance={"large_leak_pct": 0.05}),
        scenario("f3_disabled_session_excluded", "F3",
                 "6 h enabled session with 6 events, plus a disabled 1 h session with 10.",
                 [RESMED], [night(0,
                                  session(RESMED, night_start(0), 6, APAP,
                                          events={"Obstructive": [[i * 3000 + 600, 15]
                                                                  for i in range(6)]}),
                                  session(RESMED, night_start(0) + timedelta(hours=6.5), 1,
                                          APAP, events={"Obstructive": [[i * 300 + 60, 15]
                                                                        for i in range(10)]},
                                          enabled=False))],
                 {"ahi": 1.0, "hours": 6.0, "sessions": 1, "flags": []}),
    ]


def f4():
    out = []
    for sid, good, met in (("f4_medicare_just_met", 21, True),
                           ("f4_medicare_just_missed", 20, False)):
        nights = []
        for i in range(90):
            hours = 3.5
            if 10 <= i < 10 + good:
                hours = 4.0 if i == 10 else 6.0  # one night exactly on the 4 h boundary
            nights.append(night(i, session(RESMED, night_start(i), hours, APAP)))
        out.append(scenario(sid, "F4-lite", f"90 nights of 3.5 h except {good} consecutive "
                            "nights of >= 4 h (the first exactly 4.0 h).", [RESMED], nights,
                            {"medicare_met": met, "best_window_nights_ge_4h": good,
                             # earliest 30-night window holding all the good nights
                             "best_window_first_night": (BASE + timedelta(days=good - 20))
                             .isoformat() if met else None}))
    return out


def oxi_session(hours, spo2, pulse=None, start=None):
    ch = {"SPO2": spo2}
    if pulse:
        ch["Pulse"] = pulse
    return session(OXI, start or night_start(0), hours, channels=ch)


def f5():
    out = []
    shapes = [dip(600 * (i + 1), 96, d) for i, d in enumerate((5, 5, 5, 3.5, 3.5))]
    out.append(scenario(
        "f5_odi", "F5", "8 h of oximetry at 96 % with three 5 % dips and two 3.5 % dips, "
        "10 min apart (120 s baseline is exactly 96).", [RESMED, OXI],
        [night(0, session(RESMED, night_start(0), 8, APAP), oxi_session(8, trace(96, 8, shapes)))],
        {"odi3": 0.625, "odi4": 0.375, "valid_oximetry_hours": 8.0}))

    steps_spo2 = trace(96, 8, [[[3600, 96], [3600, 87], [3960, 87], [3960, 96]],
                               [[10000, 96], [10000, 89], [10240, 89], [10240, 96]]])
    out.append(scenario(
        "f5_t90_t88", "F5", "6 min at 87 % and 4 min at 89 % (instant steps) in 8 h.",
        [OXI], [night(0, oxi_session(8, steps_spo2))],
        {"t90_pct": 2.0833, "t88_pct": 1.25}, tolerance={"t90_pct": 0.02, "t88_pct": 0.02}))

    ends = [3600 + 600 * i + 20 for i in range(6)]  # six 20 s OAs starting at 3600 + 600i
    spo2 = trace(96, 8, [[[e, 96], [e + 20, 90], [e + 50, 96]] for e in ends])
    pulse = trace(60, 8, [[[e, 60], [e + 10, 72], [e + 30, 60]] for e in ends])
    out.append(scenario(
        "f5_hypoxic_burden_and_pulse_response", "F5",
        "Six OAs; each is followed by a triangular dip 96 -> 90 -> 96 over 50 s (area "
        "150 %.s) and a pulse rise 60 -> 72 -> 60 over 30 s.", [RESMED, OXI],
        [night(0, session(RESMED, night_start(0), 8, APAP,
                          events={"Obstructive": [[e - 20, 20] for e in ends]}),
               oxi_session(8, spo2, pulse))],
        {"hypoxic_burden_pct_min_per_h": 1.875, "delta_hr_bpm": 12.0, "overlap_hours": 8.0},
        tolerance={"hypoxic_burden_pct_min_per_h": 0.1, "delta_hr_bpm": 0.5}))

    hyp = [[1800, 20], [5400, 20], [9000, 20], [12600, 20]]
    shapes = [dip(1825, 96, 5), dip(5425, 96, 3.5), dip(12665, 96, 5)]
    out.append(scenario(
        "f5_desat_confirmed_hypopneas", "F5",
        "Four hypopneas: #1 followed 5 s later by a 5 % dip, #2 by a 3.5 % dip, #3 by none, "
        "#4 by a 5 % dip that starts 45 s after it ends (outside the 30 s window).",
        [RESMED, OXI], [night(0, session(RESMED, night_start(0), 8, APAP,
                                         events={"Hypopnea": hyp}),
                              oxi_session(8, trace(96, 8, shapes)))],
        {"desat_confirmed_hypopneas_3pct": 2, "desat_confirmed_hypopneas_4pct": 1,
         "index_3pct": 0.25, "index_4pct": 0.125}))

    gap = {"rate_s": 1, "segments": [
        [[0, 96]] + dip(1200, 96, 5) + [[7200, 96]],                 # 0-2 h
        [[10800, 96]] + dip(12000, 96, 5) + [[14400, 96], [14400, 0], [15000, 0],
                                              [15000, 96], [28800, 96]]]}  # 3-8 h, 10 min of 0
    out.append(scenario(
        "f5_gaps_and_invalid_samples", "F5",
        "1 h with no samples (2-3 h) and 10 min of 0 % readings (sensor off). Two valid 5 % "
        "dips. The drop to 0 must not count as a desaturation.", [OXI],
        [night(0, oxi_session(8, gap))],
        {"valid_oximetry_hours": 6.8333, "odi4": 0.2927}, tolerance={"odi4": 0.002}))

    out.append(scenario(
        "f5_oximetry_not_overlapping_cpap", "F5",
        "Oximeter recorded 2 h starting after the CPAP session ended.", [RESMED, OXI],
        [night(0, session(RESMED, night_start(0), 5, APAP,
                          events={"Obstructive": [[600, 20]]}),
               oxi_session(2, trace(96, 2, [dip(1200, 96, 5)]),
                           start=night_start(0) + timedelta(hours=5.5)))],
        {"odi4": 0.5, "hypoxic_burden_valid": False, "delta_hr_valid": False,
         "desat_confirmed_valid": False}))

    oxi2 = {**OXI, "id": "oxi2", "loader": "CMS50"}
    out.append(scenario(
        "f5_two_oximeters_one_night", "F5",
        "Two oximeters on one night: oxi1 overlaps CPAP for 6 h, oxi2 for 1 h. Use only the "
        "one with the longest overlap (PRD decision to confirm).", [RESMED, OXI, oxi2],
        [night(0, session(RESMED, night_start(0), 7, APAP),
               oxi_session(6, trace(96, 6, [dip(600, 96, 5), dip(1800, 96, 5)])),
               session(oxi2, night_start(0) + timedelta(hours=6), 1,
                       channels={"SPO2": trace(93, 1, [dip(600, 93, 5)])}))],
        {"oximeter_used": "oxi1", "odi4": 0.3333}, tolerance={"odi4": 0.001}))
    return out


def f6():
    convention = {"supine_deg": 0, "half_width_deg": 45}
    events = {"Obstructive": [[600 + 1100 * i, 20] for i in range(9)]               # supine
              + [[11400 + 3400 * i, 20] for i in range(5)]}                          # side
    return [
        scenario("f6_positional_split", "F6",
                 "3 h supine (0 deg) with 9 events, then 5 h on the side (90 deg) with 5. "
                 "The Somnopose degree convention is unconfirmed, so the scenario declares it.",
                 [RESMED, POS], [night(0, session(RESMED, night_start(0), 8, APAP,
                                                  events=events),
                                       session(POS, night_start(0), 8, channels={
                                           "Orientation": steps(5, (10800, 0), (18000, 90))}))],
                 {"ahi_supine": 3.0, "ahi_non_supine": 1.0, "supine_pct": 37.5},
                 orientation_convention=convention),
        scenario("f6_too_little_supine", "F6", "Only 20 min supine: split is hidden.",
                 [RESMED, POS], [night(0, session(RESMED, night_start(0), 8, APAP,
                                                  events={"Obstructive": [[600, 20]]}),
                                       session(POS, night_start(0), 8, channels={
                                           "Orientation": steps(5, (1200, 0), (27600, 90))}))],
                 {"positional_valid": False}, orientation_convention=convention),
    ]


def f7():
    # 8 h: 30 min awake, then 1.5 h REM spread over three 30 min blocks, 6 h NREM.
    stages = steps(30, (1800, AWAKE), (5400, LIGHT), (1800, REM), (5400, DEEP), (1800, REM),
                   (5400, LIGHT), (1800, REM), (5400, LIGHT))
    rem_starts = (7200, 14400, 21600)
    events = {"Obstructive": [[s + 300 + 500 * i, 20] for s in rem_starts for i in range(3)]
              + [[3000, 20], [10000, 20], [17000, 20]]}
    short = steps(30, (1800, AWAKE), (12600, LIGHT), (600, REM), (13800, DEEP))
    return [
        scenario("f7_rem_vs_nrem", "F7",
                 "1.5 h REM with 9 events, 6 h NREM with 3, 30 min awake (excluded).",
                 [RESMED, STAGES], [night(0, session(RESMED, night_start(0), 8, APAP,
                                                     events=events),
                                          session(STAGES, night_start(0), 8,
                                                  channels={"SleepStage": stages}))],
                 {"ahi_rem": 6.0, "ahi_nrem": 0.5, "rem_nrem_ratio": 12.0, "rem_hours": 1.5}),
        scenario("f7_too_little_rem", "F7", "Only 10 min of REM: split is hidden.",
                 [RESMED, STAGES], [night(0, session(RESMED, night_start(0), 8, APAP,
                                                     events={"Obstructive": [[15000, 20]]}),
                                          session(STAGES, night_start(0), 8,
                                                  channels={"SleepStage": short}))],
                 {"rem_nrem_valid": False}),
    ]


def f8():
    nights = []
    for i, hours in enumerate((8, 8, 5)):
        ev = [[h * 3600 + 600 + k * 600, 15] for h in range(int(hours)) for k in range(h % 3)]
        nights.append(night(i, session(RESMED, night_start(i), hours, APAP,
                                       events={"Obstructive": ev})))
    per_hour = [{"hour": h + 1, "events": (h % 3) * sum(1 for x in (8, 8, 5) if x > h),
                 "nights": sum(1 for x in (8, 8, 5) if x > h)} for h in range(8)]
    for row in per_hour:
        row["per_hour"] = row["events"] / row["nights"]
    return [scenario("f8_hourly_profile", "F8", "Hour h of the night has (h-1) mod 3 events; "
                     "the third night is only 5 h long.", [RESMED], nights,
                     {"hourly": per_hour})]


def f9():
    rng = random.Random(11)
    a = series(rng, 0, 30, 7, 2.5, ["Obstructive", "Hypopnea", "Hypopnea", "ClearAirway"],
               {**APAP, "PressureMin": 8, "PressureMax": 12})
    b = series(rng, 30, 30, 7, 8, ["Obstructive", "ClearAirway", "ClearAirway",
                                   "ClearAirway"], {**APAP, "PressureMin": 10,
                                                    "PressureMax": 15})
    obstructive = ["Obstructive", "Hypopnea"]

    def idx(ns, kinds):
        e, h = pooled(ns, kinds)
        return round(e / h, 4)
    # The seed must actually produce the pattern the scenario claims.
    ca, oa = idx(b, ["ClearAirway"]), idx(b, obstructive)
    assert ca >= 5 and ca > oa and oa < 5, (ca, oa)
    assert idx(a, ["ClearAirway"]) < 5, idx(a, ["ClearAirway"])
    out = [scenario(
        "f9_centrals_majority_after_increase", "F9",
        "Pressure range raised; centrals become the majority (CAHI >= 5, > 50 %, OAHI < 5).",
        [RESMED], a + b,
        {"periods": [{"cahi": idx(a, ["ClearAirway"]), "oahi": idx(a, obstructive),
                      "flag": False},
                     {"cahi": idx(b, ["ClearAirway"]), "oahi": idx(b, obstructive),
                      "flag": True}]})]
    ev = {"ClearAirway": [[600 + 1200 * i, 15] for i in range(18)],
          "CentralHypopnea": [[1000 + 1200 * i, 20] for i in range(18)],
          "Obstructive": [[1400 + 3600 * i, 15] for i in range(6)],
          "ObstructiveHypopnea": [[2000 + 3600 * i, 20] for i in range(6)]}
    out.append(scenario(
        "f9_device_scores_central_hypopneas", "F9",
        "PRS1-style device that splits hypopneas: CA 3/h + CH 3/h, OA 1/h + OH 1/h.",
        [PRS1], [night(i, session(PRS1, night_start(i), 6, APAP, events=ev)) for i in range(7)],
        {"periods": [{"cahi": 6.0, "oahi": 2.0, "central_pct": 75.0, "flag": True}]}))
    return out


def f10():
    return [
        scenario("f10_high_flow_limitation", "F10", "FLG 0.5 for 2 h of 8 h, 0 otherwise.",
                 [RESMED], [night(0, session(RESMED, night_start(0), 8, APAP,
                                             {"FLG": steps(2, (14400, 0), (7200, 0.5),
                                                           (7200, 0))}))],
                 {"fl_time_pct": 25.0}, tolerance={"fl_time_pct": 0.1}),
        scenario("f10_no_flg_channel", "F10", "Device without the FLG channel.", [PRS1],
                 [night(0, session(PRS1, night_start(0), 8, APAP))], {"fl_valid": False}),
    ]


def slope(points):
    """Least-squares slope of (x, y) points."""
    n = len(points)
    mx = sum(x for x, _ in points) / n
    my = sum(y for _, y in points) / n
    return sum((x - mx) * (y - my) for x, y in points) / sum((x - mx) ** 2 for x, _ in points)


def f11():
    weights = ((0, 110), (30, 107.5), (60, 105), (90, 102.5), (119, 100))
    nights = []
    for i in range(120):
        p = round(12 - 2 * i / 119, 4)
        nights.append(night(i, session(RESMED, night_start(i), 7, APAP,
                                       {"Pressure": flat(p, 7)})))
    journal = [{"date": (BASE + timedelta(days=d)).isoformat(), "weight_kg": w}
               for d, w in weights]
    return [
        scenario("f11_weight_vs_pressure", "F11",
                 "Nightly pressure falls linearly 12 -> 10 cmH2O over 120 nights while weight "
                 "falls 110 -> 100 kg. Change per 30 days = least-squares slope x 30.",
                 [RESMED], nights,
                 {"last_weight_kg": 100,
                  "weight_change_per_30d": round(30 * slope([(d, w) for d, w in weights]), 4),
                  "pressure_p95_change_per_30d": -0.5042},
                 journal=journal, tolerance={"weight_change_per_30d": 0.05,
                                             "pressure_p95_change_per_30d": 0.002}),
        scenario("f11_too_few_weights", "F11", "Only 3 weight entries: hidden.", [RESMED],
                 nights[:60], {"weight_pressure_valid": False}, journal=journal[:3]),
    ]


def general():
    return [
        scenario("general_empty_range", "all", "No nights at all: every insight is invalid, "
                 "nothing crashes.", [RESMED], [], {"all_valid": False}),
        scenario("general_two_cpap_devices", "all",
                 "Home device 6 h with 6 events, travel device 1 h with 4 events, same night.",
                 [RESMED, PRS1],
                 [night(0, session(RESMED, night_start(0), 6, APAP,
                                   events={"Obstructive": [[i * 3000 + 600, 15]
                                                           for i in range(6)]}),
                        session(PRS1, night_start(0) + timedelta(hours=6.25), 1, APAP,
                                events={"Obstructive": [[i * 600 + 60, 15]
                                                        for i in range(4)]}))],
                 {"ahi": 1.4286, "hours": 7.0}, tolerance={"ahi": 0.001}),
    ]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="oscar/tests/insights_scenarios")
    out = Path(ap.parse_args().out)
    out.mkdir(parents=True, exist_ok=True)
    scenarios = f1() + f2() + f3() + f4() + f5() + f6() + f7() + f8() + f9() + f10() + f11() \
        + general()
    for old in out.glob("*.json"):
        old.unlink()
    for s in scenarios:
        (out / f"{s['id']}.json").write_text(json.dumps(s, indent=1) + "\n")
    print(f"wrote {len(scenarios)} scenarios to {out}")


if __name__ == "__main__":
    main()
