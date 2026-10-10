# SOURCE: spec de l'atelier § 4.5.5 — le journal
# AUTHOR: engineer
# DATE: 2026-10-03
# STATUS: actif
#
# fr_encode vient de components/phy/phy_common.h, en-tête privé de phy : d'où "../phy".
idf_component_register(
    SRCS "journal.c"
    INCLUDE_DIRS "../../include"
    PRIV_INCLUDE_DIRS "../phy"
    PRIV_REQUIRES phy)
