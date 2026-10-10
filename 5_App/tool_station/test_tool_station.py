#!/usr/bin/env python3
# SOURCE: PROTOCOL.md § 3.1 (la commande de labo `CUSTOM`)
# AUTHOR: engineer
# DATE: 2026-10-03
# STATUS: actif — joué par run.sh, à côté (job `test` de la CI)
# tool_station.py contre un faux port, sans pyserial : `serial` est rendu inimportable, le port est injecté dans main().
#
# Le faux port joue la carte telle que PROTOCOL.md § 3.1 la décrit : `CUSTOM READ` -> `ok` et 16 octets, la configuration
# au 8e ; `CUSTOM WRITE <h><b>` -> `ok` et 16 zéros, la configuration rangée. Il note chaque ligne reçue. Les attendus (les
# lignes envoyées, les octets) sont écrits ici à la main.
import io
import os
import sys
import unittest

sys.modules["serial"] = None          # `import serial` lève ImportError : le test ne dépend pas de pyserial
sys.dont_write_bytecode = True        # rien d'écrit dans le dépôt (__pycache__)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tool_station  # noqa: E402

ZEROS = " ".join(["00"] * 16)


class FauxPort:
    """La carte au bout de l'USB, un 135 en READY derrière elle."""

    def __init__(self, cfg=0x10, read_reply=None, write_reply=None, ignore_write=False, journal=False):
        self.cfg = cfg
        self.read_reply = read_reply      # une réponse forcée à CUSTOM READ (une erreur de la carte)
        self.write_reply = write_reply
        self.ignore_write = ignore_write  # l'objectif rend l'écho sans ranger (une valeur qu'il refuse)
        self.journal = journal            # des lignes `* …` (LOG ON) avant chaque réponse
        self.lines = []
        self.out = []

    def write(self, b):
        line = b.decode("ascii")
        assert line.endswith("\n") and line.count("\n") == 1, line
        line = line[:-1]
        self.lines.append(line)
        if self.journal:
            self.out.append(b"* rx 12345 F0 1B 00 02\n")
        if line == "CUSTOM READ":
            r = self.read_reply or "ok 00 00 00 00 00 00 00 %02X 00 00 00 00 00 00 00 00" % self.cfg
        elif line.startswith("CUSTOM WRITE "):
            if not self.ignore_write:
                self.cfg = int(line[-2]) << 4 | int(line[-1])
            r = self.write_reply or "ok " + ZEROS
        else:
            r = "er nocap"
        self.out.append((r + "\n").encode("ascii"))

    def readline(self):
        return self.out.pop(0) if self.out else b""


class Demandes:
    """La confirmation : note chaque question, rend la réponse prévue."""

    def __init__(self, answer):
        self.answer = answer
        self.prompts = []

    def __call__(self, prompt):
        self.prompts.append(prompt)
        return self.answer


def run(argv, port, answer="o"):
    out = io.StringIO()
    ask = Demandes(answer)
    rc = tool_station.main(argv, port=port, demander=ask, sortie=out)
    return rc, out.getvalue(), ask


