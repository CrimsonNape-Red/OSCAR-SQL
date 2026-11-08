---
title: Preferences
index: Preferences; Settings, see Preferences; Session splitting; Day split time; Compliance; Clinical Mode; Permissive Mode; AHI!or RDI; RDI; Unintentional leak!calculating; Leak redline; Percentile!preferred; Median; Custom event flagging; Steady Breathing; SpO2!baseline; SpO2!drop thresholds; Pulse!thresholds; Graphics engine; Fusion theme; Fonts; Updates!automatic
---

# Preferences

[[idx:Preferences]]
*File → Preferences* (on macOS, *OSCAR → Settings*) opens the Preferences window. Its settings belong to the open profile, so each profile can have its own. The window has eight tabs: **Import**, **CPAP**, **Clinical**, **Oximetry**, **Events**, **Waveforms**, **General** and **Appearance**.

Some changes only apply after a recalculation or a re-import. OSCAR tells you when that is needed and offers to do it when you click **Ok**.

!!! tip
    Hover over any option in the Preferences window to see a short explanation of it.

## Import tab

[[idx:Preferences!Import tab]]

### Session Splitting Settings

[[idx:Session splitting]] [[idx:Day split time]]
These settings decide which day a session belongs to. They are locked for ResMed machines, which decide this themselves (a warning at the top of the tab says so).

Combine Close Sessions
:   Sessions closer together than this many minutes are kept on the same day.

Ignore Short Sessions
:   Sessions shorter than this many minutes are not displayed. Useful for hiding brief mask-fitting sessions.

Day Split Time
:   Sessions that start before this time belong to the previous day. With the default of noon, a session starting at 2 am counts as part of the night before.

Don't Split Summary Days
:   Keeps summary-only sessions from the same day record together. Read its tooltip before changing it: turning it off can make the figures for summary-only days less accurate.

### Oximetry, Sleep Stage and Position Data

[[idx:Preferences!split recordings at day split time]]
Split recordings at the day split time
:   Cuts oximetry, sleep stage and position recordings at the day split time, so that the part recorded after it appears on the next day. Use it with CPAP machines that end their sessions at the day split time, such as ResMed (which splits at noon), so the oximetry and CPAP graphs for a day cover the same hours. It applies only to data imported after you turn it on.

### Session Storage Options

These options affect how much disk space OSCAR uses and how long imports take.

