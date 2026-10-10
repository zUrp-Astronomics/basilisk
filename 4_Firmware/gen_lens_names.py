#!/usr/bin/env python3
# SOURCE: ExifTool Sony.pm (%sonyLensTypes2) — la table des noms d'objectifs par code LensType2
# AUTHOR: engineer
# DATE: 2026-10-03
# STATUS: actif — outil de développement, joué à la main ; se lance depuis n'importe quel répertoire
"""Genere la table des noms d'objectifs E-mount a partir de ExifTool (Sony.pm, %sonyLensTypes2).

Le code est le tag `LensType2` du maker note Sony : l'objectif l'annonce dans sa reponse 0x07
(offsets 9-10, l'offset 0 suivant l'octet de type, petit-boutiste), le boitier le recopie dans
l'EXIF. ExifTool tient la table a jour (Sony, Sigma, Tamron, Samyang, Viltrox...). Licence ExifTool : Perl (Artistic/GPL).

Usage : python3 4_Firmware/gen_lens_names.py [chemin/Sony.pm]
        (sans argument : telecharge la version courante depuis GitHub)
Ecrit : 4_Firmware/components/host/lens_names.c  et  la table LENS_NAMES de 5_App/emount-bench.html.
"""
import re, sys, os, urllib.request, collections

URL = "https://raw.githubusercontent.com/exiftool/exiftool/master/lib/Image/ExifTool/Sony.pm"
ICI = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(ICI)
C_PATH = os.path.join(ROOT, "4_Firmware", "components", "host", "lens_names.c")
HTML_PATH = os.path.join(ROOT, "5_App", "emount-bench.html")

if len(sys.argv) > 1:
    src = open(sys.argv[1], encoding="utf-8", errors="replace").read()
else:
    src = urllib.request.urlopen(URL, timeout=60).read().decode("utf-8", "replace")

ver = re.search(r"\$VERSION\s*=\s*'([^']+)'", src)
ver = ver.group(1) if ver else "?"
i = src.find("%sonyLensTypes2 = (")
j = src.find("\n);", i)
assert i > 0 and j > i, "table %sonyLensTypes2 introuvable"
body = src[i:j]
table = {}
for code, name in re.findall(r"^\s*(\d+)\s*=>\s*'([^']*)'", body, re.M):
    c = int(code)
    if c == 0 or c > 0xFFFF:
        continue
    name = name.replace("\\'", "'")
    table[c] = name  # les cles « n.1 » (variantes) sont ignorees : l'entree principale les cite deja
codes = sorted(table)
print(f"{len(codes)} objectifs (ExifTool Sony.pm {ver})")

# Marques par bloc (LENS_BRANDS de la page seule ; le firmware n'en a pas) : l'octet haut du code est
# attribue par constructeur (0x80-0x81 Sony, 0xC0 Zeiss, 0xC1 Tamron, 0xC2 Tokina, 0xC5 Sigma,
# 0xC7 Voigtlander, 0xC9 Samyang, 0xF1 Viltrox...). Un bloc dont >= 80 % des entrees portent la meme
# marque donne cette marque a tout code inconnu du bloc. Le bloc 0x00 (adaptateurs, index Samyang
# recents) est trop mele : exclu.
blocks = collections.defaultdict(collections.Counter)
for c in codes:
    if c < 256:
        continue
    brand = table[c].split()[0]
    if brand.upper() == "ZEISS":
        brand = "Zeiss"
    blocks[c >> 8][brand] += 1
brand_by_block = {}
for hb, cnt in sorted(blocks.items()):
    brand, n = cnt.most_common(1)[0]
    if n >= 0.8 * sum(cnt.values()) and sum(cnt.values()) >= 2:
        brand_by_block[hb] = brand
print("marques par bloc :", {f"{k:#04x}": v for k, v in brand_by_block.items()})

def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'

# L'en-tete et les fonctions sont ceux de 4_Firmware/components/host/lens_names.c : seule la table change.
with open(C_PATH, "w", encoding="utf-8") as f:
    f.write("/* SOURCE: spec de l'atelier § 4.5.2 — la table de repli des noms de `i` et de `id`\n"
            " * AUTHOR: engineer\n"
            " * DATE: 2026-10-03\n"
            " * STATUS: actif — C pur, sans ESP-IDF : compilé aussi par les tests hôte\n"
            " *\n"
            " * Noms d'objectifs E-mount par code LensType2. Table GÉNÉRÉE par 4_Firmware/gen_lens_names.py, ne pas l'éditer\n"
            f" * à la main. Source : ExifTool Sony.pm (%sonyLensTypes2, version {ver}), licence Perl\n"
            " * (Artistic/GPL), https://exiftool.org. Codes triés : la recherche est dichotomique. */\n"
            "#include \"lens_names.h\"\n\n"
            "static const struct { uint16_t code; const char *name; } T[] = {\n")
    for c in codes:
        f.write(f"    {{{c}, {c_str(table[c])}}},\n")
    f.write("};\n\n"
            "const char *lens_name_lookup(uint16_t code)\n"
            "{\n"
            "    int lo = 0, hi = (int)(sizeof T / sizeof T[0]) - 1;\n"
            "    while (lo <= hi) {\n"
            "        int mid = (lo + hi) / 2;\n"
            "        if (T[mid].code == code) return T[mid].name;\n"
            "        if (T[mid].code < code) lo = mid + 1; else hi = mid - 1;\n"
            "    }\n"
            "    return NULL;\n"
            "}\n")

html = open(HTML_PATH, encoding="utf-8").read()
js = ("const LENS_NAMES={" + ",".join(f"{c}:{c_str(table[c])}" for c in codes) + "};"
      + "const LENS_BRANDS={" + ",".join(f"{hb}:{c_str(b)}" for hb, b in sorted(brand_by_block.items())) + "};")
begin, end = "/*LENS_NAMES_BEGIN*/", "/*LENS_NAMES_END*/"
a, b = html.find(begin), html.find(end)
assert a > 0 and b > a, "marqueurs LENS_NAMES absents de l'app"
html = html[:a + len(begin)] + js + html[b:]
open(HTML_PATH, "w", encoding="utf-8").write(html)
print("4_Firmware/components/host/lens_names.c et 5_App/emount-bench.html mis a jour")
