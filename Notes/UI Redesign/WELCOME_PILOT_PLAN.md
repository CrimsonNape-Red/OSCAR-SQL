# Welcome Page Pilot Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A redesigned Welcome page in Qt Widgets, switchable against the old page from Preferences, built on a reusable theme module and components.

**Architecture:** The new page (`WelcomePage`) and the old one (`Welcome`) share a small base class so `MainWindow` can hold either. Pure logic (comparisons, formatting, sparkline geometry) lives in unit-tested files under `oscar/uikit/`. Data for the cards comes from one class (`WelcomeMetrics`) that calls OSCAR's existing calculations only. Widgets take all colours, sizes and fonts from `Theme`.

**Tech Stack:** C++17, Qt 6.10.2 Widgets (no new Qt modules), qmake, OSCAR's `AutoTest` unit-test target (`CONFIG+=test`).

**Spec:** `Notes/UI Redesign/WELCOME_PILOT_DESIGN.md`. Visual reference: `Notes/UI Redesign/Welcome_Concept.html` (With data, By status, Light). Source of the mockup's CSS values: the `:root` block at the top of that file.

## Global Constraints

- Work in worktree `C:\OSCAR\OSCAR-next` on branch `welcome-pilot`, created off `ui-next`. Merge back into `ui-next` when done. Never commit to master.
- C++17, compile clean with Qt 6.10.2; also must build with Qt < 6.5 (no 6.5+ APIs).
- No new Qt modules (`QT +=` line in `oscar.pro` unchanged). In particular no QtSvg.
- No hard-coded colours, font families, or sizes in page or component code; everything comes from `Theme`.
- The old `Welcome` class keeps its behaviour. Its only change is its base class.
- No schema changes, no loader changes, no edits under `oscar/SleepLib/thirdparty`.
- All user-visible text through `tr()`. Do not run lupdate.
- New files get the module description header, "Copyright (c) 2026 The OSCAR Team", GPL notice, and Doxygen comments (see `oscar/tests/portablemodetests.h` for the header form). Add every new file to `oscar/oscar.pro` (tests to the `test {}` section).
- Don't add entries to `Notes/Developer Notes/BUG_FIXES.md`.
- Commits: one-line subject plus 2–3 sentence body, lines ≤ 90 chars, no Co-Authored-By. Write the message to a temp file, `git commit -F`, check `git log -1 --pretty=%B`. Ask the user before each commit and before any push.
- Build with the `build-oscar` skill after QtCreator's active project is set to `C:\OSCAR\OSCAR-next\oscar\oscar.pro`. Unit tests: build with `CONFIG+=test`, run `.\test.exe` (exit code = number of failures).

## Review Focus

1. **Gaps in history** (nights with no CPAP data inside the 7/30-day windows, or fewer than 7 nights of data in total): averages use only nights with data; with zero such nights the comparison shows "—" and the card is untinted. Test: `ComparisonTests::testNoHistory`, plus manual check in Task 6.
2. **Zero baselines** (large leaks average 0, flow limitation 0): no division by zero, no "inf%". Night 0 vs average 0 is "same"; night > 0 vs average 0 is "—". Test: `ComparisonTests::testZeroAverage`.
3. **RDI preference on:** AHI card title, value, events total and averages all switch to RDI consistently. Manual check in Task 6 against Daily.
4. **Very narrow or very wide windows, HiDPI, long translated strings:** tiles and cards re-flow (6/3/2 and 4/2/1 columns); no clipped values. Manual check in Task 6.
5. **Profile switch / rebuild / import while the new page is showing:** the page is recreated or refreshed through the same paths as the old one, with no stale data and no crash. Manual check in Task 6.

---

### Task 1: Settings, shared base class and the page switch

**Files:**
- Create: `oscar/welcomepagebase.h`, `oscar/welcomepage.h`, `oscar/welcomepage.cpp`
- Modify: `oscar/welcome.h` (base class only), `oscar/mainwindow.h` (`welcome` member type, two new methods), `oscar/mainwindow.cpp` (creation at ~line 799 and ~line 3149; Preferences handling), `oscar/SleepLib/appsettings.h/.cpp`, `oscar/preferencesdialog.ui/.cpp`, `oscar/oscar.pro`

