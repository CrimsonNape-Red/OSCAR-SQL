# Overview Page Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cylinder bars with day gaps, a thin hover line with a Details button that opens draggable
per-night popups, and coloured title strips on the Overview page.

**Architecture:** Pure bar geometry and cylinder painting go in a new `Graphs/overviewbars.*`
(unit-tested). `gSummaryChart` and `gSessionTimesChart` use it, draw the hover line and Details
button, drop the yellow/red hover and the numbers tooltip, and emit a signal when Details is
clicked. `Overview` opens an `OverviewDetailsPopup` (new widget) from rows each chart supplies.
`gGraph` gains an optional title colour that turns its rotated title into a coloured strip.

**Tech Stack:** C++17, Qt 6 Widgets/QPainter, QtTest via `tests/AutoTest.h`.

**Spec:** `Notes/UI Redesign/OVERVIEW_PAGE_DESIGN.md`

## Global Constraints

- C++17, Qt 6; must compile on Qt older than 6.5 (no newer-only API without a guard).
- All graph colours go through `GraphTheme` (`ink()` for bars and text, `fill()` for areas); printing stays light (`GraphTheme::PrintScope` is already active in `gGraph::renderPixmap`).
- Existing mouse actions unchanged: left click zoom in, right click zoom out (Ctrl stronger), drag zoom, Shift-drag measure, Shift+click bar opens Daily, Y-axis double-click.
- No schema change. New user-visible strings in `tr()`. File headers + Doxygen per AGENTS.md; new files "Copyright (c) 2026 The OSCAR Team"; add new files to `oscar/oscar.pro` (tests under `test {}`).
- Not in `BUG_FIXES.md`; work stays on branch `overview-page`.
- Cylinder colour stops (agreed): left edge 40% towards black, body 8% towards black at 28% and 62%, highlight 22% towards white at 45%, right edge 45% towards black.

## Review Focus

1. Very long ranges (a year or more, slot < 3 px): no gaps, flat fills, no slowdown versus today. Test: `testGeometryNarrowSlot`.
2. A click on the Details button must not zoom, and a plain click elsewhere must still zoom. Manual check in Task 5.
3. Printing an Overview report while the mouse is over a graph: no line, no button, light colours. Code guard `graph.printing()` in Task 2; manual check in Task 5.
4. Pale or black title colours (AHI light green, journal graphs black) keep a readable strip title. Test: `testContrastingText`.
5. Days with no data show no Details button; a single-slice day (Usage) opens a one-row popup. Checked by hand in Task 5.

---

### Task 1: Bar geometry and cylinder painting helpers

**Files:**
- Create: `oscar/Graphs/overviewbars.h`, `oscar/Graphs/overviewbars.cpp`
- Create: `oscar/tests/overviewbarstests.h`, `oscar/tests/overviewbarstests.cpp`
- Modify: `oscar/oscar.pro` (sources, headers, test lists)

**Interfaces:**
- Produces (namespace `OverviewBars`):
  - `struct BarGeometry { double offset; double width; bool cylinder; };` offset = space before the bar inside its slot.
  - `BarGeometry barGeometry(double slotWidth);`
  - `void paintSegment(QPainter &p, const QRectF &r, const QColor &c, bool top, bool cylinder);` (cylinder: gradient + rounded cap on `top`; else plain fill)
  - `QLinearGradient cylinderGradient(const QColor &c, double left, double width);`
  - `QColor contrastingText(const QColor &background);` (white or black)

- [ ] **Step 1: Write the failing tests** in `OverviewBarsTests`:
  - `testGeometryWideSlot`: `barGeometry(84)` → `width` 59 ± 1, `offset` (84 − width)/2, `cylinder == true`.
  - `testGeometrySmallSlotKeepsOnePixelGap`: `barGeometry(4)` → `width == 3`, gap (`4 − width`) `== 1`, `cylinder == false`.
  - `testGeometryNarrowSlot`: `barGeometry(2.5)` → `width == 2.5`, `offset == 0`, `cylinder == false`.
  - `testCylinderGradientStops`: for `QColor(0x40,0x80,0xff)`, stops at 0, 0.28, 0.45, 0.62, 1.0 equal the colours mixed 40%/8%/22%-white/8%/45% as in Global Constraints.
  - `testContrastingText`: light green `#90EE90` → black; `Qt::black` → white; `#6D4BC9` → white.
