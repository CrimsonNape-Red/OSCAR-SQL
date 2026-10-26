# Portable Mode (Windows ZIP distribution)

GitLab #328. Run OSCAR from a USB stick or SD card without installing it and without
writing settings to the host computer's registry.

## Switching it on

Portable mode is on when either:

- a marker file `portable.ini` sits next to `OSCAR.exe` (the normal case: the portable ZIP
  ships it), or
- OSCAR is started with `--portable` (no marker needed; restarts pass it on).

Deleting the marker returns that copy of OSCAR to normal (registry + Documents) behaviour.
The marker's only setting is `DataFolder` (below); everything else in it is comments.

## Folder layout

Everything the user keeps is in a folder beside the program folder, so an upgrade is
"delete `OSCAR Portable`, put the new one in its place":

```
E:\
├── OSCAR Portable\             program folder (any name, any drive); replaced on upgrade
│   ├── OSCAR.exe, Qt DLLs, plugins, Help\, Html\, Translations\   (= deploy's Release\)
│   └── portable.ini            marker; DataFolder=..\OSCAR Portable Data
├── README-Portable.txt         at the top of the ZIP, so it is seen on opening the ZIP
└── OSCAR Portable Data\        portable data folder; never in the ZIP; must be writable
    ├── Settings\OSCAR_Team\OSCAR 2.0.ini    (development builds: branch name added)
    └── OSCAR20_Data\                        default data folder
```

The data folder name comes from `getModifiedAppData()`, so test builds use the usual
suffix (e.g. `OSCAR20_Data-test`), as they do in Documents.

### DataFolder

`portable.ini` is read as plain text (`dataFolderSetting()`), not through `QSettings`, which
would treat the backslashes of a Windows path as escapes. A `DataFolder=` line outside a
comment gives the portable data folder: relative to the program folder unless absolute,
quotes allowed. Missing or empty means `DefaultDataFolder` (`../OSCAR Portable Data`).
`resolveDataRoot()` stops ".." at a drive root, so a program folder at the top of a drive
keeps its data folder inside it.

### "Extract All" warning

Windows' "Extract All" proposes a destination named after the ZIP, so the program folder
ends up in e.g. `E:\OSCAR-2.1.0-Win64-Portable\OSCAR Portable`, and the data folder beside
it would be inside that version-named folder, where the next version, extracted the same
way, would not find it. On first use (the portable data folder did not exist at start-up),
with the default data folder, and when the program folder's parent is not a drive root and
its name starts with `OSCAR-` and contains `-Portable` (`zipNamedParent()`), `main()` shows
a warning with **Quit** (default) and **Continue Here**. Quit removes the still-empty
Settings and data folders (`removeUnusedFolders()`). Batch mode only logs the warning.

## Implementation

Module: `oscar/SleepLib/portablemode.{h,cpp}` (namespace `PortableMode`).

### Settings redirection

Every OSCAR `QSettings` is default-constructed, and a default-constructed `QSettings` uses
`QSettings::defaultFormat()`. `PortableMode::initialize()` therefore only needs:

```cpp
QSettings::setDefaultFormat(QSettings::IniFormat);
QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,   dataRoot + "/Settings");
QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, dataRoot + "/Settings");
```

SystemScope is redirected as well so QSettings' fall-back reads never pick anything up from
the host. The marker is not reused as the settings file because `setPath()` always names the
file `<path>/<organization>/<application>.ini`.

The one explicit `NativeFormat` use, the read-only `MachineGuid` lookup in
`getFingerprint()`, is deliberately left alone: a new PC still gets its OpenGL test.

### Qt's own registry key