**Interfaces:**
- Produces:
  - `class WelcomePageBase : public QWidget` (`Q_OBJECT`, `using QWidget::QWidget;`, `virtual void refreshPage() = 0;`). `Welcome` and `WelcomePage` both derive from it; `Welcome::refreshPage()` gains `override`.
  - `class WelcomePage : public WelcomePageBase` with `explicit WelcomePage(QWidget *parent = nullptr)` and `void refreshPage() override`. In this task it shows a single placeholder label.
  - In `appsettings.h`: `enum WelcomeCardColour { WCC_Plain = 0, WCC_ByGroup = 1, WCC_ByStatus = 2 };`, keys `STR_AS_UseNewWelcome = "UseNewWelcome"` (default `true`) and `STR_AS_WelcomeCardColour = "WelcomeCardColour"` (default `WCC_ByStatus`), getters `bool useNewWelcome() const`, `WelcomeCardColour welcomeCardColour() const`, setters `setUseNewWelcome(bool)`, `setWelcomeCardColour(WelcomeCardColour)`. Follow the `m_useFusionTheme` pattern.
  - `MainWindow::createWelcomePage()` returns `WelcomePageBase *` (new `WelcomePage` or `Welcome` per `useNewWelcome()`); both existing creation sites call it.
  - `MainWindow::rebuildWelcomePage()` replaces the Welcome tab in place (same index and title), keeps it current if it was current, and calls `refreshPage()`.

- [ ] **Step 1:** Create branch: `git -C C:/OSCAR/OSCAR-next switch -c welcome-pilot ui-next`.
- [ ] **Step 2:** Add the settings, base class, placeholder `WelcomePage`, and `createWelcomePage()`; change `MainWindow::welcome` to `WelcomePageBase *`.
- [ ] **Step 3:** In Preferences, next to the existing `useFusionTheme` checkbox, add checkbox `useNewWelcome` ("Use the new Welcome page") and combo `welcomeCardColour` ("Welcome card colour": "Plain", "By group", "By status", index = enum value). The combo is enabled only while the checkbox is checked. Load in the dialog's load code, save in its save code, following the `useFusionTheme` lines.
- [ ] **Step 4:** Where `MainWindow` opens Preferences, record both values before `exec()`; if the dialog is accepted and either changed, call `rebuildWelcomePage()`.
- [ ] **Step 5:** Build. Expected: compiles clean, no new warnings.
- [ ] **Step 6:** Run OSCAR. Expected: Welcome tab shows the placeholder. Untick the checkbox, OK: the old page appears at once, fully working (buttons, CPAP and oximetry text). Tick again: placeholder returns. Combo greys out while unticked. Settings survive a restart.
- [ ] **Step 7:** Commit (after asking): "Add switch between the old and new Welcome pages".

### Task 2: Theme module and bundled fonts

**Files:**
- Create: `oscar/uikit/theme.h`, `oscar/uikit/theme.cpp`, `oscar/tests/themetests.h`, `oscar/tests/themetests.cpp`
- Create (binary): `oscar/fonts/Lexend-Regular.ttf`, `oscar/fonts/Lexend-Medium.ttf`, `oscar/fonts/AtkinsonHyperlegible-Regular.ttf`, `oscar/fonts/AtkinsonHyperlegible-Bold.ttf`, `oscar/fonts/OFL-Lexend.txt`, `oscar/fonts/OFL-AtkinsonHyperlegible.txt`
- Modify: `oscar/Resources.qrc`, `oscar/oscar.pro`

