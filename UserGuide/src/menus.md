---
title: Menu and Keyboard Reference
index: Menus!File; Menus!View; Menus!Data; Menus!Help; Keyboard shortcuts; Show Personal Data
---

# Menu and Keyboard Reference

Every menu item in OSCAR, with what it does and where to read more. On macOS, *Preferences* is under the **OSCAR** menu (as *Settings*), and so is *About OSCAR*.

## File menu

[[idx:Menus!File]]
| Item | What it does |
|---|---|
| Database → New, Open, Recent, Delete | Work with several OSCAR databases. Shown only when turned on in Preferences. [More](database.md#the-database-menu) |
| Import CPAP Card Data | Imports from your CPAP's SD card. <kbd>Shift</kbd>+<kbd>F2</kbd>. [More](importing-cpap.md#importing) |
| Preferences | Opens the Preferences window. [More](preferences.md) |
| Edit Profile | Changes the open profile's details. [More](profiles.md#editing-a-profile) |
| Print Report | Prints the current view. <kbd>Ctrl</kbd>+<kbd>P</kbd>. [More](export-backup-share.md#printing-reports) |
| Export Data → CSV Export Wizard | Exports any data as CSV, using report definitions. [More](export-backup-share.md#csv-export-wizard) |
| Export Data → Version 1 CSV Reports... | Exports in OSCAR 1.7's CSV format. [More](export-backup-share.md#version-1-csv-reports) |
| Export Data → Journal Notes... | Exports journal notes to HTML or Markdown. [More](export-backup-share.md#journal-notes) |
| Profiles → Import from OSCAR... | Imports a profile from OSCAR 1.x. [More](installing.md#importing-one-profile-later) |
| Profiles → Backup Profile... | Saves a profile to a `.oscar` file. [More](export-backup-share.md#backing-up-a-profile) |
| Profiles → Restore Profile... | Restores a profile from a `.oscar` file or a share link. [More](export-backup-share.md#restoring-a-profile) |
| Profiles → Share Profile... | Prepares data to share with another OSCAR user. [More](export-backup-share.md#sharing-a-profile) |
| Journals → Export Journal... | Saves the whole journal to an XML file. [More](export-backup-share.md#journals) |
| Journals → Import Journal | Reads a journal file back in. [More](export-backup-share.md#journals) |
| Exit | Closes OSCAR. |

## View menu

[[idx:Menus!View]] [[idx:Show Personal Data]]
| Item | What it does |
|---|---|
| View Daily | The Daily view. <kbd>F5</kbd>. [More](daily.md) |
| View Overview | The Overview. <kbd>F6</kbd>. [More](overview.md) |
| View Statistics | The Statistics page. <kbd>F4</kbd>. [More](statistics.md) |
| Maximize Toggle | Fills the screen with OSCAR, or restores it. <kbd>F11</kbd> |
| Take Screenshot | Saves a picture of the window. <kbd>F12</kbd>. [More](graphs.md#screenshots) |
| Reset Graphs → Standard - CPAP, APAP | Graph order for CPAP, APAP and basic bilevel. [More](graphs.md#arranging-graphs) |
| Reset Graphs → Advanced - BPAP, ASV | Graph order for bilevel with backup rate, ASV, AVAPS and iVAPS. |
| Reset Graph Heights | Sets all graphs to the same height. |
| Show Line Cursor | A vertical line with values across all graphs. <kbd>Ctrl</kbd>+<kbd>L</kbd>. [More](graphs.md#line-cursor) |
| Show Pie Chart | The event breakdown pie chart on the Daily view. <kbd>F3</kbd> |
| Daily Sidebar | Shows or hides the Daily view's left sidebar. <kbd>F8</kbd> |
| Daily Calendar | Shows or hides the Daily view's calendar. <kbd>F9</kbd> |
| Right Sidebar | Shows or hides the right sidebar. <kbd>F10</kbd> |
| Show Personal Data | Shows your name and details on the Statistics page. [More](statistics.md#personal-data) |

## Data menu

[[idx:Menus!Data]]
| Item | What it does |
|---|---|
| Oximetry Wizard | Imports oximeter data. <kbd>F7</kbd>. [More](oximetry.md#the-oximetry-wizard) |
| Import ZEO Data | Zeo sleep stages. [More](other-devices.md#zeo) |
| Import Dreem Data | Dreem sleep stages. [More](other-devices.md#dreem) |
| Import Apple Health Data | Apple Watch sleep and vitals. [More](other-devices.md#apple-health-apple-watch) |
| Import Somnopose Data | Somnopose position data. [More](other-devices.md#somnopose) |
| Import Viatom/Wellue Data | Opens the Oximetry Wizard, which handles Viatom/Wellue files, cable and Bluetooth downloads. Kept from earlier versions of OSCAR. [More](oximetry.md#the-oximetry-wizard) |
| Import RemStar MSeries Data | Philips Respironics M-Series data. [More](supported-cpap.md#philips-respironics) |
| Time Corrections... | Corrects device clock errors. [More](data-tools.md#time-corrections) |
| Drift Analysis... | Measures and corrects clock drift. [More](data-tools.md#drift-analysis) |
| Automatic Oximetry Cleanup | Removes very short oximetry segments. [More](data-tools.md#automatic-oximetry-cleanup) |
| Rebuild CPAP Data | Re-imports a device from OSCAR's SD card backup. [More](data-tools.md#rebuild-cpap-data) |
| Advanced → Purge Current Selected Day | Removes CPAP, oximetry, sleep stage, position or all data for one day. [More](data-tools.md#purging-data) |
| Advanced → Purge Range of Days... | Removes data for a range of days. |
| Advanced → Purge ALL Device Data | Removes all data for one device. |
| Advanced → Purge Oximetry Data | Removes oximetry data. |
| Advanced → Purge All Time Corrections | Removes every time correction. |

## Help menu

[[idx:Menus!Help]]
| Item | What it does |
|---|---|
| Check For Updates | Checks for a newer version of OSCAR. [More](installing.md#updates) |
| Online Users Guide | Opens the OSCAR help pages on the Apnea Board wiki. |
| Frequently Asked Questions | Opens the OSCAR FAQ. |
| Sleep Disorder Terms Glossary | Opens a glossary of sleep terms. See also the [Glossary](glossary.md). |
| Change Language | Changes OSCAR's language (OSCAR restarts). |
| Troubleshooting → … | Diagnostic and support tools. [More](troubleshooting.md#the-troubleshooting-menu) |
| About OSCAR | Version, credits, licence and release notes. |

## Keyboard shortcuts

[[idx:Keyboard shortcuts]]
| Keys | Action |
|---|---|
| <kbd>Shift</kbd>+<kbd>F2</kbd> | Import CPAP card data |
| <kbd>F3</kbd> | Show or hide the pie chart |
| <kbd>F4</kbd> | Statistics |
| <kbd>F5</kbd> | Daily view |
| <kbd>F6</kbd> | Overview |
| <kbd>F7</kbd> | Oximetry Wizard |
| <kbd>F8</kbd> | Daily sidebar |
| <kbd>F9</kbd> | Daily calendar |
| <kbd>F10</kbd> | Right sidebar |
| <kbd>F11</kbd> | Maximize toggle |
| <kbd>F12</kbd> | Take screenshot |
| <kbd>Ctrl</kbd>+<kbd>L</kbd> | Line cursor |
| <kbd>Ctrl</kbd>+<kbd>P</kbd> | Print report |
| <kbd>Ctrl</kbd>+<kbd>Tab</kbd> | Cycle through the main views |
| <kbd>Ctrl</kbd>+<kbd>B</kbd> / <kbd>I</kbd> / <kbd>U</kbd> | Bold, italic, underline in journal notes |

On macOS, use <kbd>Cmd</kbd> where this table says <kbd>Ctrl</kbd>. On many laptops you must hold <kbd>Fn</kbd> to use the function keys.
