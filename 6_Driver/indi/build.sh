#!/usr/bin/env bash
# SOURCE: 6_Driver/indi/README.md (cmake puis make) — le driver INDI compilé et lié
# AUTHOR: engineer
# DATE: 2026-10-03
# STATUS: actif — joué par le job `indi` de la CI ; se lance depuis n'importe quel répertoire
# Compile et lie le driver par son CMakeLists.txt, tel quel, dans build/ à côté de ce script (recréé à chaque fois).
# Prérequis : cmake, make, g++, pkg-config, libindi-dev. Rien n'y est testé.
set -euo pipefail
ICI=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
build="$ICI/build"
rm -rf "$build"
cmake -S "$ICI" -B "$build"
make -C "$build"
