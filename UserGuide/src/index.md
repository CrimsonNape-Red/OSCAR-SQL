---
title: Welcome to OSCAR 2
index: OSCAR!what it is; OSCAR 2!what's new; Medical disclaimer
---

# Welcome to OSCAR 2

<p class="lead">OSCAR, the Open Source CPAP Analysis Reporter, is free software for Windows, macOS and Linux. It reads the data your CPAP, bilevel or ASV machine records, together with data from pulse oximeters and some sleep trackers, and shows it as graphs and statistics you can study night by night.</p>

This guide explains how to install OSCAR 2, bring your data into it, and use each view, menu and setting. Every option is described where it appears.

## What OSCAR does {.noindex}

[[idx:OSCAR!what it is]]
- **Reads your therapy data** from the SD card (or USB stick) of a supported machine: usage, pressure, leaks, events such as apneas and hypopneas, and for most machines the full breath-by-breath flow waveform.
- **Adds oximetry** from a pulse oximeter, so you can see oxygen saturation (SpO2) and pulse next to your breathing.
- **Shows each night in detail** on the [Daily view](daily.md), trends over weeks or months on the [Overview](overview.md), and summary tables on the [Statistics](statistics.md) page.
- **Keeps a journal**: notes, how you felt, your weight, and bookmarks to interesting moments.
- **Exports, backs up and shares** your data, so you can keep it safe or ask someone experienced to look at it.

!!! warning "Medical disclaimer"
    [[idx:Medical disclaimer]]
    OSCAR is not a medical device and is not intended to diagnose or treat any condition. Its figures may differ from those your machine or your clinician's software reports. Talk to your doctor or sleep clinic before changing your therapy.

## What's new in OSCAR 2

[[idx:OSCAR 2!what's new]]
OSCAR 2 replaces OSCAR 1.x. It looks and works much like OSCAR 1.7, but stores everything in a single database, which makes these new features possible:

| Feature | Where to find it | Read more |
|---|---|---|
| SQLite database instead of many separate files | `OSCAR20_Data` folder | [Your data and the database](database.md) |
| CSV Export Wizard: export any number OSCAR shows | *File → Export Data → CSV Export Wizard* | [Exporting data](export-backup-share.md#csv-export-wizard) |
| Back up and restore a whole profile, or a date range, as one `.oscar` file | *File → Profiles* | [Backing up and restoring](export-backup-share.md#backing-up-a-profile) |
| Share part of a profile through a file, Dropbox, Google Drive or OneDrive | *File → Profiles → Share Profile* | [Sharing a profile](export-backup-share.md#sharing-a-profile) |
| Direct SQL access to all data, including waveforms | Any SQLite tool | [Direct database access](database.md#reading-the-database-yourself) |
| Apple Watch data from Apple Health | *Data → Import Apple Health Data* | [Apple Health](other-devices.md#apple-health-apple-watch) |
| A new Oximetry Wizard with USB, Bluetooth and file imports | *Data → Oximetry Wizard* | [Oximeters](oximetry.md) |
| Device time corrections and clock-drift analysis | *Data → Time Corrections*, *Drift Analysis* | [Data tools](data-tools.md) |
| More machines: Yuwell, BMC/React Health G3 and G3X, Sefam, Apex, Löwenstein prisma VENT and more | — | [Supported CPAP devices](supported-cpap.md) |

OSCAR 2 keeps its data in its own folder, `OSCAR20_Data`, separate from the `OSCAR_Data` folder OSCAR 1.x uses. Both versions can be installed side by side. Keep OSCAR 1.x until you are happy with OSCAR 2.

## How to use this guide {.noindex}

- **New to OSCAR?** Read the pages under *Getting started* in order: [Installing OSCAR 2](installing.md), [Profiles](profiles.md) and [Importing CPAP data](importing-cpap.md).
- **Looking for an option or a menu item?** Use the **Search** box at the top of every page, or the A–Z [Index](genindex.html).
- **Wondering what a term means?** See the [Glossary](glossary.md).
- Menu paths are written like *File → Profiles → Backup Profile*. On macOS, *Preferences* and *About OSCAR* are under the **OSCAR** application menu rather than *File* and *Help*.

## Getting help

- *Help → Online Users Guide* and *Help → Frequently Asked Questions* open the OSCAR pages on the Apnea Board wiki.
- The [Apnea Board](https://www.apneaboard.com) forum has a friendly community that helps people read their OSCAR data.
- To report a problem, see [Troubleshooting](troubleshooting.md#reporting-a-problem).
