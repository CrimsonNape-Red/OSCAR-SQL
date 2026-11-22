# Reorganizing Notes and private developer material — design

Date: 2026-11-21. Status: carried out 2026-11-22.

One change from the text below: `DevPrivate` is a plain folder inside the OSCAR-code working
folder, listed in `.gitignore`, not a link to a shared folder. How it is shared between
developers is undecided.

## Intent

A user who opens the OSCAR repository should find the few things meant for them without wading
through developer working notes. Developers and contributors should find notes by subject.
Material that must not be public (anonymizers, tester data) needs a defined home outside the
repository.

## Goals

- One clearly named top-level folder for user-facing material.
- `Notes/` sorted by subject, replacing the 60-file `Developer Notes` grab bag.
- A private shared folder for anonymizers and related notes, linked into each clone and
  ignored by git.
- Every reference to a moved file still points somewhere valid afterwards.

## Non-goals

- No change to program behaviour. Source edits are comment-only.
- No rewriting of note content, and no deleting of notes (superseded ones are archived).
- No anonymizing or reorganizing of the sample data itself.
- No change to `Building/`, `Translations/`, `Htmldocs/`, `dumpSTR/`, `anotDump/`, `history/`.
- No paid service.

## Anti-goals

- An anonymizer, a checksum algorithm, or anything identifying a tester ending up in the
  repository.
- A broken build on any platform because a script the build calls was moved.

## Constraints

- The build runs `Tools/gen_system_report_strings.py` from `oscar/oscar.pro`, so `Tools/`
  stays where it is.
- Ten loader source files cite 15 notes by path in comments.
- `AGENTS.md` and the `shipfix` skill name `Notes/Developer Notes/BUG_FIXES.md` and
  `Notes/database/...`. `AGENTS.md` is not tracked, so each developer updates their own copy.
- The `user-guide` branch adds a top-level `UserGuide/` folder; the new user folder must not be
  confused with it.
- No open branch currently changes anything under `Notes/` or `Tools/`, so the move will not
  collide with work in progress.
- The project has no budget.

## Approaches considered

| | Approach | Trade-off |
|---|---|---|
| A | Keep `Notes/` paths, sort only `Developer Notes`, add a user folder | least churn; names stay uneven |
| B | Rename `Notes/` into subject folders (`Formats/`, `Design/`, ...) | clean names; comment edits in ten source files; old GitLab links into `Notes/` break |
| C | Conventional `docs/user` and `docs/dev` | familiar to outsiders; most churn; `/docs/` is ignored today |

**Chosen: B**, by the lead developer on 2026-11-21.

An earlier idea, moving all of `Notes/` and `Tools/` to a shared Dropbox folder, was dropped:
hiding the format analysis is no longer a goal, source comments cite the notes, and contributors
need them.

## Repository layout

```
OSCAR-code/
├─ UserResources/          for users
│   ├─ Getting Started/    Getting Started with OSCAR 2.0.md and its images
│   ├─ Usage/              Time Corrections, Profile Restore Constraints
│   ├─ Data Access/        Python library notes, CSV export script and notes, waveform demo
│   └─ Sharing Data/       today's Cloud Services
├─ Notes/                  for developers; README.md says so and indexes the folders
│   ├─ Guides/             how OSCAR works
│   ├─ Database/           unchanged
│   ├─ Formats/            one folder per manufacturer, plus _General/ and Oximeters/
│   ├─ Design/             specs and plans; UI/ and Archive/ inside
│   ├─ Investigations/     dated analyses and code reviews
│   ├─ Process/            BUG_FIXES.md, Git how-tos, maintenance, AI aids
│   ├─ Wiki/               unchanged
│   └─ temp/               unchanged, ignored
├─ Tools/                  unchanged, flat
└─ DevPrivate/             link to the private shared folder; in .gitignore
```

## Where today's folders go

| Today | Goes to |
|---|---|
| `Notes/Getting Started with OSCAR 2.0.md`, `Notes/images/` | `UserResources/Getting Started/` |
| `Notes/OSCAR Usage/` | `UserResources/Usage/` |
| `Notes/Accessing OSCAR Data/`, `Notes/Database/oscar_details_csv.py` | `UserResources/Data Access/` |
| `Notes/Waveform Demo/` | dropped if a diff confirms it is the older copy of the two files in `Accessing OSCAR Data`; otherwise the differences are reported first |
| `Notes/Cloud Services/` | `UserResources/Sharing Data/` |
| `Notes/Database/` (the rest) | unchanged |
| `Notes/loaders/**` | `Notes/Formats/<Manufacturer>/` |
| `Notes/G3X/` | format doc to `Notes/Formats/BMC/G3X/`; the dated import design to `Notes/Design/` |
| `Notes/specs/`, `Notes/plans/` | `Notes/Design/` |
| `Notes/UI Redesign/` | `Notes/Design/UI/` |
| `Notes/Git/`, `Notes/Maintenance/`, `Notes/AI Aids/` | `Notes/Process/` |
| `Notes/Maintenance/import_bugs_to_gitlab.py` | `Tools/` |
| `Notes/Developer Notes/` | split by the rules below; the folder disappears |
| `Notes/Wiki/`, `Notes/temp/`, `Tools/` | unchanged |

