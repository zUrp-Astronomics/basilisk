# The Sony E-mount protocol — common reference

**Date** : 2026-10-07
**Dernière révision** : 2026-10-10
**Statut** : actif — first of the five interoperability documents of `7_Docs/E-Mount/` (ticket #505), with `samyang.md`, `tamron.md`, `traces.md`, `provenance.md` and the index `README.md`
**Référencé par** : `7_Docs/E-Mount/README.md`, `samyang.md`, `tamron.md`, `traces.md`, `provenance.md`, the comments of `4_Firmware/components/` and `4_Firmware/include/` (ticket #558)
**Dérivé de** : the code of this repository (`4_Firmware/components/`, `4_Firmware/include/`), the captures (`4_Firmware/traces/`, `7_Docs/E-Mount/traces/`) and public sources cited in each section, and the project's own static analyses of Samyang and Tamron lens firmware, which are not published

## 0. What this reference is

**Scope.** This document describes what travels between a Sony E-mount camera body and a lens,
and what decides what the lens answers, for everything that is **common** to the lens makers the
project has studied: Sony (from captures), Samyang and Tamron (from captures where they exist, from
static analysis otherwise). Someone who has only this folder should be able to rewrite the
body-side firmware of this repository (`4_Firmware/`) and the fake lenses it uses in simulation
(`4_Firmware/sim/lens135.c`, `4_Firmware/sim/lens_std.c`) without ever opening a lens firmware.
It stops at the mount: it says what the body sees on the ten contacts and which inputs determine
the lens's answers. It does not describe the inside of a lens (motor control, internal timers,
memories). What only one maker does goes to `samyang.md` and `tamron.md` (§ 13).

**Conventions.**

- **Offsets.** "Offset *n*" of a message is the *n*-th byte **after its type byte**, starting at 0.
  This is weiziqian's `pl[n]` and the convention of our code (`4_Firmware/components/session/lens_rx.c`:
  "offset k of a message is `msg[k + 1]`"). Positions inside a whole frame are called "frame byte *n*".
- **Sizes.** The size of a message (a sub-message, § 4) is given **type byte included**: a `0x1D`
  is 5 bytes, `1D` plus four. weiziqian counts payload bytes without the type byte (his 0x08 reply
  is "201 bytes", ours 202).
- **Numbers.** Hexadecimal is written `0x..` or as bare byte pairs (`F0 0B 00`). Every 16-bit field
  is little-endian unless stated. B→L is body to lens, L→B lens to body.

**Order of authority** (decision of the project owner, 2026-10-07). When sources disagree:

1. **what a trace shows**: the lens really did it;
2. **what the code of this repository does**: it is the code that moves real lenses, validated on
   the Samyang AF 135 F1.8 FE and in part on the Sony FE 24-105 mm F4 G OSS;
3. **our static analyses**, only for what neither traces nor code cover, always marked
   **static analysis, not observed**.

Where our code is contested, this document says what the code does **and** that it is contested,
with the reason (§ 11). Where our code contradicts a trace, it says so (§ 11). Nothing here
corrects the code.

**Proof.** Every statement carries one of these:

| Kind | Form |
|---|---|
| a capture of this repository (Samyang AF 135) | `` `4_Firmware/traces/<file>` line <n> `` |
| a bench capture of the project, made with this project's board | `` `traces/<file>` line <n> ``, relative to `7_Docs/E-Mount/` (published by the `traces.md` ticket) |
| the code of this repository | the file and the function or constant |
| a public source | its URL; a public capture with its URL and line |
| our static analysis of a lens firmware | the words **static analysis, not observed** |

**Evidence base.** Repository captures: Samyang AF 135 F1.8 FE, firmware 1.05, against an earlier
firmware of this board. Bench captures: Sony FE 24-105 mm F4 G OSS (`sony.txt`, `sony-2.txt`,
`sony_boutons.txt`, `195_sony.txt`, `195_sony_boutons.txt`, `emount-bench-*.txt`) and Samyang
AF 135. Public captures (LexOptical, `E-Mount-Traffic-Samples`, branch `master`): a NEX-7 with a
Sony 28-70 mm, a Sony 55 mm, a Voigtländer 15 mm and a Zeiss Loxia 21 mm; in the two
`nex7-clocks-and-sync-lines*.txt` files the body is real and the lens is emulated by the LexOptical
firmware. Static analyses: Samyang AF 135 (firmware 1.06) and eleven other Samyang lenses, Tamron
F051 (E 24 mm F2.8). No Tamron lens has been on the bench: everything Tamron here is static.

---

## 1. Physical layer

### 1.1 Contacts

Numbered 1 to 10 from left to right, looking at the back of the lens. First published in the
Google Doc "Sony E Mount Lens Protocol" (bostwickenator, Entropy512,
`https://docs.google.com/document/d/1iw54nzrF0bzQgLINpcP9F8Odd0N5cd7LjlwCDPTNZK0/edit`), repeated by
weiziqian (`https://github.com/weiziqian/E-mount-protocol-RE`, README § 1).

| Contact | Name | Direction | Function | Our board |
|---|---|---|---|---|
| 1 | `LENS_GND` | — | motor ground | — |
| 2 | `LENS_POWER` | B→L | motor supply: 5.0 V or unregulated battery (7.4 V nominal); how the two are chosen is unknown | rail D1, 5 V, switched (`4_Firmware/include/bsk_phy.h`, `bsk_phy_rail()`, `BSK_RAIL_MOTOR`) |
| 3 | `LOGIC_GND` | — | logic ground | — |
| 4 | `BODY_VD_LENS` (VD) | B→L | frame sync: high, short low pulse once per frame (§ 1.3) | output, `PIN_BODY_VD` (`4_Firmware/components/phy/pins.h`) |
| 5 | `LOGIC_VCC` | B→L | logic supply, 3.15 V; all data lines are 3.15 V logic | rail D0, 3.3 V, switched (`bsk_phy.h`, `BSK_RAIL_LOGIC`) |
| 6 | `LENS_CS_BODY` (LENS_CS) | L→B | high while the lens transmits | input, `PIN_LENS_CS` |
| 7 | `RXD` | L→B | serial data from the lens | UART RX, `PIN_LENS_RXD` |
| 8 | `TXD` | B→L | serial data from the body | UART TX, `PIN_LENS_TXD` |
| 9 | `BODY_CS_LENS` (BODY_CS) | B→L | high while the body transmits | output, `PIN_BODY_CS` |
| 10 | `LENS_XDETECT` | L→B | pulled high by the body, grounded by the lens (a Viltrox EF adapter through 680 Ω); the body powers nothing while it is high (Google Doc) | input, `PIN_LENS_DETECT`, pulled high, 0 = present |

No level shifting is needed between the mount and 3.3 V logic: the LexOptical emulator connects a
Teensy directly (`https://github.com/LexOptical/E-Mount`, README, "Electrical interface"), and so
does our board (`4_Firmware/components/phy/pins.h`).

### 1.2 Serial line

- UART **8N1, least significant bit first** (Google Doc; weiziqian README § "UART parameters").
- **750 000 baud** from power-up (same sources; our board: `4_Firmware/components/phy/phy.c`,
  `BAUD`). A switch to **1 500 000 baud** is negotiated by message `0x0C` (§ 5). The start byte `F0`
  (`11110000`) lets a receiver detect the rate (Google Doc).

### 1.3 VD

- One frame of the protocol per VD period. A real NEX-7 drives VD with a period of 16 683 µs and a
  low pulse of 62 µs (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-clocks-and-sync-lines.txt`
  lines 1-3: falling edges at 7124848 and 7141531 µs, rising edge at 7124910 µs). Some regions may
  run at 50 Hz (Google Doc); the lens is told the rate by `0x0D` (§ 7.13).
- The **falling edge** starts the frame: the NEX-7 times its pair from it (§ 6.2), and the Samyang
  AF 135 restarts its frame timing on it (static analysis, not observed).
- Our board: 60 Hz, low pulse of 60 µs (`4_Firmware/components/phy/phy.c`, `VD_LOW_US`,
  `bsk_phy_vd()`; `4_Firmware/components/session/session.c`, `VD_HZ`).
- VD runs **before** the handshake: the NEX-7 capture above shows six VD periods (lines 1-12) before
  BODY_CS first rises (line 13); our board starts VD, then drives its lines, then shakes hands
  (`session.c`, `on_due()`, step `S_RAIL_MOTOR`).
- On the Samyang AF 135, a commanded movement of focus or iris only starts on a VD falling edge:
  without VD it never starts (static analysis, not observed).

### 1.4 The two CS lines

- **BODY_CS** is high for the whole of each body frame (Google Doc; NEX-7:
  `https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-clocks-and-sync-lines.txt`
  lines 17-23; our board: `4_Firmware/include/bsk_phy.h`, `bsk_phy_send()`: BODY_CS raised, the bytes,
  BODY_CS lowered). The Samyang AF 135 drops every byte received while BODY_CS is low (static
  analysis, not observed).
- **LENS_CS** is high while the lens transmits. The Samyang AF 135 waits for BODY_CS low, raises
  LENS_CS, sends, waits more than 200 µs, then lowers LENS_CS (static analysis, not observed).
- **LENS_CS does not delimit a frame for our receiver.** The Sony FE 24-105 G lowers LENS_CS as soon
  as its last byte leaves, before the UART has delivered it; judging the frame at the falling edge
  truncated its reply to `0x01`. Our board therefore accumulates bytes, extracts a frame as soon as
  its own length field is satisfied, skips any byte that cannot start a frame, and drops an
  incomplete frame after 20 ms of silence (`4_Firmware/components/phy/phy_common.h`, reception
  comment, `RX_STALE_US`; `phy_common.c`, `rx_next()`, `fr_scan()`).
  - *Disagreement.* weiziqian's README (§ "UART parameters") says data flows only while the
    matching CS is high, with exactly one frame per CS-high period, "framing therefore needs no
    heuristic"; his `docs/frame_format.md` adds that a window can be longer or shorter than the frame
    and that a receiver must parse by the frame length, bounded by the bytes received. Ours rests on
    the code above and the Sony 24-105 behaviour it was written for.
- **Both CS high at once**: during the power-up handshake (§ 2), and during the switch to 1.5 Mbaud
  (weiziqian README: both high, no data, about 5 ms).
  - *Disagreement.* weiziqian calls the speed change "the only time both are high". In our
    handshake both are high from the lens raising LENS_CS until the body lowers BODY_CS, by design
    (`session.c`, steps `S_HS_UP` to `S_HS_HOLD`), and the Sony 24-105 completes it
    (`traces/195_sony.txt` line 8, `result=ok lens_cs_up_us=3140`); the real NEX-7 also keeps
    BODY_CS high after LENS_CS rises
    (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-clocks-and-sync-lines.txt`
    lines 13-15, LENS_CS from the emulator).

### 1.5 XDETECT and the powering of the lens (our board)

These are rules of our board, not of the protocol; they are given because a body rewrite needs them.

- XDETECT is the last contact made on insertion and the first broken on removal; while the lens
  slides, the other contacts touch the neighbouring pads. A drop of XDETECT lasting more than 2 ms
  cuts both rails and puts TXD, BODY_CS and VD at rest at once; an insertion counts after 300 ms of
  stable contact (`4_Firmware/include/bsk_phy.h`, the comment on the XDETECT lock;
  `4_Firmware/components/phy/phy_common.h`, `D2_DROP_US`, `D2_DEBOUNCE_US`).
- Lines at rest are high-impedance and pulled low: a line held high towards an unpowered lens
  powers it through its protection diodes. The lines are driven only once both rails are up: logic
  rail, 50 ms, motor rail, 50 ms (`bsk_phy.h`, `bsk_phy_lines()`; `session.c`, `RAIL_MS`).

---

## 2. Power-up handshake

The handshake opens the lens's serial port. Before it, a freshly powered lens hears nothing.

**What the lens does** (Samyang AF 135; static analysis, not observed):

| Step | Lens |
|---|---|
| 1 | waits for BODY_CS high |
| 2 | BODY_CS still high after more than 1 ms: raises LENS_CS, then one more output line |
| 3 | waits for one of its input lines to be high for more than 1 ms, then opens its UART at 750 000 baud |
| 4 | waits for BODY_CS low for more than 1 ms |
| 5 | lowers LENS_CS: the link is up, every known message is now served (§ 6.1) |

"More than 1 ms" is counted on a 1 ms tick, so 1 to 2 ms in real time. Which mount contacts the
extra output and input lines of steps 2 and 3 are is not established; our board holds TXD at its
idle high level from before the handshake (`session.c`, step `S_RAIL_MOTOR`, which routes TXD to
the UART before the first frame). A step that does not progress returns to step 1 after 10 ms (`session.c`, comment of
`HS_LOW_MS`).

**What our board does** (`4_Firmware/components/session/session.c`, `on_due()`, constants
`HS_LOW_MS` = 15, `HS_HOLD_MS` = 3, parameter `handshake_ms` = 500):

| Step | Body |
|---|---|
| 1 | rails up (§ 1.5), VD started, lines driven: TXD idle high, BODY_CS low, for 15 ms |
| 2 | BODY_CS high; LENS_CS awaited high, 500 ms at most (already high counts) |
| 3 | LENS_CS high: 3 ms, then BODY_CS low; LENS_CS awaited low, 500 ms at most |
| 4 | the `0x01` (§ 7.1) within about 1 ms |

A missed deadline at step 2 or 3 is not a failure: the board sends the `0x01` anyway and the reply
decides.

**Observed timings.**

- Sony FE 24-105 G on our board: LENS_CS rises 3140 µs and 9049 µs after BODY_CS
  (`traces/195_sony.txt` line 8, `traces/sony.txt` line 8).
