# Löwenstein prisma VENT — SD card format and loader notes

GitLab #331. Loader: `oscar/SleepLib/loader_plugins/prismavent_loader.cpp`
(class `PrismaVentLoader`, loader name `PrismaVent`). Tests:
`oscar/tests/prismaventtests.cpp`.

Three cards have been examined, two VENT50-C and one VENT40 (GitLab #365):

| Card | Model | Firmware | Day archives | Sessions | Notes |
|---|---|---|---|---|---|
| A | VENT50-C | 3.9.0011 (`release-P34A11.3-3.09`) | 19 (2023) | 81 | Multi-session days, up to 19 per day |
| B | VENT50-C | 3.9.0017 (`release-P34A11.6-3.09`) | 1 (2026) | 3 | Two device folders on one card; only one has data |
| C | VENT40 | 3.9.0017 (`release-P34A11.6-3.09`) | 16 (2026) | 55 | All sessions in autoS/T; `WM29510` |

Card C has the same layout, archive members, 29 WMEDF signals and
`parametersmap.xml` as the VENT50-C cards, and the same vendor plugin. Its only
differences are the device type and the therapy mode (see below).

### Device type

`Device_type` in `device.xml` is the `DeviceProductType` parameter. prismaTS names
each product type in its localization resources (keys
`WTS/Therapy/DeviceTypes/P3_2/<product>_<branding>_<battery>`). With the Löwenstein
branding: 0 = VENT30, 1 = VENT40, 2 = VENT50, 3 = VENT30-C, 4 = "prisma VENT dev",
5 and 6 = VENT50-C, 7 = VENT40-C. Other brandings rename some types (TIVAN 30/40/50,
Prisma Comfort40); the loader uses the Löwenstein names.

The card-level layout and `device.xml` / `configuration.json` schemas are also
described in `SD_CARD_FINGERPRINTS.md` ("Löwenstein Prisma VENT V50-C"). That
section's statement that settings history is not on the card is wrong:
every day archive carries `parameter.xml` (see below).

## Card layout

```
WM<article number>/            e.g. WM30620 for the VENT50-C
  plugin/<dcm version>/dcm.zip vendor software plugin (not used)
  SN<serial>/                  one folder per device
    device.xml                 identity (root element Device_settings)
    configuration.json         current snapshot of the 3 therapy programs
    prismaVENT.sdpvdat         0-byte marker
    <counter>_<yyyy-mm-dd>.zip one archive per therapy day   <- imported
    trendcurve/<counter>_<date>.tc  (not decoded)
    battery/<counter>_<n>_<date>.wmedf  battery telemetry EDF (not imported)
    logs/                      device diagnostic logs (not imported, not backed up)
```

`therapy.log` shows the device building `/mnt/data/export/<counter>_<date>.zip`
and copying it to the card at the end of each session, so a day's archive is
rewritten whenever another session is added that day. The leading counter is
not a reliable key: card B's battery files show the counter jumping on clock
changes and three counters sharing one date.

## Day archive contents

| Member | Use |
|---|---|
| `NNNN.wmedf` | One per session (`0001`, `0002`, … restarting each day). Signals. |
| `event_NNNN.xml` | Respiratory events of the matching session |
| `parameter.xml` | Parameter log for the day: full snapshot at each power-on, then one entry per change |
| `parametersmap.xml` | Parameter name ↔ numeric id table used by `parameter.xml` (123 entries, `version="6.3.0"`) |
| `event.xml` | Device events (`id` + `status`); 55 = ventilation on (true) / off (false). 56 and 75 unknown |
| `alarm.xml` | Alarms (`begin`/`end`/`reset`); 794 = "Therapy stopped" (prismaTS event table), 581 unknown |
| `statistic.proto` | Protobuf, version string `1.0.12`; looks like daily histograms. Not decoded |
| `device.xml`, `service/*`, `logs/*` | Copies of device identity, service counters and logs |

Timestamps in `parameter.xml`, `event.xml` and `alarm.xml` are `+HHHH:MM:SS.mmm`
from midnight of the `<log date="…">`; a session past midnight keeps counting
(`+0032:46:51` = 08:46:51 next day). They are local wall-clock time, matching the
WMEDF start time.

### Signals (`NNNN.wmedf`)

The Prisma LINE WMEDF container (EDF header, `#1`/`#2` in the per-signal reserved
field for 8/16-bit samples), so `WMEDFInfo` from `prisma_loader.h` reads it
unchanged. 29 signals, 1 s records, 206 bytes per record. The start time has
1 s resolution and matches the ventilation-on event in `event.xml`.

| WMEDF label | Rate | OSCAR channel |
|---|---|---|
| Airway Pressure (hPa) | 10 Hz | `CPAP_MaskPressure` |
| Patient Flow (l/min) | 10 Hz | `CPAP_FlowRate` |
| Leakage Flow (l/min) | 1 Hz | `CPAP_Leak` (mean ~0.7 l/min on card B: unintentional leak) |
| TotalLeakage (l/min) | 1 Hz | `CPAP_LeakTotal` (mean ~29 l/min: includes vent flow) |
| Frequency (1/min) | 1 Hz | `CPAP_RespRate` |
| Tidal Volume (ml) | 1 Hz | `CPAP_TidalVolume` |
| Minute Volume (l/min) | 1 Hz | `CPAP_MinuteVent` |
| Part Spont Insp. (%) | 1 Hz | `CPAP_PTB` |
| IPAPsoll / EPAPsoll (hPa) | 1 Hz | `CPAP_IPAP` / `CPAP_EPAP` (targets; ramp visibly during softSTART) |
| SpO2 Value / Pulse Rate | 1 Hz | `OXI_SPO2` / `OXI_Pulse`, only when not all zero |

prismaTS lane names for the WMEDF signals, from its 10-minute signal view of card
B: "Volume fluctuations" = Online Volume, "Breath phase" = Breath Phase,
"Respiration" = Phase Trigger, "Ti/T" = Part Inspiration, and "% spontaneous
In/Ex" = Part Spont Insp./Exp. Phase Trigger is 1 while breaths are
patient-triggered. Its only pulse in that view, 372–375 s into the session,
lines up with the "Respiration" pulse and with Frequency rising to 14.5. The
same view draws softSTART as a span from the session start labelled "600 s".

Not imported: Online Volume, Breath Phase, Phase Trigger, Part Inspiration,
Part Spont Exp., SpO2 Sig. Qual., AirtrapControl, Target Volume, LIAM,
Trigger Level, Trigger Delay, RSBI, and five `* Dbg` signals. All imported
signals have physical and digital ranges anchored at zero, so the parser's
zero offset is correct for them.

### Respiratory events (`event_NNNN.xml`)

```xml
<desc>
<start time="+0022:42:01.154"/>
<RespEvent time = "5332" id = "111" event = "begin" Strength = "0"/>
<RespEvent time = "5342" id = "111" event = "end" Strength = "0"/>
<stop time="+0008:46:51.592"/>
</desc>
```

`time` is seconds from the session start. Begin/end pairs are matched per id.
Records are not in time order across ids. A begin can be left open at the end
of the file, and an end can appear with no begin (six softSTART ramps on card A,
ending up to 1799 s in): the loader starts those at the session start.

**Event names come from prismaTS itself.** Its localization resources
(`MCC.WTS.Localization.dll` in the prismaTS 5.21 install, keys
`EventsNames/RespEvent_<id>` and `EventsAbbreviations/RespEvent_<id>`) hold
the full id → name table shared by the Prisma LINE and VENT. Its signal-view
colour table (`<RespEvent TypeId=…>` in `MCC.WTS.Signals.dll`) matches the
colours in the report's legend. Ids seen on both cards (begin records):

| id | n | Duration | Strength | prismaTS name | Loader channel |
|---|---|---|---|---|---|
| 6 | 141 | ≤123 s | 1 (131), 2 (9), 3 (1) | Epoch with premature cyclings (ePC); labelled by strength | `PrismaVent_ePC` (off by default, like the other epoch channels) |
| 101 | 7 | 10–28 s | 0 | Obstructive apnea (oA) | `CPAP_Obstructive` |
| 106 | 4 | 11–19 s | 0 | Unclassified apnea during position change | `CPAP_Apnea` |
| 111 | 660 | 10–89 s | 0 | Obstructive hypopnea (oH) | `CPAP_ObstructiveHypopnea` |
| 112 | 22 | 12–49 s | 0 | Central hypopnea (cH) | `CPAP_CentralHypopnea` |
| 113 | 2 | 11–88 s | 0 | Unclassified hypopnea with very high leakage | `CPAP_Hypopnea` |
| 141 | 47 | 11–3503 s | 0 | Artifact | `Prisma_Artifact` |
| 171 | 53 | 4–29 s | 0 | Disconnection | `PrismaVent_Disconnect` |
| 241 | 47 | 0–1800 s | 0 | softSTART | `CPAP_Ramp` |
| 330 | 171 | always 10 s | 0 | Sudden Decline of Leakage ("SEL" in reports) | `PrismaVent_SEL` |

Also mapped, though not seen on a VENT card: 102 cA, 103/105 unclassified
apnea (very high leakage / very high pressure), 108 closed apnea with very high
pressure (hPrA), 121 RERA, 131 snore, 151 flow limitation, 161 critically high
leakage, 181 periodic breathing, 221 timed breath, 242 softSTOP (`CPAP_Ramp`),
261, and epochs 1–4. Ids 300–497 and 755–825 in the same table are alarms
(e.g. 311 apnea alert, 794 therapy stopped). Any id not mapped is still reported
as unexpected.

`alarm.xml` uses the same table: its 794 is "Therapy stopped". 581 is not in it.

**241 = softSTART.** On every session, its presence and length follow the
active program's `sSTART_T`. With 0 (31 sessions) it never appears. With 10
it is at most 600 s; with 30 it is at most 1800 s (17 sessions exactly 1800 s;
shorter ones end early). On card B the `IPAPsoll`/`EPAPsoll` targets ramp from
12/4 to 17/9 hPa over exactly that 600 s. One card-A session leaves it open
through a 6-hour session, so an open softSTART is capped at `sSTART_T`.


### Settings (`parameter.xml`)

```xml
<log version="1" date="2026-10-16"><config version="6.3.0"/>
<start time="+0013:44:40.081"/>
<parameter time="+0013:44:40.080" id="1" value="0"/>                 device-wide
<parameter time="+0013:44:40.080" id="69" value="1000" program="0"/> per program
...
<parameter time="+0013:45:56.040" id="69" value="1700" program="0"/> later change
<stop time="+0013:52:26.244"/></log>
```

Device-wide parameters carry no `program`. Therapy parameters are written once
per program (`program="0".."2"`). `ActiveProgram` (device-wide) selects the
program in use. The loader takes each value as it stood at the WMEDF start
(+999 ms for the start's 1 s truncation), resolving names through
`parametersmap.xml`.

Units, from the values and the vendor plugin's labels:
- pressures: hPa × 100 (`IPAP=1700` = 17 hPa)
- `Frequency`: breaths/min × 10 (`140` = 14/min)
- `Ti`, `Ti_min`, `Ti_max`, `Ti_timed`: ms
- `sSTART_T`: minutes
- `TriggerSensitivityExspiration`: % of peak flow
- `TriggerSensitivityInspiration`: level; the WMEDF "Trigger Level" signal runs 0–8

**prismaTS cross-check (card B, prismaTS 5.21.0.11 short report).** The report
names program 1 **S/T** (TherapyMode 2) and program 2 **MPVp** (TherapyMode 8).
Every program-1 value the loader reads matches its settings table: IPAP 17,
EPAP 9, F min 14, Ti max 1.8, Ti min 0.6, Ti timed 1.4, Trigger IN 3,
Trigger EX 25 %, RAMP IN 3, RAMP EX 2, softSTART max 10 min, softSTART pressure
4 cmH₂O (`sSTART_EPAP`). "Trigger: Manually" is `TriggerType=0`, and "Hose
system: Leakage" is `ExhalationSystem=1`. Program 2 shows IPAP 10 and Ti 1.2
(the `Ti` parameter), so `Ti` belongs to fully timed modes and `Ti_timed` to S/T.
Its 10:24 h usage is the sum of all three sessions (37,499 s). Its medians
match the imported signals: frequency 14, tidal volume 350 ml, minute volume
5 l/min, and Ti/T 33 % ("Part Inspiration"). It gives AHI 0/h. The SDB lane
draws marks where the loader puts the one OA (03:44) and the five OH, and the
artefact lane marks 03:38. Leakage median and P95 are both 2.5 l/min, which
looks like a 0–5 l/min class midpoint (the signal itself is ~0.5 l/min).

**TherapyMode.** Every session on both cards has `TherapyMode=2` (S/T);
card B's unused program 2 had 8 (MPVp). The mode names are CPAP, S, S/T, T,
aPCV, PSV, PCV, autoST, MPVp, MPVv, SIMVp and HFT. prismaTS's localization also
has a numbered list, `WTS/Therapy/Mode/P3_2/0..9` = CPAP, S, S/T, T, aPCV, PSV,
PCV, MPVp, MPVv, SIMVp. It agrees on 2 but puts MPVp at 7, while the report
shows TherapyMode 8 as MPVp. That list is therefore not the device's numbering
(it lacks autoST and HFT; one of those may sit at 7). Only 2 and 8 are taken
as known. The loader stores the raw number
(`PrismaVent_Mode`, labelled "S/T", "autoST", "MPVp" or "Mode N"). It treats S/T as
`MODE_BILEVEL_FIXED`, with IPAP/EPAP/PS, backup rate, Ti timed, Ti min/max,
triggers, rise/fall, softSTART time (`CPAP_RampTime`) and pressure
(`CPAP_RampPressure`). Any other mode, MPVp included, gets `MODE_UNKNOWN` and
is reported as unexpected until a session in it has been checked, because
volume, CPAP and high-flow modes use different parameters.

**TherapyMode 3 = autoS/T (card C, confirmed in prismaTS 5.21.0.11).** All 55
sessions use it. prismaTS's numbered list says 3 = T, but its settings view of
card C shows "Mode: autoS/T" for program 3 with EPAPmax 14, EPAPmin 10, ΔPinsp 2,
autoF Off, F min 10, Ti max 4, Ti min 0.5, Ti timed auto, Trigger auto, RAMP IN 4,
RAMP EX 2, softSTART max 10, all matching the loader. It also shows 1 % mandatory
breaths, 7 days on program 2 and 9 on program 3. The signals agree:
Part Spont Insp. is 99–100 % and Phase Trigger is 1 (patient-triggered) for all
but ~1 % of breaths, and the pressure targets follow the autoEPAP parameters exactly:

- `EPAPsoll` stays within `AutoEPAPmin`..`AutoEPAPmax` (10–12 hPa with 1000/1200;
  10–12.3 hPa with 1000/1400).
- `IPAPsoll` is `EPAPsoll` + `AutoEPAPdeltaPinsp` throughout (+1 hPa with 100,
  +2 hPa with 200). `IPAP`=1000 and `EPAP`=400 are set but not used.
- softSTART (event 241) lasts exactly `sSTART_T` (300 s with 5, 600 s with 10)
  and ramps EPAP linearly from 8 hPa to `AutoEPAPmin`. 8 hPa is `sSTART_EPAPmin`
  (800); `sSTART_EPAP` is 400, so S/T and autoST use different softSTART pressures.
- The rare untriggered breaths last 0.7–4.0 s, within `Ti_min`..`Ti_max` (0.5–4.0 s),
  not the fixed `Ti` (1.2 s). `Ti_timed` is 0.

The loader treats autoST as `MODE_BILEVEL_AUTO_FIXED_PS`: `CPAP_EPAPLo`/`EPAPHi`
from autoEPAP min/max, `CPAP_IPAPLo`/`IPAPHi` = those + ΔPinsp, `CPAP_PS` = ΔPinsp,
`CPAP_RampPressure` = `sSTART_EPAPmin`, and the same backup rate, Ti min/max,
triggers, rise/fall and softSTART time as S/T. It reports no Ti ("Ti timed: auto",
`Ti_timed` = 0). The device order may be CPAP, S, S/T, autoS/T, T, aPCV, PSV, PCV,
MPVp, ..., which fits 2, 3 and 8; beyond those it is a guess.

**TriggerType.** 0 = "Trigger: Manually", 1 = "Trigger: auto". prismaTS lists
Trigger IN/EX only for a manual trigger, so the loader stores `TriggerType`
(`PrismaVent_TriggerType`) and the sensitivities only when it is 0. Cards A and C
use auto on every program; card B uses manual.

Not used yet: `Humidifier`/`HumidifierLevel` (card B has `Humidifier=0` with
level 3, meaning unclear), alarm settings, LIAM, AutoEPAP and volume-target
parameters.

## Loader behaviour

- **Detect**: a folder with `device.xml` (root `Device_settings`) plus
  `prismaVENT.sdpvdat` or `configuration.json`. It is searched at the path,
  its children, and grandchildren under `WM*` children, so the card root, the
  `WM` folder, the `SN` folder and the backup folder are all accepted.
- **Several devices on one card**: the `SN` folder with the most recent day
  archive is imported. Card B carries two; the other has battery files only.
- **Session id**: WMEDF start time (seconds since the epoch). It stays stable
  when a day archive is rewritten, unlike the per-day `NNNN` numbering.
- **Backup**: the `SN` folder is copied to `<backup>/WM…/SN…/` without `logs/`.
  Files whose size is unchanged are skipped.
- **Untested**: `deviceIsUntested` is raised for every model except the
  VENT50-C (`Device_type` 5 or 6) and VENT40 (1), which have been checked
  against prismaTS.
- Device type: OSCAR has no ventilator machine type, so the device is
  `MT_CPAP`. Whether sleep-oriented summaries (AHI, usage) suit round-the-clock
  ventilator use is an open question (#331).

## Testing

`PrismaVentTests` (unit-test build, `CONFIG+=test`) covers the parsers on
synthetic input. Two tests read real cards and skip unless environment
variables point at them, since sample cards carry serial numbers:

- `OSCAR_PRISMAVENT_CARD` = card B's root: checks exact settings, waveforms
  and event counts for its night session.
- `OSCAR_PRISMAVENT_CARDS` = `;`-separated card roots: imports every session
  and checks ids are unique, flow and mode are present, and events lie inside
  their session. Card A: 81 sessions from 19 archives. Card B: 3 from 1.
  Card C: 55 from 16.

`testAutoSTSettings` checks the autoS/T settings mapping and the automatic trigger
on a synthetic log built from card C's program 3.

Neither test exercises `Open()` end to end (database, backup, daily rollup).
That still needs a real import in the app.

## Open questions

1. TherapyMode numbering beyond 2 (S/T), 3 (autoS/T) and 8 (MPVp), and the
   MPVp parameter set (needs a session recorded in that mode).
2. Whether prismaTS counts the unclassified 106/108/113 in its AHI. The loader
   counts them, via `CPAP_Apnea` and `CPAP_Hypopnea`.
3. `event.xml` ids 55/56/75 (55 behaves as ventilation on/off), and 52/70/82
   on card C; `alarm.xml` id 581, and 460/580 on card C.
4. `trendcurve/*.tc` (JSON header `{"Type":"P32",…,"Offset":"0061"}` then
   binary) and `statistic.proto`.
5. Other VENT models (VENT30, VENT30-C, VENT40-C, VENT50): the device type numbers
   are known from prismaTS; the VENT40 card has the VENT50-C layout, so the others
   probably do too.
