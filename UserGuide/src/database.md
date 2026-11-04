---
title: Your Data and the Database
index: OSCAR20_Data!contents; Database; oscar.db; SQLite; SQL access; Backup folder; Logs folder; Database menu
---

# Your Data and the Database

## Where OSCAR keeps your data {.noindex}

[[idx:OSCAR20_Data!contents]] [[idx:oscar.db]] [[idx:Backup folder]] [[idx:Logs folder]]
Everything is in the data folder you chose when you [installed OSCAR](installing.md#choosing-the-data-folder), `OSCAR20_Data` in your Documents folder by default. *Help → Troubleshooting → System Information* shows where it is.

| File or folder | What it holds |
|---|---|
| `oscar.db` | The SQLite database. All the data OSCAR uses is in this one file. |
| `oscar.db-wal`, `oscar.db-shm` | Working files SQLite uses while OSCAR is running. Do not delete them. |
| `Logs` | Debug and connection logs, useful when [reporting a problem](troubleshooting.md#reporting-a-problem). |
| `Profiles` | One folder for each profile. |

Each profile folder contains:

| File or folder | What it holds |
|---|---|
| A folder for each device, such as `ResMed_23123456789` | The device's **Backup** folder: the original SD card data, used to rebuild your data if ever needed. |
| `Oximetry_Data` | Recordings downloaded from oximeters over a cable or Bluetooth. |
| `Journal_…` | Kept for compatibility; the journal itself is in the database. |
| `rxchanges.cache` | A speed-up file for the Statistics page. OSCAR recreates it if it is missing. |

OSCAR also stores a few conveniences, such as the last folder you used, in your computer's settings (the Registry on Windows). If these are lost, OSCAR simply recreates them.

!!! tip "Backing up"
    The safest backup is a `.oscar` file from *File → Profiles → Backup Profile* with **Everything (including SD card backups)** (see [Backing up a profile](export-backup-share.md#backing-up-a-profile)). If you copy the data folder instead, close OSCAR first.

## Reading the database yourself {.noindex}

[[idx:SQLite]] [[idx:SQL access]] [[idx:Database]]
Because OSCAR 2 keeps its data in SQLite, any program that can read SQLite can read your data directly, including the detailed waveforms used for the graphs. Examples are DB Browser for SQLite, spreadsheet programs with a database connector, Python, and R.

- Open the database **read-only**, or work on a copy, so you cannot damage OSCAR's data.
- Close OSCAR, or open the file read-only, to avoid conflicts while OSCAR is writing to it.
- The [CSV Export Wizard](export-backup-share.md#csv-export-wizard)'s **View Query** button shows the SQL behind each export, which is a good starting point for your own queries.

## The Database menu

[[idx:Database menu]]
*File → Database* is for support staff and developers who work with several OSCAR databases. Turn it on with *Add database menu items* in [Preferences → General](preferences.md#general-settings).

New...
:   Creates a new OSCAR database in a folder you choose.

Open...
:   Opens an existing OSCAR database folder.

Recent
:   Reopens a database you used recently.

Delete...
:   Permanently deletes an OSCAR database that is not the one in use.

## Keeping the database healthy {.noindex}

*Help → Troubleshooting* has two database tools:

- **Check database integrity** checks the database for damage.
- **Compress database (rarely needed)** reclaims unused space, for example after purging a lot of data.

See [Troubleshooting](troubleshooting.md#the-troubleshooting-menu).
