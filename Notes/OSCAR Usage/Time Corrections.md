# Time Corrections

*Draft user documentation — OSCAR 2.x*

## What it is for

Every device OSCAR reads — CPAP, oximeter, watch, sleep-stage or position sensor — stamps its data with its own clock. Those clocks are often wrong, and rarely agree with each other:

- the clock was set to the wrong time zone, or never changed for daylight saving time;
- you travelled, and the device stayed on home time (or didn't);
- the clock was reset;
- the clock runs slightly fast or slow, gaining or losing a few seconds every day.

When a clock is wrong, a night shows up at the wrong time of day, and data from two devices does not line up: an oxygen desaturation appears a minute before the apnea that caused it.

Time Corrections lets you tell OSCAR how far off a device's clock was, for one night or a range of nights, so everything is shown on one consistent timeline. It replaces the single **CPAP Clock Drift** setting that older versions had in Preferences. If you had a value there, OSCAR converted it automatically into a correction for your CPAP device that covers all of your data.

There are two tools, both in the **Data** menu:

| Menu item | Use it for |
|---|---|
| *Data / Time Corrections...* | Shifting a device's data by a fixed amount for a night or a range of nights |
| *Data / Drift Analysis...* | A CPAP clock that steadily gains or loses time |

## How it works

A correction is an amount of time, plus or minus, that OSCAR adds to everything a device recorded on the nights the correction covers.

- **+** moves that device's data **later** (to the right on the graphs); **−** moves it **earlier**.
- Each correction belongs to **one device**. Correcting the CPAP does not move the oximeter.
- A correction covers a single night, a range of dates, or everything from a start date onward. Dates are OSCAR days, the same ones you select in the Daily view.
- If more than one correction covers a night, they **add together**. A time-zone correction of +2:00:00 and a fine correction of −0:00:12 give a total shift of +1:59:48.
- Your recorded data is **not changed**. Corrections are kept separately and applied when data is displayed, so deleting a correction puts things back exactly as they were. Corrections are included in profile backups.
- One correction can be at most 23:59:59.

OSCAR also uses the corrected time to decide which day a session belongs to. This is done when data is imported and when a profile is opened, so after saving a correction of several hours a session may appear under a different day the next time you start OSCAR.

### Correction types

Each correction has a type. All types shift the data in the same way; the type records *why*, and sets a few defaults.

| Type | Typical use | Covers by default |
|---|---|---|
| **Offset** | Fine alignment, seconds to a few minutes | The night shown |
| **Travel** | Nights spent in another time zone | The night shown |
| **DST Change** | Clock not changed for daylight saving time | The night shown |
| **Device Timezone Misconfiguration** | Clock set to the wrong time zone | The night shown and every night after it (No end) |
| **Device Reset** | Clock was reset | The night shown |

These are only starting values; the dates are always shown in the editor and you can change them before saving.

Two types behave specially:

- **Device Timezone Misconfiguration** — only one open-ended time-zone correction is in force at a time. Saving a new one automatically ends the previous one on the day before the new one starts.
- **Offset** — if you enter more than 15 minutes, OSCAR suggests using one of the other types. Single-night Offsets are also what Drift Analysis uses as its measurements.

## Using Time Corrections

1. Open the **Daily** view and select a night you want to correct.
2. Choose *Data / Time Corrections...*. The first time, an instructions window appears; click **Continue**. Tick **Don't show this again** to skip it in future.

The Time Corrections window stays open while you work in OSCAR, and the graphs behind it update as you make changes, so you can see the effect of a correction before saving it.

The window is not limited to the day shown in the Daily view. It always lists all of your devices, and a correction can be given any dates. The day shown in Daily is used for three things: it is the starting date of a new correction, the table starts out listing the corrections in force on it, and it is the day whose graphs you see change.

### The window

The window has three titled panels. Start in **Select Device to Correct**; then go to **Edit Correction** to add a correction, or to **Saved Corrections** to find one you saved earlier.

- **Select Device to Correct (top left).** Every device in the profile, with its serial number, under **CPAP devices** or **Other devices**. Hold the mouse over a device to see the dates it has data for and how many corrections it has. A device with no data on the day shown in Daily is greyed but can still be selected. If the same device appears twice with different serial numbers, each one needs its own corrections; the data dates show which is which.
- **Edit Correction (top right).** The device being corrected is named at the top. Below it are *New correction* or *Editing: ...*, the type, the time correction, and the dates covered.
- **Saved Corrections (bottom).** The corrections of the selected device that are in force on the day shown in Daily, with their dates, type, amount, and the date they were added. Rows that apply to the day shown in Daily are in **bold**. In the light theme, rows are coloured by type. Tick **Show all devices** to include the other devices, and **Show all days with corrections** to list every saved correction in date order; the rows that do not apply to the day shown in Daily are greyed, not bold. The line above the table says how many saved corrections are not listed.

### Adding a correction

1. Select the device, and check its name at the top of the editor.
2. Choose the **Type**.
3. Set the **Time Correction**. Click the **+ / −** button to choose the direction, then enter hours, minutes, and seconds. The up and down arrows change whichever field the cursor is in and carry over into the next field; stepping down past zero flips the sign.
4. Set the **Dates Covered**: **From** and **To**, or tick **No end** for a correction that continues on every later night. They start on the day shown in Daily.
5. Read the summary under the dates. It says in words what will be saved, for example "Shifts *device* by +1:00:00 from 2027-03-14 to 2027-11-07." It also tells you if the graphs will not change, either because the dates do not include the day shown in Daily or because the device has no data on that day.
6. Click **Save**.

After saving, a message confirms what was stored and the new correction is highlighted in the table. If its dates do not include the day shown in Daily, OSCAR ticks **Show all days with corrections** so that you can see it.

Below the summary, **Total of all corrections for this device** shows the combined shift on the day shown in Daily, including the change you are making. If it is not what you expect, look in the table for another correction covering that day.

Nothing is stored until you click **Save**. **Cancel** throws away the unsaved change and puts the graphs back. Closing the window does the same. If you move to another day in the Daily view with an unsaved change, OSCAR asks whether to save or discard it.

Other buttons:

- **Reuse Previous** — copies the single-night corrections from the most recent of the previous seven nights that has any, for the selected device, to the day shown in Daily. This is saved immediately.
- **Reset to Zero** — sets the time correction to zero. Saving a time of zero while editing an existing correction removes that correction.
- **Clear** — abandons what is in the editor and starts a new, blank correction.

### Finding, changing or deleting a correction

Tick **Show all days with corrections**, and **Show all devices** if needed, to find a correction that is not for the day shown in Daily.

- **To see its effect**, click the correction and then **Show in Daily** (above the table), or double-click it. Daily moves to the first day in the correction's dates on which that device has data.
- **To change it**, click it. Its values load into the editor; change them and click **Save**.
- **To remove it**, click it and then **Delete Correction**.

What Delete Correction does depends on the day shown in Daily:

- A single-night correction for the day shown is deleted straight away.
- A correction that does not cover the day shown is deleted completely, after OSCAR asks you to confirm.
- For a correction covering a range that includes the day shown, OSCAR asks what to do:
  - **Close from *date* onward** — the correction ends the day before the day shown and earlier nights keep it. Use this when the device's clock has since been fixed.
  - **Remove only *date*** — the day shown is taken out and the rest of the range is kept.
  - **Remove entirely** — the correction is removed from every night.

Rows of type *Drift model* come from Drift Analysis. They can be viewed here but not edited or deleted.

### Recommended order

All devices are aligned to the CPAP's timeline, so work in this order:

1. **Correct the CPAP first.** Fix time-zone, daylight-saving, and travel errors so the night appears at the right time of day.
2. **Apply the same kind of corrections to the other devices** if their clocks had the same problem.
3. **Fine-tune.** Use Offsets on the other devices to bring them into line with the CPAP.

For fine-tuning, zoom in on a short event that both devices recorded. The most reliable are events caused by physical movement: a sudden irregular change in flow rate, a sleep-stage transition, a movement spike. Adjust the other device until the two line up. If the other device shows the event *before* the CPAP does, it needs a **+** correction; if *after*, a **−** correction.

### Examples

**CPAP clock two hours behind because of the wrong time zone.** Select the CPAP, type *Device Timezone Misconfiguration*, time correction **+ 02:00:00**. Set **From** to the first affected night and leave **No end** ticked, then **Save**. When you later fix the clock on the device, go to the first night recorded with the right time, select the correction, click **Delete Correction**, and choose **Close from *date* onward**.

**Clock not changed for daylight saving.** Select the device, type *DST Change*, one hour in the right direction, and set **From** and **To** to the first and last affected nights. You can enter the periods for several years one after another without leaving the window; each appears in the table as you save it. Repeat for every device whose clock had the same problem.

**Oximeter a few seconds off each night.** Select the oximeter, type *Offset*, and adjust the seconds until a desaturation follows its apnea as expected. On following nights, **Reuse Previous** gives you a starting point.

## Drift Analysis

Use this when the CPAP clock steadily gains or loses time, so the Offset needed to line up another device grows a little every night. Instead of adjusting every night by hand, OSCAR works out the rate of drift and corrects the CPAP for it automatically, including on future nights.

### What you need

- A second device whose clock you trust, such as an oximeter or watch that sets its time from a phone. Drift Analysis calls this the **reference device**.
- At least **three nights**, spread over the period, on which you have saved a single-night **Offset** for that reference device that lines it up with the CPAP. More nights, further apart, give a better result.

Each of those Offsets is treated as a measurement of how far ahead of the reference device the CPAP clock was on that night.

### Steps

1. Choose *Data / Drift Analysis...*.
2. Select the CPAP in **Device** and the trusted device in **Reference device**.
3. Set **Range start** and **Range end**. The default is the 30 days up to the day shown in the Daily view.
4. Click **Load Data**. Each Offset appears as a dot on the chart, in milliseconds against date.
5. Click **Fit Model**. OSCAR draws the best straight line through the dots and reports the drift rate in milliseconds per day and a quality figure, **R²** (1.0 is a perfect fit).
6. If the line follows the dots well, click **Use Model**.

**Use Model** is only available when R² is at least 0.5. Between 0.5 and 0.8 OSCAR labels the result a moderate fit. A poor fit usually means one of the nightly Offsets is wrong; correct it in Time Corrections and load the data again.

### What Use Model does

- Adds a drift correction to the **CPAP**, starting at Range start and continuing indefinitely. It shifts the CPAP's data by an amount that changes a little each night.
- Removes the reference device's single-night Offsets inside the range. They are no longer needed because the CPAP is now being moved instead.

The bottom of the window shows the active model, for example "Active drift model: 850.0 ms/day, active from 2026-03-01". OSCAR warns if the rate is more than 5 seconds a day, which normally points to a problem with the measurements rather than a real clock.

### Refining the model later

If the devices slowly move apart again, add Offsets for the reference device on a few recent nights and run Drift Analysis again. The existing model is shown as a grey dashed line and the new fit as a red line. **Use Model** ends the old model the day before Range start and starts the new one there.

## Removing all corrections

*Data / Advanced / Purge All Time Corrections* removes every correction, including drift models, for every device in the profile. OSCAR asks for confirmation first. Your recorded data is not affected; it is simply shown with the devices' original times again.

This is currently the only way to remove a drift model.

## Good to know

- A profile must be open to use either tool.
- Corrections are per profile and per device. A replacement CPAP starts with none.
- If you let OSCAR set an oximeter's clock while importing from it, OSCAR tells you how fast or slow the clock was. Recordings already stored on the oximeter keep the old clock's times; use that figure in Time Corrections to shift them after importing.