Create SD Card Backups during Import
:   Keeps a copy of your card's data in the profile's Backup folder. Highly recommended: many machines delete old detailed data from the card, and with the backup OSCAR can rebuild your history. For newer loaders such as BMC and Sefam, the backup holds only the most recent card data (see [SD card backups](importing-cpap.md#sd-card-backups)). Turn it off only if you are very short of space.

Compress SD Card Backups
:   Stores ResMed backups compressed (`.gz`), making them much smaller but the first import slower. OSCAR reads the compressed files directly; ResScan cannot until they are uncompressed. Changing this does not recompress existing backups.

Compress Session Data
:   Makes OSCAR's own data take about half the space, but makes importing and changing days slower. Worth it on a computer with a small drive.

Do not import sessions older than
:   Sessions older than the date you choose are not imported.

### Memory and Startup Options

Pre-Load all summary data at startup
:   Starts OSCAR a little more slowly, but makes the Overview and some calculations faster later.

Keep Waveform/Event data in memory
:   Keeps detailed data in memory after use, to make revisiting days faster. Rarely needed; leave it off unless your computer has a lot of memory.

Import without asking for confirmation
:   Skips unimportant confirmation messages during import.

Auto-Launch CPAP Importer after opening profile
:   Starts an import as soon as you open the profile.

Automatically load last used profile on start-up
:   Skips the Profiles page and opens the profile you used last.

## CPAP tab

[[idx:Preferences!CPAP tab]]

### Calculate Unintentional Leaks When Not Present

[[idx:Unintentional leak!calculating]]
For machines that record only total leak (such as Philips Respironics), OSCAR can estimate unintentional leak by subtracting your mask's intended vent flow. Enter your mask's vent rate at **4 cmH2O** and at **20 cmH2O** (from the mask's instructions). The calculation is linear and does not model the mask's vent curve exactly; if you use several masks, use average values. Changing the values requires a recalculation.

### Custom CPAP User Event Flagging

[[idx:Custom event flagging]]
An experimental way for OSCAR to flag events your machine may have missed, such as borderline apneas. These user flags are not included in the AHI. Turn it on before importing; for data already imported, you need to purge and re-import it.

Flow Restriction (#1)
:   The percentage drop in airflow, from the median, that counts as an event. 20% works well for detecting apneas.

Event Duration
:   How long the restriction must last.

Show in Event Breakdown Piechart
:   Includes the user flags in the [pie chart](daily.md#event-breakdown-pie-chart).

Resync Device Detected Events (Experimental)
:   Uses OSCAR's flagging to improve where the machine's own events are placed on the timeline.

User flags are shown on the Statistics page in Permissive Mode (see [Clinical tab](#clinical-tab)).

### General CPAP and Related Settings

[[idx:AHI!or RDI]] [[idx:RDI]] [[idx:Leak redline]]
Enable Unknown Events Channels
:   Shows flags for device-detected events that have not been identified yet.

Preferred major event index
:   **AHI** (apnea–hypopnea index) or **RDI** (respiratory disturbance index, which also counts RERAs). OSCAR uses your choice throughout.

AHI/Hour Graph Time Window
:   How many minutes of data each point on the AHI/Hour graph covers. It is fixed at 60 minutes and shown greyed out.

Zero Reset
:   Resets the AHI/Hour count to zero at the start of each time window.

Flag leaks over threshold
:   Draws a red line on the Leak graph at the threshold you set, and flags leaks above it as large leaks.

### Preferred Calculation Methods

[[idx:Percentile!preferred]] [[idx:Median]]
How OSCAR summarises a night's or a period's values. Changes need a restart, but no recalculation. ResMed machines do not support changing these, because of how they store summaries.

Middle Calculations
:   **Median** (recommended for ResMed), **Weighted Average** or **Normal Average**.

Upper Percentile
:   The percentile used for "upper" figures. ResMed users should keep 95%, the only value available on summary-only days.

Maximum Calcs
:   **True Maximum** (the highest value) or **99% Percentile** (which ignores the rarest spikes).

Cumulative Indices
:   How indices like AHI are combined over several days. It is shown greyed out and cannot be changed.

### Steady Breathing

[[idx:Steady Breathing]]
*Experimental; Permissive Mode must be enabled.* Steady Breathing (SB) is calculated from flow rate. When enabled, OSCAR creates an SB waveform and SPAN events from your CPAP flow data. It works only for data imported (or re-imported) while it is enabled. To avoid a rebuild later, choose Permissive Mode on the [Clinical tab](#clinical-tab) and turn on Steady Breathing before your first import. The option becomes available as soon as you pick Permissive Mode.

### Settings for Events Tab

Event postcontext size
:   How much extra time is shown after an event when you click it in the Daily view's Events tab.

Consolidate event types
:   Groups related events under one heading in the Events tab.

## Clinical tab

[[idx:Compliance]] [[idx:Clinical Mode]] [[idx:Permissive Mode]]

Compliance defined as
:   The hours of use a night needs to count as compliant. 4 hours is the usual standard.

Select OSCAR Operating Mode
:   - **Clinical Mode** — sessions cannot be disabled, and every session counts in graphs and statistics, the way a clinician sees your data.
    - **Permissive Mode** — you can disable sessions to leave them out of graphs and statistics. It is also required for user event flags in Statistics and for Steady Breathing.

The tab's **Reset Defaults** button is greyed out.

## Oximetry tab

[[idx:Preferences!Oximetry tab]]

### SpO2 Drop Baseline

[[idx:SpO2!baseline]]
OSCAR measures SpO2 drops from a baseline level.

Method
:   - **Rolling (recommended)** — the average of the readings just before, so the baseline follows slow changes through the night.
    - **Whole night** / **First hour** — one baseline for the session, at the percentile set below.
    - **Manual** — a fixed baseline you choose.

Rolling window
:   How far back the rolling baseline looks. Readings that are already part of a drop are left out.

Baseline percentile
:   For *Whole night* and *First hour*: the baseline is the SpO2 level the readings are at or below for this share of the time. The 50th percentile (the median) suits most nights. Higher percentiles cope better with nights spent mostly desaturated, but on steady nights count normal variation as drops.

Manual baseline
:   The fixed level used by the *Manual* method. Useful when the automatic baselines do not suit your data, or to compare days more easily.

### Flag rapid changes in oximetry stats

[[idx:SpO2!drop thresholds]] [[idx:Pulse!thresholds]]
SpO2
:   The percentage drop in SpO2, and how long it must last, to be flagged as a desaturation.

Pulse
:   The change in pulse rate, and the time window it must happen within, to be flagged.

### Other oximetry options

Discard segments under
:   When you run *Data → Automatic Oximetry Cleanup*, oximetry segments shorter than this are removed.

SpO2 Lower Threshold, Pulse Lower Threshold, Pulse Upper Threshold
:   Where the threshold lines are drawn on the SpO2 and Pulse graphs. Turn the lines on from the graph's right-click menu, under **Dotted Lines**.

**Reset Defaults** restores the oximetry settings.

## Events tab

[[idx:Preferences!Events tab]] [[idx:Event colours]]
A table of every kind of event, with a **Search** box to find one.

| Column | What it sets |
|---|---|
| Name | The event type |
| Color | The colour used for the event on graphs |
| Overview | Whether the event gets its own graph on the Overview |
| Flag Type | How the event is drawn (for example a flag, a span or a dot) |
| Label | The short label shown on graphs |
| Details | A description of the event |

**Reset Defaults** restores the defaults. As the warning on the tab says, changing these is possible but not always wise.

## Waveforms tab

[[idx:Preferences!Waveforms tab]]
The same kind of table for waveforms (flow rate, pressure, leak and so on), with **Lower** and **Upper** columns that set the threshold lines drawn on each waveform's graph. **Reset Defaults** restores them.

## General tab

[[idx:Preferences!General tab]] [[idx:Graphics engine]] [[idx:Updates!automatic]]

### General Settings

Show Remove Card reminder notification on OSCAR shutdown
:   Reminds you to put the card back in your machine.

Use MessageBox to display Notifications
:   Shows notifications in a message box rather than the system notification area.

Always save screenshots in the OSCAR Data folder
:   Saves screenshots without asking where.

Add database menu items
:   Adds *New*, *Open*, *Recent* and *Delete* to *File → Database*. Intended for support staff and developers (see [Database menu](database.md#the-database-menu)).

### Graphics Engine (Requires Restart)

Choose **Desktop OpenGL** (the default) or **Software Engine**. Try the software engine if graphs draw incorrectly on your computer.

### Check For Updates

Automatically check for updates
:   Lets OSCAR check for new versions. Test versions do not show this option, because they always check.

Check for new version every … days
:   How often it checks: up to 90 days for release versions, and at least every seven days for test versions.

I want to be notified of test versions
:   For people who want to help test new features early. Release versions show an invitation to the OSCAR testing page instead, unless this option is already turned on.

## Appearance tab

[[idx:Preferences!Appearance tab]] [[idx:Fusion theme]] [[idx:Fonts]]

### Graph Settings

On Opening
:   Which page to show when a profile opens: Profile, Welcome, Daily, Overview or Statistics.

Switch Tabs After Import
:   Which page to show after an import, or **No change**.

Graph Height
:   The default height of graphs, in pixels.

Overlay Flags
:   **Standard Bars** or **Top Markers** for events on the flow waveform.

Line Thickness
:   The thickness of graph lines.

Graph Tooltips / Tooltip Timeout
:   Whether hovering shows values, and how long the tooltip stays visible.

Scroll Dampening
:   Makes scrolling when zoomed in easier on sensitive touchpads. 50 ms is recommended.

Overview Linecharts
:   **Bar Tops** or **Line Chart**.

Grid Line Darkness
:   How dark the horizontal grid lines are. The default is 64.

### Other Visual Settings

Use Anti-Aliasing
:   Smooths graph lines, on screen and in printed reports.

Square Wave Plots
:   Draws some plots as steps rather than slopes.

Skip over Empty Days
:   The Daily view's previous/next buttons skip days with no data.

Allow YAxis Scaling
:   Lets you change a graph's scale by double-clicking its axis numbers.

Combine similar machines
:   Combines machines with the same capabilities in the device-settings table on the Statistics page.

Include Serial Number
:   Shows serial numbers in that table.

Print reports in black and white (monochrome)
:   Better for printers without colour.

Enables SessionBar in Event Flags, SpO2, Pulse, and Movement Graphs
:   With several sessions in a night, draws a thin grey bar for each session at the top of those graphs.

Disable daily graph titles
:   For high-resolution screens where the titles overlap the labels.

Use Fusion theme (OSCAR will restart)
:   Uses Qt's Fusion style instead of your system's, for the same look on every platform.

Use system tooltip colors
:   Shows tooltips, including those on graphs, in your system's colours instead of OSCAR's black on yellow. Follows high contrast themes, and dark mode if it was on when OSCAR started.

### Statistics

Alternating Color Spacing
:   Shades **every 3rd line**, **every 2nd line**, or none (**Disabled**) on the Statistics page.

### Fonts

The font, size, bold and italic for the **Application**, **Graph Text**, **Graph Titles**, **Big Text** and **Details**. These apply across all of OSCAR.
