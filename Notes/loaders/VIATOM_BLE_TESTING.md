# Testing Viatom BLE Import (branch `vihealth-dev`)

Thank you for helping. This branch adds **Data → Viatom BLE Import…**, which downloads
recordings from a Viatom/Wellue oximeter over Bluetooth and imports them into OSCAR.

- **Classic models:** O2Ring, SleepU, Checkme O2 and their relatives.
- **O2Ring S family:** O2Ring S, SC, SF, SP. This support is **experimental**. It was
  written from the vendor app's code, and nobody has tried it on a real ring yet. Your test
  is the first.

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
2. **Open the dialog.** Start the OSCAR you built, open your test profile, and choose
   **Data → Viatom BLE Import…**.
3. **Connect.** Click **Search for devices**. Your ring should be listed, marked
   *(O2Ring S family: experimental)*. Select it and click **Connect**.
4. **Download.** If it gets as far as a list of recordings, download one or two and let
   OSCAR import them.
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

## Known limits

- The O2Ring S support is based only on the vendor app's code, so the first test may well
  fail. The logs will show where, and that is what's needed to fix it.
- Live readings, ring settings and deleting recordings are not part of this feature.
