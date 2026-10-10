#!/usr/bin/env python3
# SOURCE: PROTOCOL.md § 3.1 (la commande de labo `CUSTOM`)
# AUTHOR: engineer
# DATE: 2026-10-03
# STATUS: actif — outil de l'utilisateur, hors du firmware ; testé par test_tool_station.py (run.sh, job `test`)
# Lire et écrire la configuration du commutateur Custom du Samyang AF 135, par la carte.
#
# Seule la carte tient le bus E-mount : l'outil ne parle qu'à elle, par sa ligne USB, et ne connaît du protocole de
# l'objectif que ce que sa page de manuel en dit. pyserial (requirements.txt) n'est importé que pour ouvrir un vrai port :
# main() accepte un port déjà ouvert (les tests lui en donnent un faux).
"""Configuration du commutateur Custom du Samyang AF 135, par la carte emount-bench."""

import argparse
import re
import sys
import time

MANUEL = """\
NOM
    tool_station.py - lire et écrire la configuration du commutateur Custom (M1, M2) du Samyang AF 135 F1.8 FE

SYNOPSIS
    python tool_station.py --read [--port PORT]
    python tool_station.py [--M1 AF|MF|AP] [--M2 AF|MF|AP] [--yes] [--port PORT]
    python tool_station.py --man

DESCRIPTION
    Le Samyang AF 135 F1.8 FE a un commutateur à deux positions, M1 (en haut) et M2 (en bas). Ce que fait
    chaque position est une configuration rangée dans la mémoire flash de l'objectif :
        AF  mise au point automatique ;
        MF  mise au point manuelle à la bague ;
        AP  la bague règle l'ouverture (APERTURE).
    L'outil de Samyang (Lens Station) la règle ; celui-ci le fait par la carte emount-bench, branchée en USB,
    l'objectif monté sur elle.

    --read            lit la configuration et l'affiche. Rien n'est écrit.
    --M1, --M2        la position à écrire. Une seule donnée : l'autre est relue dans l'objectif et gardée.
                      Avant d'écrire, l'outil affiche la configuration lue et celle qu'il va écrire, puis
                      demande une confirmation. Après l'écriture, il relit la configuration pour la vérifier.
                      Si elle est déjà celle demandée, rien n'est écrit.
    --yes             écrit sans demander de confirmation.
    --port PORT       le port série de la carte (défaut : /dev/ttyACM0 ; sous Windows, COM3 par exemple).
    --man, --help     cette page.

    Après une lecture ou une écriture : DÉBRANCHER PUIS REBRANCHER L'OBJECTIF (voir EFFETS).

CONDITIONS
    - L'objectif est un Samyang AF 135 F1.8 FE. La carte le reconnaît (Samyang, code LensType2 8) et refuse
      tout autre objectif (réponse « er nocap custom »), d'autres Samyang compris.
    - La carte est prête : la ligne « t » de sa console dit boot_state=ready. Sinon elle répond
      « er nolens » (pas d'objectif), « er busy boot » (démarrage en cours) ou « er fault ».
    - Après l'opération, l'objectif est débranché puis rebranché.

LE RISQUE
    Le format de ces commandes est établi sur le firmware 1.06 de l'objectif (lu dans son code). Sur une autre
    version, il n'est pas vérifié, et ni l'outil ni la carte ne lisent la version de l'objectif : l'opération
    se fait AUX RISQUES DE L'UTILISATEUR. L'écriture modifie la mémoire flash de l'objectif.

ÉTAT D'ORIGINE
    D'usine, la configuration vaut 0x10 : M1 = AF, M2 = APERTURE (AP). Pour y revenir :
        python tool_station.py --M1 AF --M2 AP

LES DEUX COMMANDES (canal de service 0x40 de l'objectif ; message de 19 octets : 0x40, MainCmd, SubCmd,
puis 16 octets de données)
    Chaque position est un quartet : 0 = APERTURE (AP), 1 = AF, 2 = MF. La configuration est un octet :
        configuration = (M1 << 4) | M2          M1 au quartet haut, M2 au quartet bas
    Lecture   : 0x40 'P' 0xFA, premier octet de données 0x00. L'objectif répond 0x40 'P' 0xFA et 16 octets de
                données ; la configuration est le 8e. Ses effets restent en RAM, sans rien écrire en flash :
                l'objectif passe en mode « JIG » (des drapeaux du commutateur posés, le drapeau « MTF » remis
                à 0) et les oublie à sa mise sous tension. Avec 0x53 pour premier octet de données (le mode
                « MTF »), la lecture changerait d'autres réponses de l'objectif : la carte la refuse.
    Écriture  : 0x40 'P' 0x38, premier octet de données 0x30 + configuration (M1 = MF, M2 = AF : 0x30 + 0x21
                = 0x51). L'objectif range la configuration en flash, et la prend aussi en RAM. Il répond l'écho,
                16 octets de données à zéro, même s'il a refusé la valeur : seule une relecture dit ce qui est
                rangé. Une position au-delà de 2 n'est jamais émise : la carte la refuse.
    La carte n'émet ces deux messages, et aucune autre commande de ce genre, qu'au Samyang AF 135.

PROCÉDURE MANUELLE (sans cet outil, à la console série de la carte)
    Ouvrir le port série de la carte (par exemple : screen /dev/ttyACM0 115200 ; la vitesse est sans effet,
    c'est un port USB). Chaque ligne tapée est une commande ; la carte répond une ligne.
    1. t                     doit contenir boot_state=ready.
    2. CUSTOM READ           répond « ok » puis 16 octets en hexadécimal ; le 8e est la configuration :
                             ok 00 00 00 00 00 00 00 10 00 00 00 00 00 00 00 00   -> 10 : M1 = AF, M2 = AP
    3. CUSTOM WRITE <h><b>   h = M1, b = M2, un chiffre chacun (0 AP, 1 AF, 2 MF) ;
                             CUSTOM WRITE 12 : M1 = AF, M2 = MF. Répond « ok » et 16 octets à zéro.
    4. CUSTOM READ           vérifie : le 8e octet doit valoir 12.
    5. Débrancher puis rebrancher l'objectif.
    Réponses d'erreur : « er nocap custom » (pas un Samyang AF 135), « er nolens », « er busy boot »,
    « er fault » (carte pas prête), « er busy » (une commande en cours), « er link timeout » (l'objectif n'a pas
    répondu en 1 s), « er link framing » (une autre réponse est arrivée : recommencer), « er link aborted »
    (l'objectif a été retiré), « er range … » (ligne mal écrite).

EFFETS
    L'écriture change la configuration en flash : elle survit à la mise hors tension. La lecture laisse des
    drapeaux en RAM. Débrancher puis rebrancher l'objectif le fait repartir de sa flash, sans eux, et la carte
    refait son initialisation avec lui.

VÉRIFIER M1 ET M2 AU PREMIER ESSAI
    « M1 = quartet haut » est lu dans le code de l'objectif (sa position « UP ») et dans les traces de la carte,
    pas encore vérifié sur un objectif. Au premier essai : écrire par exemple --M1 MF --M2 AP, rebrancher,
    mettre le commutateur sur M1 (en haut) et tourner la bague : elle doit faire la mise au point. Sur M2, elle
    doit régler l'ouverture. Si c'est l'inverse, M1 et M2 sont échangés : le signaler, et revenir à l'état
    d'origine (--M1 AF --M2 AP).

SORTIE
    0 : fait (ou rien à écrire) ; 1 : erreur, ou écriture non confirmée ; 2 : ligne de commande invalide.

DÉPENDANCE
    pyserial (pip install -r requirements.txt), pour ouvrir le port série.
"""

