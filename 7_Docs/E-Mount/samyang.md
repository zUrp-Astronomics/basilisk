# Samyang E-mount lenses — what the body sees

**Date** : 2026-10-07
**Dernière révision** : 2026-10-10
**Statut** : actif — third interoperability document of `7_Docs/E-Mount/` (ticket #521); only the Samyang AF 135 F1.8 FE has been on the bench: every fact on the eleven other models is static analysis
**Référencé par** : `7_Docs/E-Mount/protocol.md` (§ 0, § 4.1, § 6.1, § 6.2, § 7.1, § 7.4, § 7.4.2, § 7.8, § 7.13, § 7.15, § 7.18, § 7.20, § 10, § 13), `7_Docs/E-Mount/README.md`, `traces.md`, `provenance.md`, the comments of `4_Firmware/components/` and `4_Firmware/include/`, `7_Docs/PROTOCOL.md` § 3.1, `CLAUDE.md` § Interdits (ticket #558); the fake AF 135 and the tests of `4_Firmware/sim/` (ticket #563)
**Dérivé de** : `7_Docs/E-Mount/protocol.md`; the captures of the Samyang AF 135 (`4_Firmware/traces/`, and the bench captures published as `7_Docs/E-Mount/traces/`); the code of this repository (`4_Firmware/components/session/`, `4_Firmware/components/bench_core/bench_core.c`, `4_Firmware/sim/lens135.c`, `4_Firmware/sim/sources135.c`, `5_App/tool_station/`, `7_Docs/PROTOCOL.md`); the project's own static analyses of twelve Samyang lens firmwares, which are not published

## 0. What this document is

**Scope.** What a Sony E-mount body sees of a Samyang lens, beyond what `protocol.md` says for every
maker: the twelve models the project has read (§ 1), the session automaton of the AF 135 as the
body sees it (§ 2), homing (§ 3), focus moves, their acknowledgements and bounds (§ 4), the ring and
the barrel switches, and what the lens reads of the body (§ 5), the service channel `0x40` (§ 6),
the writes to the lens's non-volatile memory, model by model and path by path (§ 7), how each model
departs from the AF 135 and from `protocol.md` (§ 8), and what our board does with a Samyang (§ 9).
With this file and `protocol.md`, one should be able to write the Samyang part of a body-side
driver, and a fake Samyang that answers with the decoded fields given here.

**`protocol.md` first.** The frame, the classes, the message sizes, the stream layouts, the
encodings and the common meaning of each message are in `protocol.md` and are not repeated. This
document uses its conventions: offsets counted after the type byte, sizes with the type byte, B→L
and L→B, little-endian 16-bit fields (`protocol.md` § 0). It stops at the mount, as `protocol.md`
does: the inside of a lens appears only where it decides what the body sees.

**One Samyang has been on the bench.** The Samyang AF 135 F1.8 FE, firmware 1.05, against earlier
firmwares of this board (`4_Firmware/traces/`) and against its firmware 1.0 (`traces/samyang.txt`).
The bench captures cited as `` `traces/<file>` line <n> `` are relative to `7_Docs/E-Mount/` and are
published by the `traces.md` ticket (`protocol.md` § 0). The firmware the project decompiled for the
135 is version 1.06: where a capture of the 1.05 and the 1.06 analysis disagree, the capture wins,
and the disagreement is written (§ 3.2). The eleven other models have never been connected.

**Order of authority** (decision of the project owner, 2026-10-07):

1. **what a trace shows**: for the AF 135 only;
2. **what the code of this repository does**: the board's code, which drives the AF 135 on the
   bench (`4_Firmware/components/`), and the fake AF 135 of the simulation (`4_Firmware/sim/lens135.c`,
   whose byte templates are copied from the traces, `4_Firmware/sim/sources135.c`, `g_src135`). This
   code was written from our analyses and from the traces; where it is contested, this document says
   so and does not correct it (§ 9.3, and `protocol.md` § 11);
3. **our static analyses**, marked **static analysis, not observed**. For the eleven models other
   than the AF 135 this is the only source; their sections say so at their head.

**Proof.** Every statement carries one of: a capture line, `` `4_Firmware/traces/<file>` line <n> ``
or `` `traces/<file>` line <n> ``; a file of this repository with its function or constant; a public
URL; or the words *static analysis, not observed*. Where a paragraph or a table is entirely static,
its heading says so once.

**What is not published.** The firmwares and their decompilations stay private. A Samyang answers
some requests with constant blocks of its flash or with values computed from calibration tables (a
distance per focus position, an aperture per ring step, the constant byte of the `'M'` reply, the
blocks of `0x3B`). **Their raw bytes are not published.** What is published: the decoded fields
(meaning, offset, and the value where it has a meaning of its own: an identifier, a flag, a size, a
version, a bound in motor steps, the name in clear), the sizes of the replies, and the masks
described field by field. Bytes we have not decoded are declared **not published (extracted from
firmware, meaning unknown)**. The bytes the AF 135 sent in **our captures** are not concerned: they
are our observations of the wire, and they are cited where they are.

**Three cautions on the material** (static analysis, not observed):

- the analyses of six models (AF 135, V-AF 24, AF 35-150, AF 16, AF 14 on PIC32 and on STM32) were
  made partly by delegated readers whose citations were not all reread; the AF 135 and the AF 14 on
  STM32 were then read as complete automatons (§ 2, § 8.1);
- the analyses of the six newer models (AF 24 in two hardware revisions, AF 75, AF 85 II, AF 35 P,
  AF 14-24) enumerate every writer of non-volatile memory and trace their callers; the limit of that
  proof is in § 7.3;
- the firmware images do not contain the boot loaders: what a lens does once it has jumped to its
  boot loader is unknown on all twelve.

---

## 1. The twelve models

### 1.1 Identity

Static analysis, not observed, except the AF 135 row (captures cited) and the ExifTool column.

| Model | Firmware read | Microcontroller | LensType2, `0x07` offsets 9-10 | `0x3F` name (offsets 1-64) | ExifTool name for this LensType2 |
|---|---|---|---|---|---|
| AF 135 F1.8 FE | 1.06 (bench lens: 1.05) | PIC32 | 8 | `SAMYANG AF 135mm F1.8` | none (ExifTool lists the 135 under 51518) |
| V-AF 24 T1.9 | 1.06 | PIC32 | 12 | `SAMYANG VAF 24mm T1.9`, or `SAMYANG ANA 24mm T1.9` with an anamorphic accessory | none |
| AF 35-150 F2-2.8 | 7.03 | STM32 | 13 | `SAMYANG AF 35-150mm F2-2.8` | Samyang AF 35-150mm F2-2.8 |
| AF 16 F2.8 | 3.02 | STM32 | 23 | `LK SAMYANG AF 16mm F2.8 P FE` | none |
| AF 14 F2.8 (STM32, "14 A" below) | 3.27 | STM32L451 | `0xC931` = 51505 | `SAMYANG AF 14mm F2.8` | Samyang AF 14mm F2.8 or Samyang AF 35mm F2.8 |
| AF 14 F2.8 (PIC32, "14 P" below) | 2.27 | PIC32 | `0xC931` = 51505 | `SAMYANG AF 14mm F2.8` | the same |
| AF 24 F1.8 (two files, "24" and "24 A") | 2.05 and 3.05 | PIC32 | `0xC93A` = 51514 | `SAMYANG AF 24mm F1.8` | Samyang AF 24mm F1.8 |
| AF 75 F1.8 | 1.07 | PIC32 | `0xC938` = 51512 | `SAMYANG AF 75mm F1.8` | Samyang AF 75mm F1.8 |
| AF 85 F1.4 II | 1.03 | PIC32 | 9 | `SAMYANG AF 85mm F1.4 II` | none |
| AF 35 F1.4 P FE | 1.02 | STM32L451 | 20 | `SAMYANG AF 35mm F1.4 P FE` | Samyang AF 35mm F1.4 P FE |
| AF 14-24 F2.8 | 1.03 | STM32L4 | 21 | `SAMYANG AF 14-24mm F2.8` | Samyang AF 14-24mm F2.8 |

- **The AF 135, on the wire**: LensType2 `08 00` = 8 (`4_Firmware/traces/sy135-2026-09-20-full.txt`
  line 12); the name `SAMYANG AF 135mm F1.8`, padded with zeros (`traces/firmware2_ring.txt`
  lines 17-18); the version 1.05, in the `0x07` and in the `'V'` reply of the service channel
  (`4_Firmware/traces/sy135-2026-09-20-full.txt` lines 12 and 43).
- **The version** is the `'V'` reply of the service channel (§ 6.3), two bytes, major then minor.
  The AF 135 also puts it at `0x07` offsets 5-6, high byte first (`01 05` = 1.05, `protocol.md`
  § 7.7); so do the 24 (`02 05`), the 24 A (`03 05`) and the 14 A (`03 27`) (static analysis, not
  observed). The first digit names the electronic generation on the 14 and 24 pairs: 2.xx for the
  original board, 3.xx for the new one (static analysis, not observed).
- **ExifTool** names: `https://exiftool.org`, `Sony.pm`, `%sonyLensTypes2`; the generated copy in this
  repository is `4_Firmware/components/host/lens_names.c`, table `T`. LensType2 8, 9, 12 and 23 are
  not in it. The ExifTool code 32823 ("Sony FE 85mm F1.4 GM or Samyang AF 85mm F1.4") is not what
  the AF 85 II sends.
- **The microcontroller family** matters to the body in one place: the STM32 generation of the 14
  differs from its PIC32 twin and from the AF 135 in the handshake, the homing replies, the `0x0A`
  and the sleep (§ 8.1).

### 1.2 Recognising a Samyang

Static analysis, not observed, except where a trace or the code is cited.

- **`0x07` offsets 1-2 identify no maker.** All twelve send `03 70`, and so does the Tamron F051
  (`protocol.md` § 7.7). The AF 135 sends it on the wire (`4_Firmware/traces/sy135-2026-09-20-full.txt`
  line 12).
- **The `0x3F` name contains `SAMYANG` on all twelve**; the AF 16 prefixes it with `LK `, so a rule
  "starts with SAMYANG" misses it. The six older models serve the `0x3F` in every state (§ 2.1); on
  the six newer ones the name does not vary with the state of the lens.
- **LensType2 collisions.** The 14 A and 14 P share `0xC931`, and ExifTool also gives that code to an
  AF 35 F2.8 the project has not read. The 24 and 24 A share LensType2, name and internal design;
  only the version separates them. No LensType2 of the twelve is that of the Tamron F051 (`0xC134`)
  or the Sony FE 24-105 G (`0x8025`).
- **Our board's rule** (`4_Firmware/components/session/init.c`, `init_recognize()`): a name that
  starts with `SAMYANG ` or `LK SAMYANG `, or a LensType2 in the closed list 8, 9, 12, 13, 20, 21, 23,
  `0xC938`, `0xC93A`. `0xC931` is not in the list: the two AF 14 are recognised by their name only.
  The AF 135 (LensType2 8) is declared apart to `bench_core` (§ 7.4).
- **A message outside the standard set goes to a recognised Samyang only** (`CLAUDE.md` of the
  repository; `bench_core.c`, `svc_ok()`): a Sony FE 24-105 G that receives a `'V'` floods the line
  with `0x02` until its power is cut.

### 1.3 The decoded fields of the constant replies

Static analysis, not observed, except where a capture or the code is cited.

**`0x01` reply** (33 bytes).

- AF 135, on the wire: `01 FF 9F 38 5D A2 60 18 5E` followed by 24 zeros
  (`4_Firmware/traces/sy135-2026-09-20-full.txt` line 9); the types it lists are in `protocol.md`
  § 7.1.
- 14 A and 14 P (static analysis, not observed): the bitmap of offsets 0-7 lists 01-0D, 10-19, 1B,
  1C, 1D, 1F, 22, 23, 24, 26, 27, 28, 2E, 2F, 34, 35, 3A-40. Offsets 8-31: not published (extracted
  from firmware, meaning unknown). **The list names types these lenses have no size for** (0x11,
  0x12, 0x13, 0x17, 0x18, 0x23, 0x24, 0x27): a body that sends one blocks the lens (§ 2.6). The list
  is not "send it to me", as `protocol.md` § 7.1 says for the AF 135.
- The ten other models: not read.

**`0x07` reply** (35 bytes).

| Offset | Field | AF 135 (`4_Firmware/traces/sy135-2026-09-20-full.txt` line 12) | Other models (static analysis, not observed) |
|---|---|---|---|
| 0 | `01`, a native lens (`protocol.md` § 7.7) | `01` | 14 A: `01`; others not read |
| 1-2 | `03 70` (§ 1.2) | `03 70` | `03 70` on all eleven |
| 3-4 | unknown | `01 00` | 14 A: not published (extracted from firmware, meaning unknown) |
| 5-6 | firmware version, high byte first | `01 05` | 24 `02 05`, 24 A `03 05`, 14 A `03 27`; others not read |
| 7 | unknown | `00` | 14 A: not published (extracted from firmware, meaning unknown) |
| 8 | weiziqian's `A0` on every device (`protocol.md` § 7.7) | `00` | 14 A: `A0` |
| 9-10 | LensType2 (§ 1.1) | `08 00` | § 1.1 |
| 11-14, 19-33 | unknown | zeros | not published (extracted from firmware, meaning unknown) |
| 15-18 | the constant common to every lens (`protocol.md` § 7.7) | `60 92 86 5E` | not read |

- The AF 75 builds offset 1 with a bit 7 inherited from RAM whose value is not established; the six
  newer models build the reply once, at start-up (static analysis, not observed).
- Our board reads offsets 0-1 and 9-10 (`4_Firmware/components/session/lens_rx.c`, `lens_rx_id()`).

**`0x08` reply** (202 bytes). AF 135, on the wire: offsets 0-3 `BF 11 00 19`, f/1.8 to f/22.6
(`4_Firmware/traces/sy135-2026-09-20-full.txt` lines 15-18; `protocol.md` § 7.8); the 198 other bytes
are in the same lines, meaning unknown. One condition changes the reply (static analysis, not
observed): a body that announces itself with the `0x01` signature of § 5.3 named after the a6000,
while the switch position is configured APERTURE at that moment, gets the narrowest aperture equal to
the widest (offsets 2-3 = offsets 0-1) and three other fields changed (offsets 20, 25, and the low
nibble of 26). Other models: not read.

**`0x09` reply** (12 bytes). AF 135: eleven zeros (`4_Firmware/traces/sy135-2026-09-20-full.txt`
line 23). On the six newer models the handler only answers; its content is not published (static
analysis, not observed). On the V-AF 24 the handler also reloads an accessory flag from its EEPROM
(§ 7.2, note 4).

**`0x0B` reply** (3 bytes): `0B <offset 0 of the request> <x>`. AF 135: `x` = `00`, `0B 60 00`
(`4_Firmware/traces/sy135-2026-09-20-full.txt` line 21). 14 A and 14 P: `x` is not the 135's `00`;
its value is not published (extracted from firmware, meaning unknown) (static analysis, not observed).

**`0x0D` reply**: `0D 01` (`4_Firmware/traces/sy135-2026-09-20-full.txt` line 25; 14 A: static
analysis, not observed). What the request selects: § 2.4.

**`0x10` reply**: `10 00` (`4_Firmware/traces/sy135-2026-09-20-full.txt` line 35). It is `10 00`
whatever the homing's outcome (§ 3).

**`0x3F` reply** (66 bytes): offset 0 = `00`, the name of § 1.1 from offset 1, padded with zeros
(`traces/firmware2_ring.txt` lines 17-18 for the AF 135). On the six newer models the name is not
terminated, and the rest of the 66-byte buffer is zero on the 24 A, 85, 35 P and 14-24, and not
written by any code on the 24 and 75 (static analysis, not observed). A reader stops at the first
zero or at offset 64, as our board does (`lens_rx.c`, `name_3f()`).

**The `0x3B` blocks** (L→B, 492 bytes, class 3): after a `0x3A` whose offset 0 was zero, the AF 135
copies one of seven 488-byte blocks of a table, chosen by the `0x3B` request's offsets 0-1; the 14 A
has six such blocks. The blocks' content is not published (static analysis, not observed). Our board
sends neither `0x3A` nor `0x3B`.

---

## 2. The AF 135 session, seen from the mount

Static analysis of the 1.06 firmware, not observed, except where a capture or the code is cited.
The 14 A, read the same way, keeps this automaton with the departures of § 8.1; the other models
have not been read as automatons. The fake AF 135 implements this section
(`4_Firmware/sim/lens135.c`, `l135_vd()`, `l135_byte()`, `l135_body_cs()`, `l135_advance()`, and
the simplifications S1 to S15 in its header).

### 2.1 Three machines, and no guard

- **No message is guarded by a phase.** Every type the lens has a size for is served in every state:
  an init message while it streams, a `0x1D` while it does not. The AF 135 never refuses a message and
  never sends a `0x02` (`protocol.md` § 7.2); nor does the 14 A (§ 8.1). The capture of a lens left
  powered shows an init served while it was streaming: 29 `0x05` were received during that init
  (`4_Firmware/traces/sy135-2026-09-20-full.txt` lines 8-41).
- **The link** carries the only real changes of state: handshake at power-up, normal, 1.5 Mbaud
  switch, shutdown, sleep, wake-up handshake (§ 2.2).
- **The stream** is a switch that decides whether the lens *sends* its `0x05` and `0x06`; it decides
  nothing about what it accepts. Only the `0x0A` (§ 2.3) and the sleep that follows a `0x16` write
  it. It is off at power-up.
- **The processing** (homing, focus orders, iris) runs on events the body gives: VD edges, `0x03`,
  `0x04` (§ 2.4).
- **At power-up** every session variable is zero: stream off, no homing done (positions not
  referenced until a `0x10` has run), no focus order, no acknowledgement pending, 750 000 baud,
  service mode off.

### 2.2 The link

- **Handshake.** `protocol.md` § 2, steps 1 to 5. The AF 135 runs its loop, and so starts the
  handshake, about 20 ms after power-up. **Before step 3 the AF 135 cannot receive
  anything**: its UART is opened there. The 14 A opens its UART before the handshake (§ 8.1).
- **A lens left powered never shakes hands again**: the body's handshake gets no `LENS_CS`, and the
  `0x01` is answered all the same (`4_Firmware/traces/sy135-2026-09-20-full.txt` lines 7-9;
  `4_Firmware/traces/sy135-2026-09-20-reboots-0x10-stall.txt` line 108). Nothing brings a powered lens
  back to the handshake but the sleep that follows a `0x16`.
- **`0x0C`**: `0C 01` at once, then a second short CS handshake, then 1.5 Mbaud **until the power is
  cut**, a wake-up from sleep included (`protocol.md` § 5).
- **`0x16`**: the handler does not answer itself. A shutdown task takes the iris to a rest position,
  waits at least 128 ms then for both motors to stop, **with no deadline** (a 1.5 s error branch exists
  and cannot run), answers `16 00`, and 16 ms later goes to sleep: stream off, buffers emptied, UART
  closed. It then waits for one of its input lines to be low, waits 50 ms without reading it again,
  and requires it still low; the peripherals are reset, **not the RAM**, and the lens waits for the
  wake-up handshake, at the speed it had. Which mount contact that input line is, is not established.
  No write to non-volatile memory is part of these steps (§ 7.2). `protocol.md` § 7.15 lines
  1082-1086 point here.
- **Throughout**, the dispatcher keeps running: a `0x0C` or a `0x16` received during another
  transition rewrites the link state.

### 2.3 The `0x0A`: layout, motors, and the stream switch

A task serves it in two steps:

1. outside slots 1 and 2 of the frame (§ 2.4): if focus or iris moves, **the `0x1D` order in progress
   is cancelled without acknowledgement** and both motors are asked to stop;
2. once the transmitter is free and the motors stopped, **200 ms at most**: the stream is switched
   off, the layout of the `0x05` and `0x06` is **recomputed from the masks at every `0x0A`**, the
   reply — **the request, echoed** — leaves in class 2, and the stream is switched back on if any of
   the 16 mask bytes is non-zero; with all masks zero it stays off and pending `0x05`/`0x06` are
   forgotten.

- On the wire: the echo 6 ms after the request (`4_Firmware/traces/sy135-2026-09-20-full.txt` lines
  38-39); 97-byte `0x05` and 40-byte `0x06` for the masks `FF 7F … 3F`
  (`4_Firmware/traces/sy135-2026-09-20-dump05.txt` lines 5 and 11). Before any `0x0A` the `0x05` is
  109 bytes (static analysis, not observed).
- **A `0x0A` during a homing stops the reference movement in progress**; the homing step that waited
  for it goes on as if it had finished. What that does to the reference is not established.
- **A second `0x0A` during the first restarts step 1.**
- **Consequence for a body**: a `0x0A` mid-session stops the focus, drops the acknowledgement of the
  current `0x1D` and suspends the stream; it does not toggle anything as on a Tamron
  (`tamron.md` § 2.4). Our board sends it once per session, and once more only as the soft reset of a
  silent `0x01` (`init.c`, comment of `idempotent()` and of `INIT0A`).

`protocol.md` § 7.10 lines 1022-1024 and § 13 line 1361 ("`0x0A` stopping the motors") point here.

### 2.4 What paces the lens: VD, slots, and the `0x03`/`0x04` pair

- **VD.** A falling edge of VD, in interrupt: starts the movements that are ready (focus and iris
  movements start only here, with one exception below); restarts the slot timer at slot 0; in stream,
  adds one to the stream sequence. **The lens does not measure the VD period.**
- **Slots.** The slot timer cuts the frame into eight slots, then stops until the next VD. Its period
  is set by the `0x0D` (below): an eighth of a frame at the declared rate, 2083 µs at 60 Hz.
  - slot 0: a `0x03` received since the last slot 0 makes a `0x05` due;
  - slot 1: a `0x04` received makes a `0x06` due;
  - slot 3: iris processing, **when the stream is off**;
  - slot 4: frame processing (focus homing, focus orders, focus planner), **when the stream is off**.
- **The pair.** Each `0x03` received triggers the iris processing, in every state; each `0x04`
  received triggers the frame processing, in every state.
- **The stream is a reply**: **one `0x05` per `0x03` received and one `0x06` per `0x04` received**,
  sent at the next slot 0 or 1, and only while the stream is on. No `0x03`, no `0x05`; no VD, no slot,
  hence no `0x05`/`0x06` at all. A `0x03`/`0x04` that arrives after slots 0 and 1 of a frame waits for
  the next VD. `protocol.md` § 6.2 line 501 and § 13 line 1361 ("stream cadenced by the pair") point
  here.

| Situation | Iris, iris homing | Focus homing, focus orders | Movements |
|---|---|---|---|
| stream off, VD present | every frame (slot 3) | every frame (slot 4) | start at the next VD |
| stream off, no VD | at each `0x03` only | at each `0x04` only | **never** (one exception below) |
| stream on, `0x03` and `0x04` every frame | at the `0x03` | at the `0x04` | start at the next VD |
| stream on, no `0x03`/`0x04` | **stopped** | **stopped** | one already started runs to its end; nothing new |

- **The exception**: step 4 of the iris homing starts its motor directly, without VD; it is reached
  by the iris processing, so by a `0x03` even without VD. `protocol.md` § 1.3 says "without VD it
  never starts": true of every other movement.
- **A movement started runs to its target without further VD.**
- **The `0x0D`** (offset 0, bits 0-1): 0 = 60 Hz, 1 = 50 Hz, 2 = 48 Hz, 3 = 60 Hz; there is no 30 Hz.
  It sets the slot period, and the period the lens uses to time its steps and to forecast its
  position. A real VD at another rate than the one declared is not detected: the step rate does not
  change, only the frames do. Before any `0x0D` the slots have the 60 Hz period. `protocol.md` § 7.13
  line 1052 and § 13 line 1361 point here.
- **A 120-VD cap on focus movements.** While bit 1 of the last `0x04`'s offset 3 is set (and it is
  taken as set before any `0x04`), a focus movement is cut after 121 VD, about 2 s at 60 Hz; while it
  is clear, an overshoot check runs at each VD instead. `protocol.md` § 7.4 line 620 ("read when a
  movement is stopped") points here.

### 2.5 Replies, and the transmit lock

- **There is no reply state.** Each reply leaves through one sender, from its handler or later from a
  task. Its class is that of its first message's entry in the lens's emission table; a class 2 or 3
  frame leaves with sequence 0 and resets the stream sequence to 0 (`protocol.md` § 3.4).
- **The lock.** A class 2 or 3 frame waiting to leave locks the sender until its last byte is out,
  and that frame first waits for BODY_CS low. **While it is locked, any other class 2 or 3 reply is
  dropped, silently and for good.** Hence:
  - **two init messages in one frame get one reply**, the first's;
  - a `0x40` reply or notification pending makes a standard reply be dropped, and the reverse (§ 6.1);
  - **a BODY_CS that never goes low stops every reply**, and the `0x05`/`0x06` too, which are built
    only while the sender is free;
  - class 1 frames (`0x05`, `0x06`, the immediate `1E 00`) do not set the lock but are refused while it
    is set.
- **Deferred replies**:

  | Reply | When | On the wire |
  |---|---|---|
  | `0A …` | motors stopped, 200 ms at most (§ 2.3) | 6 ms (`4_Firmware/traces/sy135-2026-09-20-full.txt` lines 38-39) |
  | `10 00` | end of the homing (§ 3) | 527 to 757 ms for `10 1F` (§ 3.1) |
  | `16 00` | 128 ms at least, then both motors stopped, no deadline (§ 2.2) | not captured |
  | `34 …` | at the next frame processing, as soon as the move is launched (§ 4.6) | not captured |
  | `40 …` | 50 ms at least after the command; for some commands, after the motor stops (§ 6.1) | 61-62 ms (`4_Firmware/traces/sy135-2026-09-20-full.txt` lines 42-43, 178-187); 141 ms for an `'F'` `FB` that moved the focus (lines 387-393) |
  | `17 xx yy` | an internal failure (§ 3.2, § 4.4) | not captured |

- **Immediate replies** (all other types): 5 to 6 ms, 9 ms for the 202 bytes of the `0x08`
  (`4_Firmware/traces/sy135-2026-09-20-full.txt` lines 8-25). These delays are those of a lens in
  service mode (§ 6.5) against an earlier firmware of this board; they say nothing of a Sony body.
- **Transmission**: the lens waits for BODY_CS low, raises LENS_CS, sends, waits more than 200 µs,
  lowers LENS_CS (`protocol.md` § 1.4).

### 2.6 Bad frames and unknown types

- **A bad frame is dropped in silence**: start byte missing, length under 9, length high byte not
  zero (over 255 bytes), class 0 or over 3, wrong checksum, last byte not `55`; bytes received while
  BODY_CS is low are never stored. No reply, no state changed. The parser resynchronises only after
  the announced length (`protocol.md` § 3.2).
- **The body cannot tell** a lost frame from a reply dropped by the lock, a lens at 1.5 Mbaud or a
  blocked lens: in every case nothing comes back.
- **A type the lens has no size for blocks it until its power is cut.** The dispatcher adds the size
  of each message and stops when the count equals the frame length; a size of zero leaves it looping
  on the same byte, the background loop never runs again, and only the interrupts go on. The
  watchdog is disabled by the PIC32 configuration word. Types with no size: `0x02`, `0x05`, `0x06`,
  `0x0E`, `0x0F`, `0x11`-`0x13`, `0x17`, `0x18`, `0x1A`, `0x20`, `0x21`, `0x23`-`0x25`, `0x27`,
  `0x29`-`0x2D`, `0x30`-`0x33`, `0x36`-`0x39`, `0x41`-`0x4A`. **`0x02`, `0x05` and `0x06` are types
  the lens sends itself.** Type 0 and types at or above `0x4D` read a size outside the table, from
  bytes that change while the lens runs: a block, or a size and a copy address that are arbitrary;
  which, is not established. `0x4B` and `0x4C` have a size (3 and 8) and no handler: consumed and
  ignored. No trace shows a block.
- **A message of the wrong size** shifts the paving; a shift that crosses the end of the frame
  without landing on it makes the dispatcher read bytes beyond the frame until a size of zero.
- `protocol.md` § 4 lines 316-318 and § 7.20 point here.

### 2.7 No session timeout; a lens left powered

- **Nothing in the lens reacts to a silent body**: no timeout brings it out of the stream, back to
  the handshake or to sleep. With the stream on and no `0x03`/`0x04`, it sends nothing and processes
  nothing; it waits.
- **Facing a full init while it streams** (a body that restarted without cutting its power):
  the handshake gets nothing; every init message is served; the `0x10` starts a homing that stalls
  without `0x03`/`0x04` (§ 3.1); the final `0x0A` stops the motors and switches the stream back on with
  the new layout. Captures: `4_Firmware/traces/sy135-2026-09-20-full.txt` lines 7-41 and
  `4_Firmware/traces/sy135-2026-09-20-reboots-0x10-stall.txt` lines 108-132.
- **Only a power cut** resets the link speed, the service mode (§ 6.5) and a blocked dispatcher.
- **"`0x01` without reply"** has been captured: the `0x01` gets nothing while the next messages are
  answered (`4_Firmware/traces/sy135-2026-09-20-reboots-0x10-stall.txt` lines 537-543;
  `4_Firmware/traces/sy135-2026-09-20-astro-normal.txt` lines 123-127). A block is excluded, since the
  lens answers afterwards. The code offers two transient causes, a reply dropped by the lock and a
  frame swallowed by a desynchronised parser; the captures do not tell which.

### 2.8 What a body can observe of the state

- `0x05`/`0x06` coming back for its `0x03`/`0x04`: the stream is on.
- The echo of a `0x0A`: that `0x0A` was served; it says nothing of the stream before.
- No reply: undecidable (§ 2.6).
- No reply publishes the state of a homing; only `10 00` marks its end, and it does not say whether
  it succeeded (§ 3).

---

## 3. Homing: `0x10`

### 3.1 The AF 135

Static analysis, not observed, except where a capture is cited.

- **Request offset 0**: bit 2 = iris then focus; bit 3 alone = focus only (`protocol.md` § 7.14).
  **Neither bit: nothing happens, and no reply ever comes.** The handler first reconfigures the motor
  driver and busy-waits about 10 ms.
- **No guard: a `0x10` during a homing restarts it from its first step.**
- **The steps**: the iris looks for its optical fork, then settles on its rest aperture; then, if
  bit 3 is set, the focus reads its fork, goes towards a stop, looks for the edge, recomputes its
  position and settles; then `10 00`. Each step has a deadline of 1.5 s, 3 s for the iris search.
  The first iris move, the iris rest move and the focus part each start after a pause of about 5 ms.
- **The focus part**, in order (the fake AF 135 plays it so: `4_Firmware/sim/lens135.c`,
  `homing_focus()`). On the lower side of its fork the lens declares its position to be the lower
  soft limit (§ 4.3), moves up 1600 steps and reads the fork again; on the upper side it declares the
  upper soft limit and moves down towards the lower one until the fork changes, then reads it again.
  From the upper side it declares the upper limit once more and moves towards 14623 until the edge
  of the fork; it stops there, gives its position the value the edge must read, and settles (§ 3.2).
  The edge search runs at the speed of an `'F'` `FB` move, the other moves at twice that speed. The
  value given to the edge is computed from a correction held in the lens's non-volatile memory; on
  the wire the edge reads 14834 and 14837 after a homing (`'H'` notifications,
  `4_Firmware/traces/sy135-2026-09-20-astro-normal.txt` lines 323 and 403), and the published
  position jumps to the limits during one (`4_Firmware/traces/sy135-2026-09-20-full.txt` lines 30-34).
- **The homing progresses on the pacing of § 2.4**: with the stream on, the iris steps need `0x03`
  and the focus steps `0x04`; with the stream off, VD edges. **A lens that streams and receives no
  pair stalls its homing.** Three captured sessions fit it: a lens left powered, a `10 1F` sent
  during an init, no `10 00` in 8 s, one `0x05` received by the board in all that time
  (`4_Firmware/traces/sy135-2026-09-20-reboots-0x10-stall.txt` lines 128-132, 259-263, 500-504); what
  the board sent meanwhile (pairs, VD edges) is not in the captures. This is why our board
  starts its `0x03`/`0x04` loop with the `0x10` for a recognised Samyang, and only after the `0x0A`
  reply otherwise (`init.c`, `init_reply()`, case `S_Q0D`, `loop_start()`). `protocol.md` § 6.1 line
  472 and § 13 line 1361 ("homing in stream") point here.
- **Durations on the wire**, `10 1F` to `10 00`: 675 ms (`4_Firmware/traces/sy135-2026-09-20-full.txt`
  lines 27-35), 527 and 663 ms (`4_Firmware/traces/sy135-2026-09-20-reboots-0x10-stall.txt`, the
  sessions at lines 332-333 and 854-855), 757 ms (`4_Firmware/traces/sy135-2026-09-20-astro-normal.txt`
  lines 241-251). During it the published position sweeps the whole range, down to 13723 and up to
  30988 (`4_Firmware/traces/sy135-2026-09-20-full.txt` lines 30-34).
- **The reply is always `10 00`**, success or not. A focus homing that fails twice still answers
  `10 00`, then tries a `0x17` (lost if the `10 00` has not left yet, § 2.5). A first iris failure
  hands over to the focus homing; a second one, even in a later homing, sends `0x17` and **abandons
  the homing without any `10 00`**; the retry flags survive from one homing to the next until the
  power is cut. Each abandon also blocks the lens's background loop for 300 ms.
- **The service channel also homes** (§ 6.3): `'F'` `0x32` homes the focus only, and its end sends a
  standard `10 00` although no `0x10` was sent (`4_Firmware/traces/sy135-2026-09-20-reboots-0x10-stall.txt`
  lines 547-549, 329 ms).
- **During a homing every message is served**, the `0x0A` included, which stops the reference
  movement (§ 2.3). A `0x1D` received in stream during a homing is served at the next `0x04` like any
  order; how it interacts with the reference is not established.

### 3.2 Where the focus rests after a homing

- **Without service mode**, the 1.06 analysis puts the focus at 16384 (`0x4000`) at the end of the
  homing, before the `10 00`; **after an `'M'`** (service mode, § 6.5), at 14623 (`0x391F`), which is
  also the infinity origin of its distance table (static analysis, not observed; the fake:
  `4_Firmware/sim/lens135.c`, `POS_REST`, `POS_THRESH_LO`).
- **On the wire** both values appear: 14623 after a `10 1F` (`4_Firmware/traces/sy135-2026-09-20-full.txt`
  line 34; `traces/firmware1_ring.txt` line 44), 16384 after an `'F'` `0x32`
  (`4_Firmware/traces/sy135-2026-09-20-reboots-0x10-stall.txt` line 149). These sessions ran on a lens
  left powered by a board firmware that sent `'M'` at each boot, so the service mode was probably
  set in all of them; the 1.05 and the 1.06 may differ here, or the position has another writer. Not
  established.
- **In the astro mode** (§ 5.6) the homing poses nothing, and the lens returns to its mark on its own
  at the 61st VD after the `10 00`: on the wire, about 1.0 s after the reply, from 14550 up to 14820
  then down to the mark 14620, an overshoot of 200 steps
  (`4_Firmware/traces/sy135-2026-09-20-astro-normal.txt` lines 264-287).

### 3.3 The other models

Static analysis, not observed.

- **Same steps on the six newer models**: re-referencing on the fork, a rest move, wait for the motor,
  then the `0x10` reply: in normal mode **the rest move happens before the reply**. Rest position,
  cold / after `'M'`: 24 and 24 A 16384 / 16048 (the infinity origin of their table); 75 16384 /
  unchanged; 85 II 16384 / 15486; 35 P 15734 (its infinity) in both; 14-24 the infinity of the current
  focal length, unless a distance is memorised, with an overshoot of +500 then an approach by +300
  after `'M'`. After a sleep (`0x16`), the six newer models go back to their position before the sleep.
- **What moves after the reply** (normal mode): nothing on the 24, 24 A, 75 and 35 P; the 85 II sends
  its `0x10` **without stopping the motor** when a homing step misses its deadline a second time;
  **the 14-24 moves on its own after the reply**: its zoom follower sees a changed zoom index, and
  drives the focus to the memorised distance at the current focal length. Outside a homing, the focus
  of the 14-24 and of the 35-150 follows the zoom ring continuously.
- **In preset mode** (button held during the iris homing, 24 and 24 A only): return to the EEPROM
  mark at the 61st VD after the reply, after any homing (`0x10` or `'F'` `0x32`), about 1.02 s at
  60 Hz plus the travel.
- The 14-24 also sends its `0x10` reply, identical, when its focus initialisation fails twice: success
  and failure are indistinguishable.
- **The 14 A** (§ 8.1): an iris-only homing (`0x10` with bit 2 and not bit 3, or `'I'` `0x32`) **never
  answers**; a first focus failure retries the focus only. **The 14 P**: a focus homing that fails twice
  sends `0x17` without `10 00`.

---

## 4. Focus moves

Static analysis, not observed, except where a capture or the code is cited. The AF 135 is the
reference; § 4.2 and § 8 give the other models.

### 4.1 One flag per order

Each focus order has its own job flag, picked up at the frame processing (§ 2.4); there is no single
job overwritten by every order as on a Tamron (`tamron.md` § 4.1).

| Order | Picked up | End and acknowledgement |
|---|---|---|
| `0x1D` | computes a target, clamps it (§ 4.3), arms the move, which starts at the next VD | `1D 00` when the motor stops |
| `0x1F` | a state machine (`protocol.md` § 7.4.3) | `1F 00` at the end, or on a 1.2 s deadline |
| `0x3C` | drives to the soft limit on the side of the sign of offsets 1-2; the direction byte comes from offset 0, bits 2-3 | `3C xx 00 00` when the limit is reached |
| `0x1C` | stop, then wait for stillness | `1C 00` once still; after 1.2 s, a forced stop, a `0x17`, and `1C 00` |
| `0x34` | an absolute or relative move (§ 4.6) | its own reply `34 …`, no acknowledgement in the stream |
| `0x22`, `0x2E` | converted at once, no job touched | `22 vL vH`, `2E vL vH` after a later `0x06` |

### 4.2 `0x1D`: units refused, and the constants of bits 6-7

The flags of offset 3 have the layout of `protocol.md` § 7.4.2, on all twelve models.

| Field | Samyang (AF 135; the others where stated) |
|---|---|
| bits 0-1, unit | 0 = motor steps; 2 = the value times a constant the lens recomputes (a "depth" unit); 3 = Sony distance unit (`protocol.md` § 8.3), converted; **1 = refused**: the order ends without movement and is acknowledged **`1D 00`**, in absolute and in relative mode. In unit 3, the value `0x1C0` is treated as a special target on the 14 P, 14 A, 24, 24 A and 75; its meaning is not established |
| bit 2 | 0 = absolute, 1 = relative: target = position + the signed value ± half the oscillation amplitude, then clamped. On the STM32 models the base of a relative move is **the last target commanded**, not the encoder |
| bit 3 | wait: while offset 11 of the last `0x03` is not zero the order stays pending, frame after frame; the test is reached only when offset 10 of the `0x03` is 1 or a cycle is running. Same on the STM32 models. With our board's `0x03` (offset 11 always 0) the order would start at once (`drive.c`; `protocol.md` § 7.3) |
| bits 4-5 and offset 2 | oscillation amplitude, **in relative mode only**: scale 0 = the raw byte, 2 = scaled by the depth constant, divided by 16 or by 32 depending on the body profile (§ 5.3). The 85 II and the V-AF 24 have three scales chosen by the aperture |
| bits 6-7 | side of arrival, **absolute only**: the target is shifted by +20 steps for `01` and by −23 for `10` on the AF 135; `00` compensates 3 to 5 steps depending on the previous direction. Constants −5 / −8 on the 14 P family, ±10 / −20 / −30 on the 14 A; not read on the 35 P and the 35-150 |
| speed | **not in the `0x1D`**: offset 12 of the `0x03`, bits 3-4, with the body profile (§ 5.3). AF 135: speed `0x180` with bit 3 clear (our board never sets it), `0x100` with the a7r4 signature of § 5.3. STM32 models: offset 12 bits 3-4 = `01` → slow, otherwise fast |

- **A `0x34` changes the speed profile of later `0x1D`** (§ 4.6).
- **On the wire** the AF 135 moves on `1D lo hi 00 00`, the order our board sends (`drive.c`,
  `drive_goto()`), and stops a few steps from the target: 15359 → 15354, 25726 → 25731, 26995 → 27000,
  15354 → 15349 (`traces/samyang.txt` lines 36-38, 47-49, 76-78, 85-87), 24384 → 24381
  (`traces/firmware2_ring.txt` lines 42-44). The compensation of bits 6-7 = `00` is the probable
  cause; it is not modelled by the fake (`lens135.c`, header, S6).

`protocol.md` § 7.4.2 lines 661-662 point here.

### 4.3 Bounds and limits

- **Internal soft limits** of the AF 135: 13723 and 30988 at start-up. Every target of a `0x1D`,
  `0x1F`, `0x3C` or `0x34` is **clamped silently** into them. On the wire the homing reaches both
  (`4_Firmware/traces/sy135-2026-09-20-full.txt` lines 30 and 32), and a focus moved by the ring stops
  at 13723 with `0x06` offset 0 = `52`, bit 6 set (`traces/195_samyang.txt` line 186).
- **Published limits**: `0x06` offsets 7-10 read **13873 and 30738** (`4_Firmware/traces/sy135-2026-09-20-dump05.txt`
  line 11; `traces/samyang.txt` line 38): 150 steps inside the internal lower limit and 250 inside the
  upper one. The lens accepts targets between the published and the internal limits. Our board
  narrows the published limits by 5 more steps and never targets beyond (`lens_rx.c`, `LIMIT_MARGIN`).
- **The limiter narrows the soft limits** to 17884-30988 or 13723-21386, depending on two inputs of
  the lens (a limiter selector), when the service mode is off, **bit 1 of the last `0x04`'s offset 3
  is set**, and two other internal conditions hold; otherwise they stay at their start-up values.
  `protocol.md` § 7.4 lines 619-620 point here.
- **Two other thresholds**, 14623 and 29988, drive bits 4 and 3 of `0x06` offset 0 (§ 4.8).
- **No hard stop zone**: an order towards the inside always runs.
- **Already at a limit, target beyond it**: the two analyses disagree. The AF 135 analysis says no
  movement, then `1D 00`; the 14 A analysis reads the same code on both models and finds that the
  "target = position" exit compares the target *before* clamping, so that a movement towards the
  current position is launched, with an effect not read. Not settled.
- **The other models**: § 6.3 gives the bounds of `'F'` `FB`; the 14 A and 14 P have fixed soft limits,
  3810-4909, with no limiter; the 35-150 and 14-24 bounds move with the focal length.

### 4.4 Acknowledgements and eviction

- **Place**: after the `0x06`, in the same class 1 frame (`protocol.md` § 7.6.1). On the wire:
  `1D 00` after the 40 bytes of the `0x06` (`traces/samyang.txt` lines 38, 49, 58;
  `traces/firmware2_ring.txt` line 44).
- **Three at most per frame**, in the order 1D, 1F, 22, 2E, then 1C and 3C if room is left; a fourth
  `1C` or `3C` waits for the next frame, a fourth among `1D`, `1F`, `22`, `2E` is lost. Several ends of
  the same kind before one `0x06` give one acknowledgement.
- **Values**: `1D 00`, `1F 00`, `1C 00`, always (the byte is written only at start-up); `3C xx 00 00`,
  `xx` bits 0-1 = the direction received. **There is no failure acknowledgement**: a refused unit is
  `1D 00` too.
- **Eviction**:

  | In progress | Received | Effect |
  |---|---|---|
  | `0x1D` | `0x1D`, `0x1F`, `0x3C`, `0x1C` | the old one is acknowledged `1D 00`, the new one takes its place |
  | `0x1D` | `0x22`, `0x2E` | nothing evicted; both acknowledgements come |
  | `0x1D` | `0x34` | the flag is untouched: the `0x1D` is acknowledged at the end of the `0x34` move |
  | `0x1D` | `0x0A` | **motors stopped, `0x1D` cancelled without acknowledgement** (§ 2.3) |
  | `0x3C` | `0x1D`, `0x1F`, `0x3C`, `0x1C` | the old one gets its acknowledgement |
  | `0x1F` | `0x1D`, `0x3C` (and a new `0x1F` on the 14 A) | **the `0x1F` is lost, without acknowledgement** |
  | `0x1F` | `0x1C` | `1F 00` |

- **A `1D 00` proves neither arrival nor success**: it may acknowledge an order replaced while the
  next one still runs. Compare the published position with the target.
- **No `0x1C` is needed between two `0x1D`.** On the wire the AF 135 receives a lone `0x1C` in a class
  1 frame from an earlier board firmware (`4_Firmware/traces/sy135-2026-09-20-full.txt` line 391).
- The 14 A acknowledges and evicts the same way (§ 8.1).

### 4.5 `0x1C`

It evicts the orders in progress (§ 4.4), stops, waits for stillness up to 1.2 s, and acknowledges
`1C 00`; past 1.2 s, a forced stop, a `0x17` and `1C 00`. A `0x1D` received in the same pass of the
lens's loop as the start of the stop is probably dropped and acknowledged `1D 00` (inferred from the
order of the calls).

### 4.6 `0x34`

- Fields: `protocol.md` § 7.18; offset 4 bounds an absolute target to offset 4 × 384 / 17 steps of the
  current position.
- Launched at the next frame processing (the next `0x04` in stream); the reply `34 …` leaves **as
  soon as the move is launched**, not when it ends.
- **It sets a mode that changes the speed profile of every later `0x1D`.** The mode is cleared only
  when a `0x05` is built while bit 4 of the last `0x03`'s offset 12 is clear; with the stream off no
  `0x05` is built and the mode stays. Our board never sends `0x34`, and its `0x03` carries bit 4 for
  30 pairs after each `0x1D` (`drive.c`, `AF_FRAMES`). `protocol.md` § 7.18 lines 1173-1174 point here.

### 4.7 `0x1F`, `0x3C`, `0x22`, `0x2E`

- `0x1F`: `protocol.md` § 7.4.3 gives the AF 135's reading of every field.
- `0x3C`: to the soft limit on the side of the sign; the acknowledgement is sent when the limit is
  reached (§ 4.4).
- `0x22`, `0x2E`: conversions between motor steps and the Sony distance unit through the lens's
  distance table (not published); `0x0700` = infinity (`protocol.md` § 7.4.4, § 8.3).

### 4.8 What the AF 135 publishes of its focus in the `0x06`

On the wire, with the masks `FF 7F … 3F`:

| Offset | Field | Captures |
|---|---|---|
| 0 | bits 0-2 = 2; bit 3 = position ≥ 29988; bit 4 = position ≤ 14623; bit 5 = at or above the upper soft limit; bit 6 = at or below the lower one | `12` at rest at 14623 (`4_Firmware/traces/sy135-2026-09-20-dump05.txt` line 11); `02` at 15354 (`traces/samyang.txt` line 38); `52` at 13723 (`traces/195_samyang.txt` line 186); `0A` at 30473 (same file, line 336). The meaning of the thresholds of bits 3-4 is not established |
| 1 | bit 1 = the position increased since the previous `0x06`, bit 2 = it decreased | `02` while the ring drives the focus up (`traces/195_samyang.txt` lines 126-146) |
| 2-3 | position, motor steps | above |
| 7-8, 9-10 | the published lower and upper limits (§ 4.3) | 13873, 30738 |
| 20-21 | the position of the previous frame, in these captures: equal to offsets 2-3 at rest; one `0x06` late while the focus moves | `traces/195_samyang.txt` lines 126 and 131 |
| 32-38 | non-zero while the focus moves, zero at rest (`protocol.md` § 7.6) | `traces/195_samyang.txt` lines 126-146 and 161 |
| 39 … | acknowledgements (§ 4.4) | `traces/samyang.txt` line 38 |

- Which sample offsets 20-21 and 32-38 carry also depends on the body profile (§ 5.3): the previous
  one with the profile of a NEX-7 or of a `0x08` without bits 1-2, the current one otherwise (static
  analysis, not observed). The body profile of the capture `traces/195_samyang.txt` is not in it.
- In the `0x05` the AF 135 sets offset 22 bit 6 while the focus moves, here moved by the ring
  (`traces/195_samyang.txt` line 124: `C0`; line 4: `80` at rest), and publishes the distance at
  offsets 20-21 (same lines). `protocol.md` § 7.5 gives the other fields.

### 4.9 The iris

- **Continuous target**: each `0x03` stores offsets 3-4 as the aperture target (offsets 5-6 are not
  read); the iris processing converts it to steps and moves if the step index changes; offset 12 bit 3
  chooses the movement profile. The movement starts at a VD (§ 2.4).
- **`0x1B` drives the iris**, at once, and answers at once with 11 bytes, before the iris arrives
  (`protocol.md` § 7.17).
- **No iris acknowledgement** in the stream.
- With the native aperture of § 5.5 and the a6000 profile only, the lens takes its target from the
  ring instead.

---

## 5. The ring, the switches, and what the lens reads of the body

### 5.1 Barrel controls, model by model

Static analysis, not observed, except where a capture is cited.

| Model | Button | Custom switch | Limiter | How the body sees them |
|---|---|---|---|---|
| AF 135 | yes | 2 positions, M1 (up) and M2 (down), configurable | 3 positions | `0x05` offset 64 bit 3, offset 62 bit 1, offset 60; `'W'` notifications after `'M'` (§ 6.4) |
| V-AF 24 | yes | yes | no | `0x05`; `'W'` 01, 02, 03 |
| AF 35-150 | two (two presets) | a 3-position switch, and a physical MF switch | not read | `0x05`; no `'W'` |
| AF 16 | not reported | yes | no | `0x05`; `'W'` 02, 03 |
| 14 A, 14 P | no | no | no | offsets 62 bit 1 and 64 bit 3 always 0; no `'W'` |
| 24, 24 A | yes | 2 positions | not found | `0x05` offset 64 bit 3, offset 62 bit 1, offset 60; no `'W'` |
| 75 | not found | 2 positions | not found | `0x05`; no `'W'` |
| 85 II | yes | 2 positions | no | `'W'` 01, 02, 03 and the `0x05` |
| 35 P | none notified | none (MF mode, LED mode and custom mode are settings in flash) | no | `0x05` |
| 14-24 | yes | AF/MF | not found | `0x05`; the zoom is read by an analogue input |

"Not found" means no input read and no string points to it: an absence seen, not proven.

- **AF 135, on the wire**: the button is `0x05` offset 64 bit 3, `08` while pressed
  (`traces/195_samyang.txt` lines 865 and 875; `traces/samyang.txt` lines 52-53); the switch is
  offset 62 bit 1, `03` in a position configured MF, `01` otherwise
  (`4_Firmware/traces/sy135-2026-09-20-dump05.txt` lines 5 and 7; `traces/195_samyang.txt` line 395).
- **The button bit** follows 16 consecutive samples of the lens's loop; the `'W'` 01 notification
  leaves on the first raw sample, in every mode.

### 5.2 The Custom switch of the AF 135, and its forcing

Static analysis of the AF 135, not observed, except where a capture, the code or `7_Docs/PROTOCOL.md` is
cited.

- **Configuration.** One byte of the lens's flash: M1 in the high nibble, M2 in the low one; each
  position 0 = APERTURE, 1 = AF, 2 = MF. **Factory value `0x10`: M1 = AF, M2 = APERTURE**; the lens
  takes this default when both of its flash pages are invalid. It is read from flash into RAM at
  power-up only (`7_Docs/PROTOCOL.md` § 3.1; `4_Firmware/sim/lens135.c`, header, S13 and S14).
- **Forcing.** At every pass of its loop, once a `0x08` has been received and the homing has put the
  lens in its normal mode, the lens **forces M1 = AF and M2 = MF in RAM** if the body profile asks for
  it (§ 5.3): the body's `0x01` carried `FF 01 00` at offsets 6-8 and no `0x08` with bit 2 cleared
  that flag since, or the `0x08` had neither bit 1 nor bit 2. **The forcing writes RAM only, and the
  flash value is not read again**: a later `0x08` with bit 2 lifts the forcing but does not bring the
  configuration back; only a power cut does.
- **Consequence on a re-init without power cut**: our `0x01` re-arms the flag, the lens forces the RAM
  between the `0x01` and the `0x08`, and the configuration is lost until the lens is unplugged
  (`7_Docs/PROTOCOL.md` § 3.1, "Après une reprise sans coupure").
- **Offset 62 bit 1** of the `0x05`: set in a position configured MF, clear in AF and APERTURE, clear
  outside the normal mode; forced to 1 in the astro mode (§ 5.6); also set at start-up if the button is
  held. **AF and APERTURE cannot be told apart by offset 62**: only by what moves when the ring turns,
  offset 60 in AF, offsets 17-19 in APERTURE (§ 5.4, § 5.5).
- **The only persistent writer is the service channel**: `'P'` `0x38` (§ 6.3). A debug console of the
  lens changes the two positions in RAM only (§ 7.2).
- **Same forcing** on the V-AF 24, the AF 16, the 24 and the 75; the 14 A and 14 P have no switch; the
  35-150 was not compared (static analysis, not observed).

### 5.3 What the lens reads of the body: "old" and "modern" bodies

Static analysis of the AF 135, not observed; the codes are those the AF 135 compares.

**The `0x01` request.** Three signatures, compared byte for byte:

| Request offsets | Value | Effect | Name in the firmware's strings |
|---|---|---|---|
| 6-8 | `FF 7F 00` | the "a6000" profile (below) | `a6000` |
| 6-8 | `FF 01 00` | arms the forcing of § 5.2 | none |
| 8-10, if 6-8 are not `FF 01 00` | `FF EF FF` | the "a7r4" profile: the `0x1D` speed becomes `0x100` | `a7r4` |

- The `0x01` of the NEX-7 and of our board carries `FF 01 00` at offsets 6-8 (`protocol.md` § 6.1).
- The a6000 and `FF 01 00` flags are cleared only by a `0x08` with bit 2 of offset 0; the a7r4 flag is
  never cleared. **They survive from one session to the next while the lens stays powered.**
- `protocol.md` § 7.1 lines 551-552 point here.

**The `0x08` request.**

| Field | Effect |
|---|---|
| offset 0, bit 2 (`0x04`) | clears the a6000 and `FF 01 00` flags of the `0x01`; makes the `0x05` fill its offsets 96-107 (meaning unknown) |
| offset 0, bit 1 or bit 2 | with no `0x01` flag left, **lifts the forcing of § 5.2**; `0x06` offsets 20-21 and 32-38 carry the current sample instead of the previous one; the oscillation scale of a relative `0x1D` divides by 32 instead of 16 (§ 4.2) |
| offset 0, bit 1 (`0x02`) | read only on paths taken when the `0x03` sets offset 12 bit 3, which our board never does |
| offset 1, low nibble | a shift, in entries, of the table of apertures that feeds `0x05` offsets 0-3 (the NEX-7's `21` shifts it by one); meaning not established |
| offsets 4-5 | a body model identity: the V-AF 24 compares it with `0x0933` (the NEX-7) and `0x0C0C`; the AF 135 keeps it unread |
| offset 5 | a body family: the AF 135 scales a depth-of-field value by it (used by the "depth" unit of `0x1D` and `0x1F`, and published at `0x06` offsets 13-14); the V-AF 24 derives body family names from it |

**The resulting profiles** (columns: what the body sends):

| | `0x08` offset 0 = `00` (our board to a non-Samyang) | NEX-7 (`C0 21 00 00 33 09 62 02`) | `02` | `06` (our board to a Samyang) | `04` | a6000 signature |
|---|---|---|---|---|---|---|
| switch forced (§ 5.2) | yes | yes | yes | **no** | **no** | no with `02`, yes with `00` |
| `0x06` offsets 20-21, 32-38 | previous sample | previous sample | current | current | current | current with `02` |
| `0x05` offsets 96-107 | zero | zero | zero | **filled** | filled | zero |
| depth-of-field value | 0 | **not 0** (offset 5 = 9) | 0 | 0 | 0 | not established |
| ring aperture applied to the iris by the lens (§ 5.5) | — | — | — | no | no | **yes** |

- **The firmware does not sort bodies into "old" and "modern"**: it names two `0x01` signatures, and
  bit 2 of the `0x08` is its only hierarchy. "Modern" is our board's word for a `0x08` with bits 1
  and 2 set.
- **With the `0x03` our board sends** (offset 12 bit 3 never set, `0x1D` unit 0, absolute, no side of
  arrival), the profiles `02` and `06` differ only by the forcing and by `0x05` offsets 96-107: focus
  speeds, focus by wire, ring sensitivity, homing, mark and astro mode are the same.
- **Our board sends `08 06` + 7 zeros to a recognised Samyang**, `08` + 8 zeros otherwise
  (`init.c`, `body08_flags()`), and logs it (`7_Docs/PROTOCOL.md` § 2, "La bague"). Earlier firmwares
  sent `02` (`4_Firmware/traces/sy135-2026-09-20-full.txt` line 14) and once `04`
  (`4_Firmware/traces/sy135-2026-09-20-reboots-0x10-stall.txt` line 115).
- **The other PIC32 models** (24, V-AF 24) and the 35-150 have the same `0x01` signatures (the 24
  and the 35-150 without a7r4) and the same role for bit 2 of the `0x08`; the V-AF 24 identifies the
  body by offsets 4-5 and reads, in the a6000 profile, a setting of its EEPROM.

`protocol.md` § 7.8 lines 961-963 and § 13 line 1361 ("`0x08` flags") point here.

### 5.4 The focus ring: focus by wire

Static analysis of the AF 135, not observed, except where a capture or the code is cited.

- **Bit 1 of `0x04` offset 3 clear means "focus by wire"**: at each `0x04` received, with that bit
  clear, the lens moves its focus by the ring, whatever the switch position and whatever the body
  profile. With the bit set, the ring does not move the focus. Before any `0x04` the bit is taken as
  set. The NEX-7 sends `83` (set) to an AF lens (`protocol.md` § 7.4).
- **Conditions**: the stream on; not during a return to the mark; an arming sequence first: from the
  first `0x04` without the bit, the lens needs about 16 ms and two more `0x04` before the focus moves;
  the ring edges turned meanwhile are lost.
- **The step**: per `0x04`, the edge count is bounded to ±15, multiplied by a sensitivity of 4, 6 or 8
  (a setting of the flash), scaled, capped at 100 steps, and the target clamped to the soft limits.
- **Offset 60 of the `0x05`**:

  | Position (configuration) | `0x04` bit 1 | Lens | Offset 60 |
  |---|---|---|---|
  | AF | set | nothing from the ring | ±1 if an edge since the last `0x04`, cleared at each `0x04` |
  | AF | clear | focus by wire | the direction of the movement launched, `01`, `FF` or `00` |
  | APERTURE | set | native aperture (§ 5.5) | 0 |
  | APERTURE | clear | focus by wire; the native aperture stops | the direction of the movement launched |
  | MF | clear | focus by wire | the direction of the movement launched |
  | MF | set | nothing | 0 |

- On the wire: with `0x04` offset 3 = `81` the AF 135 moves its focus by the ring, offset 60 = `01`,
  offset 22 = `C0`, the `0x06` position climbing (`traces/195_samyang.txt` lines 3 and 124-146).
- **Our board** sets the bit when it gives the ring the aperture role, and clears it in the focus role;
  the role follows offset 62 bit 1 (`4_Firmware/components/session/ring.c`, `role()`, `BODY_AF_OFF`,
  `BODY_AF_BIT`, `SW_MF`). In APERTURE and AF the board is in the aperture role, so the AF 135 does
  focus by wire only in a position configured MF.

### 5.5 The native aperture (APERTURE position)

Static analysis of the AF 135, not observed, except where the code or `7_Docs/PROTOCOL.md` is cited.

- **Conditions**: normal mode (after the homing), the current position configured APERTURE, and bit 1
  of `0x04` offset 3 set.
- **What the lens does**: it counts every ring edge into an index of 133 steps, f/1.8 to f/22 in
  thirds of a stop, **six edges per third**; the first step comes after 3 edges and is worth 3 indexes;
  it publishes the aperture at `0x05` offsets 17-18, with offset 19 bit 0 set (`protocol.md` § 7.5).
  On entering the mode the index starts from the body's target (`0x03` offsets 3-4), after a
  resynchronisation that waits for three sequence numbers without `0x03` offset 12 bit 4 and two equal
  targets; edges turned meanwhile are lost.
- **It does not move the iris**, except in the a6000 profile: the body must copy the published
  aperture into its `0x03`. Our board does (`ring.c`, path of the published aperture;
  `7_Docs/PROTOCOL.md` § 2, "La bague").
- **Leaving the mode** (bit cleared, or switch moved): the publication is cleared at the second
  `0x03`.
- **The 23 aperture codes**, each held by six indexes (index 132 is the 23rd): the widest aperture of
  the `0x08` reply (offsets 0-1, `0x11BF` on the wire), then the exact thirds of `protocol.md` § 8.1
  from f/2 (`0x1200`) to the narrowest aperture of the `0x08` (offsets 2-3, `0x1900`), each third
  `256 / 3` truncated (`4_Firmware/traces/sy135-2026-09-20-full.txt` line 15; the fake computes them,
  `4_Firmware/sim/lens135.c`, `ap_table()`). The table read in the firmware is not published.
- **The AF 16 publishes both** (static analysis, not observed): it sets `0x05` offset 60 to ±1 at
  every ring edge, in every switch configuration (the guard meant to reserve this to the AF position
  always lets the write through), and in the APERTURE position, with bit 1 of `0x04` offset 3 set, it
  publishes at offsets 17-19 an aperture drawn from the same edge count. One ring edge then shows at
  both places. The AF 135 never has both at once (offset 60 is 0 while it publishes, § 5.4), nor does
  any other Samyang read. Our board, which takes ring pulses from offset 60 and the published aperture
  from offsets 17-19, mutes the pulses for 1 s after a change of the published aperture
  (`4_Firmware/components/session/ring.c`, `aperture()`, `PUB_US`).

### 5.6 The button and the astro (preset) mode

Static analysis, not observed, except where a capture or the code is cited.

- **AF 135**: the button held during step 2 of the iris homing puts the lens in its astro mode until
  a homing without the button or a wake-up from sleep. In that mode offset 62 bit 1 is forced to 1 and
  offset 64 bit 3 to 0, so the body sees no button press; the switch forcing and the native aperture do
  not apply. A short press drives the focus to the lens's mark; **holding the button more than 120 VD
  counts stores the current position and the direction of the last manual movement in the lens's
  EEPROM** (§ 7.2). The recall arrives on the mark always from the side of the last manual movement
  before the memorisation, overshooting by 200 steps only when the direct path would arrive from the
  other side; an invalid direction (a blank mark) arrives decreasing. A blank mark reads as 14623.
  After every homing in this mode the lens returns to its mark on its own (§ 3.2).
- **On the wire**: `'W'` 01 for a press with offset 64 bit 3 at 0, then the recall
  (`4_Firmware/traces/sy135-2026-09-20-astro-normal.txt` lines 296-303).
- **The body cannot enter or leave this mode**, cannot read the mark, and no reply says whether a
  lens has the mode or is in it, on any of the twelve. It only chooses when the homing happens.
- **V-AF 24**: the preset is entered by the switch configuration, recomputed continuously; no return
  after the homing; offset 62 not forced; the overshoot is 50 steps; a button held about 9.7 s
  recalibrates infinity and restarts the lens (§ 7.2).
- **35-150**: two presets, entered by configuration; the mark is a distance stored in flash; no
  `'W'`, no autonomous return: its preset is invisible to the body.
- **24 and 24 A**: preset by the button held at the iris homing, as the AF 135; return at the 61st VD
  after the homing reply; overshoot ±50 steps; the long press writes the mark and a longer one erases
  the EEPROM (§ 7.2).
- **AF 16, 14 A, 14 P, 75, 85 II, 35 P**: no preset machine; the 14-24 has one, never reached.
- **Our board's own mark** (`4_Firmware/components/session/mark.c`, `restore.c`) is kept in the
  board's memory, not the lens's; its recall copies the AF 135's recipe with an overshoot of 200 steps
  on the AF 135 (`restore.c`, `X_135`) and 1 % of the published range on other lenses.

---

## 6. The service channel `0x40`

Static analysis, not observed, except where a capture or the code is cited.

### 6.1 The message

- **Request**, 19 bytes, class 2: `40 <main> <sub> <16 data bytes>`; the main command is an ASCII
  letter. **Reply**, 19 bytes: `40 <main> <sub> <16 data bytes>` (`4_Firmware/traces/sy135-2026-09-20-full.txt`
  lines 42-43). The AF 135 does not list `0x40` in its `0x01` bitmap (`protocol.md` § 7.1) and serves it
  all the same; the 14 A and 14 P list it (§ 1.3).
- **Served in every state**: in or out of stream, during a homing or a `0x0A`, and during the link
  transitions while the UART is open. It never blocks the dispatcher. It does not change the stream or
  the link state.
- **Deferred reply**: 50 ms at least after the command, and, for the commands that move a motor,
  after it stops. Some commands have **no `0x40` reply**: `'F'` `0x32`, `EB` and `20`, `'I'` `0x32`, and
  `'X'` outside `34`-`37` on the 135, 35-150 and 14 A; they end with the standard `10 00` of the homing
  they start (§ 3.1).
- **The transmit lock is shared** (§ 2.5): a pending `0x40` reply or notification drops a standard
  class 2 reply, and the reverse. **The notifications share the reply buffer of the channel**: a `'W'`
  emitted while a reply is pending replaces its content, and a `'W'` lost to the lock is never sent
  again.
- **Any other main command** gets an echo with zero data (AF 135, 35-150).
- `protocol.md` § 4.1 line 361, § 7.20 line 1204 and § 0 line 19 point here.

### 6.2 Main commands, model by model

| Model | Main commands |
|---|---|
| AF 135, 14 A, 14 P | F I J K M P V X |
| V-AF 24 | F I J K M P V X, and `a` |
| AF 35-150 | F I J K M P S V X Z |
| AF 16 | F I J K M P S V X Z (its `Z` sets the AF, not a zoom) |
| 24, 24 A, 75, 85 II, 35 P, 14-24 | at least F I J K M P V X (the commands read for § 7.2); the 35 P and 14-24 also `Z` |

### 6.3 The commands

- **`'V'` `00`**: the version, data bytes 0-1 (`01 05` on the bench lens,
  `4_Firmware/traces/sy135-2026-09-20-full.txt` lines 43-44; § 1.1 for the others). **On the 14-24,
  `'V'` also sets the service mode** (§ 6.5).
- **`'F'` `FA`**: the focus position, u16 little-endian in data bytes 0-1
  (`4_Firmware/traces/sy135-2026-09-20-full.txt` lines 178-188: 14623). Its reply is deferred like
  `FB`'s, with the position read when the command was received.
- **`'F'` `FB`**: a focus target, u16 little-endian in data bytes 0-1, **clamped** to the bounds below
  (never refused); the reply, data zero, is deferred until the motor is still
  (`4_Firmware/traces/sy135-2026-09-20-full.txt` lines 387-394). With the "MTF" flag of `'P'` `FA`
  set (below), the AF 135 also sends an immediate `0x40` echo of the `FB`. The 85 II has no deadline: a motor
  that never settles gets no reply. After an `'F'` `0x20` the 75 uses another speed once, and the
  35-150 swallows the first `FB` within bounds.

  | Model | `FB` bounds, motor steps |
  |---|---|
  | AF 135 | 13723-30988 |
  | V-AF 24 | 15848-18216 |
  | AF 16 | 15748-19063 |
  | 14 A, 14 P | 3810-4909 |
  | 24, 24 A | 15848-18216 |
  | 75 | 15459-18931 |
  | 85 II | 15336-19109 |
  | 35 P | 15434-19040 |
  | 35-150, 14-24 | move with the focal length: infinity − 300 to closest + 300 (14-24: ±600 in service mode) |

  Increasing steps mean closer focus on the 24, 24 A, 85 II and 35 P; on the 75, not established.
- **`'F'` `0x32`**: a focus-only homing, no `0x40` reply, ended by `10 00` (§ 3.1).
- **`'F'` `0x32`, `EB`, `20`, `'I'` `0x32`, `'I'` `0x20`, and `'X'` outside `34`-`37`** restart the homing
  and reset the slot period to the 60 Hz value of the `0x0D` (AF 135, 14 A).
- **`'M'`**: sets the service mode (§ 6.5); its reply carries one constant byte per model, different on
  each, meaning unknown: not published, except the AF 135's, `26` on the wire
  (`4_Firmware/traces/sy135-2026-09-20-full.txt` lines 52-56). On the 135, V-AF 24, 16 and 85 II it
  arms the `'W'` notifications; `'M'` `0x31` stops the `'H'`/`'L'` notifications (§ 6.4), except on the
  14 P, which ignores the sub-command.
- **`'I'`**, whatever its sub-command, also sets the service mode (AF 135, 35-150, 14 A).
- **`'P'` `FA`**: the AF 135 returns its switch configuration at data byte 7 (`7_Docs/PROTOCOL.md`
  § 3.1); data bytes 0-6 are other settings of the same flash page, zero from the factory, changed
  only by `'P'` `0x11` and `0x31`-`0x37` (§ 7.2). It also declares a factory bench: with data byte `0x53` it sets an "MTF" flag that changes
  the reply of `'F'` `FB`, any other byte takes a "JIG" branch; without an `'S'` it makes the AF 135
  emit a spurious `'W'` 05/06. Effects in RAM only, forgotten at power-up.
- **`'P'` `0x38`**: writes the switch configuration of the AF 135 (§ 7.2): data byte 0 =
  `0x30 + (M1 << 4 | M2)`, each 0, 1 or 2; the lens also applies it in RAM at once; another value
  writes nothing. The reply is an echo with zero data, whether the value was stored or not
  (`7_Docs/PROTOCOL.md` § 3.1).
- **`'P'` `FA` and `'P'` `0x38` read data byte 0 only** on the AF 135; the 15 others are ignored. Our
  board's whitelist therefore examines only that byte (`4_Firmware/components/bench_core/bench_core.c`,
  `svc135_allowed()`).
- **`'X'` `0x34`**: data `01` writes the update flag and restarts the lens into its boot loader; data
  `00` writes the flag back to zero **in flash**; another value does nothing (§ 7.2). This is the
  service channel's `'X'`, sub-command `0x34` — not the standard message `0x34` of § 4.6, which moves
  the focus and writes nothing.
- **`'Z'`** (35-150, 14-24, 35 P, 16): `'Z'` `0x57` reads the switches of the 35-150, but other `'Z'`
  sub-commands write its flash.
- **`'a'`** (V-AF 24, sub-commands `0x31`-`0x45`): writes nothing in the lens; it makes an accessory
  store a calibration point over a serial link.
- **The other sub-commands** of `'F'`, `'I'`, `'J'`, `'K'`, `'P'` are adjustment commands; those that
  write are in § 7.2. None is described further here.

### 6.4 Notifications

- **`'W'`** (`0x57`), spontaneous `0x40` frames after an `'M'`: data byte 0 = `01` the button, `02` the
  switch moved to M1 (up), `03` to M2 (down), `04` to `06` the limiter (AF 135 only). On the wire:
  `'W'` 02 and 03 (`4_Firmware/traces/sy135-2026-09-20-astro-normal.txt` lines 35-46), `'W'` 01
  (`4_Firmware/traces/sy135-2026-09-20-full.txt` line 382). The AF 135 sends `'W'` 02/03 only if an
  input read at start-up allows it.
- **`'H'` and `'L'`** (`0x48`, `0x4C`): the focus fork, at each edge, in service mode outside a homing,
  until an `'M'` `0x31`; on the wire with a position in data bytes 0-1
  (`4_Firmware/traces/sy135-2026-09-20-astro-normal.txt` lines 28-29). The 14 A sends them from an
  interrupt, and no `'W'`.
- Which models send which: § 5.1.

### 6.5 The service mode

Set by `'M'` (all twelve), by `'I'` (AF 135, 35-150, 14 A), by `'X'` outside `34`-`37` (AF 135, 14 A) and by
`'V'` (14-24); **never cleared but by a power cut**; it survives the sleep. Its effects, model by model:

| Model | End-of-homing rest position | Other effects |
|---|---|---|
| AF 135 | 14623 instead of 16384 (§ 3.2) | the limiter is ignored; **a short wait before every byte the lens sends**, standard frames included (duration not established); LED; iris profiles |
| V-AF 24 | 16048 instead of 16384 | not read |
| 24, 24 A | 16048 instead of 16384 | forks left on; iris profiles; a wait before each byte sent |
| 75 | unchanged | forks; iris profiles; a wait before each byte sent |
| 85 II | 15486 instead of 16384 | forks; iris profiles; a wait before each byte sent |
| 35 P | unchanged | forks; iris; LEDs forced again at each `0x04` |
| 14-24 | the infinity of the current focal length, with overshoot | **`'F'` `FB` bounds ±600 instead of ±300**; zoom calibration and filters; iris profiles |
| 14 A, 14 P | not read | no wait before each byte |

All the captures of `4_Firmware/traces/` were taken with the service mode set: the board firmware of
the time sent `'M'` at every boot (`4_Firmware/traces/sy135-2026-09-20-full.txt` lines 52-56).

### 6.6 What our board sends on `0x40`

- `bench_core` lets through, **to a recognised Samyang only**: `'V'` `00`, `'F'` `FA`, `FB`, `0x32`,
  `'M'` `00` and `0x31` (`4_Firmware/components/bench_core/bench_core.c`, `svc_allowed()`), none of
  which writes non-volatile memory on any of the twelve (§ 7.2); and, **to the AF 135 only**, `'P'`
  `0x38` and `'P'` `FA` (§ 7.4).
- The session sends only `'P'` `FA` and `'P'` `0x38`, through the lab command `CUSTOM`
  (`4_Firmware/components/session/cmd.c`, `CMD_LENS_CUSTOM`; `7_Docs/PROTOCOL.md` § 3.1). It sends no
  `'M'`: a 135 sends `'W'` only if another master armed them since its last power cut.

---

## 7. Writes to non-volatile memory

### 7.1 The memories

Static analysis, not observed.

- **Flash pages** of the microcontroller, a user page and, on most models, a backup page: all
  twelve. The 24 A has no backup page; the 35-150 and 14-24 have a sub-command that erases both pages.
- **An external I²C EEPROM**: AF 135 (the astro mark), V-AF 24 (the mark, an infinity correction, a
  body setting), 24 and 24 A (the mark). None on the 75, 85 II, 35 P, 14-24.
- **The application never erases its own code**; reprogramming happens in a boot loader that is not
  in the images.

### 7.2 The map: model × path

Static analysis, not observed. Each cell is either **recorded** — what is written, and the limit of
the proof — or **unknown**: no source read covers it. Columns:

- **`0x40`**: the sub-commands of the service channel that write, as `<main>: <sub-commands>`, hex;
  `'X'` `34` is the letter `'X'`, sub-command `0x34` (§ 6.3);
- **`0x14`**, **`0x16`**: the standard messages;
- **start-up**: writes at power-up with no message;
- **gestures**: writes caused by the lens's own controls, with no message;
- **serial consoles**: debug or factory consoles on a serial port of the lens.

| Model | `0x40` | `0x14` | `0x16` | Start-up | Gestures | Serial consoles |
|---|---|---|---|---|---|---|
| AF 135 | F: EB DB CB BB AB 4B 20 21 22 23 · I: 23 · J: FB · K: FB · P: 11, 31-38 (38 = the switch configuration) · **X: 34, data `00` as `01`** | writes the update flag, waits 3 s, restarts into the boot loader if present | none in the steps read (iris to rest, wait for the motors, `16 00`, sleep) (1) | unknown (2) | **the EEPROM mark**, by the button held more than 120 VD in astro mode, and only by the button | unknown: a debug console changes the switch configuration in RAM only; no write established (3) |
| V-AF 24 | F: EB AB 20 21 22 23 CB BB 4B · I: 23 · J: FB · K: FB · P: 11, 36, 37, 38 and every other value but FA · X: 34 · F: 20 also writes the EEPROM (infinity correction) | unknown | **writes the EEPROM** (two bytes) when the body has the a6000 profile (§ 5.3) (4) | unknown | the mark in EEPROM (preset); **a hold of about 9.7 s writes the infinity correction to the EEPROM, then restarts the lens** | unknown |
| AF 35-150 | F: EB DB CB BB AB 4B 20-23 · I: 23 · J: FB · K: FB · P: 11, 36, 37 (erases both pages), 38, and a range of configuration entries · X: 34, `00` and `01` · Z: 32 34 36 38-3D 3F 40-46 59 | unknown | unknown | unknown | **the preset mark, a distance, in flash**; the long press erases nothing | unknown |
| AF 16 | F: EB AB 20-23 CB BB 4B · I: 23 · J: FB · K: FB · P: 11, 31-38 · X: 34 · Z: 38 39 3C | unknown | unknown | unknown | none found: no preset machine (5) | unknown |
| 14 A | F: EB DB CB BB AB 4B 20-23 · I: 23 · J: FB · K: FB · P: 11, 31-38 · X: 34, `00` as `01` | writes the update flag in flash, waits about 3.9 s, resets the microcontroller | none in the steps read (iris to rest, wait for the motors, `16 00`, sleep in the microcontroller's Stop mode) (1) | **at every start, if the update flag is set, it is reset and the flash page rewritten** | none: no button, no switch, no preset (5) | a debug console on a serial port separate from the mount; it moves iris and focus; no write reported; its link to the contacts is not established |
| 14 P | F: EB DB CB BB AB 4B 20-23 · I: 23 · J: FB · K: FB · P: 11, and every other value but FA · X: 34 | unknown | unknown | unknown | none: no button, no switch, no preset (5) | unknown |
| 24 | F: EB DB CB BB AB 4B 20-23 · I: 23 · J: FB · K: FB · P: 11, 31-37, 38 (a valid value) · X: 34, `00` and `01` | **update flag = 3 written to flash**, a wait of about 3000 ticks, a reset if a boot loader is present | **none**: sleep, no write, no reset | (6) | **in preset: held more than 120 VD writes the mark (position and direction) to the EEPROM; held 462 VD more erases the EEPROM** | consoles on separate serial ports write the flash and the EEPROM; link to the contacts not established |
| 24 A | as the 24 | as the 24 | as the 24 | (6), no backup page | as the 24 | as the 24 |
| 75 | F: EB DB CB BB AB 4B 20-23 · I: 23 · J: FB · K: FB · P: 11, 31-37, 38 · X: 34 | as the 24 | as the 24 | (6) | none | consoles write the flash; link not established |
| 85 II | as the 75 | as the 24 | as the 24 | (6) | none | consoles write the flash; link not established; an over-long console line would overwrite the RAM copy of the flash data |
| 35 P | F: EB CB BB AB 4B 20-23 · I: 23 · J: FB · K: FB · P: 11, 31-38 · X: 34 · Z: 38 39 3C | as the 24 | as the 24 | (6) | none | consoles write the flash; a console command resets the lens; link not established |
| 14-24 | F: EB DB CB BB AB 4B 20-23 · I: 23 · J: FB · K: FB · P: 11, 31-38 (37 erases both pages) · X: 34 · Z: 32 34 36 38-3D 3F 40-46 59 | as the 24 | as the 24 | (6) | none: the preset path exists but the mode it needs is never set | consoles write the flash; a console command resets the lens; link not established |

Notes:

1. **AF 135 and 14 A `0x16`**: established by reading the handler and its shutdown task, not by a
   census of the memory writers as for the six newer models. `protocol.md` § 7.20 line 1201 ("on one
   Samyang model it also writes the lens's memory") is the V-AF 24 (note 4).
2. **AF 135 start-up**: when both flash pages are invalid the lens takes the factory switch
   configuration `0x10` (§ 5.2); whether it then writes it to flash is not established.
3. **AF 135 console**: the console that changes the two switch positions in RAM is the lens's internal
   serial link, not the mount; its other commands were not read.
4. **V-AF 24**: the `0x16` write and the EEPROM part of `'F'` `20` come from a delegated reading, not
   reread. The same model's `0x09` reloads an accessory flag read from its EEPROM, and an accessory
   task resets that byte to zero when it was 1, a value only an accessory sets; whether that reset
   writes the EEPROM is not stated (`4_Firmware/components/bench_core/bench_core.c`, comment of
   `listed_size()`, which keeps the `0x09` because refusing it would break every init). On the six
   newer models the `0x09` only answers.
5. **AF 16, 14 A, 14 P gestures**: the analyses establish the absence of a preset machine, the only
   gesture-to-memory path known on the other models; no other gesture path was searched.
6. **Six newer models, start-up**, with no message: no valid page → the defaults are written; the main
   page invalid → restored from the backup; the two pages different → the backup copied (except the
   24 A); and once per start, if the update flag is not zero, it is reset and the page rewritten.
   **Any configuration write is therefore followed by a second write at the next start.**

- **On the six newer models no command of our board's `0x40` list** (`'V'` `00`, `'F'` `FA`/`FB`/`0x32`,
  `'M'` `00`/`0x31`), whatever its 16 data bytes, delayed effects included, **and no standard message
  our board sends** writes non-volatile memory; only `0x14` and `'X'` `0x34` lead to the update path
  (§ 7.3 for the limit). On the six older models the same holds for the `0x40` list by the reading of
  each handler (`bench_core.c`, comment of `svc_allowed()`).
- **On the wire**, no write has been provoked or observed; the bench lens has received only the
  `0x40` list above and, since `CUSTOM`, `'P'` `FA` and `'P'` `0x38`.

### 7.3 The limit of the proof

Static analysis, not observed.

- **Six newer models**: a census on each image of the accesses to the flash-programming registers
  (and to the I²C controller), of the direct call graph, and of address constants that could make an
  indirect call; no writer is reached through a function pointer. Self-modifying code or a computed
  jump table are not formally excluded; some interrupt routines were read only by scanning their calls
  and writes.
- **Six older models**: the `0x40` lists come from a reading of each handler, partly delegated and not
  all reread; the AF 135's `'X'` `0x34` and its `'P'` `0x38` were reread.
- **The boot loaders** are not in the images: what they do with an update flag of 1 or 3 is unknown.
- **Cells marked unknown** were not covered by any analysis read; they are not "none".

### 7.4 Our defence

- **A whitelist, last before the wire.** `bench_core` is the only caller of the board's transmit
  function; a frame passes only if its messages pave it exactly with the sizes of its list:
  `0x01 03 04 07 08 09 0A 0B 0D 10 1C 1D 3F`, and `0x40` to a recognised Samyang with the sub-commands of
  § 6.6. `0x0C`, `0x14`, `0x15`, `0x16`, `'X'` `0x34` and every unlisted type or sub-command are refused,
  counted and reported (`4_Firmware/components/bench_core/bench_core.c`, `listed_size()`, `allowed()`,
  `svc_allowed()`; `4_Firmware/include/bsk_bench_core.h`). Its tests stay
  (`4_Firmware/sim/test/test_bench_core.c`, `4_Firmware/sim/test/garde_emission.sh`).
- **One exception, decided by the project owner**: `'P'` `0x38` with a data byte
  `0x30 + (h << 4 | b)`, `h` and `b` from 0 to 2, and `'P'` `FA` with a data byte other than `0x53`, **to
  the AF 135 only** (`bench_core.c`, `svc135_allowed()`; the session declares it by its LensType2 8,
  `init.c`, `init_recognize()`, `BSK_BENCH_SAMYANG135`). The same commands write other Samyang lenses
  with a meaning nobody has reread.
- **Its contract**: `7_Docs/PROTOCOL.md` § 3.1, `CUSTOM READ` / `CUSTOM WRITE <h><b>`: one byte of the
  AF 135's flash, its switch configuration, and nothing else; the format is established on the 1.06,
  and the board does not read the lens's version.
- **Its client**: `5_App/tool_station/tool_station.py`, which reads the configuration, asks for a
  confirmation, writes, and reads back.
- **What a body-side driver should refuse**, from the map: `0x14`, `0x15`, `0x16` (on the V-AF 24),
  `'X'` `0x34` whatever its data, and every `'F'`, `'I'`, `'J'`, `'K'`, `'P'`, `'Z'` sub-command of § 7.2;
  and, since unknown cells are not "none", every `0x40` sub-command not shown harmless on the model at
  hand.

---

## 8. How each model departs

### 8.1 From the AF 135

Static analysis, not observed.

**The 14 A, a STM32 read as a complete automaton**, against the AF 135 and against its PIC32 twin,
the 14 P:

| Subject | AF 135 | 14 A | 14 P |
|---|---|---|---|
| UART at power-up | closed until handshake step 3 | **open before the handshake**: a message received with BODY_CS high is served during the handshake | as the AF 135 |
| no phase, stream switch, `0x0A` echo, layout at every `0x0A`, one `0x05` per `0x03`, one `0x06` per `0x04` | — | the same | the same |
| `0x0A` | stops the slot timer; 200 ms at most | **does not stop the slot timer**, resets the slot to 0; **100 ticks** at most, about 128 ms | stops it; 100 ticks, about 100 ms |
| homing, first focus failure | the whole homing again, iris first | **the focus only** | as the AF 135 |
| homing, second focus failure | `10 00` then `0x17` | `10 00` then `0x17` | **`0x17` only, no `10 00`** |
| iris-only homing | answers `10 00` | **never answers** | answers |
| sleep | waits for its input low, 50 ticks, low again | **no 50 ticks**; the microcontroller's Stop mode, woken by an edge on BODY_CS | as the AF 135 |
| tick | about 1.003 ms | **about 1.285 ms**: the same counts last about 28 % longer (homing step about 1.93 s, `0x0A` about 128 ms) | about 1.003 ms |
| type 0 | effect not established | consumes 8 bytes, no block | not read |
| watchdog | disabled | no access by the code; the hardware option bytes are not in the image | disabled |
| soft limits | 13723-30988, narrowed by the limiter | 3810-4909, fixed | the same as the 14 A |
| wait per byte in service mode | yes | no | no |
| `0x3B` table | 7 blocks | 6 blocks | not read |

Everything else in § 2 to § 4 holds for the 14 A: the transmit lock, the deferred replies, the silent
drop of bad frames, the block on an unknown type, the acknowledgements and their eviction, the
`0x1D` fields, the `0x34`, the iris, the 1.5 Mbaud switch.

**The other models** (their automaton has not been read; what follows was read on their handlers):

| Model | Departs from the AF 135 |
|---|---|
| V-AF 24 | 215 of 300 functions identical, 44 very close; name changed by an anamorphic accessory; preset by configuration, no autonomous return; reads the body identity (`0x08` offsets 4-5); `0x16` writes its EEPROM in the a6000 profile; `'a'` command |
| AF 35-150 | zoom: bounds and infinity move with the focal length (about 1 400 steps between 35 and 150 mm); **the focus moves on its own when the zoom ring turns**; no distance table (a model per zoom index); offset 62 bit 1 is a physical MF switch; two presets invisible to the body; `'Z'` commands |
| AF 16 | `'W'` 02/03 only; no preset; `'Z'` sets the AF |
| 24, 24 A | two hardware revisions of one lens, the same table and logic; preset by the button at the homing |
| 75 | no button; the service mode does not change the rest position |
| 85 II | `'W'` 01/02/03 like the AF 135; LensType2 9, absent from ExifTool; no deadline on `'F'` `FB`; on a second homing deadline the `0x10` leaves with the motor running |
| 35 P | its debug banner names it a Sony lens, its `0x3F` a Samyang; no button, no switch |
| 14-24 | zoom; **`'V'` sets the service mode**; the focus follows the zoom and moves on its own after the homing reply; homing success and failure indistinguishable |

`0x1D` fields: on the 14 A as on the AF 135 (§ 4.2); the target computation differs and was not
detailed. Bits 4-5 and 6-7 were not read on the 35 P and the 35-150.

### 8.2 From `protocol.md`

Static analysis, not observed, except where the section cited in the right-hand column cites a capture.

| Subject | Common view (`protocol.md`) | Samyang |
|---|---|---|
| when init messages are served | Samyang: any time (§ 6.1) | confirmed, every known type in every state (§ 2.1) |
| bad frame | dropped in silence (§ 3.2) | the same; never a `0x02` (§ 2.6) |
| an unknown type | blocks (§ 4) | blocks until power cut; type 0 not established on the AF 135, not a block on the 14 A (§ 2.6, § 8.1) |
| VD | a movement never starts without VD (§ 1.3) | true except step 4 of the iris homing (§ 2.4) |
| `0x16` sleep | waits for an input line low for 50 ms (§ 7.15) | low, 50 ms, low again; the 14 A without the 50 ms (§ 2.2, § 8.1) |
| `0x1D` beyond a limit already reached | no movement and an acknowledgement (§ 7.4.2) | contested between the analyses (§ 4.3) |
| `0x06` limits | 13873-30738 on the AF 135 (§ 7.6) | published; the lens clamps to 13723-30988 (§ 4.3) |
| `0x0D` | `0D 01`; 50 or 48 Hz on the AF 135 (§ 7.13) | offset 0 bits 0-1; no 30 Hz; the VD period is not measured (§ 2.4) |
| `0x1D` speed | — | from the `0x03` offset 12 and the body profile (§ 4.2) |

---

## 9. Our board with a Samyang

What the code of this repository does; it moves the AF 135 on the bench.

### 9.1 The session

- **Recognition** by the name or a closed list of LensType2 (§ 1.2), at the `0x07` and `0x3F` replies
  of the init (`init.c`, `init_recognize()`).
- **Init order** `01 07 3F 08 0B 09 0D 10 0A` (`protocol.md` § 6.1); `08 06` + 7 zeros to a Samyang
  (§ 5.3); **the `0x03`/`0x04` loop starts with the `0x10`** for a recognised Samyang (§ 3.1); the
  `0x0A` and the `0x10` are never re-sent within a session (`init.c`, `idempotent()`).
- **A homing of its own**, `0x1C`, 100 ms, then `10 08` (focus only), when the init did not finish
  (`4_Firmware/components/session/session.c`, `to_homing()`, `HOME10`).
- **Moves**: `1D lo hi 00 00`, absolute, steps, no wait, no side of arrival, inside the published
  limits narrowed by 5 steps (`drive.c`, `drive_goto()`; `lens_rx.c`, `LIMIT_MARGIN`); offset 12 bit 4
  of the `0x03` set for 30 pairs after each `0x1D` (`drive.c`, `AF_OFF`, `AF_BIT`, `AF_FRAMES`); a
  stop is a lone `0x1C` in class 1, re-sent every 1.5 s, three sends at most, until a `1C` of any value
  or a still lens (`motion.c`, `STOP_MS`).
- **Ring role** from offset 62 bit 1 (§ 5.4); the published aperture of offsets 17-19 copied into the
  `0x03` (§ 5.5); the button from offset 64 bit 3 (`mark.c`, `BTN_BIT`).
- **The `0x40`**: only `CUSTOM`, to the AF 135 (§ 6.6).
- **A re-init without a power cut** (loss of link, soft reset) loses the AF 135's switch configuration
  until it is unplugged (§ 5.2); the board's rails are not cut by default (`session.c`,
  `bsk_session_params_default()`, `cut_rails`).

### 9.2 The fake AF 135

The suite plays the board against a fake AF 135 (`4_Firmware/sim/lens135.c`), whose replies are byte
templates copied from the traces (`4_Firmware/sim/sources135.c`) and whose mechanisms follow § 2 to
§ 5. Its declared simplifications (header, S1 to S15) include: the `0x1D` in unit 0 only, without the
compensation of bits 6-7 (S6); only `1D` and `1C` acknowledgements (S11); no astro mode, no limiter,
no `'W'` of the switch (S7); the iris as an abstract motor (S4). Its limits are those its traced `0x06`
publishes, 13873 and 30738, and its rest position after a homing is 16384, or 14623 in service mode
(`POS_REST`, `POS_THRESH_LO`).

### 9.3 Contested points that concern a Samyang

Reported, not corrected; the full list is `protocol.md` § 11.

- **Movement read at `0x05` offset 60** (`lens_rx.c`, `move`; `motion.c`; `still.c`): on the AF 135
  offset 60 is a ring flag, or the direction of a focus-by-wire move (§ 5.4), and neither a `0x1D` nor an
  `'F'` `FB` move sets it (static analysis, not observed); the AF 135 sets offset 22 bit 6 while its focus moves (§ 4.8).
- **Arrival tested on `0x06` offsets 2-3**: on the AF 135 these are its position; offsets 20-21 lag by
  one frame in the capture of § 4.8.
- **The appendix of the `0x06` walked two bytes at a time**: harmless with a Samyang while the board
  sends no `0x22`, `0x2E` or `0x3C`; a Samyang appends at most three acknowledgements (§ 4.4).

---

## 10. What `protocol.md` points here for

| `protocol.md` line | Subject | Here |
|---|---|---|
| 19 | what only one maker does | the whole document |
| 361 | the `0x40` size, Samyang only | § 6.1 |
| 472 | why the loop starts with the `0x10` | § 3.1 |
| 501 | the stream paced by the `0x03`/`0x04` pair | § 2.4 |
| 552 | the three bytes of the `0x01` the AF 135 reads | § 5.3 |
| 620 | `0x04` offset 3 bit 1, the soft limits and the stop of a movement | § 4.3, § 2.4 |
| 661 | the `0x1D` units refused and the constants of bits 6-7 | § 4.2 |
| 963 | bits 1-2 of the body's `0x08` offset 0 | § 5.3 |
| 1052 | the `0x0D` rates | § 2.4 |
| 1086 | what else the `0x16` does | § 2.2, § 7.2 |
| 1174 | the speed profile after a `0x34` | § 4.6 |
| 1201 | the Samyang model whose `0x16` writes its memory | § 7.2 (V-AF 24) |
| 1204 | the service channel | § 6 |
| 1315 | what each maker does with each common message | § 2 to § 6 |
| 1361 | the brand file's list: stream, homing in stream, `0x0A`, `0x08` flags, `0x0D`, `0x40`, writes, exceptions | § 2.4, § 3.1, § 2.3, § 5.3, § 2.4, § 6, § 7, § 7.4 |

---

## 11. What stays unknown

- Everything on a wire for the eleven models other than the AF 135; their delays.
- On the AF 135: which mount contacts the extra lines of the handshake and of the sleep are; the
  meaning of `0x06` offset 0 bits 3-4 and of the wait of `0x1D` bit 3; the duration of the per-byte
  wait in service mode; the effect of type 0 and of types at or above `0x4D`; how a `0x1D` received
  during a homing interacts with the reference; the cause of the captured "`0x01` without reply"
  (§ 2.7); which rest position the 1.05 takes after a homing (§ 3.2); what happens when a target beyond
  a limit is given at that limit (§ 4.3); the meaning of `0x05` offsets 96-107 and of `0x08` offset 1.
- What the 1.05 firmware of the bench lens does differently from the 1.06 that was read.
- The cells marked unknown in § 7.2, and what the boot loaders do with an update flag.
- The link between the serial consoles of the lenses and the mount contacts.
- On the 14 A: what a reply sent during the handshake does; the effect of a `0x0A` that leaves the
  slot timer running; whether a hardware watchdog is armed.
- The `0x1D` target computation of the models other than the AF 135; bits 4-5 and 6-7 of the 35 P and
  35-150; the special unit-3 target of the 14 P, 14 A, 24, 24 A and 75.
- The 75's sense of increasing steps; the angle of a ring edge on the AF 135.
- Whether the 24 or the 24 A is the product Samyang sells as "astro": the firmware does not say.
