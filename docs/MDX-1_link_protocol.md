# MDX-1 phone link protocol, version 1

Firmware: `firmware/core/link/link.h`, `link.c`. App: `app/ios/MDX1/Link/LinkProtocol.swift`.
Transport on the detector: the BM83 Bluetooth audio module's BLE **Transparent UART** service
(`firmware/stm32/bm83.c`). The same bytes would work over USB CDC or any serial link.

## Frame

```
A5  len  type  seq  payload[len]  crc_hi  crc_lo
```

* `len` payload length, 0..240.
* `type` request type (below). The detector answers with `type | 0x80` and the same `seq`.
  Unsolicited frames (status stream, events) have `seq = 0`.
* `crc` CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, check value of "123456789" = 0x29B1)
  over `type seq payload`, transmitted big-endian.
* Every multi-byte payload field is little-endian. Strings are NUL-padded fixed fields unless
  marked `z` (NUL-terminated, variable length).

Errors: type `0xFF` (= `0x7F | 0x80`), payload `[failed type][code]` with codes
1 bad CRC, 2 not authenticated, 3 wrong PIN, 4 bad index, 5 bad value, 6 bad length, 7 unknown, 8 busy.

## Requests

| type | name | request payload | response payload |
|---|---|---|---|
| 0x01 | HELLO | `ver u8, pin u16` | `ver u8, auth u8, fw[8], hw[4], nitems u8, nfinds u16, status_len u8, name[12]` |
| 0x02 | ITEM | `idx u8` | `idx u8, is_action u8, lo i16, hi i16, nlabels u8, name z, labels z…` |
| 0x03 | VALUES | – | `n u8, value i16 × n` (actions report 0) |
| 0x04 | SET | `idx u8, value i16` | `idx u8, value i16` (as applied) |
| 0x05 | ACTION | `id u8, args…` | `id u8, result u8` (0 = done, else error code) |
| 0x06 | STATUS | `period_ms u16` (0 stop, 0xFFFF one-shot, else stream ≥ 50 ms) | status packet (below), also streamed with `seq 0` |
| 0x07 | FIND | `idx u16` (0 = newest) | `idx u16, utc u32, lat i32 (1e-7°), lon i32, id i16, fe u8, depth_cm u8, conf u8, name[12]` |
| 0x08 | LEARNED | – | `n u8, {idx u8, name[11], id u8, fe u8} × n` |
| 0x09 | DIAG | – | `21 × f32: VSYS 3V3 5VA −5VA 12V VTX VBRIDGE Itx_A temp_C noise_floor thresh ground_a ground_b ground_phase coil_L_mH coil_R f1…f5 ; periods u32, pi_records u32, uptime u32, flags u8 (1 tx fault, 2 clipped, 4 coil present, 8 calibrated), coil_name[12]` |
| 0x10 | EVENT | (detector → phone only) | `event u8, a u8, b u16`: 1 noise cancel done (a = channel M, b = noise µΩ), 2 find (b = count), 3 low battery (a = %, b = mV), 4 wizard step (a = step, 0xFF = closed), 5 coil changed, 6 settings changed |

Everything but HELLO is refused with *not authenticated* until a HELLO with the right PIN has
been answered. The PIN is menu item "Phone PIN" on the detector (factory 1234). A BLE drop
clears the authentication; the phone must HELLO again.

### Actions

1 noise cancel · 2 ground grab · 3 pinpoint on · 4 pinpoint off · 5 learn (`args` = name, up to
11 bytes; needs a target under the coil) · 6 forget (`args` = learned index) · 7 mark find ·
8 save settings · 9 coil wizard start · 10 wizard step · 11 wizard cancel · 12 power off ·
13 factory defaults (keeps the PIN) · 14 accept all IDs.

### Menu from the item table

The phone never hard-codes the settings. It asks `HELLO` for `nitems`, then `ITEM 0..n-1`.
Each item has a name, a range, optional value labels (then the phone shows a picker) and an
`is_action` flag (then it shows a button and uses `SET idx 0` or the matching ACTION). The
item order is the detector's own menu order, so the app's Settings tab is the device menu.

## Status packet (55 bytes)

| off | field | unit |
|---|---|---|
| 0 | flags | 1 present, 2 accepted, 4 ferrous, 8 pinpoint, 16 charging, 32 low battery, 64 coil present, 128 noise cancel running |
| 1 | cond_id i16 | ×0.1 |
| 3 | fe_index u8 | 0..100 |
| 4 | confidence u8 | % |
| 5 | strength i8 | dB over threshold |
| 6 | depth u8 | cm |
| 7 | signal u8 | 0..255 (pinpoint level when pinpointing) |
| 8 | ground_phase i16 | ×0.1° |
| 10 | ground_strength u8 | mΩ |
| 11 | salt u8 | % |
| 12 | hot_rock u8 | ×0.01 |
| 13 | noise_M u8 | period length (channel) |
| 14 | noise u16 | µΩ |
| 16 | itx u16 | mA rms |
| 18 | vtx u8 | ×0.1 V |
| 19 | batt u8 | % |
| 20 | batt u16 | mV |
| 22 | batt_hours u8 | ×0.1 h |
| 23 | spectrum u8[5] | ×1/255, lines 5/10/15/20/41 kHz |
| 28 | gnss_valid u8 | |
| 29 | lat i32 | 1e-7° |
| 33 | lon i32 | 1e-7° |
| 37 | sats u8 | |
| 38 | hdop u8 | ×0.1 |
| 39 | finds u16 | this session |
| 41 | pi_depth u8 | cm |
| 42 | match_idx i8 | learned-target index, −1 none |
| 43 | program u8 | |
| 44 | mode u8 | |
| 45 | sensitivity u8 | |
| 46 | uptime u32 | s |
| 50 | pinpoint_level u8 | ×1/255 |
| 51 | swing u8 | ×0.1 m/s |
| 52 | pi_target u8 | |
| 53 | wizard_step u8 | 0xFF when the wizard is not open |
| 54 | screen u8 | detector screen id |

A captured one-shot from the firmware bench is in `firmware/build/link_status_fixture.hex`
and is decoded by the app's unit test (`MDX1Tests/LinkProtocolTests.swift`).

## BLE details

Service `49535343-FE7D-4AE5-8FA9-9FAFD205E455`; phone writes to
`49535343-8841-43F4-A8D4-ECBE34729BB3`, subscribes to `49535343-1E4D-4BD9-BA61-23C647249616`.
Frames may be split across notifications; both ends use a streaming parser. The firmware
chunks outgoing frames into 20-byte notifications. Advertised name `MDX-1`.

The BM83 host-MCU command opcodes used in `bm83.c` are marked VERIFY and must be checked against
Microchip's BM83 UART command set document at bring-up; only that table should need changing.
