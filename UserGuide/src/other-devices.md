---
title: Other Supported Devices
index: Supported devices!other; Apple Health; Apple Watch; Zeo; Dreem; Somnopose; Sleep stages; Position data; Unmasked Sleep; Weight!from Apple Health
---

# Other Supported Devices

Besides CPAP machines and oximeters, OSCAR can show data from sleep trackers and position sensors on the same timeline as your therapy. Each one is imported from a file with an item on the **Data** menu.

| Device | What OSCAR shows | Menu item |
|---|---|---|
| Apple Watch (through Apple Health) | Sleep stages, heart rate, SpO2, respiratory rate, heart rate variability, breathing disturbances, wrist temperature, weight | *Data → Import Apple Health Data* |
| Zeo sleep manager | Sleep stages | *Data → Import ZEO Data* |
| Dreem headband | Sleep stages | *Data → Import Dreem Data* |
| Somnopose (iPhone app) | Body position and movement | *Data → Import Somnopose Data* |
| Viatom/Wellue oximeters | SpO2, pulse, movement | The [Oximetry Wizard](oximetry.md) |

[[idx:Supported devices!other]] [[idx:Sleep stages]] [[idx:Position data]]

## Apple Health (Apple Watch)

[[idx:Apple Health]] [[idx:Apple Watch]]
OSCAR reads the export file the iPhone Health app produces and imports the watch's:

- sleep stages,
- heart rate, SpO2, respiratory rate and heart rate variability,
- nightly breathing-disturbance score,
- wrist temperature,
- body weight, into your journal.

The import creates two devices: **Apple Watch Sleep** (sleep stages) and **Apple Watch** (vitals). Only nights the watch recorded sleep stages for are imported. Vitals are trimmed to each night's sleep, because the watch records heart rate all day.

### Step 1: Export from the iPhone {.noindex}

1. Open the **Health** app and tap your profile picture at the top right of the Summary screen.
2. Scroll to the bottom and tap **Export All Health Data**. Preparing the export takes a few minutes.
3. When it is ready, a share sheet opens with `export.zip`. Send it to your computer by AirDrop, or choose **Save to Files** to put it in iCloud Drive and download it from [icloud.com](https://www.icloud.com).

You do not need to unzip it: OSCAR reads `export.zip` directly. An unzipped `export.xml` works too.

### Step 2: Import into OSCAR {.noindex}

1. Choose *Data → Import Apple Health Data*.
2. If your profile already has CPAP data, OSCAR asks how much history to import:
    - **Overlapping CPAP history (recommended)** — only the period your CPAP data covers.
    - **Full history** — everything in the export.
    You can run the import again later for more.
3. Select `export.zip`.
4. If your Health data has sleep records from phone apps as well as the watch, choose which source to use for sleep stages. The Apple Watch is preselected.
5. A progress bar shows the import. A summary then says how many sleep and vitals sessions were added, and how many days got a weight.

Re-importing a newer export adds only nights that are not already in OSCAR.

### Weight and BMI

[[idx:Weight!from Apple Health]]
Body-weight readings go into OSCAR's journal, one per night (the last reading of that night). If your height is set in your [profile](profiles.md#editing-a-profile), OSCAR also fills in your BMI. A weight you typed in yourself is never overwritten.

### Unmasked sleep

[[idx:Unmasked Sleep]]
On nights with both watch sleep data and CPAP data, the Daily view's sidebar adds an **Unmasked Sleep** row: how much of the watch's sleep had no mask on, in hours and minutes and as a share of the night. It shows sleep you may be getting without therapy, for example after taking the mask off in the early morning.

## Zeo

[[idx:Zeo]]
Imports sleep-stage data from the Zeo sleep manager's exported CSV file. Choose *Data → Import ZEO Data* and select the file.

## Dreem

[[idx:Dreem]]
Imports sleep-stage data exported from the Dreem headband. Choose *Data → Import Dreem Data* and select the file.

## Somnopose

[[idx:Somnopose]]
Somnopose is an iPhone app that records your sleeping position and movement with the phone's motion sensor. Export its data as CSV, then choose *Data → Import Somnopose Data*. Position data helps show whether your events happen mostly when you sleep on your back.

## Removing this kind of data {.noindex}

Each kind of data can be removed for a single day with *Data → Advanced → Purge Current Selected Day* (**Sleep Stage** or **Position**). See [Purging data](data-tools.md#purging-data).

If your oximetry or sleep-stage recordings cross the day split time (noon on ResMed machines), see the [Oximetry, Sleep Stage and Position Data](preferences.md#oximetry-sleep-stage-and-position-data) setting.
