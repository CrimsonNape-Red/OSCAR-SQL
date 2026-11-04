---
title: Data Tools
index: Time corrections; Clock!correcting; Daylight saving time; Travel!time zones; Drift analysis; Clock drift; Automatic Oximetry Cleanup; Purging data; Purge!current day; Purge!range of days; Purge!all device data; Purge!oximetry; Purge!time corrections; Rebuild CPAP Data
---

# Data Tools

The **Data** menu holds the tools for fixing device clocks, tidying oximetry, re-importing and removing data.

## Time corrections

[[idx:Time corrections]] [[idx:Clock!correcting]] [[idx:Daylight saving time]] [[idx:Travel!time zones]]
CPAP machines and oximeters keep their own clocks, and these are often wrong: never set, not changed for daylight saving time, left on home time while travelling, or reset after a power cut. A wrong clock puts your data on the wrong night, or makes oximetry and CPAP data fail to line up.

*Data → Time Corrections...* lets you tell OSCAR how far a device's clock was off, and when, without changing the original data.

1. Go to an affected day in the Daily view, then open *Data → Time Corrections...*. The window shows the device and the corrections active on that day.
2. Choose the **Type** of correction:
    - **Offset** — the clock was simply wrong by a fixed amount.
    - **Travel** — you took the device to another time zone.
    - **DST Change** — the clock was not changed for daylight saving time.
    - **Device Timezone Misconfiguration** — the device was set to the wrong time zone.
    - **Device Reset** — the clock was reset, for example after a power cut.
3. Set the **Time Adjustment**. The **+** / **−** button sets the direction. **Reuse Previous** copies the last offset you used; **Reset to Zero** clears it.
4. To limit the correction to certain days, tick **Apply to date range** and set **Start** and **End**, or tick **No end** for a correction that continues until you change it. Time-zone corrections are open-ended by default.
5. Click **Save**.

| Button | What it does |
|---|---|
| Save | Stores the correction and re-times the affected data |
| Cancel | Discards unsaved changes |
| Clear | Clears the correction being edited |
| Delete Correction | Removes the selected correction |

OSCAR warns you if an offset is very large, because a different type of correction may suit better. Corrections made with Drift Analysis (below) are shown here but are edited in the Drift Analysis window.

[[idx:Purge!time corrections]]
*Data → Advanced → Purge All Time Corrections* removes every time correction for the profile.

## Drift analysis

[[idx:Drift analysis]] [[idx:Clock drift]]
Some device clocks run slightly fast or slow, so they drift further from the right time week by week. *Data → Drift Analysis...* measures the drift by comparing one device with another whose clock you trust, and corrects for it.

1. Choose the **Device** whose clock drifts, and a **Reference device**.
2. Set **Range start** and **Range end**, then click **Load Data**.
3. Click **Fit Model** to calculate the drift over that period.
4. If the result looks right, click **Use Model**. The *Active drift model* line shows the model in use.

## Automatic Oximetry Cleanup

[[idx:Automatic Oximetry Cleanup]]
*Data → Automatic Oximetry Cleanup* removes oximetry segments shorter than the *Discard segments under* setting on the [Oximetry tab of Preferences](preferences.md#other-oximetry-options). These short scraps usually come from putting the sensor on or taking it off.

## Rebuild CPAP Data

[[idx:Rebuild CPAP Data]]
*Data → Rebuild CPAP Data* re-imports a CPAP device's data from the backup OSCAR made of its SD card. Choose the device from the submenu. Use it:

- after updating OSCAR, if the new version reads your machine's data better,
- after turning on settings that only apply at import, such as [custom event flagging](preferences.md#custom-cpap-user-event-flagging) or [Steady Breathing](preferences.md#steady-breathing).

Rebuilding needs the SD card backups (*Create SD Card Backups during Import*, on by default). Your journal notes and bookmarks are kept.

## Purging data

[[idx:Purging data]] [[idx:Purge!current day]] [[idx:Purge!range of days]] [[idx:Purge!all device data]] [[idx:Purge!oximetry]]
The purge commands, under *Data → Advanced*, delete data from OSCAR. They cannot be undone, so consider [backing up the profile](export-backup-share.md#backing-up-a-profile) first. Purged CPAP data can be brought back with *Rebuild CPAP Data* or by importing the card again, as long as the data is still on the card or in OSCAR's backup.

Purge Current Selected Day
:   Removes data for the day shown in the Daily view. Choose what to remove:
    - **CPAP** — the CPAP data.
    - **Oximetry** — the oximetry data.
    - **Sleep Stage** — sleep stage data.
    - **Position** — position data.
    - **All except Notes** — all device data, keeping your journal.
    - **All including Notes** — everything for that day.

Purge Range of Days...
:   Removes data for a range of days you choose.

Purge ALL Device Data
:   Removes all data for one device. Choose the device from the submenu.

Purge Oximetry Data
:   **Current Days** removes the oximetry for the days shown.

Purge All Time Corrections
:   See [Time corrections](#time-corrections).
