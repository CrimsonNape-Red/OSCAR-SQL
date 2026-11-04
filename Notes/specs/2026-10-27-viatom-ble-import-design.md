# Viatom BLE Import: design

**Date:** 2026-10-27
**Update 2026-10-30:** the separate **Data → Viatom BLE Import…** item described below has
been removed. **Data → Import Viatom/Wellue Data** now opens the Oximetry Wizard on its
import choices with Viatom/Wellue selected, and the wizard offers data files or the
Bluetooth/USB download, which opens this dialog.
**Status:** draft, not implemented. Development happens in `~/QtProjects/2.2-Dev/OSCAR-code`,
branch `vihealth-dev`.
**Background:**
- **oxytool** (`~/QtProjects/oxytool/OxyTool`, a Qt 6 port of oxy-rs written earlier in this
  project) already downloads files over Bluetooth from the **SleepU / Checkme O2 and
  classic O2Ring**. It does not handle the **O2Ring S**.
- This design builds the classic downloader on oxytool's code and adds the O2Ring S.
- Protocol details beyond what oxytool covers were decoded from the ViHealth 2.75.68 app
  (Lepu `blepro` SDK). The disassembly and tools are in
  `~/QtProjects/2.2-Dev/ViHealth-analysis/`.

## As built: phase 2, O2Ring S (2026-10-27, experimental, not yet tested on hardware)

- **Decisions (2026-10-27):** the app key is embedded in the source; no test device is
  available here, so a tester with an O2Ring S builds the branch.
  `Notes/loaders/VIATOM_BLE_TESTING.md` is their guide.
- **Code:**
  - `viatomble_o2rings` holds the frames, handshake, payloads and parsers, all pure
    functions.
  - `viatomble_dialect` splits out what differs between the two families:
    - `ClassicDialect`: the phase 1 frames;
    - `O2RingSDialect`: handshake, AES session, generic Lepu commands.

    The `Downloader` keeps the request sequence, retries, the pause for late replies and
    the `.part` handling for both. For the O2Ring S, information takes three steps:
    handshake, info, file list.
  - AES-128-ECB with PKCS#7 padding is in `SleepLib/crypto`, using the Botan already
    bundled with OSCAR.
  - `Device` uses the `e8fb…` service for the O2Ring S, falling back to the classic one.
- **Dialog:** O2Ring S devices can be selected, marked "experimental".
- **Diagnostics:** the downloader writes every frame sent and every reply received
  (command, status, number, first bytes) to the debug log.
- **Tests:** a fake O2Ring S runs the full encrypted session in the unit tests, including
  an unsolicited live-data frame in the middle of a transfer and a ring answering with the
  wrong key. AES is checked against the FIPS-197 test vector.

## As built: phase 1 (2026-10-27, not yet tested on hardware)

- **Code:**
  - `SleepLib/viatomble/`:
    - `viatomble_protocol` (CRC, frames, splitter, info JSON)
    - `viatomble_link` (interface)
    - `viatomble_downloader`
    - `viatomble_availability`
    - `viatomble_device` and `viatomble_scanner` (Qt Bluetooth only)
  - `viatombleimportdialog.{h,cpp}`: built in code, styled after the oximeter wizard.
  - Tests in `tests/viatombletests.cpp`.
- **Late replies.** Replies carry no command code, so after a retry the late reply to the
  first attempt could be taken as the next reply. After accepting a reply to a retried
  request, the downloader pauses for twice the reply timeout and drops anything that
  arrives meanwhile. Replies do carry a packet number at bytes 3–4 (the SDK ignores it).
  Once the device has shown that it echoes the request's number, late file chunks are
  also dropped by number.
- **File check before renaming:** size only. `ViatomFile::ParseHeader` is not called by
  the downloader (it would tie it to the loader); a bad file is caught at import.
- **Device file names are kept as they are.** O2Rings name their files `<timestamp>.vld`,
  which is the loader's version-3 format. oxytool renamed them to `.dat`; OSCAR doesn't
  need to, because the loader goes by the header and the 14-digit timestamp.
