# SOURCE: le projet ESP-IDF 5.3.2 de 4_Firmware/, cible esp32s3 (spec § 12, § 13)
# AUTHOR: engineer
# DATE: 2026-09-27
# STATUS: actif — construit par 4_Firmware/build.sh (job `firmware-build` de .gitea/workflows/ci.yml) ; PROJECT_VER
#         rendu tel quel par `v`
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_VER "1.0")   # la seule source de `v` (<maj>.<min>, sans suffixe : décision de l'humain)
project(emount_bench2)
