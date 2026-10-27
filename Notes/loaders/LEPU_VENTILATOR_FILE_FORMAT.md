# Lepu / Viatom LeRes ventilators — file format analysis

**Status:** decoded from the parsers in the ViHealth app (2.75.68, Lepu `blepro` SDK,
package `com.lepu.blepro.ble.data.ventilator`). **No real device file has been seen yet**,
so every layout here is what the vendor code reads, not something checked against data.
Fields the SDK skips are marked unknown. Open questions are listed at the end.

**Devices:** Bluetooth model IDs R20 (107), LeRes (108), R21 (115), R10 (116), R11 (117),
R200 (200). A Viatom/Lepu EU declaration of conformity pairs LeRes-A = R20,
LeRes-B = R100, LeRes-C = R10, LeRes-S = R200 (one document only). R20/R10 are
CPAP/APAP; R200 and LeRes-B add the bilevel modes S, S/T and T.

**Not to be confused with** Wellue's own CPAPs: the Wellue iBreeze is a Resvent machine
and is handled by the Resvent loader.

## Where files come from

- **Bluetooth** (`VentilatorBleInterface`, Viatom service `14839ac4-…`, `A5` framing)
  lists records and downloads only **`.stat`** files:
  - record type 1: daily summary, named `yyyyMMdd_day.stat`
  - record type 2: session summary, named `yyyyMMdd_HHmmss.stat`
- **`.para`** (per-second parameters) and **`.wave`** (waveforms) reach the app from
  Lepu's cloud (`DownloadVentilatorWorker`, `getVentilatorData`), which the machine
  fills over Wi-Fi. The app saves its files under a `…/host/` folder on the phone
  (the same layout OSCAR already relies on for the Wellue POD2).
- `EventFile`, `CtrlFile`, `ConfigFile` and `EcgFile` parsers exist in the SDK, but no
  app code calls the event, control or config parsers, so how those files are obtained
  is unknown. Their extensions don't appear in the app either.

## Conventions

- All multi-byte values are **little-endian**; the SDK's `toUInt`/`toLong` assemble bytes
  low first.
- Signed 16-bit values are read with `bytesToSignedShort(lo, hi)`.
- **Timestamps** are 32-bit seconds holding the **local wall-clock time** as if it were
  UTC. The SDK subtracts the phone's time-zone offset to get a real epoch time. A loader
  should read the value as UTC and take its date and time as local.

## Common file header (10 bytes)

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | File version |
| 1 | 1 | File type |
| 2–9 | 8 | Unknown (the SDK never reads it; the ECG file has a timestamp at 10) |

Every file below starts with this header, and the records start at offset 10.

## Statistics file (`.stat`, `StatisticsFile`)

One per session (type 2) and one per day (type 1). Minimum length 474 bytes.

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0–9 | 10 | Header | |
| 10 | 4 | duration | seconds, presumably |
| 14 | 4 | usageDays | for a multi-day summary |
| 18 | 4 | moreThan4hDays | days with ≥ 4 h use |
| 22 | 4 | meanSecond | mean usage, seconds |
| 26 | 4 | spont | spontaneous-breath measure (bilevel) |
| 30 | 4 | ahiCount | AHI events |
| 34 | 4 | aiCount | apneas |
| 38 | 4 | hiCount | hypopneas |
| 42 | 4 | oaiCount | obstructive apneas |
| 46 | 4 | caiCount | central apneas |
| 50 | 4 | rearCount | RERAs |
| 54 | 4 | sniCount | snores |
| 58 | 4 | pbCount | periodic breathing |
| 62 | 4 | takeOffCount | mask-off count |
| 66 | 4 | llTime | large-leak time |
| 70 | 4 | unknown | skipped by the SDK |
| 74 | 240 | 12 statistics arrays | see below |
| 314 | 148 | unknown | skipped |
| 462 | 2 | odi3Count | 3% desaturations |
| 464 | 2 | odi4Count | 4% desaturations |
| 466 | 4 | sleepTimes | |
| 470 | 4 | percent90Times | time with SpO2 below 90% |

**Statistics arrays (offset 74):** 12 arrays, each of 5 unsigned 32-bit values in the
order **[min, max, mean, median, 95th percentile]** (from the app's
`MedicalMetric(min, max, mean, middle, percent_95)` and `fromFloatArray`).

| # | Offset | Channel | Scale |
|---|---|---|---|
| 1 | 74 | pressure | ÷ 10 → cmH₂O |
| 2 | 94 | IPAP | ÷ 10 → cmH₂O |
| 3 | 114 | EPAP | ÷ 10 → cmH₂O |
| 4 | 134 | tidal volume | as is (mL) |
| 5 | 154 | minute ventilation | ÷ 10 → L/min |
| 6 | 174 | leak | ÷ 10 → L/min |
| 7 | 194 | respiratory rate | as is (bpm) |
| 8 | 214 | Ti | ÷ 10 → s |
| 9 | 234 | I:E ratio | ÷ 10000 |
| 10 | 254 | SpO2 | as is (%) |
| 11 | 274 | pulse rate | as is (bpm) |
| 12 | 294 | heart rate | as is (bpm) |

The SDK reads these as unsigned 32-bit values even though the array type is float for
the scaled ones.

## Parameter file (`.para`, `ParaFile`)

Header, then 40-byte records from offset 10; count = (length − 10) / 40.
**One record per second**: the app times record *i* at start + *i* × 1000 ms, taking
the start from the cloud record's measure time, not from the file.

