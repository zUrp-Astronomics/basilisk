/* SOURCE: spec de l'atelier § 2 et § 7.1.3 — bench_core
 * AUTHOR: engineer
 * DATE: 2026-10-03
 * STATUS: actif — déclarations seulement ; implémenté par components/bench_core/bench_core.c
 *
 * bench_core : le filtre de fil, dernier avant PHY, seul appelant de son émission (sim/test/garde_emission.sh). Il est
 * sous le protocole pour qu'aucun pilote, aucun modèle d'objectif, aucune évolution ne puisse le contourner. */
#ifndef BSK_BENCH_CORE_H
#define BSK_BENCH_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "bsk_err.h"
#include "bsk_phy.h"

/* Liste blanche, inconditionnelle, non configurable à l'exécution :
 *   - types 0x01 0x03 0x04 0x07 0x08 0x09 0x0A 0x0B 0x0D 0x10 0x1C 0x1D 0x3F, pour tout objectif,
 *     avant comme après l'identification ;
 *   - type 0x40, sous-commandes 'V' 00, 'F' FA / FB / 0x32, 'M' 00 / 31, seulement si l'appelant
 *     déclare le Samyang reconnu ;
 *   - exception voulue par l'humain à « jamais d'écriture dans la flash d'un objectif » : type 0x40, 'P' 0x38
 *     (l'écriture de la configuration du commutateur Custom, octet de données 0x30 + (haut << 4 | bas), chaque position
 *     0, 1 ou 2) et 'P' 0xFA (sa lecture, octet de données différent de 0x53), seulement si l'appelant déclare le
 *     Samyang AF 135 (BSK_BENCH_SAMYANG135). Chaque sous-commande, en termes de protocole : bench_core.c, svc_allowed et
 *     svc135_allowed ; la preuve : 7_Docs/E-Mount/samyang.md § 6.3 et § 7.2 (la carte des écritures).
 * Tout le reste est refusé : 0x0C, 0x14, 0x15, 0x16, tout type inconnu, toute autre sous-commande,
 * et toute trame plus longue que BSK_MSG_MAX (ce n'est pas une trame, bsk_phy.h). */

/* Ce que l'appelant déclare de l'objectif (seul le canal 0x40 en dépend). SAMYANG135 : le Samyang AF 135, reconnu par
 * son LensType2 (8). Une valeur hors de cette énumération ferme le canal 0x40, comme OTHER. */
typedef enum {
    BSK_BENCH_OTHER = 0,       /* aucun 0x40 */
    BSK_BENCH_SAMYANG = 1,     /* la liste du canal 0x40 */
    BSK_BENCH_SAMYANG135 = 2,  /* la liste, et l'exception du commutateur Custom */
} bsk_bench_lens_t;

/* Émet `f` si chacun de ses sous-messages est dans la liste, pour l'objectif que l'appelant déclare (`lens`). Rend l'issue
 * de l'émission par PHY (E_OK, E_BUS), ou E_FORBIDDEN : rien n'est émis, et le compteur de refus augmente. */
bsk_err_t bsk_bench_send(const bsk_frame_t *f, bsk_bench_lens_t lens);

/* Refus depuis le démarrage. Le refus n'est jamais silencieux : il est rendu à l'appelant et compté ici. */
uint32_t bsk_bench_refused(void);

#endif
