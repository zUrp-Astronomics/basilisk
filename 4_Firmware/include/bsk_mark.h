/* SOURCE: spec de l'atelier § 9 et § 9.1 — le format de la marque
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — en-tête partagé, déclarations seulement, aucune logique
 *
 * La marque : le seul état persistant que la carte possède en propre. Écrite seulement sur un geste de l'utilisateur
 * (bouton du fût, `js`), jamais automatiquement ; lue en RESTORING ; invalidée par une identité différente ou un
 * ordre explicite de l'hôte. */
#ifndef BSK_MARK_H
#define BSK_MARK_H

#include <stdint.h>

/* Sens du dernier mouvement avant la pose. 0 vaut « inconnu » : une marque remise à zéro ne prétend aucun sens. Toute
 * valeur hors de cette énumération est « invalide » ; inconnu et invalide mènent au même défaut, l'arrivée en
 * décroissant, celle du Samyang AF 135 vers sa propre marque (7_Docs/E-Mount/samyang.md § 5.6). */
typedef enum {
    BSK_APPROACH_UNKNOWN    = 0,
    BSK_APPROACH_INCREASING = 1,
    BSK_APPROACH_DECREASING = 2,
} bsk_approach_t;

/* L'identité de l'objectif et la focale sont dans la clé de stockage, pas dans la valeur. */
typedef struct {
    int32_t  position;           /* position au moment du geste de l'utilisateur (js, bouton), ou l'arg de CMD_SET_MARK */
    uint8_t  approach_dir;       /* bsk_approach_t */
} bsk_mark_t;

#endif
