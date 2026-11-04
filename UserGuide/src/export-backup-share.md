---
title: Exporting, Backing Up and Sharing
index: Export!CSV Export Wizard; CSV Export Wizard; Report Manager; Export!Version 1 CSV; Export!journal notes; Journal Notes!export; Journals!export and import; Backup!profile; .oscar files; Restore!profile; Share Profile; Dropbox; Google Drive; OneDrive; Privacy!when sharing; Printing!reports
---

# Exporting, Backing Up and Sharing

## CSV Export Wizard

[[idx:CSV Export Wizard]] [[idx:Export!CSV Export Wizard]] [[idx:Report Manager]]
*File → Export Data → CSV Export Wizard* exports your data as CSV files that open in a spreadsheet or analysis program. It replaces OSCAR 1.x's CSV export and is far more flexible: you can export any number OSCAR shows, or the underlying data needed to recalculate it.

Exports are defined as **reports**. Each report has one or more **varieties**, which are variations of it, such as a different set of columns or level of detail.

1. Choose the profile (the open one is preselected) and a report.
2. Choose the variety and the date range.
3. Choose where to save the file and export.

To create or change reports, use the report manager inside the wizard:

| Button | What it does |
|---|---|
| New Report | Creates a report definition |
| Copy Report | Copies a report, as a starting point for your own |
| Delete Report | Deletes a report you made |
| New Variety | Adds a variety to the selected report |
| Copy Variety | Copies a variety |
| View Query | Shows the database query behind a variety, for people who know SQL |

## Version 1 CSV Reports

[[idx:Export!Version 1 CSV]]
*File → Export Data → Version 1 CSV Reports...* exports in the same format as OSCAR 1.7. Use it if you have spreadsheets or scripts built for the old format.

Dates
:   The range to export, with **Quick Range** choices and **Start** / **End**.

Resolution
:   - **Daily** — one row per day.
    - **Sessions** — one row per session.
    - **Details** — one row per event.

Filename
:   Where to save the file.

## Journal notes

[[idx:Export!journal notes]] [[idx:Journal Notes!export]]
*File → Export Data → Journal Notes...* writes the notes from your journal to a file, skipping days with no notes.

- **Journal Date Range** — the days to include.
- **Format** — **HTML** (view and print it in a browser) or **Markdown**.
- **Include feelings and weight** — adds those journal values.
- **Open file after export** — opens the file when it is done.

## Journals

[[idx:Journals!export and import]]
*File → Journals → Export Journal...* saves your whole journal (notes, feelings, weight and bookmarks) to a file. *File → Journals → Import Journal* reads it back, for example into another profile. This is how you merge notes and bookmarks from one profile into another.

## Backing up a profile

[[idx:Backup!profile]] [[idx:.oscar files]]
*File → Profiles → Backup Profile...* saves a profile, or part of it, to a single `.oscar` file that any OSCAR 2 installation can restore.

Profile
:   The profile to back up.

Date Range
:   **Range** offers quick choices, or set **From** and **To**. SD card backups are included only when you choose **Everything (including SD card backups)**. Use that for a full backup you can rebuild from later.

Replace personal information with blanks
:   Removes your name and other personal details from the file.

Simplify file name
:   Uses a shorter file name.

Output
:   The **Directory** and **File** name to save to.

Click **Backup**. Keep backups somewhere other than your computer's main drive, such as an external drive or cloud storage.

## Restoring a profile

[[idx:Restore!profile]]
*File → Profiles → Restore Profile...* loads a `.oscar` file, from your own backup or shared by someone else.

1. **Source**: choose **Local file** and click **Browse...**, or choose **Cloud share link**, paste the link and click **Download**.
2. Click **Validate**. The **Package Information** shows the user name, export type, date range, whether personal information was removed, the number of sessions and the size.
3. Under **Restore profile as**, enter the name for the restored profile.
4. If a profile with that name already exists, choose:
    - **Abort** — do not import.
    - **Rename** — import it as a copy under another name.
    - **Replace** — overwrite the existing profile. *This deletes the existing profile's data.* Replace is offered only when the backup includes SD card data, so that the replaced profile can still be rebuilt.
5. Click **Restore**.

## Sharing a profile

[[idx:Share Profile]] [[idx:Dropbox]] [[idx:Google Drive]] [[idx:OneDrive]] [[idx:Privacy!when sharing]]
*File → Profiles → Share Profile...* prepares a copy of your data for someone else to look at in their own OSCAR, with all OSCAR's graphs and details. This is the best way to get help from an experienced user. Personal information is removed.

1. Choose the **Profile** and the **Date Range**: a day, a week, a month, or your own range.
2. Leave **Replace personal information with blanks** ticked.
3. Under **Share via**, choose where the file goes:
    - **a file** in a folder on your computer,
    - **Dropbox**, **Google Drive** or **OneDrive**: click **Sign In...** the first time to connect your account.
4. Click **Share**.
5. For a cloud destination, click **Copy Link** and send the link to the person helping you. For a file, **Open Containing Folder** shows where it was saved.

The other person opens it with *File → Profiles → Restore Profile*, choosing **Cloud share link** or **Local file**.

## Printing reports

[[idx:Printing!reports]]
*File → Print Report* (<kbd>Ctrl</kbd>+<kbd>P</kbd>) prints the current view: the Daily view with its graphs, the Overview, or the Statistics page. To share a single screen quickly, a [screenshot](graphs.md#screenshots) is often easier.