POSITIONS = {"AP": 0, "AF": 1, "MF": 2}
NOMS = {0: "AP", 1: "AF", 2: "MF"}
REBRANCHER = "Débrancher puis rebrancher l'objectif."


class Erreur(Exception):
    """Une réponse de la carte qui arrête l'outil."""


# La forme d'une réponse à `CUSTOM` (PROTOCOL.md § 3.1 ; une majuscule rend `ok …` ou `er …`, § 3), la seule commande
# de l'outil. Une autre ligne n'est jamais prise pour la réponse : une ligne du journal (`* …`, sous `LOG ON`), ou une ligne
# de la ROM de l'ESP32-S3 que l'ouverture du port, qui peut redémarrer la carte, fait arriver après l'envoi (audit R5).
REPONSE = re.compile(r"(ok|er)( |$)")

# Le temps qu'une demande attend sa réponse, lignes sautées comprises (audit R10 : une carte qui émet des lignes `*` sans
# interruption ne laissait jamais `readline` rendre la main sur un silence). La carte répond à `CUSTOM` en 1 s au plus
# (`er link timeout`, PROTOCOL.md § 3.1) ; 3 s, l'échéance de `readline` du port (ouvrir()) et celle de la page de banc.
ECHEANCE = 3.0


class Carte:
    """La ligne USB de la carte : une ligne envoyée, une ligne de réponse (PROTOCOL.md § 1)."""

    def __init__(self, port, horloge=time.monotonic):
        self.port = port
        self.horloge = horloge

    def demande(self, ligne):
        vider = getattr(self.port, "reset_input_buffer", None)
        if vider:
            vider()
        self.port.write((ligne + "\n").encode("ascii"))
        fin = self.horloge() + ECHEANCE
        while self.horloge() < fin:
            r = self.port.readline()
            if not r:
                break
            r = r.decode("ascii", "replace").strip()
            if REPONSE.match(r):
                return r
        raise Erreur("« %s » : pas de réponse de la carte" % ligne)