class Test(unittest.TestCase):
    def test_read(self):
        p = FauxPort(cfg=0x10)
        rc, out, ask = run(["--read"], p)
        self.assertEqual(rc, 0)
        self.assertEqual(p.lines, ["CUSTOM READ"])
        self.assertIn("M1=AF M2=AP (0x10)", out)
        self.assertIn("Débrancher puis rebrancher l'objectif.", out)
        self.assertEqual(ask.prompts, [])

    def test_write_both(self):
        p = FauxPort(cfg=0x10)
        rc, out, ask = run(["--M1", "AP", "--M2", "MF"], p)
        self.assertEqual(rc, 0, out)
        self.assertEqual(p.lines, ["CUSTOM READ", "CUSTOM WRITE 02", "CUSTOM READ"])
        self.assertEqual(p.cfg, 0x02)
        self.assertEqual(len(ask.prompts), 1)
        self.assertLess(out.index("configuration lue      : M1=AF M2=AP (0x10)"), out.index("configuration à écrire : M1=AP M2=MF (0x02)"))
        self.assertIn("configuration relue    : M1=AP M2=MF (0x02)", out)
        self.assertTrue(out.endswith("Débrancher puis rebrancher l'objectif.\n"), out)

    def test_confirmation_shown_before_question(self):
        p = FauxPort(cfg=0x10)
        out = io.StringIO()
        seen = []

        def ask(prompt):
            seen.append(out.getvalue())
            return "oui"
        tool_station.main(["--M2", "MF"], port=p, demander=ask, sortie=out)
        self.assertEqual(len(seen), 1)
        self.assertIn("configuration lue      : M1=AF M2=AP (0x10)", seen[0])
        self.assertIn("configuration à écrire : M1=AF M2=MF (0x12)", seen[0])
        self.assertEqual(p.lines, ["CUSTOM READ", "CUSTOM WRITE 12", "CUSTOM READ"])

    def test_one_position_keeps_other(self):
        p = FauxPort(cfg=0x21)
        rc, out, _ = run(["--M1", "af"], p)                          # M2 = AF, relu, gardé
        self.assertEqual(rc, 0, out)
        self.assertEqual(p.lines, ["CUSTOM READ", "CUSTOM WRITE 11", "CUSTOM READ"])
        p = FauxPort(cfg=0x21)
        rc, out, _ = run(["--M2", "AP", "--yes"], p)                 # M1 = MF, relu, gardé
        self.assertEqual(rc, 0, out)
        self.assertEqual(p.lines, ["CUSTOM READ", "CUSTOM WRITE 20", "CUSTOM READ"])

    def test_refused_confirmation(self):
        for answer in ("", "n", "non", "N", "x"):
            p = FauxPort(cfg=0x10)
            rc, out, ask = run(["--M1", "MF"], p, answer)
            self.assertEqual(rc, 1)
            self.assertEqual(p.lines, ["CUSTOM READ"], answer)
            self.assertEqual(p.cfg, 0x10)
            self.assertEqual(len(ask.prompts), 1)
            self.assertIn("rien n'est écrit", out)
            self.assertIn("Débrancher puis rebrancher l'objectif.", out)

    def test_yes_skips_question(self):
        p = FauxPort(cfg=0x10)
        rc, _, ask = run(["--M1", "AF", "--M2", "AF", "--yes"], p, answer="n")
        self.assertEqual(rc, 0)
        self.assertEqual(ask.prompts, [])
        self.assertEqual(p.lines, ["CUSTOM READ", "CUSTOM WRITE 11", "CUSTOM READ"])

    def test_factory_restore(self):
        p = FauxPort(cfg=0x22)
        rc, _, _ = run(["--M1", "AF", "--M2", "AP", "--yes"], p)
        self.assertEqual(rc, 0)
        self.assertEqual(p.lines[1], "CUSTOM WRITE 10")
        self.assertEqual(p.cfg, 0x10)

    def test_nothing_to_write(self):
        p = FauxPort(cfg=0x12)
        rc, out, ask = run(["--M2", "MF"], p)
        self.assertEqual(rc, 0)
        self.assertEqual(p.lines, ["CUSTOM READ"])
        self.assertEqual(ask.prompts, [])
        self.assertIn("rien à écrire", out)

    def test_invalid_values(self):
        for argv in (["--M1", "XX"], ["--M2", "3"], ["--M1", "M"], ["--M1"], [], ["--read", "--M1", "AF"], ["--yes"]):
            p = FauxPort()
            err = io.StringIO()
            old, sys.stderr = sys.stderr, err
            try:
                with self.assertRaises(SystemExit) as e:
                    run(argv, p)
            finally:
                sys.stderr = old
            self.assertEqual(e.exception.code, 2, argv)
            self.assertEqual(p.lines, [], argv)

    def test_board_errors(self):
        for reply in ("er nocap custom", "er nolens", "er busy boot", "er link timeout", "ok 00 00"):
            p = FauxPort(read_reply=reply)
            rc, out, ask = run(["--M1", "MF", "--yes"], p)
            self.assertEqual(rc, 1, reply)
            self.assertEqual(p.lines, ["CUSTOM READ"], reply)
            self.assertIn(reply, out)
        p = FauxPort(write_reply="er link aborted")
        rc, out, _ = run(["--M1", "MF", "--yes"], p)
        self.assertEqual(rc, 1)
        self.assertEqual(p.lines, ["CUSTOM READ", "CUSTOM WRITE 20"])
        self.assertIn("er link aborted", out)
        self.assertIn("Débrancher puis rebrancher l'objectif.", out)

    def test_no_reply(self):
        class Muet(FauxPort):
            def readline(self):
                return b""
        p = Muet()
        rc, out, _ = run(["--read"], p)
        self.assertEqual(rc, 1)
        self.assertIn("pas de réponse", out)

    def test_readback_mismatch(self):
        p = FauxPort(cfg=0x10, ignore_write=True)
        rc, out, _ = run(["--M1", "MF", "--yes"], p)
        self.assertEqual(rc, 1)
        self.assertEqual(p.lines, ["CUSTOM READ", "CUSTOM WRITE 20", "CUSTOM READ"])
        self.assertIn("pas celle écrite", out)

    def test_unreadable_kept_position(self):
        p = FauxPort(cfg=0x13)                                       # M2 lu à 3 : rien à garder
        rc, out, ask = run(["--M1", "MF", "--yes"], p)
        self.assertEqual(rc, 1)
        self.assertEqual(p.lines, ["CUSTOM READ"])
        p = FauxPort(cfg=0x13)
        rc, out, _ = run(["--M1", "MF", "--M2", "AP", "--yes"], p)  # les deux donnés : écrit
        self.assertEqual(rc, 0, out)
        self.assertEqual(p.lines[1], "CUSTOM WRITE 20")

    def test_journal_lines_skipped(self):
        p = FauxPort(cfg=0x10, journal=True)
        rc, out, _ = run(["--M2", "MF", "--yes"], p)
        self.assertEqual(rc, 0, out)
        self.assertEqual(p.cfg, 0x12)

    def test_rom_lines_before_first_reply(self):
        # Audit R5 : l'ouverture du port peut redémarrer la carte ; les lignes de la ROM de l'ESP32-S3 (son démarrage
        # ordinaire, documenté par ESP-IDF, la console de la carte étant muette au-delà) et des octets hors ASCII
        # arrivent après l'envoi de la première commande, pendant l'attente de sa réponse. Aucune n'est prise pour elle.
        rom = [b"ESP-ROM:esp32s3-20210327\r\n", b"Build:Mar 27 2021\r\n",
               b"rst:0x15 (USB_UART_CHIP_RESET),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\n", b"Saved PC:0x40378ad6\r\n",
               b"SPIWP:0xee\r\n", b"mode:DIO, clock div:1\r\n", b"load:0x3fce3810,len:0x178c\r\n",
               b"entry 0x403c9900\r\n", b"\x00\xff\xfe\x1b\r\n"]

        class Rom(FauxPort):
            def write(self, b):
                first = not self.lines
                FauxPort.write(self, b)
                if first:
                    self.out[0:0] = rom
        p = Rom(cfg=0x21)
        rc, out, _ = run(["--read"], p)
        self.assertEqual(rc, 0, out)
        self.assertEqual(p.lines, ["CUSTOM READ"])
        self.assertIn("M1=MF M2=AF (0x21)", out)
        self.assertEqual(p.out, [])
        p = Rom(cfg=0x10)
        rc, out, _ = run(["--M1", "MF", "--yes"], p)
        self.assertEqual(rc, 0, out)
        self.assertEqual(p.lines, ["CUSTOM READ", "CUSTOM WRITE 20", "CUSTOM READ"])
        self.assertEqual(p.cfg, 0x20)

    def test_endless_journal_bounded(self):
        # Audit R10 : la réponse perdue (`dropped`) et des lignes `*` sans interruption (`LOG ALL` laissé par la page) :
        # readline ne rend jamais b"" sur un silence. La demande s'arrête à son échéance, comptée sur l'horloge qu'on
        # lui donne (ici, 10 ms par lecture de l'horloge, une par ligne lue : 3 s après 300 lignes), et le dit.
        class Flot:
            def __init__(self):
                self.reads = 0
                self.lines = []

            def write(self, b):
                self.lines.append(b.decode("ascii"))

            def readline(self):
                self.reads += 1
                if self.reads > 100000:
                    raise AssertionError("la demande ne s'arrête pas")
                return b"* rx 12345 F0 1B 00 02\n"

        class Horloge:
            def __init__(self):
                self.t = 1000.0

            def __call__(self):
                self.t += 0.01            # l'horloge avance de 10 ms à chaque lecture
                return self.t
        p = Flot()
        carte = tool_station.Carte(p, horloge=Horloge())
        with self.assertRaises(tool_station.Erreur) as e:
            carte.demande("CUSTOM READ")
        self.assertIn("pas de réponse", str(e.exception))
        self.assertEqual(p.lines, ["CUSTOM READ\n"])
        self.assertLessEqual(p.reads, 301)
        self.assertGreaterEqual(p.reads, 299)

    def test_man(self):
        p = FauxPort()
        rc, out, _ = run(["--man"], p)
        self.assertEqual(rc, 0)
        self.assertEqual(p.lines, [])
        for needle in ("0x40 'P' 0xFA", "0x40 'P' 0x38", "0x30 + configuration", "(M1 << 4) | M2", "0 = APERTURE (AP), 1 = AF, 2 = MF",
                       "RAM", "CUSTOM READ", "CUSTOM WRITE", "0x10 : M1 = AF, M2 = APERTURE", "--M1 AF --M2 AP", "1.06",
                       "AUX RISQUES DE L'UTILISATEUR", "LensType2 8", "boot_state=ready", "DÉBRANCHER PUIS REBRANCHER", "pyserial"):
            self.assertIn(needle, out, needle)

    def test_help_is_the_manual(self):
        out = io.StringIO()
        old, sys.stdout = sys.stdout, out
        try:
            with self.assertRaises(SystemExit) as e:
                tool_station.main(["--help"], port=FauxPort())
        finally:
            sys.stdout = old
        self.assertEqual(e.exception.code, 0)
        self.assertIn("AUX RISQUES DE L'UTILISATEUR", out.getvalue())

    def test_no_pyserial_needed(self):
        with self.assertRaises(ImportError):
            tool_station.ouvrir("/dev/null")


if __name__ == "__main__":
    unittest.main(verbosity=1)
