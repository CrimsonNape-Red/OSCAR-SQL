#!/usr/bin/env python3
"""Make an anonymized copy of an OSCAR data folder for the Therapy Insights tests.

Copyright (c) 2026 The OSCAR Team
This file is subject to the terms and conditions of the GNU General Public License.
See the file COPYING in the main directory of the source code for more details.

What it does (see Notes/Design/2026-10-17-therapy-insights-prd.md section 8.2):
  * snapshots <src>/oscar.db (safe while OSCAR is running; the source is opened read-only)
  * picks the nights that cover each real-data test case and writes manifest.json with
    reference values computed straight from the source tables
  * shifts every timestamp back by a random whole number of weeks (weekday and local time of
    night are kept; the shift is not recorded anywhere)
  * scrubs names, DOB, doctor details, serial numbers, file paths and audit timestamps
  * keeps the per-breath and 25 Hz channels only on the case nights (--full keeps all)
  * fails if any identifier from the source is still present anywhere in the copy

Usage:
  python3 -I anonymize_oscar_db.py --src ~/Documents/OSCAR_Data/OSCAR20_Data \
      --dst testdata/insights/real
"""

import argparse
import json
import random
import re
import shutil
import sqlite3
import sys
from datetime import date, datetime, timedelta
from pathlib import Path
from zoneinfo import ZoneInfo

HEAVY_CHANNELS = ("FlowRate", "MaskPressureHi", "MaskPressure", "Te", "Ti", "RespRate",
                  "MinuteVent", "TidalVolume")
APNEA_CHANNELS = ("Obstructive", "Hypopnea", "ClearAirway", "Apnea")
IDENTITY_PREFS = ("FirstName", "LastName", "DOB", "Address", "Phone", "EmailAddress",
                  "DoctorName", "DoctorAddress", "DoctorEmail", "DoctorPatientID",
                  "DoctorPhone", "DoctorPractice", "UserName", "Password")
PREF_OVERRIDES = {"FirstName": "Test", "LastName": "User", "UserName": "Test",
                  "DOB": "1980-01-01", "Height": "175", "Gender": "0", "Address": "",
                  "Phone": "", "EmailAddress": "", "DoctorName": "", "DoctorAddress": "",
                  "DoctorEmail": "", "DoctorPatientID": "", "DoctorPhone": "",
                  "DoctorPractice": "", "Password": "", "LastCPAPPath": "",
                  "LastExportCsvPath": "", "LastSDCardSerial": "", "CPAPNotes": "",
                  "MaskDescription": ""}
DATE_RE = re.compile(r"^(\d{4}-\d{2}-\d{2})(.*)$")
AUDIT_TS = "2000-01-01 00:00:00"
TIME_AT_MAX_TOLERANCE = 0.2  # cmH2O, same as PRD F2
DAY_MS = 86400 * 1000


def q(conn, sql, args=()):
    return conn.execute(sql, args).fetchall()


def channel_ids(conn):
    return {code: cid for cid, code in q(conn, "SELECT channel_id, channel_code FROM channels")}


def night_of(start_ms, tz):
    """OSCAR's default day split: a session belongs to the date 12 h before its start."""
    return (datetime.fromtimestamp(start_ms / 1000, tz) - timedelta(hours=12)).date()