`Formats/` folders: `_General` (card fingerprints, detection patterns), `BMC/G3X`, `BMC/Luna`,
`Sefam`, `Apex`, `Lowenstein` (prisma VENT, Weinmann), `Aeonmed`, `Lepu`, `Oximeters` (Viatom,
Contec, EMAY, CSV). A loader's own design and test notes stay with its manufacturer. Grammars
and plot scripts stay beside the format doc they belong to. The two empty folders
`loaders/ResMed` and `loaders/PRS1` are not carried over.

## Sorting rules for `Developer Notes`

| Kind of file | Goes to | Examples |
|---|---|---|
| Explains how a part of OSCAR works today | `Guides/` | architecture, new-developer guide, signals and slots, instrumentation, the legacy file-use notes |
| Describes a machine's data format | `Formats/<Manufacturer>/` | the `.001` events format notes, the Weinmann loader notes |
| Proposes or plans a feature | `Design/` | dark mode, portable mode, sharing, database menu, backup/restore, migration |
| Superseded version of a design | `Design/Archive/` | the `_v1` database menu design |
| One-off analysis, review or measurement | `Investigations/` | leak percentile review, profile-open timing and its script, time-alignment reviews |
| SQL query | `Database/` | the pulse P95 query |
| Running log or how-to for the team | `Process/` | `BUG_FIXES.md`, the "getting updates" how-to |

One file in `Developer Notes` has a person's name in its title; it is renamed during the move.

## Private material

| Tier | Contents | Where |
|---|---|---|
| Shared | all anonymizers (every manufacturer), notes on checksum algorithms, the sample manifest, working drafts | a small private shared folder (a few MB), linked as `OSCAR-code/DevPrivate` |
| Data | anonymized samples | each developer's own disk in the same layout; backed up to an external drive; single samples sent on request |
| Local only | the map from sample IDs to testers, raw tester data | the developer's machine, never shared |

```
DevPrivate/
├─ README.md       what this is, and how to link it into a clone
├─ Anonymizers/    one folder per manufacturer
├─ Checksums/      notes on how each manufacturer's checksums are computed
├─ Samples/        MANIFEST only: ID, model, firmware, what it shows, related issue
└─ Drafts/         notes not ready to publish
```

Public format docs may say which checksum files exist and how big they are, never how to
compute them.

## References to update

- Comments in the ten loader source files that cite notes by path.
- `.claude/skills/shipfix.md` and each developer's `AGENTS.md`.
- Cross-references between notes (about 45 notes mention a `Notes/` path).
- `.gitignore`: add `DevPrivate`.
- Links from GitLab issues or forum posts into `Notes/` on master will break. Accepted.

## How it is carried out

- The first step is a full old-path to new-path table for review. Nothing moves before that
  table is approved.
- Moves are done with `git mv` in a commit that changes no file content, so history follows
  each file. Reference fixes go in a second commit.
- OSCAR is rebuilt after the comment edits to confirm a clean compile.

## Calls made by Claude (open to correction)

- The developer folder keeps the name `Notes/`.
- The user folder is named `UserResources/`, to avoid confusion with the coming `UserGuide/`.
  Getting Started and the usage notes are candidates to fold into the User Guide later.
- The private folder is named `DevPrivate`.
- `Tools/` stays flat and in place, because the build calls a script there.
- `Notes/Wiki/` stays under `Notes/`: it is a maintainer's mirror in wiki markup, not something
  a user would read from the repository.
- `Notes/Database/` stays under `Notes/`; `UserResources/Data Access/` links to it.
- `Notes/temp/` keeps its name.

## Left to the builder

- The file-by-file table, made by opening each file whose title does not settle its category.
- Which superseded designs go to `Design/Archive/`.
- The wording of `Notes/README.md` and `DevPrivate/README.md`.
- The command each platform uses to create the `DevPrivate` link.
