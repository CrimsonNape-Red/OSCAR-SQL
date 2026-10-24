# BMC G3X `.dat` file (internal firmware SC.76)

Status: **decoded from one card, not imported.** With the GitLab #348 changes, the EVT-only
import of this card already shows the same data as PAP-Link, so nothing is done with `.dat`
for now. These notes are kept in case a waveform import is wanted later.

## Where it appears

A G3 A20 on user-facing firmware G3-2.12.58.02, internal build G3-2.SC.76.01, writes
`<base>.idx`, `.evt`, `.log`, `.set` and `.dat`. It writes **no `.000`–`.062` waveform
files**, and every IDX record has waveStart, waveEnd and waveLen set to 0. The IDX header
still declares the old ring geometry (0xD4 = 64 MiB, 0xD8 = 63 files).

No other G3X card in the sample collection has a `.dat`.

## Layout

The file size seen was 2,080,768 bytes, which is 8,128 frames of 256 bytes. All integers
are little-endian.

### Frame (256 bytes)

| Offset | Size | Content |
|---|---|---|
| 0x00 | 4 | Signature `55 57 FF FF` |
| 0x04 | 12 | `FF` padding |
| 0x10 | 5 × 48 | Five one-second records |

Two of the 8,128 frames did not carry the signature. These are probably frames at the
write pointer.

### Record (48 bytes, one second)

| Offset | Size | Content |
|---|---|---|
| 0x00 | 6 | Timestamp: year−1900, month, day, hour, minute, second (same encoding as EVT) |
| 0x06 | u16 | Milliseconds, 0–999 (same role as EVT offset 0x1A) |
| 0x08 | 10 × int16 | Signal A, 10 Hz: probably flow |
| 0x1C | 10 × u16 | Signal B, 10 Hz: mask pressure |

### Ring behaviour

- Records are written only while therapy is running. Gaps between records match the EVT
  0x40/0x41 session markers exactly.
- The file is a ring with no header or pointer. The newest record sits just before the
  point where timestamps jump back to the oldest. On the sample card that point was in the
  middle of the file.
- Capacity is 8,128 × 5 s = 40,640 s, about 11.3 h of **therapy time**. In practice that is
  the most recent night plus part of the one before.
- Consecutive records are 1 s apart. A few steps of 0 or 2 s show up because of
  millisecond jitter.

## Signals

### B: mask pressure, hundredths of cmH2O (confident)

- 1,211 EVT 0x42 pressure snapshots were matched to the record with the same second. The
  record's 10-sample mean follows the snapshot EPAP closely (for example 728 vs 728 and
  527 vs 528), and runs slightly higher while APAP pressure is rising.
- The value is 400 (4.00 cmH2O) at session start, matching the 0x42 snapshot written with
  the 0x40 marker.

### A: probably flow (unconfirmed)

- Signed, with a range of −742 to 1800 and a median of about 354.
- Clear breath shapes with a period of about 5 s.
- The baseline sits around +350 rather than 0. One explanation is that it is total flow,
  including vent leak, in 0.1 L/min. Confirming this needs a PAP-Link flow graph for the
  same night.

## If an import is ever added

- Detection: `.dat` frames start with `55 57 FF FF` at 256-byte boundaries.
- Order records by timestamp rather than file position, because the ring wraps.
- Assign records to EVT-only sessions by time. Only nights still inside the ~11 h window
  would get waveforms. Older nights stay EVT-only.
- The waveform channels would be flow and mask pressure at 100 ms intervals. This is lower
  resolution than the `.000` packets, which carry 100 flow samples per second.
