# Viatom/Wellue USB cables (SleepU, O2Ring S, O2Ring, Checkme O2)

How OSCAR reads a SleepU through its USB cable, on Linux and Windows, worked out on
2026-10-30 from a USB capture of Wellue's O2 Insight Pro (Windows) and confirmed by
downloading recordings on Linux and Windows.

On 2026-10-30 a tester also downloaded recordings from an **O2Ring S** through its cable
on Windows 11. Over Bluetooth the O2Ring S uses its own protocol, but over the cable it
answers the classic one described here. The classic O2Ring's cable is expected to work
the same way but has not been tested.

The same tester found that with the ring already on the cable, OSCAR didn't find it until
the ring was taken off the cable and put back. When it finds no cable, the wizard's
import page therefore keeps looking every second and connects once one appears.

The **Checkme O2 / O2 Max** has a different cable; see [Checkme O2 cable](#checkme-o2-cable)
below. Everything else in this note is about the WCH cable.

## The cable

- USB ID `1a86:e010`, "WCH.CN" "HID To Serial": a WCH USB-to-serial bridge that presents
  itself as a vendor-defined HID device (usage page `0xFFA0`, no report IDs).
- Input and output reports are 32 bytes, on interrupt endpoints `0x82` (in) and `0x02`
  (out), polled every 1 ms. There is also a 5-byte feature report, which O2 Insight Pro
  doesn't use.
- Each report carries serial data as a length byte followed by up to 31 data bytes, then
  zero padding. That holds in both directions.

## Setup

Before any data, O2 Insight Pro sends:

1. `SET_IDLE` (class request `0x0A` to interface 0).
2. `SET_REPORT` for output report 0 (`bmRequestType 0x21`, `bRequest 0x09`,
   `wValue 0x0200`) through the **control** endpoint, 32 bytes:
   `ff c7 83 cc 30` followed by zeros.

Step 2 sets up the bridge's serial side. Without it the device never answers. Sent over
the interrupt endpoint instead, the same bytes have no effect. That is why OSCAR doesn't
use hidraw on Linux: the HID driver sends output reports over the interrupt endpoint
whenever the device has one. OSCAR uses usbfs instead (`ViatomBle::UsbLink`): it detaches
the HID driver, claims the interface, sends the setup through the control endpoint and
exchanges reports on the interrupt endpoints, then gives the interface back to the HID
driver.

## Protocol

The classic Viatom protocol, as over Bluetooth (`AA cmd ~cmd seq len payload crc8`
requests, `55 status ~status ...` replies), with one addition:

- `0x15` is sent first, with no payload. The device answers with status 0 and four zero
  bytes. Its meaning is unknown; OSCAR sends it because O2 Insight Pro does, and asks for
  the info anyway if it goes unanswered (`ClassicUsbDialect`).
- `0x14` info, `0x03` / `0x04` / `0x05` file read, as over Bluetooth. File chunks are 512
  bytes. A 31 KB recording takes about 4 seconds.

O2 Insight Pro repeats setup, `0x15` and `0x14` every 5 seconds while connected.

## Permissions (Linux)

The usbfs node is root-only by default. The Debian packages install a udev rule as
`/usr/lib/udev/rules.d/60-<package>-viatom-usb.rules`:

```
SUBSYSTEM=="usb", ATTR{idVendor}=="1a86", ATTR{idProduct}=="e010", MODE="0660", TAG+="uaccess"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="1915", ATTRS{idProduct}=="f33d", MODE="0660", TAG+="uaccess"
```

The second line is for the Checkme cable's hidraw node. Without the package (an AppImage,
a build from source), OSCAR offers to install the same rules itself with **Allow OSCAR
to use the cable...** (pkexec), as `/etc/udev/rules.d/60-oscar-viatom-usb.rules`.

## Windows

Windows needs no driver and no permission. `UsbLink` finds the cable among the HID
devices by its USB ID (SetupAPI, `HidD_GetAttributes`) and opens it through the HID driver:

- `HidD_SetOutputReport` sends an output report through the **control** endpoint, which is
  what the setup needs. (Windows sends `SET_IDLE` itself when the device is plugged in.)
- `WriteFile` and `ReadFile` on the HID handle carry the data reports over the interrupt
  endpoints. Windows adds the report ID byte (0) in front: 33 bytes per report.
- While O2 Insight Pro has the cable open, OSCAR can usually open it too, but the two
  programs would talk over each other, so close O2 Insight Pro first.
- The cable needs no Qt Bluetooth, so MinGW builds have it too (since 2026-11-03): the
  wizard's import panel is in every build, and only its Bluetooth search and connection
  depend on `OSCAR_HAVE_BLUETOOTH`. Bluetooth itself still needs an MSVC build on Windows.
  `qmake CONFIG+=no_bluetooth` makes such a build on any system, for testing.
  Tested on 2026-11-03: a Windows 10 MinGW build (Qt 6.10.2, mingw1310_64) and a Linux
  `no_bluetooth` build each downloaded and imported a Checkme O2 Max recording over its
  cable, identical to the MSVC and Bluetooth builds' download.

## Checkme O2 cable

Worked out on 2026-11-03 with a Checkme O2 Max (model 1642, firmware 1.7.0) on Linux.
Its cable is not the WCH bridge: the USB plug end holds a Nordic chip that talks to the
oximeter itself.

- USB ID `1915:f33d`, product "Checkme O2". One vendor-defined HID interface (usage page
  `0xFF00`), no report IDs. Report descriptor:
  `06 00 ff 09 01 a1 01 15 00 26 ff 00 75 08 95 40 09 01 81 02 95 40 09 01 91 02 95 40 09 01 b1 02 c0`
  — 64-byte input, output and feature reports.
- One interrupt IN endpoint (`0x81`) and **no OUT endpoint**: output reports go through
  the control endpoint (`SET_REPORT`), which the HID driver does by itself.
- Reports carry the data as for the WCH cable, but 64 bytes long: a length byte, up to 63
  data bytes, zero padding.
- No setup report and no `0x15`: the plain classic protocol, as over Bluetooth (`0x14`
  info, `0x03`-`0x05` file read, set the clock). The wizard uses the classic `Downloader`
  for it, not `ClassicUsbDialect`.

So OSCAR uses the HID driver here: on Linux `/dev/hidrawN` (found by
`HID_ID=0003:00001915:0000F33D` in `/sys/class/hidraw/*/device/uevent`; written as a 0
report ID plus the 64-byte report), on Windows the same HID API as for the WCH cable,
without the setup report. The wizard lists it as "Checkme O2 on the USB cable".

On Linux it needs the hidraw rule above. Without it the node is root-only and OSCAR
offers the permission button.

Tested on 2026-11-03 on Linux and on Windows 10 (no driver needed; Windows lists it as
"HID-compliant vendor-defined device"): connects, reads the details (model 1642, firmware
1.7.0, battery), sets the clock, and downloads and imports a recording (25 minutes,
3,885 bytes, the same file on both systems; shown in the Daily view with SpO2, pulse and
movement).

## Other systems

Not implemented on macOS.