- **Two entry points:**
  - Data → Viatom BLE Import…;
  - a button on the oximeter wizard's import-method page, which closes the wizard and
    opens the dialog.

  Both are greyed out with the reason in the tooltip when Bluetooth can't be used.
- **The dialog stays connected after a download.** "Choose more recordings" returns to the
  refreshed file list.
- **The test runner creates no application object.** `ViatomBleTests` creates a
  `QCoreApplication` for its own event loop.
- **macOS:** `Building/MacOS/Info.plist.in` has `NSBluetoothAlwaysUsageDescription`.
- **Clock set on every connection,** as ViHealth does: `0x16` with
  `{"SetTIME":"yyyy-MM-dd,HH:mm:ss"}` in local time. It's an option on the device page,
  on by default. The files page says how far the clock was off, since recordings already
  on the device carry that error. (oxytool sends an extra `}` and a NUL; OSCAR follows the
  SDK.)
- **Packaging (2026-10-27):**
  - The .deb depends on `libqt6bluetooth6` and `libqt6dbus6`, and recommends `bluez`.
  - The Linux, Windows and macOS build notes name the Qt Connectivity module.
  - The Flatpak manifest (kept outside the repo) has `--system-talk-name=org.bluez` and
    `--allow=bluetooth`; the KDE 6.10 runtime already ships Qt Bluetooth.
  - The AppImage needs no change: linuxdeploy bundles the linked Qt libraries.
- **Not done yet:** testing on hardware.

## Goal

Let OSCAR download recordings straight from a Viatom/Wellue oximeter over Bluetooth Low
Energy, instead of the user copying files off a phone. Every downloaded file is kept in a
folder of its own, and the user can choose to import each file into OSCAR as soon as it
arrives.

## User-facing behaviour

### Menu

- A new item, **Data → Viatom BLE Import…**, placed directly after the existing
  **Import Viatom/Wellue Data** item.
- The item is **disabled (greyed out)** when OSCAR finds no usable Bluetooth adapter. Its
  tooltip gives the reason:
  - "This build of OSCAR has no Bluetooth support", when Qt Bluetooth is missing from the
    build;
  - "No Bluetooth adapter found";
  - "Bluetooth is turned off".
- The state is checked when the Data menu opens (`aboutToShow`), so turning Bluetooth on
  takes effect without restarting OSCAR. `QBluetoothLocalDevice::hostModeStateChanged`
  updates it while the menu is closed.
- Like the other import items, it is disabled when no profile is open.

### Dialog

1. **Scan.** Searches for about 10 seconds (Rescan restarts it) and lists Viatom devices by
   Bluetooth name and signal strength. Devices are recognised by the classic Viatom service
   UUID `14839ac4-7d7e-415c-9a42-167340cf2339` when they advertise it, otherwise by name
   prefix (see "Supported devices").
2. **Connect.** Asks the device for its info, which includes the serial number, model and
   file list, and shows the device's model, serial number and battery level.
3. **File list.** Lists the files on the device. Files already in the download folder (same
   name and same size) are ticked as "already downloaded" and skipped unless the user ticks
   **Download again**.
4. **Options:**
   - **Import into OSCAR as files arrive**, on by default and remembered in the profile
     preferences.
5. **Download.** Shows two progress bars, one for the current file and one overall, plus a
   Cancel button. Each finished file is written to disk and, if the option is on, imported
   at once.
6. **Summary:** files downloaded, files skipped, sessions imported and any errors. Closing
   the dialog refreshes Daily, Overview and the welcome page, as the oximeter wizard does.

### Download folder

```
<profile folder>/Oximetry_Data/<device serial>/<file name as reported by the device>
```

- `<profile folder>` is the open profile's `{DataFolder}`, i.e.
  `<data folder>/Profiles/<user>`, where `<data folder>` is `GetAppData()` (`OSCAR20_Data`
  or whatever the user renamed or chose it to be, or the portable-mode location). The
  recordings belong to one person, so they stay with that person's profile.
- **Keep the device's own file names.** `ViatomLoader` takes the session start from the
  14-digit timestamp in the file name and the serial number from the parent folder name
  (`viatom_loader.cpp`, `ParseFileViatom`). Storing files this way means the existing
  loader imports them unchanged, and the user can re-import the folder later with the
  ordinary **Import Viatom/Wellue Data** item.
