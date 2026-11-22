# Overview Page Redesign v2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild the Overview page as outlined graph cards with a top selection bar, a statistics row,
a per-graph menu, dotted lines and one shared date axis, selectable against the original 2.1 page
by one old/new switch.

**Architecture:** The graph frame is drawn by `gGraph` and laid out by `gGraphView`, both shared
with Daily, so the new look is a per-view setting (`GraphLook::Card`) that only Overview turns on;
`GraphLook::Classic` restores the 2.1 Overview drawing. New page chrome (toolbar, statistics row,
shared date axis) is added as separate widgets that drive Overview's existing slots and hidden
`overview.ui` controls, so range, layout and graph-list logic is not rewritten.

**Tech Stack:** C++17, Qt 6.12 widgets + QPainter, MSVC 2022, qmake/jom. Unit tests: QtTest suites
under `oscar/tests` (`DECLARE_TEST`).

**Spec:** `Notes/UI Redesign/OVERVIEW_PAGE_DESIGN_V2.md` (mockup linked there).

## Global Constraints

- Work in `C:\OSCAR\OSCAR-next` on branch `overview-v2` (off `ui-next`). Never master; nothing in
  `BUG_FIXES.md` or `release_notes.html`.
- Daily must look and behave exactly as it does now. Every change to `gGraph`, `gGraphView`,
  `gXAxis*` is behind `GraphLook`.
- Qt 6 APIs only, no `#if QT_VERSION` guards, cross-platform, no files under
  `SleepLib/thirdparty`. New files get the file header, "Copyright (c) 2026 The OSCAR Team",
  Doxygen comments, and an entry in `oscar/oscar.pro`.
- Colours come from `Theme` / `GraphTheme` so light and dark both work; printed graphs stay light
  (`GraphTheme::PrintScope`).
- Bars keep their slices, slice colours and shading (`OverviewBars`, 15% gap, flat tops).
- Keep: hover line, Details button, Details popups (`OverviewDetailsPopup`), unchanged.
- Tab order unchanged. Dynamic scale is not offered on Overview.
- Commits: ask first; message via `git commit -F <file>`; no Co-Authored-By trailer.
- Build: `build_next_msvc.bat`; tests: `build_next_tests.bat` (both in the session scratchpad;
  exit code = number of failures; `tests.txt` in the UnitTests build dir holds the last suite).
- Visual checks use in-app `grab()` images from a temporary, env-gated harness in
  `mainwindow.cpp` marked `TEMP-OVW-DIAG` (scratchpad `ovw_harness.py add|remove`), removed
  before every commit.

## Review Focus

1. **Empty or sparse data:** a profile with no days, a range with no CPAP days, or one night.
   Statistics show a dash and no comparison; graphs show their empty state; no division by zero.
2. **Switching old/new in Preferences while the page is live:** popups open, a graph popped out,
   Custom range active. The page is rebuilt; nothing dangles; saved heights, order, pin and
   visibility carry across both looks.
3. **Narrow window:** the graph header never draws text over the menu button; summary text is
   elided, then dotted-line names are dropped; the toolbar wraps to a second row.
4. **Zoom and scroll:** statistics, the shared axis and dotted-line values follow the visible
   dates, including a single visible day and "Everything" on a multi-year profile.
5. **Popped-out and printed graphs:** each draws its own date axis; neither shows the menu
   button's hover state, the hover line or the Details button when printing.

---

### Task 1: One old/new switch and the graph look setting

**Files:**
- Modify: `oscar/SleepLib/appsettings.h`, `oscar/preferencesdialog.cpp`, `oscar/preferencesdialog.ui`,
  `oscar/mainwindow.h/.cpp`, `oscar/Graphs/gGraphView.h/.cpp`, `oscar/overview.cpp`
- Create: `oscar/Graphs/graphlook.h`
- Test: `oscar/tests/graphcardtests.h/.cpp` (created here, extended in Task 3)

