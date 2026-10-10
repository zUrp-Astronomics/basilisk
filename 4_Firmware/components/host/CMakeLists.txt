# SOURCE: spec de l'atelier § 4.5 — la couche HOTE
# AUTHOR: engineer
# DATE: 2026-10-03
# STATUS: actif — host.c et lens_names.c sont du C pur, compilés aussi par les tests hôte ; host_usb.c est la carte seule
#
# La traduction de PROTOCOL.md, la table des noms d'objectif et l'USB de la carte.
idf_component_register(
    SRCS "host.c" "lens_names.c" "host_usb.c"
    INCLUDE_DIRS "../../include"
    PRIV_REQUIRES session journal led bench_core driver esp_timer esp_app_format)

# Avertissements de la cible, fatals sur nos sources (ticket #539, audit M9 ; 7_Docs/DOCTRINE.md) : ESP-IDF pose
# -Wall -Werror=all -Wextra, pas -Wconversion ni -Wshadow, et ses -Wextra ne sont pas fatals. Les en-têtes d'ESP-IDF
# qui les déclenchent en sont écartés à l'include, nommément (7_Docs/DOCTRINE.md dit lesquels).
target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Wconversion -Wshadow -Werror=extra -Werror=conversion -Werror=shadow)
