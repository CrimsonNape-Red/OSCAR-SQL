# Viatom/Wellue USB cable (SleepU, O2Ring)

How OSCAR reads a SleepU through its USB cable, on Linux and Windows, worked out on
2026-10-30 from a USB capture of Wellue's O2 Insight Pro (Windows) and confirmed by
downloading recordings on Linux. The O2Ring's cable is expected to work the same way but
has not been tested. The O2Ring S family uses a different protocol and is not expected to
work over the cable.

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
```

## Windows

Windows needs no driver and no permission. `UsbLink` finds the cable among the HID
devices by its USB ID (SetupAPI, `HidD_GetAttributes`) and opens it through the HID driver:

- `HidD_SetOutputReport` sends an output report through the **control** endpoint, which is
  what the setup needs. (Windows sends `SET_IDLE` itself when the device is plugged in.)
- `WriteFile` and `ReadFile` on the HID handle carry the data reports over the interrupt
  endpoints. Windows adds the report ID byte (0) in front: 33 bytes per report.
- While O2 Insight Pro has the cable open, OSCAR can usually open it too, but the two
  programs would talk over each other, so close O2 Insight Pro first.

## Other systems

Not implemented on macOS.
