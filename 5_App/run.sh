#!/usr/bin/env bash
# SOURCE: les tests de la page de banc (emount-bench.html), sans carte
# AUTHOR: engineer
# DATE: 2026-10-03
# STATUS: actif — la première commande de `## Test` (CLAUDE.md), jouée telle quelle par le job `test` de la CI et par la
#         sonde de pertinence ; test/test_app_vm.cjs (le transport de la page dans une VM Node, sur des trames réelles
#         de l'objectif) et test/test_app.js (la page dans Chromium, contre un faux port Web Serial)
#
# Joue TOUS les tests, même après un échec (pour voir d'un coup tout ce qui casse), puis sort en 1
# si l'un d'eux a échoué. Prérequis : node ; pour test_app.js, le module `playwright`
# (npm install à la racine) et un Chromium sous /opt/pw-browsers/chromium-*/ ou CHROMIUM_PATH.
# Se lance depuis n'importe quel répertoire : les chemins partent de celui de ce script.
set -uo pipefail
ICI=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# Les traces de l'objectif, lues en place par test_app_vm.cjs : elles appartiennent au firmware.
TRACES="$ICI/../4_Firmware/traces"

rouges=()
joue() {
    echo "===== $* ====="
    if "$@"; then echo "----- vert : $*"; else echo "----- ROUGE (sortie $?) : $*"; rouges+=("$*"); fi
}

joue node "$ICI/test/test_app_vm.cjs" "$TRACES"
joue node "$ICI/test/test_app.js"

if [ "${#rouges[@]}" -ne 0 ]; then
    echo "===== ${#rouges[@]} test(s) rouge(s) :"
    printf '  %s\n' "${rouges[@]}"
    exit 1
fi
echo "===== page de banc : tout est vert"