**Interfaces:**
- Produces: `enum class GraphLook { Classic, Card };` (`graphlook.h`);
  `void gGraphView::setLook(GraphLook)`, `GraphLook gGraphView::look() const` (default Classic),
  `bool gGraphView::cardLook() const`; a popout view copies its parent's look.
  `bool AppSettings::useNewPages() const` / `setUseNewPages(bool)` stored under the existing key
  `"UseNewWelcome"` (so current choices carry over); `useNewWelcome()` is removed and callers
  renamed. `void MainWindow::rebuildOverviewPage()` recreates `overview` in the same tab position.

- [ ] Step 1: Rename the setting accessors; Preferences checkbox text "Use the redesigned pages",
  tooltip "Show the redesigned Welcome and Overview pages. Untick to go back to the original
  pages."
- [ ] Step 2: Add `graphlook.h`, the `gGraphView` look members, and `Overview` setting
  `GraphLook::Card` on its view when `useNewPages()` (before `CreateAllGraphs`).
- [ ] Step 3: `MainWindow`: when the switch changes in Preferences, call `rebuildWelcomePage()`
  and `rebuildOverviewPage()`; the latter closes detail popups first and keeps the current tab.
- [ ] Step 4: Build, run all unit tests. Expected: 0 failures.
- [ ] Step 5: Commit.

### Task 2: Classic look restores the 2.1 Overview drawing

**Files:**
- Modify: `oscar/Graphs/overviewbars.h/.cpp`, `oscar/Graphs/gSummaryChart.h/.cpp`,
  `oscar/Graphs/gSessionTimesChart.cpp`, `oscar/Graphs/gXAxis.cpp`, `oscar/Graphs/gGraph.cpp`,
  `oscar/overview.cpp`
- Test: `oscar/tests/overviewbarstests.h/.cpp`