def collect_nights(conn, tz):
    """Per-night reference values, computed from summary tables only (no blob decoding)."""
    ch = channel_ids(conn)
    sessions = q(conn, """SELECT s.id, s.start_time, s.end_time, s.duration, s.summary_only FROM sessions s
                          JOIN machines m ON m.id = s.machine_id
                          WHERE m.machine_type = 1 AND s.enabled = 1 ORDER BY s.start_time""")
    settings = {}
    for sid, cid, val in q(conn, "SELECT session_id, channel_id, value FROM session_settings"):
        settings.setdefault(sid, {})[cid] = val
    counts = {}
    for sid, cid, cnt, total, p95 in q(conn, "SELECT session_id, channel_id, count, sum, p95 "
                                             "FROM session_channels"):
        counts[(sid, cid)] = (cnt or 0, total or 0, p95 or 0)

    # session_channel_values.time_ms holds SECONDS at each raw value (value * gain = cmH2O).
    at_max = {}
    for sid, gain, value, secs in q(conn, """
            SELECT sc.session_id, sc.gain, scv.value, scv.time_ms FROM session_channel_values scv
            JOIN session_channels sc ON sc.id = scv.session_channel_id
            WHERE sc.channel_id = ?""", (ch.get("Pressure"),)):
        pmax = settings.get(sid, {}).get(ch.get("PressureMax"))
        tot, hit = at_max.get(sid, (0, 0))
        hit += secs if pmax is not None and value * gain >= pmax - TIME_AT_MAX_TOLERANCE else 0
        at_max[sid] = (tot + secs, hit)

    nights = {}
    for sid, st, et, dur, summ in sessions:
        n = nights.setdefault(night_of(st, tz), {"session_ids": [], "seconds": 0,
                                                 "summary_only_sessions": 0, "events": {},
                                                 "large_leak_seconds": 0, "pressure_seconds": 0,
                                                 "at_max_seconds": 0, "flg_p95_max": 0,
                                                 "pressure_max_setting": None, "first_start": st,
                                                 "last_end": et})
        n["session_ids"].append(sid)
        n["seconds"] += dur / 1000
        n["summary_only_sessions"] += summ
        n["last_end"] = max(n["last_end"], et)
        for code in APNEA_CHANNELS + ("RERA",):
            n["events"][code] = n["events"].get(code, 0) + counts.get((sid, ch.get(code)), (0,))[0]
        n["large_leak_seconds"] += counts.get((sid, ch.get("LeakSpan")), (0, 0))[1]
        n["flg_p95_max"] = max(n["flg_p95_max"], counts.get((sid, ch.get("FLG")), (0, 0, 0))[2])
        tot, hit = at_max.get(sid, (0, 0))
        n["pressure_seconds"] += tot
        n["at_max_seconds"] += hit
        n["pressure_max_setting"] = settings.get(sid, {}).get(ch.get("PressureMax"),
                                                              n["pressure_max_setting"])
    for n in nights.values():
        hours = n["seconds"] / 3600
        n["hours"] = round(hours, 3)
        n["sessions"] = len(n["session_ids"])
        n["ahi"] = round(sum(n["events"][c] for c in APNEA_CHANNELS) / hours, 3) if hours else None
        n["large_leak_pct"] = round(100 * n["large_leak_seconds"] / n["seconds"], 2) \
            if n["seconds"] else 0
        n["time_at_max_pct"] = round(100 * n["at_max_seconds"] / n["pressure_seconds"], 2) \
            if n["pressure_seconds"] else None
        n["flg_p95_max"] = round(n["flg_p95_max"], 3)
    return nights


def settings_periods(conn, tz):
    """Runs of consecutive CPAP sessions with identical therapy settings."""
    ch = channel_ids(conn)
    keys = ("PAPMode", "PressureMin", "PressureMax", "EPR", "EPRLevel", "RMS9_Mask")
    rows = q(conn, """SELECT s.id, s.start_time FROM sessions s JOIN machines m ON m.id = s.machine_id
                      WHERE m.machine_type = 1 AND s.enabled = 1 ORDER BY s.start_time""")
    vals = {}
    for sid, cid, val in q(conn, "SELECT session_id, channel_id, value FROM session_settings"):
        vals.setdefault(sid, {})[cid] = val
    periods = []
    for sid, st in rows:
        key = {k: vals.get(sid, {}).get(ch.get(k)) for k in keys}
        if not periods or periods[-1]["settings"] != key:
            periods.append({"settings": key, "first_night": night_of(st, tz), "nights": set()})
        periods[-1]["nights"].add(night_of(st, tz))
    for p in periods:
        p["last_night"] = max(p["nights"])
        p["nights"] = len(p["nights"])
    return periods


def medicare_best_window(nights):
    """Best count of >=4 h nights in any 30-day window starting within the first 90 days."""
    first = min(nights)
    best = 0
    for offset in range(61):
        start = first + timedelta(days=offset)
        good = sum(1 for d in (start + timedelta(days=i) for i in range(30))
                   if d in nights and nights[d]["hours"] >= 4)
        best = max(best, good)
    return {"first_night": first, "best_window_nights_ge_4h": best, "met": best >= 21}


