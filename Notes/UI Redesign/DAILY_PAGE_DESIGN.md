# Daily page redesign

Status: design settled 2026-11-14 after two rounds of review; built on branch `daily-modern`
(plan: `DAILY_PAGE_PLAN.md`). Two items wait for an on-screen judgement (see Open).
Mockup: https://claude.ai/artifact/BTbtLbvEQScYbr8pMkvaZn (three boards: the page on an ordinary
night, the left panel with the calendar open plus the session information, and the other kinds
of day; sample data).

## Intent

Give the Daily page the Modern look already used by Welcome and Overview, starting from a team
mockup: the left panel becomes a set of cards in theme colours, and the graphs become the same
outlined cards as on Modern Overview. Everything today's page shows or does keeps a place; the
mockup drew a simple night only, so this note says where the rest goes.

Goals:
- One look across Welcome, Overview and Daily, in light and dark.
- The left panel readable in dark mode with no contrast failures (today's coloured rows and
  banners have several).
- No loss of information or function against the Classic page.
- A screenshot of the page explains itself to someone helping the user: the device, its mode
  and its pressures are in view, because the rest of the page cannot be read without them.

Anti-goals:
- Marks or text that need focused attention to read. The calendar is the test case: small
  markers under each date were considered and rejected for this reason.
- Scroll areas inside scroll areas.
- Reference material in the space above the graphs. That space is the most valuable on the
  page; a strip of session chips there was considered and dropped for this reason.

## Decided

**Classic or Modern.** The "Use Modern design" preference and the Classic/Modern button cover
Daily as well. Classic Daily is the page as it is on `ui-next` now, unchanged.

**Left panel, top to bottom.**
- Day bar: previous day, the date (click to show or hide the calendar), next day, Today. As now.
- Calendar: closed by default and its open or closed state is remembered, both as now. When open
  it sits in a card. Days keep today's text code exactly (blue, red, green, underline, italic,
  shaded background, line above), and the code is explained in the day's tooltip, as now. There
  is no legend on the page and there are no markers. The selected day is shown by an outline,
  not a filled cell, so its colour and shading stay readable. The week starts on the day the
  locale says. Today's date gets a thinner outline.
- Tabs: Details, Events, Notes, Bookmarks, Search, as now.
- Session bar: the coloured bar of CPAP sessions stays below the tab area, as now. Clicking a
  segment switches that session off or on, as now. A switched-off segment gets a red tint in
  place of today's empty box; to be tried on screen and dropped if it adds nothing.
- The panel stays resizable by the splitter.

**Details tab.** Cards replace the HTML page. Each card is as tall as its contents; the tab
scrolls as a whole, and no card scrolls by itself. In order:
- **AHI card.** The index (AHI, or RDI when that preference is on) as a large number. Where the
  device reports them, a small second line gives the obstructive and central shares. Then the
  comparison with the user's recent average, following the Welcome page's rule: one line for 7
  nights and one for 30 ("7 nights 1.80", "30 nights 2.10") with Welcome's arrows, the same "no
  change" band, a green or red tint only when both agree, and no comparison when there is no
  history. The nights counted are those before the day shown, not the latest in the profile.
  Beneath: hours of use, number of sessions, start and end times of the night.
- **Device card.** Manufacturer, model, PAP mode and pressure settings, as today's section
  headed by the brand. It can be collapsed, as today's section can; it starts open and its
  state is remembered. The model's tooltip keeps the model number and serial. When the day's
  settings are missing and yesterday's are shown, the warning sits in this card.
- **Events card.** One row per index today's panel lists: a swatch in the channel's colour, the
  name, the count and the rate per hour. Text is always the theme's text colour. Clicking a row
  opens that event in the Events tab, as clicking the name does today.
- **Statistics card.** Today's table: one row per channel with minimum, middle value, upper
  percentile and maximum, the columns following the same preferences as now, then the extra
  lines (total time in apnea, time over leak redline, ramp times).
- **Collapsed cards**, opened by clicking the header, their open or closed state remembered:
  Event Breakdown (the pie chart, when the preference shows it), Oximeter Information (when
  the day has it), Device Settings, Session Information.
