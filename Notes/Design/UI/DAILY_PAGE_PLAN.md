# Daily Page Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the Daily page the Modern look: graph cards with one fixed time axis and a top
bar, and a Details tab made of cards, selectable against the present page by the existing
Classic/Modern switch.

**Architecture:** One `Daily` class serves both looks, as `Overview` does: it reads
`AppSetting->useNewPages()` once when constructed, and `MainWindow` rebuilds the page when the
switch changes. The graph side reuses `GraphLook::Card` from Overview. The Details tab's figures
are moved out of the HTML builders into value collectors (`DailyDetails`), which both the Classic
HTML and the Modern cards read, so the two looks cannot disagree; the Classic HTML is proved
unchanged by comparing it for every day of the test profiles before and after.

**Tech Stack:** C++17, Qt 6.12 widgets + QPainter, MSVC 2022, qmake/jom. Unit tests: QtTest suites
under `oscar/tests` (`DECLARE_TEST`), listed in `oscar/oscar.pro`.

**Spec:** `Notes/UI Redesign/DAILY_PAGE_DESIGN.md` (mockup linked there).

## Global Constraints

- Work in `C:\OSCAR\OSCAR-next` on branch `daily-modern` (off `ui-next`). Never master; nothing in
  `BUG_FIXES.md` or `release_notes.html`.
- With the switch on Classic, Daily looks and behaves exactly as it does on `ui-next` now, and
  Overview is untouched in both looks. Every change to `gGraph`, `gGraphView`, `gLineChart`,
  `gFlagsLine`, `gXAxis` is behind `cardLook()` / `sharedXAxis()`.
- Printed daily reports do not change. `Daily::GetDetailsText()` reads `webView`'s document, so
  `webView` is still filled (hidden) in Modern.
- What is drawn inside each graph does not change, apart from the hatching between sessions.
- Calendar: today's text code and tooltip exactly; no markers, no legend on the page.
- Qt 6 APIs only, no `#if QT_VERSION` guards, cross-platform, no files under
  `SleepLib/thirdparty`. New files get the file header, "Copyright (c) 2026 The OSCAR Team",
  Doxygen comments, and an entry in `oscar/oscar.pro`.
- Colours come from `Theme` / `GraphTheme` so light and dark both work. Text on a card is always
  a theme text colour; channel colours appear as swatches only.
- Commits: ask first; message via `git commit -F <file>`; no Co-Authored-By trailer.
- Build: `build_next_msvc.bat`; tests: `build_next_tests.bat` (both in the session scratchpad;
  exit code = number of failures).
- Visual and data checks use a temporary, env-gated harness in `mainwindow.cpp` marked
  `TEMP-DAILY-DIAG` (in-app `grab()` images and text dumps), removed before every commit.
  Never `Stop-Process` a test OSCAR; close its main window.

## Review Focus

1. **Days that are not an ordinary CPAP night:** no data, compliance-only device, summary-only
   day, oximetry with no CPAP, all sessions off, a zero-length session, settings missing. Each
   shows its callout and only the cards that have something to show; nothing divides by zero.
