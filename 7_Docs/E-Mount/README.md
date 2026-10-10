# Sony E-mount interoperability

**Date** : 2026-10-08
**Dernière révision** : 2026-10-08
**Statut** : actif — index of `7_Docs/E-Mount/` (ticket #544)
**Référencé par** : `protocol.md`, `samyang.md`, `tamron.md`, `traces.md`, `provenance.md`
**Dérivé de** : the documents of this folder

How a Sony E-mount camera body talks to its lens, seen from the body: the ten contacts, the
frames, the session, every message and what decides the lens's answers, for Sony, Samyang and
Tamron lenses. With this folder alone, one should be able to rewrite the body-side firmware of
this project (`4_Firmware/`) and the fake lenses it tests against, without opening a lens
firmware.

What the project adds to the public work (`protocol.md` § 10): bench captures of real lenses
taken with our own board, the code that drives them, and what each maker does with each common
message.

## Reading order

1. `protocol.md`: the common reference. Read it first; the other files use its conventions.
2. `samyang.md`: what the body sees of Samyang lenses, beyond `protocol.md`.
3. `tamron.md`: what the body sees of Tamron lenses, beyond `protocol.md`.
4. `traces.md`: how to read the bench captures the documents cite.
5. `provenance.md`: where each fact comes from, and what is not published.

## `traces/`

The bench captures cited as `` `traces/<file>` line <n> ``: eleven journals of our board facing a
Sony FE 24-105 mm F4 G OSS or a Samyang AF 135 F1.8 FE, published as they were exported.
`traces.md` describes each one.

## Licence

This folder follows the licences of the repository: see § Licences of the root `README.md`.