- **Session Information card.** Contents as now: the day's sessions grouped by kind (CPAP,
  oximetry, sleep stage, position sensor), each with its on/off switch, date, start, end and
  duration. A switched-off session has a red tint.
- Today's General section (date, start, end, hours) is covered by the AHI card.

**Other kinds of day.** Each has a drawn state, using two callout cards (information and warning)
with dark text on a pale fill:
- No data for the day.
- A device that records only compliance data.
- A day that holds only summary data on a device that normally records detail.
- Oximetry (or other non-CPAP data) with no CPAP data.
- Sessions exist but all are switched off. The Session Information card and the session bar
  stay available so the sessions can be switched back on.
- Settings missing, so yesterday's are shown.

The callouts keep today's messages where they inform ("No CPAP data is available for this
day", "Sessions exist for this day but are switched off.", "This CPAP device does NOT record
detailed data", "This day just contains summary data, only limited information is available.").
The jokes and the logo picture leave the panel. The graph area keeps its own empty message and
picture.

**Graph area.**
- Top bar: at the left, one line naming the device, its mode and its pressures, so that a
  screenshot shows them whichever tab is open in the left panel and however far the Details
  tab is scrolled. The line is cut short with an ellipsis when the window is narrow. At the
  right, Flags (today's events drop-down), Graphs (today's graphs drop-down, with its count as
  on Overview), Layout and Help (today's Layout and Hint buttons). The bar under the graphs
  goes away.
- Graphs look like the graphs on Modern Overview: the same cards (`GraphLook::Card`) with
  outline, title and unit at the top left, information along the header, menu button at the
  right. Dragging the header re-orders, double-click pins, graphs resize vertically, all as on
  Overview.
- Header contents: title, unit, then the text each graph draws along its top today (the values
  at the cursor and the names of the dotted lines), then the menu button.
- The menu button opens the graph's menu, grouped as on Overview: Display (Reset graph
  heights, Pin or Unpin, Pop out, Clone, 100% zoom); Y-axis (Auto scale, Default scale,
  Override scale, and Dynamic scale where the graph offers it today); Overlays (plots, event
  overlays and dotted lines, as offered today). Right-click on the header opens the same menu;
  right-click in the plot keeps its present meaning.
- Event Flags is a card like the others. Pinned graphs stay fixed above the scrolling ones.
- One time axis, fixed below the graphs. A graph whose horizontal scale is not the night's
  time draws its own axis (Time at Pressure is the case today). A popped-out graph draws its
  own.
- Time readout: a slim status line under the fixed time axis, centred, with the same text as
  now. Tentative: other kinds of information may come to share this line, and Overview may
  need the same line. To be revisited; not part of this work.
- Time between sessions is marked across all graphs with diagonal hatching in the theme's
  line colour. Not a flat fill: a Large Leak span is a flat pale grey fill on the Leak Rate
  graph, and the two must not be confused. To be judged on screen when built, and dropped if
  it still reads as a leak span.
- Otherwise, what is drawn inside each graph (lines, colours, event marks, dotted lines) does
  not change.

**Other tabs.** Events, Notes, Bookmarks and Search get theme colours and the card background
only; their layout and controls stay.

**Printing.** Printed daily reports are unchanged by this work. Printed reports for all pages
may be revisited later.

## Open

Two items are to be judged on screen; both are built and wait for that judgement:
- The hatching between sessions. Built in the theme's line colour in light mode; in dark mode
  that colour did not show on the graph background, so the muted text colour, softened, is used
  there. To the builder it reads clearly differently from a Large Leak span in both modes.
- The red tint on a switched-off session, on the session bar and in the Session Information
  card. In dark mode the tint is dark, and the bar's switched-off segment reads almost black.

## Decided while building

- A day with oximetry but no CPAP data (or CPAP data without oximetry) is normal: it gets no
  callout, only the cards that have something to show.
- Event Flags keeps its own time scale: it always shows the whole night while the other graphs
  zoom, so the shared axis would not match it.
- A day whose sessions are all off, or add up to no time, shows no Device card: with no session
  on there are no settings to show (the Classic page shows no device section either).