**Interfaces:**
- Consumes: `gGraphView::cardLook()`.
- Produces: `BarGeometry OverviewBars::classicBarGeometry(double slotWidth)` → offset 0,
  width = slot, `shaded` = slot > 8. `void OverviewBars::paintClassicSegment(QPainter&, const
  QRectF&, const QColor&, double barWidth)` (fill by 2.1's three width bands, square top).

Reference for the 2.1 behaviour: `git show f4f133fb:oscar/Graphs/gSummaryChart.cpp` and
`...gSessionTimesChart.cpp` (the last versions before the first redesign, already dark-mode aware).

- [ ] Step 1: Failing tests `testClassicGeometryHasNoGap` (slot 84 → offset 0, width 84, shaded;
  slot 6 → not shaded) and `testClassicSegmentSquareTop` (40×40 segment: pixel (0,0) painted).
- [ ] Step 2: Run tests, see both fail.
- [ ] Step 3: Implement the two helpers.
- [ ] Step 4: In `gSummaryChart::paint` and `gSessionTimesChart::paint`, when the view is Classic:
  classic geometry and segments, black outlines, the yellow slice and red day highlight, and the
  numbers tooltip (`tooltipData`), with no hover line, Details button or bold axis dates. Card
  look keeps the current code. `mousePressEvent`/`mouseReleaseEvent` ignore the Details button in
  Classic. `Overview` connects `watchDetails` only in Card look.
- [ ] Step 5: Remove the coloured title strip from `gGraph::paint` (2.1 had none; Card look has no
  title column). `gGraph::titleColor()` stays as the colour Details popups use for their header.
- [ ] Step 6: Tests pass; harness grab of Overview with the switch off matches 2.1 behaviour
  (hover turns the slice yellow, day washed red, tooltip shown, bars touching).
- [ ] Step 7: Commit.

### Task 3: Graph card frame and header

**Files:**
- Create: `oscar/Graphs/graphcard.h/.cpp`
- Modify: `oscar/Graphs/gGraph.h/.cpp`, `oscar/Graphs/gGraphView.h/.cpp`,
  `oscar/Graphs/gSummaryChart.cpp`, `gUsageChart.cpp`, `gSessionTimesChart.cpp`, `gTTIAChart.cpp`,
  `gAHIChart.cpp`, `gPressureChart.cpp` (header text position only), `oscar/oscar.pro`
- Test: `oscar/tests/graphcardtests.h/.cpp`

**Interfaces:**
- Produces (namespace `GraphCard`, all sizes in device-independent px, pure functions):
  `constexpr int Radius = 10, Padding = 10, HeaderHeight = 26, Gap = 8, MenuButton = 22;`
  `QRect headerRect(const QRect &card)` (top band inside the padding);
  `QRect menuButtonRect(const QRect &card)` (right end of the header, vertically centred);
  `bool inHeader(const QRect &card, const QPoint &p)`;
  `QString elideSummary(const QString &text, const QFontMetrics &fm, int available)`.
  On `gGraph`: `QRect headerTextRect() const` (space between the title/unit and the legend, valid
  after paint; empty in Classic), `void setHeaderLegendWidth(int)` (layers reserve room at the
  right), `QRect cardRect() const`.
  On `gGraphView`: `bool inTitleZone(gGraph *g, int x, int yInGraph) const` (Classic: the existing
  `x < titleWidth + 20` test; Card: `GraphCard::inHeader`), `int graphGap() const` (Classic:
  `graphSpacer`; Card: `GraphCard::Gap`).

- [ ] Step 1: Failing tests: `testHeaderInsideCard` (card 0,0 600×200 → header 10,6 580×26),
  `testMenuButtonAtTopRight` (button right edge = 590, inside the header, 22×22),
  `testInHeader` (point (300,15) true; (300,60) false; (300,-1) false),
  `testElideSummary` (text wider than the space ends with an ellipsis and fits; zero space → "").
- [ ] Step 2: Run, see them fail.
- [ ] Step 3: Implement `graphcard.*`.
- [ ] Step 4: `gGraph::paint`, Card look: fill the card with `GraphTheme::background()`, outline it
  with `Theme` line colour and `Radius`; no rotated title or title column; top margin =
  `HeaderHeight`; draw the title bold and the units muted at the header's left; side and bottom
  padding `Padding`. Pinned graphs keep the pin icon, drawn at the header's left before the title.
- [ ] Step 5: `gGraphView`: paint the area between cards with the window colour; use `graphGap()`
  wherever layout uses `graphSpacer`; replace each title-column hit test in
  `mousePressEvent`, `mouseMoveEvent`, `mouseReleaseEvent`, `mouseDoubleClickEvent` and
  `wheelEvent` with `inTitleZone()`, so dragging the header re-orders and double-click pins;
  `titleWidth` is 0 in Card look. The bottom-edge resize zone is unchanged.
- [ ] Step 6: Summary charts draw their summary text (`afterDraw` and its overrides) in
  `graph.headerTextRect()` through `elideSummary` when the rect is not empty; otherwise where
  they draw it today.
- [ ] Step 7: Tests pass; harness grabs (2 weeks, 3 months) in light and dark show cards, titles
  and summary text; dragging a header re-orders (harness logs graph order before and after).
- [ ] Step 8: Commit.

### Task 4: Graph menu button and regrouped menu

**Files:**
- Modify: `oscar/Graphs/gGraph.cpp`, `oscar/Graphs/gGraphView.h/.cpp`, `oscar/uikit/lineicons.h/.cpp`

**Interfaces:**
- Consumes: `GraphCard::menuButtonRect`, `gGraphView::cardLook()`.
- Produces: `UiKit::LineIcon::Menu` (three lines). `void gGraphView::showGraphMenu(gGraph *g, const
  QPoint &globalPos)`. `bool gGraphView::isPopout() const` (true for a view created by
  `popoutGraph`). `void gGraphView::closePopout()` closes the view's `QDockWidget`.

- [ ] Step 1: `gGraph::paint` draws the menu button in Card look (not when printing), highlighted
  while the mouse is over it. A left click on it calls `showGraphMenu` with the button's
  bottom-right; a right click in the header calls it at the pointer. Right-click in the plot keeps
  its present meaning.
- [ ] Step 2: `populateMenu` in Card look builds sections with `QMenu::addSection`:
  **Display**: "Reset graph heights", "Pin" / "Unpin", "Pop out", "Clone" / "Remove clone",
  "100% zoom"; **Y-axis**: checkable "Auto scale", "Default scale", "Override scale…" (the last
  opens the existing min/max editor); **Overlays**: the existing Plots, CPAP Overlays, Oximeter
  Overlays and Dotted Lines submenus when they have entries. Classic look keeps today's menu.
- [ ] Step 3: Popout views: "Close" replaces "Pop out"; Pin, Clone and Reset graph heights are
  hidden.
- [ ] Step 4: Harness: click the button on the AHI graph, grab the open menu; choose Pin and log
  `isPinned()`; pop out and grab the popout with its menu.
- [ ] Step 5: Commit.

### Task 5: Dotted lines on summary charts

**Files:**
- Create: `oscar/Graphs/summarylines.h/.cpp`
- Modify: `oscar/Graphs/gSummaryChart.h/.cpp`, `gUsageChart.h/.cpp`, `gAHIChart.h/.cpp`,
  `gSessionTimesChart.h`, `gTTIAChart.h`, `oscar/Graphs/gGraphView.cpp`, `oscar/overview.cpp`,
  `oscar/database/preferences_repository.cpp`, `oscar/oscar.pro`
- Test: `oscar/tests/summarylinestests.h/.cpp`

**Interfaces:**
- Produces: `struct SummaryLine { int id; QString label; double value; QColor color; bool
  enabled; };` with ids `enum SummaryLineId { SL_Min, SL_Middle, SL_Perc, SL_Max, SL_Threshold };`
  `virtual QVector<SummaryLine> gSummaryChart::lines()` (values for the dates last painted);
  `void gSummaryChart::setLineEnabled(int id, bool)`, `bool lineEnabled(int id) const`.
  Namespace `SummaryLines`: `QString encode(const QHash<QString, quint32> &masks)` and
  `QHash<QString, quint32> decode(const QString &)` in the form `name=mask;name=mask`;
  `double percentile(QVector<double> values, double fraction)`.
  Profile preference key `"OverviewDottedLines"` (added to `generalKeys`).

Which lines (spec, Proposed 1): a chart whose channel Daily graphs offers Daily's set for that
channel (`gLineChart::SetDay`'s rules: Max; percentile and middle; Min except AHI, RDI, Snore,
Flow Limitation; upper threshold on Leak; thresholds on Pulse and SpO2), each recalculated over
the visible dates with the profile's range functions (`calcMin`, `calcPercentile(code, 0.5)` or
`calcWavg` per the middle-calculation preference, `calcPercentile(code, prefCalcPercentile)`,
`calcMax`), the same calls the statistics row uses, so a line and its box agree; never an
average of nightly figures. With label and colour from `schema::channel[code].calc[type]` and default from its `enabled`.
AHI: Max, percentile and middle of the nightly index. Usage: the middle value labelled as the
profile's middle calculation (Median / Average) and "Compliance" at
`p_profile->cpap->complianceHours()`, the latter on by default. Session Times and Total Time in
Apnea return none.