def pick_cases(nights):
    """Pick one night per real-data test case. Each lambda returns the night's sort key."""
    full = {d: n for d, n in nights.items() if not n["summary_only_sessions"]}

    def best(pool, key):
        return max(pool, key=lambda d: key(pool[d])) if pool else None
    last_pmax = nights[max(nights)]["pressure_max_setting"]
    cases = {
        "F2_most_time_at_max": best(full, lambda n: n["time_at_max_pct"] or 0),
        "F2_never_at_max": best({d: n for d, n in full.items()
                                 if n["time_at_max_pct"] == 0
                                 and n["pressure_max_setting"] == last_pmax},
                                lambda n: n["hours"]),
        "F2_F8_most_obstructive_events": best(full, lambda n: n["events"]["Obstructive"]
                                              + n["events"]["Hypopnea"]),
        "F3_most_large_leak": best(full, lambda n: n["large_leak_pct"]),
        "F3_short_night_high_ahi": best({d: n for d, n in full.items() if n["hours"] < 4},
                                        lambda n: n["ahi"] or 0),
        "F3_most_sessions": best(full, lambda n: n["sessions"]),
        "F3_summary_only": best({d: n for d, n in nights.items() if n["summary_only_sessions"]},
                                lambda n: n["hours"]),
        "F3_clean_night": best({d: n for d, n in full.items()
                                if n["sessions"] == 1 and n["large_leak_pct"] == 0},
                               lambda n: n["hours"]),
        "F9_most_central_apneas": best(full, lambda n: n["events"]["ClearAirway"]),
        "F10_highest_flg": best(full, lambda n: n["flg_p95_max"]),
    }
    return {k: v for k, v in cases.items() if v is not None}


def dst_nights(nights, tz, shift_ms):
    """Nights (in the shifted timeline) whose sessions span a DST change."""
    found = []
    for d, n in nights.items():
        a = datetime.fromtimestamp((n["first_start"] - shift_ms) / 1000, tz)
        b = datetime.fromtimestamp((n["last_end"] - shift_ms) / 1000, tz)
        if a.utcoffset() != b.utcoffset():
            found.append(d)
    return found