- NEX-7 (body real, lens emulated): BODY_CS rises at 7208717 µs, LENS_CS 1000 µs later, BODY_CS
  falls 5234 µs after rising, LENS_CS falls 317 µs later, and the first frame (BODY_CS high again)
  starts 4888 µs after that
  (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-clocks-and-sync-lines.txt`
  lines 13-17).

**A lens that stayed powered** while the body restarted does not shake hands again: the Samyang AF
135 never raises LENS_CS, and answers the `0x01` all the same
(`4_Firmware/traces/sy135-2026-09-20-full.txt` lines 7-9). A Sony FE 24-105 G left powered holds
LENS_CS high before the handshake (`4_Firmware/include/bsk_phy.h`, `bsk_phy_lens_cs()`); this is
why our board reads the level of LENS_CS, not only its edges. Nothing in the Samyang or Tamron
firmware brings the lens back to the handshake on a silent body (static analysis, not observed).

**After sleep** (`0x16`, § 7.15), a Samyang lens replays the same handshake, at the speed
negotiated before (static analysis, not observed).

---

## 3. The frame

### 3.1 Layout

| Frame byte | Field | Value |
|---|---|---|
| 0 | start | `F0` |
| 1-2 | length | u16, **total** frame length, start and end bytes included = payload + 8 |
| 3 | class | 1, 2 or 3 (§ 3.3) |
| 4 | sequence | § 3.4 |
| 5 … L−4 | payload | one or more messages, each `[type][fixed number of bytes]` (§ 4) |
| L−3 … L−2 | checksum | u16, sum modulo 65536 of frame bytes 1 to L−4 (length, class, sequence, payload); `F0` and `55` excluded |
| L−1 | end | `55` |

Sources: the code (`4_Firmware/components/phy/phy_common.c`, `fr_encode()`, `fr_sum()`,
`fr_decode()`; `4_Firmware/include/bsk_phy.h`, `BSK_FRAME_OVERHEAD`); the Google Doc and weiziqian's
`https://github.com/weiziqian/E-mount-protocol-RE/blob/main/docs/frame_format.md` (checksum verified
on 3 393 frames). Every frame cited in this document has a correct checksum.

**Worked example**, the `0x0B` exchange:
`F0 0B 00 02 00 0B 60 00 78 00 55`
(`4_Firmware/traces/sy135-2026-09-20-full.txt` line 21;
`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` line 15).
Length `0x000B` = 11 = 3 + 8. Checksum `0B + 00 + 02 + 00 + 0B + 60 + 00` = `0x0078`.

### 3.2 Length limits and bad frames

- Smallest frame: 9 bytes, one 1-byte message (`phy_common.c`, `fr_decode()`).
- **Body → lens frames stay under 256 bytes.** The Samyang AF 135 drops any received frame whose
  length high byte is not zero (static analysis, not observed). Our board never sends more than
  255 bytes and rejects any received frame longer than 255 (`bsk_phy.h`, `BSK_FRAME_MAX`); the
  longest reply it asks for is the `0x08`, 202 + 8 = 210 bytes.
- **Lens → body frames can be longer**: the Samyang reply to `0x3B` is 492 bytes of message (static
  analysis, not observed). Our board asks for none.
- **Padding.** A Sony a9 II clocks 32 bytes in every body window of the loop whatever the frame
  inside (weiziqian, `docs/frame_format.md`); bytes after the `55` are to be ignored (Google Doc).
- **A bad frame** (start, end, checksum, class):
  - Samyang AF 135: dropped in silence; it resynchronises only after the announced length, so a
    stray byte taken for `F0` can swallow the next frame (static analysis, not observed);
  - Tamron F051: answers a `0x02` (§ 7.2) (static analysis, not observed);
  - Sony FE 24-105 G: answers `02 FF FF 01 00 00 00 00` or `02 FF FF 02 00 00 00 00` in class 2
    (`traces/emount-bench-2026-09-28T17-45-01-034Z.txt` lines 64 and 67);
  - our board: the rejected bytes are reported as one `E_FRAMING` error, for the log; no reply
    (`phy_common.c`, `rx_next()`; `4_Firmware/include/bsk_phy.h`, `bsk_phy_raw_t`).

### 3.3 Classes

| Class | Carries | Proof |
|---|---|---|
| 1 | the loop: `0x03`, `0x04` and what is appended to the `0x04` (B→L); `0x05`, `0x06` and what is appended to the `0x06` (L→B). Our board also sends a lone `0x1C` in class 1 | public: `https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` lines 28-31; our board: `4_Firmware/components/txn/txn.c`, `bsk_txn_send()`, `traces/emount-bench-2026-09-29T18-12-13-937Z.txt` line 114 |
| 2 | request and reply: the init messages, `0x0A`, `0x10`, `0x16`, and every request of our board | `4_Firmware/traces/sy135-2026-09-20-full.txt` lines 8-39; `traces/sony.txt` lines 10-32; `https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` lines 15-24 and 462-465; `txn.c`, `request_out()` |
| 3 | L→B only: in both makers' emission tables, `0x02` (8 bytes), `0x17` (3), `0x3A` (24) and `0x3B` (492). Never seen on a trace here | static analysis, not observed |

- A Samyang reply takes the class of its first message's entry in the lens's emission table (static
  analysis, not observed). The Sony FE 24-105 G sends its `0x02` in class 2
  (`traces/emount-bench-2026-09-28T17-45-01-034Z.txt` line 64); the Tamron F051 sends it in class 3
  (static analysis, not observed).
- Receivers accept classes 1 to 3: the Samyang AF 135 (static analysis, not observed) and our board
  (`phy_common.c`, `fr_decode()`).
- *Disagreement.* weiziqian, `docs/frame_format.md` § "Class byte": only `0x01` and `0x02`,
  CERTAIN. The Google Doc lists "?? 0x03". Ours is static only.

### 3.4 Sequence numbers

- **Request/reply frames from a real body carry sequence 0**: NEX-7
  (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt`
  lines 15-23 and 462-464); weiziqian `docs/frame_format.md`. **Lens replies carry 0**
  (`traces/sony.txt` lines 11-32; `4_Firmware/traces/sy135-2026-09-20-full.txt` lines 9-39). A
  Samyang lens resets its own count to 0 whenever it sends a class 2 or 3 frame (static analysis,
  not observed).
- **Our board** keeps one counter for all it sends, advanced after each frame sent (once per loop
  pair), wrapping from `0xEF` to 0 (`4_Firmware/components/txn/txn.c`, `seq_advance()`). Its init
  frames therefore carry non-zero numbers (`traces/sony.txt` line 10: `25`; line 12: `26`). The
  lenses answer them (`traces/sony.txt` lines 11-32); the Samyang AF 135 does not check the body's
  number (static analysis, not observed). Contested, § 11.
- **In the loop**, the `0x03` and the `0x04` of one VD share a number (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` lines 28-29, both
  `2A`; `txn.c`, `pair_due()`). The lens's `0x05` and `0x06` carry the number of the last `0x03`
  received plus one per VD (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` lines 28-31: body `2A`, lens `2B`; Google Doc: "the lens will
  always report the last seen sequence number plus 1"; on the Samyang AF 135, the number of each
  `0x03` is copied and advanced at each VD, static analysis, not observed).
- **Wrap.** The lens goes from `0xEF` to `0x00`
  (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-55mm-init.txt`
  lines 738 and 742). The NEX-7 body goes from `0xEF` to `0x80`, counts `0x80` to `0x8F`, then
  restarts at `0x00` (same file, lines 740, 744, 804 and 808; the same pattern in
  `https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-voigtlander-15mm-4.5-30cm-to-inf.txt` lines 405 and 469); what the
  range `0x80`-`0x8F` means is unknown. Our board wraps `0xEF` → `0x00` (`seq_advance()`).
  - *Disagreement.* weiziqian, `docs/frame_format.md` § "Sequence byte": "increments by 1 per cycle,
    wrapping at 256". The Google Doc: "the sequence count appears to reset after 0xEF". The traces
    above support the Google Doc for the lens and show a third behaviour for the NEX-7 body.
- The Tamron F051 passes the number of each class 1 frame to its link layer; what it does with it
  is unknown (static analysis, not observed).

---

## 4. Messages inside a frame

A payload is a sequence of messages. Each message is a type byte followed by a number of bytes
fixed by the type; nothing in the frame gives the size of each message. The receiver walks the
payload with a size table (Samyang AF 135 and Tamron F051, whose receive sizes are identical for
every type both implement; static analysis, not observed). Consequences:

- a message of the wrong size shifts everything after it: the next bytes are read as types. The
  Tamron F051 then answers a `0x02` code 4; the Samyang AF 135 walks until its count equals the
  frame length exactly, and a type it has no size for (0x00, 0x02, 0x05, 0x06, and others) makes it
  loop forever, silent until its power is cut (static analysis, not observed);
- our board sends a frame only if its messages pave the payload exactly with the sizes of its
  whitelist (`4_Firmware/components/bench_core/bench_core.c`, `allowed()`, `listed_size()`);
- our board does not pave received frames: it reads the type of the first message and fixed
  offsets (`4_Firmware/components/session/lens_rx.c`, `lens_rx_frame()`; § 7.6 for the `0x06`).

The sub-messages of the focus loop (`1C`, `1D`, `1F`, `22`, `2E`, `3C`, `2F`) are ordinary messages
in this paving: the lens dispatches them by type like any other (static analysis, not observed).
weiziqian calls them "records" of the `0x04` (`docs/msg_0x04.md`); § 7.4.

### 4.1 Body → lens sizes

| Type | Size | Sent by our board | Source of the size |
|---|---|---|---|
| `0x01` | 33 | yes | traces (§ 7.1); `bench_core.c`, `listed_size()` |
| `0x03` | 21 | yes | traces; `listed_size()` |
| `0x04` | 14 | yes | traces; `listed_size()` |
| `0x07` | 2 | yes | traces; `listed_size()` |
| `0x08` | 9 | yes | traces; `listed_size()` |
| `0x09` | 5 | yes | traces; `listed_size()` |
| `0x0A` | 17 | yes | traces; `listed_size()` |
| `0x0B` | 3 | yes | traces; `listed_size()` |
| `0x0C` | 2 | **never** | static analysis, not observed; weiziqian `docs/msg_0x0C.md` |
| `0x0D` | 2 | yes | traces; `listed_size()` |
| `0x10` | 2 | yes | traces; `listed_size()` |
| `0x14` | 16 | **never** | static analysis, not observed |
| `0x15` | 1032 | **never** | static analysis, not observed (it cannot pass the Samyang 255-byte limit, § 3.2) |
| `0x16` | 2 | **never** | `https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` line 464 |
| `0x19` | 2 | no | static analysis, not observed |
| `0x1B` | 6 | no | static analysis, not observed |
| `0x1C` | 1 | yes | `listed_size()`; weiziqian `docs/msg_0x04.md` |
| `0x1D` | 5 | yes | traces; `listed_size()`; weiziqian `docs/msg_0x04.md` |
| `0x1E` | 5 | no | static analysis, not observed |
| `0x1F` | 14 | no | static analysis, not observed; weiziqian `docs/msg_0x04.md` |
| `0x22`, `0x2E` | 3 | no | static analysis, not observed |
| `0x26`, `0x28`, `0x35` | 2 | no | static analysis, not observed |
| `0x2F` | 3 | **never** | `https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` line 92; weiziqian `docs/msg_0x04.md` |
| `0x34` | 24 | no | static analysis, not observed |
| `0x3A` | 24 | no | static analysis, not observed |
| `0x3B`, `0x3C` | 8 | no | static analysis, not observed |
| `0x3D` | 24 | no | static analysis, not observed |
| `0x3E` | 9 | no | static analysis, not observed |
| `0x3F` | 2 | yes | traces; `listed_size()` |
| `0x40` | 19 | Samyang only | `listed_size()`; `samyang.md` |

A message size table for types up to `0x2C`, read in a Sony adapter firmware, was posted by Leegong
on the Dyxum forum, "E-mount electronic protocol reverse engineering" (topic 119522), in May 2017.

### 4.2 Lens → body sizes

| Type | Size | Source |
|---|---|---|
| `0x01` | 33 | `4_Firmware/traces/sy135-2026-09-20-full.txt` line 9; `traces/sony.txt` line 11 |
| `0x02` | 8 | `traces/emount-bench-2026-09-28T17-45-01-034Z.txt` line 64 |
| `0x05` | 97 after a `0x0A` with masks `FF 7F` (§ 7.10); 109 on the Samyang AF 135 before any `0x0A` | 97: `4_Firmware/traces/sy135-2026-09-20-dump05.txt` line 5, `traces/sony-2.txt`, `https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` line 30; 109: static analysis, not observed |
| `0x06` | 40 after a `0x0A` with mask `3F`, plus what is appended (§ 7.6) | `4_Firmware/traces/sy135-2026-09-20-dump05.txt` line 11; `traces/195_sony.txt` line 37 (40 + 2) |
| `0x07` | 35 | `4_Firmware/traces/sy135-2026-09-20-full.txt` line 12; `traces/sony.txt` line 13 |
| `0x08` | 202 | `4_Firmware/traces/sy135-2026-09-20-full.txt` line 15; `traces/sony.txt` line 19 |
| `0x09` | 12 | `4_Firmware/traces/sy135-2026-09-20-full.txt` line 23; `traces/sony.txt` line 26 |
| `0x0A` | 17 | `4_Firmware/traces/sy135-2026-09-20-full.txt` line 39; `traces/sony.txt` line 32 |
| `0x0B`, `0x0D`, `0x10` | 3, 2, 2 | `4_Firmware/traces/sy135-2026-09-20-full.txt` lines 21, 25, 35 |
| `0x0C` | 2 | static analysis, not observed; weiziqian `docs/msg_0x0C.md` |
| `0x16` | 2 | `https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` line 465 |
| `0x17` | 3 | static analysis, not observed |
| `0x19` | 2 | static analysis, not observed |
| `0x1B` | 11 | static analysis, not observed; weiziqian `docs/msg_0x1B.md` (10 payload bytes) |
| `1C`, `1D`, `1F` / `22`, `2E` / `3C` (appended to the `0x06`) | 2 / 3 / 4 | `1D 00`: `traces/195_sony.txt` line 37; `1C 00`: `traces/emount-bench-2026-09-29T18-12-13-937Z.txt` line 116; the others: static analysis, not observed |
| `0x26`, `0x28`, `0x34`, `0x35` | 5, 36, 24, 40 | static analysis, not observed |
| `0x3A`, `0x3B`, `0x3D`, `0x3E` | 24, 492, 64, 12 | static analysis, not observed |
| `0x3F` | 66 | `traces/sony.txt` lines 15-16 |