- [ ] **Step 2: Build the test target and run `.\test.exe`;** expect compile failure (no `overviewbars.h`).
- [ ] **Step 3: Implement.** Rule: `gap = slot >= 3 ? max(1, round(slot * 0.3)) : 0`; `width = slot − gap`; `offset = gap / 2`; `cylinder = width >= 8`. Contrast by WCAG relative luminance (white if contrast with white ≥ contrast with black). Top cap: rounded rect with x-radius `width/2`, y-radius `min(6, height/2)` on the top edge only.
- [ ] **Step 4: Run `.\test.exe`;** expect all `OverviewBarsTests` pass and the suite count unchanged otherwise.
- [ ] **Step 5: Commit** "Overview: bar geometry and cylinder painting helpers".

### Task 2: Summary charts draw cylinders, gaps, hover line and Details button

**Files:**
- Modify: `oscar/Graphs/gSummaryChart.h`, `oscar/Graphs/gSummaryChart.cpp` (paint loop ~lines 560-660, `mouseReleaseEvent`, `mouseMoveEvent`)
- Modify: `oscar/Graphs/gSessionTimesChart.cpp` (`paint`)
- Modify: `oscar/Graphs/gGraph.h/.cpp` (hover/pinned dates), `oscar/Graphs/gXAxis.cpp` (`gXAxisDay::paint` label style)

**Interfaces:**
- Consumes: Task 1 `OverviewBars::*`.
- Produces:
  - `gSummaryChart` signal `void detailsRequested(gSummaryChart *chart, QDate date, int idx);`
  - `gGraph::setHoverDate(QDate)`, `QDate hoverDate() const`, `setPinnedDates(const QSet<QDate> &)`, `const QSet<QDate> &pinnedDates() const`.

- [ ] **Step 1:** In both paint loops, position each bar with `barGeometry(barw)` inside its slot and draw each slice with `paintSegment(...)` (top = last slice drawn). Keep `GraphTheme::ink()` on colours and monochrome printing as plain fills.
- [ ] **Step 2:** Remove the `Qt::yellow` slice colour, the translucent red `hl_rect` fill and the `graph.ToolTip(...)` calls on hover (both files).
- [ ] **Step 3:** When not `graph.printing()` and the mouse is in a day slot: draw a 1 px line through the slot centre, full plot height, colour `GraphTheme::ink(QColor(0,0,0,110))`; set `graph.setHoverDate(date)`. If the day has data, draw a Details pill (≈22 px high, accent `#6D4BC9`, white text `tr("Details")`) centred on the line at the plot top and remember its rect in `m_detailsRect` with `m_detailsIdx`/`m_detailsDate`.
- [ ] **Step 4:** `mouseReleaseEvent`: if `m_detailsRect` contains the point and no modifier, emit `detailsRequested` and return `true` (blocks zoom); otherwise keep the Shift+click → Daily behaviour; else return `false`. `mousePressEvent` returns `true` inside `m_detailsRect`.
- [ ] **Step 5:** `gXAxisDay::paint`: draw the label for `graph.hoverDate()` bold and labels in `graph.pinnedDates()` bold with a 2 px underline in the graph's title colour (Task 3) or the accent.
- [ ] **Step 6:** Build; run OSCAR on the test profile; check bars, gaps, line and button in light and dark mode; check left/right click still zoom and Shift+click opens Daily.
- [ ] **Step 7: Commit** "Overview: cylinder bars, day gaps, hover line and Details button".

### Task 3: Coloured title strips

**Files:**
- Modify: `oscar/Graphs/gGraph.h/.cpp` (title drawing in `paint`, ~lines 360-385)
- Modify: `oscar/overview.h/.cpp` (`createGraph` callers, `ReloadGraphs`)

