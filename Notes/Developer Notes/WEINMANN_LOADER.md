# Weinmann SOMNObalance e Loader

**Source:** `oscar/SleepLib/loader_plugins/weinmann_loader.cpp` / `.h`
**Data version:** `weinmann_data_version = 3` (unchanged). The loader now stores its data under the real serial number of the device instead of the old hard-coded `141819`, so old and new data never share a device and no re-import is needed. Increment the version whenever a later change alters data that was imported before.
**Origin:** ported from TBM's SleepyHead fork; the event table, timing, suppression rules and bounds checks were verified or added for OSCAR against
WEINMANNsupport (the manufacturer's Windows software) using real cards.

---

## 1. Card layout

The loader needs one file, `WM_DATA.TDF`, in the root of the SD card (`Detect()` only checks that it exists).
The settings file `WM_PARA.TPG` has the same XML settings block as `WM_DATA.TDF` at `DeviceConfigOffset`.

`WM_DATA.TDF` = **XML index** (`<desc>...</desc>`, ISO-8859-1, one `<par name=".." val=".."/>` per line) followed directly by **binary blocks**.
All offsets in the index are absolute file offsets; integers are little endian.

| Block (index key) | Content | Record |
|---|---|---|
| `DeviceConfigOffset` | XML settings block (device, firmware, serial, usage hours, ...) | - |
| `YearComplianceOffset` | year compliance (not used) | - |
| `WeekComplianceOffset` | week records, `WeekComplianceCount` of them | 0x84 B |
| `WCD_Pin_Offset` | week data: inspiratory pressure | 1440 B per week record |
| `WCD_Pex_Offset` | week data: expiratory pressure | 1440 B |
| `WCD_Snore_Offset` | snore bit field (1 bit = 2 min), **not read** (same information as the snore epochs) | - |
| `WCD_Lf_Offset` | week data: leak | 1440 B |
| `WCD_Events_Offset` | week data: events, 460 x 6 B | 2760 B |
| `WCD_IO_Offset` | week data: mask on/off (I/O) records | 180 B |
| `DayComplianceOffset` | day records, `DayComplianceCount` of them | 0xD6 B |
| `TID_Flow_Offset` | day data: flow waveform | 5 Hz |
| `TID_Status_Offset` | day data: status bits | 2 Hz |
| `TID_P_Offset` | day data: pressure | 2 Hz |
| `TID_AMV_Offset` | day data: relative minute volume | 0.5 Hz |
| `TID_Events_Offset` | day data: events | 6 B per event |

The keys must appear in this order in the file (week keys ascending; day keys either all missing/0 on a week-only card, or ascending).
The size of a block is the difference to the next offset (the last one runs to the end of the file or to `DayComplianceOffset`).

### Week data versus day data

* **Week data** (up to 60 usage days) is low resolution: 1 sample/min for pressure and leak, 2 s resolution for events. It always exists.
* **Day data** (detailed) only covers about 35-36 hours, it exists only if the device stores detailed data (storage settings, see section 8).
  It holds flow, pressure, status bits, relative minute volume and events with full resolution.
* A night that exists as day data replaces the low resolution session created from week data (`new_sessions` / `delete new_sessions.take(ts)`).
* A night already stored from week data only (for example from a newer card, when cards are imported in reverse order) is replaced
  by the day data of an older card: the stored session is destroyed (`Session::Destroy()`) and the detailed one is added. The test is
  whether the stored session has `CPAP_FlowRate`; week sessions never have it. A night already stored with day data is skipped.
  Matching relies on the start time of the day record being equal to the start time of the week session, as within one card.

---

## 2. Records

### Week record (0x84 B)

Date/time of the session start (year as two bytes, month, day, hour, minute, second), session count (`wnsess` at +68, at most 30 sessions per record),
and per session the offset and duration into the 1440 B pressure/leak blocks (+102/+104/+108/+110 hold the block lengths and are always 1440/1440/1440/460).
Each session has pointers into the week event table and the I/O table; those pointers are checked against the block sizes.

Sessions starting between 00:00 and 12:00 belong to the night that began on the previous day (`date.addDays(1)` logic is applied
to the *evening* date; see the code for the exact 12:00 noon convention). The operating manual confirms it: each therapy day
begins and ends at 12 noon, data from midnight to noon belong to the previous calendar day.

### Day record (0xD6 B)

| Offset | Meaning |
|---|---|
| +8 | start of the session in the flow block |
| +12 | start in the status block |
| +16 | start in the pressure block |
| +20 | start in the AMV block |
| +24 | start in the event block |
| +0x44, +0x48, +0x4C, +0x50 | lengths of the flow, status, pressure and AMV data |
| +0x54 | number of event records |
| +0x80 ... +0x93 | deleted sample counts (flow, status, pressure, AMV, events) of a record whose beginning the device has overwritten |
| +0xBC / +0xBE | LPL / UPL (pressure limits, 1/1000 cmH2O) |
| +0xC0 | pressure rise |
| +0xC1 | mode (7 = CPAP, 4 = APAP) |

Session duration = `min(pressure length / 2, flow length / 5)` seconds; the last second is cut.
If the deleted AMV count (+0x8C) is not 0, the remaining data starts `ceil(count / 5)` seconds later. Only the session start
moves; the session ID stays the time of the record, so the night still matches its week session (`SessionExists()`,
`new_sessions`). **Not verified**: the count was 0 on all test cards.

---

## 3. Waveforms

| Data | Rate | OSCAR channel | Notes |
|---|---|---|---|
| Flow | 5 Hz | `CPAP_FlowRate` | Compressed byte, decompressed to the total flow; stored is the respiratory flow, see section 3a. |
| Pressure (day) | 2 Hz | `CPAP_Pressure` | Stored as `CPAP_Pressure` (like week data), because Overview takes median/95 % from this channel. |
| Pressure (week) | 1/min | `CPAP_Pressure` | |
| Leak (week) | 1/min | `CPAP_Leak` | Unintentional leak, same compression as the flow (`wmDecompressFlow()`); byte 75 = 0.5 l/min, values below 0 are clamped to 0. |
| Leak (day) | 2 Hz | `CPAP_Leak` | **Not on the card**: derived from flow and pressure like WEINMANNsupport does, see section 3a. |
| Relative minute volume (day) | 0.5 Hz | `WM_rRMV` (0xd140) | One byte per 2 s, 0-255, **100 = reference value**. The signal is *relative*, it is not l/min, do not store it as `CPAP_MinuteVent`. Daily does not draw channels automatically: `WM_rRMV` is listed in `cpapcodes`, in both graph order lists, in the line chart layers and in the statistics channel list of `daily.cpp`. |
| Status bits | 2 Hz | `CPAP_LargeLeak`, `CPAP_VSnore`, `WM_Artefact` | Bit 6 large leak, bit 7 snoring, bit 2 artefact (merged with event code 100). A span runs from the first sample with the bit to the first sample without it (or to the end of the session). |

### 3a. Flow and leak of detailed days

The signals are derived so that the result matches the values shown by WEINMANNsupport (`wmDecompressFlow()` and
`wmFlowAndLeak()`). All values are in 0.1 l/min and 0.1 hPa.

**Flow compression.** One byte per sample:

| Byte v | Flow (0.1 l/min) |
|---|---|
| v < 50 | 45 v - 2478 |
| 50 <= v < 200 | 10 (v - 50) - 245 |
| v >= 200 | round(1250 (v - 200) / 56) + 1261 |

The fine middle range is about -24.5 ... +124.5 l/min of *total* flow; only coughs, mask-off periods and the like reach the coarse ranges.
The leak of the week data uses the same compression.

**Respiratory flow** (stored as `CPAP_FlowRate`, 5 Hz):

1. The flow is interpolated to 10 Hz.
2. A baseline follows it slowly: it moves by about 1/150 of the distance per sample while the flow crosses it or stays within 40 l/min,
   and faster (down to 1/2) the longer the flow stays more than 40 l/min on one side. It is settled with 100 steps on the first value,
   smoothed again by 1/32 per sample and sampled at 2 Hz: this is the mean total flow (vent flow + leak).
3. Respiratory flow = flow - mean total flow.

**Leak** (stored as `CPAP_Leak`, 2 Hz):

1. Mean pressure = the pressure smoothed by 1/16 per sample.
2. Every 15 s a 30 s window (60 samples) is checked; if the summed absolute deviation is at most 120 l/min for the mean flow and
   at most 25 hPa for the pressure, its means are a point of the vent curve. Per pressure class of 0.5 hPa (3 ... 20 hPa) the point with
   the lowest flow relative to a typical vent curve (11 l/min at 4 hPa, 45 l/min at 30 hPa) is kept.
3. The lowest of these points, and the lowest one at least 1.5 hPa away from it, define the vent curve of the night: a parabola
   pressure = a flow^2 + b flow through both points (or through one point and an assumed second point 20 l/min and 10 hPa higher).
4. Leak = mean total flow - vent flow at the mean pressure, not below 0.

A night without any quiet window uses the typical curve. The baseline starts fresh for every record.
No settings of the OSCAR profile (mask leak profile) are used.

---

## 4. Events

An event record has 6 bytes: `[code, hour, minute, second, dataLo, dataHi]`.

* The time stamp is the **end** of the event **+ 1 s**.
* `data` is the duration in 1/10 s. Day data: `duration = ceil(data / 10)`. Week data: `floor(data / 10)` (rounding up produced out-of-order events there).
* Start = stamp - 1 s - duration. Only events that *start inside the session* are kept (window filter).
* Codes >= 0x80 are **settings changes** (0x84 soft start, 0x85/0x86 soft start time/pressure, 0x87/0x88 humidifier), not events.

### Code table (`wmEventChannel()`)

| Code | Meaning | OSCAR channel | Verified |
|---|---|---|---|
| 3 | epoch with severe obstruction | `WM_EpSevereObs` | yes |
| 4 | epoch with mild obstruction | `WM_EpMildObs` | yes |
| 8 | flow limitation | `CPAP_FlowLimit` | yes |
| 9 | RERA | `CPAP_RERA` | yes |
| 10 | epoch with flattening | `WM_EpFlowLimit` | yes |
| 11, 12 | RERA (with snoring) | `CPAP_RERA` | **no** |
| 16 | obstructive hypopnea | `CPAP_ObstructiveHypopnea` | yes |
| 17 | central hypopnea | `CPAP_CentralHypopnea` | yes |
| 34 | epoch with snoring | `WM_EpSnore` | not in test data |
| 64 | central apnea | `CPAP_ClearAirway` | yes |
| 65 | obstructive apnea | `CPAP_Obstructive` | not in test data |
| 100 | artefact | `WM_Artefact` | yes |

Deliberately **not imported**: 0x12, 0x13, 0x28, 0x2A, 0x46, 0x91 (internal analysis markers that WEINMANNsupport does not show) and 96/97 (mask open - OSCAR has no channel for it).
Across all cards tested so far no unknown code other than 0x84-0x88 occurred. counting the raw codes of the day event table (a small script that parses the index and tallies the code byte of every 6-byte record) is the quickest way to check a new card.

### Suppression rules (`wmSuppress()`)

WEINMANNsupport does not show events that overlap certain other events; without these rules OSCAR shows more events than the manufacturer's software:

1. A **RERA** is suppressed by an overlapping obstructive apnea or obstructive hypopnea.
2. A **central apnea / central hypopnea** is suppressed by an overlapping obstructive apnea, obstructive hypopnea or (not suppressed) RERA.
3. Suppressed events do **not** suppress other events.

Overlap: `a.end > b.end - b.duration` and `a.end - a.duration <= b.end`.

### RERA and RDI

* Week data: Weinmann drops RERA events of the week data (it shows no RERA statistics for such nights), so the loader does not add them either.
* Weinmann's RDI = apneas + hypopneas **without** RERA; OSCAR's RDI includes RERA. Small differences in RDI against WEINMANNsupport are expected.

---

## 5. Settings / channels defined by the loader

`WM_*` channels (humidifier status/level/usage, SoftPAP lock/level, soft start, pressure rise, mask test, autostart, info menu, diary, automatic/manual storage,
usage/therapy/humidifier time, firmware, epochs, artefact) are registered in `WeinmannLoader::initChannels()`. Mode/pressure settings come
from the settings block (`CPAP_00`, `LPL__00`, `UPL__00`, `SSTME00`, ...), mode 7 = CPAP, 4 = APAP.

---

## 5a. Settings block and its checksum

The device settings are an XML block (`<par name=".." val=".."/>`, ISO-8859-1) in two places: the file `WM_PARA.TPG`
(the whole file, after a UTF-8 BOM) and `WM_DATA.TDF` at `DeviceConfigOffset`. Both have the same keys.

The last parameter `CRC__00` is a checksum of all other parameters:

* Message: the `val` attributes of all parameters except `CRC__00`, in file order, concatenated as one string (names and
  separators are not included), encoded as ISO-8859-1.
* Algorithm: CRC-16/CCITT-FALSE (polynomial 0x1021, initial value 0xFFFF, no reflection, no final XOR).
* Result: written as a decimal number.

```python
def crc16(data, c=0xFFFF):
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c
```

Checked on `WM_PARA.TPG` and on the settings block of two `WM_DATA.TDF` files; the calculated value matched the stored
one in all three cases.

Consequence: editing a value by hand without recalculating `CRC__00` makes the file inconsistent. It is not known whether the
device itself checks the CRC when it reads the card. The loader does not check it (yet); a mismatch could be reported as a
damaged card together with the index checks of section 6. The settings block holds therapy settings (pressure, mode, storage),
so it should only be changed through WEINMANNadjust.

---

## 6. Robustness (damaged cards)

TBM reports that damaged cards ("hiccups") crash the manufacturer's software. The loader therefore checks:

* `wmIndexValid()` right after `ParseIndex()`: week keys ascending and inside the file, day keys all zero or ascending, `WeekComplianceOffset - YearComplianceOffset >= 752`,
  record counts fit into their blocks. A card failing this is rejected (`return -1`, log: *the index of WM_DATA.TDF is damaged*).
* Week record: session count <= 30, cumulative offsets inside the 1440 B blocks, event and I/O pointers inside their blocks -> the remaining week data is ignored.
* Day record: valid date, every start + length inside its block, usable length -> the record is skipped (*skipping a damaged session record*).

All records of the real test cards pass these checks.

---

## 7. Known gaps / ideas

* The patch touches `daily.cpp` (the graph for `WM_rRMV`); everything else is in the loader.
* Cancel discards the whole import (nothing is saved, no stored session is replaced), so the import can simply be repeated.
* `weinmann_data_version` stays at 3 (see the header of this note). Users with data imported by the old loader can delete the old device (serial `141819`) by hand.
* Mask open events (96/97) are not imported (no OSCAR channel).
* Codes 0x0B / 0x0C as RERA are taken from Weinmann's table, not verified with real data.
* The card holds no leak curve for detailed days; the leak is derived as in section 3a.
* `PopulateSettings()` applies the *current* settings block of the device to all sessions of the week data (up to 60 days). A pressure change within that period is therefore attributed to earlier nights too: the week records carry no per-day settings that the loader decodes. Detailed days read pressure mode and limits from their own day record.
* Prisma devices use different formats (`.pdat` ZIP, `.wmedf` EDF, `statistic.psstat`) and are not supported by this loader.
* Classification of central/obstructive events is done by the device via OPP; it is unreliable below about 6 cmH2O.
* The device splits sessions after 10 h.
* Icon `wmsb2.png` is not part of the loader and is meant for a separate patch.

---

## 8. For users: getting detailed data onto the card

The device has two settings, "Automatic data storage" (`STRDA00`) and "Manual data storage" (`STRDM00`).
Each is "Compliance" (0), "Compliance + efficacy" (1) or "Detailed data" (3). The loader shows both as the
device settings `WM_AutoStorage` and `WM_ManStorage` in the Daily sidebar.

* **Automatic data storage** is triggered after every finished sleep session, when the device is switched off.
* **Manual data storage** is triggered by keeping the Info key (the (I) key, key 4 in the physician manual) depressed. The on/off key only switches the device on and off.

Detailed days are on the card only if the setting of the trigger that ran is "Detailed data". A device with both
set to "Detailed data" (like the test device) stores detailed data after every session without manual action.
If detailed days are missing, check these settings first. The settings are made in WEINMANNadjust
(the physician software), not on the device.

For the manual save, hold the Info key until the timer counts down and the red SD LED lights up (TBM, forum).
The timer must run for more than 20 s, otherwise only overview/compliance data is stored. Wait until the LED goes off before removing the card.

Always switch the device off with the on/off key before pulling the plug, otherwise the therapy data are not stored completely
(operating manual, "Save energy").

Import soon afterwards: detailed data lasts only about 35 hours, week data about 60 usage days.
Cards can be imported in any order: a night that is known only from week data is upgraded when a card with its detailed data is imported.

---

## 9. Testing

* Compare Daily and Overview of OSCAR with WEINMANNsupport for the same nights: events per type, indices, pressure 95 %/median.
* Check the raw event codes of a new card for unknown codes.
* Syntax check without a full build: `g++ -std=c++17 -fPIC -fsyntax-only -Wall -Wextra` with the Qt6 include paths.
* After changing data that was imported before, increment `weinmann_data_version`.
