# Daily Full-Height Screenshot Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** F12 on the Daily page (Modern and Classic, Details tab in front) saves one tall image:
the whole left sidebar, and the graphs down to the one that crosses the sidebar's bottom.

**Architecture:** Everything happens in one synchronous pass, never returning to the event loop,
so nothing reaches the screen. The Daily page widget is temporarily resized to the tall height
and drawn with `QWidget::grab()`; its graph view is hidden meanwhile (so its OpenGL framebuffer
never grows) and the graphs are drawn by the existing raster path at a given size. The window's
menu bar and page tabs come from an ordinary window grab, which is lengthened by repeating one
pixel row, and the tall page is painted over it.

**Tech Stack:** C++17, Qt 6 Widgets (Qt 6.4 through 6.12), QtTest.

**Spec:** `Notes/specs/2026-11-16-daily-tall-screenshot-design.md`

## Global Constraints

- C++17; compiles clean with Qt 6.12 and stays compatible back to Qt 6.4 (some Linux systems
  ship 6.4.x): no API introduced after Qt 6.4. Qt6-native APIs only, no `QT_VERSION` guards in
  new code.
- No platform-specific APIs.
- Every new or changed function documented Doxygen-style; new files carry a module description
  and "Copyright (c) 2026 The OSCAR Team" (copy the header form of `Graphs/graphcard.h`).