**Interfaces:**
- Consumes: `OverviewBars::contrastingText`.
- Produces: `gGraph::setTitleColor(const QColor &)`, `QColor titleColor() const` (invalid = today's plain rotated title; Daily is unchanged).

- [ ] **Step 1:** In `gGraph::paint`, when `titleColor()` is valid, fill the title column (`titleWidth`, full graph height) with `GraphTheme::ink(titleColor())` and draw the rotated title directly with the painter in `contrastingText(...)` (not via the text queue, whose `ink()` would recolour white text).
- [ ] **Step 2:** `Overview::updateTitleColors()`: AHI/RDI → channel `CPAP_AHI`/`CPAP_RDI` colour; Pressure → `CPAP_Pressure`; per-channel graphs → their channel; Usage → `uc` calc colour; Session Times → `QColor(64,128,255)`; Total Time in Apnea → `QColor(255,147,150)`; Weight/BMI/Feeling → their channel colour. Call it after graphs are created and in `ReloadGraphs()`.
- [ ] **Step 3:** Build; check strips in both modes and in a printed (PDF) Overview report.
- [ ] **Step 4: Commit** "Overview: coloured title strips".

### Task 4: Details popups

**Files:**
- Create: `oscar/overviewdetailspopup.h`, `oscar/overviewdetailspopup.cpp`
- Modify: `oscar/Graphs/gSummaryChart.h/.cpp`, `gAHIChart.*`, `gPressureChart.*`, `gUsageChart.*`, `gTTIAChart.*`, `gSessionTimesChart.*` (detail rows)
- Modify: `oscar/overview.h/.cpp` (open, track and close popups), `oscar/oscar.pro`

**Interfaces:**
- Consumes: Task 2 `detailsRequested`, `gGraph::setPinnedDates`; Task 3 `gGraph::titleColor()`; Task 1 `paintSegment`, `contrastingText`.
- Produces:
  - `struct DetailRow { QString value; QString name; QString note; QColor color; double size; };` (in `gSummaryChart.h`; `size` = the part's height in chart units, for the swatch).
  - `virtual QList<DetailRow> gSummaryChart::detailRows(Day *day, int idx);` (base: one row per cached slice, value + calc label).
  - `virtual QString gSummaryChart::detailHeadline(Day *day, int idx);` (base: graph title).
  - `class OverviewDetailsPopup : public QFrame` — `OverviewDetailsPopup(QWidget *parent, const QString &title, const QString &subtitle, const QColor &headerColor, const QList<DetailRow> &rows)`; signal `closed()`.

- [ ] **Step 1:** Implement the popup: header (drag handle, title, close button), subtitle, rows with a painted cylinder swatch (`paintSegment`, height proportional to `size`, min 8 px, max 60 px), value bold + name, note in the row colour. Background/border from the palette (`Theme::color`), header colour = graph title colour, header text `contrastingText`. Drag keeps it inside the parent. Esc closes when it has focus.
- [ ] **Step 2:** Overview: connect each summary chart's `detailsRequested`; on it, build title `"<date> · <detailHeadline>"`, open a popup near the clicked day offset so it does not exactly cover a previous one, keep it in `QList<QPointer<OverviewDetailsPopup>>`, and update that graph's pinned dates; on `closed()` remove the date. Close all in `on_rangeCombo_activated`, the start/end date changes, `ReloadGraphs()`, and `hideEvent` (leaving the tab).
- [ ] **Step 3:** Rows per chart: AHI — per event type: value `/h`, note `"%1 events · %2% of AHI"`, subtitle hours of use; Pressure — Max/95%/Median/Min (whatever slices the mode has) with one-line meanings; Usage — hours, note compliant / below threshold; TTIA — minutes; Session Times — one row per session: start–end, length; base — slices as-is.
- [ ] **Step 4:** Build; open several popups from different graphs and days; drag; close with × and Esc; change range → all close; switch tab → all close; both modes.
- [ ] **Step 5: Commit** "Overview: details popups".

### Task 5: Verify and hand over

- [ ] **Step 1:** Run unit tests (`.\test.exe` in the MSVC test build); expect 0 failures.
- [ ] **Step 2:** Screenshots of Overview in light and dark mode at 2 weeks, 3 months and 1 year; one with two popups open; check Review Focus items 2-5 by hand, including a printed Overview report.
- [ ] **Step 3:** Remove any temporary test code; final build; commit; report to the user with screenshots' locations.
