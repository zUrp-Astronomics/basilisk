# SOURCE: spec de l'atelier § 7 — bench_core
# AUTHOR: engineer
# DATE: 2026-10-03
# STATUS: actif
#
# Le filtre de fil, dernier avant la PHY.
idf_component_register(
    SRCS "bench_core.c"
    INCLUDE_DIRS "../../include"
    PRIV_REQUIRES phy)
