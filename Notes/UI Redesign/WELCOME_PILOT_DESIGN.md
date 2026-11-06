# Welcome Page Pilot — Design

**Date:** 2026-10-31
**Branch:** a short branch off `ui-next` (see `UI_REDESIGN_PLANNING.md`, section 1)
**Visual reference:** `Welcome_Concept.html` in this folder ("With data", "By status", light)

## Intent

Build the redesigned Welcome page in Qt so testers can judge, on their own data, whether the
new UI is better than the current one. Their verdict decides whether the redesign carries on
to other pages. A second question is whether colouring cards red/green by status is the right
emphasis.

The pilot is also the first use of the shared design system (theme module and reusable
components) that later pages will build on.

## Goals

- Testers see the new page with their own most recent night, and can switch to the old page
  to compare.
- The page follows the mockup's layout: same tiles, cards and spacing. Colours and fonts
  follow the system theme and the application font (see Constraints).
- Testers can try each card-colour option and say which they prefer.
- The theme module and components are reusable by the next redesigned page.

## Non-goals

- Dark mode as a feature. OSCAR still forces the light colour scheme; the theme already
  follows the palette, so the page will turn dark when OSCAR allows a dark palette.
- Any change to Daily, Overview, Statistics, menus, or the tab bar. No Insights tab.
- The Insights page itself.
- Clickable cards. Cards are display-only in the pilot; click behaviour can be added once
  tester feedback shows what people expect.
- An "import now" reminder.
- Animation.

## Anti-goals (failure even if it works)

- **Numbers that disagree with Daily or Statistics.** One mismatched figure undermines trust
  in the whole page. Every value must come from the same calculations OSCAR already uses.
- A slow or janky page, especially on macOS (painting has been slow there before, #235).
- Redesign changes leaking into other pages, or into master before the redesign is released.
- A switch that doesn't give a fair comparison (e.g. old page broken or missing data).

## Constraints

- Qt Widgets and stylesheets, with custom painting only where needed (card shadows, icons,
  the events bar). No QML, no WebEngine.
- Cross-platform; must build on all ~25 targets, including testers on Qt < 6.5.
- **No hard-coded colours, sizes or fonts in the page or its components.** All come from the
  theme module, which is readable from C++.
- **Same design rules as the Oximetry Wizard** (decided 2026-11-03). Base colours come from
  the system palette (page = Window, cards = Base, text = Text, borders = Mid, accent =
  Highlight). Good and warning colours are the wizard's, with light and dark values; other
  status colours have both too, and their tints are mixed into the card colour. Cards and
  tiles have a 10 px corner radius.
- **Application font only**, sized as multiples of it (headings larger, captions smaller),
  so the font and size chosen in Preferences apply. The bundled Lexend and Atkinson
  Hyperlegible fonts were tried and removed.
- New page and components in **new files**. The old `Welcome` class stays unchanged and fully
  working. Shared-file edits are limited to `mainwindow.cpp` (the page is created in two
  places, at startup and after a rebuild, and `refreshPage()` is called from many places
  through the `welcome` pointer), `preferencesdialog.*`, `appsettings.*`, `oscar.pro` and the
  resource file.
- No schema changes. No loader changes.
- All user-visible text through `tr()`. Don't run lupdate on the branch.
- Follow the project conventions in `AGENTS.md` (file headers, Doxygen, copyright line).

## Settings (Preferences)

Two app-wide settings, stored like the existing Fusion-theme option:

1. **Use new Welcome page** — checkbox. On by default in `ui-next` builds.
2. **Welcome card colour** — Plain / By group / By status. Default **Plain**. Enabled only
   while the new page is on.

Changing either takes effect when Preferences is closed with OK, without a restart.

## Page content

Top to bottom, as in the mockup.

### Header and tiles

- Logo and the title "Welcome to the Open Source CPAP Analysis Reporter".
- Six tiles with line icons, icon above text: **Import CPAP data** (highlighted as the main
  action), **Oximetry wizard**, **Daily view**, **Overview**, **Statistics**, **Insights**.
- Each tile does what the matching button on today's page does. Daily, Overview and
  Statistics are disabled until there is data, as now. Oximetry wizard is always enabled.
- Insights is shown disabled with a "Coming later" label.

### Most recent night

- A strip with the device image, brand and model, the night's date (long format, system
  locale) and "last night" / "N days ago", using the same wording rules as today's page.
