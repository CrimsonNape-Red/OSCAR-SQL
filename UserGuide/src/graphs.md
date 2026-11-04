---
title: Working with Graphs
index: Graphs!zooming; Graphs!pinning; Graphs!reordering; Graphs!resizing; Graphs!right-click menu; Graphs!layouts; Dotted lines; Line cursor; Ctrl+L; Graph order; Reset graph heights; Screenshot; F12; Y axis scaling
---

# Working with Graphs

The Daily view and the Overview use the same kind of graphs, and they work the same way. The **Hint** button under the graphs shows a short reminder of these controls.

## Zooming and scrolling

[[idx:Graphs!zooming]]
| Action | Result |
|---|---|
| Drag across a graph with the left mouse button | Zoom in to that period. All graphs follow. |
| Scroll wheel | Zoom in or out around the pointer. |
| Drag with the right mouse button, or <kbd>Shift</kbd>+scroll | Move earlier or later in time while zoomed in. |
| Double-click | Zoom back out (on the Daily view, to the whole night). |
| Click an event in the Events tab or a session in the session list | Zoom to it. |

*Scroll Dampening* in [Preferences → Appearance](preferences.md#graph-settings) makes zoomed-in scrolling easier on sensitive touchpads.

## Line cursor

[[idx:Line cursor]] [[idx:Ctrl+L]]
*View → Show Line Cursor* (<kbd>Ctrl</kbd>+<kbd>L</kbd>) draws a vertical line through all graphs at the pointer, with the values at that moment, so you can compare channels at the same instant.

## Arranging graphs

[[idx:Graphs!reordering]] [[idx:Graphs!resizing]] [[idx:Graphs!pinning]] [[idx:Graphs!layouts]]
- **Reorder**: drag a graph by its title on the left to move it up or down.
- **Resize**: drag the bottom edge of a graph to make it taller or shorter.
- **Pin**: right-click a graph and choose **Pin *name* Graph** to keep it at the top while you scroll through the others. **Unpin** releases it.
- **Show or hide**: use the graph drop-down at the bottom of the view.
- **Save a layout**: **Layout** saves the current arrangement and restores saved ones.

[[idx:Reset graph heights]] [[idx:Graph order]]
To start again:

- *View → Reset Graph Heights* sets all graphs to the same height (the *Graph Height* in [Preferences](preferences.md#graph-settings)).
- *View → Reset Graphs → Standard - CPAP, APAP* puts the graphs in an order that suits CPAP, APAP and basic bilevel therapy.
- *View → Reset Graphs → Advanced - BPAP, ASV* puts them in an order that suits bilevel with backup rate, ASV, AVAPS and iVAPS.

## The right-click menu

[[idx:Graphs!right-click menu]] [[idx:Dotted lines]]
Right-click a graph for options that apply to it:

Plots
:   Choose which data lines are drawn on the graph (for example, minimum and maximum pressure as well as actual pressure).

Dotted Lines
:   Draw reference lines, such as percentiles or the SpO2 and pulse thresholds set on the [Oximetry tab of Preferences](preferences.md#other-oximetry-options).

Oximeter Overlays
:   On CPAP graphs, overlay oximetry events such as SpO2 drops.

Hide All Events / Show events
:   Hide or show the event markers on the graph.

Pin / Unpin Graph
:   See above.

Reset Graph Heights
:   Resets all graphs to a uniform height.

## Y axis

[[idx:Y axis scaling]]
When *Allow YAxis Scaling* is on in [Preferences → Appearance](preferences.md#other-visual-settings), double-click the numbers on a graph's left edge to set the scale yourself: automatic, a fixed range, or the default range for that kind of data.

## Screenshots

[[idx:Screenshot]] [[idx:F12]]
*View → Take Screenshot* (<kbd>F12</kbd>) saves a picture of the OSCAR window. It is the easiest way to share a night on a forum. With *Always save screenshots in the OSCAR Data folder* on ([Preferences → General](preferences.md#general-tab)), OSCAR saves without asking where.

*View → Maximize Toggle* (<kbd>F11</kbd>) fills the screen with the OSCAR window before you take the screenshot.

## How events are drawn {.noindex}

*Overlay Flags* in [Preferences → Appearance](preferences.md#graph-settings) chooses whether events on the Flow Rate graph are drawn as **Standard Bars** (vertical bars across the waveform) or **Top Markers** (small marks along the top). *Line Thickness*, *Square Wave Plots* and *Use Anti-Aliasing* change how lines look.