- The Search tab keeps the white and black that its own buttons, labels and result table set
  for themselves; only the tab around them takes the theme.
- The graph header shows a graph's unit only (Daily's captions also hold a description).

## Not in this work

- Calendar markers or a legend on the page (rejected).
- A session strip above the graphs (dropped in review).
- New channel colours, line styles or event marks.
- A redesign of the Events, Notes, Bookmarks or Search tabs.
- New statistics. The AHI comparison is the only new figure.
- Changes to printed reports.
- An exported image of the page for analysis, taller than the screen and independent of which
  cards are open. A longer-term idea; it would also settle what a screenshot can show.

## Notes for the builder

- The Details tab is one `MyTextBrowser` (`webView`) filled by `Daily::getLeftSidebar()`, which
  joins HTML pieces built by `getAHI`, `getCPAPInformation`, `getSleepTime`, `getIndices`,
  `getPieChart`, `getStatisticsInfo`, `getOximeterInformation`, `getMachineSettings` and
  `getSessionInformation`. The day states are decided in `Daily::Load` (the `isBrick`, no-hours
  and no-CPAP branches). The Modern page needs the same data as values, not HTML; the Classic
  page keeps the HTML path.
- The Device card's contents are those of `getCPAPInformation`: `MachineInfo::brand` and
  `model`, a tooltip with `modelnumber` and `serial`, `Day::getCPAPModeStr()`,
  `Day::getPressureSettings()`, and the missing-settings line when `Day::noSettings()`.
- Section open/closed state is `leftSideBarEnable[]`, stored as one value
  (`AppSetting->leftSideBarEnable`) and toggled through `leftsidebarenable=` links in
  `Daily::Link_clicked`. Today every section starts open. In Modern the Device card starts
  open and the cards listed as collapsed start closed, so Modern needs its own stored value;
  sharing the Classic one would change the Classic page.
- Session switches arrive in `Daily::Link_clicked` (`toggle...session=`) and through
  `SessionBar::sessionClicked` into `doToggleSession`. Event names link with `event=`.
- The session bar (`sessionbar.cpp`) sits under the Details browser and takes CPAP sessions
  only (`Daily::Load`), in gold and sky blue alternately. It paints its text and outlines in
  black and leaves a switched-off segment unfilled, so check it on the dark window colour.
- `gSessionBarLayer` already draws a thin grey session bar along the top of some graphs when
  the `EventFlagSessionBar` preference is on, mirroring the one on Event Flags. Large Leak
  spans are drawn by `gLineOverlayBar` with `FT_Span`, filled with `GraphTheme::fill()` of the
  channel's colour (`COLOR_LargeLeak` is light grey). Both bear on the hatching between sessions.
- The time readout is `dateDisplay`, set by `on_LineCursorUpdate` and `on_RangeUpdate`; it
  lives in `sessionBarLayout` in the bottom bar with `eventsCombo`, `graphHelp`, `layout` and
  `graphCombo`.
- Calendar styling is `Daily::UpdateCalendarDay` (text formats and the tooltip). Drawing the
  selected day as an outline needs a `QCalendarWidget` subclass overriding `paintCell`.
  `CalendarVisible` already defaults to off.
- The card look is `GraphLook::Card` with geometry in `Graphs/graphcard.h`; `gGraphView` and
  `gGraph` already draw it for Overview, and `gGraphView` already builds the grouped menu
  (Display, Y-axis, Overlays, including the Plots, CPAP Overlays, Oximeter Overlays and Dotted
  Lines sub-menus) for that look. `OverviewDateAxis` is the fixed axis there and works
  in days; Daily needs the same idea in time of day. Each Daily graph adds its own `gXAxis`
  layer today (`gXAxisPressure` for Time at Pressure).
- The comparison is `uikit/comparison` (`compare`, `statusTint`, `NoChangeBand`); the Welcome
  page collects its 7- and 30-night series in `welcomemetrics.cpp` (`collect`, which takes the
  night to count back from).
- `MainWindow::rebuildOverviewPage` shows how Overview switches between Classic and Modern.