- New files are added to `oscar/oscar.pro`. Nothing under `oscar/SleepLib/thirdparty` is touched.
- The graph view (QOpenGLWidget) is never enlarged and never grabbed through OpenGL (#237).
- The top-level window is never resized.
- Captures other than Daily-with-Details-in-front behave exactly as today.
- The cards' stored open/closed preference (`AppSetting->dailyCardsOpen()`) is never written by
  a capture.
- No new user-visible strings.
- Commits: ask the user first; message written to a temp file and committed with
  `git commit -F`; one-line subject plus 2-3 sentences, lines of 90 characters or less, no
  co-author line. Ask about `Htmldocs/release_notes.html` before the last commit.

## Review Focus

1. **A very long sidebar** (a day with dozens of sessions in Session Information): the image
   must still save. Page height is capped at 30000 device pixels. Test: Task 1,
   `testCappedPageHeight`.
2. **Display scaling above 100%:** the lengthened window grab and the tall page must line up
   with no seam or shrinkage. Test: Task 1, `testInsertRowsKeepsRatio`; manual check in Task 4.
3. **A day with no graphs or no data:** the image is the on-screen size, no crash, no
   zero-height pixmap. Test: Task 1, `testGraphAreaNoGraphs`; manual check in Task 4.
4. **Cancel or failure in the save dialog:** the page is already back to normal before the
   dialog opens. Manual check in Task 4.
5. **Graphs scrolled, pinned or zoomed when F12 is pressed:** the image starts at the first
   graph with pinned graphs on top, at the zoom shown; afterwards the scroll position, zoom
   and the shared time scale are as before. Manual check in Task 4.

## File Structure

- Create `oscar/Graphs/tallcapture.h`, `oscar/Graphs/tallcapture.cpp`: pure arithmetic and
  image helpers for the tall capture (no widgets), unit tested.
- Create `oscar/tests/tallcapturetests.h`, `oscar/tests/tallcapturetests.cpp`.
- Modify `oscar/dailydetails.h`: which cards a capture opens (pure).
- Modify `oscar/dailydetailspanel.h/.cpp`: open the cards for a capture and put them back.
- Modify `oscar/Graphs/gGraphView.h/.cpp`: draw all graphs at a given size, own time scales.
- Modify `oscar/daily.h/.cpp`: decide when the tall capture applies; produce the tall page.
- Modify `oscar/mainwindow.h/.cpp`: choose the capture, join window grab and tall page.
- Modify `oscar/oscar.pro`: new files, beside the `graphcard` entries (main lists) and the
  `graphcardtests` entries (test lists).

Unit tests: build the `CONFIG+=test` target in
`oscar/build/UnitTests_Qt_6_12_0_MSVC2022_64bit` and run `.\test.exe` there (never bare
`test.exe`). The exit code is the number of failures; stdout is not captured, so add
`-o result.txt,txt` to read results (the file holds the last suite only). App build: the
`build-oscar` skill.

---

### Task 1: Tall-capture arithmetic and image helpers

**Files:**
- Create: `oscar/Graphs/tallcapture.h`, `oscar/Graphs/tallcapture.cpp`
- Create: `oscar/tests/tallcapturetests.h`, `oscar/tests/tallcapturetests.cpp`
- Modify: `oscar/oscar.pro`

**Interfaces:**
- Produces, in `namespace TallCapture`:
  - `constexpr int MaxDevicePixels = 30000;`
  - `int graphAreaHeight(const QVector<float> &heights, float gap, int needed);`
  - `int cappedPageHeight(int wanted, int onScreen, qreal devicePixelRatio);`
  - `QImage insertRows(const QImage &image, int y, int rows);`

- [ ] **Step 1: Write the failing tests** (`TallCaptureTests`, registered with `DECLARE_TEST`,
  following `tests/graphcardtests.h/.cpp`)

```cpp
void TallCaptureTests::testGraphAreaCrossingGraphDrawnWhole()
{   // graphs occupy 0-100, 108-208, 216-316
    QCOMPARE(TallCapture::graphAreaHeight({100, 100, 100}, 8, 250), 316);
}
void TallCaptureTests::testGraphAreaGraphEndsOnLine()
{
    QCOMPARE(TallCapture::graphAreaHeight({100, 100, 100}, 8, 208), 208);
}
void TallCaptureTests::testGraphAreaLineInGap()
{   // the next graph starts at 216, below the line: it is not drawn
    QCOMPARE(TallCapture::graphAreaHeight({100, 100, 100}, 8, 210), 210);
}
void TallCaptureTests::testGraphAreaGraphsEndAboveLine()
{
    QCOMPARE(TallCapture::graphAreaHeight({100, 100, 100}, 8, 500), 500);
}
void TallCaptureTests::testGraphAreaNoGraphs()
{
    QCOMPARE(TallCapture::graphAreaHeight({}, 8, 400), 400);
}
void TallCaptureTests::testGraphAreaFractionalHeights()
{
    QCOMPARE(TallCapture::graphAreaHeight({100.5f}, 8, 50), 101);
}
void TallCaptureTests::testCappedPageHeight()
{
    QCOMPARE(TallCapture::cappedPageHeight(5000, 900, 1.0), 5000);
    QCOMPARE(TallCapture::cappedPageHeight(40000, 900, 1.0), 30000);
    QCOMPARE(TallCapture::cappedPageHeight(40000, 900, 2.0), 15000);
    QCOMPARE(TallCapture::cappedPageHeight(500, 900, 1.0), 900);   // never below on-screen
}
void TallCaptureTests::testInsertRows()
{
    QImage image(4, 4, QImage::Format_RGB32);
    const QRgb rows[4] = { qRgb(10,0,0), qRgb(20,0,0), qRgb(30,0,0), qRgb(40,0,0) };
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) image.setPixel(x, y, rows[y]);
    const QImage out = TallCapture::insertRows(image, 2, 3);
    QCOMPARE(out.size(), QSize(4, 7));
    const QRgb expected[7] = { rows[0], rows[1], rows[1], rows[1], rows[1], rows[2], rows[3] };
    for (int y = 0; y < 7; ++y) QCOMPARE(out.pixel(3, y), expected[y]);
}
void TallCaptureTests::testInsertRowsNone()
{
    QImage image(4, 4, QImage::Format_RGB32);
    image.fill(Qt::red);
    QCOMPARE(TallCapture::insertRows(image, 2, 0), image);
}
void TallCaptureTests::testInsertRowsKeepsRatio()
{
    QImage image(4, 4, QImage::Format_RGB32);
    image.fill(Qt::red);
    image.setDevicePixelRatio(1.5);
    const QImage out = TallCapture::insertRows(image, 4, 2);    // at the bottom edge
    QCOMPARE(out.devicePixelRatio(), 1.5);
    QCOMPARE(out.size(), QSize(4, 6));
    QCOMPARE(out.pixel(0, 5), qRgb(255, 0, 0));
}
```

- [ ] **Step 2: Add the four files to `oscar.pro`, build the test target, confirm it fails**
  (the `TallCapture` functions are undefined).

- [ ] **Step 3: Implement the three functions in `Graphs/tallcapture.cpp`**

  - `graphAreaHeight`: `heights` are the drawn heights in drawing order, `gap` the space
    after each, `needed` the height that shows the sidebar to its bottom. It mirrors
    `gGraphView::renderGraphs()`:

    ```cpp
    float py = 0;
    for (float h : heights) {
        if (py >= needed) break;                        // starts below the line: not drawn
        if (py + h >= needed) return int(std::ceil(py + h));
        py = std::ceil(py + h + gap);
    }
    return needed;
    ```
  - `cappedPageHeight`: `qMax(onScreen, qMin(wanted, int(MaxDevicePixels / devicePixelRatio)))`.
  - `insertRows`: `y` and `rows` are in device pixels. Returns an image `rows` taller, same
    format and device pixel ratio: rows above `y` unchanged, then row `y - 1` repeated `rows`
    times, then the rows from `y` on. `rows <= 0` returns `image`; `y` is clamped to
    `1..height`.

- [ ] **Step 4: Build the test target and run `.\test.exe`** — exit code 0.

- [ ] **Step 5: Commit** (ask first).

---

### Task 2: Cards open for a capture

**Files:**
- Modify: `oscar/dailydetails.h` (beside `cardOpen` / `setCardOpen`, line ~40)
- Modify: `oscar/dailydetailspanel.h`, `oscar/dailydetailspanel.cpp`
- Test: `oscar/tests/dailydetailstests.h/.cpp`

**Interfaces:**
- Produces:
  - `inline int DailyDetails::captureCardsOpen(int bits)` — every `DailyCard` bit set,
    except `DC_EventBreakdown`, which keeps its value from `bits`.
  - `void DailyDetailsPanel::setCaptureOpen(bool capture);`
  - `int DailyDetailsPanel::contentHeight() const;`

- [ ] **Step 1: Write the failing test** in `DailyDetailsTests`

```cpp
void DailyDetailsTests::testCaptureCardsOpen()
{
    const int all = (1 << (DC_Statistics + 1)) - 1;                 // the eight cards
    const int pie = 1 << DC_EventBreakdown;
    QCOMPARE(DailyDetails::captureCardsOpen(0), all & ~pie);        // pie stays closed
    QCOMPARE(DailyDetails::captureCardsOpen(pie), all);             // pie stays open
    QCOMPARE(DailyDetails::captureCardsOpen(all & ~pie), all & ~pie);
}
```

- [ ] **Step 2: Build the test target, confirm it fails** (`captureCardsOpen` undefined).

- [ ] **Step 3: Implement `captureCardsOpen`** in `dailydetails.h`. `DC_Statistics` is the last
  enumerator; say so in a comment at the enum so a new card is added to the mask.

- [ ] **Step 4: Implement the panel methods**

  - The panel must find its cards: add a member
    `QVector<QPair<DailyCard, UiKit::CollapsibleCard *>> m_cards;`, cleared at the top of
    `setPage()` and appended to in `collapsible()`.
  - `setCaptureOpen(true)` calls `card->setOpen(DailyDetails::cardOpen(
    DailyDetails::captureCardsOpen(m_cardsOpen), which))` on each card;
    `setCaptureOpen(false)` calls `card->setOpen(DailyDetails::cardOpen(m_cardsOpen, which))`.
    `CollapsibleCard::setOpen()` emits nothing, so `m_cardsOpen` and the stored preference are
    untouched.
  - `contentHeight()` returns `widget() ? widget()->height() : 0` — the cards column, which
    the scroll area keeps at least as tall as its contents. It is valid once pending layout
    requests have been delivered (the caller does that, Task 4).

- [ ] **Step 5: Build the test target and run `.\test.exe`** — exit code 0.

- [ ] **Step 6: Commit** (ask first).

---

### Task 3: Graph view draws all graphs at a given size

**Files:**
- Modify: `oscar/Graphs/gGraphView.h`, `oscar/Graphs/gGraphView.cpp`

**Interfaces:**
- Consumes: `TallCapture::graphAreaHeight()` (Task 1).
- Produces (public, on `gGraphView`):
  - `void beginTallCapture();` — call while the view is still shown.
  - `int tallCaptureHeight(int needed) const;`
  - `QPixmap renderTallRasterPixmap(const QSize &size);`
  - `void endTallCapture();`

- [ ] **Step 1: Add capture state and the size override**

  - Private members: `bool m_tallCapture = false; bool m_tallOwnAxes = false;`
    `QSize m_canvas;` (invalid = the widget's own size) `int m_tallSavedOffsetY = 0;`
  - Private `int viewWidth() const` / `int viewHeight() const`: the canvas size when
    `m_canvas` is valid, else `width()` / `height()`.
  - In the paint path only — `paintView()` and `renderGraphs()` (today's lines 1424-1579 and
    1641-1720) — replace `width()` / `height()` with `viewWidth()` / `viewHeight()`. Mouse
    handling and scroll-bar code keep the widget's size.
  - `updateScale()` returns at once while `m_tallCapture` is set, so the on-screen scale
    (`m_scaleY`) is held while the view is hidden (hidden, `updateScale()` would zero it).

- [ ] **Step 2: Add the drawn-height helper**

  Private `float drawnHeight(gGraph *g) const`: `g->height() * m_scaleY`, plus, while
  `m_tallOwnAxes` is set, the sum of `layer->minimumHeight()` over the graph's visible layers
  for which `g->leavesAxisToPage(layer)` is true (the same sum the popout code makes at
  line ~525). `renderGraphs()` uses it in the four places it now computes
  `g->height() * m_scaleY` (pinned height, unpinned draw, pinned draw).

- [ ] **Step 3: Implement the four public functions**

  - `beginTallCapture()`: sets `m_tallCapture`; `m_tallOwnAxes = m_sharedXAxis`; saves
    `m_offsetY` and sets it to 0; sets `m_sharedXAxis = false` so each graph draws its own
    time scale.
  - `tallCaptureHeight(needed)`: builds the drawn heights of the visible, non-empty graphs in
    drawing order (pinned first, then the rest, each in list order) with `drawnHeight()`, and
    returns `TallCapture::graphAreaHeight(heights, graphGap(), needed)`.
  - `renderTallRasterPixmap(size)`: as `renderToRasterPixmap()`, but with `m_canvas = size`
    for the duration, so the pixmap is `size` times the device pixel ratio. Resets `m_canvas`
    before returning.
  - `endTallCapture()`: restores `m_sharedXAxis` and `m_offsetY`, clears the flags, and calls
    `redraw()` so the graphs' on-screen rectangles (used by mouse handling and the shared
    time scale) are rebuilt by the next paint.

- [ ] **Step 4: Build the app (build-oscar skill)** — compiles clean, no new warnings. Build
  the test target and run `.\test.exe` — exit code 0. Behaviour is checked in Task 4.

- [ ] **Step 5: Commit** (ask first).

---

### Task 4: Tall page and the screenshot command

**Files:**
- Modify: `oscar/daily.h`, `oscar/daily.cpp`
- Modify: `oscar/mainwindow.h`, `oscar/mainwindow.cpp` (`on_action_Screenshot_triggered`,
  `DelayedScreenshot`, lines ~2610-2754)

**Interfaces:**
- Consumes: Task 1 (`cappedPageHeight`, `insertRows`), Task 2 (`setCaptureOpen`,
  `contentHeight`), Task 3 (the four `gGraphView` functions).
- Produces:
  - `bool Daily::tallCaptureAvailable() const;`
  - `QPixmap Daily::grabTall();`

- [ ] **Step 1: `Daily::tallCaptureAvailable()`** — true when all hold: the left column is
  loaded and shown (`ui->splitter->isVisible()`), the left sidebar has width
  (`ui->splitter_2->sizes().at(0) > 0`), and the Details tab is in front
  (`ui->tabWidget->currentWidget() == webView->parentWidget()`; the tabs are movable, so
  compare widgets, not indexes).

- [ ] **Step 2: `Daily::grabTall()`** — one synchronous function; it never runs the event
  loop. "Deliver layouts" below means
  `QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest)`. Order:

  1. Remember: the page size, the Details view's vertical scroll value (`m_detailsPanel` on
     Modern, `webView` on Classic), the focus widget (`QPointer`).
  2. `GraphView->beginTallCapture()` (the view is still shown here).
  3. Modern: `m_detailsPanel->setCaptureOpen(true)`; hide `m_timeAxis`.
  4. Hide `GraphView` and `scrollbar`. Deliver layouts.
  5. Sidebar extra: Modern `m_detailsPanel->contentHeight() -
     m_detailsPanel->viewport()->height()`; Classic
     `ceil(webView->document()->size().height()) - webView->viewport()->height()`;
     not below 0.
  6. `needed = ui->graphFrame->contentsRect().height() + extra`;
     `graphArea = GraphView->tallCaptureHeight(needed)`.
  7. `pageHeight = TallCapture::cappedPageHeight(height() + extra + (graphArea - needed),
     height(), devicePixelRatioF())`. `resize(width(), pageHeight)`. Deliver layouts.
  8. `QPixmap page = grab();` Then paint
     `GraphView->renderTallRasterPixmap(area.size())` onto it at `area.topLeft()`, where
     `area` is `ui->graphFrame->contentsRect()` mapped to the page.
  9. Restore, in reverse: page size; show `GraphView`, `scrollbar` and (Modern)
     `m_timeAxis`; `setCaptureOpen(false)`; deliver layouts; scroll value; focus;
     `GraphView->endTallCapture()`.
  10. Return `page`.

  Restoration (step 9) must run on every path out of the function.

