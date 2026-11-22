# Daily page: capture for analysis

Status: draft 2026-11-14, for review. Nothing here is built yet.

## Intent

People post a screenshot of the Daily page when they ask for help. Today's capture shows only
what fits on the screen, so the helper often lacks the device settings, the statistics or the
session list, because those cards were closed or scrolled out of view. This adds a second kind
of capture, made for that use: one image that holds the whole Details tab with every card open,
next to as many graphs as fit beside it, even when that is taller than the screen.

Goals:
- One image tells a helper what they need: the figures, the device and its settings, the
  sessions, and the graphs.
- The image does not depend on which cards the user happens to have open, or on the height of
  their monitor.
- Files stay small enough to attach to a forum post. One forum limits attachments to 1.2 MB.

Anti-goals:
- An image with every graph. A profile can have a great many graphs, and each adds to the file.
- Changing what the user sees on screen, or their saved card states, as a side effect.

## Decided

**Two captures.** The View menu keeps the existing capture, unchanged, and gains the new one.
F12 moves to the new one. On any page other than the Modern Daily page, the new command does
the same as the existing capture, so F12 always produces an image.

**Modern Daily only.** The Classic Daily page is not covered.

**What the image holds.**
- The header line with the OSCAR version, as now. The window title bar is left out, as now.
- The calendar closed and the right sidebar hidden, as now.
- Left column: the day bar, the tab bar, then the Details tab at its full height with every
  card open, then the session bar.
- Right column: the top bar (device line and buttons), the graphs, the time axis and the status
  line under it.
- Image height: the height needed to show the whole left column, or the window's height when
  that is greater. When all the cards already fit on screen with every card open, the image is
  the size of today's capture.
- Graphs fill the right column down to that height and no further. Graphs that do not fit are
  left out, as they are on screen.
- Light or dark as the page is at the time.

**Nothing on screen changes.** The cards the user had closed stay closed after the capture, and
the stored open or closed states are not touched.

**File format.** Full resolution (the display's own scaling is kept), saved as an 8-bit PNG:
at most 256 colours, chosen from the image itself, with no dithering, at maximum compression.
An image that has 256 colours or fewer keeps them exactly. The existing capture keeps its
present format.

**Saving.** Same folder, file name pattern, prompt and confirmation message as the existing
capture.

**Size limit.** The saved file is checked against a size limit. The limit is a preference,
1.2 MB unless changed, set in Preferences beside "Always save screenshots in the OSCAR Data
folder".

## My calls, to confirm

- **Menu wording.** "Take Screenshot" stays for the existing capture; the new one is "Capture
  Daily Page for Analysis".
- **Another tab in front.** When Events, Notes, Bookmarks or Search is the open tab, the image
  still shows Details, since that is what the capture is for. The user's tab stays in front on
  screen.
- **Graph position.** The graphs start from where the user has scrolled them, at the zoom the
  user has set, with pinned graphs at the top as on screen. The user arranges the view; the
  capture only extends it downward.
- **Over the limit.** The file is saved as it is, and the confirmation message says that it is
  larger than the limit, giving both sizes, so the user knows before trying to attach it.
  OSCAR does not shrink the image or drop graphs to get under the limit.
- **Turning the check off.** A limit of 0 means no check.
- **Which capture.** The limit applies to the new capture only; the existing one is unchanged.

## To be judged when built

The 8-bit format is decided on the expectation that the page (flat fills, thin lines, text)
loses nothing visible and the file is about half the size or less. Neither has been measured.
Compare captures of real nights, light and dark, in both formats: file size, the colours of the
graph lines and event marks, small text, and the shading of the pie. If channel colours shift
visibly, the format is to be reconsidered before release.

## Not in this work

- The Classic Daily page, and other pages.
- Capturing every graph, or choosing which graphs go in.
- Reducing the image to 100% scale, or shrinking it to meet the size limit.
- Changing the format of the existing capture.
- Printed reports.

## Notes for the builder

- The existing capture is `MainWindow::on_action_Screenshot_triggered` (hides the calendar via
  `Daily::hideSpaceHogs` and the right sidebar, then waits 250 ms) and
  `MainWindow::DelayedScreenshot`. That routine already builds a composite: it grabs the
  window with the graph views hidden, paints a raster render of each graph view over the grab
  (`gGraphView::renderToRasterPixmap`), and adds the header line. Graph views must not be
  grabbed directly; see the comment there about #237.
- A window cannot be made taller than the screen, so the tall image has to be assembled from
  parts on a larger canvas, not grabbed from a resized window.
- The cards are built by `DailyDetailsPanel::setPage` from the open bits given to
  `setCardsOpen`; `Daily` stores those bits through `AppSetting->setDailyCardsOpen` when the
  panel emits `cardToggled`. The capture needs all cards open without that signal reaching the
  setting. The panel is a `QScrollArea`; `grab()` on its content widget renders all of it, not
  only the part in view. The pie in the Event Breakdown card is a pixmap, not a graph view.
- `gGraphView::paintView` and `renderGraphs` lay out against the widget's own `width()` and
  `height()`, and `renderToRasterPixmap` sizes its pixmap the same way. Rendering to a greater
  height needs a way to give the height for one render.
- The time axis (`DailyTimeAxis`) and the status line sit under the graph view, and the top
  bar (`DailyToolbar`) above it; in the image they go at the bottom and top of the right
  column.
- `QImage::convertToFormat(QImage::Format_Indexed8)` is not suitable for more than 256
  colours: it maps to a fixed colour cube with dithering. The palette has to be built from the
  image (median cut or similar). This is pure code and can have unit tests.
- `QPixmap::save` is called with the default quality; for PNG, quality 0 is the smallest file.
- The existing screenshot preference is `AppSetting->dontAskWhenSavingScreenshots`
  (`dontAskWhenSavingScreenshotsCheckbox` in `preferencesdialog.ui`); the limit goes with it.