- [ ] Step 1: Failing tests: `testEncodeDecodeRoundTrip` ({"AHIBreakdown":5,"Usage":16} survives;
  decode("") is empty; a malformed entry is skipped), `testPercentile` ({1,2,3,4,5}, 0.95 → 5;
  0.5 → 3; empty → 0).
- [ ] Step 2: Run, see them fail. Step 3: implement `summarylines.*`.
- [ ] Step 4: `lines()` in `gSummaryChart` and the overrides above.
- [ ] Step 5: Card look paint: each enabled line as a `Qt::DotLine` (width as `gLineChart` uses) at
  its value across the plot, drawn over the bars; legend in the header right of the summary text
  (label, value, short sample), dropped from the left when space runs out
  (`setHeaderLegendWidth`).
- [ ] Step 6: Menu: "Dotted lines" submenu lists `lines()` as checkboxes with value and colour
  sample; toggling repaints and saves. `Overview` loads the preference after `CreateAllGraphs`
  and saves on change.
- [ ] Step 7: Tests pass; harness grab with Median on AHI and both lines on Usage.
- [ ] Step 8: Commit.

### Task 6: One shared date axis

**Files:**
- Create: `oscar/overviewdateaxis.h/.cpp`
- Modify: `oscar/Graphs/gXAxis.h/.cpp`, `oscar/Graphs/gGraph.h/.cpp`, `oscar/Graphs/gGraphView.h/.cpp`,
  `oscar/overview.h/.cpp`, `oscar/oscar.pro`
