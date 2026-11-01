# Contec CMS50 oximeters over Bluetooth LE

Worked out on 2026-11-01 from Contec's Android app **Health Aide 3.5.8**
(`com.contec.android.spo2device`; its Chinese name, 智能设备助手, is "Smart Device Assistant"),
decompiled with jadx. Not checked against a real device. Class names are the app's
obfuscated ones.

The app is Flutter. The oximeter code is in its Java plugin `contec_ble` (`L0.a`), which
uses Contec's SDK (`I0.a`, `D0.b`, `E0.*`). The key derivation is in a small native library,
`liblib_encryptkeyiv.so`, disassembled below.

## Models

Oximeters advertise a name starting with `SpO2` plus a two-digit code (`I0.a.c()`):

| Name prefix | Model |
|---|---|
| `SpO201` | CMS50E |
| `SpO202` | CMS50F |
| `SpO206` | CMS50I |
| `SpO208` | CMS50D |
| `SpO209`, `SpO210` | CMS50K (own class, `E0.g`) |
| `SpO212` | CMS60D1 |
| `SpO213` | CMS50S+ |

Other names in the app (CMS50D-BT, CMS50EW/FW/IW, CMS50K1, CMS50S, CMS60D) appear only in its
Dart code. All models except the CMS50K use one class, `E0.a` ("CMSBleStandardDevice").
`PULMO…` and `RT10…` are Contec spirometers and other devices, not oximeters.

## GATT

- Service `0000ff12-…`: write to `ff01`, notifications on `ff02` (CCCD `0x2902`).
- This is the same service the EMAY apps use (see `EMAY_EMO80_BLE.md`).
- The app connects 800 ms after the link comes up, then discovers services and enables notifications.
- No pairing or bonding. On newer firmware the app encrypts at the application level (below).

## Frames

The same family as the CMS50F v3.7 serial protocol, but with different command codes:

- The first byte is the command or reply code, with the high bit set. Every other byte is 7-bit.
- Where data needs 8 bits, the high bits are packed into extra bytes: bit *i* of a packing
  byte is the high bit of data byte *i* in the next group of 7.
- The last byte is a checksum: the sum of all earlier bytes, AND `0x7F`.

### Commands (`E0.b`)

| Byte | Builder | Notes |
|---|---|---|
| `81` | `w()` | first command; reply `F1` holds an 8-character device ID |
| `82` | `u()` | reply `F2`: firmware string and protocol version (`f242v`) |
| `83` | `E()` | set time: `YY MM DD hh mm ss ms(2×7 bits)` |
| `84` | `s(a,b)` | |
| `8E` | `l()` = `8E 09`, `y()` = `8E 06` | |
| `8F` | `D()`, `o(…)` | |
| `90` | `x(n)` | used in steps `n` = 0–6 of the download state machine |
| `91`, `92`, `93`, `94`, `95`, `96` | one parameter | |
| `9A` | `i()` = `9A 1A` | |
| `9B` | `A(n)` | `A(127)` is sent on disconnect |
| `9C` | `z(n)` | |
| `9D` | `c`, `d`, `h`, `C` | settings and stored-data requests |
| `9F` | `q()` | |
| `A0`, `A1`, `A2`, `A3`, `A6` | one or two parameters | |
| `F3` | `a()` | key exchange, protocol version above 13 |
| `F4` | `e(…)` | wrapper for an encrypted command |

Replies the handler (`E0.a`) acts on:
- `D0`, `D1`
- `E0`–`E4`, `EA`–`ED`, `EF`
- `F0`: a general result
- `F1`, `F2`, as above
- `83`: the device's key half
- `84`: an encrypted reply

`E2` and `EA` replies add points to a list (`J0.b.o()`, `J0.b.n()`), so they probably carry
stored data. `EB` is parsed differently for protocol versions below 11. **The meaning of
each data reply has not been mapped yet.**

### Download sequence (as far as read)

1. `81` → `F1` (device ID) → `82` → `F2` (firmware, protocol version).
2. If the protocol version is above 13: key exchange (below).
3. `83` set time (encrypted if keys are set) → `EF`.
4. `8E 09`, then the `90 n` / `9D` requests, answered by `E0`–`ED` replies.

## Encryption (protocol version above 13)

Commands are AES-128-CTR encrypted, then wrapped:

```
F4 len packing… data… 00 01 02 checksum
```

The key and IV come from `getEncryptArray(16 bytes, ID string)`. ID is the 8-character
device ID; ID′ is that ID reversed.

1. **Phone → device:** the phone sends `F3 10` with 16 bytes that are just the current time
   twice (`yy mm dd hh mm ss ms ms`, then the same with the high bit set), 7-bit packed. It
   sends 18 bytes, then the last 4 bytes 300 ms later.
   - Receive key = `getEncryptArray(time16, ID)`.
   - Receive IV = `getEncryptArray(time16, ID′)`, with bytes 13–15 replaced by the counter
     bytes of each `84` frame, reversed.
2. **Device → phone:** the device answers `83` with 16 bytes of its own.
   - Send key = `getEncryptArray(dev16, ID)`.
   - Send IV = `getEncryptArray(dev16, ID′)`, with bytes 13–15 = `02 01 00`. That matches
     the fixed `00 01 02` in every `F4` frame, so every command uses the same IV.

`getEncryptArray`, from `liblib_encryptkeyiv.so` (armeabi-v7a, function at `0x728`, called by
the JNI wrapper at `0x7b0`):

```python
def get_encrypt_array(data16: bytes, s: str) -> bytes:
    t = bytearray(bytes.fromhex("c3edf380" "8095d289"))   # two words from .text
    for i, c in enumerate(s.encode()):
        if i % 2 == 0:
            c = ~(c << 1)
        t[i & 7] ^= c & 0xFF
    out = bytearray(16)
    for i, b in enumerate(data16):
        k = t[(i >> 1) & 7]
        if i % 2 == 0:
            out[i] = ((k // (i + 1)) + b ^ (k ^ (b >> 1))) & 0xFF   # __aeabi_uidiv at 0x1e28
        else:
            out[i] = (k ^ (b + (k >> 1))) & 0xFF
    return bytes(out)
```

The constants and the even-index step were read from the disassembly but haven't been
checked against the app. In the even-index step, `+` binds tighter than `^`, as in the
assembly.

## For OSCAR

- This is not the protocol OSCAR's CMS50F v3.7 code used over USB serial (`A0`–`B0`), but it
  is the same style: 7-bit data, high-bit commands, a sum checksum.
- The EMAY app's own Contec code (`y.java`: `83` set time, `89`, `90 06`, `8E 05`) matches this
  protocol's older, unencrypted form. The EMAY EMO-80 instead uses EMAY's `E2` protocol on the
  same service.
- Supporting it means mapping the download replies (`E0`–`ED`) and the encryption for
  version 14 and later. It's more work than the EMO-80 and needs a device to test.