- [ ] **Step 3: MainWindow — choose the capture**

  - New member `bool m_tallScreenshot = false;`.
  - `on_action_Screenshot_triggered()`: set `m_tallScreenshot = daily &&
    ui->tabWidget->currentWidget() == daily && daily->tallCaptureAvailable()`. When true,
    skip `hideSpaceHogs()` and the hiding of `ui->sidebarTabs`; still start the 250 ms timer
    (it lets the menu close).
  - `DelayedScreenshot()`: move today's grab (the `CapturedView` block through the overlay
    painter, lines ~2640-2679) into a private `QPixmap grabClientArea();` unchanged. The
    header line, file dialog and save stay in `DelayedScreenshot()` and serve both captures.
    The final restore block (`showSpaceHogs()`, `sidebarTabs`) runs only when
    `m_tallScreenshot` is false.

- [ ] **Step 4: MainWindow — `QPixmap grabClientAreaTall();`** (private), used when
  `m_tallScreenshot` is true. Synchronous, no event loop:

  1. `setUpdatesEnabled(false)`. Remember and hide `ui->sidebarTabs`. Deliver layouts.
  2. Remember `daily`'s rectangle in window coordinates (`mapTo(this, ...)`, on-screen size).
  3. `QPixmap page = daily->grabTall();`
  4. `QPixmap client = grabClientArea();`
  5. Restore `ui->sidebarTabs`. Deliver layouts. `setUpdatesEnabled(true)`.
  6. Join: `extra = page height - on-screen page height` (device-independent). Lengthen the
     window grab with `TallCapture::insertRows(client.toImage(), y, rows)`, where `y` is the
     page's bottom edge and `rows` is `extra`, both multiplied by the grab's device pixel
     ratio and rounded. Paint `page` onto the result at the page's top-left. Return it as a
     pixmap carrying the same device pixel ratio.

