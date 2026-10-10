# SOURCE: spec de l'atelier § 9 — le magasin de la marque
# AUTHOR: engineer
# DATE: 2026-10-03
# STATUS: actif — la carte seule ; les tests hôte ont leur magasin en mémoire (sim/store_sim.c)
#
# La NVS de la carte, servie par une tâche (include/bsk_store.h).
idf_component_register(
    SRCS "store.c"
    INCLUDE_DIRS "../../include"
    PRIV_REQUIRES nvs_flash)

# Avertissements de la cible, fatals sur nos sources (ticket #539, audit M9 ; 7_Docs/DOCTRINE.md) : ESP-IDF pose
# -Wall -Werror=all -Wextra, pas -Wconversion ni -Wshadow, et ses -Wextra ne sont pas fatals. Les en-têtes d'ESP-IDF
# qui les déclenchent en sont écartés à l'include, nommément (7_Docs/DOCTRINE.md dit lesquels).
target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Wconversion -Wshadow -Werror=extra -Werror=conversion -Werror=shadow)
