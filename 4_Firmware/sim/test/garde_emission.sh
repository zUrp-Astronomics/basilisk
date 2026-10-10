#!/usr/bin/env bash
# SOURCE: spec de l'atelier § 2 et § 7 — aucune émission ne contourne bench_core
# AUTHOR: engineer
# DATE: 2026-09-27
# STATUS: actif — joué par 4_Firmware/run.sh, sur le dépôt puis sur une copie où un appel interdit est posé
#
# garde_emission.sh <racine firmware> : échoue si un fichier du firmware (.c, .h hors de sim/) nomme
# bsk_phy_send ailleurs que là où il est déclaré (include/bsk_phy.h), défini (components/phy/phy.c)
# et appelé (components/bench_core/bench_core.c). sim/ est le banc hôte : il joue la carte sans son
# firmware, et n'est jamais compilé pour elle.
set -euo pipefail
r=$1
hors=$(cd "$r" && grep -rlw --include='*.c' --include='*.h' bsk_phy_send . |
    grep -v -e '^\./sim/' -e '^\./include/bsk_phy\.h$' -e '^\./components/phy/phy\.c$' \
            -e '^\./components/bench_core/bench_core\.c$' || true)
if [ -n "$hors" ]; then
    echo "   émission hors de bench_core : $hors"
    exit 1
fi
