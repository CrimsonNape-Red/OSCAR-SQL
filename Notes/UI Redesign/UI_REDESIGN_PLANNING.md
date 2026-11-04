# UI Redesign — Planning Notes

Initial planning discussion (2026-10-12) for replacing the Welcome page with a more modern
design, possibly the first step of a wider UI modernization. Branch workflow revised
2026-10-23, once the scope grew to several pages and possibly the overall structure. Branch
`ui-next` created 2026-10-31 from the 2.1.1 release commit. No redesign code yet.

---

## 1. Keeping the redesign separate from regular development

Master stays the **release line**. The redesign lives on one long-lived **integration branch**
(called `ui-next` here), checked out in its own **git worktree** (a second working folder that
shares the same repository).

### Two lines of development

- **Fixes and minor changes** to the current release are made on master and pushed as usual.
  That workflow doesn't change.
- **Redesign work:** each page or structural change gets a short branch off `ui-next` and merges
  back into `ui-next` when done.
- **Keeping `ui-next` current:** merge master into it regularly (weekly, or after each batch of
  fixes). Small, frequent merges keep conflicts manageable, and every fix reaches the redesign.
- **Release:** `ui-next` merges into master once, when the redesign as a whole is ready.
  Redesigned pages aren't merged to master one at a time, because a half-redesigned release
  would confuse users if the overall structure is changing too.

### Rules that keep the two lines from colliding

1. **Fixes always go on master, then merge forward.** Never fix on `ui-next` and copy the fix
   back to master. The exception is a bug in code that only exists on `ui-next`.
2. **Schema changes happen on master only.** If both lines raised the schema version, two
   different migrations with the same version number could exist. Under the no-data-loss
   migration policy that would be painful to untangle at release. If the redesign needs a schema
   change, land it on master first and merge it forward.
3. **Run lupdate on master only.** `.ts` files merge badly. On `ui-next`, run lupdate once,
   just before the final merge.
4. **Expect conflicts in `mainwindow.cpp`/`.ui` and `welcome.*`.** Master fixes in those files
   will conflict with the redesign. Frequent merges keep each conflict small.

### Fork or branch in origin

**Decided 2026-10-31: branch `ui-next` in origin**, checked out at `c:\OSCAR\OSCAR-next`. The
comparison below is kept for reference; a fork can still be made later if other developers start
committing redesign code.

The workflow above is the same either way; the deciding question is who pushes code.

- **Branch in origin:** simplest if the work is mainly one developer, with others testing
  builds. The repository is public, so anyone can check it out and build it.
- **Fork:** better if other developers will commit redesign code. They get write access to the
  fork without access to the main repository, and redesign issues and merge requests stay out of
  the main tracker. Costs: syncing master into the fork is a manual step, and the local worktree
  pushes to a second remote.
- Before using a branch in origin, check whether the CrimsonNape distribution mirror copies every
  branch or only protected ones.

### Worktree

- `ui-next` is checked out in `c:\OSCAR\OSCAR-next`. `c:\OSCAR\OSCAR-code`
  stays on master and never switches branches.
- Open the worktree as its own QtCreator project with its own build directory. Switching
  branches in one folder changes file timestamps and causes large rebuilds; separate folders
  avoid that, and each keeps its incremental build.
- Push the branch (to origin or the fork) as a backup.

### Keep the change easy to merge or throw away

The current Welcome page is already self-contained: `welcome.cpp/.h/.ui`, created at one place
(`mainwindow.cpp`, `new Welcome(ui->tabWidget)`) and refreshed through `refreshPage()`.

- Build the new page in **new files** (e.g. `welcome2.*`) with the same interface.
- The only edits to shared files are then about two lines in `mainwindow.cpp` and the entries in
  `oscar.pro`. Merge conflicts with master stay small, and the experiment can be dropped cleanly.
- Optional: a temporary switch to choose between the old and new page for side-by-side comparison.

### Keep the bookkeeping separate

- Put design notes and the progress log on the branch only (this folder). Don't add experiment
  entries to `Notes/Developer Notes/BUG_FIXES.md`, because that file changes constantly on
  master and would conflict.
- In GitLab, tag related issues with a label such as `ui-redesign`.
- Merge master into `ui-next` rather than rebasing, since the branch is pushed.

### Test builds identify themselves

`update_gitinfo.bat/.sh` already puts the branch name into the version string
(`version.cpp`), so builds handed to testers will show `ui-next` and won't be mistaken for
master builds.

### Claude sessions

- Run redesign sessions from the worktree folder. Claude's project memory is stored per folder,
  so that worktree gets its own memory. Existing OSCAR memories won't load there automatically;
  the useful ones can be copied over.
- The `build-oscar` skill builds in QtCreator's *active* build directory, so whichever project is
  open in QtCreator decides what gets built. Either keep the right one active or adjust the skill
  to take a target folder.

---

## 2. Working from rough design guidance

Claude can turn screenshots of liked design elements and photos of pencil sketches into a
polished layout: spacing, type sizes, color palettes, cards and visual hierarchy. The sketch
decides what goes where; Claude handles the polish.

**Strengths:** turning a rough layout into a consistent visual system, borrowing a style from
reference screenshots, offering several variations to choose from, handling light and dark
themes.