**Interfaces:**
- Produces, in `namespace Theme`:
  - `enum class Role { Page, Surface, Surface2, Ink, Ink2, Ink3, Line, Accent, AccentInk, AccentSoft, OnAccent, Good, GoodSoft, GoodTint, GoodLine, Bad, BadSoft, BadTint, BadLine, Neutral, NeutralSoft, Warn, WarnSoft, WarnLine, GroupBreath, GroupBreathLine, GroupMask, GroupMaskLine, GroupUsage, GroupUsageLine, Shadow, Count };`
  - `QColor color(Role r);` light values copied exactly from the mockup `:root` (e.g. `Page` = `--bg` `#f2f5f8`, `Surface` `#ffffff`, `Accent` `#007acc`, `Good` `#17844a`, `Bad` `#c4372c`, `GroupBreath` `#f4f0fb`, `GroupBreathLine` `#dcd2f0`, …). `Shadow` = `rgba(19, 33, 48, 0.07)`.
  - `QString css(Role r);` → `#rrggbb` when opaque, else `rgba(r, g, b, a)` with `a` 0–255.
  - `enum class FontRole { Display, Body };` and `QFont font(FontRole role, qreal pixelSize, QFont::Weight weight = QFont::Normal);` (Display = Lexend, Body = Atkinson Hyperlegible, falling back to the application font family when not registered).
  - `void registerFonts();` loads the four TTFs from `:/fonts/` once (idempotent); `WelcomePage`'s constructor calls it.
  - Size constants: `constexpr int Radius = 12, SmallRadius = 8, PagePadding = 24, SectionGap = 22, CardGap = 14, TileGap = 12, CardPadding = 16;`
  - Event colours are **not** theme roles: they come from the channel colours Daily uses.

- [ ] **Step 1:** Write `ThemeTests` (`DECLARE_TEST(ThemeTests)`):
  - `testLightValues`: `Theme::color(Theme::Role::Page).name() == "#f2f5f8"`, `Accent` → `"#007acc"`, `Good` → `"#17844a"`, `Bad` → `"#c4372c"`, `GroupUsageLine` → `"#e9d8b3"`.
  - `testEveryRoleDefined`: for every role below `Count`, `color(role).isValid()`.
  - `testCss`: `css(Role::Accent) == "#007acc"`; `css(Role::Shadow) == "rgba(19, 33, 48, 18)"`.
- [ ] **Step 2:** Build the test target and run `.\test.exe`. Expected: ThemeTests fails to compile/link (no `Theme` yet).
- [ ] **Step 3:** Implement `theme.h/.cpp`.
- [ ] **Step 4:** Run `.\test.exe`. Expected: ThemeTests pass; exit code unchanged from baseline.
- [ ] **Step 5:** Fonts. **Ask the user before downloading.** Source: `github.com/google/fonts` (`ofl/lexend/` static instances and `ofl/atkinsonhyperlegible/`), or files the user supplies. Add the four TTFs and both OFL texts under `oscar/fonts/` and list the TTFs in `Resources.qrc`.
- [ ] **Step 6:** Build the app. In `WelcomePage`'s placeholder, set the label font to `Theme::font(FontRole::Display, 24, QFont::Medium)`. Expected: the placeholder renders in Lexend.
- [ ] **Step 7:** Commit (after asking): "Add theme module and bundle the redesign fonts".

### Task 3: Comparison and formatting logic

**Files:**
- Create: `oscar/uikit/comparison.h/.cpp`, `oscar/uikit/formatting.h/.cpp`, `oscar/tests/comparisontests.h/.cpp`
- Modify: `oscar/oscar.pro`

**Interfaces:**
- Produces, in `namespace UiKit`:
  - `enum class Better { Higher, Lower, Neutral };`
  - `enum class Trend { Good, Bad, Same, Neutral, None };` (`Neutral` = changed, but direction carries no meaning; `None` = no comparison possible)
  - `struct Comparison { Trend trend = Trend::None; int percent = 0; };`
  - `constexpr int NoChangeBand = 2;` (|rounded %| below this is "same")
  - `Comparison compare(double night, double average, int nightsWithData, Better better);`
  - `enum class StatusTint { None, Good, Bad };` and `StatusTint statusTint(const Comparison &c7, const Comparison &c30);` (Good only if both Good, Bad only if both Bad)
  - `QString deltaText(const Comparison &c);` → `"▲ 12%"`, `"▼ 5%"`, `"same"`, `"—"` (via `QObject::tr` for "same")
  - `QString formatHoursMinutes(double hours);` → `"6h 18m"` (`"0h 00m"` style: minutes two digits); `QString formatMinutesSeconds(double seconds);` → `"1m 12s"`, `"45s"` under a minute, `"1h 05m"` at an hour or more.