| Offset | Size | Field | Scale (from the live-data and app code) |
|---|---|---|---|
| 0 | 2 | pressure | ÷ 10 → cmH₂O (the app divides by 10) |
| 2 | 2 | IPAP | ÷ 10 |
| 4 | 2 | EPAP | ÷ 10 |
| 6 | 2 | tidal volume | mL |
| 8 | 2 | minute ventilation | ÷ 10 → L/min |
| 10 | 2 | leak | ÷ 10 → L/min |
| 12 | 2 | respiratory rate | bpm |
| 14 | 2 | Ti | ÷ 10 → s |
| 16 | 2 | I:E | ÷ 10000 |
| 18 | 2 | SpO2 | % (oximeter accessory) |
| 20 | 2 | pulse rate | bpm |
| 22 | 2 | heart rate | bpm |
| 24 | 16 | unknown | skipped by the SDK |

All values are signed 16-bit. The scaling matches the live-data packet
(`VentilatorBleResponse.RtParam`), which carries the same 12 values in the same order.

## Waveform file (`.wave`, `WaveFile`)

Header, then fixed-size samples from offset 10. The sample size depends on the
**file version**:

| Version | Sample size | Fields (signed 16-bit each) |
|---|---|---|
| 2 | 6 bytes | pressure, flow, tidal volume |
| other | 4 bytes | pressure, flow |

The SDK applies no scaling and I found no app code that reads wave files, so the
**sample rate and units are unknown**. Pressure is probably ÷ 10 like everywhere else; flow units need a
real file.

## Event file (`EventFile`)

Header, then 12-byte events from offset 10.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | timestamp (local time as epoch seconds) |
| 4 | 1 | bit 7 = event cleared (end of an alarm/event) |
| 4–5 | 2 | event ID — see below |
| 6 | 4 | parameter (unsigned 32-bit) |
| 10 | 2 | unknown |

**The event ID decode in the SDK looks wrong.** It computes `(b4 & 0x7F) + b5`, which
can never reach the defined IDs (up to 610). The live event packet reads the ID as an
unsigned 16-bit value. The file most likely holds a 15-bit ID in bytes 4–5 with bit 15
as the cleared flag, but that needs a real file to confirm.

**Event IDs** (`VentilatorBleCmd.EventId`):

| ID | Event | ID | Event |
|---|---|---|---|
| 601 | Obstructive apnea | 606 | Snoring |
| 602 | Central apnea | 607 | Periodic breathing |
| 603 | Unclassified apnea | 608 | Large leak |
| 604 | Hypopnea | 609 | Mask off |
| 605 | RERA | 610 | Spontaneous breath |

Alarms 1–12: 1 line disconnected, 2 line clogged, 3 high leak, 4 high RR, 5 low RR,
6 low tidal volume, 7 low minute ventilation, 8 low SpO2, 9 high HR/PR, 10 low HR/PR,
11 apnea, 12 power down.
Faults: 101–104 turbine, 201–203 heater, 301–316 sensors and self-test, 401–403 power
supply, 501–504 system (EEPROM, RTC, calibration, abnormal reboot).

## Control file (`CtrlFile`) — settings changes

Header, then 36-byte records from offset 10, apparently one per settings change.

| Offset | Size | Field | Scale |
|---|---|---|---|
| 0 | 4 | timestamp (local time as epoch seconds) | |
| 4 | 1 | ventilation mode | 0 CPAP, 1 APAP, 2 S, 3 S/T, 4 T |
| 5 | 1 | CPAP pressure | ÷ 10 → cmH₂O |
| 6 | 1 | APAP max pressure | ÷ 10 |
| 7 | 1 | APAP min pressure | ÷ 10 |
| 8 | 1 | IPAP | ÷ 10 |
| 9 | 1 | EPAP | ÷ 10 |
| 10 | 1 | inspiratory time | ÷ 10 → s |
| 11 | 1 | backup respiratory rate | bpm |
| 12 | 2 | rise time | probably ms |
| 14 | 1 | inspiratory trigger | level |
| 15 | 1 | expiratory trigger | level |
| 16 | 2 | sample | unknown meaning |
| 18 | 1 | max inspiratory time | ÷ 10 → s |
| 19 | 1 | min inspiratory time | ÷ 10 → s |
| 20 | 16 | unknown | skipped |

The ÷ 10 scales come from the matching `VentilationSetting` classes, which decode the
same values from the settings commands.

## Mapping to OSCAR

| Lepu | OSCAR channel |
|---|---|
| para pressure / IPAP / EPAP | `CPAP_Pressure`, `CPAP_IPAP`, `CPAP_EPAP` |
| para leak, tidal volume, minute ventilation, RR, Ti, I:E | `CPAP_Leak`, `CPAP_TidalVolume`, `CPAP_MinuteVent`, `CPAP_RespRate`, `CPAP_Ti`, `CPAP_IE` |
| para SpO2 / pulse | `OXI_SPO2`, `OXI_Pulse` |
| wave pressure / flow | `CPAP_MaskPressure`, `CPAP_FlowRate` |
| events 601–605, 607, 608 | `CPAP_Obstructive`, `CPAP_ClearAirway`, `CPAP_Apnea`, `CPAP_Hypopnea`, `CPAP_RERA`, `CPAP_PB`, `CPAP_LargeLeak` |
| event 606 | `CPAP_VSnore` |
| control file | session settings: `CPAP_Mode`, `CPAP_Pressure`, `CPAP_PressureMin/Max`, `CPAP_IPAP`, `CPAP_EPAP` |

## Open questions — need real files

1. Header bytes 2–9: device or serial info, start time?
2. Waveform sample rate and flow units.
3. Event-file ID encoding (see above).
4. Whether `.para`/`.wave` names carry the start time (the app takes it from the cloud
   record instead).
5. Where the event and control files live, and what the users can actually copy off the
   phone or the machine (SD card? only the app's `host` folder?).
6. Bytes 24–39 of each parameter record and 20–35 of each control record.