---

## 5. Link speed: `0x0C`

| | |
|---|---|
| Request | `0C 02` (Google Doc: "appears to signal speed change"; weiziqian `docs/msg_0x0C.md`, Sony A6000) |
| Reply | `0C 01` from Sony lenses and a Viltrox adapter; a TECHART LM-EA9 replying `0C 00` makes a Sony a9 II abandon the handshake (weiziqian `docs/msg_0x0C.md`) |
| After | both CS high about 5 ms, then 1.5 Mbaud (weiziqian README) |

- **Bit 0 of the reply = speed accepted.** The Tamron F051 sets it only if bit 1 of offset 1 of its
  own `0x07` reply is set (`0x03` on the F051, § 7.7), then changes speed over several internal steps; the
  Samyang AF 135 answers `0C 01` at once, follows a second short CS handshake, and stays at
  1.5 Mbaud until its power is removed, a wake-up from sleep included (static analysis, not
  observed). This reading explains the a9 II giving up on `0C 00`.
  - *Disagreement.* weiziqian calls the exchange's meaning UNKNOWN; ours is static only.
- **Our board never sends `0x0C`** (`4_Firmware/include/bsk_bench_core.h`: refused) and stays at
  750 000 baud, which the Samyang AF 135 and the Sony FE 24-105 G accept for the whole session
  (§ 6). A lens left at 1.5 Mbaud by a camera is unreachable at 750 000 baud until its power is cut
  (static analysis, not observed, Samyang).

---

## 6. The session

### 6.1 Initialisation

**Order of the requests.**

| Body | Order | Source |
|---|---|---|
| NEX-7 | `01 07 0B 08 09 0D 10 0A` | `https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-clocks-and-sync-lines.txt` lines 23, 31, 41, 51, 59, 67, 75, 83 |
| Sony A6000 | `01 07 0C 0B 08 09 0D 10 0A` | weiziqian README § 3.1 |
| Sony a9 II | `01 07 0C 0B 3F 3D 08 09 0D 10 0A` | weiziqian README § 3.1 |
| our board | `01 07 3F 08 0B 09 0D 10 0A` | `4_Firmware/components/session/init.c`, `init_reply()` |

**Requests.**

| Type | NEX-7 | Our board |
|---|---|---|
| `0x01` | `01 FF FF FF FF FF FF FF 01` + 24 × `00` (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-clocks-and-sync-lines.txt` line 23) | the same (`init.c`, `INIT01`) |
| `0x07` | `07 00` (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-clocks-and-sync-lines.txt` line 31) | `07 00` (`INIT07`) |
| `0x3F` | — | `3F 00` (`INIT3F`) |
| `0x08` | `08 C0 21 00 00 33 09 62 02` (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-clocks-and-sync-lines.txt` line 51) | `08 06 00 00 00 00 00 00 00` to a recognised Samyang, `08` + 8 × `00` otherwise (`body08_flags()`) |
| `0x0B` | `0B 60 00` (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-clocks-and-sync-lines.txt` line 41) | `0B 60 00` (`INIT0B`) |
| `0x09` | `09 00 00 00 00` (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-clocks-and-sync-lines.txt` line 59) | the same (`INIT09`) |
| `0x0D` | `0D 00` (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-clocks-and-sync-lines.txt` line 67) | `0D 00` (`INIT0D`) |
| `0x10` | `10 1F` (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-clocks-and-sync-lines.txt` line 75); the A6000 sends `10 3F` to a Viltrox adapter (weiziqian `docs/msg_0x10.md`) | `10 1F` (`INIT10`) |
| `0x0A` | `0A FF 7F 00 00 00 00 00 00 3F` + 7 × `00` (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-clocks-and-sync-lines.txt` line 83) | the same (`INIT0A`) |

**What the lens requires.** Neither the Samyang AF 135 nor the Tamron F051 checks that an earlier
init message was received before serving the next (static analysis, not observed). They differ on
*when* init messages are served (static analysis, not observed):

- Samyang: every known type, at any time, before or after the `0x0A`; an init served while the lens
  is already streaming is shown by `4_Firmware/traces/sy135-2026-09-20-full.txt` lines 8-41 (the
  lens stayed powered; 29 `0x05` were received during the init, line 41);
- Tamron: init messages only before the first `0x0A` and while no deferred reply is pending; after
  it, a `0x02` code 4 (details in `tamron.md`).

**Reply delays observed.** Samyang AF 135: 5 to 9 ms for the init replies, 6 ms for the `0x0A`,
675 ms for the `0x10` (`4_Firmware/traces/sy135-2026-09-20-full.txt` lines 8-39). Sony FE 24-105 G: 0 to 4 ms on its clock, 22 ms for the `0x0D`,
214 ms for the `0x10` (`traces/sony.txt` lines 10-32). Sony 28-70 with a NEX-7: 583 ms for the `0x10`
(`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` lines 21-22). Sony SELP1650: about 1.3 s for the `0x10` (weiziqian `docs/msg_0x10.md`).

**Our board's policy** (`init.c`; deadlines in `session.c`, `bsk_session_params_default()`):

| Request | Deadline | If unanswered |
|---|---|---|
| `0x01` | 300 ms | three tries; then once per session a "soft reset", the `0x0A` alone (400 ms), and the whole init again; silent after that: a counted failure |
| `0x07`, `0x3F`, `0x08` | 300, 300, 400 ms | tolerated: no identity, no name, no aperture range |
| `0x0B`, `0x09`, `0x0D` | 300 ms each | required: the init stops at the first missing one, the loop is started anyway |
| `0x10` | 8 s | the loop is started anyway |
| `0x0A` | 400 ms | the loop is started anyway |

- `0x07`, `0x3F`, `0x08`, `0x0B`, `0x09`, `0x0D` are re-sent identical, three sends in total at
  most, because receiving them twice changes nothing in either maker's lens (static analysis, not
  observed). **`0x0A` and `0x10` are never re-sent**: a second `0x0A` toggles a Tamron back to its
  init phase and stops a Samyang's motors; a second `0x10` restarts a homing (`init.c`,
  `idempotent()`).
- A reply shorter than what is read from it counts as no reply: `0x07` under 4 bytes, `0x08` under
  6, `0x10` under 2 (`lens_rx.c`, `lens_rx_full()`).
- The loop (§ 6.2) starts after the `0x0A` reply, or, for a recognised Samyang, with the `0x10`
  (`init.c`, `loop_start()`); why is in `samyang.md`.
- After the init the board waits for a first `0x05`: 500 ms, then 300 ms more before counting a
  failure (`session.c`, `first05_ms`, `probe05_ms`).

### 6.2 The loop, one frame per VD

**Body side.**

- One pair per VD: a `0x03` frame, then a `0x04` frame, class 1, same sequence number.
- A real NEX-7 starts the `0x03` 8.59 ms and the `0x04` 10.12 ms after the VD falling edge
  (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-clocks-and-sync-lines.txt`
  lines 85, 87 and 93: VD at 7258313 µs, BODY_CS at 7266899 and 7268434 µs). Our board uses the
  same phase: `0x03` at VD + 8.6 ms, `0x04` at VD + 10.1 ms (`4_Firmware/components/txn/txn.c`,
  `PAIR03_US`, `PAIR04_US`).
- Focus messages travel **after the `0x04`, in the same frame**: NEX-7
  `04 … 1D FF FF 10 2C` (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` line 365); our board, `traces/195_sony.txt` line 36
  (`txn.c`, `bsk_txn_attach()`).
- Our board sends nothing between its `0x03` and its `0x04`: a frame asked for in that interval
  leaves right after the `0x04` (`txn.c`, `hold()`, `release()`).

**Lens side.**

- One `0x05` frame then one `0x06` frame per VD, class 1. With a NEX-7 the Sony 28-70 sends its
  `0x05` 9.8 ms after the body's `0x04` and its `0x06` 0.74 ms later (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` lines 29-31).
- **When a lens streams** (when it sends `0x05`/`0x06` without being asked):
  - Sony 28-70: from the `0x0A` reply on, before the body's first `0x03` (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` lines 24-28);
  - Samyang AF 135: only after a `0x0A` whose masks are not all zero, and then one `0x05` per
    `0x03` received and one `0x06` per `0x04` received, in the next VD period; a body that stops
    sending the pair gets no more status, and the lens's own processing stops with it (static
    analysis, not observed; `samyang.md`);
  - Tamron F051: after the first `0x0A`, on its own frame timer, whether or not `0x03`/`0x04` arrive
    (static analysis, not observed).
- **Stopping the stream**: a `0x0A` with all masks at zero. After the NEX-7's `0A 00 … 00` and its
  echo, the Sony 28-70 sends no `0x05`/`0x06` for the 549 ms before its `0x16` reply (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt`
  lines 462-465), although the same lens streamed without any `0x03` at start-up (lines 24-28).
- **Loss**: our board declares the link lost after 2 s with neither `0x05` nor `0x06`, and forgets
  the data of a stream silent for 2 s (`session.c`, `lost_ms`, `watch_due()`). Neither Samyang nor
  Tamron has any session timeout: a lens whose body goes quiet keeps its state (static analysis,
  not observed).

### 6.3 Shutdown

What a NEX-7 does at power-off, with a Sony 28-70
(`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt`):

| Line | Body | Lens |
|---|---|---|
| 360-361 | `0x03` offsets 0-1 `C2 2E` → `EC 00`, offset 10 `06` → `02`; `0x04` offset 10 `01` → `09` | |
| 365, 445, 453, 461 | `1D FF FF 10 2C`, then three `1D 00 00 00 2C` (§ 7.4.2) | `1D 00` two frames later (371, 451, 459) |
| 373 | `0x04` offset 6 `3D` → `00` | |
| 462-463 | `0A` + 16 × `00` | the echo |
| 464-465 | `16 01` | `16 00`, 549 ms later |

Our board never sends `0x16` (`bsk_bench_core.h`: refused). It ends a session by stopping the loop,
putting TXD, BODY_CS and VD at rest, and cutting the rails (`session.c`, `to_off()`, `lines_rest()`).

---

## 7. The messages

### 7.1 `0x01` — capabilities (B→L and L→B, class 2, 33 and 33)

- **Request**: `01 FF FF FF FF FF FF FF 01` + 24 × `00`, from the NEX-7 and from our board (§ 6.1).
- **Reply**: offsets 0-31.
- **Bitmap rule**: bit *n* of the little-endian bitmap starting at offset 0 set ⇒ message type *n* + 1
  supported (weiziqian, `https://github.com/weiziqian/E-mount-protocol-RE/blob/main/docs/msg_0x01.md`,
  "u64 LE bitmap", offsets 0-7). Applied:

| Who | Offsets 0-11 | Types listed |
|---|---|---|
| body, NEX-7 and ours | `FF FF FF FF FF FF FF 01 00 00 00 00` | `0x01` to `0x39` |
| Samyang AF 135 (`4_Firmware/traces/sy135-2026-09-20-full.txt` line 9) | `FF 9F 38 5D A2 60 18 5E 00 00 00 00` | 01-0D, 10, 14, 15, 16, 19, 1B, 1C, 1D, 1F, 22, 26, 28, 2E, 2F, 34, 35, 3A, 3B, 3C, 3D, 3F |
| Sony FE 24-105 G (`traces/sony.txt` line 11) | `FF 9F FF 5D EE 60 18 DE FF 0F F8 01` | 01-0D, 10-19, 1B, 1C, 1D, 1F, 22, 23, 24, 26, 27, 28, 2E, 2F, 34, 35, 3A, 3B, 3C, 3D, 3F, 40-4C, 54-59 |

- The Sony 24-105 sets bits beyond offset 7: read by the same rule they name types `0x41` to
  `0x59`; whether the rule extends there is unknown.
- Both lens bitmaps list `0x02`, `0x05` and `0x06`, types these lenses send; the Samyang AF 135
  blocks on receiving them (§ 4), so a set bit does not mean "send it to me".
- **Our board reads only the presence of the reply** (`init.c`, step `S_Q01`). The Samyang AF 135
  reads three bytes of the request to set internal modes (static analysis, not observed;
  `samyang.md`).

### 7.2 `0x02` — refusal (L→B, 8 bytes)

`02 <class> <type> <code> 00 00 00 00` (Tamron F051; static analysis, not observed):

| Code | Cause | Class and type fields |
|---|---|---|
| 1 | last byte not `55` | `FF FF` |
| 2 | first byte not `F0` | `FF FF` |
| 3 | wrong checksum | `FF FF` |
| 4 | a message of type 0 or ≥ `0x40`, or a type refused in the current state | class received; type = the **first** message of the frame, `FF` if the refused one was first |