- If the device reports no usable serial number, use the Bluetooth address with the colons
  removed. The loader then won't treat the folder name as a serial and files go to the
  generic Viatom machine, as with manually imported files today.
- Files are written to `<name>.part` and renamed when complete and verified, so a dropped
  connection never leaves a truncated file that looks valid.
- **OSCAR never deletes anything** from this folder or from the device. Deleting files on
  the device is out of scope.

### Import as files arrive

- Each finished file is passed to the existing import path:
  `MainWindow::importNonCPAPFiles(viatomLoader, {path}, /*silent=*/true, …)`.
  Import runs on the GUI thread between file transfers, so the database is never written
  from the Bluetooth callbacks.
- If an import fails, the file stays on disk, the failure appears in the summary, and the
  download carries on.
- When the option is off, files are only saved. The summary says where they are.
- The loader already skips sessions that exist (`SessionExists`), so downloading a file that
  was imported before does no harm.

## What exists to build on

| Piece | Where | Use |
|---|---|---|
| oxytool classic-protocol downloader | `~/QtProjects/oxytool/OxyTool/src/` — `oxylib.{h,cpp}` (CRC-8, frames, info JSON), `OxyDevice.{h,cpp}` (connection, commands, file transfer) | Starting point for the classic driver; proven on SleepU and O2Ring |
| oxytool's BlueZ write workaround | `OxyDevice::sendPacketDBus`, `acquireNotifyDBus` | Needed on Linux: Qt's BlueZ backend silently drops writes to characteristics that only allow write-without-response |
| Viatom file parser, including the 2026-10-27 fixes | `SleepLib/loader_plugins/viatom_loader.cpp` | Imports the downloaded files unchanged |
| Non-CPAP import from a file list | `MainWindow::importNonCPAPFiles` | Import as files arrive |
| Profile folder | `p_profile->Get("{DataFolder}")` | Root for `Oximetry_Data` |
| Data menu | `mainwindow.ui`, `menu_Data` | New action next to `actionImport_Viatom_Data` |
| Qt Bluetooth 6 | installed on the Linux build machine (6.4.2) | Scanning, GATT |

## Protocol: phase 1, classic Viatom oximeters

All devices that the SDK drives with `OxyCommonBleInterface` (about 34 models). oxytool
implements this protocol and has been used with the SleepU and the classic O2Ring.

**GATT**

| Role | UUID |
|---|---|
| Service | `14839ac4-7d7e-415c-9a42-167340cf2339` |
| Write (no response) | `8b00ace7-eb0b-49b0-bbe9-9aee0a26e1a3` |
| Notify | `0734594a-a8e7-4b1a-a6b1-cd5243059a57` |

**Frames.** All values are little-endian.

- App to device: `AA cmd ~cmd seq_lo seq_hi len_lo len_hi payload… crc8`
- Device to app: the same layout with `55` as the first byte.
- CRC-8: polynomial 0x07, initial value 0, computed over every byte except the CRC itself
  (`BleCRC.calCRC8`).
