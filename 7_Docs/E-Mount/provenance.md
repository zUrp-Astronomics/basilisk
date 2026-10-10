# Where each fact comes from, and what is not published

**Date** : 2026-10-08
**Dernière révision** : 2026-10-10
**Statut** : actif — fifth interoperability document of `7_Docs/E-Mount/` (ticket #544)
**Référencé par** : `7_Docs/E-Mount/README.md`, `protocol.md` (§ 13), `traces.md` (§ 0)
**Dérivé de** : `7_Docs/E-Mount/protocol.md` (§ 0, § 10), `samyang.md` (§ 0), `tamron.md` (§ 0), `traces.md`; the captures of `7_Docs/E-Mount/traces/` and `4_Firmware/traces/`

## 0. What this document is

The documents of `7_Docs/E-Mount/` describe the Sony E-mount from the body's side, well enough to
rewrite our board's firmware and our fake lenses without opening a lens firmware. This file says
what each of their facts rests on, and what the project holds but does not publish.

**The rule, in one sentence: what is not ours is not published.** The lens firmwares and what is
read out of them belong to their makers. The documents are written in our own words, from what
we observed on the wire and what we understood.

## 1. The sources, and their order

`protocol.md` § 0 sets the order of authority (a trace, then the code of this repository, then
our static analyses) and the form of each proof; `samyang.md` § 0 and `tamron.md` § 0 apply it to
their maker. They are not repeated here. The sources are of four kinds.

1. **Our captures.** Journals of our board, playing the body in front of a real lens, exported by
   the bench page: the eleven files of `traces/`, read with `traces.md`, and the four of
   `4_Firmware/traces/` (`traces.md` § 5). Cited as `` `traces/<file>` line <n> `` and
   `` `4_Firmware/traces/<file>` line <n> ``.
2. **The code of this repository**, as it is or at a named commit: the board firmware
   (`4_Firmware/`), its fake lenses (`4_Firmware/sim/lens135.c`, `4_Firmware/sim/lens_std.c`), the
   bench page (`5_App/emount-bench.html`) and the contract `7_Docs/PROTOCOL.md`. Cited by file and
   function or constant; `traces.md` cites earlier states as `` `<path>` at commit <sha> ``.
3. **Our static analyses of lens firmwares**: what the project read in the firmwares of twelve
   Samyang and seven Tamron lenses. Every fact that rests on them carries the words
   **static analysis, not observed**. The analyses themselves are not published (§ 3).
4. **Public sources.** Exactly the ones the documents cite:
   - the Google Doc "Sony E Mount Lens Protocol" (bostwickenator, Entropy512):
     `https://docs.google.com/document/d/1iw54nzrF0bzQgLINpcP9F8Odd0N5cd7LjlwCDPTNZK0/edit`
     (`protocol.md` § 1.1, § 10);
   - weiziqian, `https://github.com/weiziqian/E-mount-protocol-RE` (`protocol.md`), and the pages of
     it the documents name under `https://github.com/weiziqian/E-mount-protocol-RE/blob/main/`:
     `docs/frame_format.md`, `docs/msg_0x01.md`, `docs/msg_0x0A.md` (`protocol.md`),
     `docs/msg_0x19.md`, `docs/msg_0x4C.md`, `docs/msg_0x5A.md` (`tamron.md`);
   - LexOptical: the emulator `https://github.com/LexOptical/E-Mount` and the NEX-7 captures
     `https://github.com/LexOptical/E-Mount-Traffic-Samples`, cited file by file under
     `https://github.com/LexOptical/E-Mount-Traffic-Samples/blob/master/`
     (`nex7-sony-28-70mm-at-70mm.txt`, `nex7-sony-55mm-init.txt`, `nex7-clocks-and-sync-lines.txt`,
     `nex7-voigtlander-15mm-f4.5-inf.txt`, `nex7-voigtlander-15mm-4.5-30cm-to-inf.txt`,
     `nex7-zeiss-28mm-at-2.8-init.txt`) (`protocol.md`);
   - Entropy512's thread `https://www.dpreview.com/forums/thread/3872069`, tools
     `https://github.com/Entropy512/emount_tools` and decoder
     `https://github.com/Entropy512/libsigrokdecode` (`protocol.md` § 10);
   - the logic captures `https://github.com/EliasOenal/sigrok-dumps/tree/master/lens_mounts/sony_emount`
     (`protocol.md` § 10);
   - ExifTool, `https://exiftool.org`, for the lens names (`samyang.md` § 1.1, `tamron.md` § 1.1).

   `protocol.md` § 10 also names, without a URL, the Dyxum forum topic where Leegong published
   message sizes. `protocol.md` § 10 says what each of these sources published first, and what
   this project adds.

## 2. What rests on what

### 2.1 `protocol.md`

| Maker, lens | Captures | Code | Static analysis | Public sources |
|---|---|---|---|---|
| our board (the body side) | — | yes: what the board sends and how it reads replies (§ 6, § 7, § 11) | — | — |
| Sony FE 24-105 mm F4 G OSS | ours: `traces/sony*.txt`, `traces/195_sony*.txt`, `traces/emount-bench-*.txt` | — | none | — |
| the lenses of the public captures and descriptions (LexOptical: a NEX-7 with Sony, Voigtländer and Zeiss lenses; sigrok-dumps: an A6000 with Sony lenses and an adapter) | none of ours | — | none | LexOptical, sigrok-dumps, weiziqian, the Google Doc, Entropy512 |
| Samyang AF 135 F1.8 FE | ours: `4_Firmware/traces/`, and the Samyang files of `traces/` | yes: the fake AF 135 (`4_Firmware/sim/lens135.c`) | yes, firmware 1.06 | — |
| Tamron F051 (E 24 mm F2.8) | none: no Tamron lens has been connected | yes: the fake F051 (`4_Firmware/sim/lens_std.c`), written from the analyses | yes | — |

### 2.2 `samyang.md`

| Lens | Captures | Code | Static analysis |
|---|---|---|---|
| AF 135 F1.8 FE, firmware 1.05 | `4_Firmware/traces/`; `traces/samyang.txt`, `traces/firmware1_ring.txt`, `traces/firmware2_ring.txt`, `traces/195_samyang.txt` | the board (`4_Firmware/components/session/`, `4_Firmware/components/bench_core/bench_core.c`), the fake AF 135 and its byte templates copied from the captures (`4_Firmware/sim/lens135.c`, `4_Firmware/sim/sources135.c`), `5_App/tool_station/`, `7_Docs/PROTOCOL.md` | firmware 1.06; where it and a capture of the 1.05 disagree, the capture wins (`samyang.md` § 0, § 3.2) |
| the eleven other Samyang models | none: never connected | — | the only source; their sections say so at their head |

The names of the models come from ExifTool (`samyang.md` § 1.1).

### 2.3 `tamron.md`

| Lens | Captures | Code | Static analysis | Public sources |
|---|---|---|---|---|
| F051 and six other Tamron models | none | the fake F051 (`4_Firmware/sim/lens_std.c`, `4_Firmware/sim/test/test_lens_std.c`, `4_Firmware/sim/test/constats_std.txt`) and the board's path for a lens that is not a recognised Samyang (`4_Firmware/components/session/`) | yes, every Tamron fact | weiziqian, for `0x4C`, `0x5A` and `0x19`; ExifTool for the names |

### 2.4 `traces.md`

The captures of `traces/` and `4_Firmware/traces/`, and the history of this repository: which
board firmware and which page produced each line (`traces.md` § 0).

## 3. What is not published, and why

The project holds material it does not publish, because it is not ours.

- **The lens firmware images, their decompilations, and our analyses of them.** The documents
  give the conclusions of the analyses, in our words, marked **static analysis, not observed**;
  never an extract, pseudo-code, an address, a line of a decompilation or of an analysis, or a
  name taken from a decompilation.
- **Raw bytes taken from a firmware image.** A lens answers some requests with constant blocks
  of its flash, or with values computed from its calibration tables. The documents publish the
  decoded fields (meaning, offset, and the value where it has a meaning of its own: an
  identifier, a flag, a size, a version, a bound, a name in clear), the sizes and the masks; bytes
  not decoded are marked **not published (extracted from firmware, meaning unknown)**
  (`samyang.md` § 0, `tamron.md` § 0). The fake F051 of this repository answers with the decoded
  fields only, and zero for every byte not published (`tamron.md` § 0).
- **Captures no document cites.** The project's captures include five more exports of the same
  bench page, made during the same bench sessions as the published ones; two of them are earlier
  exports of a session whose later export is published. They are not published: no statement
  rests on them.

## 4. Observed on the wire, or taken from a firmware image

**The same bytes can be published or not, depending on where they were read.**

- **Observed on the wire.** The `* rx` lines of our captures show what a lens answered our board,
  as it passed on the contacts: its constant replies included (the `0x01`, `0x07`, `0x08` and `0x3F`
  replies of `traces/sony.txt` lines 10-32, for instance). These are our observations, made with
  our board, and the captures are published whole; the documents cite them by line.
- **Taken from a firmware image.** The same reply, read in the lens's firmware image (a constant
  block, a calibration table), is the maker's material. It is not published, and where the
  documents need it they give the decoded fields only (§ 3).

A reader who finds in a published capture a byte block that a document declares "not published"
sees no contradiction: the capture shows what one lens sent, once, to our board; the document
refuses to copy the block out of the firmware image. The first is our observation, the second the
maker's material.