- One `0x02` per frame at most; messages before the refused one have been executed (static
  analysis, not observed).
- The Sony FE 24-105 G sends this format, in class 2: `02 FF FF 01 00 00 00 00` and
  `02 FF FF 02 00 00 00 00` (`traces/emount-bench-2026-09-28T17-45-01-034Z.txt` lines 64 and 67).
- The Samyang AF 135 has a `0x02` in its emission table and never sends it (static analysis, not
  observed).
- Our board does not decode it: a `0x02` is not the reply of a request, which then runs to its
  deadline (`txn.c`, `bsk_txn_on_event()`, case `BSK_PHY_FRAME`).

### 7.3 `0x03` — body state, per frame (B→L, class 1, 21 bytes)

| Offset | NEX-7 (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` lines 92-108) | Our board (`txn.c`, initial `m03`) | What lenses do with it |
|---|---|---|---|
| 0-1 | `C2 2E` in the loop; `12 41` in the first frame (line 28); `EC 00` at shutdown (line 360) | `C2 2E` | unknown; weiziqian (`docs/msg_0x03.md`): body state, varies per frame |
| 2 | `00` | `00` | — |
| 3-4 | aperture code (§ 8.1), `0x1459` here | aperture code | Samyang AF 135: the iris target; Tamron F051: the iris target if bit 7 of offset 7 is 0 (static analysis, not observed) |
| 5-6 | the same code | the same code (`session.c`, `session_aperture_hold()`) | Tamron F051: the iris target if bit 7 of offset 7 is 1 (static analysis, not observed) |
| 7 | `1C`; `94` in the first frame (line 28) | `1C` | Tamron F051: bit 7 chooses 3-4 or 5-6 (static analysis, not observed) |
| 8-9 | `00 00` | `00 00` | — |
| 10 | "cycle" length: `06` in the loop, `01` first frame, `02` at shutdown | `06` | the cycle of the `0x1D` wait bit (§ 7.4.2) |
| 11 | counter `00` … `05`, one step per frame (lines 92, 96, 100, 104, 108) | `00`, constant | the `0x1D` wait bit only (§ 7.4.2) |
| 12 | `00`/`01`, alternating each frame | bit 4 set for 30 pairs after each `0x1D`, `00` otherwise (`4_Firmware/components/session/drive.c`, `AF_OFF`, `AF_BIT`, `AF_FRAMES`) | Samyang AF 135: bits 3-4 choose the focus speed profile, bit 3 also the iris profile; Tamron F051: bits 3-4 choose the iris mode; no maker conditions the start of a `0x1D` on bit 4 (static analysis, not observed) |
| 13-14 | `02 00` | `02 00` | Tamron F051: some values choose the iris mode (static analysis, not observed) |
| 15 | `02` or `03` | `02` | unknown |
| 16 | `01` | `01` | unknown |
| 17-19 | `00 00 00` | `00 00 00` | — |

- **Aperture.** Our board writes the same code at 3-4 and 5-6; f/1.8 (`0x11B2`) at the start of
  each session, bounded to the range of the `0x08` reply (`session.c`, `AP_F18`; `cmd.c`,
  `session_aperture()`). weiziqian (`docs/msg_0x03.md`) reads 5-6 as the aperture the body asks
  for and 3-4 as `0x1000` in the first frame, equal to 5-6 afterwards.
- **Length.** Sony bodies append a `2F xx xx` (§ 7.4.6) to the `0x03` of native lenses: 24 bytes of
  messages per frame (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` line 92; weiziqian: 23 payload bytes to natives, 20 to adapters).
  Our board never appends it.

### 7.4 `0x04` — body state and focus orders (B→L, class 1, 14 bytes + appended messages)

**Header**, offsets 0-12:

| Offset | NEX-7 | Our board (`txn.c`, initial `m04`) | Notes |
|---|---|---|---|
| 0-1 | `00 00` | `00 00` | constant on every body (weiziqian `docs/msg_0x04.md`) |
| 2 | `19` | `19` | constant |
| 3 | `83` with Sony AF lenses (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` line 29; `https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-55mm-init.txt` line 109), `81` with manual lenses and during the first frames of the 55 mm (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-voigtlander-15mm-f4.5-inf.txt` line 14, `https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-zeiss-28mm-at-2.8-init.txt` line 27, `https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-55mm-init.txt` line 29) | `81`; bit 1 set (`83`) while the lens ring drives the aperture (`4_Firmware/components/session/ring.c`, `role()`, `BODY_AF_OFF`, `BODY_AF_BIT`) | bit 1 below |
| 4-5 | `00 00` | `00 00` | constant |
| 6 | `3D` with the Sony lenses; `55` with the manual lenses (same lines); `00` from the start of shutdown (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` line 373) | `3D` | weiziqian: `3B`, `28`, `21`, `18` on other bodies; meaning unknown |
| 7 | `00` (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` line 29) | `00` | contested, § 11; weiziqian: `1F` from an A6000 to a lens with an E-mount ID, `00` to a legacy ID |
| 8-9 | `00 00` | `00 00` | — |
| 10 | `01` in the loop, `09` at shutdown (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` line 361) | `01` | weiziqian: a state code; Sony a9 II `09` idle, `40`/`41` with focus orders |
| 11-12 | `00 00` | `00 00` | — |

**Bit 1 of offset 3.**

- Tamron F051: set, the lens's manual focus ring is decoupled (static analysis, not observed).
- Samyang AF 135: one of the conditions for narrowing its soft focus limits, and read when a
  movement is stopped (static analysis, not observed; `samyang.md`).
- weiziqian (`docs/autofocus.md`): `83` = the body drives focus, `81` = the lens ring drives focus,
  POSSIBLE.
- Our board: set when the ring is given the aperture, "the lens lets go of focus" (`ring.c`).
- Neither maker tests it on the path of a `0x1D` (static analysis, not observed).

**Appended messages.** They follow the `0x04` in its frame; weiziqian has seen two in one frame
(`2F` + `1C`, `2F` + `1F`; `docs/msg_0x04.md`). Our board appends at most one (`txn.c`,
`bsk_txn_attach()`).

#### 7.4.1 `0x1C` — stop (1 byte)

- Stops the current focus order; replaces any order in progress (static analysis, not observed).
- **Acknowledgement**, appended to a later `0x06`: `1C 00` on the Sony FE 24-105 G
  (`traces/emount-bench-2026-09-29T18-12-13-937Z.txt` lines 114-116); `1C 00` on the Samyang AF 135,
  after the motor stops, or after a forced stop at 1.2 s; `1C 01` on the Tamron F051 (static
  analysis, not observed).
- Our board sends it alone in a class 1 frame, and re-sends it every 1.5 s, three sends at most,
  until an acknowledgement `1C` of any value or a still lens (`4_Firmware/components/session/motion.c`,
  `STOP_MS`, `SENDS`, `stopped()`; `drive.c`, `drive_stop()`).
- No `0x1C` is needed between two `0x1D` in either maker's lens (static analysis, not observed).

#### 7.4.2 `0x1D` — move focus (5 bytes)

| Offset | Field |
|---|---|
| 0-1 | value, u16 (signed in relative mode) |
| 2 | oscillation amplitude |
| 3 | flags |

**Flags**, identical layout in the Tamron F051 and twelve Samyang firmwares (static analysis, not
observed):

| Bits | Meaning |
|---|---|
| 0-1 | **unit** of the value: 0 = motor steps; 2 = multiples of a unit the lens defines; 3 = Sony distance unit (§ 8.3); 1 = refused by both makers |
| 2 | 0 = absolute target, 1 = signed relative move |
| 3 | wait for the cycle: the order starts only at a given point of the cycle counted by offsets 10-11 of the `0x03` (Samyang: while offset 11 ≠ 0 it waits; Tamron: it starts on the tick after offset 11 = offset 10 − 1) |
| 4-5 | scale of an oscillation whose amplitude is offset 2 |
| 6-7 | side of arrival for an absolute target (overshoot, then come back) |

- The makers differ on which units they refuse and on the constants of bits 6-7 (`samyang.md`,
  `tamron.md`).
- **The NEX-7** sends `1D FF FF 10 2C` and `1D 00 00 00 2C` (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` lines 365, 445, 453, 461).
  By the layout above: relative, cycle wait, oscillation scale 2, motor steps, value −1 with
  amplitude `0x10`, then 0. The Sony 28-70's `0x06` position goes from 16382 to 16381 (lines 363
  and 367).
- **Our board** sends `1D lo hi 00 00`: an absolute target in motor steps, no wait, no oscillation,
  no side of arrival (`drive.c`, `drive_goto()`). Its targets stay inside the limits of the `0x06`,
  narrowed by 5 steps (§ 7.6). It re-sends the same `0x1D` after 1 s with neither movement nor
  acknowledgement, three sends at most (`motion.c`, `START_MS`, `SENDS`, `motion_expire()`). The
  Sony FE 24-105 G moves on it (`traces/195_sony.txt` lines 36-37 and 49-51).
- **Target beyond a limit**: clamped to the limit, without error. Sony FE 24-105 G: `1D 00 00 00 00`
  ends at its lower limit, 15331 (`traces/emount-bench-2026-09-29T18-12-13-937Z.txt` lines 408 and
  416-430). Samyang and Tamron: same clamping; already at the limit, no movement and still an
  acknowledgement (static analysis, not observed).
- **Acknowledgement** `1D xx`, appended to a later `0x06`:
  - Sony FE 24-105 G: `1D 00` in the **first** `0x06` after the order, 20 ms later, while offsets
    20-21 are still 223 steps from the target (`traces/195_sony.txt` line 37): on this lens the
    acknowledgement does not mean "arrived";
  - Sony 28-70 with a NEX-7: `1D 00` two frames after the order, placed **before** the `0x06` in its
    frame (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` line 371);
  - Samyang AF 135: always `1D 00`, when the motor stops; an order replaced by a `1D`, `1F`, `3C`
    or `1C` is acknowledged `1D 00` too; a `0x0A` cancels it without acknowledgement (static
    analysis, not observed);
  - Tamron F051: `1D 00` done, `1D 01` refused unit or order interrupted by a `3C`; an order
    replaced by another `0x1D` gets one acknowledgement, the last; replaced by `1C`, `1F` or `34`,
    none (static analysis, not observed).
- *Disagreements* with weiziqian:
  - flags: "bits 0-2 = mode (0, 3, 4, 6), bit 3 ignored, bits 4-7 unknown" (`docs/autofocus.md`).
    His modes 0, 3, 4, 6 are exactly our unit 0 or 3 with or without bit 2; we differ on bit 3 and
    on bits 4-7, on static grounds only;
  - "`0x1D` is probably a frame length, not a message ID; no manufacturer is known to implement
    one" (`docs/msg_0x1D.md`), while his `docs/autofocus.md` describes it as a record of the `0x04`.
    Ours: both makers dispatch type `0x1D` like any other type (static analysis, not observed), and
    it is on the wire as a message after the `0x04` (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` line 365; `traces/195_sony.txt`
    line 36);
  - a `0x1D` replaced by a new one "has no completion event" (`docs/autofocus.md`). The Samyang AF
    135 acknowledges the replaced one `1D 00` (static analysis, not observed); our code expects it
    (`motion.c`, comment of `mv_frame()`).

#### 7.4.3 `0x1F` — scan (14 bytes)

A sweep of focus across a window at a steady speed, so that the body can sample the image while
focus moves (weiziqian `docs/autofocus.md` § 3.3, "Scan"). The Samyang AF 135 and the Tamron F051
cut the 13 bytes after the type into the same fields (static analysis, not observed). The meanings
below are those of the Samyang AF 135 (static analysis, not observed); what the Tamron F051 does
with each field is not established.

| Offset | Bits | Field |
|---|---|---|
| 0 | 0-1 | window: 0 = the lens's soft focus limits; 1 = two other thresholds of the lens, those of `0x06` offset 0 bits 3-4 (§ 7.6); 2 = from B to A (offsets 4-7); 3 = around the centre C (offsets 11-12), from C − B to C + A, kept inside the soft limits |
| 0 | 2-3 | where the sweep starts: 0 = beyond A, going down; 1 = beyond B, going up; 2 or 3 = beyond the end nearer to the current position |
| 0 | 4-5 | window 3 only: 0 = A and B are measured from C; otherwise C is not used (in steps the window becomes the soft limits; in the lens unit A and B are positions) |
| 0 | 6-7 | not 0: the run-up of the first leg is doubled |
| 1 | — | divisor of the run-up and speed computations; must not be 0 |
| 2 | 0 | not read by the Samyang AF 135 |
| 2 | 1 | 1 = after the sweep, a third move back to C |
| 2 | 2-5 | unit of A and B, windows 2 and 3: 0 = motor steps. Window 2: all four bits set = Sony distance unit (§ 8.3); any other value = the soft limits replace A and B. Window 3: bits 2-3 = 2 and bits 4-5 = 2 = multiples of a unit the lens defines; any other value ends the scan at once |
| 2 | 6-7 | with offset 8, the run-out of the second leg |
| 3 | 0-5 | scan speed, magnitude |
| 3 | 6-7 | scan speed unit: 2 is the only value accepted; any other ends the scan at once |
| 4-5 | — | A, u16 |
| 6-7 | — | B, u16 |
| 8 | — | run-out of the second leg (with offset 2 bits 6-7) |
| 9 | — | run-up of the first leg |
| 10 | — | not read by the Samyang AF 135 |
| 11-12 | — | C, u16; 0 = the current position |

