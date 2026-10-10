# SOURCE: PROTOCOL.md § 2 (`k`) — la LED de statut
# AUTHOR: engineer
# DATE: 2026-10-03
# STATUS: actif — led.c est du C pur, compilé aussi par les tests hôte ; led_board.c est la carte seule
#
# La broche de la cible est dans components/phy/pins.h, en-tête privé de phy : d'où "../phy".
idf_component_register(
    SRCS "led.c" "led_board.c"
    INCLUDE_DIRS "../../include"
    PRIV_INCLUDE_DIRS "../phy"
    PRIV_REQUIRES session driver esp_timer)

# Avertissements de la cible, fatals sur nos sources (ticket #539, audit M9 ; 7_Docs/DOCTRINE.md) : ESP-IDF pose
# -Wall -Werror=all -Wextra, pas -Wconversion ni -Wshadow, et ses -Wextra ne sont pas fatals. Les en-têtes d'ESP-IDF
# qui les déclenchent en sont écartés à l'include, nommément (7_Docs/DOCTRINE.md dit lesquels).
target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Wconversion -Wshadow -Werror=extra -Werror=conversion -Werror=shadow)