- A response can arrive split across several notifications. The receiver keeps a buffer and
  pulls out complete frames: find `55`, check `cmd == ~byte2`, read the length, wait for
  `8 + len` bytes, then check the CRC. If the check fails, drop that `55` and search again
  (the SDK's `hasResponse` does the same).

**Commands needed**

| Command | Code | Payload | Reply |
|---|---|---|---|
| Info | `0x14` | none | JSON text with `SN`, `Model`, `FileList` (comma-separated), `CurBAT`, `CurTIME`, `SoftwareVer`, … |
| Read start | `0x03` | file name + `00` | file size, 32-bit, at frame bytes 7–10 |
| Read content | `0x04` | none; `seq` = chunk index starting at 0 | next chunk of the file, at most 512 bytes |
| Read end | `0x05` | none | acknowledgement |

Byte 1 of a reply (where the command would be) is `0x00` for success and `0x01` for an
error. oxytool relies on this.

**Transfer loop:** Info → for each selected file: Read start → Read content repeated until
the file size is reached → Read end.

- Each step has a timeout; on timeout the command is retried once, then the file is
  abandoned.
- After the transfer, the file is checked before it is renamed into place: it must be the
  expected size, and `ViatomFile::ParseHeader` must accept it.

**Encryption.**
- The SDK's classic driver claims to need encryption (`needEncrypt()` returns true), and
  the handshake runs when the app-wide flag `LpWorkManager.needEncrypt` is set.
- oxytool talks to the SleepU and the O2Ring in plain text and they answer, so these
  devices don't enforce it.
- Phase 1 therefore **does not implement encryption**. If a classic device answers with an
  error, or stays silent where it should answer, OSCAR reports "this device requires an
  encrypted connection, which OSCAR does not support yet" and stops.

**Not in scope for phase 1:** settings other than the clock, live readings, deleting files.

## Protocol: phase 2, the O2Ring S family

The SDK drives these with `OxyIIBleInterface`. They are not a variant of the classic
protocol. oxytool's README says the O2Ring S uses the classic commands, but the SDK
disagrees, which explains why oxytool can't download from it.

**GATT.** `Pf10Aw1BleManager`:

| Role | UUID |
|---|---|
| Service | `e8fb0001-a14b-98f9-831b-4e2941d01248` |
| Write | `e8fb0002-a14b-98f9-831b-4e2941d01248` |
| Notify | `e8fb0003-a14b-98f9-831b-4e2941d01248` |

The SDK can also be told to use the classic `14839ac4…` service (its `UUIDProtocol` V1/V2
setting), so the driver should accept whichever of the two the device offers.

**Frames.**
- `A5 cmd ~cmd type seq len_lo len_hi payload… crc8`
- `type` is 0 from the app and 1 from the device; `seq` is a single byte.
- Same CRC-8 as the classic protocol.

**Commands**

| Command | Code | Payload | Reply |
|---|---|---|---|
For ordinary overnight recordings the SDK uses Lepu's **generic** commands (`LpBleCmd`).
The OxyII-specific `0x06`–`0x09` are only used for other file types (`oxi.list`,
`ppg.list`, `acc.list`) and on a few models; OSCAR doesn't use them.

| Command | Code | Payload | Reply |
|---|---|---|---|
| Encrypt (handshake) | `0xFF` | 16-byte token, scrambled — see below | session key, scrambled |
| Info | `0xE1` | none | serial number: length at byte 37, text from byte 38 (at most 18 bytes); clock: year (16-bit) at 24, then month, day, hour, minute, second |
| Set UTC time | `0xC0` | year (16-bit), month, day, hour, minute, second (local time), UTC offset in tenths of an hour | acknowledgement |
| File list | `0xF1` | none | 1-byte count, then one 16-byte name per file, zero-padded |
| Read start | `0xF2` | 20 bytes: name (up to 16, zero-padded) + 32-bit start offset | 32-bit file size |
| Read data | `0xF3` | 32-bit byte offset of the next block | next block |
| Read end | `0xF4` | none | acknowledgement |

- **Reply type byte:** 1 means a normal reply; anything else is an error code (e.g. `0xE0`
  file not found, `0xF3` decryption failed, `0xFB` busy, `0xFD` unsupported).
- **Sequence number:** one byte, counting 0–254.
- **Matching replies:** replies carry their command code, so OSCAR ignores any reply that
  doesn't answer the pending command, such as live data.
- **Order after connecting:** the SDK runs the handshake, then switches off the ring's
  automatic live-data pushes, then sets the clock. OSCAR does the handshake, info, file
  list and (optionally) the clock. It leaves the live-data setting alone, because replies
  can be told apart by their command code anyway.

**Encryption is required.**
- `OxyIIBleInterface.needEncrypt()` returns true for:

  | Model ID | Bluetooth name |
  |---|---|
  | 124 | O2Ring S |
  | 144 | S8-AW |
  | 180 | SHQO2Pro |
  | 219 | O2Ring SF |
  | 220 | O2MP |
  | 221 | O2Ring SP |
  | 226 | O2Ring SC |
  | 227 | O2RMed S |
  | 248 | PO2B |
  | 250 | PO2B-PPG |

  That is every model in the family except the Band WU.
- **The handshake** (`BleInterface.encrypt()` and the `0xFF` reply handler):
  1. Build a 16-byte token:
     - bytes 0–7: every other byte of `EncryptUtil.SECRET_KEY` (indices 0, 2, …, 14);
     - bytes 8–11: the first 4 bytes of an app ID string;
     - bytes 12–15: derived from the current Unix time.
  2. XOR the token with `SECRET_KEY` (repeating it as needed) and send it as `0xFF`.
  3. XOR the reply payload with `SECRET_KEY` again. The result holds the **AES session
     key**.
- **After the handshake,** every command payload is encrypted with AES-128-ECB, PKCS#5
  padding, using the session key (`EncryptUtil.AesEncrypt`). Reply payloads are decrypted
  the same way. The frame header and CRC stay in plain text.
- **So supporting the O2Ring S means embedding `SECRET_KEY`** — a 16-byte constant from the
  vendor app — **in OSCAR.** See "Open decisions".

## Supported devices

- **Phase 1, the classic protocol.** The Bluetooth names the SDK knows include: O2Ring,
  O2RingF, O2Ring-RE, O2R WAVE, WearO2, SleepU, OxyRing, OxyU, OxyU SE, Oxyfit,
  Oxyfit-WPS, O2M, O2M-WPS, O2S, BabyO2N, O2NCI, CMRing, BBSM S1, AI-S100, Lepu PO6, BM10,
  JFR1001, JFK1001, OMP, SHQFlex, O2 Intg, PO2. Checkme O2, SnoreO2, KidsO2, BabyO2 and
  OxyLink use the same protocol; their advertised names still need confirming.
- **Phase 2, the O2Ring S family** (O2Ring S, O2Ring SC/SF/SP and the other
  `OxyIIBleInterface` models).
  - A separate driver behind the same dialog; see "Protocol: phase 2".
  - It can't be built without the encryption decision.
  - Its files already import (`O2RingS` in the loader).
- **Out of scope:** other protocol families (PC-60FW, POD-2/PC-60NW, AP-20, …) and the Lepu
  ventilators. They use different services or framing.

## Code structure

New code goes in `oscar/SleepLib/viatomble/`, except for the dialog.

**Reusing oxytool.**
- **Carried over:** CRC-8, frame building, the info-JSON parser (`oxylib`), the connection,
  service and characteristic discovery, and the Linux BlueZ D-Bus write and
  `AcquireNotify` paths (`OxyDevice`).
- **Changed for a GUI.** oxytool is a console tool and waits for each reply by spinning a
  nested `QEventLoop`, with `QThread::msleep` between retries. In OSCAR that would let other
  menu actions and imports run in the middle of a transfer, and the sleeps would freeze the
  window. The OSCAR version becomes a non-blocking state machine: send, arm a timer, carry
  on when the reply frame arrives.
- **Replaced: frame reception.** oxytool expects every reply to start with `55 00 FF` and
  throws away anything else. The OSCAR version uses the buffering frame splitter described
  above, which handles replies that span notifications and the error reply (`55 01 FE`).
- **Left out:** live readings (PPG, live parameters, live waveform) and the VLD3 parser,
  which OSCAR doesn't need.

| File | Responsibility | Needs hardware to test? |
|---|---|---|
| `viatomble_protocol.{h,cpp}` | CRC-8, frame building, frame splitting (a buffer goes in, complete frames come out), parsing the info JSON | No — unit tests |
| `viatomble_device.{h,cpp}` | `QLowEnergyController`, service and characteristic discovery, enabling notifications, a command queue with timeouts | Yes |
| `viatomble_downloader.{h,cpp}` | File-list diff against the folder, transfer state machine, `.part` writing and renaming, progress signals | No — runs against a fake device |
| `viatomble_scanner.{h,cpp}` | `QBluetoothDeviceDiscoveryAgent` (Low Energy only), filtering by UUID or name | Yes |
| `viatombleimportdialog.{h,cpp,ui}` (top level, beside `oximeterimport.*`) | The UI | Partly |

- **`viatomble_downloader`** depends on a small interface (`IViatomBleLink`:
  `send(frame)`, signal `frameReceived`) rather than on Qt Bluetooth. The unit tests supply
  a scripted fake that replays frames built from the synthetic Viatom test files.
- **`MainWindow`** gains `on_actionViatomBleImport_triggered()` and an availability check
  that runs when the Data menu opens.
- **Threads:** all Bluetooth work stays on the GUI thread through Qt's event loop. Qt
  Bluetooth is asynchronous, and the transfers are small: a 10-hour night is about 45 KB
  for a classic O2Ring (5 bytes every 4 s) and about 110 KB for an O2Ring S (3 bytes every
  second).

## Build integration

- `oscar.pro`:
  ```
  qtHaveModule(bluetooth) {
      QT += bluetooth
      DEFINES += OSCAR_HAVE_BLUETOOTH
      SOURCES/HEADERS += the files above
  }
  ```
- Without the module, the menu action stays and is permanently disabled, with the
  "no Bluetooth support" tooltip. Nothing else in OSCAR depends on Bluetooth.
- **Linux:** needs BlueZ at run time, and `QT += dbus` for oxytool's write workaround.
  - Some distributions also need the user in the `bluetooth` group.
  - oxytool needed `setcap cap_net_admin,cap_net_raw+eip` on its binary to scan. OSCAR
    should first be tried without it, since the scan goes through BlueZ over D-Bus; if a
    capability turns out to be needed, it must be documented. The AppImage and Flatpak builds need the Qt Bluetooth library bundled,
  and the Flatpak needs `--allow=bluetooth` and the system bus D-Bus permission for BlueZ.
- **Windows:** Qt uses the WinRT Bluetooth backend; Windows 10 or later is required. The
  Windows build kit must include the Qt Bluetooth module.
- **macOS:** `Info.plist` needs `NSBluetoothAlwaysUsageDescription`, otherwise the system
  ends the app on first use of Bluetooth.

## Testing

1. **Unit tests, no hardware** (`tests/viatombletests.cpp`, run by the existing test
   runner):
   - CRC-8 against frames taken from the SDK;
   - frame splitting with frames cut across notification boundaries, bad CRCs and junk
     between frames;
   - info JSON parsing;
   - the downloader against the fake link: complete transfer, skipping existing files,
     timeout and retry, a cancelled transfer leaves no `.part` file behind, the
     import-as-you-go call order.
2. **Hardware checklist** (needs a real device, phase 1 first):
   - found by the scan;
   - info and serial number read;
   - file list matches what ViHealth shows;
   - downloaded file is byte-identical to the one ViHealth saves for the same night;
   - imports correctly;
   - second run skips everything;
   - pulling the ring off mid-transfer leaves no partial file;
   - each platform: Linux, Windows, macOS.
3. **Availability:** test with no adapter, with Bluetooth switched off and with a build
   lacking the module. The item should be greyed out with the right tooltip each time.

## Open decisions

Raised on 2026-10-27. Decided items are marked; for the rest, the design assumes the default in
brackets.

1. **Test hardware.** The SleepU and classic O2Ring have been used with oxytool. **No O2Ring
   S is available here; a tester with one will build `vihealth-dev`** (decided
   2026-10-27).
2. **Encryption.** **Embed the app key in the source** (decided 2026-10-27).
3. **Scope.** Phase 1 and phase 2 are both built; phase 2 is experimental until tested.
4. **Menu label.** "Viatom BLE Import…", as requested. ["Import Viatom/Wellue Data via
   Bluetooth…" would match the item next to it. Kept as requested unless changed.]

## Risks

- **Firmware differences.** Newer firmware on classic devices might start enforcing
  encryption. oxytool shows the SleepU and the O2Ring don't, at least with the firmware
  tested.
- **The O2Ring S protocol has never been used outside the vendor app.** Everything in
  "Protocol: phase 2" comes from the SDK alone. The first test on a real ring may well turn
  up differences (the session-key layout in the reply, the reply framing).
- **Bluetooth stack quirks.** Notification sizes (MTU) and connection reliability vary by
  platform and adapter. The frame splitter must not assume one frame per notification.
- **The vendor app holding the connection.** A device paired with and connected to the
  phone app may not advertise. The dialog should say "close the ViHealth app on your phone"
  when no device is found.