**What the Samyang AF 135 does** (static analysis, not observed): first leg, at a fixed speed, to
the start end plus the run-up; once the motor is still, second leg, at the scan speed, through the
window to the far end plus the run-out; then, if offset 2 bit 1 is set, back to C. It then
acknowledges `1F 00`. A first leg not finished within 1.2 s, or a field value it refuses, also ends
with `1F 00`. The Tamron F051 acknowledges `1F 00` or `1F 01` (static analysis, not observed).

**What a body sends.** weiziqian has seen an a9 II send `1F 02 02 83 88 3D 49 ED 47 3F 46 03 00 00`
and `1F 06 02 83 98 7D 45 F5 43 10 52 03 00 00` to a Yongnuo lens, after a `2F`
(`docs/autofocus.md` § 3.3; `docs/msg_0x04.md`). Read with the table above: window 2, starting beyond A
(`02`) or beyond B (`06`), steps, return to C, speed 8 or 24 with unit 2, A = 18749 or 17789,
B = 18413 or 17397.

- *Disagreement.* weiziqian reads offset 0 bit 0 as "centred form" and offsets 1-2 and 8-10 as
  unknown. With the observed values his "centred form" is our window 3 against window 2; we read
  offset 0 bits 0-1 as one field, and give offsets 1, 2, 8 and 9 a role, on static grounds only.
- Our board never sends it.

#### 7.4.4 `0x22` and `0x2E` — conversions (3 bytes)

`22 vL vH` converts a position in motor steps to the Sony distance unit (§ 8.3); `2E vL vH`
converts back. Nothing moves, and the focus order in progress is not replaced; the answer comes as
`22 vL vH` or `2E vL vH` appended to a later `0x06` (static analysis, not observed, both makers).
Our board never sends them.

- *Disagreement.* The Google Doc lists `0x22` as "absolute motor movement". Ours is static only.

#### 7.4.5 `0x3C` — drive to a limit (8 bytes)

A signed value at offsets 1-2: its sign gives the direction, and on the Tamron F051 its magnitude
gives the speed; the lens drives to its soft limit on that side and acknowledges
`3C xx 00 00` (4 bytes) when there, `xx` bits 0-1 = the direction received (static analysis, not
observed). weiziqian: "Drive", event `3C p 00 00` (`docs/autofocus.md`). Our board never sends it.

#### 7.4.6 `0x2F` — row index echo (3 bytes)

- The NEX-7 appends `2F xx xx` to every `0x03` of a native lens, two equal bytes, `15`, `16` or `17`
  (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` lines 92 and 104). The same values cycle in offsets 77-78 of the lens's `0x05`
  (§ 7.5).
- weiziqian (`docs/msg_0x03.md`, `docs/msg_0x05.md`): a row index of the optical table the lens
  streams, PROBABLE; a Yongnuo lens acts on it.
- The Samyang AF 135 and the Tamron F051 do nothing with it (static analysis, not observed).
- Our board never sends it (`bench_core.c`: not in the whitelist; `txn.c`, comment above the initial
  `m03`).

### 7.5 `0x05` — lens status (L→B, class 1)

**Layout.** The `0x05` is made of blocks, chosen by the `0x0A` masks (§ 7.10). The block sizes,
read in the Tamron F051 (static analysis, not observed), with the offsets they cover:

| Block | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Bytes | 8 | 1 | 8 | 3 | 10 | 28 | 2 | 6 | 1 | 2 | 8 | 6 | 6 | 6 | 1 | 6 | 3 | 3 |
| Offsets | 0-7 | 8 | 9-16 | 17-19 | 20-29 | 30-57 | 58-59 | 60-65 | 66 | 67-68 | 69-76 | 77-82 | 83-88 | 89-94 | 95 | 96-101 | 102-104 | 105-107 |

Blocks 1 to 15 make 97 bytes with the type: the length of every `0x05` observed after a
`0x0A FF 7F …`, on the Samyang AF 135 (`4_Firmware/traces/sy135-2026-09-20-dump05.txt` line 5), the
Sony FE 24-105 G (`traces/sony-2.txt`, every `0x05`) and the Sony 28-70 (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` line 30). All
18 make 109, the Samyang AF 135 before any `0x0A` (static analysis, not observed) and weiziqian's
117-byte frame variant (`docs/msg_0x05.md`).

**Fields.**

| Offset | Field | Proof |
|---|---|---|
| 0-1 | current aperture, code § 8.1 | `0x11B2` = f/1.8 (`4_Firmware/traces/sy135-2026-09-20-dump05.txt` line 5); `0x1405` = f/4 on the Sony FE 24-105 G (`traces/emount-bench-2026-09-29T18-12-13-937Z.txt` line 125); read by our board (`lens_rx.c`, `ap`) |
| 2-3 | the same value | same lines; weiziqian |
| 4 | frames left before the aperture reaches its target | weiziqian `docs/msg_0x05.md`, CERTAIN |
| 8 | bit 0 = aperture at rest | weiziqian, PROBABLE |
| 9-12, 13-16 | two duplicated pairs, 42/42 and 340/340 on AF lenses, 0 on manual ones | Samyang AF 135: 42 and 340 (`4_Firmware/traces/sy135-2026-09-20-dump05.txt` line 5); weiziqian |
| 17-18, 19 | an aperture code published by the lens when bit 0 of offset 19 is set | our board takes it as the aperture set by the lens ring (`lens_rx.c`, `ap_ring`; `ring.c`, `aperture()`); Samyang AF 135: set only when its ring drives the aperture (static analysis, not observed); weiziqian: 17-18 on the aperture grid, 19 a boolean, both zero on every Sony lens he measured |
| 20-21 | focus distance, Sony distance unit (§ 8.3); `0x0700` = infinity | Sony FE 24-105 G: 1792 at infinity, 295 at close focus (`traces/emount-bench-2026-09-29T18-12-13-937Z.txt` lines 125 and 583; `traces/sony_boutons.txt` line 53); Samyang AF 135 at infinity: `0x0700` (`4_Firmware/traces/sy135-2026-09-20-dump05.txt` line 5); not read by our board |
| 22 | flags: bit 7 = focus motor in service, bit 6 = lens in motion | Sony FE 24-105 G: `80` at rest, `C0` in the frame after a `0x1D` (`traces/emount-bench-2026-09-29T18-12-13-937Z.txt` lines 409 and 414); Tamron F051 (static analysis, not observed); weiziqian: bit 6 CERTAIN; not read by our board (§ 11) |
| 23 | coarse focus distance, 255 at infinity | Sony FE 24-105 G: 255 at infinity, 108 at close focus (same lines 125 and 583); Samyang AF 135: `FF` (`4_Firmware/traces/sy135-2026-09-20-dump05.txt` line 5); weiziqian: about −32 × dioptres |
| 24-25 | focal length, 0.1 mm, first value | Sony FE 24-105 G: 243 at 24 mm, 1006 at 105 mm (`traces/195_sony_boutons.txt` line 38; `traces/emount-bench-2026-09-29T18-12-13-937Z.txt` line 125); Samyang AF 135: 1350 (`4_Firmware/traces/sy135-2026-09-20-dump05.txt` line 5) |
| 26-27 | focal length, 0.1 mm, second value | Sony FE 24-105 G: 240 and 1050 (same lines), round values at both ends of its zoom; Samyang AF 135: 1350; read by our board as the nominal focal length (`lens_rx.c`, `focal_nom`) |
| 30-31 | a function of offsets 0-1 | weiziqian: `pl[0..1] − pl[30..31]` constant on a Voigtländer; disagreement below |
| 32-59 | optical correction rows and a second aperture encoding | weiziqian `docs/msg_0x05.md`, `docs/optical_data.md`; not read by our board |
| 60 | signed ring / movement byte | **contested**, below and § 11; read by our board (`lens_rx.c`, `move`) |
| 62 | switch on the barrel: bits 0-1 `01` or `03` | Samyang AF 135: `03` in its M2 configuration forced to manual focus, `01` in M1 forced to autofocus (`4_Firmware/traces/sy135-2026-09-20-dump05.txt` lines 5 and 7, below the comments of lines 4 and 6); Sony FE 24-105 G: `01` then `03` (`traces/sony_boutons.txt` lines 53 and 113); our board reads bit 1 as "manual focus" (`ring.c`, `SW_MF`) |
| 64 | barrel controls: bit 3 the focus button, bit 4 stabilisation | our board: bit 3 is the barrel button of the Samyang AF 135 (`4_Firmware/components/session/mark.c`, `BTN_BIT`), bit 4 the stabiliser (`session.c`, `bsk_session_status()`, field `oss`); Sony FE 24-105 G: `10`, `00`, `18` (`traces/sony_boutons.txt` lines 53, 293, 652) |
| 66 | other barrel controls | read raw and only logged by our board (`lens_rx.c`, `o66`); Sony FE 24-105 G: `00`, `30` (`traces/sony_boutons.txt` lines 53, 772) |
| 77-78 | row index of the optical table | weiziqian, CERTAIN; equal pair `15 15`, `16 16`, `17 17` on the Sony 28-70 (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` lines 90-110); `00 AE`, `00 6D`, `00 42` on the Samyang AF 135 (`4_Firmware/traces/sy135-2026-09-20-dump05.txt` lines 5, 7, 9) |
| 81-82 | effective focal length, 0.1 mm, corrected for focus breathing | Sony FE 24-105 G: 245 at infinity and 326 at close focus at 24 mm, 1008 and 1142 at 105 mm (`traces/195_sony_boutons.txt` line 38, `traces/sony_boutons.txt` line 53, `traces/emount-bench-2026-09-29T18-12-13-937Z.txt` lines 125 and 583); Samyang AF 135: 1350 at infinity (`4_Firmware/traces/sy135-2026-09-20-dump05.txt` line 5) |

Where the `0x05` lines of a bench capture are split in two (`+0/105`, `+64/105`), the line cited is
the first.

**Offset 60.** Three readings:

- Samyang AF 135: `+1` or `−1` when the focus ring moves, cleared at each `0x04` received (static
  analysis, not observed);
- Tamron F051: a signed count of ring edges, the direction of movement (static analysis, not
  observed);
- weiziqian (`docs/msg_0x05.md`): the focus ring direction, `01`/`FF`, held about ten frames,
  "moves the body commands do not set it"; the Google Doc: "focus moving flag", `00` still, `FF`
  further, `01` closer.
- The Sony FE 24-105 G leaves it at `00` while a `0x1D` moves it and offset 22 bit 6 is set
  (`traces/emount-bench-2026-09-29T18-12-13-937Z.txt` line 414).
- Our board uses it twice: as "the lens is moving" when following a move (`motion.c`, `mv_frame()`;
  `4_Firmware/components/session/still.c`, `still()`), and summed as ring pulses when the ring
  drives the aperture (`ring.c`, `aperture()`). Contested, § 11.

*Disagreements.* weiziqian says every Sony lens he measured sends offsets 20-21 constant at `0x0700`
and offsets 81-82 at zero; the Sony FE 24-105 G sends both varying with focus (lines above). He
reads offsets 24-27 as the wide and tele focal lengths of a zoom; on the Sony FE 24-105 G both
values follow the zoom, 243 and 240 at 24 mm, 1006 and 1050 at 105 mm (lines above). The
Google Doc gives offsets 30-31 as the aperture (`0x00` brightest to `0x4AB` darkest); weiziqian
shows they follow offsets 0-1, and our board reads the aperture at 0-1, which matches the
`0x08` range on both lenses (§ 7.8).

Our board reads a `0x05` only if it has at least 29 bytes (offsets 0-27), and only after the loop
is started (`lens_rx.c`, `MIN05`, `lens_rx_loop()`).

### 7.6 `0x06` — focus status (L→B, class 1)

**Layout.** Blocks chosen by the `0x0A` (§ 7.10); sizes read in the Tamron F051 (static analysis,
not observed):

| Block | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| Bytes | 2 | 11 | 9 | 4 | 6 | 7 | 45 | 4 |
| Offsets | 0-1 | 2-12 | 13-21 | 22-25 | 26-31 | 32-38 | 39-83 | 84-87 |

Blocks 1 to 6 (mask `3F`) make 40 bytes with the type: every `0x06` observed (`4_Firmware/traces/sy135-2026-09-20-dump05.txt`
line 11; `traces/sony-2.txt`; `https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` line 31). weiziqian (`docs/msg_0x06.md`) finds the same
39-byte core, an 88-byte core in one Yongnuo mode, and an appendix starting at offset 39.

**Fields.**

| Offset | Field | Proof |
|---|---|---|
| 0 | flags | below |
| 1 | direction: bit 1 = position increasing, bit 2 = decreasing, 0 = still | Sony FE 24-105 G: `04` towards a lower target, `02` towards a higher one (`traces/195_sony.txt` lines 37 and 51); Tamron F051 and Samyang AF 135 (static analysis, not observed); not read by our board |
| 2-3 | position, motor steps | **contested**, below and § 11; read by our board as the position (`lens_rx.c`, `pos`) |
| 7-8 | lower focus limit, motor steps | Samyang AF 135: 13873 (`4_Firmware/traces/sy135-2026-09-20-dump05.txt` line 11); Sony FE 24-105 G: 16111, 15265 or 15331, following the zoom (`traces/195_sony.txt` lines 37 and 133; `traces/emount-bench-2026-09-29T18-12-13-937Z.txt` line 127); read by our board |
| 9-10 | upper focus limit | 30738 (`4_Firmware/traces/sy135-2026-09-20-dump05.txt` line 11); 17490, 21068, 21086 (same lines); read by our board |
| 20-21 | encoder position now | Tamron F051 (static analysis, not observed); weiziqian, CERTAIN; Sony FE 24-105 G below |
| 25 | focus drive status | weiziqian, on a TECHART adapter only |
| 26-27 | subject distance | weiziqian: filled by Sony lenses, zero on Yongnuo lenses |
| 32-38 | seven signed samples of focus velocity | weiziqian (`docs/msg_0x06.md`, Yongnuo firmware; PROBABLE for Sony); Sony FE 24-105 G: non-zero while moving, zero at rest (`traces/195_sony.txt` lines 51, 58, 67) |
| 39 … | appended messages | § 7.6.1 |

**Offset 0.** Samyang AF 135: bits 0-2 = 2; bit 5 = at or above the upper soft limit, bit 6 = at or
below the lower one; bits 3 and 4 compare the position with two other thresholds (static analysis,
not observed). Tamron F051: bits 0-2 = 1 or 2 while the focus motor is in service, else 0; bits 3-4
= soft limits, bits 5-6 = hard stops (static analysis, not observed). Sony FE 24-105 G: `82` inside
its range, `92` at the lower limit, `8A` at the upper one (`traces/195_sony.txt` lines 37, 51, 58);
the limit bits follow offsets 2-3, not 20-21 (line 37: 2-3 at the limit, 20-21 not). weiziqian
(Yongnuo): bit 4 within 32 steps of the lower limit, bit 3 of the upper one. Our board does not read
it.

**Position: offsets 2-3 and 20-21.**

- At rest they are equal or one step apart: Samyang AF 135, 14623 and 14623 (`4_Firmware/traces/sy135-2026-09-20-dump05.txt` line 11); Sony
  FE 24-105 G, 15555 and 15554 (`traces/emount-bench-2026-09-29T18-12-13-937Z.txt` line 127).
- During a move they differ. Sony FE 24-105 G, 20 ms after `1D EF 3E 00 00` (target 16111): offsets
  2-3 = 16111, the target; offsets 20-21 = 16334 (`traces/195_sony.txt` lines 36-37). After
  `1D 00 00 00 00`: offsets 2-3 = 15331, the lower limit, at once; offsets 20-21 = 15554, then 15330
  a second later (`traces/emount-bench-2026-09-29T18-12-13-937Z.txt` lines 408, 416, 430).
- Tamron F051: 2-3 = the commanded position of two ticks before; 20-21 = the encoder sampled at the
  frame (static analysis, not observed). Samyang AF 135: 2-3 = its position (static analysis, not
  observed). weiziqian (Yongnuo firmware): 2-3 = the position one frame ahead, bounded by the
  target; 20-21 = the position now.
- Positions grow towards close focus on the Samyang AF 135 and the Tamron F051 (static analysis,
  not observed) and on the lenses of the Google Doc ("value increases as focus position moves
  closer").
- Our board reads 2-3 as the position, for its moves, its stillness test and its limits
  (`lens_rx.c`, `pos`; `motion.c`; `still.c`). Contested and contradicted by the Sony trace above,
  § 11.

**Limits.** Our board narrows each published limit by 5 steps when it reads it, and never targets
beyond (human decision: on the Sony a move stuck right after reaching the stop; `lens_rx.c`,
`LIMIT_MARGIN`). The Sony FE 24-105 G publishes limits that follow the zoom (lines above).

Our board reads a `0x06` only if it has at least 12 bytes (offsets 0-10), after the loop is started
(`lens_rx.c`, `MIN06`).

#### 7.6.1 Messages appended to the `0x06`

| Message | Bytes | Meaning |
|---|---|---|
| `1C v` | 2 | stop done (§ 7.4.1) |
| `1D v` | 2 | `0x1D` acknowledgement (§ 7.4.2) |
| `1F v` | 2 | `0x1F` acknowledgement |
| `22 vL vH`, `2E vL vH` | 3 | conversion results (§ 7.4.4) |
| `3C d 00 00` | 4 | `0x3C` reached its limit (§ 7.4.5) |

- **Place.** After the `0x06` in its frame: Sony FE 24-105 G (`traces/195_sony.txt` line 37;
  `traces/emount-bench-2026-09-29T18-12-13-937Z.txt` line 116); Samyang AF 135 and Tamron F051
  (static analysis, not observed); weiziqian (appendix at offset 39). **Before** the `0x06`: the Sony
  28-70 with a NEX-7 (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` lines 371, 451, 459: `1D 00 06 82 …`).