Rules for `compare`: `nightsWithData == 0` → `None`. `average <= 0`: night `<= 0` → `Same`, else `None`. Otherwise `percent = qRound((night - average) / average * 100)`; `|percent| < NoChangeBand` → `Same`; else `Better::Neutral` → `Neutral`; else `Good`/`Bad` by direction.

- [ ] **Step 1:** Write `ComparisonTests`:
  - `testLowerIsBetter`: `compare(3.65, 3.32, 7, Lower)` → `Bad`, percent 10; `compare(2.0, 3.0, 7, Lower)` → `Good`, percent -33.
  - `testHigherIsBetter`: `compare(6.30, 5.25, 7, Higher)` → `Good`, 20.
  - `testNeutral`: `compare(11.3, 10.9, 7, Neutral)` → `Neutral`, 4.
  - `testNoChangeBand`: `compare(3.33, 3.32, 7, Lower)` → `Same`; `compare(1.02, 1.0, 7, Lower)` → `Bad`, 2.
  - `testZeroAverage`: `compare(0, 0, 7, Lower)` → `Same`; `compare(72, 0, 7, Lower)` → `None`.
  - `testNoHistory`: `compare(3.0, 0, 0, Lower)` → `None`; `deltaText` of it → `"—"`.
  - `testStatusTint`: (Bad, Bad) → Bad; (Good, Good) → Good; (Good, Bad) → None; (Bad, Same) → None; (Neutral, Neutral) → None.
  - `testDeltaText`: `{Bad, 10}` → `"▲ 10%"`; `{Good, -33}` → `"▼ 33%"`; `{Same, 0}` → `"same"`.
  - `testFormatting`: `formatHoursMinutes(6.30)` → `"6h 18m"`; `formatHoursMinutes(0.5)` → `"0h 30m"`; `formatMinutesSeconds(72)` → `"1m 12s"`; `formatMinutesSeconds(45)` → `"45s"`; `formatMinutesSeconds(3900)` → `"1h 05m"`.
- [ ] **Step 2:** Run `.\test.exe`. Expected: ComparisonTests fail to build.
- [ ] **Step 3:** Implement `comparison.*` and `formatting.*`.
- [ ] **Step 4:** Run `.\test.exe`. Expected: all ComparisonTests pass.
- [ ] **Step 5:** Commit (after asking): "Add comparison and formatting helpers for the redesign".

### Task 4: Components

**Files:**
- Create: `oscar/uikit/sparkline.h/.cpp`, `oscar/uikit/metriccard.h/.cpp`, `oscar/uikit/actiontile.h/.cpp`, `oscar/uikit/eventbar.h/.cpp`, `oscar/uikit/sectionheader.h/.cpp`, `oscar/tests/sparklinetests.h/.cpp`
- Create (binary): `oscar/icons/welcome/{import,oximetry,daily,overview,statistics,insights}.png` and `@2x` variants
- Modify: `oscar/Resources.qrc`, `oscar/oscar.pro`

