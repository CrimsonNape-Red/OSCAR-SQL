# Portable Mode (Windows ZIP distribution)

GitLab #328. Run OSCAR from a USB stick or SD card without installing it and without
writing settings to the host computer's registry.

## Switching it on

Portable mode is on when either:

- a marker file `portable.ini` sits next to `OSCAR.exe` (the normal case: the portable ZIP
  ships it), or
- OSCAR is started with `--portable` (no marker needed; restarts pass it on).

The marker's content is never read; only its presence matters. Deleting it returns that copy
of OSCAR to normal (registry + Documents) behaviour.

## Folder layout

```
OSCAR Portable\                 (any name, any drive; must be writable)
├── OSCAR.exe, Qt DLLs, plugins, Help\, Html\, Translations\   (= deploy's Release\)
├── portable.ini                marker
├── README-Portable.txt
├── Settings\OSCAR_Team\OSCAR 2.0.ini    created on first run (development builds: branch name added)
└── OSCAR20_Data\                        created on first run (default data folder)
```

The data folder name comes from `getModifiedAppData()`, so test builds use the usual
suffix (e.g. `OSCAR20_Data-test`), as they do in Documents.

## Implementation

Module: `oscar/SleepLib/portablemode.{h,cpp}` (namespace `PortableMode`).

### Settings redirection

Every OSCAR `QSettings` is default-constructed, and a default-constructed `QSettings` uses
`QSettings::defaultFormat()`. `PortableMode::initialize()` therefore only needs:

```cpp
QSettings::setDefaultFormat(QSettings::IniFormat);
QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,   root + "/Settings");
QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, root + "/Settings");
```

SystemScope is redirected as well so QSettings' fall-back reads never pick anything up from
the host. The marker is not reused as the settings file because `setPath()` always names the
file `<path>/<organization>/<application>.ini`.

The one explicit `NativeFormat` use, the read-only `MachineGuid` lookup in
`getFingerprint()`, is deliberately left alone: a new PC still gets its OpenGL test.

### Ordering constraint

`main()` reads the graphics engine from `QSettings` before constructing `QApplication`
(engine attributes must be set first), so `initialize()` runs immediately after the
organization/application names are set and before the first `QSettings`.
`QCoreApplication::applicationDirPath()` is not available then, so the executable folder is
worked out from `argv[0]`: a full path from Explorer or a shortcut; a bare name is looked up
in the current folder, then `PATH`. After `QApplication` exists, `main()` logs a warning if
that folder differs from `applicationDirPath()`.

### Writable check

`initialize()` creates `Settings\` and writes a probe file (Qt's `isWritable()` ignores
Windows ACLs). On failure portable mode stays on, so nothing reaches the registry, and
`main()` shows an error after the command line is parsed and exits with code 1. Typical
cause: the folder was extracted under Program Files.

### Paths kept in settings

Stored absolute paths would break when the drive letter changes. In portable mode, paths go
through `toStoredPath()` on write and `fromStoredPath()` on read:

| Key | Written / read in |
|---|---|
| `Settings/AppData` | `preferences.cpp` (`GetAppData`/`SetAppData`), `recent_databases.cpp` (`setActive`), `main.cpp` (launch value, cancelled-setup restore) |
| `RecentDatabases` | `recent_databases.cpp` (all access; `database_delete_dialog.cpp` now calls `RecentDatabases::remove()`) |
| `db/LastShutdownPath` | `main.cpp`, `DatabaseManager::markCleanShutdown()` |

Rule (`encodePath()`):

- Windows: a path on the same drive as the OSCAR folder is stored relative to it
  (`OSCAR20_Data`, `../Data`); `QDir::relativeFilePath()` returns an absolute path for
  another drive, which is kept.
- Other platforms: only paths inside the OSCAR folder become relative, because a mount point
  can change.

Relative values are resolved against the current OSCAR folder. Outside portable mode both
functions return their input unchanged, so installed OSCAR behaves exactly as before.

`SavedPath/*` (last-used dialog folders) is not converted: a missing folder already falls
back to Documents.

### Other startup behaviour

- `homeDocs` in `main()` becomes the OSCAR folder, so the first-run "create" and "find"
  dialogs start there and a relative `--datadir` resolves there.
- First run with an existing OSCAR 2 database in the default data folder (Settings removed,
  or a data folder copied in) uses it without asking.
- `findLegacyOscarDataFolder()` returns nothing: the OSCAR 1.x registry is not read.
- `--portable` is accepted by the argument loop. Portable mode and its folder are logged and
  added to the build info.
- Restarts (language change, database switch, compress) add `restartArguments()`, which is
  `--portable` only when that option enabled portable mode.

### Not changed

- Help index (`help.cpp`, `AppDataLocation`): not compiled while `helpless` is defined.
- Cloud downloads use the system temp folder; nothing persistent.
- Cloud OAuth tokens are stored in plain text in the INI file, as they are in the registry
  now; a lost stick exposes them. The README warns about this; encryption is tracked in #329.

## Packaging (`Building/Windows/deploy.bat`)

Built whenever the installer is built (skipped with `skipInstall`, like the installer):

1. `:makePortable` copies `Release\` into `Portable\OSCAR Portable\`, adds `portable.ini` and
   `README-Portable.txt` from `Building\Windows\`.
2. Name follows the installer: `OSCAR-<ver>-Win64[-<rev>]-Portable<suffix>.zip`, with the
   revision added for non-release builds; fields come from the generated `buildinfo.iss`.
3. Zipped with `%SystemRoot%\System32\tar.exe -a` (bsdtar). Git's GNU tar, which can be first
   on PATH after `:getGawk`, cannot write ZIP files.
4. Output goes to `Installer\` beside the installer. `:cleanFolder` removes `Portable\` on the
   next deploy. `CopyDropbox.bat` and `nightly-build.bat` pick up only `*.exe`, so they are
   unaffected, and they don't publish the ZIP either.

## Upgrades

The ZIP never contains `Settings\` or the data folder, so extracting over an existing
portable folder cannot overwrite user data. The README recommends a clean swap (delete
everything except `Settings\`, the data folder and `portable.ini`, then extract) to avoid
stale files.

## Platform scope

The code is cross-platform, but packaging and testing are Windows-only. On macOS the
executable folder is inside the app bundle (writing there breaks the signature) and an
AppImage's folder is read-only, so neither is a supported portable layout.

## Testing

Unit tests: `oscar/tests/portablemodetests.cpp` (encode/decode rules).

Manual checklist:

1. Extract ZIP to a stick; first run creates `Settings\` and `OSCAR20_Data\` beside the
   exe; no new registry keys under `HKCU\Software\OSCAR_Team\OSCAR 2.0`.
2. Change the stick's drive letter (Disk Management); OSCAR opens the same data.
3. Switch database via File ▸ Database, change language, compress database: still portable.
4. Extract under Program Files: error message, exit.
5. Delete `Settings\`: OSCAR finds the existing data folder without asking.
6. Installed OSCAR on the same PC is unaffected (registry settings and data unchanged).
