/* SOURCE: l'interface commune « faux objectif » (sim/lens_sim.h)
 * AUTHOR: engineer
 * DATE: 2026-09-29
 * STATUS: actif — implémenté par sim/lens_sim_std.c
 * L'adaptateur du faux objectif standard (Tamron F051) à l'interface commune. */
#ifndef LENS_SIM_STD_H
#define LENS_SIM_STD_H

#include "lens_sim.h"
#include "lens_std.h"

/* Le faux standard `l`, initialisé par l'appelant (lstd_init), vu par l'interface commune. */
lens_sim_t lens_sim_std(lstd_t *l);

#endif
