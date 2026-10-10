#!/usr/bin/env bash
# SOURCE: build ESP-IDF 5.3.2 de 4_Firmware/, cible esp32s3
# AUTHOR: engineer
# DATE: 2026-09-27
# STATUS: actif — joué tel quel par le job `firmware-build` de .gitea/workflows/ci.yml ;
#         ticket #528 : la panique du chien de garde et l'ISR de l'UART en IRAM vérifiées sur le build
#
# Usage : bash 4_Firmware/build.sh      (ESP-IDF 5.3.2 chargé : . $IDF_PATH/export.sh)
#
# Le sdkconfig est généré dans 4_Firmware/build (-DSDKCONFIG), jamais dans 4_Firmware/ : rien de
# versionné n'est réécrit par un build (le piège du sdkconfig de la v1, CLAUDE.md § Gotchas). Puis la
# console muette est vérifiée sur ce qui a réellement été compilé : le niveau de journal par défaut,
# le niveau maximal (celui qui compile ou retire chaque ESP_LOG) et celui du bootloader valent 0
# (NONE), dans le sdkconfig et le sdkconfig.h du build. De même, ce qu'aucun test hôte ne joue et que la carte seule
# prouvera (ticket #528) : le chien de garde initialisé au démarrage et qui redémarre la carte (INIT, PANIC), l'ISR de
# l'UART en IRAM (sdkconfig.defaults dit pourquoi).
set -euo pipefail
ICI=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
command -v idf.py >/dev/null || { echo "idf.py introuvable : charger ESP-IDF (. \$IDF_PATH/export.sh)" >&2; exit 2; }
build="$ICI/build"
rm -rf "$build"
idf.py -C "$ICI" -B "$build" -DIDF_TARGET=esp32s3 -DSDKCONFIG="$build/sdkconfig" build

muet=0
for o in CONFIG_LOG_DEFAULT_LEVEL CONFIG_LOG_MAXIMUM_LEVEL CONFIG_BOOTLOADER_LOG_LEVEL; do
    if ! grep -qx "$o=0" "$build/sdkconfig" || ! grep -qx "#define $o 0" "$build/config/sdkconfig.h"; then
        echo "console non muette : $o ne vaut pas 0 dans le sdkconfig ou le sdkconfig.h du build" >&2
        muet=1
    fi
done
[ "$muet" -eq 0 ]
echo "console muette : journal (défaut et maximum) et bootloader à 0 dans le sdkconfig et le sdkconfig.h du build"

sur=0
for o in CONFIG_ESP_TASK_WDT_INIT CONFIG_ESP_TASK_WDT_PANIC CONFIG_UART_ISR_IN_IRAM; do
    if ! grep -qx "$o=y" "$build/sdkconfig" || ! grep -qx "#define $o 1" "$build/config/sdkconfig.h"; then
        echo "sûreté : $o n'est pas posée dans le sdkconfig ou le sdkconfig.h du build" >&2
        sur=1
    fi
done
[ "$sur" -eq 0 ]
echo "sûreté : chien de garde initialisé et en panique, ISR de l'UART en IRAM, dans le sdkconfig et le sdkconfig.h du build"