- The night shown is the profile's last CPAP day, as today.
- A chip shows the night's therapy mode. The over/under compliance chip was removed
  (2026-11-03).
- Today's ResMed S9 card warning is kept.

### Metric cards

Eight cards. A card is **not shown** when the device doesn't record that metric; never show
a zero for "not recorded".

| Card | Value for the night | Better when |
|---|---|---|
| Time used | Hours of use | Higher |
| AHI | AHI | Lower |
| Events | Breakdown bar of the AHI events (OA, CA, H and any others counted in AHI) | Lower |
| Pressure | Mode-dependent, same rules and wording as today's page (constant, percentile, EPAP–IPAP, etc.) | Neutral |
| Leak rate | Average leak | Lower |
| Large leaks | Time in large leak, as Statistics reports it | Lower |
| Flow limitation | As Statistics reports it | Lower |
| Sessions | Number of sessions | Neutral |

Each card shows:

- the night's value with units;
- the **7-day** and **30-day averages** of the nights *before* the shown night (matching how
  today's page computes its 7-day figures), each with an angled up/down arrow beside the
  number (no % difference).

Cards are compact. The sparkline of the last 30 nights was dropped after the first build
(2026-11-02) and its code removed (2026-11-03).

**Choosing cards.** A gear button to the right of the colour legend opens a checklist of
every card (oximetry included). Unticked cards are hidden. The choice is stored per profile
as the list of *hidden* card keys (profile preference `WelcomeHiddenCards`), so a card added
in a later version appears until the user hides it. If every card is hidden, a short note
points to the gear button.

**Adding a card later:** add a `MetricId`, write its builder function in
`welcomemetrics.cpp`, and add one row (id, stored key, title, builder) to the card table
there. The checklist and the stored list pick it up from that table; never change an
existing key.

Bi-level pressure cards compare IPAP, and the card says so ("Averages compare IPAP").

**Colour** follows the card-colour setting:

- **Plain:** all cards neutral; only the arrows carry colour.
- **By group:** cards tinted by group (breathing, mask, usage), as in the mockup legend.
- **By status:** a card is tinted green or red only when both comparisons agree it is better
  or worse. Otherwise it stays neutral. Pressure and Sessions are always neutral.

A small no-change band keeps trivial differences from being coloured.

### Oximetry card

Shown only when there is oximetry data **for the night shown** (from an oximeter, or from a
CPAP device's built-in SpO₂/pulse channels). Shows SpO₂ average and minimum and average
pulse, using the same values Daily shows. Same card style, always neutral, with no averages.

### First run

With no data, the cards and the most-recent-night strip are replaced by the three-step panel
from the mockup. It keeps today's advice: check Preferences first, some preferences are
forced when a ResMed device is detected, and the first import can take a few minutes.

## Reusable pieces

Kept in their own files so later pages can use them:

- **Theme module:** named colour roles derived from the palette, spacing, corner radius,
  font sizes relative to the application font.
- **Metric card**, **action tile**, **icon button**, and **section header** components.

## Left to the builder

- Class and file names, and how the new page plugs into `mainwindow.cpp` (common base class,
  interface, or otherwise).
- Layout mechanics, including how the card grid wraps on narrow windows.
- How shadows are drawn (`QGraphicsDropShadowEffect` or custom painting). Check macOS
  performance either way.
- Number formatting and rounding, as long as values match Daily/Statistics.
- The width of the no-change band.
- How data calculations are shared with, or reused from, the old page and Statistics.

## Testing

- Values checked against Daily and Statistics for several profiles and device types
  (CPAP, APAP, bi-level, ASV), including a device without flow limitation or large-leak data.
- Switching the checkbox back and forth; the old page still works.
- First-run state with an empty profile; oximetry card present/absent.
- Window resizing, HiDPI scaling, long translated strings.
- Windows, macOS and Linux, with attention to macOS painting speed.
