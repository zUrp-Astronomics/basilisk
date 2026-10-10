/* SOURCE: spec de l'atelier § 3.1 — SESSION, l'immobilité de l'objectif
 * AUTHOR: engineer
 * DATE: 2026-10-03
 * STATUS: actif — C pur, sans ESP-IDF : compilé aussi par les tests hôte
 *
 * Le suivi d'immobilité (still.h). Il ne lit que ce que l'objectif a publié (lens_rx.h). */
#include "still.h"

#include "lens_rx.h"

#define MS             1000u
#define STILL_QUIET_MS 300u
#define STILL_06       10u

bool still(still_t *w, uint64_t now)
{
    const lens_rx_t *l = lens_rx();
    if (!l->have06) return false;
    if (!w->have || l->pos != w->pos || l->move != 0) {
        w->have = true;
        w->pos = l->pos;
        w->t = now;
        w->rx06 = l->rx06;
    } else if (now - w->t > (uint64_t)STILL_QUIET_MS * MS && l->rx06 - w->rx06 >= STILL_06) {
        return true;
    }
    return false;
}