def donnees(ligne, reponse):
    """Les 16 octets de « ok XX … XX » (PROTOCOL.md § 3.1)."""
    mots = reponse.split()
    if len(mots) != 17 or mots[0] != "ok":
        raise Erreur("« %s » : %s" % (ligne, reponse))
    try:
        return [int(m, 16) for m in mots[1:]]
    except ValueError:
        raise Erreur("« %s » : %s" % (ligne, reponse))


def lire(carte):
    return donnees("CUSTOM READ", carte.demande("CUSTOM READ"))[7]


def ecrire(carte, cfg):
    ligne = "CUSTOM WRITE %d%d" % (cfg >> 4, cfg & 0x0F)
    donnees(ligne, carte.demande(ligne))


def texte(cfg):
    def pos(q):
        return NOMS.get(q, "?%d" % q)
    return "M1=%s M2=%s (0x%02X)" % (pos(cfg >> 4), pos(cfg & 0x0F), cfg)


def position(v):
    v = v.upper()
    if v not in POSITIONS:
        raise argparse.ArgumentTypeError("AF, MF ou AP, pas « %s »" % v)
    return v


def analyse(argv):
    p = argparse.ArgumentParser(prog="tool_station.py", description=MANUEL,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--M1", type=position, metavar="AF|MF|AP")
    p.add_argument("--M2", type=position, metavar="AF|MF|AP")
    p.add_argument("--read", action="store_true")
    p.add_argument("--yes", action="store_true")
    p.add_argument("--port", default="/dev/ttyACM0")
    p.add_argument("--man", action="store_true")
    a = p.parse_args(argv)
    if not a.man and a.read == bool(a.M1 or a.M2):
        p.error("--read, ou --M1 et/ou --M2 (pas les deux) ; --man pour la page de manuel")
    return a


def ouvrir(nom):
    import serial   # pyserial, seulement pour un vrai port
    return serial.Serial(nom, 115200, timeout=3)


def main(argv=None, port=None, demander=input, sortie=sys.stdout):
    a = analyse(sys.argv[1:] if argv is None else argv)
    if a.man:
        sortie.write(MANUEL)
        return 0
    carte = Carte(port if port is not None else ouvrir(a.port))
    lu = None
    try:
        lu = lire(carte)
        sortie.write("configuration lue      : %s\n" % texte(lu))
        if a.read:
            sortie.write(REBRANCHER + "\n")
            return 0
        m1 = POSITIONS[a.M1] if a.M1 else lu >> 4
        m2 = POSITIONS[a.M2] if a.M2 else lu & 0x0F
        if m1 > 2 or m2 > 2:
            raise Erreur("la position gardée n'est pas lisible (%s) : donner --M1 et --M2" % texte(lu))
        cfg = m1 << 4 | m2
        if cfg == lu:
            sortie.write("déjà configuré : rien à écrire.\n" + REBRANCHER + "\n")
            return 0
        sortie.write("configuration à écrire : %s\n" % texte(cfg))
        if not a.yes and demander("Écrire dans la flash de l'objectif ? [o/N] ").strip().lower() not in ("o", "oui", "y", "yes"):
            sortie.write("abandonné : rien n'est écrit.\n" + REBRANCHER + "\n")
            return 1
        ecrire(carte, cfg)
        relu = lire(carte)
        sortie.write("configuration relue    : %s\n" % texte(relu))
        if relu != cfg:
            raise Erreur("la configuration relue n'est pas celle écrite")
        sortie.write(REBRANCHER + "\n")
        return 0
    except Erreur as e:
        sortie.write("erreur : %s\n" % e)
        if lu is not None:              # l'objectif a reçu au moins la lecture
            sortie.write(REBRANCHER + "\n")
        return 1


if __name__ == "__main__":
    sys.exit(main())
