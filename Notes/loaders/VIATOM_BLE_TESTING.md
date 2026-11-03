# Testing Viatom BLE Import (branch `vihealth-dev`)

Thank you for helping. This branch lets the Oximetry Wizard download recordings from a
Viatom/Wellue oximeter over Bluetooth, or over its USB cable (SleepU, O2Ring S), and import
them into OSCAR. Open it with **Data → Import Viatom/Wellue Data**, which starts the wizard
with Viatom/Wellue selected, then choose **Over Bluetooth** or **Through the USB cable**.

- **Classic models:** O2Ring, SleepU, Checkme O2 and their relatives.
- **O2Ring S family:** O2Ring S, SC, SF, SP. Written from the vendor app's code and
  confirmed on O2Ring S rings (see **Test results**); the SC, SF and SP are untested.

## Before you start

- **Use a separate data folder**, not your everyday one. The first time the test build
  starts, choose to **create a new data folder**, not "find existing". Later you can
  also make one with **File → Database → New**, or use the portable ZIP.

  The import only adds data, but this is test code.
- **Close the ViHealth app on your phone**, or turn the phone's Bluetooth off. The ring
  talks to only one device at a time.
- **What OSCAR does to the ring:**
  - It sets the ring's clock to your computer's time, as ViHealth does. You can untick
    this in the dialog.
  - It never deletes anything from the ring.

## Windows installer

If you were sent `OSCAR-…-BLE.exe`, run it instead of building. It installs to
`C:\Program Files\OSCAR20-test-BLE` as "OSCAR 2 (test) -BLE", next to any other OSCAR,
and keeps its settings separate from them. Windows SmartScreen may warn that the
installer is unsigned: choose **More info → Run anyway**. The `-Portable-BLE.zip` is the
same program to unzip and run without installing.

## Building

You need Qt 6 with the **Bluetooth** module.
- **macOS:** use Qt 6.6 or later, so OSCAR can ask for Bluetooth permission.
- **Linux:** also needs the **D-Bus** module and a running BlueZ (`systemctl status
  bluetooth`).
- **Windows:** build with an **MSVC** kit (e.g. *Desktop Qt 6.10 MSVC2022 64-bit*, plus
  the free Visual Studio Build Tools). Qt 6 has no working Bluetooth with **MinGW**, so a
  MinGW build leaves the menu item greyed out. `Building\Windows\buildall-msvc.bat
  6.10.2 <Qt folder> -BLE` builds it and makes the installer (needs Inno Setup 6).

```bash
git clone -b vihealth-dev https://gitlab.com/Seeker4/OSCAR-code.git
cd OSCAR-code && mkdir build && cd build
qmake6 ../oscar/oscar.pro        # "qmake" on some systems
make -j$(nproc)
```

On Debian/Ubuntu the Qt packages are `qt6-base-dev qt6-connectivity-dev` (plus the usual
OSCAR build packages). If the qmake output says "Qt Bluetooth not found" or "does not work
with MinGW", the menu item will stay greyed out. Hovering over a greyed-out item shows the
reason, and the log has a `ViatomBle availability:` line.

## The test

1. **Prepare the ring.** Wear it for a night, or at least ten minutes, so there is a
   recording. Then take it off and wait until it shows its standby screen.
2. **Open the wizard.** Start the OSCAR you built, open your test profile, choose
   **Data → Import Viatom/Wellue Data**, select **Over Bluetooth** and click **Next**.
3. **Connect.** OSCAR searches for devices and lists every oximeter it finds. Select
   yours and click **Connect**.
4. **Download.** In the list of recordings, tick the ones you want and click
   **Download**. Each recording is imported into OSCAR as soon as it has arrived.
5. **Check the result.** Look at the night in OSCAR's Daily view. Do SpO2, pulse and the
   start time look right compared with ViHealth?

## What to send back

Whatever happens, even if it fails at the first step, please send:

1. **The diagnostic logs:** **Help → Troubleshooting → Create zip of OSCAR diagnostic
   logs**. Lines starting with `ViatomBle` show every message exchanged with the ring.
   They contain no keys. They do include the first bytes of each reply, so you may want to
   look through them before sending.
2. **What the dialog said:** a screenshot or the exact text of any message.
3. **Your ring's details:** model and firmware version (shown in ViHealth under the
   device's settings).
4. **If a download worked:** the downloaded file(s) from
   `OSCAR20_Data/Profiles/<your profile>/Oximetry_Data/<serial>/` and, if you can, the same night's file
   exported from ViHealth. Comparing the two is the best check.

## Test results

| Device | Firmware | Connection | System | Result |
|---|---|---|---|---|
| SleepU (model 1654) | 1.2.0 | USB cable | Linux, Windows 10 | Works |
| O2Ring S | | Bluetooth | Windows 11 (two testers) | Works |
| O2Ring S | | USB cable | Windows 11 | Works |
| O2Ring S | 1.13.0 | Bluetooth | Windows 11 26H2 | Works |
| O2Ring | 1.9.0 | Bluetooth | Windows 11 26H2 | Works |
| O2Ring | 1.4.0 | Bluetooth | Windows 11 26H2 | Works |
| O2Ring, small finger (kids) | 1.2.0 | Bluetooth | Windows 11 26H2 | Works |
| Checkme O2 Max ("O2M", model 1642) | 1.7 | Bluetooth | Linux Mint 22, Windows | Works |
| Checkme O2 Max ("O2M", model 1642) | 1.7.0 | USB cable | Linux Mint 22, Windows 10 | Works |

The Checkme O2 Max was first reported (2026-11-02) as working over Bluetooth and its USB
cable, but both of those tests were really over Bluetooth: its cable is a different USB
device (`1915:f33d`), which OSCAR didn't support then. Cable support was added on
2026-11-03 (see `VIATOM_USB.md`) and tested on Linux with a Checkme that held no
recordings; the same day the Windows 10 portable build (a9f2a6b5) did the same through
the HID driver, with no driver to install. A 25-minute recording made afterwards then
downloaded and imported over the cable on both systems; the two downloaded files were
identical (3,885 bytes).

The 2026-11-01 report (the last four rows) also found:

- The wizard listed all four rings at once and the tester picked each in turn.
- Bluetooth downloads were about as fast as O2 Insight Pro over its cable.
- The clock was set correctly on each ring the morning after the switch from daylight
  saving time.
- Two rings worn on the same night both imported, but the Daily view shows only one of
  them: OSCAR keeps one oximeter per day (`Day::addSession`), as it always has. Both
  recordings are stored, and Statistics counts both.

The EMAY EMO-80 (Bluetooth, see `EMAY_EMO80_BLE.md`) is untested too: choose the **EMAY**
card in the wizard, then **Over Bluetooth**.

Untested: the classic O2Ring over its cable, the Checkme O2 (not Max), the O2Ring SC/SF/SP, and
anything on macOS.

## Known limits

- Live readings, ring settings and deleting recordings are not part of this feature.
