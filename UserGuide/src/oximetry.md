---
title: Oximeters and the Oximetry Wizard
index: Supported devices!oximeters; Oximetry Wizard; Oximeters; Viatom; Wellue; O2Ring; Checkme O2; SleepU; EMAY EMO-80; Contec CMS50; Pulox; ChoiceMMed MD300W1; CSV oximetry; Bluetooth!oximeters; USB cable!oximeters; F7; Oximetry_Data folder; SpO2
---

# Oximeters and the Oximetry Wizard

A pulse oximeter measures your blood oxygen saturation (**SpO2**) and pulse rate through a sensor on your finger or wrist. During long apneas and other breathing problems, SpO2 can drop noticeably. Recording it overnight and viewing it next to your CPAP data shows whether your therapy is keeping your oxygen up.

## Supported oximeters

[[idx:Supported devices!oximeters]] [[idx:Oximeters]]

| | Family | Models | How OSCAR gets the data |
|---|---|---|---|
| ![Viatom/Wellue O2Ring](icons/wellue.png){.device} | **Viatom / Wellue** [[idx:Viatom]] [[idx:Wellue]] [[idx:O2Ring]] [[idx:Checkme O2]] [[idx:SleepU]] | O2Ring, O2Ring S, SleepU, Checkme O2 (including Checkme O2 Max), Wearable O2 (O2M), KidsO2, BabyO2, and other Viatom/Lepu sleep oximeters | USB cable, Bluetooth, or data files |
| ![EMAY EMO-80](icons/emay.png){.device} | **EMAY** [[idx:EMAY EMO-80]] | EMO-80 sleep oximeter | Bluetooth (new and not yet tested with a real device — please report how it goes) |
| ![Contec CMS50F](icons/cms50f.png){.device} | **Contec, newer protocol** [[idx:Contec CMS50]] [[idx:Pulox]] | CMS50F v3.7 and later, CMS50H, CMS50I, CMS50D+ v4.6, Pulox PO-400/500 | Serial (USB) cable, or data files |
| ![Contec CMS50D+](icons/oximeter.png){.device} | **Contec, older protocol** | CMS50D+, CMS50E, CMS50F, Pulox PO-200/300 | Serial (USB) cable, or data files |
| ![ChoiceMMed MD300W1](icons/cm300.png){.device} | **ChoiceMMed** [[idx:ChoiceMMed MD300W1]] | MD300W1 | MedView `.dat` files saved by ChoiceMMed's software |
| | **Any oximeter** [[idx:CSV oximetry]] | Anything that can save a CSV file with time, SpO2 and pulse | CSV files ([format below](#csv-file-format)) |

## Bluetooth and USB cable on your computer

[[idx:Bluetooth!permission]] [[idx:USB cable!permission]]
The wizard tells you if Bluetooth or the USB cable is not available. What each system needs:

Windows
:   The standard Windows version of OSCAR can use both Bluetooth and the USB cable. If you were given a version labelled *without Bluetooth*, it can still use the USB cable.

macOS
:   The first time OSCAR uses Bluetooth, macOS asks whether OSCAR may use it. Click **Allow**. If you clicked *Don't Allow*, turn it on again in *System Settings → Privacy & Security → Bluetooth*.

Linux
:   Bluetooth needs BlueZ (installed on almost every desktop distribution) and Bluetooth switched on.
    The `.deb` package installs the permission rule that lets OSCAR read Viatom/Wellue cables. With the AppImage, the wizard offers an **Allow OSCAR to use the cable...** button the first time; it asks for your password once and installs the same rule.

## The Oximetry Wizard

[[idx:Oximetry Wizard]] [[idx:F7]]
Open it with the **Oximetry** button on the left of the main window, *Data → Oximetry Wizard*, or <kbd>F7</kbd>. The wizard has four steps, shown across its top: **Oximeter**, **Method**, **Import** and **Match to CPAP**. The *About oximetry in OSCAR* link at the bottom explains what oximetry can and cannot tell you.

### Step 1: Oximeter {.noindex}

Click the card that matches your oximeter. OSCAR remembers your choice for next time.

If you are not sure which card is yours, plug in the oximeter's cable: a Viatom/Wellue cable is marked **Cable found** when OSCAR detects it.

### Step 2: Method {.noindex}

Choose how to get the data. Only the methods your oximeter supports are shown, each with a note saying whether it is ready.

USB cable
:   [[idx:USB cable!oximeters]] Downloads straight from the oximeter over the cable that came with it. For Viatom/Wellue, close O2 Insight Pro first.
    - *Cable found on USB* — ready to go.
    - *No cable found yet* — plug it in; the next page waits for it.
    - *Cable found, but OSCAR needs your permission to use it* (Linux) — the next page has an **Allow OSCAR to use the cable...** button, which asks for your password once.
    - *This version of OSCAR can't read the oximeter's USB cable* — use another method or another OSCAR package.

Over Bluetooth
:   [[idx:Bluetooth!oximeters]] Downloads wirelessly. For Viatom/Wellue, put the oximeter in standby and close the ViHealth app on your phone (only one program can connect at a time). The oximeter switches itself off after about two minutes without a connection. For EMAY, switch the oximeter on and close EMAY's app.

Through its serial cable
:   For Contec CMS50 models. Plug in the oximeter's USB cable. For the newer models, close Contec's software first; for the older models, choose *upload* in the oximeter's menu when OSCAR asks. For the newer Contec models you can also have OSCAR set the oximeter's clock.

From data files
:   Imports files you already have:
    - Viatom/Wellue: files copied from the oximeter, or exported from the ViHealth app.
    - EMAY: recordings OSCAR downloaded earlier (`.spo2`).
    - Contec: files saved by SpO2Review or a similar program (`.spo`, `.spor`, `.spo2`).
    - ChoiceMMed: MedView `.dat` files.
    - CSV: any CSV file in the format below.

### Step 3: Import {.noindex}

OSCAR connects and lists the recordings on the oximeter, or reads the files you chose. If there are several recordings, choose the ones to import. A progress bar shows the download.

[[idx:Oximetry_Data folder]]
Recordings downloaded over a cable or Bluetooth are also saved in your profile's **Oximetry_Data** folder, so you can import them again later without the oximeter.

For Viatom/Wellue oximeters, OSCAR can set the oximeter's clock when it connects. A correct clock keeps your oximetry lined up with your CPAP data.

### Step 4: Match to CPAP {.noindex}

OSCAR needs to know when the recording started, so it can put the oximetry on the right night next to your CPAP data. Choose one of:

- **I want to use the time reported by my oximeter's built in clock.** Best when the oximeter's clock is correct.
- **I started this oximeter recording at (or near) the same time as a session on my CPAP device.** OSCAR suggests the CPAP session that fits best and says why; choose another from the list if it is wrong. Syncing to a CPAP session is the most accurate choice when the oximeter's clock is unreliable.
Whichever you choose, you can adjust the starting time by hand if it needs it.

!!! note "Contec CMS50D+ has no clock"
    The CMS50D+ does not record when a recording started. If there is no CPAP session to match it to, enter the starting time yourself.

## CSV file format

[[idx:CSV oximetry!file format]]
Choose the **CSV** card to import from any oximeter that can save its readings as text. OSCAR expects one reading per row: date and time, SpO2 (%), and pulse (bpm).

```
Time,SpO2,Pulse
2026-11-02 02:24:16,96,59
2026-11-02 02:24:20,,
2026-11-02 02:24:24,95,61
```

- Date and time as `yyyy-MM-dd HH:mm:ss`. Seconds are optional, a `T` between date and time is fine, and an offset such as `+01:00` may be added; otherwise the time is local.
- Any sampling rate works, because each row has its own time.
- The header row is optional. With a header, the columns can be in any order, and date and time can be two columns (`Date`, `Time`).
- Fields can be separated by commas, semicolons or tabs.
- An empty field or `0` means no reading at that moment.
- A pause of more than 5 minutes starts a new recording, so one file can hold several nights.
- Files saved with *Export as CSV* in Wellue's O2 Insight Pro import as they are.

You can give the device a name in the wizard; otherwise it appears as *Oximeter (CSV)*.


## Seeing and cleaning up oximetry {.noindex}

- Oximetry appears on the [Daily view](daily.md) as SpO2 and Pulse graphs, with events for drops and rapid changes, and on the Overview and Statistics pages.
- The thresholds and the baseline OSCAR uses to flag drops are on the [Oximetry tab of Preferences](preferences.md#oximetry-tab).
- *Data → Automatic Oximetry Cleanup* removes very short scraps of oximetry data (see [Data tools](data-tools.md#automatic-oximetry-cleanup)).
- To remove oximetry for a day, use *Data → Advanced → Purge Current Selected Day → Oximetry* (see [Purging data](data-tools.md#purging-data)).
