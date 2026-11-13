# Overview page redesign, version 2

Status: draft for review, 2026-11-12. Nothing here is built yet.
Mockup: https://claude.ai/artifact/TU37PSnMVSNwfC4UpUY3pK (three boards: the page, the graph menu,
a popped-out graph; sample data).

This replaces `OVERVIEW_PAGE_DESIGN.md` as the plan for the page. What was built from that note
(bars, hover line, Details button, popups) stays; its coloured title strips do not.

## Intent

Start the Overview page over from a team mockup: a selection bar at the top, a row of statistics
for the dates shown, and graphs drawn as outlined cards with the title inside and a menu button.
The graph look is meant to carry over to the Daily page later, so it belongs to the shared graph
code, not to Overview alone.

## Decided

**Tab bar.** Unchanged, including the order of the tabs.

**Selection bar.** Moves from the bottom of the page to the top and is restyled: range buttons in
one segmented strip, the dates and number of nights beside it, then the graphs button, layout and
help at the right. The graphs button keeps its count ("4 of 9 Graphs").

**Statistics.** New. A row of figures calculated for the dates visible in the graphs, so they
follow zooming and scrolling and not only the range button. Each figure compares with the same
number of days immediately before; the comparison is left off when there is no earlier data.

**Graphs.**
- Each graph is an outlined card. The title moves from the rotated strip at the left to the top
  left of the card.
- Graphs are still dragged to re-order and resized vertically, as now.
- Kept from the earlier redesign: the vertical line marking the bar under the mouse, the Details
  button, and the Details popups.
- Bars keep their slices and slice colours, and the overall look of the data in each bar.
  Changing colours automatically for dark mode is a possible future change, not part of this.
- The dotted lines a user has switched on are named in the graph's header, each with its colour.
- Dotted lines are new on Overview (today only Daily's line graphs have them). Where a graph
  shows data that Daily also graphs, it offers the same lines Daily offers for that data, drawn
  the way Daily draws them: a thin dotted line in the line's own colour.
- A menu button (three lines) at the top right of each graph opens the graph's menu. Contents vary
  by graph, as the right-click menus do on Daily. Starting point:

  | Section | Items |
  |---|---|
  | Display | Reset graph heights · Pin / Unpin · Pop out · Clone · 100% zoom |
  | Y-axis | Auto scale · Override scale (opens its settings) |
  | Overlays | Dotted lines (choose from those available) |

  Dynamic scale is a Daily feature (double-click on the Y-axis) and is not offered on Overview,
  for now.
- A popped-out graph has the same design and abilities. Its menu has Close in place of Pop out.

**One figure, one calculation.** A dotted line and the statistics box for the same quantity show
the same number, recalculated over the dates visible in the graphs (for example the 95% pressure
across all those nights). Never an average of nightly averages or of nightly percentiles, which is
too hard to interpret.

**Date axis.** The page has one shared date axis, fixed below the graphs; the graphs no longer
each draw their own. All graphs move through time together, so one axis is enough, and it saves
height on screen and in screenshots posted to forums. A popped-out graph draws its own date axis.
The hovered day and days with an open Details popup are marked on the shared axis.

**Old or new page.** Preferences already lets the user choose the redesigned or the original
Welcome page. That becomes one switch for old or new mode, covering Welcome, Overview, and Daily
when it is redesigned. The original Overview is the page as released in 2.1: rotated titles,
yellow and red hover highlight, numbers tooltip, selection bar at the bottom. That look was
removed on `ui-next`, so it has to be restored beside the new one.

## Proposed (not yet confirmed)

Each item is a call made while drawing the mockup, to be settled in review.

1. **Which dotted lines each graph offers.** Daily's line graphs offer Max, the percentile,
   Median and Min for most channels (no Min for AHI, RDI, Snore and Flow Limitation), plus the
   upper threshold on Leak and thresholds on Pulse and SpO2. Proposed mapping:
   - Graphs of a channel Daily also graphs (Pressure, Leak, AHI and the other per-channel
     graphs): the same list, with each line at the figure recalculated over the dates shown.
   - Usage: Daily has no such graph. Average hours of use is a valid line here (confirmed).
     Proposed as well: the profile's compliance hours.
   - Session Times (bars interrupted by mask-off time) and Total Time in Apnea: no lines.
   - Line colours and on/off defaults come from the channel's settings, as on Daily.
2. **Y-axis modes.** The existing modes are Auto-Fit, Defaults and Override. Proposed: keep
   Defaults in the menu as "Default scale" wherever a graph offers it today, so nothing is lost.
3. **Statistics shown.** Proposed, as in the team mockup: average AHI (or RDI), average usage,
   nights at or above the compliance hours, median leak, 95% pressure. Built from the Welcome
   page's metric card and its better/worse/neutral colours. A fixed set in the first version.
4. **Range buttons.** All of today's ranges with shorter labels (1 week, 2 weeks, 1 month,
   2 months, 3 months, 6 months, 1 year, Everything, Custom, Snapshot). Custom shows the start
   and end date fields and goes back to the dates last saved for it (confirmed: Custom must keep
   the saved dates). Snapshot stays a separate button and, as today, sets the custom range to the
   dates on screen. The "reset view to the selected range" button stays.
5. **Graph header.** Proposed: title, unit, then the summary text each graph already draws
   ("2 low usage, 0 no usage, out of 30 days"), then the dotted-line names, then the menu
   button. Dragging the header re-orders; double-click still pins.
6. **Right-click.** Proposed: right-click on a graph's header opens the same menu as the button.
   Right-click inside the plot keeps its present meaning.
7. **Popped-out graph menu.** Proposed: Close, 100% zoom, the Y-axis items and Dotted lines.
   Pin, Clone and Reset graph heights are left out because the window holds one graph.
8. **Details popup header.** With the title strips gone, the popup header keeps the graph's main
   colour so a popup can still be matched to its graph.
9. **Dark mode.** `ui-next` already lightens bar colours that would be unreadable on a dark
    background. Proposed: leave that as it is; no further colour rules in this work.
10. **Printing and image export.** Proposed: printed graphs get the card outline and top-left
    title too, stay light, and leave out the menu button, hover line and Details button.

## Not in this work

- The Daily page (it takes the graph card and menu later).
- Tab order.
- New slice colours or bar shapes.
- A chooser for which statistics are shown.

## Notes for the builder

- The graph frame is drawn by `gGraph` and laid out by `gGraphView`, shared with Daily. The card
  outline, header and menu button need a per-view switch so Daily keeps its present look until
  its own redesign, and so the original Overview can still be shown.
- The context menu is built in `gGraphView` (pin, pop out, clone, 100% zoom, reset heights,
  Y-axis, plots, overlays, dotted lines). The new menu regroups those actions; it should not
  duplicate them.
- Dotted lines exist only for `gLineChart` (`m_dotlines`, built in `gLineChart::SetDay`; drawn
  with `Qt::DotLine` in the colour of `chan.calc[type]`, with the label and a short sample in the
  header). The summary charts already compute minimum, middle, percentile and maximum per
  calculation for their header text; the new lines can read those.
- The statistics row can use `uikit/metriccard` and `uikit/comparison`, as the Welcome page does.
- The page's top bar is `overview.ui`'s frame: range combo, start and end dates, zoom button,
  Hint, Layout, graphs combo.