- [ ] **Step 5: Build the app (build-oscar skill)** — compiles clean, no new warnings. Build
  the test target and run `.\test.exe` — exit code 0.

- [ ] **Step 6: Check the grab with updates disabled.** Take one tall screenshot. If the page
  part of the image is blank or stale, remove the `setUpdatesEnabled()` pair from
  `grabClientAreaTall()` — the pass never returns to the event loop, so nothing is painted on
  screen either way — and note it in a comment.

- [ ] **Step 7: Manual checks** (test profile "Blue Dragon 2025 test"; each in Modern and in
  Classic unless marked):

  - Details in front, a night with many graphs: the image shows the whole sidebar; the last
    graph is whole and crosses the sidebar's bottom; nothing follows it. Every graph has a
    time scale.
  - Modern: all cards open in the image except Event Breakdown, which matches the screen;
    afterwards the cards are as before, also after restarting OSCAR.
  - Calendar open and closed: the image matches the screen.
  - Nothing visibly changes on screen during the capture; the right sidebar does not blink.
  - Afterwards: right sidebar, sidebar scroll position, graph scroll position, zoom and
    (Modern) the shared time scale are as before; the mouse still tracks the graphs correctly.
  - Scrolled halfway down the graphs, one graph pinned, zoomed to an hour: the image starts
    at the first graph with the pinned graph on top, at that zoom.
  - A day with no data, and a night with few graphs (stretched to fill the window): image
    saved at on-screen height or a little more; graphs the same size as on screen.
  - Cancel the save dialog: the page is already normal behind the dialog.
  - Windows display scaling 150%: no seam where the page meets the menu and tab rows; text
    sharp; right-hand edge aligned.
  - Dark mode: the lengthened area beside and below the page matches the window's colours.
  - Classic: the whole Details text is in the image with no scroll bar, pie chart in place.
  - Events tab in front; left sidebar hidden; Overview; Statistics: the capture is today's.
  - Linux and macOS: note in the hand-off that these are untested here.

