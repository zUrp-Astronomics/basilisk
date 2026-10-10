# SOURCE: spec de l'atelier § 2 — la PHY
# AUTHOR: engineer
# DATE: 2026-10-03
# STATUS: actif
#
# La PHY de la carte (phy.c) et le code qu'elle partage avec la PHY simulée (phy_common.c).
idf_component_register(
    SRCS "phy.c" "phy_common.c"
    INCLUDE_DIRS "../../include"
    PRIV_INCLUDE_DIRS "."
    PRIV_REQUIRES driver esp_timer)

# Avertissements de la cible, fatals sur nos sources (ticket #539, audit M9 ; 7_Docs/DOCTRINE.md) : ESP-IDF pose
# -Wall -Werror=all -Wextra, pas -Wconversion ni -Wshadow, et ses -Wextra ne sont pas fatals. Les en-têtes d'ESP-IDF
# qui les déclenchent en sont écartés à l'include, nommément (7_Docs/DOCTRINE.md dit lesquels).
target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Wconversion -Wshadow -Werror=extra -Werror=conversion -Werror=shadow)