`QFileDialog` saves its last folder, history and view mode with
`QSettings(QSettings::UserScope, "QtProject")`, under `FileDialog` in
`HKEY_CURRENT_USER\Software\QtProject\OrganizationDefaults`. That constructor is hardcoded
to `NativeFormat` (Qt 6.10.2 and Qt's dev branch alike), so `setDefaultFormat()` cannot
redirect it. Instead, `initialize()` notes whether `HKCU\Software\QtProject` exists, and
`removeQtRegistryKey()` deletes the whole key if it did not. A key that already existed
may belong to another Qt program, so it is left alone, including any FileDialog values OSCAR
changed in it. `main()` calls it through a scope guard on every way out, and the three
in-app restarts (`RestartApplication()`, `switchToDatabase()`, Compress Database) call it
before spawning. The new process checks for the key before its `-p` delay, so if the old
process still held the key the new process would leave its own copy behind. The removal is
silent if the registry cannot be written. If OSCAR crashes, the key stays, and later runs
then treat it as pre-existing. Not done on macOS or Linux (portable is Windows-only).

### Ordering constraint

`main()` reads the graphics engine from `QSettings` before constructing `QApplication`
(engine attributes must be set first), so `initialize()` runs immediately after the
organization/application names are set and before the first `QSettings`.
`QCoreApplication::applicationDirPath()` is not available then, so the executable folder is
worked out from `argv[0]`: a full path from Explorer or a shortcut; a bare name is looked up
in the current folder, then `PATH`. After `QApplication` exists, `main()` logs a warning if
that folder differs from `applicationDirPath()`.

### Writable check

`initialize()` creates `<data folder>\Settings\` and writes a probe file (Qt's
`isWritable()` ignores Windows ACLs). On failure portable mode stays on, so nothing reaches
the registry, and `main()` shows an error after the command line is parsed and exits with
code 1. Typical cause: the folder was extracted under Program Files.

### Paths kept in settings

Stored absolute paths would break when the drive letter changes. In portable mode, paths go
through `toStoredPath()` on write and `fromStoredPath()` on read:

| Key | Written / read in |
|---|---|
| `Settings/AppData` | `preferences.cpp` (`GetAppData`/`SetAppData`), `recent_databases.cpp` (`setActive`), `main.cpp` (launch value, cancelled-setup restore) |
| `RecentDatabases` | `recent_databases.cpp` (all access; `database_delete_dialog.cpp` now calls `RecentDatabases::remove()`) |
| `db/LastShutdownPath` | `main.cpp`, `DatabaseManager::markCleanShutdown()` |

Rule (`encodePath()`):

- Windows: a path on the same drive as the portable data folder is stored relative to it
  (`OSCAR20_Data`, `../Data`); `QDir::relativeFilePath()` returns an absolute path for
  another drive, which is kept.
- Other platforms: only paths inside the portable data folder become relative, because a
  mount point can change.

Relative values are resolved against the current portable data folder. Paths are relative
to it rather than to the program folder because that is what moves together with the
data; it also means a data folder moved into `OSCAR Portable Data` together with its
`Settings` keeps working. Outside portable mode both functions return their input
unchanged, so installed OSCAR behaves exactly as before.

`SavedPath/*` (last-used dialog folders) is not converted: a missing folder already falls
back to Documents.

### Other startup behaviour

- `homeDocs` in `main()` becomes the portable data folder, so the first-run "create" and
  "find" dialogs start there and a relative `--datadir` resolves there.
- First run with an existing OSCAR 2 database in the default data folder (Settings removed,
  or a data folder copied in) uses it without asking.
- `findLegacyOscarDataFolder()` returns nothing: the OSCAR 1.x registry is not read.
- `--portable` is accepted by the argument loop. Portable mode, its folders and first use
  are logged; the program and Settings folders are added to the build info.
- Restarts (language change, database switch, compress) add `restartArguments()`, which is
  `--portable` only when that option enabled portable mode.

### Not changed

- Help index (`help.cpp`, `AppDataLocation`): not compiled while `helpless` is defined.
- Cloud downloads use the system temp folder; nothing persistent.
- Cloud OAuth tokens are encrypted (#329) with a key built into OSCAR, so they are not
  readable as plain text, but the key travels with the program; the README still warns
  that a lost stick exposes the sign-in.

## Packaging (`Building/Windows/deploy.bat`)

Built whenever the installer is built (skipped with `skipInstall`, like the installer):

1. `:makePortable` copies `Release\` into `Portable\OSCAR Portable\` and adds `portable.ini`
   from `Building\Windows\`. `README-Portable.txt` goes into `Portable\` itself, so it sits
   at the top of the ZIP beside the `OSCAR Portable` folder.
2. Name follows the installer: `OSCAR-<ver>-Win64[-<rev>]-Portable<suffix>.zip`, with the
   revision added for non-release builds; fields come from the generated `buildinfo.iss`.
3. Zipped with `%SystemRoot%\System32\tar.exe -a` (bsdtar). Git's GNU tar, which can be first
   on PATH after `:getGawk`, cannot write ZIP files.
4. Output goes to `Installer\` beside the installer. `:cleanFolder` removes `Portable\` on the
   next deploy. `CopyDropbox.bat` and `nightly-build.bat` pick up only `*.exe`, so they are
   unaffected, and they don't publish the ZIP either.

## Upgrades

The ZIP holds only the `OSCAR Portable` folder, so upgrading never touches user data: delete
`OSCAR Portable` and put the new one in its place. A changed `DataFolder` has to be made
again in the new marker; the README says so.

Early test builds kept `Settings\` and `OSCAR20_Data\` inside the program folder. They are
not moved automatically; the README tells those testers to move both into
`OSCAR Portable Data\`, after which the stored relative paths resolve unchanged.

## Platform scope

The code is cross-platform, but packaging and testing are Windows-only. On macOS the
executable folder is inside the app bundle (writing there breaks the signature) and an
AppImage's folder is read-only, so neither is a supported portable layout.

## Testing

Unit tests: `oscar/tests/portablemodetests.cpp` (encode/decode rules, `DataFolder` parsing,
data folder resolution, ZIP-named folder detection).

Manual checklist:

1. Put `OSCAR Portable` at the top of a stick; first run creates
   `OSCAR Portable Data\Settings\` and `OSCAR Portable Data\OSCAR20_Data\` beside it; no new
   registry keys under `HKCU\Software\OSCAR_Team\OSCAR 2.0`.
2. "Extract All" with the proposed destination: first run warns; Quit leaves no
   `OSCAR Portable Data`. Second run after moving the folder to the root: no warning.
3. `DataFolder=Data` in the marker: settings and data go in `OSCAR Portable\Data\`.
4. Change the stick's drive letter (Disk Management); OSCAR opens the same data.
5. Switch database via File ▸ Database, change language, compress database: still portable.
6. Extract under Program Files: error message, exit.
7. Delete `OSCAR Portable Data\Settings\`: OSCAR finds the existing data folder without asking.
8. Installed OSCAR on the same PC is unaffected (registry settings and data unchanged).
