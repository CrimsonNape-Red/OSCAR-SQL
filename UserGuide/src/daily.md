---
title: Daily View
index: Daily view; Calendar; Journal; Notes; Feelings; Weight; BMI; Bookmarks; Events tab; Search tab; Sessions!enabling and disabling; Pie chart; Event breakdown; F5; F8; F9; F3
---

# Daily View

The **Daily** view shows one night in full detail: the statistics, settings, events and graphs for every session your devices recorded. Open it with the **Daily** button on the left, *View → View Daily*, or <kbd>F5</kbd>.

The view has three parts:

- the **left sidebar**, with the calendar, the night's details and the journal tabs,
- the **graph area**, with one graph per kind of data,
- the **right sidebar** (*View → Right Sidebar*, <kbd>F10</kbd>), with the main view buttons.

[[idx:Daily view]]

## Moving between days {.noindex}

[[idx:Calendar]] [[idx:F9]]
At the top of the left sidebar:

| Control | What it does |
|---|---|
| ◀ / ▶ | Go to the previous or next day. With *Skip over Empty Days* turned on in [Preferences](preferences.md#other-visual-settings), these jump over days with no data. |
| Calendar button | Shows or hides the calendar (also *View → Daily Calendar*, <kbd>F9</kbd>). |
| Most-recent button | Goes to the most recent day with data. |
| Calendar | Click a date to show it. Days with data are highlighted, and the colour shows whether the day met your [compliance](preferences.md#clinical-tab) target. |

*View → Daily Sidebar* (<kbd>F8</kbd>) hides or shows the whole left sidebar to give the graphs more room.

## The night's details {.noindex}

Below the calendar, OSCAR summarises the night:

- **Device information** — the machine, its mode and model.
- **Event indices** — AHI (or RDI, depending on *Preferred major event index* in [Preferences](preferences.md#general-cpap-and-related-settings)) and the index for each kind of event.
- **Statistics** — a table of minimum, middle, upper percentile and maximum values for pressure, leak, flow limitation, respiratory rate, SpO2, pulse and other channels the device recorded. Which figures are shown follows the [Preferred Calculation Methods](preferences.md#preferred-calculation-methods).
- **Device settings** — the pressure settings, pressure relief, ramp, humidity and other settings the machine reported for that night.
- **Sessions** — each session with its start, end and length (see below).
- **Unmasked Sleep** — on nights with Apple Watch sleep data (see [Apple Health](other-devices.md#unmasked-sleep)).

### Event breakdown pie chart

[[idx:Pie chart]] [[idx:Event breakdown]] [[idx:F3]]
*View → Show Pie Chart* (<kbd>F3</kbd>) adds a pie chart showing how the night's events divide between the different types.

### Sessions

[[idx:Sessions!enabling and disabling]]
A **session** is one continuous period of use: from when you put the mask on until you take it off. One night can have several sessions.

The session list shows each one. Click a session to zoom the graphs to it. Its check box enables or disables it: a disabled session is left out of graphs and statistics, which is useful for a few minutes of mask fitting. Whether you can disable sessions depends on the [operating mode](preferences.md#clinical-tab):

- In **Permissive Mode**, you can disable sessions.
- In **Clinical Mode**, all sessions are always counted, as a clinician would see them.

## The journal tabs {.noindex}

Under the details are four tabs.

### Events tab

[[idx:Events tab]]
Lists every event of the night, grouped by type (obstructive apneas, hypopneas, flow limitations and so on). Click an event to zoom the graphs to it. With *Consolidate event types* turned on ([Preferences → CPAP](preferences.md#settings-for-events-tab)), related events are grouped together; *Event postcontext size* sets how much time is shown after the event.

### Notes tab

[[idx:Journal]] [[idx:Notes]] [[idx:Feelings]] [[idx:Weight]] [[idx:BMI]]
Your journal for the day.

Journal
:   Free text, with bold, italic, underline and colour buttons (<kbd>Ctrl</kbd>+<kbd>B</kbd>, <kbd>Ctrl</kbd>+<kbd>I</kbd>, <kbd>Ctrl</kbd>+<kbd>U</kbd>).

Feelings
:   How you felt, from *Poor* to *Awesome*, with the slider or the number box. The **0..10** button switches the scale between 0–10 and 0–100. A dash means no value is set.

Weight
:   Your weight that day. If your height is set in your [profile](profiles.md#editing-a-profile), OSCAR also shows your **B.M.I.**

Journal notes can be exported to HTML or Markdown with *File → Export Data → Journal Notes* (see [Exporting journal notes](export-backup-share.md#journal-notes)).

### Bookmarks tab

[[idx:Bookmarks]]
Bookmarks mark interesting moments, such as a cluster of apneas or a big leak, so you can find them again.

- Zoom the graphs to the moment, then click **Add Bookmark**. The bookmark stores the time range shown.
- Click a bookmark to return to it. Double-click its text to rename it.
- **Remove Bookmark** deletes the selected one.

Bookmarks can be filtered from the main window's bookmark filter.

### Search tab

[[idx:Search tab]]
Finds days that meet a condition you set, for example days with an AHI above a value, days with a note containing some text, days with bookmarks, or days where a particular event occurred. Click a result to go to that day.

## The graph area {.noindex}

The graphs show the night's data against time. Typical graphs are:

| Graph | Shows |
|---|---|
| Event Flags | A marker for every event the device (or OSCAR) flagged |
| Flow Rate | The air flow, breath by breath. Events are overlaid on it |
| Pressure | The pressure delivered (with EPAP/IPAP on bilevel machines) |
| Leak Rate | Unintentional (or total) leak, with the leak redline if turned on |
| Flow Limitation, Snore, Respiratory Rate, Tidal Volume, Minute Ventilation | As recorded by the device |
| SpO2 and Pulse | From an oximeter |
| Sleep Stage, Position | From sleep trackers and position sensors |

Which graphs appear depends on what your devices record. Use the graph drop-down at the bottom of the view to show or hide graphs, and **Layout** to save and restore graph arrangements. **Hint** shows the graph controls. See [Working with graphs](graphs.md) for zooming, moving and pinning graphs.