**Interfaces:**
- Consumes: `Theme::*` (Task 2), `UiKit::Trend`, `UiKit::StatusTint`, `UiKit::deltaText` (Task 3).
- Produces, in `namespace UiKit`:
  - `class Sparkline : public QWidget` — `void setData(const QVector<double> &values, double reference, QColor dotColour);` `values` oldest → newest, `NaN` = no data (gap in the line). Static `QVector<QPointF> mapPoints(const QVector<double> &values, double reference, QSizeF size, double pad);` returns one point per value (NaN entries map to NaN points); x evenly spaced across the width; y scaled between min and max of the non-NaN values and `reference`; if that range is ~0, widen it by ±1. Paints: area under the line in `AccentSoft`, line in `Accent` (1.6 px), dashed `reference` line in `Ink3`, dot on the last value in `dotColour`. Fixed height 38 px.
  - `class EventBar : public QWidget` — `struct Part { QColor colour; int count; QString label; };` `void setParts(const QVector<Part> &parts);` Draws the proportional bar plus a legend line ("OA 12", …).
  - `class MetricCard : public QFrame` — `enum class Tint { None, Good, Bad, Breathing, Mask, Usage };` setters `setTitle(QString)`, `setValue(QString value, QString unit)`, `setSubtitle(QString)`, `setComparisons(QString label7, Comparison c7, QString label30, Comparison c30)`, `setSparkline(QVector<double>, double reference, QColor dot)` (hidden when not called), `setSparklineCaption(QString left, QString middle, QString right)`, `setExtraWidget(QWidget *)` (under the subtitle, e.g. an `EventBar`), `setTint(Tint)`, `setShowComparisons(bool)`. Delta text colour: Good → `Good`, Bad → `Bad`, otherwise `Neutral`. Comparison boxes use `Surface2` when untinted and `Surface` when tinted (mockup v6b rule). Tints: Good → `GoodTint`/`GoodLine`, Bad → `BadTint`/`BadLine`, groups → `Group*`/`Group*Line`. Shadow via `QGraphicsDropShadowEffect` (blur 18, offset 0,6, colour `Shadow`).
  - `class ActionTile : public QAbstractButton` — `ActionTile(QString iconPath, QString title, QString subtitle, QWidget *parent = nullptr)`, `setPrimary(bool)`, `setTag(QString)` (small pill, e.g. "Coming later"). Icon above text. Visible hover, pressed and disabled states. The icon PNG is a mask, tinted at paint time with `Accent` (or `OnAccent` when primary, `Ink3` when disabled).
  - `class SectionHeader : public QLabel` — small uppercase label in `Ink3`, Body font 12 px bold, letter spacing as the mockup.
- Icons: white-on-transparent PNG masks at 32 px and 64 px (`@2x`), produced from the line-icon SVGs in `Welcome_Concept.html`. Ask the user before installing any conversion tool.

- [ ] **Step 1:** Write `SparklineTests`:
  - `testEvenSpacing`: 3 values, size 200×38, pad 4 → x = 0, 100, 200.
  - `testScale`: values {1, 3}, reference 2 → y(1) = 34, y(3) = 4.
  - `testFlatSeries`: values {5, 5}, reference 5 → both y = 19 (range widened, no division by zero).
  - `testGaps`: values {1, NaN, 3} → middle point is NaN; the others as in `testScale`.
- [ ] **Step 2:** Run `.\test.exe`. Expected: SparklineTests fail to build.
- [ ] **Step 3:** Implement `Sparkline` (including `mapPoints`), then the other components.
- [ ] **Step 4:** Run `.\test.exe`. Expected: all tests pass.
- [ ] **Step 5:** Temporarily put one of each component on the placeholder page with sample values from the mockup (AHI 3.65, 7d 3.32, 30d 3.04). Build and compare side by side with `Welcome_Concept.html`. Expected: same colours, radii, fonts and spacing; hover and press visibly change the tile. Remove the sample before committing.
- [ ] **Step 6:** Commit (after asking): "Add reusable card, tile and sparkline components".

### Task 5: Card data

**Files:**
- Create: `oscar/welcomemetrics.h/.cpp`
- Modify: `oscar/oscar.pro`

