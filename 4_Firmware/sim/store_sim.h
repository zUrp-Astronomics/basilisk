/* SOURCE: le magasin de la carte, include/bsk_store.h
 * AUTHOR: engineer
 * DATE: 2026-09-30
 * STATUS: actif — implémente include/bsk_store.h (sim/store_sim.c) ; ce qui suit est réservé aux tests
 * Le magasin de la marque en mémoire, pour les tests hôte.
 *
 * Même interface que le magasin de la carte (components/store/store.c), sans tâche : une opération est faite au dépôt,
 * son résultat attend bsk_store_result, dans l'ordre. Avec `hold`, les dépôts attendent store_sim_flush, qui les fait dans
 * l'ordre, comme la tâche du magasin de la carte les ferait plus tard : une lecture rend ce qui est rangé quand elle est
 * faite. Les valeurs survivent aux sessions ; store_sim_reset les efface. */
#ifndef STORE_SIM_H
#define STORE_SIM_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     refuse;      /* la file est pleine : bsk_store_request refuse tout dépôt */
    bool     fail;        /* PUT et DEL ratés (ok faux), rien n'est changé */
    bool     hold;        /* les dépôts attendent store_sim_flush */
    uint32_t requests;    /* dépôts acceptés depuis store_sim_reset */
    uint32_t writes;      /* PUT et DEL acceptés depuis store_sim_reset */
} store_sim_t;

extern store_sim_t g_store_sim;

/* Tout effacé, pannes retirées, compteurs à zéro. */
void store_sim_reset(void);

/* Les dépôts en attente (hold), faits dans l'ordre ; leurs résultats attendent bsk_store_result. */
void store_sim_flush(void);

/* Ce qui est rangé sous `key`, sans passer par la file : faux s'il n'y a rien. */
bool store_sim_peek(const char *key, uint32_t *v);

/* Range `v` sous `key` sans passer par la file : une marque déjà rangée, telle qu'un démarrage la relit. */
void store_sim_poke(const char *key, uint32_t v);

#endif
