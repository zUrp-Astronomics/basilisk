/* SOURCE: spec de l'atelier § 3.1 — SESSION, l'immobilité de l'objectif
 * AUTHOR: engineer
 * DATE: 2026-10-03
 * STATUS: actif — en-tête privé de la SESSION, inclus par session.c, motion.c et still.c seuls
 *
 * L'objectif est immobile quand ses 0x06 donnent la même position, octet de mouvement nul, depuis plus de 300 ms et sur
 * au moins dix 0x06. Chaque appelant tient son propre suivi (still_t) : le homing de démarrage et MOUVEMENT ne partagent
 * aucun état. */
#ifndef BSK_STILL_H
#define BSK_STILL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     have;                    /* faux : le suivi repart de zéro au prochain appel */
    uint16_t pos;
    uint64_t t;
    uint32_t rx06;
} still_t;

/* Évalué à chaque 0x05 ou 0x06 et à l'échéance, à l'instant now (µs) : vrai quand l'objectif est immobile. */
bool still(still_t *w, uint64_t now);

#endif