- **How many.** Samyang AF 135: three at most per frame; a fourth `1C` or `3C` waits for the next
  frame, a fourth among `1D`, `1F`, `22`, `2E` is lost; several ends of the same kind before one
  `0x06` give one acknowledgement (static analysis, not observed).
- **Our board** looks for `1D` and `1C` (any value) from offset 39 on, two bytes at a time
  (`lens_rx.c`, `LEN06`, the loop over `f->msg` in `lens_rx_frame()`). Contested, § 11: `22`, `2E` and
  `3C` acknowledgements are not two bytes long. It reads a frame by its first message, so a frame
  that starts with `1D 00` is not read as a `0x06` (§ 11).
- weiziqian (`docs/msg_0x06.md`): the appendix is one event `[code][parameter]`, and the codes are
  not shared between vendors (TECHART and Yongnuo disagree on `0x1F`).

### 7.7 `0x07` — identity (B→L 2 bytes, L→B 35 bytes, class 2)

Request `07 00`. Reply, offsets 0-33:

| Offset | Samyang AF 135 (`4_Firmware/traces/sy135-2026-09-20-full.txt` line 12) | Sony FE 24-105 G (`traces/sony.txt` line 13) | Meaning |
|---|---|---|---|
| 0 | `01` | `01` | weiziqian: `01` on natives, `02` on adapters, CERTAIN |
| 1-2 | `03 70` | `07 60` | bit 1 of offset 1 = can switch to 1.5 Mbaud (Tamron F051, § 5; static analysis, not observed); `03 70` is shared by twelve Samyang and the Tamron F051, so it identifies no maker (static analysis, not observed) |
| 3-4 | `01 00` | `01 00` | unknown |
| 5-6 | `01 05` | `03 01` | the firmware version on the Samyang AF 135 (1.05, also answered by its service channel, `4_Firmware/traces/sy135-2026-09-20-full.txt` line 43); not the version on the Tamron F051 (`01 03` for 3.01; static analysis, not observed) |
| 7 | `00` | `17` | unknown |
| 8 | `00` | `A0` | weiziqian: `A0` on every device he measured; the Samyang AF 135 sends `00` |
| 9-10 | `08 00` = 8 | `25 80` = `0x8025` | **LensType2**, the lens model, u16 (weiziqian: "Sony lens ID", CERTAIN); Tamron F051 `0xC134` (static analysis, not observed) |
| 15-18 | `60 92 86 5E` | `60 92 86 5E` | constant (weiziqian, CERTAIN) |

- Our board reads offsets 0-1 and 9-10 to build the key of its stored focus mark, and LensType2 to
  recognise a Samyang (`lens_rx.c`, `lens_rx_id()`; `init.c`, `init_recognize()`). It reads the
  `0x07` only as the reply of its init.
- *Disagreement.* weiziqian (`docs/msg_0x07.md`): native E-mount IDs are ≥ `0x8000`, and the range
  may be what decides. The Samyang AF 135 is a native (offset 0 = `01`) with ID 8 (`4_Firmware/traces/sy135-2026-09-20-full.txt` line 12).

### 7.8 `0x08` — the large descriptor (B→L 9 bytes, L→B 202 bytes, class 2)

**Request**: offset 0 is a set of flags; offsets 1-7 differ between bodies.

| Body | Request | Source |
|---|---|---|
| NEX-7 | `08 C0 21 00 00 33 09 62 02` | `https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-clocks-and-sync-lines.txt` line 51 |
| A6000, a9 II | offset 0 `C2`, `C6` | weiziqian (`docs/msg_0x08.md`) |
| our board | `08 06` + 7 × `00` to a recognised Samyang, `08` + 8 × `00` otherwise | `init.c`, `body08_flags()` |

- Samyang AF 135: bits 1 and 2 of offset 0 change what it publishes and how it applies its own
  switch configuration (bit 2 = "modern body"); Tamron F051: bit 7 chooses between two sets of
  values it publishes in the `0x06` (static analysis, not observed; `samyang.md`, `tamron.md`).
- weiziqian: bit 7 of offset 1 decides whether the lens fills offsets 9-10 of its `0x28` and `0x35`.

**Reply**, offsets 0-200. Offsets 0-1 = the widest aperture, offsets 2-3 = the narrowest, as
aperture codes (§ 8.1):

| Lens | Offsets 0-3 | Read as | Proof |
|---|---|---|---|
| Samyang AF 135 F1.8 | `BF 11 00 19` | f/1.8 to f/22.6 | `4_Firmware/traces/sy135-2026-09-20-full.txt` line 15 |
| Sony FE 24-105 G F4 | `05 14 00 19` | f/4.0 to f/22.6; its `0x05` offsets 0-1 read `0x1405` wide open | `traces/sony.txt` line 19; `traces/emount-bench-2026-09-29T18-12-13-937Z.txt` line 125 |

Our board uses this range to bound the aperture it asks for, and ignores a range outside the codes
`0x0E00`-`0x2000` (`lens_rx.c`, `ap_min`, `ap_max`, `ap_known()`; `4_Firmware/include/bsk_contract.h`,
`BSK_AP_CODE_MIN`, `BSK_AP_CODE_MAX`).

- *Disagreement.* weiziqian (`docs/msg_0x08.md`): offsets 0-1 "equal the lens's focus position",
  PROBABLE. His own values are apertures: `bd 13` = 5053 = f/3.65, the SELP1650 at its widest
  (his `docs/msg_0x03.md`), and `00 13` = f/2.83 on the LM-EA9 (his `docs/msg_0x05.md`). Ours rests
  on the two traces above.

### 7.9 `0x09` (B→L 5 bytes, L→B 12 bytes, class 2)

Request `09 00 00 00 00` (NEX-7, our board). Reply: all zero from the Samyang AF 135 (`4_Firmware/traces/sy135-2026-09-20-full.txt` line 23);
`09 00 … 00 FD FF` from the Sony 28-70 (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` line 18); `09 10 00 …` from the Sony FE
24-105 G (`traces/sony.txt` line 26); `10 00 … FD FF` from a SELP1650 and all zero from a SEL55210
(weiziqian `docs/msg_0x09.md`). Meaning
unknown. The Tamron F051 answers a constant (static analysis, not observed). Our board reads only
its presence, and requires it (§ 6.1).

### 7.10 `0x0A` — stream layout and switch (B→L 17 bytes, L→B 17 bytes, class 2)

**Request**, 16 bytes of masks:

| Offset | Content |
|---|---|
| 0-2 | the `0x05` blocks: bit *k* of the little-endian value set ⇒ block *k* + 1 is sent (§ 7.5) |
| 8 | the `0x06` blocks: bit *k* ⇒ block *k* + 1 (§ 7.6) |
| 3-7, 9-15 | zero in every request seen |

Sources: the Tamron F051 builds its `0x05` and `0x06` from these masks, and its own capability mask
is `FF FF 03 00 00 00 00 00 FF 00 …`, that is 18 `0x05` blocks and 8 `0x06` blocks; the Samyang AF
135 recomputes both lengths from the masks at every `0x0A` (static analysis, not observed). The
lengths observed after `FF 7F … 3F` are exactly blocks 1-15 and 1-6 (§ 7.5, § 7.6), on three lenses
of two makers.

| Request | Effect |
|---|---|
| `FF 7F 00 00 00 00 00 00 3F 00 …` | the stream used by the NEX-7, the A6000 and our board; 97-byte `0x05`, 40-byte `0x06` |
| all zero | no stream (NEX-7 shutdown, `https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` lines 462-465) |

**Reply**: the request, unchanged, from the Samyang AF 135 (`4_Firmware/traces/sy135-2026-09-20-full.txt` lines 38-39), the Sony FE 24-105 G
(`traces/sony.txt` lines 31-32) and the Sony 28-70 (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` lines 23-24 and 462-463). The
Tamron F051 answers the request ANDed with its capability mask, which for `FF 7F … 3F` is the
request again (static analysis, not observed).

**What it does to the lens.**

- It starts the stream when a mask is not zero and stops it when all are zero (§ 6.2; `https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt`
  lines 24-28 and 462-465).
- Samyang AF 135: while serving it the lens suspends the stream, **stops both motors and cancels a
  `0x1D` in progress without acknowledgement**, recomputes the layout, answers, then restarts the
  stream if a mask is not zero; at most 200 ms (static analysis, not observed).
- Tamron F051: the first `0x0A` passes the lens from its init phase to its stream phase, the next
  one back; the layout is set by the first one and never recomputed (static analysis, not observed;
  `tamron.md`).
- **A body must therefore never send a `0x0A` during a session it wants to keep.** Our board sends
  it at the end of the init, and once as the soft reset of a silent `0x01` (§ 6.1); never re-sent
  (`init.c`, `idempotent()`).
- *Disagreement.* weiziqian (`https://github.com/weiziqian/E-mount-protocol-RE/blob/main/docs/msg_0x0A.md`):
  "init echo", "plausibly a link test", PROBABLE; "what the request constant `ff 7f … 3f` encodes,
  if anything", open. Ours rests on the lengths observed for the masks used, on the stream that
  starts and stops with it (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` lines 24-28, 462-465), and on static analysis of both
  makers.

### 7.11 `0x0B` (B→L 3 bytes, L→B 3 bytes, class 2)

Request `0B 60 00`. Reply `0B 60 00` from the Samyang AF 135 (`4_Firmware/traces/sy135-2026-09-20-full.txt` line 21), the Sony FE 24-105 G
(`traces/sony.txt` line 24) and the Sony 28-70 (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` line 16); `60 20` from a SELP1650
(weiziqian `docs/msg_0x0B.md`). Both makers answer `0B <offset 0 of the request> 00` (static
analysis, not observed). Meaning of offset 1 unknown. Our board requires the reply (§ 6.1).