- Test: `oscar/tests/dateaxistests.h/.cpp`

**Interfaces:**
- Produces: `struct DayLabel { double x; QString text; bool bold; bool underlined; };`
  `QVector<DayLabel> gXAxisDay::layoutLabels(qint64 minx, qint64 maxx, double width, const
  QFontMetrics &fm, const QDate &hover, const QSet<QDate> &pinned)` (static, pure; the label
  spacing and suppression rules `gXAxisDay::paint` uses now, which then calls it).
  `void gGraphView::setSharedXAxis(bool)`, `bool sharedXAxis() const` (false on popouts);
  `QRect gGraph::plotRect() const` (centre layer rect of the last paint).
  `class OverviewDateAxis : public QWidget` with `void setView(gGraphView *)`; it reads the first
  visible graph's `plotRect()` for its left and right, the view's X bounds, the hovered date of
  whichever graph has one and the union of pinned dates, and repaints on the view's
  `updateRange` signal.

- [ ] Step 1: Failing tests: `testLabelsFitWidth` (30 days in 1190 px → no two labels overlap,
  first label ≥ 0, last ≤ width), `testHoverIsBoldAndSuppressesNeighbours`,
  `testPinnedIsUnderlined`, `testSingleDay` (one label).
- [ ] Step 2: Run, see them fail. Step 3: extract `layoutLabels`; `gXAxisDay::paint` uses it.
- [ ] Step 4: `gGraph::paint` skips `gXAxisDay` layers (no height reserved) when the view has
  `sharedXAxis()` and is not printing. `Overview` (Card look) sets it, and adds an
  `OverviewDateAxis` under the graph area, outside the scrolling list.
- [ ] Step 5: Tests pass; harness grabs: page shows one axis, hover in any graph bolds the date
  on it, a popped-out graph and `renderPixmap(..., true)` both show their own axis.
- [ ] Step 6: Commit.

### Task 7: Selection bar at the top

**Files:**
- Create: `oscar/overviewtoolbar.h/.cpp`
- Modify: `oscar/overview.h/.cpp`, `oscar/oscar.pro`

**Interfaces:**
- Produces: `class OverviewToolbar : public QWidget` with signals `rangeChosen(int comboIndex)`,
  `resetViewClicked()`, `layoutClicked()`, `helpClicked()`; slots `setRangeIndex(int)`,
  `setDates(const QDate &start, const QDate &end, int nights)`; `void adoptDateEdits(QDateEdit
  *start, QDateEdit *end)` and `void adoptGraphCombo(QComboBox *)` (reparents Overview's existing
  widgets into the bar; the date edits show only for Custom).

- [ ] Step 1: Build the bar: a segmented strip of buttons "1 week", "2 weeks", "1 month",
  "2 months", "3 months", "6 months", "1 year", "Everything", "Custom…", "Snapshot" mapped
  one-to-one to `rangeCombo` indices 0–9, so Custom keeps its saved dates and Snapshot sets the
  custom range to the dates on screen, exactly as the combo does today; the dates and "N nights"; a reset-view icon button; then, right-aligned, the adopted
  graphs combo (it keeps its "N of M Graphs" text), a layout icon button and a help button.
  Styled with `Theme`; restyles on palette change; wraps to two rows when narrow.