2. **Switching Classic/Modern while Daily is live:** a graph popped out, a zoomed range, the
   Events tab open, a note being edited. The page is rebuilt on the same date; the note is saved
   first; Overview (which shares Daily's graph view) is rebuilt after it; nothing dangles.
3. **Narrow panel and narrow window:** cards never force a horizontal scroll bar; the device line
   in the top bar and a graph header's text are cut short with an ellipsis, never drawn over the
   buttons or the menu button.
4. **Session switches:** switching a session off or on from the Session Information card or the
   session bar reloads the day, keeps the card open and the scroll position, updates Overview and
   Statistics, and is refused in Clinical Mode with today's message.
5. **Zoom, popouts and graphs with their own scale:** the fixed axis follows zoom and pan; a
   popped-out graph and Time at Pressure draw their own axis; hatching follows the zoom.

---

### Task 1: Baseline, the switch, and graph cards on Daily

**Files:**
- Modify: `oscar/mainwindow.h/.cpp`, `oscar/daily.h/.cpp`, `oscar/preferencesdialog.ui`

**Interfaces:**
- Produces: `void MainWindow::rebuildDailyPage()`; `bool Daily::modern() const` (set once in the
  constructor from `AppSetting->useNewPages()`).

- [ ] Step 1: Add the `TEMP-DAILY-DIAG` harness. Mode `html`: for every day of the open profile,
  `LoadDate` and write `date <tab> SHA-1 of getLeftSidebar(true)` to a file. Mode `grab`: load a
  given date and save `grab()` of the Daily page. Run `html` on the "Blue Dragon 2025 test"
  profile and on one profile with oximetry, on unmodified `ui-next`; keep the files as the
  Classic baseline. Save Classic `grab` images of an ordinary night in light and dark.
- [ ] Step 2: `rebuildDailyPage()`: remember the date and the current tab; `daily->Unload()`,
  `clearLastDay()`; remove and delete `overview`, then `daily`; recreate both as
  `doReprocessEvents()` does (including the `dateLoaded` connection), `ReloadGraphs()` each,
  `LoadDate` the remembered date, restore the current tab. Call it from the Classic/Modern
  button and from Preferences in place of `rebuildOverviewPage()` (it rebuilds both).
- [ ] Step 3: `Daily` (Modern): `GraphView->setLook(GraphLook::Card)` before the graphs are
  created; `snapGV` takes the same look. Preference tooltip names Daily with Welcome and
  Overview.
- [ ] Step 4: Build; unit tests. Expected: 0 failures. Harness: Classic `html` output and grabs
  are identical to the baseline; a Modern grab shows every graph as a card.
- [ ] Step 5: Commit.

### Task 2: Graph header text and menu for Daily's graphs

**Files:**
- Modify: `oscar/Graphs/gLineChart.cpp`, `oscar/Graphs/gFlagsLine.cpp`,
  `oscar/Graphs/graphcard.h/.cpp`, `oscar/Graphs/gGraphView.cpp`
- Test: `oscar/tests/graphcardtests.h/.cpp`

**Interfaces:**
- Consumes: `gGraph::headerTextRect()`, `gGraph::setHeaderLegendWidth(int)`,
  `GraphCard::elideSummary()`.
- Produces: `struct GraphCard::HeaderSplit { QRect text; QRect legend; };`
  `HeaderSplit GraphCard::splitHeader(const QRect &headerText, int legendWidth, int minText)`:
  the legend takes its width from the right; when less than `minText` would be left for the
  text, the legend is narrowed to what remains (never negative).

- [ ] Step 1: Failing tests: `testSplitHeaderFits` (rect 100,6 400x26, legend 150, minText 80 →
  text 100..349, legend 350..499), `testSplitHeaderNarrow` (legend 380 → text width 80, legend
  width 320), `testSplitHeaderEmpty` (empty rect → both empty).
- [ ] Step 2: Run, see them fail. Step 3: implement `splitHeader`.
- [ ] Step 4: Card look only: `gLineChart::paint` draws its cursor text (`lasttext`, and the Flow
  Rate "Duration ... AHI ..." text) in the split's `text` rect, elided, instead of at `top-6`;
  the channel legend and the dotted-line legend markers are drawn right-aligned in the `legend`
  rect, vertically centred in the header, dropped from the left when they do not fit.
  `gFlagsLine` hover text (`renderText(text, left, top-5)`) goes to the header text rect the
  same way. Classic drawing is not touched.
- [ ] Step 5: Card menu: the Y-axis section adds "Dynamic scale" for graphs that offer it today;
  check that Display and Overlays hold everything today's Daily right-click menu holds
  (compare the two menus item by item in the harness and record the result).
- [ ] Step 6: Tests pass. Harness grabs, light and dark, at two widths: no text over the title
  or the menu button; Event Flags is a card; a pinned graph shows its pin; a snapshot graph
  still shows its tint; header drag re-orders; double-click pins.
- [ ] Step 7: Commit.

### Task 3: One fixed time axis and the status line

**Files:**
- Create: `oscar/dailytimeaxis.h/.cpp`
- Modify: `oscar/Graphs/gXAxis.h/.cpp`, `oscar/Graphs/gGraph.cpp`, `oscar/Graphs/gGraphView.cpp`,
  `oscar/daily.h/.cpp`, `oscar/oscar.pro`

**Interfaces:**
- Consumes: `gGraphView::setSharedXAxis(bool)`, `gGraph::plotRect()`, `gGraphView::painted()`
  (the signal `OverviewDateAxis` follows).
- Produces: `void gXAxis::setCacheEnabled(bool)` (off: paints directly, ignoring
  `invalidate_xAxisImage`). `class DailyTimeAxis : public QWidget` with
  `DailyTimeAxis(gGraphView *view, QWidget *parent)`, `sizeHint()` of `gXAxis::minimumHeight()`,
  slot `refresh()`. It owns one `gXAxis` (cache off) and paints it with the first visible
  time-based graph and a region whose left and width are that graph's `plotRect()`; it shows
  nothing when the view is empty.

- [ ] Step 1: `gGraph::paint` and the popout height code leave out `gXAxis` layers as well as
  `gXAxisDay` when `sharedXAxis()` (not when printing, not in a popout). `gXAxisPressure` is
  never left out.
- [ ] Step 2: `DailyTimeAxis`, modelled on `OverviewDateAxis` (state read on `painted()`,
  repaint only on change).
- [ ] Step 3: `Daily` (Modern): `GraphView->setSharedXAxis(true)`; the axis goes under
  `graphFrame` in `verticalLayout_3`, outside the scrolling list; `dateDisplay` moves from
  `sessionBarLayout` to a slim centred line under the axis, in the default font size and
  `Theme::Role::Ink2`. `ui->frame` (the bottom bar) stays for now; Task 5 hides it.
- [ ] Step 4: Harness: grabs at full range and zoomed show one axis whose ticks line up with the
  plots (log the axis left/width against two graphs' `plotRect()`); Time at Pressure and a
  popped-out graph show their own axis; the status line shows the cursor time and, after a
  drag-zoom, the range text.
- [ ] Step 5: Commit.

### Task 4: Hatching between sessions

**Files:**
- Create: `oscar/Graphs/gSessionGapLayer.h/.cpp`
- Modify: `oscar/daily.cpp`, `oscar/oscar.pro`
- Test: `oscar/tests/sessiongaptests.h/.cpp`

**Interfaces:**
- Produces: `struct TimeSpan { qint64 start, end; };`
  `QVector<TimeSpan> SessionGaps::gaps(QVector<TimeSpan> sessions)`: sorts, merges spans that
  touch or overlap, returns the spaces between the merged spans (none before the first or after
  the last). `class gSessionGapLayer : public Layer` (`SetDay` collects the enabled CPAP
  sessions' spans in display time as `gSessionBarLayer` does; `paint` fills each gap inside the
  visible X range with `Qt::BDiagPattern` in `Theme::Role::Line`, under the data).

- [ ] Step 1: Failing tests: `testNoGapForOneSession`, `testGapBetweenTwo` ({0-100},{130-200} →
  {100-130}), `testOverlapMerged` ({0-100},{90-200} → none), `testUnsorted` (same result when
  given in reverse), `testEmpty`.
- [ ] Step 2: Run, see them fail. Step 3: implement `gaps` and the layer.
- [ ] Step 4: `Daily` (Modern) adds the layer first (so it draws under the data) to every graph
  that has a `gXAxis`; not to Time at Pressure.
- [ ] Step 5: Tests pass. Harness grabs of a two-session night, light and dark, with a Large
  Leak span on screen in the same grab: record in the ledger whether the hatching can be
  mistaken for the leak span (spec: drop the layer if it can; the user judges the grab).
- [ ] Step 6: Commit.

### Task 5: Top bar

**Files:**
- Create: `oscar/dailytoolbar.h/.cpp`
- Modify: `oscar/daily.h/.cpp`, `oscar/oscar.pro`

**Interfaces:**
- Consumes: `DailyDetails::Device` is not available yet, so the bar takes plain text.
- Produces: `class DailyToolbar : public QWidget` with `void setDeviceLine(const QString &name,
  const QString &settings)` (name bold, settings after a middle dot; elided at the right; empty
  name hides the line), `void adoptCombos(QComboBox *events, QComboBox *graphs)` (reparents
  Daily's `eventsCombo` and `graphCombo`), signals `layoutClicked()`, `helpClicked()`.

- [ ] Step 1: Build the bar: device line at the left taking the spare width; at the right the
  events combo (its text is set by `setFlagText()`), the graphs combo (text from
  `setGraphText()`), a layout `IconButton` and a help `IconButton`, as on the Overview toolbar.
  Styled from `Theme`; restyles on palette change.
- [ ] Step 2: `Daily` (Modern): the bar goes at the top of `verticalLayout_3`; `ui->frame` is
  hidden; signals connect to `on_layout_clicked` and `on_graphHelp_clicked`. In `Load`, the
  device line is `MachineInfo::brand + " " + model` and `Day::getCPAPModeStr()` with
  `Day::getPressureSettings()` (HTML stripped), or empty when the day has no CPAP device.
- [ ] Step 3: Harness: grabs at 1280 and 800 px wide (line elided, buttons whole); toggle an
  event and a graph through the adopted combos and log that the graph view changed; a day with
  no CPAP shows no device line.
- [ ] Step 4: Commit.

### Task 6: Details figures as values

**Files:**
- Create: `oscar/dailydetails.h/.cpp`
- Modify: `oscar/daily.h/.cpp`, `oscar/oscar.pro`
- Test: `oscar/tests/dailydetailstests.h/.cpp`

**Interfaces:**
- Produces (namespace `DailyDetails`; every text already translated and formatted as the Classic
  page shows it, so the two looks print the same digits):
  `enum class DayKind { NoData, Cpap, ComplianceOnly, SessionsOff, ZeroHours, NonCpapOnly };`
  `DayKind classify(bool hasCpap, double cpapHours, int sessionCount, bool brick, bool
  hasOximeterOrPosition)` (pure; the branches of `Daily::Load`);
  `struct Headline { bool present; QString name, fullName; double value; bool hasShares; double
  obstructive, central; };`
  `struct Usage { bool present; QDateTime start, end; qint64 seconds; int cpapSessions; };`
  `struct Device { bool present; QString brand, model, tooltip, mode, pressure; bool
  settingsMissing; };`
  `struct EventRow { ChannelID code; QString name, tooltip, count, rate; QColor colour; bool
  link; double value; };` (`count` empty for span channels, whose `rate` is a percentage)
  `struct StatRow { QString label, tooltip, min, mid, perc, max; };`
  `struct Statistics { bool present; QString midName, percName, maxName; QVector<StatRow> rows;
  QVector<QPair<QString, QString>> extras; bool summaryOnlyNote; };`
  `struct TextRow { QString label, tooltip, value; };`
  `struct Oximeter { bool present; QString heading; QVector<TextRow> rows; QString note; };`
  `struct Settings { bool present, loaderMissing, assumed; QVector<TextRow> rows; };`
  `struct SessionRow { Session *session; QString name, date, start, end, duration; bool on; };`
  `struct SessionGroup { MachineType type; QString heading; QVector<SessionRow> rows; };`
  and one collector per struct taking `Day *` (`headline`, `usage`, `device`, `events`,
  `statistics(Day *, bool graphsEmpty)`, `oximeter`, `settings`, `sessions`), plus
  `double pieTotal(const QVector<EventRow> &)`.

- [ ] Step 1: Failing tests for `classify`: no CPAP and nothing else → NoData; no CPAP with
  oximetry → NonCpapOnly; CPAP, 6.5 h, not brick → Cpap; CPAP, 6.5 h, brick → ComplianceOnly;
  CPAP, 0 h, 2 sessions → SessionsOff; CPAP, 0 h, 0 sessions → ZeroHours.
- [ ] Step 2: Run, see them fail. Step 3: implement `classify`; `Daily::Load` uses it to choose
  its branches.
- [ ] Step 4: Move the figures out of `getAHI`, `getSleepTime`, `getCPAPInformation`,
  `getIndices`, `getStatisticsInfo`, `getOximeterInformation` (with `getViatomSummary`),
  `getMachineSettings` and `getSessionInformation` into the collectors, one builder at a time,
  moving the code rather than rewriting it; each builder then formats its HTML from the struct.
  After each builder: build, run the `html` harness on both profiles, and compare with the
  Task 1 baseline. Expected: identical for every day. A difference is a defect in the move.
- [ ] Step 5: List in the ledger the branches no test day reached (for example Viatom summary,
  Apple Health rows) and re-read those moves line by line against `git diff`.
- [ ] Step 6: Unit tests pass; Classic grabs equal the Task 1 grabs.
- [ ] Step 7: Commit.

### Task 7: Card pieces for the UI kit

**Files:**
- Create: `oscar/uikit/collapsiblecard.h/.cpp`, `oscar/uikit/callout.h/.cpp`,
  `oscar/uikit/toggleswitch.h/.cpp`
- Modify: `oscar/SleepLib/appsettings.h/.cpp`, `oscar/uikit/lineicons.h/.cpp`, `oscar/oscar.pro`
- Test: `oscar/tests/dailydetailstests.h/.cpp`

**Interfaces:**
- Produces (namespace `UiKit`): `class CollapsibleCard : public Panel` with
  `CollapsibleCard(const QString &title, QWidget *parent)`, `void setContent(QWidget *)`,
  `void setOpen(bool)`, `bool isOpen() const`, signal `toggled(bool open)`; the whole header is
  the click target, shows a chevron, and takes keyboard focus (Space/Enter toggle).
  `class Callout : public Panel` with `enum class Kind { Info, Warning }`,
  `Callout(Kind, const QString &title, const QString &text, QWidget *parent)` (icon, bold title,
  word-wrapped text; Warning uses `WarnSoft`/`WarnLine`, Info uses `AccentSoft`/`Line`).
  `class ToggleSwitch : public QAbstractButton` (checkable, 28x16, accent when on, `BadLine`
  outline when off; `sizeHint` fixed).
  `enum DailyCard { DC_Device, DC_EventBreakdown, DC_Oximeter, DC_DeviceSettings,
  DC_SessionInfo };` in `dailydetails.h`, with `bool DailyDetails::cardOpen(int bits,
  DailyCard)` and `int DailyDetails::setCardOpen(int bits, DailyCard, bool)`.
  `int AppSettings::dailyCardsOpen() const` / `setDailyCardsOpen(int)`, key `"DailyCardsOpen"`,
  default `1 << DC_Device` (only the Device card open). `leftSideBarEnable` is not used by
  Modern, so the Classic page's sections are unaffected.

- [ ] Step 1: Failing tests: `testCardBitsDefault` (default bits: Device open, the other four
  closed), `testCardBitsRoundTrip` (set SessionInfo open, Device closed; read both back; the
  other bits unchanged).
- [ ] Step 2: Run, see them fail. Step 3: implement the bit helpers, the setting and the three
  widgets.
- [ ] Step 4: Harness: grab a scratch page holding an open and a closed card, both callouts and
  both switch states, in light and dark; check text contrast on each fill.
- [ ] Step 5: Commit.

### Task 8: Details tab cards

**Files:**
- Create: `oscar/dailydetailspanel.h/.cpp`
- Modify: `oscar/daily.h/.cpp`, `oscar/oscar.pro`

**Interfaces:**
- Consumes: Task 6 structs and collectors; Task 7 widgets; `UiKit::MetricCard`;
  `WelcomeMetrics::cards(QDate night)` (take `MetricId::Ahi` for `avg7`, `avg30`, `nights7`,
  `nights30`); `UiKit::compare`, `UiKit::statusTint`.
- Produces: `struct DailyDetails::Page { DayKind kind; Headline headline; Usage usage; Device
  device; QVector<EventRow> events; Statistics statistics; Oximeter oximeter; Settings settings;
  QVector<SessionGroup> sessions; bool showPie; QPixmap pie; bool pieZero; };`
  `class DailyDetailsPanel : public QScrollArea` with `void setPage(const DailyDetails::Page &)`
  (rebuilds the cards, keeps the scroll position when the date is unchanged), signals
  `eventClicked(ChannelID)`, `sessionToggled(Session *)`, `cardToggled(int card, bool open)`.

- [ ] Step 1: `Daily` (Modern): the Details tab holds the panel above the session bar; `webView`
  is created, filled as now and kept hidden. `Load` builds the `Page` from the collectors and
  calls `setPage`.
- [ ] Step 2: Cards in order. AHI card: a `MetricCard` (index name, value, unit "events per
  hour"; subtitle with the obstructive and central shares when `hasShares`; the two comparison
  boxes labelled "7 nights" and "30 nights"; tint from `statusTint`) with an extra row of Usage,
  Sessions, Start - End. Device card: collapsible, brand and model as the header (tooltip from
  `Device::tooltip`), mode and pressures beneath, a Warning callout "Mode and Pressure settings
  missing; yesterday's shown." when `settingsMissing`. Events card: one row per `EventRow`
  (swatch, name, count, rate); rows with `link` are buttons emitting `eventClicked`. Statistics
  card: header row from `midName`, `percName`, `maxName`; rows; then `extras`.
- [ ] Step 3: Day kinds: NoData → Info callout "No data is available for this day." only;
  ComplianceOnly → Warning callout "This CPAP device does NOT record detailed data", a card
  with the Usage row, the Device card; SessionsOff → Warning callout "Sessions exist for this
  day but are switched off."; ZeroHours → Info callout with today's "Impossibly short session"
  wording; NonCpapOnly → Info callout "No CPAP data is available for this day", then
  Statistics; `summaryOnlyNote` → Info callout with today's summary-only sentence under the
  Statistics card.
- [ ] Step 4: `eventClicked` connects to the code `Link_clicked` runs for `event=`.
- [ ] Step 5: Harness: grabs of the ordinary night at 300 and 400 px panel width, light and
  dark; dump the panel's texts for five days and compare each figure with the Classic HTML of
  the same day; grabs of each day kind the test profiles contain.
- [ ] Step 6: Commit.

### Task 9: Collapsed cards, session switches and the session bar

**Files:**
- Modify: `oscar/dailydetailspanel.h/.cpp`, `oscar/daily.cpp`, `oscar/sessionbar.h/.cpp`

**Interfaces:**
- Consumes: `CollapsibleCard`, `ToggleSwitch`, `AppSettings::dailyCardsOpen()`,
  `Daily::doToggleSession(Session *)`.
- Produces: `void SessionBar::setThemed(bool)` (off by default; on: outline and "No Sessions
  Present" text in `Theme::Role::Ink2`, a switched-off segment filled with `BadTint` and
  outlined with `BadLine`, its text in the theme's text colour).

- [ ] Step 1: Event Breakdown (only when `AppSetting->showPieChart()`): the pixmap
  `eventBreakdownPie()->renderPixmap(155, 155, false)`, or the "0.0" image when `pieZero`.
  Oximeter Information: heading, rows, note. Device Settings: rows with tooltips; a Warning
  callout with today's "All settings shown below are based on assumptions..." text when
  `assumed`. Session Information: per group a heading and a table (On, Date, Start, End,
  Duration) with the session name above each row; a `ToggleSwitch` per row with today's
  tooltip; an off row tinted `BadTint`.
- [ ] Step 2: Open state: each card starts from `dailyCardsOpen()`; `cardToggled` stores the new
  bits. The state survives a day change and a restart.
- [ ] Step 3: `sessionToggled` → `doToggleSession` (refused in Clinical Mode with today's
  message; the switch snaps back). `Daily` (Modern) calls `sessionbar->setThemed(true)`.
- [ ] Step 4: Harness: toggle the second session off through the card and through the bar; log
  hours and AHI before and after against Classic for the same toggle; grab the card and the bar
  with a session off, light and dark; restart and log the stored card bits.
- [ ] Step 5: Commit. The red tint on the bar is "worth trying": show the grab to the user.

### Task 10: Calendar card and left panel styling

**Files:**
- Create: `oscar/dailycalendar.h/.cpp`
- Modify: `oscar/daily.ui`, `oscar/daily.cpp`, `oscar/oscar.pro`

**Interfaces:**
- Produces: `class DailyCalendar : public QCalendarWidget` with `void setOutlineSelection(bool)`
  (off by default: paints exactly as `QCalendarWidget`). On: `paintCell` draws the selected day
  and today's date itself from `dateTextFormat(date)` (background, font, foreground, as the
  other cells show them) and adds a 2 px `Accent` outline for the selected day, 1 px for
  today's date; all other cells go to the base class.

- [ ] Step 1: Promote `calendar` in `daily.ui` to `DailyCalendar`. Harness: Classic calendar
  grab equals the Task 1 grab.
- [ ] Step 2: `Daily` (Modern): `setOutlineSelection(true)`; `calendarFrame` drawn as a card
  (surface colour, line outline, radius 10); day bar buttons and the tab bar in theme colours.
  `CalendarVisible`, the week start and `UpdateCalendarDay` are not touched.
- [ ] Step 3: Events, Notes, Bookmarks and Search tabs: surface background and theme text
  colours only; no layout or control changes.
- [ ] Step 4: Harness: calendar grabs, light and dark, with the selected day on a day that has
  each text attribute (colour, underline, italic, shading, line above): the attribute is still
  visible inside the outline; grabs of the four other tabs.
- [ ] Step 5: Commit.

### Task 11: Whole-page verification and notes

**Files:**
- Modify: `Notes/UI Redesign/DAILY_PAGE_DESIGN.md` (record the two on-screen judgements and any
  ruling that changed the design)

- [ ] Step 1: Harness passes in light and dark: ordinary night, two-session night, each other
  day kind available; zoomed; a popout; the card menu; a narrow window.
- [ ] Step 2: Review Focus items 1-5, each exercised and its result recorded in the ledger.
- [ ] Step 3: Classic check: `html` harness and Classic grabs equal the Task 1 baseline; a
  printed daily report (to PDF) from Modern equals one from Classic for the same day.
- [ ] Step 4: Switch Classic → Modern → Classic with a popout open and a note half written;
  Overview still works in both looks.
- [ ] Step 5: Remove the harness (`git grep TEMP-DAILY-DIAG` is empty), full unit tests, build.
- [ ] Step 6: Commit; final whole-branch review.
