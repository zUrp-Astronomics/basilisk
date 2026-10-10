/* SOURCE: include/bsk_host.h (bsk_host_stacks) — ticket #528
 * AUTHOR: engineer
 * DATE: 2026-10-08
 * STATUS: actif — compilé par les programmes de la suite qui compilent host.c, hors test_host (sim/test/programmes.sh)
 *
 * Les marges de pile de `DEBUG` : sur l'hôte, aucune tâche, aucune pile mesurée ; toutes à zéro. test_host pose les
 * siennes pour vérifier leur place dans la ligne. */
#include "bsk_host.h"

void bsk_host_stacks(bsk_host_stacks_t *s) { *s = (bsk_host_stacks_t){0}; }
