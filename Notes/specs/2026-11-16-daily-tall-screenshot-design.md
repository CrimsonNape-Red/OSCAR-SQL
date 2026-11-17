# Daily page: full-height screenshot — design

Date: 2026-11-16

## Intent

A screenshot (F12, View > Take Screenshot) of the Daily page should be one image a forum
helper can read without asking for more: the whole Details tab, and enough graphs to fill
the height beside it. Today the image is a grab of the window as displayed, so it can never
be taller than the window, and on Modern it shows only the cards the user happens to have
open.

## Scope

- Applies to the Daily page, Modern and Classic, with the left sidebar shown and its Details
  tab in front.
- Everything else keeps today's capture unchanged: Overview, Statistics and the other pages;
  Daily with the left sidebar hidden; Daily with the Events, Notes, Bookmarks or Search tab
  in front (the showing tab is captured at its on-screen height, with the calendar hidden,
  as now).

## What the tall capture shows

Both styles:

- **Right sidebar:** closed for the capture, as today.
- **Calendar and pie chart:** left as the user set them. (Today's capture hides the calendar
  and, on Classic, drops the pie chart to save height; the tall capture does neither.)
- **Left sidebar:** drawn at its full height, without scrolling.
- **Graphs:** drawn from the top of the list whatever the scroll position, pinned graphs
  first as on screen, at the current zoom and at the heights currently displayed (including
  the stretch OSCAR applies when few graphs are shown).
- **Time scale:** every graph carries its own.
- **Height:** the page is as tall as needed to show the left sidebar to its bottom, then
  extended so the graph crossing that line is drawn whole; no graph after it is drawn.
  - Never shorter than the page is on screen.
  - If the graphs end above the sidebar's bottom, the space under them is blank; the graphs
    are not stretched further.
- **Unchanged:** the version header line, menu bar, page tabs, the window width, the PNG/8
  file format, the save dialog and its preferences.

Modern only:

- **Cards:** every collapsible card of the Details tab is opened, except Event Breakdown (the
  pie chart), which stays as the user set it.
- **Time scale:** every graph that normally leaves its time scale to the page draws its own.
  Each such graph is taller by the height of its scale, so the plot keeps its on-screen
  height (as a popped-out graph already does). The shared time scale under the graphs is
  left out; the status line and the Daily top bar stay.

Classic only:

- **Left sidebar:** the Details tab is one HTML document; it is shown at the document's full
  height. There are no cards to open.
- **Graphs:** already draw their own time scales, so they are drawn as on screen. The bar
  under the graphs stays.

## After the capture

The cards (Modern), the right sidebar, the page size, the scroll positions of the sidebar
and the graphs, and the shared time scale (Modern) are put back as they were. The stored
open/closed preference for the cards is never written. The user sees none of it: the capture
is one pass that never returns to Qt's event loop, so nothing is painted on screen between the
first change and the last restore, and everything is back in place before the save dialog opens.

## Approaches considered

1. **Stretch the page off-screen (chosen).** With updates off, open the cards, give the
   Daily page the tall size, let Qt lay it out, draw it, restore. The sidebar, session bar
   and status line land where the real layout puts them, so later changes to the page carry
   into captures without more code, and the same code serves Modern and Classic.
2. **Stitch separately drawn parts.** Leaves the live page's size alone, but the capture
   code would duplicate the page's layout, once per style, and break quietly when the page
   changes.
3. **A hidden second Daily page.** Leaves the live page untouched, but zoom, switched-off
   sessions, graph order and heights and event selection would all have to be copied, and
   parts of the code assume a single Daily page.

## Constraints

- The graph view (a QOpenGLWidget) must not be enlarged or grabbed through OpenGL: its
  framebuffer would be reallocated at the tall size, and GL grabs are corrupted on some
  drivers (#237). The graphs are drawn with the existing raster path
  (`gGraphView::renderToRasterPixmap()`), extended to draw at a given size rather than the
  widget's own.
- The top-level window is not resized (the platform may clamp it to the screen). The menu
  bar and page tabs come from an ordinary grab of the window and are placed above the tall
  page.
- No platform-specific code. High-DPI: the image carries the display's device pixel ratio,
  as today.
- Restoration must also happen when the user cancels the save dialog or the save fails.

## To verify in the build

- Classic: the HTML view (a QTextBrowser) drawn at a height of several thousand pixels —
  full document shown, no scroll bar, images (the pie chart) in place.

## Left to the builder

- Which widget is stretched (the Daily page or a container of it) and how the window grab
  and the tall page are joined.
- How the graph view is told to draw at a size other than its own, and how the height
  needed for the cutoff rule is worked out (a function of the graph heights, testable
  without a window).
- Where the capture code lives; `MainWindow::DelayedScreenshot()` is already long, so the
  Daily-specific part belongs in `Daily`.
- Unit tests for the height/cutoff calculation and for the cards being restored.
