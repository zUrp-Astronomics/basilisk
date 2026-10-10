/* SOURCE: spec de l'atelier § 3.1 et § 9 — RESTORING, le retour à la marque
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — en-tête privé de la SESSION, inclus par session.c et restore.c seuls
 *
 * À la fin d'un démarrage, aller à la marque ou non, et par quel trajet ; en RESTORING, le goto suivant et la cause de la
 * sortie. Les gotos sont ceux de MOUVEMENT (motion_restore). Le superviseur garde les états : il attend la lecture de la
 * marque, entre en READY ou en RESTORING selon restore_plan, et passe à READY quand restore_track le dit ou sur q. */
#ifndef BSK_SESSION_RESTORE_H
#define BSK_SESSION_RESTORE_H

#include <stdbool.h>
#include <stdint.h>

/* Ce que le superviseur fait en RESTORING, à chaque pas. */
typedef enum {
    RESTORE_WAIT,      /* le goto en cours n'est pas fini : rien */
    RESTORE_NEXT,      /* le dépassement arrivé : le goto à la marque déposé */
    RESTORE_END,       /* arrivé sur la marque, bloqué, ou la clé ou les bornes changées (le 0x1C émis) : READY
                          (restore_cause) */
} restore_step_t;

/* La fin d'un démarrage, la lecture de la marque rendue ou échue : vrai, un trajet est tracé (RESTORING, restore_start) ;
 * faux, READY (restore_skip) : pas de marque, ou une marque hors des bornes du 0x06. */
bool restore_plan(void);

/* Après l'entrée en READY sur un restore_plan faux : la ligne `* restore … skip=`, `nomark` le jeton sans marque
 * (`nomark`, ou `timeout` à l'échéance de la lecture). */
void restore_skip(const char *nomark);

/* Après l'entrée en RESTORING sur un restore_plan vrai : la ligne `* restore`, le premier goto déposé à `now` (µs). */
void restore_start(uint64_t now);

restore_step_t restore_track(uint64_t now);

/* q en RESTORING : la cause de la sortie est l'arrêt. */
void restore_stop(void);

/* La cause de la dernière sortie de RESTORING vers READY (`arrived`, `stall`, `stop`, `changed`), pour le
 * journal. */
const char *restore_cause(void);

#endif
