# Oximeter support review (2026-11-25)

A check of the Oximetry Wizard's oximeters and connection methods against the makers'
software: ViHealth 2.75.68 (Viatom/Wellue, Android), EMAY Bluetooth Pulse Oximeter 1.2.8
(Android), Contec Health Aide 3.5.8 (Android), Contec Smart Device Assistant 3.1.0.1
(Windows), and Contec's "Communication protocol of pulse oximeter V7.0" (2010, from
https://github.com/albertcbraun/CMS50FW).

## The wizard

| Card | Methods | Notes |
|---|---|---|
| Viatom/Wellue | USB cable (`1a86:e010`, `1915:f33d`), Bluetooth LE, data files | see `VIATOM_USB.md`, `VIATOM_BLE_TESTING.md` |
| EMAY | Bluetooth LE (EMO-80 only), files OSCAR saved | see `EMAY_EMO80_BLE.md`; untested on a device |
| Contec CMS50F v3.7+ | serial cable, data files | Contec's V7.0 protocol, below |
| Contec CMS50D+/E/F | serial cable, data files | the older CMS50 protocol |
| ChoiceMMed MD300W1 | data files | |
| Other (CSV) | data files | see `CSV_OXIMETRY.md` |

## Viatom/Wellue Bluetooth names

OSCAR recognises an oximeter by its advertised name (`viatomble_scanner.cpp`), or by
Viatom's service UUID when the device advertises it. ViHealth's table is
`com.lepu.blepro.objs.Bluetooth.getDeviceModel()`: most names are matched with
`contains()`, and a few by the first word of the name. The model number it returns picks
the protocol class (`LpWorkManager.initInterfaces()`).

Found in this review and added:

| Advertised name | Model | Protocol class |
|---|---|---|
| `O2` | Checkme O2 (the Checkme O2 Max is `O2M`) | OxyCommon (classic) |
| `O2BAND` | SnoreO2 | OxyCommon |
| `KidsO2-WPS` | KidsO2 (WPS) | OxyCommon |
| `R10` | JFR1001 | OxyCommon |
| `Band-WU` | Band-WU | OxyII (O2Ring S) |

`Checkme O2` and `SnoreO2` were product names OSCAR had guessed at; no device advertises
them, so they were removed. The other 28 classic names and the 10 O2Ring S names agree
with the app.

Viatom/Lepu oximeters with other protocol classes, which OSCAR doesn't have: `BBSM S2`
(OxyS2), `BBSM S3` (OxyS3), `SI PO2` / `SI PO4` / `SI PO6` (OxySi), and the fingertip
family on `Pc60FwBleInterface` (PC-60FW, PF-10, POD-1W, OxySmart, ...) and `Sp20`.
OSCAR ignores these in a search.

## EMAY

The app picks the model from the name's start: `EMO-80` (EMAY's own protocol: supported),
and `SleepO2`, `SleepO2-2`, `EM90` (the EMO-90), `EMO-100`, `EMAY BT Oximeter`, `EMAY
Oximeter` (classic Bluetooth) and `EM60`, which OSCAR lists greyed out as not supported.
All but the EM60 use Contec's Bluetooth LE protocol (`CONTEC_BLE.md`), so the EMO-90 needs
that protocol in OSCAR, including its application-level encryption on newer firmware.

## Contec

### What Contec's Windows software connects through

From the Smart Device Assistant 3.1.0.1 installer (unpacked with innoextract, not run):

| Connection | Evidence | OSCAR |
|---|---|---|
| CP210x USB-to-serial cable, `10c4:ea60` | driver `slabvcp.inf` | both CMS50 loaders |
| STM32 virtual COM port, `0483:5740` | driver `stmcdc.inf` | tried when no CP210x is found (since this review; untested, and a generic ID other STM32 devices use too) |
| TI wireless USB dongle, `0451:16aa` ("HMDongle") | driver `HMDongle.inf` | no |
| Classic Bluetooth (serial port profile, a COM port) | Bluetooth pairing code in `SpO2.exe`, the manual's "wireless connection" | only on Linux, where the CMS50F v3.7 loader also tries `rfcomm` ports |
| USB HID | `SpO2.exe` imports `HidD_GetAttributes` etc. | no |

The HID cable of hardware-2.0 models (`28e9:028a`, 64-byte reports; see
https://codeberg.org/klimd/pulseoxdl) was not found as a constant in `SpO2.exe`; its
protocol was not traced. A contributor offered on 2026-11-17 to add it using HIDAPI.

### Contec's V7.0 protocol and the CMS50F v3.7 loader

The document describes the protocol `cms50f37_loader.cpp` implements: 115200 baud 8N1 over
"COM port or virtual COM or wireless"; a package starts with its type (bit 7 clear), then
a byte holding bit 7 of each following byte, then the data with bit 7 set. Commands are
`7D 81 <code> ...`, padded with `80` to 9 bytes.

- Agrees with the loader: the command codes (`A2`-`A7`, `AA`, `AB`, `AD`, `AE`, `B0`-`B2`),
  the reply types and lengths, both storage-data formats (`0x09` with the perfusion index,
  6 bytes; `0x0F` with three SpO2/pulse pairs, 8 bytes), and the clock order (date `B2`,
  then time `B1`; Sunday = 0).
- In the document but not used by OSCAR: `A1` real-time data, `AC` and `B6` (is there a
  perfusion index), `AF` (a keep-alive every 5 s), deleting all of a user's segments with
  segment `0xFF`.
- On devices but not in the document: `A8` / `A9` (model and vendor strings, replies
  `0x02` / `0x03`), reply `0x06` (version), `0x13`, `0x14`.
- Fixed in this review: the loader's length table had no entry for `0x01` (9), `0x0D` (3,
  the device's disconnect notice) and `0x15` (9), and a package of unknown type made its
  parsing loop spin for ever. It now steps over an unknown byte.

Classic-Bluetooth models (CMS50EW/FW/IW, advertised as `SpO2xx` too) speak this protocol
over their serial port profile. OSCAR could reach them on Windows and macOS if the serial
method could use a paired Bluetooth COM port; not done, no device to test.

## What the log shows when a connection fails

Added in this review, so a tester's `debug.txt` is enough to work from:

- **Wizard:** the oximeter type and method at the start of an import, the summary the
  user was shown, and a file no loader could read (with its size).
- **Bluetooth search:** for every oximeter seen (also the ones not supported yet), the
  advertised manufacturer data as well as the name, signal and services.
- **Bluetooth connection:** every service the device offers and the characteristics (with
  their properties) of the one used; each failure and its reason; on macOS, Qt's own
  CoreBluetooth messages.
- **Viatom/Wellue:** the device details, whether the clock was set, a failure before the
  download. (Every frame sent and received was already logged.)
- **EMAY:** every frame sent and every chunk received, with the downloader's state. The
  EMO-80 code has never met a device, so this is what a first report needs.
- **USB cables:** a Contec HID cable (`28e9:028a`) that is plugged in is named once (Linux
  and Windows).
- **Serial (CMS50):** the list of other serial ports whenever it changes (before: once per
  run), with the IDs in hex, and a port that could not be opened.

Not logged: what a Bluetooth oximeter that isn't supported yet (Contec's, EMAY's other
models) offers once connected, because OSCAR doesn't connect to those.

## Open

- Contec Bluetooth LE protocol (EMO-90, SleepO2, EMO-100, Bluetooth CMS50s): not started.
- Contec HID cable: waiting for the contributor's plan.
- Classic-Bluetooth Contec serial ports on Windows and macOS.
- A tester for the STM32 virtual COM port.