**Weaker areas:** original artwork such as illustrations or custom icons (use existing icon sets
or OSCAR's current icons). Claude can't see the running app, so checking that the Qt build
matches the mockup needs screenshots.

### Proposed process

1. Send screenshots of what you like (and what you dislike, which helps just as much) plus a
   sketch.
2. Claude makes two or three **HTML mockups** to view in a browser. Changing a mockup takes
   minutes; changing Qt code means rebuilding every time.
3. Mark them up or describe changes, and repeat until it looks right.
4. Then build it in Qt.

The mockups stay within what Qt widgets can actually reproduce (see section 3), so the design
doesn't depend on effects we can't build. Before the first mockup, read `welcome.cpp` to see how
the current page is drawn.

---

## 3. What Qt can do visually

Colors and gradients are easy; soft shadows take a bit more work.

| Effect | How in Qt Widgets | Effort |
|---|---|---|
| Colors, transparency | Stylesheets (Qt's version of CSS), including semi-transparent colors | Easy |
| Gradients | Built into stylesheets (linear, radial and conical) | Easy |
| Rounded corners, borders | Stylesheets | Easy |
| Hover/pressed states | Stylesheets | Easy |
| Custom fonts | Can be bundled with the app, so every platform looks the same | Easy |
| **Soft shadows** | No stylesheet support. Qt's built-in drop-shadow effect, or the shadow is drawn in code | Moderate |
| Animation | Qt's animation classes (fades, slides, numbers counting up) | Moderate |

**Shadows.** Qt's built-in drop-shadow effect looks good but costs some performance: fine for a
handful of cards, not for dozens. Mac painting has been slow before (#235), so test there early.
If it turns out slow, draw the shadow in code instead, which is fast but more work. A design with
4–8 shadowed cards is no problem.

**Animation.** Qt has no automatic CSS-style transitions, but its animation classes work well for
small touches like a card fading in on hover or a number counting up. Easy to add later if the
page is structured for it, so leaving it out for now is fine.

**Dark mode.** Detecting the system's light/dark setting needs Qt 6.5, and some testers build
with older Qt, so a fallback is needed, such as a setting in Preferences.

### Alternatives to avoid

- **QML/Qt Quick:** richer visuals, with shadows and animations built in, but it would bring a new
  UI technology and new Qt modules into all ~25 build environments. A big cost for one page.
- **An embedded web page:** OSCAR has already moved away from WebKit (see the "kill off webkit"
  comment in `mainwindow.cpp`), and Qt's web engine is very large.

**Recommendation:** regular Qt widgets with stylesheets, plus a few widgets drawn in code where a
shadow or chart needs it.

---

## 4. If this becomes a full UI redesign

Treat the Welcome page as the **pilot for a shared design system**, not a one-off page, so most
of the work carries over to later screens.

### What to set up from the start

1. **A central theme module.** One place defines colors, spacing, corner radii and font sizes,
   and the Welcome page uses only those values. The graph widget (`gGraphView`) paints itself
   rather than using stylesheets, so the theme has to be readable from C++, not only from
   stylesheets.
2. **Reusable building blocks.** Cards, stat tiles, section headers and buttons go in their own
   files, not inside the Welcome page code, so the next screen can reuse them.
3. **A written style guide in `Notes/`** (palette, spacing rules, component list), to keep the
   look consistent across many sessions and screens.

### What the current code looks like

- **Base style:** OSCAR uses each platform's native style by default, with Fusion as an option
  (`main.cpp`, `useFusionTheme()`). Native styles look different on Windows, Mac and Linux. A
  consistent modern look usually means Fusion everywhere plus an app-wide stylesheet. That's an
  early decision for the redesign, though not for the Welcome pilot.
- **Scattered styling:** there are about 50 `setStyle`/`setStyleSheet` calls across 16 files.
  They can override or clash with an app-wide theme, so moving them into the theme module belongs
  to the larger redesign, not this first step.
- **Dark mode:** a comment in `main.cpp` says it "would require an exhaustive change to OSCAR". A
  central theme is what would make dark mode practical later.

### Order of work

- Build the theme module and the new Welcome page first, as the pilot, on a short branch off
  `ui-next`.
- Later screens build on that foundation, each on its own short branch merged into `ui-next`
  (see section 1). Nothing reaches master until the whole redesign is released.

### Pilot status

- 2026-10-31: first Qt build of the Welcome pilot on branch `welcome-pilot` (off `ui-next`),
  following `WELCOME_PILOT_DESIGN.md` and `WELCOME_PILOT_PLAN.md`. Switch in Preferences →
  Appearance: "Use the new Welcome page" and "Welcome card colour".
- Reusable pieces are in `oscar/uikit/` (theme, cards, tiles, icon button, event bar, icons,
  panels, labels); card data and the card table are in `oscar/welcomemetrics.*`.
- 2026-11-03: cards can be shown or hidden per profile from a gear button beside the legend.
- 2026-11-03: the theme now follows the Oximetry Wizard: colours from the system palette
  (so a dark palette works), the wizard's good and warning colours, 10 px corners, and the
  application font instead of bundled fonts.
- 2026-11-03: device pictures are trimmed in code (empty margins removed when shown, files
  unchanged) and the night strip's picture box is 76 x 52 px, so the text sets its height.
- For testers to judge: is the new page better than the old one, and which card colouring
  (Plain, By group, By status) works best.
- Still to check: macOS painting speed with the card shadows (#235), Linux, HiDPI, and
  long translated strings.

---

## Open decisions

- Native style vs. Fusion as the base for the redesigned UI.
- Whether dark mode is a goal, and if so, the fallback for Qt < 6.5. Since 2026-11-03,
  `ui-next` no longer forces the light colour scheme (`main.cpp`), so OSCAR follows the
  system setting while the redesign is in progress. **Before release:** if dark mode isn't
  fully working, put the forced light scheme back.
- Whether animation is wanted (can be deferred).