### 7.12 `0x0C` — speed switch

§ 5.

### 7.13 `0x0D` — frame rate (B→L 2 bytes, L→B 2 bytes, class 2)

- Request offset 0 = the VD rate: `00` = 60 Hz (NEX-7, `https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` line 19; our board, `init.c`,
  `INIT0D`). On the Samyang AF 135 other values select 50 or 48 Hz (static analysis, not observed;
  `samyang.md`).
- Reply: `0D 01` from the Samyang AF 135 (`4_Firmware/traces/sy135-2026-09-20-full.txt` line 25), the Sony FE 24-105 G (`traces/sony.txt` line
  28) and the Sony 28-70 (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` line 20); `0D 00` from the Tamron F051 (static analysis, not
  observed).
- *Disagreement.* weiziqian (`docs/msg_0x0D.md`): every device replies `01`, meaning unknown. The
  Tamron F051's `00` is static only.
- Our board requires the reply, not its value (§ 6.1).

### 7.14 `0x10` — homing (B→L 2 bytes, L→B 2 bytes, class 2, deferred reply)

- Request offset 0: **bit 2 = iris, bit 3 = focus** (both makers); bit 0 = a third module on the
  Tamron F051 (static analysis, not observed). Bodies send `10 1F` (NEX-7, `https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` line 21;
  A6000 to natives, weiziqian), `10 3F` (A6000 to a Viltrox adapter, weiziqian `docs/msg_0x10.md`).
  Our board sends `10 1F` at the init and `10 08`, focus only, when it homes the focus itself
  (`session.c`, `HOME10`).
- The lens searches its mechanical references and **answers only at the end**: Samyang AF 135 675 ms
  (`4_Firmware/traces/sy135-2026-09-20-full.txt` lines 27-35); Sony FE 24-105 G 214 ms (`traces/sony.txt` lines 29-30), and 460 ms for a
  `10 08` (`traces/emount-bench-2026-09-29T18-12-13-937Z.txt` lines 115-118); Sony 28-70 583 ms
  (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` lines 21-22).
- Reply `10 00`. The Tamron F051 answers `10 01` when one of its modules reports an error; the
  Samyang AF 135 answers `10 00` even after a failed focus homing, and not at all after a second
  iris failure or when neither bit 2 nor bit 3 is set (static analysis, not observed). Our board
  reads `10 01` as a failed homing and stops (`lens_rx.c`, `lens_rx_home_failed()`; `session.c`,
  `E_HOME_FAILED`).
- A `0x10` received during a homing restarts it, in both makers (static analysis, not observed).
- weiziqian reads the exchange's meaning as UNKNOWN (`docs/msg_0x10.md`).

### 7.15 `0x16` — shutdown (B→L 2 bytes, L→B 2 bytes, class 2)

- NEX-7: `16 01`, reply `16 00` 549 ms later, after `0A 00 … 00` (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` lines 462-465).
- Samyang AF 135: takes its iris to a rest position, waits for both motors to stop (no deadline),
  answers `16 00`, closes its serial port, waits for one of its input lines to stay low for 50 ms,
  then sleeps until the wake-up handshake (§ 2). Tamron F051: `16 00` at once. (Static analysis,
  not observed.) What else each maker does on
  `0x16` is in `samyang.md` and `tamron.md`.
- Our board never sends it (§ 6.3).
- *Disagreements* with weiziqian (`docs/msg_0x16.md`):
  - the reply "is the request frame sent straight back"; the Sony 28-70 answers `16 00` to `16 01`
    (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` lines 464-465), and both makers answer `16 00` (static analysis, not observed);
  - an a9 II removes power only once the lens has also "driven a logic line high", contact unknown.
    In the Samyang AF 135 no such line appears: after its reply it closes its serial port and waits
    for an input line to stay **low** (static analysis, not observed). Which contact that is, is
    not established.

### 7.16 `0x17` — spontaneous error (L→B, 3 bytes, class 3)

Sent by both makers on an internal failure: Samyang AF 135 on a second iris or focus homing
failure and on a stop not obtained within 1.2 s; Tamron F051 on an internal event (static analysis,
not observed). Never seen on a trace here. Our board ignores it.

### 7.17 `0x1B` — aperture order (B→L 6 bytes, L→B 11 bytes, class 2)

**It drives the iris**, not focus: Samyang AF 135 and Tamron F051 (static analysis, not observed);
weiziqian (`docs/msg_0x1B.md`): "the aperture command".

**Request**, read the same way by both makers (static analysis, not observed):

| Offset | Field |
|---|---|
| 0-1 | aperture code (§ 8.1), first |
| 2-3 | aperture code, second |
| 4 | bit 7: 0 = the target is offsets 0-1, 1 = it is offsets 2-3; bits 0-6 not read |

The same rule as `0x03` offsets 3-6 and bit 7 of offset 7 on the Tamron F051 (§ 7.3).

**Reply**, offsets 0-9 (static analysis, not observed):

| Offset | Samyang AF 135 | Tamron F051 |
|---|---|---|
| 0-1 | an aperture code: the target set before this order, plus 1 when that target equals the widest aperture of its `0x08` reply; the lens also writes it to its `0x05` offsets 0-1 | the current aperture, a copy of its `0x05` offsets 0-1 |
| 2-3 | the same code, also written to `0x05` offsets 2-3 | a copy of `0x05` offsets 2-3 |
| 4 | a copy of `0x05` offset 4 | a copy of `0x05` offset 4 |
| 5-6 | copies of `0x05` offsets 5 and 6 | offsets 0-1 minus the target of this order, signed |
| 7 | `FF` | low byte of `0x05` offsets 15-16 |
| 8 | `FF` | low byte of `0x05` offsets 13-14 |
| 9 | bits 1-2 set; bit 0 = `0x05` offset 8 bit 0 (aperture at rest, § 7.5) | bits 0-2 = `0x05` offset 8 bits 0-2; bits 3-7 = 0 |

- **When.** Samyang AF 135: at once, before the iris arrives. Tamron F051: only before the first
  `0x0A`; the reply is deferred until a condition on the iris is met, whose meaning is not
  established (static analysis, not observed).
- weiziqian (`docs/msg_0x1B.md`) shows a TECHART adapter's reply, `00 12 00 12 00 00 00 0A 0A 01`:
  the aperture at offsets 0-1, repeated at 2-3, as here.
- *Disagreement.* weiziqian lists request bytes up to offset 9 ("`pl[2..9]` purpose unknown"); both
  makers take 5 bytes after the type, and a longer `0x1B` would shift the paving of the frame (§ 4).
  Ours is static only.
- Our board never sends it: it drives the iris by offsets 3-6 of the `0x03` (§ 7.3).

### 7.18 `0x34` — move focus (B→L 24 bytes, L→B 24 bytes, class 2 reply)

A focus move whose reply is a frame of its own, not an acknowledgement in the `0x06`. Both lenses
list it in their `0x01` (§ 7.1).

**Request** (static analysis, not observed, both makers):

| Offset | Field |
|---|---|
| 0-1 | value, u16: a target, or a signed move in relative mode |
| 2 | not read |
| 3 | flags, the layout of the `0x1D` (§ 7.4.2): bits 0-1 unit (0 steps, 2 multiples of a lens unit, 3 Sony distance unit; 1 refused), bit 2 relative, bits 6-7 side of arrival. Bit 3 and bits 4-5 are not read |
| 4 | a bound, read differently by the two makers (below) |
| 5-22 | not read |

**Offset 4.**

- Samyang AF 135: an absolute target is brought within offset 4 × 384 / 17 steps of the current
  position; a relative move is not bounded (static analysis, not observed).
- Tamron F051: a time in milliseconds. The lens counts how many whole frames of 16 667 µs it
  covers and reports in its reply where focus will be after them; the move itself goes to the
  full target (static analysis, not observed).

**Reply**, offsets 0-22 (static analysis, not observed):

| Offset | Samyang AF 135 | Tamron F051 |
|---|---|---|
| 0 | `00` | a status of the unit conversion, meaning not established |
| 1-2 | the request's value, unchanged | the position focus will have reached after the time of offset 4, in the request's unit; less than one frame: the current position (absolute) or 0 (relative) |
| 3-22 | `00` | `00` |

- **When.** The reply leaves as soon as the move is launched, not when it ends. The Samyang AF 135
  launches it at its next frame processing (the next `0x04` while streaming); the Tamron F051 serves
  `0x34` only before the first `0x0A`. On the Samyang AF 135 a refused unit (1) gets the same reply, and nothing moves.
  On the Samyang AF 135 a `0x34` also changes the speed profile of later `0x1D` (static analysis, not
  observed; `samyang.md`).
- Our board never sends it.
- *Disagreement.* weiziqian (`docs/msg_0x34.md`): a 32-byte response from a TECHART adapter,
  `… 52 03 52 03 …` (850 twice), "the last exit-pupil-distance candidate", direction and class
  unknown. Ours is static only.

### 7.19 `0x3F` — name (B→L 2 bytes, L→B 66 bytes, class 2)

- Request `3F 00`; the Tamron F051 does not read offset 0 (static analysis, not observed).
- Reply: offset 0 = `00` (Samyang AF 135, static analysis, not observed; Sony FE 24-105 G,
  `traces/sony.txt` line 15) or not written (Tamron F051, static analysis, not observed); the name in
  ASCII from offset 1, padded with zeros: `FE 24-105mm F4 G OSS` (`traces/sony.txt` lines 15-16),
  `SAMYANG AF 135mm F1.8`, `E 24mm F2.8 F051` (static analysis, not observed). weiziqian: 65 payload
  bytes with a leading `00` (`docs/msg_0x3F.md`).
- Not every lens has it: the Tamron A046 answers a `0x02` (static analysis, not observed). The
  Tamron F051 answers it only before the first `0x0A`. The A6000 never asks for it, the a9 II asks
  between `0x0B` and `0x08` (weiziqian).
- Our board asks for it right after the `0x07`, reads at most 64 bytes, stops at the first zero,
  drops trailing spaces, and discards the whole name on a byte outside `0x20`-`0x7E` or a `"`
  (`lens_rx.c`, `name_3f()`). The name is also one way it recognises a Samyang (`init.c`,
  `init_recognize()`).

### 7.20 Messages a body must not send

| Type | Why |
|---|---|
| `0x14`, `0x15` | the firmware update path. Samyang: `0x14` writes an update flag in the lens's non-volatile memory and restarts it into its boot loader after 3 s. Tamron F051: `0x14` jumps to a second entry point and never returns; `0x15` takes a 1032-byte block. The lens leaves the protocol described here (static analysis, not observed). |
| `0x16` | puts the lens to sleep; on one Samyang model it also writes the lens's memory (static analysis, not observed; `samyang.md`) |
| `0x0C` | an irreversible change of speed (§ 5) |
| a type the lens has no size for | blocks a Samyang until its power is cut (§ 4) |
| `0x40` | the Samyang service channel, `samyang.md` |

Our board sends only `0x01 03 04 07 08 09 0A 0B 0D 10 1C 1D 3F`, and `0x40` to a recognised Samyang
with a list of sub-commands (`4_Firmware/include/bsk_bench_core.h`;
`4_Firmware/components/bench_core/bench_core.c`, `listed_size()`, `svc_allowed()`). The guard and
its reasons are in `bench_core.c`.

### 7.21 Other types

| Type | Sizes (B→L / L→B) | Known |
|---|---|---|
| `0x19` | 2 / 2 | reply `19 00`, at once on the Samyang AF 135, deferred on the Tamron F051 (static analysis, not observed); weiziqian: unknown |
| `0x1E` | 5 / — | Samyang AF 135: answers `1E 00` in class 1 (static analysis, not observed) |
| `0x26` | 2 / 5 | reply `26 00 00 00 00` on the Samyang AF 135 (static analysis, not observed) |
| `0x28` | 2 / 36 | weiziqian: a carrier of the optical rows and of the focal length (`docs/msg_0x28.md`) |
| `0x35` | 2 / 40 | weiziqian: a carrier of the optical rows (`docs/msg_0x35.md`) |
| `0x3A`, `0x3B` | 24, 8 / 24, 492 | a block read of a table, class 3 replies (static analysis, not observed) |
| `0x3D` | 24 / 64 | the a9 II asks for it at init (weiziqian `docs/msg_0x3D.md`); content unknown |
| `0x3E` | 9 / 12 | unknown |

---

## 8. Encodings

### 8.1 Aperture code

`code = 256 × (Av + 16)`, with `Av = 2 × log2(N)` for f/N; `0x1000` = f/1.0, each third of a stop
adds about 85. Examples: `0x11B2` = f/1.8, `0x11BF` ≈ f/1.8, `0x1405` = f/4.0, `0x1900` = f/22.6.
Sources: our code (`4_Firmware/components/session/lens_rx.h`, comment of `ap`;
`4_Firmware/components/host/host.c`, the f/ conversion; `ring.c`, `THIRDS`); weiziqian
(`docs/aperture_value.md`): `256·AV + 4096`, the same formula. Used by `0x03` offsets 3-6, `0x05`
offsets 0-3 and 17-18, `0x08` offsets 0-3, `0x1B`. Our board converts codes `0x0E00` (f/0.5) to
`0x2000` (f/256) and treats any other code as no aperture (`bsk_contract.h`).

### 8.2 Focus position

