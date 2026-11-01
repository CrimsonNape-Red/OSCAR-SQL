# EMAY EMO-80 over Bluetooth LE

Worked out on 2026-11-01 from EMAY's Android app, **EMAY Oximeter 1.2.8**
(`com.jack.emaybloodoxygen`), decompiled with jadx. Not yet checked against a real device.
Class names below are the app's obfuscated ones.

## Which protocol

The app supports several EMAY models and picks the model from the advertised name
(`BleScanHelper`):

| Name starts with | Model code | Bluetooth | Protocol |
|---|---|---|---|
| `EMAY Oximeter` | 100 | classic (SPP) | Contec |
| `EMAY BT Oximeter` | 110 | LE | Contec |
| `EM90` | 120 | LE | Contec |
| `SleepO2-2`, `SleepO2` | 131, 130 | LE | Contec |
| `EMO-100` | 140 | LE | Contec |
| `EM60` | 200 | LE | other (`MrdBlueService`) |
| **`EMO-80`** | **210** | **LE** | **"Store" protocol (`MrdStoreBlue`)** |

`isKangtai()` (康泰 = Contec) is true for the EMO-80, but `BlueChain` sends model 210 to its
own chain (`z5` → `MrdStoreBlue`) and every other Contec model to `p3`. So **the EMO-80 does
not use a CMS50 protocol**. The other Contec models use a command set (`0x80`, `0x83` set
time, `0x89`, `0x90`, `0x9C`, `0x9D`, `0x8E`; `y.java`) that differs from the CMS50F v3.7
serial protocol OSCAR used to have (`0xA0`–`0xB0`, 7-bit encoded).

## GATT

- Service `0000ff12-0000-1000-8000-00805f9b34fb`.
  - `ff01`: write, **with response** (`WRITE_TYPE_DEFAULT`), in chunks of 20 bytes.
  - `ff02`: notify; the app enables it through the CCCD (`0x2902`).
- No MTU request. Scanning filters on the name, not on a service UUID.
- No pairing, bonding or encryption.

## Frames

Both directions:

```
E2  cmd  type  lenHi lenLo  payload[len]  E3  xor
```

- `xor` is the XOR of `cmd`, `type`, the length and the payload: everything between `E2`
  and `E3`.
- Requests from the app have `type` = `0x2F` (`/`); acknowledgements have `type` = `0x4F` (`O`).
- A reply carries the same `cmd`; `payload[0]` is a status, 0 = OK.
- Replies can span several notifications. The app collects bytes from an `E2` until it has
  `len + 7` bytes, then checks `E3` and the XOR.
- One command at a time; the app gives up after 10 s.
- Times are BCD: `YY MM DD hh mm ss`, year + 2000.

| cmd | Request payload | Reply payload after status |
|---|---|---|
| `A1` | none | communication reset |
| `A2` | BCD time | set time |
| `A3` | none | BCD time |
| `A4` | none | type, battery percent |
| `A5` | none | model, serial, BLE MAC, firmware (each a length byte + ASCII), then a length byte + BCD time |
| `A6` | none | total space (u32), free space (u32), max records (u16), records stored (u16) |
| `A7` | `00 00 00` | one recording header, see below |
| `A8` | `03 serial(u16) package(u16) packageSize(u16)` | one data package, see below |
| `A9` | `00 00 00 00` | **delete all recordings** |
| `AA` | none | SpO2 alarm, pulse alarm, SpO2 low, pulse high, pulse low |
| `AB` | those 5 bytes | set alarms |

All multi-byte numbers are big-endian.

### Recording list (`A7`)

Each reply describes one recording:

```
status endFlag serial(u16) userId BCDtime[6] duration(u32) checksum
```

`duration` is in seconds. The app acknowledges each reply with `A7 'O' 00`, and the device
sends the next header until `endFlag` = 1. The app then reverses the list.

### Recording data (`A8`)

The app asks for recording `serial` from package 1 with `packageSize` 200. Each reply is:

```
status endFlag package(u16) data...
```

The app acknowledges each package with `A8 'O' 00` until `endFlag` = 1.

- If a package arrives out of order, or a reply times out, the app waits 2 s and asks
  again from the package it expects, up to 2 times.
- The data is pairs of bytes, `SpO2, pulse`, **one pair per second**: the app expects
  `duration × 2` bytes. The app treats SpO2 outside 20–100 as no reading, and pulse 255 as
  no reading.
- The header's `checksum` is presumably the XOR of the data bytes. The app computes that
  value but doesn't compare it.

## The app's download sequence

1. Connect, find `ff12`, enable notifications on `ff02`.
2. `A3` get time. If it differs from the phone's time, `A2` set time. In practice it
   always sets the clock, because it compares to the second.
3. `A6` storage. If no recordings are stored, stop.
4. `A7` list all recordings, then skip the ones it already has.
5. `A8` download each new recording.
6. **`A9` delete all**: on by default for the EMO-80 (`isDeleteAfterDownload`), but skipped
   after the app's first download in a session.

## In OSCAR

Implemented on 2026-11-01, from this write-up only; not yet tried on a device.

- `SleepLib/viatomble/emay.*`: the frames, `Emay::FrameSplitter`, and `EmayDownloader`,
  which the import panel uses instead of the Viatom `Downloader` for a device named `EMO-80…`
  (`DeviceFamily::Emay`). Both implement `DownloaderBase`.
- Info is `A5`, `A4`, `A6`, then `A7` with acknowledgements. The clock is set with `A2`
  when the "set the clock" option is on. Each recording is downloaded with `A8` in
  packages of 200 bytes; a package that doesn't arrive in 10 s is asked for again, twice.
  `A9` (delete all) is never sent.
- Writes always go with response, as in EMAY's app.
- Each recording is saved as `Oximetry_Data/EMAY_<serial>/EMAY_<start>_<number>.spo2`, in
  the SpO2 Assistant format that OSCAR's CMS50 readers already import. The wizard stores
  it as a session at its own start time, on the oximeter machine of the first reader that
  opens it (the CMS50 one); a recording already in OSCAR is left alone. The same files can
  be imported again through the EMAY card's "From data files", which goes through the
  CPAP-matching step like any CMS50 file.
- The wizard has an EMAY card (Bluetooth, or files OSCAR saved earlier). Contec's
  Bluetooth models (`SpO2xx`) are listed in the Bluetooth search but greyed out.
- EMAY's other recording oximeters (SleepO2, SleepO2 Pro = `SleepO2-2`, EMO-90 = `EM90`,
  EMO-100) use Contec's protocol and are listed greyed out, labelled EMAY. Viatom also has
  a "SleepO2": the advertised service decides (Viatom's, or `ff12` for EMAY); without one,
  the card chosen in the wizard does, and Viatom is assumed otherwise. The list shows the
  brand OSCAR assumed.
- Tests: `tests/emaytests.cpp`, against a scripted fake EMO-80.

## Notes from the review

- The protocol fits the `ViatomBle` design: a link (the same BLE code with different UUIDs),
  a frame splitter, and a downloader. It is simpler than the Viatom one: no file system and
  no encryption.
- OSCAR should **never send `A9`**. It could keep track of the recordings it has by serial
  and start time instead.
- Recordings are 1 Hz SpO2 and pulse with a start time, like a Viatom recording, so no
  CPAP matching is needed.
- To confirm on a device: the advertised name, the `A5` reply, the timing of each
  package, and whether `checksum` is the XOR of the data.
