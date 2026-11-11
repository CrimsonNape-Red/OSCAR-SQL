# Overview Page Redesign: Design

Status: agreed in discussion 2026-11-10; awaiting approval before planning.
Branch: `overview-page` (off `ui-next`). Mockups: the "Overview Redesign" canvas (bar styles, hover
marker, details popups).

## Why

The Overview page is used to see overall trends and to compare the last few days with a longer
period. It does that well today. Two things fall short:

- **Reading one night's bar.** The hover tooltip lists numbers, and it is hard to see from them
  what matters. A picture of each part of the bar beside its value is much easier to read (as in
  SomniCharts, whose Overview popup prompted this).
- **The look.** The flat bars look dated next to the redesigned Welcome, Profile and Statistics
  pages.

## Goals

1. Bars look modern: shaded cylinders, with space between days.
2. Any night's bar can be opened as a popup that shows each part of the bar with its value, and
   several popups can be open at once to compare nights or graphs.
3. Each graph is easy to identify by a coloured title strip on its left.
4. Light and dark mode both work, as the rest of the graphs do (`Graphs/graphtheme.*`).

## Non-goals

- The white light-mode graph background is too glaring, but changing it is deferred.
- The Daily page graphs are not changed.
- The Weight, BMI and Feeling graphs draw lines, not bars; they get the title strip only.

## Anti-goals (a failure even if it works)

- The Overview gets noticeably slower, especially on macOS, where scrolling already lags as the
  number of visible days grows (GitLab #235).
- Existing mouse actions change: left click zooms in, right click zooms out (Ctrl for more), drag
  zooms to a range, Shift-drag measures, Shift+click on a bar opens that night in Daily,
  double-click on the Y axis changes scaling. All keep working as now.
- Printed reports change colour scheme: they stay in light colours whatever the screen mode.

## Decisions

### Bars

- **Style:** each part of a stacked bar keeps OSCAR 2.1.0's shading: bars over 8 px run from the
  channel colour at the left edge to twice as bright at the right (`brighten(c, 2.0)`); bars of
  4-8 px are a plain fill 25% brighter; narrower bars use the channel colour. The top segment is
  flat with 3 px rounded corners. (Revised after the first build: the cylinder shading looked
  darker than 2.1.0 and its domed top was not wanted.)
- **All bar graphs** get the same treatment: AHI, Usage, Session Times, Pressure, Total Time in
  Apnea, and every per-channel summary graph.
- **Colours** are the existing channel and calculation colours (including Preferences choices),
  translated for dark mode by `GraphTheme::ink()` as now.
- **Gaps:** each day has a slot; the bar fills most of it and the rest is a gap between days. Bar
  and gap shrink together as the date range grows; the gap is 15% of the slot. Keep a gap of at least 1 px for as long as
  there is room for it; only when there is not does the gap disappear.
- **Narrow bars:** once bars are too narrow for the shading to show, draw them as plain flat
  fills. This keeps long ranges (many visible days) at today's speed.
- **No drop shadows** (cost); a darker edge stands in for one.

### Hover

- The day under the mouse is marked by a thin, faint vertical line through that day, **in the
  graph under the mouse only**. Its date label is bold.
- This replaces today's hover effect (the part under the pointer turns yellow and a translucent
  red rectangle covers the day).
- The hover tooltip that lists numbers is **removed**. Hovering shows only the line and the
  Details button. There is no hover preview of the popup.

### Details button and popups

- While the line marks a day, a small **Details** button appears at the top of that day in that
  graph. Clicking it opens a popup for that graph and day. Clicking the button must not also
  zoom.
- **Several popups** can be open at once, from any graph and any day. Each stays open, not
  following the mouse, until closed. Each can be dragged by its header.
- **Contents vary by graph and by the parts of the bar.** One row per part: a cylinder swatch
  whose height is proportional to that part, the value with units, and a short line saying what
  it is. Examples from the mockup:
  - AHI: one row per event type: events per hour, number of events, share of the AHI; the
    subtitle gives hours of use.
  - Pressure: Max, 95%, Median, Min with a one-line meaning each.
  - Usage: a single row (hours, and compliance where relevant).
- **Header:** the date and the graph's headline value (for example "Wed Aug 25, 2026 · AHI 4.30")
  and a close button.

### Title strips

- Each graph has a vertical coloured strip on its left carrying the graph's title, replacing
  today's rotated title text.
- Colour: the graph's main channel colour (for per-channel graphs, that channel; AHI and
  Pressure, their channels). Usage, Session Times and Total Time in Apnea, which have no single
  channel, use the colour of their own bars.

## Builder's calls (made during design, open to change)

- Popup header uses the same colour as its graph's title strip, so a popup shows which graph it
  came from.
- Strip and popup header text is white or black, whichever contrasts better with the colour
  (some channel colours are pale, such as AHI's light green; the journal graphs are black).
- A day that has a popup open has its date highlighted in that graph.
- Popups close with their close button or Esc (front-most popup first), and all close when the
  date range changes or the Overview tab is left.
- Popups live inside the Overview page (not as separate desktop windows) and are kept within it
  when dragged.
- Printing: bars keep the cylinder look in light colours; monochrome printing keeps flat bars.
  Popups, the hover line and the Details button never print.
- Each chart supplies its popup rows itself, the way it supplies tooltip text today
  (`tooltipData()` in `gSummaryChart` and its subclasses, which this replaces).
- Gradient brushes are reused across bars of the same colour and width rather than rebuilt per
  bar.
- Session Times popups: one row per session (or mask-on period) with its start, end and length,
  the swatch sized by length.
- Days with no data show the line but no Details button.
- Popups stay where they were placed when the graphs scroll; they are not tied to their bar's
  position.
- The title strip replaces the rotated title in printed reports too, in its colour.

## Constraints

- C++17, Qt 6 (some testers build with Qt older than 6.5); cross-platform; no schema change.
- Bar drawing lives in `gSummaryChart::paint()` (shared by AHI, Usage, Pressure, Total Time in
  Apnea and the per-channel graphs) and `gSessionTimesChart::paint()`; hover handling and the
  tooltip are there too. Mouse handling for zoom is in `gGraph`.
- Colours go through `GraphTheme` (`ink()` for bars and text, `fill()` for areas), which keeps
  printed output light.
- New user-visible strings use `tr()`.
- Redesign work stays on `ui-next` branches and out of `BUG_FIXES.md`.

## Left to the builder

- Exact pixel thresholds: when the gap drops to 1 px and then to none, and when bars fall back to
  flat fills.
- Popup widget structure, drag behaviour, placement of a new popup (near the clicked day, without
  covering an earlier one where possible).
- Wording of each chart's popup rows.
- Details button size and icon, within the look of the mockup.

## Possible later work

- A softer light-mode graph background.
