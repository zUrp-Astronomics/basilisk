#!/usr/bin/env bash
# SOURCE: le test de l'outil tool_station (test_tool_station.py, python3 seul, sans pyserial)
# AUTHOR: engineer
# DATE: 2026-10-03
# STATUS: actif — joué par le job `test` de la CI ; se lance depuis n'importe quel répertoire
set -euo pipefail
ICI=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
python3 "$ICI/test_tool_station.py"
