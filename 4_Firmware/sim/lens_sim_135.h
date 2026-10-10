/* SOURCE: l'interface commune « faux objectif » (sim/lens_sim.h)
 * AUTHOR: engineer
 * DATE: 2026-09-29
 * STATUS: actif — implémenté par sim/lens_sim_135.c
 * L'adaptateur du faux 135 à l'interface commune. */
#ifndef LENS_SIM_135_H
#define LENS_SIM_135_H

#include "lens135.h"
#include "lens_sim.h"

/* Le faux 135 `l`, initialisé par l'appelant (l135_init), vu par l'interface commune. */
lens_sim_t lens_sim_135(l135_t *l);

#endif
