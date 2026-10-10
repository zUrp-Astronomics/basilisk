# SOURCE: composant main — app_main
# AUTHOR: engineer
# DATE: 2026-10-03
# STATUS: actif
idf_component_register(
    SRCS "main.c"
    PRIV_REQUIRES phy bench_core txn session host journal store led esp_timer freertos)

# Avertissements de la cible, fatals sur nos sources (ticket #539, audit M9 ; 7_Docs/DOCTRINE.md) : ESP-IDF pose
# -Wall -Werror=all -Wextra, pas -Wconversion ni -Wshadow, et ses -Wextra ne sont pas fatals. Les en-têtes d'ESP-IDF
# qui les déclenchent en sont écartés à l'include, nommément (7_Docs/DOCTRINE.md dit lesquels).
target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Wconversion -Wshadow -Werror=extra -Werror=conversion -Werror=shadow)