- [ ] Step 2: `Overview` (Card look): hide `ui->frame`, put the toolbar at the top of the page
  layout, connect its signals to `on_rangeCombo_activated`, `on_zoomButton_clicked`,
  `on_layout_clicked`, `on_graphHelp_clicked`; update it from `on_XBoundsChanged` and wherever
  the range combo changes. Classic look is untouched.
- [ ] Step 3: Harness: choose each range button and log the X bounds; grab at 1280 and 800 px wide.
- [ ] Step 4: Commit.

### Task 8: Statistics row

**Files:**
- Create: `oscar/overviewstats.h/.cpp`
- Modify: `oscar/overview.h/.cpp`, `oscar/oscar.pro`
- Test: `oscar/tests/overviewstatstests.h/.cpp`

**Interfaces:**
- Produces (namespace `OverviewStats`): `struct Range { QDate start, end; };`
  `Range previousRange(const Range &shown)` (same number of days, ending the day before `start`);
  `enum class Verdict { Good, Bad, Neutral };`
  `struct Stat { QString title, value, unit, change; Verdict verdict; };`
  `Verdict verdict(double now, double before, UiKit::Better better)` (`UiKit::Better` from
  `uikit/comparison.h`; equal to the shown precision → Neutral; `Better::Neutral` → Neutral);
  `QVector<Stat> compute(Profile *, const Range &shown)`.
  `class OverviewStatsRow : public QWidget` with `void setRange(const QDate &start, const QDate
  &end)` (recomputes after a 150 ms single-shot timer so zoom drags stay smooth).

The five figures: average AHI or RDI (`calcCount` of the index's event channels ÷ `calcHours`),
average usage (`calcHours` ÷ days with CPAP data), nights at or above compliance hours
(`countCompliantDays` of `countDays`, with %), leak (`calcPercentile(CPAP_Leak, 0.5)`), pressure
(`calcPercentile` at `prefCalcPercentile` of the profile's pressure channel). Better: Lower,
Higher, Higher, Lower, Neutral. No data in the shown range → value "—", no change line; no data
in the previous range → no change line.

- [ ] Step 1: Failing tests: `testPreviousRange` (11 Oct–9 Nov 2026 → 11 Sep–10 Oct 2026; a single
  day → the day before), `testVerdict` (1.96 vs 2.10 Lower → Good; 5.8 vs 4.6 Lower → Bad;
  11.2 vs 11.4 Neutral → Neutral; equal → Neutral).
- [ ] Step 2: Run, see them fail. Step 3: implement the pure functions, then `compute` and the row
  (five cards: title, large value with unit, change chip in the verdict's colours).
- [ ] Step 4: `Overview` (Card look): the row sits between the toolbar and the graphs and is fed
  from `on_XBoundsChanged`.
- [ ] Step 5: Tests pass; harness: log the five values for 1 month and after zooming in, and
  compare the 1-month AHI and compliance figures with the Statistics page for the same dates.
- [ ] Step 6: Commit.

### Task 9: Whole-page verification and notes

**Files:**
- Modify: `Notes/UI Redesign/OVERVIEW_PAGE_DESIGN_V2.md` (move confirmed items out of "Proposed";
  record any ruling that changed a proposal)

- [ ] Step 1: Harness passes in light and dark for 2 weeks, 3 months and 1 year: grabs of the
  page; hover line, Details button and a popup; the menu; a popout; a print render.
- [ ] Step 2: Review Focus items 1–5, each exercised and its result recorded in the ledger.
- [ ] Step 3: Switch old → new → old in Preferences with a popup open; confirm Daily is unchanged
  in both (grab before and after are identical).
- [ ] Step 4: Remove the harness (`git grep TEMP-OVW-DIAG` is empty), full unit tests, build.
- [ ] Step 5: Commit; final whole-branch review.
