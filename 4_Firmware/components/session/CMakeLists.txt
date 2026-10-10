# SOURCE: spec de l'atelier § 3.1 — SESSION
# AUTHOR: engineer
# DATE: 2026-10-03
# STATUS: actif — C pur, compilé aussi par les tests hôte
#
# Le superviseur de la session (session.c) et ses parties.
idf_component_register(
    SRCS "session.c" "lens_rx.c" "still.c" "motion.c" "drive.c" "ring.c" "mark.c" "cmd.c" "init.c" "restore.c"
    INCLUDE_DIRS "../../include"
    PRIV_REQUIRES txn phy journal store)
