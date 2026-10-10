# The bench captures — how to read them

**Date** : 2026-10-08
**Dernière révision** : 2026-10-09
**Statut** : actif — fourth interoperability document of `7_Docs/E-Mount/` (ticket #544); the eleven captures it describes are published, byte for byte, in `7_Docs/E-Mount/traces/`
**Référencé par** : `7_Docs/E-Mount/README.md`, `protocol.md` (§ 0, § 13), `samyang.md` (§ 0), `provenance.md`
**Dérivé de** : the captures of `7_Docs/E-Mount/traces/` and `4_Firmware/traces/`; the history of this repository (the commits named below); `7_Docs/E-Mount/protocol.md`, `samyang.md`

## 0. What this document is

A reading guide for the captures that `protocol.md`, `samyang.md` and `tamron.md` cite as
`` `traces/<file>` line <n> ``. With it, a capture can be read without any other help: what
produced each line, what the fields mean, which board firmware was running, and which sections
of the three documents rely on the file.

**What a capture is.** Each file of `traces/` is the log of the project's bench page, a web page
that talks to the board over USB. The board plays the camera body; a lens is on its mount. The
page writes one line per event: the commands it sends, the board's replies, the board's journal
lines, and its own messages. A capture is that log, exported by the page or copied out of it.
The bytes shown in `* tx` and `* rx` lines are frames our board sent and received on the
lens's wire: **our observations**, not bytes taken out of a lens firmware (`provenance.md`).

The eleven files are published as they were exported: same names, same bytes, nothing added.
The line numbers the documents cite depend on it.

**Where the code was.** The page was `app/emount-bench.html` until commit 52fc056, and has been
`5_App/emount-bench.html` since. The board firmware exists in two lines in this repository's
history:

- **v1**: `firmware/` until commit f7780b9 renamed it `firmware_old/`; it has since been archived
  out of the repository. Its last code change is commit c96d3e2.
- **firmware2**: created as `firmware2/` (its journal at commit 7552185, its USB command layer at
  commit d01ebf0), renamed `firmware/` by commit 53fbed1, then `4_Firmware/` by commit 52fc056.
  It is the firmware of this repository today.

**How a capture is matched to a firmware.** No capture names the build that produced it. Each
one is matched against the history: a line form seen in the capture rules out every commit
whose code cannot write it, and a behaviour seen in the capture (a reply, a byte the board
sends) rules out the commits that behave otherwise. What remains is a **window**: "from A,
before B" is every commit that descends from A (A included) and not from B. Every line form of
the capture is then proved at one commit chosen inside that window (§ 2, § 3). The window is the
most the capture establishes: when it holds several commits, the capture cannot tell them apart.
The commit dates of a window bound the capture date only if the board was flashed from committed
code; the captures do not record that, so the dates of § 4 stay "not recorded" unless the file
itself carries one.

**Clocks.** Two clocks appear on a line:

- the line prefix `HH:MM:SS.mmm` is the clock of the computer running the page, local time,
  taken when the page wrote the line (§ 2.1);
- the number after a journal keyword (`* tx 5531 …`, `* session 5403 …`) is the board's clock,
  in milliseconds since the board started (`firmware2/components/host/host_usb.c` at commit
  c9b674e, `clock_us()`, which reads `esp_timer_get_time()`; v1: `firmware/components/link/link.c`
  at commit 7631eb6, `t_us / 1000`).

Durations between frames are read on the board clock: the page clock records when the page
wrote the line, not when the frame passed on the wire.

## 1. The captures at a glance

| File | Lines | Board firmware | Window | Lens | Date |
|---|---|---|---|---|---|
| `emount-bench-2026-09-28T17-45-01-034Z.txt` | 950 | v1 | from e099498 (v1 code unchanged after c96d3e2) | Sony FE 24-105mm F4 G OSS | 2026-09-28 |
| `firmware1_ring.txt` | 160 | v1 | from 75461fa, before 9f90b7d | Samyang AF 135, firmware 1.05 | not recorded |
| `emount-bench-2026-09-29T18-12-13-937Z.txt` | 1548 | firmware2 | from c9b674e, before 5549081 | Sony FE 24-105mm F4 G OSS | 2026-09-29 |
| `sony_boutons.txt` | 832 | firmware2 | from d01ebf0, before 5f13a3e | Sony FE 24-105mm F4 G OSS | not recorded |
| `195_samyang.txt` | 1293 | firmware2 | from d01ebf0, before 5f13a3e | not named; Samyang AF 135 per `samyang.md` | not recorded |
| `195_sony_boutons.txt` | 307 | firmware2 | from c9b674e, before 5f13a3e | Sony FE 24-105mm F4 G OSS | not recorded |
| `195_sony.txt` | 458 | firmware2 | from 5549081, before 2baff88 | Sony FE 24-105mm F4 G OSS | not recorded |
| `sony-2.txt` | 253 | firmware2 | from 5f13a3e, before c9ae37f | Sony FE 24-105mm F4 G OSS | not recorded |
| `firmware2_ring.txt` | 87 | firmware2 | from 75a30de, before a252911 | Samyang AF 135 F1.8 | not recorded |
| `sony.txt` | 1420 | firmware2 | from 4cf8726, before 5b333ee | Sony FE 24-105mm F4 G OSS | not recorded |
| `samyang.txt` | 89 | firmware, version 1.0 | from 99809eb | Samyang AF 135 F1.8 | not recorded |

The window of `samyang.txt` is open: its forms are all still written at commit 111fda2, the state
of the repository this document was written from (§ 4.11).

The **proof commit** of each capture, at which § 2 and § 3 prove its line forms: 7631eb6 for
both v1 captures; c9b674e for `emount-bench-2026-09-29T18-12-13-937Z.txt`, `sony_boutons.txt`,
`195_samyang.txt` and `195_sony_boutons.txt`; 5549081 for `195_sony.txt`; 75a30de for
`sony-2.txt` and `firmware2_ring.txt`; 4cf8726 for `sony.txt`; 99809eb for `samyang.txt`. Each
lies inside its capture's window.

## 2. Line forms

### 2.1 Lines written by the page (every capture)

Every line starts with the page clock, `HH:MM:SS.mmm`, then a space (`app/emount-bench.html` at
commit c9b674e, `addLine()`: `toLocaleTimeString('fr-FR')` and the milliseconds). The page keeps
every line in its export (the last 20 000), repeated lines included; on screen it folds repeats
into a counter.

| Form | Meaning | Proof |
|---|---|---|
| `> <command>` | a command the page sent to the board (`PROTOCOL.md` of the time). The page's own polling commands are not written unless its poll box is ticked: the captures show the operator's commands only | `app/emount-bench.html` at commit c9b674e, `send()` |
| `ok…`, `er …` | the board's reply to the command above, as received | `app/emount-bench.html` at commit c9b674e, `send()`, its `resolve`; the reply itself is the board's (§ 3) |
| `* …` | a line of the board's journal, passed through untouched (§ 3) | `app/emount-bench.html` at commit c9b674e, `onLine()` |
| `(réponse inattendue à « <cmd> », ignorée) <line>` | the board sent `<line>` while the page waited for the reply to `<cmd>`, and `<line>` did not have the expected form; the page ignored it | `app/emount-bench.html` at commit c9b674e, `onLine()` and `expectFor()` |
| `[app] carte : protocole <v>` | the board's reply to `v`, read as a protocol version (pages before commit 5b333ee); both firmware lines answer `1.3` until then | `app/emount-bench.html` at commit c9b674e, `connect()`; `firmware2/components/host/host.c` at commit c9b674e; `firmware/components/pinefeat/host_pinefeat.c` at commit 7631eb6 |
| `[app] carte : firmware <v>` | the board's reply to `v`, read as the board firmware version (pages from commit 5b333ee) | `app/emount-bench.html` at commit 99809eb, `connect()` |
| `[app] prêt : <id> — focus par « <module> »` | the lens identity and the focus module, both from the board's `t` reply (pages before commit 5b333ee, which always write the module). firmware2 reports no module: `module=-` (`firmware2/components/host/host.c` at commit c9b674e) | `app/emount-bench.html` at commit c9b674e, `refreshAll()` |
| `[app] prêt : <name>` | the lens name, from the board's `i` reply (pages from commit 5b333ee). These pages read the module from the `n` reply and add ` — focus par « <module> »` only when `n` names one; firmware 1.0 answers `n` with `er nocap`, so the line ends at the name (commit b49070f later drops the module altogether) | `app/emount-bench.html` at commit 99809eb, `refreshAll()`; `firmware/components/host/host.c` at commit 99809eb, the `n` case |
| `[app] objectif disparu` | the board answered `nc` (no lens) to a position poll | `app/emount-bench.html` at commit c9b674e, `pollFast()` |
| `[app] bornes inconnues à cette focale (<f>) : Calibrer pour les mesurer` | the board's `t` reply stopped giving focus limits; `(?)` when it gives no focal length either | `app/emount-bench.html` at commit c9b674e, `pollSlow()` |
| `[app] ouverture refusée : <reply>` | an aperture command (`a<f>`) got a reply other than `ok` | `app/emount-bench.html` at commit c9b674e, `irisGoto()` |
| `[app] déplacement refusé : <reply>` | a focus command (`f<n>`) got a reply other than `ok`; `null` means no reply at all (the link was gone) | `app/emount-bench.html` at commit c9b674e, `focusGoto()` and `send()` |
| `[app] dernière opération : <op>:<result>` | the `last_op` field of the board's `t` reply changed | `app/emount-bench.html` at commit c9b674e, `pollSlow()` |
| `[app] lecture : <message>` | reading the USB serial port failed; `<message>` is the browser's error text, which no file of this repository writes (observed: `The device has been lost.`, `sony.txt` line 1419) | `app/emount-bench.html` at commit c9b674e, `readLoop()` |
| `[app] liaison perdue` | the page lost the link and disconnects | `app/emount-bench.html` at commit c9b674e, `readLoop()` |

The **export name** is `emount-bench-<date and time>.txt`, the date and time being the export
instant in UTC, its `:` and `.` replaced by `-` (`app/emount-bench.html` at commit c9b674e, the
`btnExport` handler, `toISOString()`). The two captures named that way therefore carry their
export date.

Every page from the first commit of the repository (75461fa) to the commit before 5b333ee writes
these forms with the same code; the v1 captures are proved on the page at commit 7631eb6
(`app/emount-bench.html` at commit 7631eb6), the firmware2 captures on the page at their proof
commit, and `samyang.txt` on the page at commit 99809eb, which keeps every form of the table
except the two that change from commit 5b333ee (`carte : firmware`, `prêt : <name>`).

### 2.2 Board lines common to both firmware lines

| Form | Meaning | Proof (v1; firmware2) |
|---|---|---|
| `* tx <t> F0 …` | a frame the board sent to the lens, in hex: `F0`, length (2 bytes, little-endian, whole frame), class, sequence number, then the messages, checksum (2 bytes), `55` (`protocol.md` § 3.1). firmware2 writes it whole, cut like a received frame beyond 64 bytes; v1 writes its first 64 bytes, then ` ...` | `firmware/components/link/link.c` at commit 7631eb6; `firmware2/components/journal/journal.c` at commit c9b674e, `frame()` |
| `* rx <t> F0 …` | a frame received from the lens, same layout | same files |
| `* rx <t> +<off>/<len>#<id> <hex>` | a frame longer than 64 bytes, cut into lines of 64 bytes: `<off>` is the frame byte where the line starts, `<len>` the frame length, `<id>` a frame counter shared by the lines of one frame. Join the lines of the same `#<id>` in `<off>` order to get the frame | `firmware/components/link/link.c` at commit 7631eb6 (`<id>`: frames received); `firmware2/components/journal/journal.c` at commit c9b674e, `hex_lines()` (`<id>`: frames the journal handled) |
| `ok log=<0/1> all=<0/1>` | reply to `LOG ON`, `LOG ALL`, `LOG OFF` | `firmware/components/diag/diag.c` at commit 7631eb6, `cmd_log()`; `firmware2/components/host/host.c` at commit c9b674e |
| `ok` | a command accepted | `firmware/components/pinefeat/host_pinefeat.c` at commit 7631eb6; `firmware2/components/host/host.c` at commit c9b674e |

The firmware2 rows hold at every firmware2 proof commit, as stated at the head of § 3.2.

**`LOG ON` and `LOG ALL`.** Under `LOG ON`, the periodic frames are not written; under
`LOG ALL`, they are. v1 counts as periodic the received `0x02`, `0x05` and `0x06`, and writes the
`0x03`/`0x04` pair of the loop under `LOG ALL` only (`firmware/components/link/link.c` at commit
7631eb6). firmware2 counts the received `0x02`, `0x05`, a `0x06` of 40 bytes or less, the sent
`0x03` and a `0x04` of 14 bytes or less: a `0x04` carrying a focus order and a `0x06` carrying an
acknowledgement are written even under `LOG ON` (`firmware2/components/journal/journal.c` at
commit c9b674e, `periodic_rx()`, `periodic_tx()`).

**The line ceiling.** v1 writes at most 30 received-frame lines per one-second window
(`firmware/components/link/link.c` at commit 7631eb6, `LOG_RX_PER_S`); firmware2 at most 30
lines of any kind (`firmware2/components/journal/journal.c` at commit c9b674e, `PER_S`, `admit()`).
Each summarises what it left out (§ 3.1, § 3.2): a capture under `LOG ALL` is a sample, not the
whole stream. Gaps show as jumps in the frame counter `#<id>` and in the sequence numbers.

### 2.3 Lines no file of this repository defines

- `ESP-ROM:esp32s3-20210327` (`sony-2.txt` line 1): a line the board sent while the page waited
  for the reply to `v`. No file of this repository writes it; the page shows it as an unexpected
  reply (§ 2.1).
- The text after `[app] lecture :` (§ 2.1).
- What the lens puts in its frames: the documents describe it (`protocol.md` § 7); the captures
  only record it.

## 3. Board lines, by firmware line

### 3.1 v1 (proof commit 7631eb6)

| Form | Meaning | Proof |
|---|---|---|
| `* rxflood suppressed=<n> in_last_window rx_frames=<total> (DUMP <type> pour voir une trame)` | `<n>` received frames left out by the ceiling in the last window; `<total>`: frames received since start. In v1 only received frames count against the ceiling | `firmware/components/link/link.c` at commit 7631eb6, `link_log_budget()` |
| `* boot lens=0 why=<cmd>` | a lens restart requested (`b`) | `firmware/components/control/control.c` at commit 7631eb6 |
| `* boot step=power logic=<0/1> motor=<0/1>` | the two lens rails | `firmware/components/control/lensctl.c` at commit 7631eb6 |
| `* boot step=init done=<0/1> loop=1 rx05=<n>` | the initialisation finished, the loop runs; `<n>`: `0x05` received | same file |
| `* boot step=home result=deja_fait (0x10 de l'init)`, `* boot step=calibrate result=stored source=<src>` | no second homing (the init's `0x10` did it); limits taken from storage | same file |
| `* boot driver=<name> caps=<list>`, `* boot caps=<list> driver=<name>`, `* boot identity="<id>" version=<v>` | the driver chosen for the lens, its capabilities, the identity it built | same file |
| `* boot result=ok attempts=<n>` | the boot succeeded, after `<n>` attempts | `firmware/components/control/control.c` at commit 7631eb6 |
| `* lens present=<0/1>` | lens absent / present | `firmware/components/control/lensctl.c` at commit 7631eb6 |
| `* std step=handshake result=<r> …` | the power-up handshake (`protocol.md` § 2): `ok lens_cs_up_us=<µs>`, plus `hint=LENS_CS_deja_haut_objectif_deja_reveille` below 200 µs; or `<failure> hint=on_tente_le_0x01_quand_meme` | `firmware/components/session/std.c` at commit 7631eb6 |
| `* std step=01 reply_len=<n>`, `step=07 reply_len=<n>`, `step=08 body_flags=<hh> reply_len=<n>`, `step=0B/09/0D ok`, `step=10 homing=done`, `step=0A ok` | each initialisation request answered; `<n>`: the reply's message size | same file |
| `* std step=3F rx02=<n> reply=<hex>` | the `0x3F` (name) reply, raw, type byte included; `<n>`: `0x02` received while waiting | same file |
| `* homing done t=<ms>` | the deferred `0x10` reply arrived | same file |
| `* lens mf=<0/1> 0x05[62]=<hh>`, `* lens btn=<hh>` | `0x05` offset 62 and offset 64 changed | same file |
| `* lens fnum_x10=<f×10> …`, `* lens std_move=<n>`, `* lens pos=<n>` | aperture (f-number × 10) read in the `0x05`; `0x05` offset 60 as a signed number; focus position of the `0x06`. At most one line per 100 ms, on change | same file |
| `* ring ap fnum_x10=<f×10> source=board` | the aperture target the board set, one third-stop from the previous one, after a ring turn | same file, `std_ap_step()` |
| `* limits focal=<mm> source=<src> min=<n> max=<n> home=<n>` | stored focus limits for that focal length | `firmware/components/calib/calib.c` at commit 7631eb6 |
| `* svc main=<c> …` | a reply on the `0x40` service channel (`samyang.md` § 6): `V` gives the lens firmware version | `firmware/components/drv_samyang/drv_samyang.c` at commit 7631eb6 |
| `* lens switch_notify=ok astro=? hint=…` | the service-channel notifications were armed | same file |
| `* ring mode=<aperture/focus> source=<s> 0x05[62]=<hh>` | the role of the ring, from `0x05` offset 62 | same file |
| `* ring gesture pulses=<n> (seuil <s> par tiers de diaph.)` | the end of a ring gesture (a pause of 400 ms): `<n>` the number of `0x05` with a non-zero offset 60, signed by direction; `<s>` such frames per third-stop | same file |

### 3.2 firmware2 (proof commits c9b674e, 5549081, 75a30de, 4cf8726)

In firmware2 every journal line carries the board time `<t>` after its keyword, and the ceiling
counts every line, not only received frames: under a busy `LOG ON` or `LOG ALL`, event lines can
be left out too.

Each row is proved at the first proof commit where its form appears, and the same code still
writes it at every later proof commit: `firmware2/components/journal/journal.c` at commit 5549081,
at commit 75a30de and at commit 4cf8726 write every journal form of this table and of § 2.2 that
an earlier proof commit wrote, and `firmware2/components/host/host.c` at commit 5549081, at
commit 75a30de and at commit 4cf8726 every reply of both tables. The rows marked c9b674e
therefore also hold for `195_sony.txt` (5549081), `sony-2.txt` and `firmware2_ring.txt` (75a30de),
and `sony.txt` (4cf8726).

| Form | Meaning | Proof |
|---|---|---|
| `* rxflood <n>` | `<n>` received frames left out by the ceiling in the last window | `firmware2/components/journal/journal.c` at commit c9b674e, `admit()` |
| `* session <t> <state>` | the session state machine entered `<state>`: `off`, `powering`, `identifying`, `homing`, `restoring`, `ready`, `recovering`, `fault` (then the error) | same file, `bsk_journal_session()`, `bsk_state_name()` |
| `* std <t> step=handshake result=<r>[ lens_cs_up_us=<µs>]` | the power-up handshake: `ok`, `LENS_CS_never_high` (then no time), `LENS_CS_stuck_high`; `<µs>`: how long `LENS_CS` took to rise, `0` when it was already high | same file, `bsk_journal_handshake()` |
| `* motion <t> <state>` | the focus-move machine entered `<state>`: `idle`, `planning`, `commanded`, `moving`, `settling`, `arrived`, `stalled`, `aborted` | same file, `bsk_journal_motion()`, `bsk_motion_name()` |
| `er nocap`, `er nocap aperture`, `er busy`, `er range nomark` | refusals: not served (`aperture`: no aperture range known); a move in progress; no mark stored | `firmware2/components/host/host.c` at commit c9b674e |
| `* lens <t> 0x05[<off>]=<hh>` | `0x05` offset 62, 64 or 66 changed; the first value of each in a session is written too | `firmware2/components/journal/journal.c` at commit 75a30de, `bsk_journal_lens()`; `firmware2/components/session/ring.c` at commit 75a30de, `ring_frame()` |
| `* ring <t> mode=<aperture/focus> source=0x05` | the role of the ring, from `0x05` offset 62 (`01` aperture, `03` focus) | `firmware2/components/journal/journal.c` at commit 75a30de |
| `* restore <t> mark=<n> dir=<inc/dec> x=<n> path=direct`, `* restore <t> end=<why>` | the return to the stored mark at the end of a start | same file |
| `* std <t> step=08 body_flags=<hh> samyang=<yes/no>` | the `0x08` request of the init is sent: its offset 0, and whether the lens was recognised as a Samyang | same file |
| `* ring <t> gesture pulses=<n> thirds=<m>` | the end of a ring gesture in the aperture role (a pause of 400 ms, or a change of direction): `<n>` the number of `0x05` with a non-zero offset 60, signed by direction, `<m>` the third-stops applied | same file; `firmware2/components/session/ring.c` at commit 75a30de, `gesture_end()` |
| `* ring <t> gesture pulses=<n> thirds=<m> frames=<k> capped=<c>` | the same, but `<n>` is now the signed **sum** of `0x05` offset 60 over the gesture (commit a252911), `<k>` the number of `0x05` with a non-zero offset 60, `<c>` the thirds dropped by the limit of three per second | `firmware2/components/journal/journal.c` at commit 4cf8726; `firmware2/components/session/ring.c` at commit 4cf8726 |
| `* <tx/rx> <t> ~<tt>[ s<hh>][ <off>=<hex>…]` | **difference form**, under `LOG ALL` only: the frame of type `<tt>` written as its changes against the previous frame of the same direction and type (`0x03`, `0x04` sent; `0x02`, `0x05`, `0x06` received). `s<hh>`: the sequence number, written only when it does not follow the previous step. `<off>=<hex>`: from message offset `<off>` (`protocol.md` § 0), the changed bytes, run together. Nothing after `~<tt>`: the frame is identical. The first frame of each kind is written whole, and the references are dropped after a `0x0A` and after any line left out | same file, `diff_line()`, `frame_all()` |

Under `LOG ALL`, the difference form (commit c9ae37f onwards) also takes the frames out of the
ceiling; before it, `LOG ALL` writes whole frames under the ceiling, with `* rxflood` lines.

### 3.3 firmware, version 1.0 (proof commit 99809eb)

`samyang.txt` uses the forms of § 3.2 (`firmware/components/journal/journal.c` at commit 99809eb)
and two more:

| Form | Meaning | Proof |
|---|---|---|
| `* btn <t> press=<short/long/held> result=<ok/er/ignore>` | the barrel button, decided on release | `firmware/components/journal/journal.c` at commit 99809eb, `bsk_journal_btn()` |
| `* mark <t> set key=<key> pos=<n> dir=<inc/dec>` | a mark stored for this lens: its storage key, position and approach direction | same file, `bsk_journal_mark()` |

## 4. The captures, one by one

### 4.1 `emount-bench-2026-09-28T17-45-01-034Z.txt`

- **Board firmware: v1, from commit e099498.** The v1 journal forms (§ 3.1), including
  `* std step=3F …` (line 25), first written at commit e099498. v1 code does not change after
  commit c96d3e2: the capture cannot tell apart the commits from e099498 to c96d3e2.
- **Lens**: `FE 24-105mm F4 G OSS` (line 2); `* boot identity="Sony FE 24-105mm F4 G OSS fw3.01"`
  (line 51).
- **Page**: `carte : protocole 1.3` (line 1), `focus par « standard »` (line 2): a page before
  commit 5b333ee.
- **Date**: 2026-09-28, exported at 17:45:01 UTC (file name, § 2.1); the page clock reads
  19:44:26–19:44:56, local time two hours ahead of UTC.
- **What it shows**: `LOG ON` (line 3), a lens restart `b` (line 5) and the whole v1 boot: handshake
  with the lens already awake (line 15), the init requests and replies including the name
  (lines 15-48), stored limits (line 53), boot ok (line 57). `LOG ALL` (line 62): the loop under the
  ceiling, with the Sony's class-2 `0x02` messages (lines 64, 67), `0x03` frames of 32 bytes that
  end with an appended `2F` message (line 69), and `* rxflood suppressed=…` lines (from line 222).
  `LOG OFF` at line 950.
- **Cited by**: `protocol.md` § 3.2, § 3.3, § 4.2, § 7.2.

### 4.2 `firmware1_ring.txt`

- **Board firmware: v1, from commit 75461fa, before 9f90b7d.** The line
  `* lens switch_notify=ok astro=? hint=decide_a_la_premiere_bascule_M1_ou_au_premier_appui`
  (line 63) is written by v1 code up to commit 7631eb6; commit 9f90b7d removed it. Every other form
  of the capture is already written at commit 75461fa, the first state of the repository.
- **Lens**: `Samyang AF 135.0mm f/1.8-22 fw1.05` (line 2); `* svc main=V version=1.05` (line 51).
- **Page**: before commit 5b333ee (`carte : protocole 1.3`, line 1).
- **Date**: not recorded. Page clock: 05:54:36–05:55:02. Window commits: 2026-09-20 to 2026-09-25.
- **What it shows**: a restart `b` (line 5) with `LENS_CS` never high (line 13), the init
  `0x01`, `0x07`, `0x08`, `0x0B`/`0x09`/`0x0D`, `0x10`, `0x0A` (lines 14-46), the service channel
  (`'V'` reply line 51, `'M'` sent line 58, its reply line 62), boot ok (line 65). Then the aperture ring turned:
  `* lens std_move`, `* ring ap`, `* lens fnum_x10` and `* ring gesture` lines (lines 67-160).
- **Cited by**: `samyang.md` § 3.2.

### 4.3 `emount-bench-2026-09-29T18-12-13-937Z.txt`

- **Board firmware: firmware2, from commit c9b674e, before 5549081.** The
  handshake line (line 85) is written from commit c9b674e. `c` answered `ok` and followed by
  `* session … homing` (lines 111-113): commit 5549081 stopped serving `c`. `f65535` answered `ok`
  (lines 578-579): commit 20671f9, later still, refuses a target outside the limits.
- **Lens**: `FE 24-105mm F4 G OSS` (line 2).
- **Page**: before commit 5b333ee (line 1).
- **Date**: 2026-09-29, exported at 18:12:13 UTC (file name); page clock 20:08:24–20:11:46,
  local time two hours ahead of UTC.
- **What it shows**: focus moves without the journal (lines 3-78), aperture refused
  (`er nocap aperture`, lines 31-36), `er busy` (line 51), `ms` and `mg` refused (lines 73-78);
  `LOG ON` (line 79), restart (line 81), ready (line 109); `c`, a homing with `10 08` (lines 111-120);
  `LOG ALL` (line 121): the loop with `* rxflood` lines, a move to `f0` (line 393), to `f65535`
  (line 578), `c` (line 700); the link lost (lines 1543-1548).
- **Cited by**: `protocol.md` § 3.3, § 4.2, § 7.4.1, § 7.4.2, § 7.5, § 7.6, § 7.6.1, § 7.8,
  § 7.14, § 11.

### 4.4 `sony_boutons.txt`

- **Board firmware: firmware2, from commit d01ebf0, before 5f13a3e.** The
  `* rxflood <n>` form is firmware2's (§ 3.2) and the page got its `v` reply, which firmware2
  serves from commit d01ebf0. Under `LOG ALL` the frames are whole and capped (lines 51-111),
  which rules out commit c9ae37f and later (§ 3.2). The `0x04` offset 3 is `81` while `0x05`
  offset 62 is `01` (lines 52-54): from commit 5f13a3e, the board sets bit 1 of that byte when the
  ring has the aperture role (`firmware2/components/session/ring.c` at commit 5f13a3e,
  `BODY_AF_BIT`), as `sony.txt` line 53 shows.
- **Lens**: `FE 24-105mm F4 G OSS` (line 2).
- **Page**: before commit 5b333ee (line 1).
- **Date**: not recorded. Page clock: 23:45:07–23:46:13. Window commits: 2026-09-27 to 2026-09-30.
- **What it shows**: 23 absolute focus moves without the journal (lines 3-48); `LOG ALL`
  (line 49): the loop, while `0x05` offsets 62, 64 and 66 change (lines 53, 113, 293, 652, 772);
  `LOG OFF` at line 831.
- **Cited by**: `protocol.md` § 7.5.

### 4.5 `195_samyang.txt`

- **Board firmware: firmware2, from commit d01ebf0, before 5f13a3e**, on the same
  evidence as `sony_boutons.txt`: `* rxflood <n>` (line 1), whole frames under `LOG ALL`, `0x04`
  offset 3 `81` while `0x05` offset 62 is `01` (lines 3-5).
- **Lens**: the capture does not name it. `samyang.md` (§ 4.3, § 4.8, § 5.1, § 5.4) attributes it
  to the Samyang AF 135.
- **Page**: the capture does not show it (it starts after the connection).
- **Date**: not recorded. Page clock: 23:02:11–23:02:56. Window commits: 2026-09-27 to 2026-09-30.
- **What it shows**: the loop under `LOG ALL`, from its middle (no header): `0x03`, `0x04`, `0x05`
  (105 bytes, two lines), `0x06`, and `* rxflood` lines. The `0x05` offset 60 and the `0x06`
  position move together (lines 124-146); offset 64 goes to `08` (line 865) and back; offset 62
  goes to `03` (line 395) and back. `LOG OFF` at line 1291, then the link is lost.
- **Cited by**: `samyang.md` § 4.3, § 4.8, § 5.1, § 5.4.

### 4.6 `195_sony_boutons.txt`

- **Board firmware: firmware2, from commit c9b674e, before 5f13a3e.** The
  handshake line (line 8) is written from commit c9b674e; whole capped frames under `LOG ALL` and
  `0x04` offset 3 `81` with `0x05` offset 62 `01` (lines 37-39), as in § 4.4.
- **Lens**: `FE 24-105mm F4 G OSS` (line 2).
- **Page**: before commit 5b333ee (line 1).
- **Date**: not recorded. Page clock: 23:04:25–23:04:43. Window commits: 2026-09-29 to 2026-09-30.
- **What it shows**: `LOG ON`, restart `b`, handshake with `lens_cs_up_us=0` (line 8), the init,
  ready (line 33); `LOG ALL` (line 34): the loop, with `* rxflood` lines; `LOG OFF` at line 306.
- **Cited by**: `protocol.md` § 7.5.

### 4.7 `195_sony.txt`

- **Board firmware: firmware2, from commit 5549081, before 2baff88.** `h` and
  `c` answer `er nocap` (lines 253-256): from commit 5549081 they are not served (`firmware2/components/host/host.c`
  at commit 5549081, the reply to an unknown line). `a22` answers `er nocap aperture` while the
  session is ready (lines 118-119): from commit 2baff88 the board reads the aperture range in the
  `0x08` reply and serves `a`. No `* lens` line at any of the six starts, and `0x04` offset 3 is
  `81` (line 36): consistent, as commit 5f13a3e comes after 2baff88.
- **Lens**: `FE 24-105mm F4 G OSS` (line 2).
- **Page**: before commit 5b333ee (line 1).
- **Date**: not recorded. Page clock: 22:57:33–23:00:09. Window commits: 2026-09-29 to 2026-09-30.
- **What it shows**: `LOG ON`, a restart (line 5), handshake (line 8), the init (lines 10-31),
  ready (line 32); a series of absolute and relative focus moves, each with its `0x04` carrying a
  `1D` order and the `0x06` followed by `1D 00` (from line 33), one `stalled` (line 45); aperture
  refused (lines 118-126, 247-252); `h` and `c` refused; five more restarts (lines 257-440), one
  with `lens_cs_up_us=0` (line 378); the link lost (lines 457-458).
- **Cited by**: `protocol.md` § 1.4, § 2, § 4.2, § 6.2, § 7.4.2, § 7.6, § 7.6.1, § 11.

### 4.8 `sony-2.txt`

- **Board firmware: firmware2, from commit 5f13a3e, before c9ae37f.** `* lens`
  and `* ring … source=0x05` lines (lines 6-9) are written from commit 5f13a3e, and the `0x04`
  offset 3 is `83` while the ring has the aperture role (line 13). Under `LOG ALL` the frames are
  whole and capped, with `* rxflood` lines (from line 42): before commit c9ae37f.
- **Lens**: `FE 24-105mm F4 G OSS` (line 3).
- **Page**: before commit 5b333ee (line 2).
- **Date**: not recorded. Page clock: 04:31:09–04:31:28. Window commits: 2026-09-30 to 2026-10-02.
- **What it shows**: an unexpected line in answer to `v` (line 1, § 2.3); `LOG ON` (line 4); the
  ring role switched to focus and back (lines 6-9); `LOG ALL` (line 10): the loop of a session
  already running (no init in the file), whole frames, `* rxflood` lines; `LOG OFF` at line 252.
- **Cited by**: `protocol.md` § 4.2, § 7.5, § 7.6.

### 4.9 `firmware2_ring.txt`

- **Board firmware: firmware2, from commit 75a30de, before a252911.** The line
  `* std … step=08 … samyang=yes` (line 19) is written from commit 75a30de; the gesture lines carry
  `thirds=` and no `frames=` (from line 51), the form written from commit e484995 and before
  a252911.
- **Lens**: `SAMYANG AF 135mm F1.8` (line 2), the name of its `0x3F` reply (lines 17-18).
- **Page**: before commit 5b333ee (line 1).
- **Date**: not recorded. Page clock: 05:47:06–05:49:00. Window commits: 2026-10-02.
- **What it shows**: `LOG ON`, restart (line 5), `LENS_CS` never high (line 10), the init with the
  `0x3F` name (lines 12-38), the return to the stored mark: `restoring`, a `1D` order to 24384 and
  its `1D 00` acknowledgement, `ready` (lines 39-48); then the ring turned in the aperture role:
  37 gesture lines (lines 51-87).
- **Cited by**: `samyang.md` § 1.1, § 1.3, § 4.2, § 4.4.

### 4.10 `sony.txt`

- **Board firmware: firmware2, from commit 4cf8726, before 5b333ee.** The
  difference form `~<tt>` under `LOG ALL` (from line 54) is written from commit c9ae37f, the
  gesture lines with `capped=` (line 38) from commit 4cf8726. The page reads `v` as
  `protocole 1.3` (line 1): from commit 5b333ee the board answers its firmware version instead,
  `1.0b` (`firmware/components/host/host.c` at commit 5b333ee; `firmware/CMakeLists.txt` at commit
  5b333ee, `PROJECT_VER`).
- **Lens**: `FE 24-105mm F4 G OSS` (line 2).
- **Page**: before commit 5b333ee.
- **Date**: not recorded. Page clock: 14:47:53–14:48:40. Window commits: 2026-10-02.
- **What it shows**: `LOG ON`, restart (line 5), handshake (line 8), the whole init: `0x01`, `0x07`,
  `0x3F`, `0x08`, `0x0B`, `0x09`, `0x0D`, `0x10` (deferred reply, lines 29-30), `0x0A` (lines
  10-32); `* lens` and `* ring` lines, ready (line 37); ring gestures; `LOG ALL` (line 47): the loop
  in difference form; `LOG OFF` at line 1418, then the link is lost.
- **Cited by**: `protocol.md` § 2, § 3.3, § 3.4, § 4.2, § 6.1, § 7.1, § 7.7, § 7.8, § 7.9, § 7.10,
  § 7.11, § 7.13, § 7.14, § 7.19, § 11.

### 4.11 `samyang.txt`

- **Board firmware: firmware version 1.0, from commit 99809eb.** The board answers `v` with `1.0`
  (line 1): `PROJECT_VER` is `1.0` from commit 99809eb (`firmware/CMakeLists.txt` at commit
  99809eb); before it, `1.0b` (commit 5b333ee). Every form of the capture is already written at
  99809eb, the mark command `js` included (§ 3.3). The page line `prêt : <name>`, without
  `focus par` (line 2), is what every page from commit 5b333ee writes for a board that answers `n`
  with `er nocap`, as this firmware does (§ 2.1): it does not narrow the window. Every
  form of the capture is still written at commit 111fda2 (`4_Firmware/components/journal/journal.c`
  and `5_App/emount-bench.html` at commit 111fda2), and `PROJECT_VER` is still `1.0`: the capture
  cannot be placed more precisely.
- **Lens**: `SAMYANG AF 135mm F1.8` (line 2).
- **Page**: from commit 5b333ee (`carte : firmware`, line 1).
- **Date**: not recorded. Page clock: 00:19:21–00:21:55.
- **What it shows**: `LOG ON` (line 3); ring gestures (lines 5-32 and later); three moves
  (`f15359` line 33, `f25726` line 44, `f26995` line 73), each with its `0x04` carrying a `1D` order
  and the `0x06` followed by `1D 00`; a mark stored with `js` (lines 41-43); twice, the barrel
  button pressed (`0x05[64]=08` then `00`, lines 52-53 and 81-82) and a short press sending the
  lens back to the mark (lines 55, 84).
- **Cited by**: `samyang.md` § 0, § 4.2, § 4.3, § 4.4, § 4.8, § 5.1.

## 5. The captures of `4_Firmware/traces/`

Four captures of the Samyang AF 135 F1.8 FE, firmware 1.05, published with the firmware's tests,
which read them (`4_Firmware/sim/test/replay.c`, `4_Firmware/sim/test/test_session135.c`,
`4_Firmware/sim/test/test_lens135.c`, `5_App/test/test_app_vm.cjs`). Their file names carry
2026-09-20. They were taken with a v1 board firmware older than the first commit of this
repository: `4_Firmware/traces/sy135-2026-09-20-dump05.txt` line 1 names the board version
`0.8.10`, where `firmware/CMakeLists.txt` at commit 75461fa sets `0.9.3`, and
`4_Firmware/traces/sy135-2026-09-20-reboots-0x10-stall.txt` line 136 has a `* boot module=…`
line that no commit of the repository writes. Most of their journal lines have the v1 forms of
§ 3.1; § 3.1 does not cover the older ones (`* boot module=…`, `* lens astro=…`,
`* btn court=…`).

- `4_Firmware/traces/sy135-2026-09-20-full.txt` (395 lines): a restart `b` and the whole v1 boot
  (lines 1-59), `DUMP 05` and `DUMP 06` (lines 61-64), one second of `LOG ALL` (lines 65-194), then
  `LOG ON` (line 199) and the ring turned in the aperture role, until `q` (line 389).
- `4_Firmware/traces/sy135-2026-09-20-astro-normal.txt` (425 lines): `LOG ON`, button presses
  (short, line 26; long, line 88), two restarts (lines 116 and 215), and the lens's astro mode
  detected after the second (line 300).
- `4_Firmware/traces/sy135-2026-09-20-reboots-0x10-stall.txt` (951 lines): four restarts `b`
  (lines 102, 233, 474, 828) and two more lens losses (lines 310 and 535), each followed by a new
  boot; the boot of line 548 names the lens `Samyang AF fw1.05` and its calibration fails
  (lines 553 and 575).
- `4_Firmware/traces/sy135-2026-09-20-dump05.txt` (11 lines): not a page export, an annotated
  list of raw `0x05` frames of 97 bytes with the mode switch in positions M2, M1, then M2 again,
  and a `0x06` at rest; its `#` lines are the comments of the person who made it.

`protocol.md` and `samyang.md` cite them as `` `4_Firmware/traces/<file>` line <n> ``.