- [ ] **Step 8: Commit** (ask first; ask about `Htmldocs/release_notes.html`).

---

### Task 5: Version header line follows the theme

Applies to every screenshot, not only the tall one. Today the line that replaces the title
bar is always black text on white (`DelayedScreenshot()`: `pixmap.fill(Qt::white)`,
`fillRect(..., Qt::white)`, `setPen(Qt::black)`).

**Files:**
- Modify: `oscar/mainwindow.cpp` (`DelayedScreenshot()`, the header block, lines ~2693-2708)
- Modify: `Notes/Developer Notes/BUG_FIXES.md`

- [ ] **Step 1:** Ask the user whether to open a GitLab issue for this (bug-fix workflow); note
  the IID if one is made.
- [ ] **Step 2:** Replace the two white fills with `Theme::color(Theme::Role::Page)` and the pen
  with `Theme::color(Theme::Role::Ink)` (`uikit/theme.h`, already used in this file).
- [ ] **Step 3: Build the app; manual check** — a screenshot in light mode has the header as
  before; in dark mode the header is dark with light text, matching the window below it. Check
  one tall capture and one ordinary capture (Overview).
- [ ] **Step 4:** Log the fix in `Notes/Developer Notes/BUG_FIXES.md`.
- [ ] **Step 5: Commit** (ask first; `Closes #IID` in the body if an issue was opened).