def anonymize(conn, shift_weeks, keep_sessions, full, tz):
    shift_ms = shift_weeks * 7 * DAY_MS
    shift_days = timedelta(weeks=shift_weeks)
    c = conn.cursor()

    def shift_date_text(text):
        m = DATE_RE.match(text or "")
        if not m:
            return text
        return (date.fromisoformat(m.group(1)) - shift_days).isoformat() + m.group(2)

    # Timeline. Event times inside blobs are offsets from event_lists.first_time.
    c.execute("UPDATE sessions SET start_time = start_time - ?, end_time = end_time - ?",
              (shift_ms, shift_ms))
    # ResMed (and most loaders) derive session_id from the start time in seconds.
    c.execute("""UPDATE sessions SET session_id = session_id - ?
                 WHERE abs(session_id - (start_time + ?) / 1000) < 400 * 86400""",
              (shift_ms // 1000, shift_ms))
    for table, cols in (("event_lists", ("first_time", "last_time")),
                        ("session_channels", ("first_time", "last_time")),
                        ("respiratory_events", ("start_time", "end_time")),
                        ("session_slices", ("start_time", "end_time"))):
        c.execute(f"UPDATE {table} SET " + ", ".join(f"{col} = {col} - ?" for col in cols),
                  (shift_ms,) * len(cols))
    # (profile_id, date) is UNIQUE, so move the rows out of the way before shifting them.
    c.execute("UPDATE daily_summaries SET date = 'x' || date")
    c.execute("UPDATE daily_summaries SET date = date(substr(date, 2), ?)",
              (f"-{shift_weeks * 7} days",))
    for table, col in (("device_time_corrections", "date_from"),
                       ("device_time_corrections", "date_to")):
        for rowid, val in q(conn, f"SELECT rowid, {col} FROM {table}"):
            c.execute(f"UPDATE {table} SET {col} = ? WHERE rowid = ?",
                      (shift_date_text(val), rowid))

    # Audit timestamps record when the real import happened.
    for (table,) in q(conn, "SELECT name FROM sqlite_master WHERE type = 'table'"):
        for col in (r[1] for r in q(conn, f"PRAGMA table_info({table})")):
            if col in ("created_at", "updated_at", "applied_at", "status_changed_at",
                       "calculated_at", "last_imported", "undone_at"):
                c.execute(f"UPDATE {table} SET {col} = ? WHERE {col} IS NOT NULL", (AUDIT_TS,))

    # Identity.
    c.execute("""UPDATE user_info SET dob = '1980-01-01', first_name = 'Test', last_name = 'User',
                 address = '', phone = '', email = '', height = 175, gender = 0,
                 password_hash = ''""")
    c.execute("""UPDATE doctor_info SET name = '', phone = '', email = '', practice_name = '',
                 address = '', patient_id = ''""")
    c.execute("UPDATE profiles SET username = 'Test', data_folder = '%PROFDIR%/Test'")
    for rowid, key, val in q(conn, "SELECT rowid, key, value FROM profile_preferences"):
        if key in PREF_OVERRIDES:
            val = PREF_OVERRIDES[key]
        elif val and ("/Users/" in val or "/Volumes/" in val or "/home/" in val
                      or ":\\" in val):
            val = ""
        else:
            val = shift_date_text(val)
        c.execute("UPDATE profile_preferences SET value = ? WHERE rowid = ?", (val, rowid))
    c.execute("UPDATE app_preferences SET value = '' WHERE key = 'Profile'")
    for rowid, loader, mtype in q(conn, "SELECT rowid, loader_name, machine_type FROM machines"):
        fake_serial = f"{rowid:011d}"
        c.execute("""UPDATE machines SET serial_number = ?, machine_id = ?,
                     properties = json_set(coalesce(properties, '{}'), '$.SerialNumber', ?)
                     WHERE rowid = ? AND json_valid(coalesce(properties, '{}'))""",
                  (fake_serial if mtype != 4 else f"{rowid:08x}", 100000 + rowid, fake_serial,
                   rowid))

    # Size: drop the heavy channels except on case nights.
    if not full:
        ch = channel_ids(conn)
        heavy = [ch[x] for x in HEAVY_CHANNELS if x in ch]
        marks = ",".join("?" * len(heavy))
        keep = ",".join(str(s) for s in keep_sessions) or "-1"
        c.execute(f"""DELETE FROM event_data WHERE eventlist_id IN (
                      SELECT id FROM event_lists WHERE channel_id IN ({marks})
                      AND session_id NOT IN ({keep}))""", heavy)
        c.execute(f"DELETE FROM event_lists WHERE channel_id IN ({marks}) "
                  f"AND session_id NOT IN ({keep})", heavy)
        c.execute(f"""DELETE FROM session_channel_values WHERE session_channel_id IN (
                      SELECT id FROM session_channels WHERE channel_id IN ({marks})
                      AND session_id NOT IN ({keep}))""", heavy)
        c.execute(f"DELETE FROM session_channels WHERE channel_id IN ({marks}) "
                  f"AND session_id NOT IN ({keep})", heavy)
    conn.commit()


def identifiers(conn, src_folder):
    """Every identifying string in the source, for the final leak scan."""
    found = {str(Path.home()), "/Volumes/"}
    for row in q(conn, "SELECT first_name, last_name, email, phone, address FROM user_info"):
        found.update(v for v in row if v and len(str(v)) > 2)
    for row in q(conn, "SELECT name, phone, email, practice_name, address, patient_id "
                       "FROM doctor_info"):
        found.update(v for v in row if v and len(str(v)) > 2)
    found.update(v for (v,) in q(conn, "SELECT username FROM profiles") if v)
    found.update(v for (v,) in q(conn, "SELECT serial_number FROM machines") if v and len(v) > 5)
    marks = ",".join("?" * len(IDENTITY_PREFS))
    found.update(v for (v,) in q(conn, f"SELECT value FROM profile_preferences WHERE key IN "
                                       f"({marks})", IDENTITY_PREFS) if v and len(v) > 2)
    found.update(p.name for p in (Path(src_folder) / "Profiles").glob("*"))
    return sorted(s for s in found if s not in ("Test", "User", "true", "false"))


def leak_scan(conn, needles):
    hits = []
    encoded = [(n, n.encode(), n.encode("utf-16-le")) for n in needles]
    for (table,) in q(conn, "SELECT name FROM sqlite_master WHERE type = 'table' "
                            "AND name != 'event_data'"):  # sensor samples only
        cols = [r[1] for r in q(conn, f"PRAGMA table_info({table})")
                if r[2].upper() in ("TEXT", "BLOB", "")]
        for col in cols:
            for (val,) in q(conn, f"SELECT {col} FROM {table} WHERE {col} IS NOT NULL"):
                raw = val if isinstance(val, bytes) else str(val).encode()
                for name, b8, b16 in encoded:
                    if b8 in raw or b16 in raw:
                        hits.append(f"{table}.{col}: {name[:3]}…")
    return sorted(set(hits))


def jsonable(obj):
    if isinstance(obj, dict):
        return {str(k): jsonable(v) for k, v in obj.items()}
    if isinstance(obj, (list, tuple, set)):
        return [jsonable(v) for v in obj]
    if isinstance(obj, date):
        return obj.isoformat()
    return obj


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", required=True, help="OSCAR data folder containing oscar.db")
    ap.add_argument("--dst", required=True, help="output folder (must not exist)")
    ap.add_argument("--full", action="store_true", help="keep heavy channels on every night")
    args = ap.parse_args()

    src, dst = Path(args.src).expanduser(), Path(args.dst)
    if dst.exists():
        sys.exit(f"{dst} already exists; remove it first")
    shift_weeks = random.SystemRandom().randint(52, 260)  # never written out

    source = sqlite3.connect(f"file:{src / 'oscar.db'}?mode=ro", uri=True)
    tz_name = (q(source, "SELECT value FROM profile_preferences WHERE key = 'TimeZone'")
               or [("UTC",)])[0][0] or "UTC"
    tz = ZoneInfo(tz_name)
    nights = collect_nights(source, tz)
    periods = settings_periods(source, tz)
    cases = pick_cases(nights)
    needles = identifiers(source, src)

    dst.mkdir(parents=True)
    out_db = dst / "oscar.db"
    copy = sqlite3.connect(out_db)
    source.backup(copy)
    source.close()

    keep = {sid for d in cases.values() for sid in nights[d]["session_ids"]}
    dst_shifted = dst_nights(nights, tz, shift_weeks * 7 * DAY_MS)
    if dst_shifted:  # keep one DST-straddling night's full detail as well
        cases["general_dst_change_night"] = dst_shifted[0]
        keep.update(nights[dst_shifted[0]]["session_ids"])
    anonymize(copy, shift_weeks, keep, args.full, tz)
    copy.execute("PRAGMA journal_mode = DELETE")
    copy.execute("VACUUM")
    hits = leak_scan(copy, needles)
    copy.close()
    if hits:
        shutil.rmtree(dst)
        sys.exit("identifiers survived anonymization, output removed:\n  " + "\n  ".join(hits))
    (dst / "Profiles" / "Test").mkdir(parents=True)

    shift = timedelta(weeks=shift_weeks)

    def moved(d):
        return d - shift
    case_out = {}
    for name, d in sorted(cases.items()):
        n = nights[d]
        case_out[name] = {"night": moved(d), **{k: n[k] for k in (
            "hours", "sessions", "summary_only_sessions", "events", "ahi", "large_leak_pct",
            "time_at_max_pct", "pressure_max_setting", "flg_p95_max")}}
    manifest = {
        "about": "Anonymized real CPAP data for the Therapy Insights tests. Dates are shifted "
                 "by an unrecorded whole number of weeks. Reference values come straight from "
                 "the source summary tables; see Tools/insights_testdata/README.md.",
        "profile": "Test",
        "timezone": tz_name,
        "day_split": "12:00",
        "time_at_max_tolerance_cmh2o": TIME_AT_MAX_TOLERANCE,
        "nights_total": len(nights),
        "range": [moved(min(nights)), moved(max(nights))],
        "heavy_channels_kept_only_on_case_nights": None if args.full else list(HEAVY_CHANNELS),
        "cases": case_out,
        "settings_periods": [{**p, "first_night": moved(p["first_night"]),
                              "last_night": moved(p["last_night"])} for p in periods],
        "medicare_first_90_days": {k: moved(v) if isinstance(v, date) else v
                                   for k, v in medicare_best_window(nights).items()},
        "dst_change_nights": [moved(d) for d in dst_shifted],
    }
    (dst / "manifest.json").write_text(json.dumps(jsonable(manifest), indent=2) + "\n")
    print(f"wrote {out_db} ({out_db.stat().st_size / 1e6:.1f} MB), {len(nights)} nights, "
          f"{len(case_out)} case nights; leak scan clean ({len(needles)} identifiers checked)")


if __name__ == "__main__":
    main()
