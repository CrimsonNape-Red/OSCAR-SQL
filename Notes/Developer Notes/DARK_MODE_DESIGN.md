# Dark Mode Support — Assessment and Design Note

**Date:** 2026-10-16
**Status:** Assessed, not scheduled. No work in progress.

## Question

How hard would it be for OSCAR to run in either light or dark mode, selected by a user
preference or inherited from the operating system?

## Verdict

Moderate difficulty: not a rewrite, but broad. Switching the mode is easy. The cost is
that OSCAR has light colours hard-coded in hundreds of places. Estimate: several weeks
of careful work, plus a long tail of visual QA across the supported platforms.

## Current state

- `main.cpp` forces light mode with `styleHints()->setColorScheme(Qt::ColorScheme::Light)`,
  guarded to Qt >= 6.5. A commented-out line just above it already notes that dark mode
  "would require an exhaustive change to OSCAR".
- Several widgets carry patches that force light mode on a dark system. For example,
  `ProfileSelector` sets a white `QPalette` and a black-on-white stylesheet, because
  native Windows styles ignore palette changes.
- The Fusion style is already a user preference (`AppSettings::useFusionTheme()`).

## Inventory of hard-coded colour

Counts are for `oscar/`, excluding `build/` and `SleepLib/thirdparty/` (2026-10-16).

| Area | Count | Notes |
|---|---|---|
| `QColor(...)`, `Qt::white`/`Qt::black` etc. in .cpp | ~374 | Heaviest in `schema.cpp`, `daily.cpp`, `gOverviewGraph.cpp`, `gGraphView.cpp` |
| `#rrggbb` literals in .cpp | ~100 | Mostly in stylesheets and generated HTML |
| `setStyleSheet` calls | 49 in 15 files | Plus 67 `styleSheet` properties in 16 `.ui` files |
| Inline HTML colours (`bgcolor`, `color:`, `<font color`) | ~145 | Daily, Statistics, Welcome, About, etc. |
| Palette-aware code (`palette()`, `QPalette::`) | ~24 | Very little code respects the theme today |

## Main pieces of work

1. **Graph engine.** `gGraph` and `gGraphView` paint plain white backgrounds, and axis,
   grid and text colours are fixed. These need to come from a theme object.
2. **Channel and flag colours.** Defaults are in `common_gui.cpp` and `schema.cpp`, and
   users can customise them. Some defaults are unreadable on a dark background
   (e.g. `COLOR_FlowRate = Qt::black`, `COLOR_Plethy = Qt::darkBlue`). This needs either
   a second set of dark defaults (and a decision on how user overrides work in each
   mode) or automatic lightness adjustment.
3. **Generated HTML panels.** Statistics, Daily details, Welcome and others are built as
   HTML with fixed colours. They need theme tokens or a stylesheet chosen per mode.
4. **Stylesheets in .ui and .cpp files.** Each needs to be removed in favour of the
   palette, or made to switch with the theme.
5. **Things that must stay light.** Printed reports and exported graph images should keep
   a white background whatever the screen mode. Rendering needs a print/export theme
   separate from the screen theme.
6. **Platform mechanics.**
   - Windows' native styles (`windowsvista`) handle dark palettes poorly, so dark mode on
     Windows effectively needs Fusion.
   - Some testers build with Qt < 6.5, which has no `setColorScheme`. Fusion with a custom
     dark `QPalette` works on every supported Qt version and gives a single code path.
   - Following the system mode live means handling `QStyleHints::colorSchemeChanged`.

## Suggested approach (if undertaken)

1. Add a small `Theme` class holding named colour roles (graph background, grid, text,
   HTML background/text, etc.), and a three-way preference: System / Light / Dark.
2. Convert the graph engine and the HTML generators to use it. This covers most of what
   users see.
3. Give channel defaults light and dark variants.
4. Work through the stylesheets and dialogs one at a time, removing the existing
   force-light patches as each is converted.

Each step can ship with dark mode still hidden behind the preference, keeping the risk
low for the ~25 distribution targets while the remaining work is done.