**Interfaces:**
- Consumes: `UiKit::Better`, `UiKit::formatHoursMinutes`, `UiKit::formatMinutesSeconds` (Task 3); `calcAHI(QDate, QDate)` from `statistics.cpp` (declare `extern` as `welcome.cpp` does).
- Produces:
  - `enum class MetricId { TimeUsed, Ahi, Events, Pressure, Leak, LargeLeak, FlowLimit, Sessions };`
  - `enum class MetricGroup { Usage, Breathing, Mask };`
  - `struct MetricData { MetricId id; MetricGroup group; UiKit::Better better; QString title, value, unit, sub; double night = 0; double avg7 = 0, avg30 = 0; int nights7 = 0, nights30 = 0; QVector<double> series; QVector<UiKit::EventBar::Part> events; };` — `series` has 31 entries, night − 30 … night, `NaN` where there is no CPAP data.
  - `struct NightSummary { bool valid = false; QDate date; QString deviceName; QString pixmapPath; int daysAgo = 0; };`
  - `struct OximetrySummary { bool present = false; double spo2Avg = 0, spo2Min = 0, pulseAvg = 0; };`
  - `class WelcomeMetrics` with static `bool hasAnyData()`, `NightSummary lastNight()`, `QVector<MetricData> cards(QDate night)` (only cards the device records, in table order), `OximetrySummary oximetry(QDate night)`.

