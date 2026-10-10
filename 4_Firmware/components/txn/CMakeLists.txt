# SOURCE: spec de l'atelier § 3.3 — TRANSACTION
# AUTHOR: engineer
# DATE: 2026-10-03
# STATUS: actif
#
# Le propriétaire du bus : la boucle 0x03/0x04 et les requêtes.
idf_component_register(
    SRCS "txn.c"
    INCLUDE_DIRS "../../include"
    PRIV_REQUIRES bench_core journal)