---

## As built (2026-11-16)

Where the code differs from the tasks above:

- **No `setUpdatesEnabled()` pair around the capture (Task 4, step 4).** Qt does not draw the
  children of a widget whose updates are disabled, so the grabs would be empty. The capture is
  one pass that never returns to the event loop, so nothing is painted on screen regardless.
- **Layout requests are delivered to the Daily page and its children only (Task 4, step 2).**
  Delivered application-wide, a request reaches the layout of the page's parent, which puts the
  page back to its on-screen size in the middle of the capture. The symptom was a page far
  taller than the sidebar needed, with the last graph cut off.
- **The Details view's queued calls are run as well.** A scroll area shows or hides its scroll
  bars through a queued call; without this the vertical scroll bar stayed in the image.
- **One correction pass after the first resize**, for a layout that gives part of the extra
  height to something other than the Details view and the graph area.
- **The choice of capture is checked again when the timer fires**, in case the page changed in
  the 250 ms since the command.
- `renderGraphs()` computed a graph's scaled height in three places, not four.

Checked by running the build (Windows, 100% scaling, dark mode, a night with 17 graphs):
Modern and Classic captures, calendar shown and hidden, cards restored afterwards, the stored
card preference unchanged, an ordinary capture with the Events tab in front. Not checked:
display scaling above 100%, a day without data, a zoomed or scrolled graph list, Linux, macOS.