Definitions (night = the profile's last CPAP day, `p_profile->LastDay(MT_CPAP)`, as `Welcome::GenerateCPAPHTML()`; "available" uses the CPAP `Machine::hasChannel()`, not `Day::channelHasData()`):

| Card | Group, better | Night value (compared) | Shown text | Average over nights before |
|---|---|---|---|---|
| Time used | Usage, Higher | `day->hours(MT_CPAP)` | `formatHoursMinutes`; sub = first session start – last session end (locale short time) | mean of nightly hours over nights with data |
| AHI (RDI when `p_profile->general->calculateRDI()`) | Breathing, Lower | `day->calcAHI()` / `calcRDI()` | 2 decimals, "events/hr" | `calcAHI(start, end)` (already follows the RDI preference) |
| Events | Breathing, Lower | `count(AllAhiChannels)` (+ `CPAP_RERA` when RDI) | integer, "total"; `EventBar` parts = each channel in `ahiChannels` (+ RERA) with count > 0, label = channel label, colour = the colour Daily draws that channel's flags with | mean nightly total |
| Pressure | Mask, Neutral | CPAP: setting; APAP: percentile; bi-level/ASV/AVAPS/tri-level: IPAP percentile | same mode rules and wording as `GenerateCPAPHTML()`; bi-level sub adds "Averages compare IPAP." | mean of nightly compared value |
| Leak rate | Mask, Lower | `day->wavg(CPAP_Leak)` | 2 decimals + units; sub "Average · <percentile label> <`day->calcPercentile(CPAP_Leak)`>" | `p_profile->calcWavg(CPAP_Leak, MT_CPAP, start, end)` (as today's page) |
| Large leaks | Mask, Lower | `day->calcPON(CPAP_LargeLeak)` (% of night, as Statistics) | `formatMinutesSeconds(day->sum(CPAP_LargeLeak))`; sub "N episodes · x% of the night" | mean nightly % |
| Flow limitation | Breathing, Lower | `CPAP_FlowLimit` available: `day->calcIdx(CPAP_FlowLimit)`; else `CPAP_FLG` available: `day->calcPercentile(CPAP_FLG)` (both as Statistics) | 2 decimals; sub names which measure | mean of nightly value |
| Sessions | Usage, Neutral | number of enabled CPAP sessions | integer, "mask on/off"; sub "Longest <formatHoursMinutes>" | mean nightly count |

7-day window = night − 7 … night − 1; 30-day = night − 30 … night − 1. `nights7`/`nights30` count nights in the window with CPAP data.

Oximetry: present when the night has SpO₂ or pulse data from an oximeter day for that date or from the CPAP day's own channels (same channel lookup as `Welcome::GenerateOxiHTML()`). Values are the ones Daily's oximetry summary shows for that night.

- [ ] **Step 1:** Implement `welcomemetrics.*`.
- [ ] **Step 2:** Build. Expected: clean.
- [ ] **Step 3:** Temporarily dump `cards(lastNight().date)` with `qDebug()` from `WelcomePage::refreshPage()`. Open the test profile in `C:/Users/Guy/Documents/OSCAR21_Data/oscar.db`. Expected: every night value equals what Daily shows for that night, and each 7-day AHI/leak average equals today's old Welcome page text. Repeat on a bi-level or ASV profile and on one without flow-limitation or large-leak data (those cards absent). Remove the dump.
- [ ] **Step 4:** Commit (after asking): "Add the Welcome card data layer".

### Task 6: Assemble the new Welcome page

**Files:**
- Modify: `oscar/welcomepage.h/.cpp`

**Interfaces:**
- Consumes: everything above; `AppSetting->welcomeCardColour()`; `mainwin->JumpImport()`, `JumpOxiWizard()`, `JumpDaily()`, `JumpOverview()`, `JumpStatistics()`, `EnableTabs(bool)` exactly as `welcome.cpp` calls them (Daily/Overview/Statistics via `QTimer::singleShot(0, …)`).

Layout, top to bottom, inside a `QScrollArea` on `Page` background with `PagePadding`:
1. Header: `:/icons/logo-lg.png` at 48 px high, title "Welcome to the Open Source CPAP Analysis Reporter" in Display 24 px Medium.
2. Tiles: Import CPAP data (primary), Oximetry wizard, Daily view, Overview, Statistics, Insights (disabled, tag "Coming later"). Subtitles as in the mockup. Columns by page width: ≥ 1100 px → 6, ≥ 700 → 3, else 2.
3. Night strip (when data): device image from `machine->getPixmapPath()`, brand + model, long date (`QLocale::system()`), and the same "today / last night / N days ago" rule as `GenerateCPAPHTML()`. Keep the S9 card warning with the same condition as `welcome.cpp`.
4. Group legend (only in By group).
5. Cards grid: one `MetricCard` per `WelcomeMetrics::cards()`, then the oximetry card when present (neutral, `setShowComparisons(false)`, no sparkline). Columns: ≥ 1000 px → 4, ≥ 560 → 2, else 1. Tint per setting: Plain → `None`; By group → group tint; By status → `statusTint(c7, c30)` (Pressure and Sessions never tinted). Sparkline dot colour from the 30-day trend (Good/Bad/else Accent). Caption: first date, "dashed = 30-day avg", night date.
6. First run (no data): the three-step panel from the mockup instead of 3–5, including today's text about checking Preferences first, ResMed-forced preferences, and the first import taking a few minutes.

`refreshPage()` rebuilds the dynamic parts, enables/disables tiles as `Welcome::refreshPage()` does, and calls `mainwin->EnableTabs()` the same way.

- [ ] **Step 1:** Implement the page.
- [ ] **Step 2:** Build. Expected: clean.
- [ ] **Step 3:** Compare with `Welcome_Concept.html` at the same window width. Expected: matching layout, spacing, colours and fonts in all three colour settings.
- [ ] **Step 4:** Check the Review Focus items: a profile with a gap of several nights before the last night; a new empty profile (first-run panel, tiles disabled); RDI preference on (title and numbers match Daily); window narrowed to ~500 px and widened to full screen; Windows display scaling 150%; switch profile, import, and Rebuild CPAP data while the page shows. Expected: correct values, no "inf"/"nan", no clipping, no crash.
- [ ] **Step 5:** Check that each tile opens the same thing as the old page's buttons, and that the pressed state shows on click.
- [ ] **Step 6:** Commit (after asking): "Build the redesigned Welcome page".

### Task 7: Hand-off

- [ ] **Step 1:** In `Notes/UI Redesign/UI_REDESIGN_PLANNING.md`, add a short "Pilot status" line (what's built, where), and list anything left for testers to judge.
- [ ] **Step 2:** Ask the user to test on macOS and Linux (macOS painting speed with card shadows, #235). If shadows are slow, replace `QGraphicsDropShadowEffect` in `MetricCard` with painted shadows.
- [ ] **Step 3:** With the user's OK: merge `welcome-pilot` into `ui-next` (`git merge --no-ff`), push `ui-next`.
