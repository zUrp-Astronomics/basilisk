#!/usr/bin/env bash
# SOURCE: outillage des tests hôte en CI
# AUTHOR: engineer
# DATE: 2026-09-23
# STATUS: actif — joué par le job `test` de .gitea/workflows/ci.yml ET par la sonde
#         .gitea/workflows/probe-test-relevance.yml (une seule définition : les deux ne dérivent pas)
#
# Script de CI : il tourne en root dans l'image catthehacker/ubuntu:act-latest (apt, écriture sous
# /opt). Pas destiné à un poste de dev — là, `npm install` à la racine suffit, avec CHROMIUM_PATH ou
# un Chromium sous /opt/pw-browsers (voir ## Test du CLAUDE.md).
#
# ⚠ TOUT S'INSTALLE HORS DE $HOME, ET C'EST LE POINT. La sonde rejoue la suite sous `env -i` avec un
# HOME VIERGE : un Chromium laissé dans le cache par défaut de Playwright (~/.cache/ms-playwright)
# y serait introuvable. D'où :
#   - le module `playwright` dans node_modules/ à la racine du dépôt (résolu par require() depuis
#     le test de la page dans Chromium, sans NODE_PATH, où qu'il soit sous la racine) ;
#   - Chromium sous /opt/pw-browsers, le chemin que ce test lit quand CHROMIUM_PATH est
#     absent (et env -i l'enlève).
set -euo pipefail
RACINE=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
export DEBIAN_FRONTEND=noninteractive

if ! command -v gcc >/dev/null; then
    apt-get update -qq && apt-get install -y -qq gcc libc6-dev >/dev/null
fi
for outil in gcc python3 node npm; do
    command -v "$outil" >/dev/null || { echo "outil absent de l'image : $outil" >&2; exit 1; }
done
echo "node $(node --version), npm $(npm --version), $(gcc --version | head -n 1), $(python3 --version)"

cd "$RACINE"
npm install --no-audit --no-fund --no-package-lock
PLAYWRIGHT_BROWSERS_PATH=/opt/pw-browsers npx --no-install playwright install --with-deps chromium

# Le test de la page prend le premier /opt/pw-browsers/chromium-*/chrome-linux/chrome. La disposition
# `chrome-linux` est celle de Playwright 1.49.1 (épinglé dans package.json) : si une montée de
# version la change, c'est ici que ça doit casser, pas dans le test.
ls /opt/pw-browsers/chromium-*/chrome-linux/chrome