Motor steps of the lens, in its own range: `0x06` offsets 2-3, 7-10 and 20-21, and `0x1D` in unit 0.
There is no common origin or scale: 13873-30738 on the Samyang AF 135, 15265-21068 on the Sony FE
24-105 G at one zoom position (§ 7.6). Our board assumes no range: it uses what the `0x06`
publishes (`session.c`, header comment; `lens_rx.c`).

### 8.3 Sony distance unit

Used by `0x05` offsets 20-21, by `0x1D` in unit 3, and by the `0x22`/`0x2E` conversions.

- `0x0700` = infinity, decreasing towards close focus (Tamron F051, Samyang AF 135; static analysis,
  not observed).
- weiziqian (`docs/msg_0x05.md`, `docs/autofocus.md`): `384 + 64 × log2(D)`, D in metres: 1 m = 384,
  each doubling adds 64, CERTAIN on a Voigtländer whose distance marks it reproduces. With this
  formula the 295 of the Sony FE 24-105 G at close focus (§ 7.5) is 0.38 m.
- `0x05` offset 23 is a coarser scale of the same distance, 255 at infinity (§ 7.5).

### 8.4 Focal length

Tenths of a millimetre: `0x05` offsets 24-25, 26-27 and 81-82 (§ 7.5).

---

## 9. Where we disagree with public sources

Every disagreement on a common message, with where it is argued. "W" is weiziqian,
`https://github.com/weiziqian/E-mount-protocol-RE`; "G" is the Google Doc.

| Subject | Public source | Ours | Our basis | § |
|---|---|---|---|---|
| `0x0A` | W: an exact echo, plausibly a link test | lays out and switches the stream; stops Samyang motors and cancels a `0x1D`; toggles the Tamron phase; never re-send it | traces of lengths and of the stream; code; static | 7.10 |
| `0x1D` flags | W: bits 0-2 mode, bit 3 ignored, bits 4-7 unknown | bits 0-1 unit, 2 relative, 3 cycle wait, 4-5 oscillation, 6-7 side of arrival | static | 7.4.2 |
| `0x1D` as a type | W: probably a frame length, not an ID | a message type, dispatched as such | traces; static | 7.4.2 |
| replaced `0x1D` | W: no completion event | Samyang acknowledges it `1D 00` | static; code | 7.4.2 |
| `0x08` offsets 0-1 | W: the focus position | the widest aperture; 2-3 the narrowest | traces | 7.8 |
| `0x06` offsets 2-3 | W: position one frame ahead | Tamron: commanded position; Sony trace: equals the target at once; our code reads it as the position (contested) | trace; static | 7.6, 11 |
| `0x05` offset 60 | W: ring direction, not set by body moves; G: focus moving flag | Samyang: ring flag; Tamron: signed ring count; our code reads it as movement (contested) | trace; static | 7.5, 11 |
| `0x05` 20-21 and 81-82 on Sony | W: constant `0x0700` and zero on every Sony lens | vary with focus on the Sony FE 24-105 G | trace | 7.5 |
| `0x05` 24-27 | W: wide and tele focal lengths of a zoom | both follow the zoom on the Sony FE 24-105 G | trace | 7.5 |
| `0x05` 30-31 | G: the aperture | follows 0-1 (W); the aperture is at 0-1 | traces; code | 7.5 |
| class 3 | W: classes 1 and 2 only; G: "??" | class 3 exists for `0x02`, `0x17`, `0x3A`, `0x3B` | static | 3.3 |
| sequence wrap | W: at 256 | lens: `0xEF` → 0 (as G); NEX-7 body: `0xEF` → `0x80` … `0x8F` → 0 | traces | 3.4 |
| `0x2F` | W: a row index the lens acts on (Yongnuo) | no effect on Samyang and Tamron | static | 7.4.6 |
| lens ID | W: natives ≥ `0x8000`, the range may decide | the native Samyang AF 135 has ID 8 | trace | 7.7 |
| `0x0D` reply | W: `01` everywhere | `0D 00` on the Tamron F051 | static | 7.13 |
| `0x34` | W: a 32-byte response, exit pupil candidate | a 24-byte focus move | static | 7.18 |
| `0x1B` request | W: payload bytes up to offset 9 | 5 bytes after the type: two aperture codes and a selector | static | 7.17 |
| `0x1F` fields | W: offset 0 bit 0 = centred form; offsets 1-2, 8-10 unknown | offset 0 bits 0-1 = window form; roles for offsets 1, 2, 8, 9 | static | 7.4.3 |
| `0x22` | G: absolute motor movement | a conversion, nothing moves | static | 7.4.4 |
| `0x0C` | W: meaning unknown | reply bit 0 = speed accepted | static | 5 |
| both CS high | W: only at the speed change | also during the power-up handshake | code; traces | 1.4, 2 |
| CS delimits a frame | W README: one frame per CS-high period | LENS_CS does not delimit; parse by length | code | 1.4 |
| `0x16` reply and line | W: the reply is an echo; the lens must drive a line high | `16 00` to `16 01`; no such line in the Samyang AF 135 | trace; static | 7.15 |
| `0x4C`, `0x5A` | W: real IDs beyond the bitmap | Tamron table artefacts | static | `tamron.md` |

weiziqian's pages contradict each other in places (the meaning of the `0x1B` in his `docs/msg_0x34.md`
against his `docs/msg_0x1B.md`, for instance); this document follows the page dedicated to each
message.

---

## 10. Prior art

| Source | What it published first | Method |
|---|---|---|
| DPReview thread, Entropy512, 2015 (`https://www.dpreview.com/forums/thread/3872069`) | the first probe, dumps and decoder | captures |
| Google Doc "Sony E Mount Lens Protocol", bostwickenator and Entropy512 (URL in § 1.1) | contacts, levels, UART, 1.5 Mbaud, frame and checksum, classes 1 and 2 ("?? 0x03"), sequence reset after `0xEF`, message list, init and loop of a Voigtländer 15 mm, first fields of `0x05` and `0x06` | captures |
| sigrok-dumps (`https://github.com/EliasOenal/sigrok-dumps/tree/master/lens_mounts/sony_emount`) | logic captures: A6000 with SEL55210, SELP1650, a Viltrox adapter | captures |
| Entropy512's tools (`https://github.com/Entropy512/emount_tools`, decoder on branch `emount` of `https://github.com/Entropy512/libsigrokdecode`) | a sigrok decoder; the focus messages `1C 1D 1F 22 2F 3C` and the `0x05`/`0x06` sub-groups, the latter from Leegong's firmware work | captures, and Leegong's tables |
| Dyxum forum, "E-mount electronic protocol reverse engineering" (topic 119522), 2016-2021 | Leegong: message sizes up to `0x2C` read in an adapter firmware; `0x1C` = stop, `0x1D` = move | firmware |
| LexOptical (`https://github.com/LexOptical/E-Mount`, `https://github.com/LexOptical/E-Mount-Traffic-Samples`) | a lens emulator that satisfies a NEX-7; NEX-7 captures with four lenses, with and without the CS and VD lines | captures |
| weiziqian (`https://github.com/weiziqian/E-mount-protocol-RE`, MIT) | the most complete public description: confidence per field, `0x01` bitmap rule, A6000 and a9 II init orders, `0x03`/`0x04`/`0x05`/`0x06` field maps, focus records, `0x06` appendix and velocity samples, aperture and distance encodings, the `0x16` power-off line | captures; Yongnuo and TECHART firmware |

What this project adds, from its own captures, its code and its analyses: class 3; the `0x0A` as
layout and switch; the `0x0C` reply bit; the `0x0D` rate; the `0x10` bits; the flags of the `0x1D`;
the place and values of the acknowledgements; the Sony FE 24-105 G traces of § 7.5 and § 7.6; and
what each maker does with each common message (§ 7, `samyang.md`, `tamron.md`). Where this document
uses a fact first published by one of these sources, it names it in place.

---

## 11. Our code: contested points, and where a trace contradicts it

None of these is a demonstrated failure: the Samyang AF 135 works with this code on the bench.
They are reported, not corrected.

| # | What our code does | Why it is contested | Evidence |
|---|---|---|---|
| 1 | takes `0x05` offset 60 as "the lens is moving", to follow a move and to judge stillness (`motion.c`, `mv_frame()`; `still.c`, `still()`; `lens_rx.c`, `move`) | on the Samyang AF 135 it is a ring flag (static), on the Sony a ring direction not set by commanded moves (weiziqian); the indicator both weiziqian and the Tamron F051 agree on is offset 22 bit 6. A ring gesture during a move could make the board believe the move started or ended | **trace**: the Sony FE 24-105 G leaves offset 60 at `00` during a `0x1D` while offset 22 bit 6 is set (`traces/emount-bench-2026-09-29T18-12-13-937Z.txt` line 414) |
| 2 | reads the position at `0x06` offsets 2-3 and tests arrival as position = target (`lens_rx.c`, `pos`; `motion.c`) | the encoder is at 20-21; 2-3 is a forecast (weiziqian) or a commanded position (Tamron, static). Both coincide at rest | **trace contradicts the reading**: on the Sony FE 24-105 G, 2-3 equals the target 20 ms after the order while 20-21 is 223 steps away (`traces/195_sony.txt` line 37), and jumps to the clamped limit at once (`traces/emount-bench-2026-09-29T18-12-13-937Z.txt` line 416). What this does to our arrival time on that lens is not measured |
| 3 | scans the `0x06` appendix two bytes at a time (`lens_rx.c`, loop in `lens_rx_frame()`) | `22`/`2E` results are 3 bytes, `3C` 4 (static; weiziqian: 4 bytes for `3C`). Harmless while the whitelist sends none of `22`, `2E`, `3C` (`bench_core.c`, `listed_size()`) | static; code |
| 4 | sends `0x04` offset 7 = `00` (`txn.c`, initial `m04`) | an A6000 sends `1F` to lenses with an E-mount ID (weiziqian `docs/msg_0x04.md`); effect on the Sony unknown | the NEX-7 sends `00` to the native Sony 28-70 (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` line 29) |
| 5 | numbers its init frames with its running counter, not 0 (`txn.c`, `seq_advance()`) | a real body sends 0 (weiziqian; NEX-7, `https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` lines 15-23) | the Samyang AF 135 and the Sony FE 24-105 G answer them (`traces/sony.txt` lines 10-32); the Samyang does not check it (static) |
| 6 | reads a received frame by its first message only (`lens_rx.c`, `lens_rx_frame()`) and looks for acknowledgements after the `0x06` | **trace**: with a NEX-7, the Sony 28-70 puts `1D 00` **before** its `0x06` (`https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/nex7-sony-28-70mm-at-70mm.txt` lines 371, 451, 459); such a frame would not be read as a `0x06`, nor its acknowledgement found. The Sony FE 24-105 G puts it after (`traces/195_sony.txt` line 37) | public trace |
| 7 | takes a `1D` acknowledgement as the lens declaring its move over: the move goes to SETTLING, then ARRIVED once the position of offsets 2-3 is still (`motion.c`, `mv_frame()`, `mv_settling()`) | **trace**: the Sony FE 24-105 G acknowledges in the first `0x06` after the order, 20 ms later, with offsets 20-21 still 223 steps from the target; the board logs `moving` and `settling` on that frame and `arrived` 316 ms later (`traces/195_sony.txt` lines 37-40). Where the encoder was at `arrived` is not in the capture | bench trace |
| 8 | says in a comment that the Sony's acknowledgement of `0x1C` "is not established" (`motion.c`, comment of `stopped()`) | **trace**: the Sony FE 24-105 G answers `1C 00` (`traces/emount-bench-2026-09-29T18-12-13-937Z.txt` line 116). The code accepts any `1C`, so its behaviour is right | bench trace |
| 9 | wraps its sequence from `0xEF` to `0x00` (`txn.c`, `seq_advance()`) | the NEX-7 body goes `0xEF` → `0x80` … `0x8F` → `0x00` (§ 3.4); lenses wrap like our board | public trace |

---

## 12. What stays unknown

- What decides `LENS_POWER`'s voltage (§ 1.1).
- The meaning of `0x01` bits beyond offset 7 (§ 7.1), of the `0x09` reply, of `0x0B` offset 1, of
  `0x07` offsets 3-4 and 7.
- The meaning of `0x03` offsets 0-1, 15, 16 and of `0x04` offsets 6 and 10.
- What the NEX-7's sequence range `0x80`-`0x8F` means (§ 3.4).
- What the Tamron F051 does with each field of the `0x1F` (it cuts them like the Samyang AF 135),
  the units of the `0x1F` speed and run lengths, and what the Tamron's `0x34` reply offset 0 means.
- Whether a Sony lens's `1D 00` ever means "arrived" (§ 7.4.2), and when the Sony FE 24-105 G
  considers a move done.
- Class 3 on a real wire: no trace shows it (§ 3.3).
- The 1.5 Mbaud switch on a trace with CS lines: none here (§ 5).
- The blocks of the `0x05` beyond block 15 and of the `0x06` beyond block 6 on any lens observed
  (§ 7.5, § 7.6).
- Which barrel control drives `0x05` offsets 64 and 66 on the Sony FE 24-105 G: the capture records
  the values, not the gestures (§ 7.5).

---

## 13. Brand files

- `samyang.md`: what the Samyang lenses do with the common messages beyond this document (stream
  cadenced by the pair, homing in stream, `0x0A` stopping the motors, `0x08` flags, frame rates of
  `0x0D`), the service channel `0x40`, the writes to lens memory and the exceptions our board allows.
- `tamron.md`: the two-phase session of the Tamron F051, the `0x02` refusals, the deferred replies,
  `0x4C`/`0x5A`, the `0x1D` units it refuses.
- `traces.md`: the captures cited as `traces/…`, published in `7_Docs/E-Mount/traces/`.
- `provenance.md`: where each fact comes from, and what was not published.
