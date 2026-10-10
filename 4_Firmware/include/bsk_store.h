/* SOURCE: spec de l'atelier § 9 — le magasin de la marque, dans la carte
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — déclarations seulement ; sur la carte components/store/store.c (NVS), en test sim/store_sim.c
 *
 * Le seul état persistant de la carte : un entier de 32 bits par clé, dans l'espace NVS `mark`. La clé et la forme de la
 * valeur sont choisies par la SESSION (components/session/mark.c) ; le magasin ne les interprète pas.
 *
 * Les tâches : bsk_store_request et bsk_store_result sont appelées par la tâche de SESSION seule. */

/* Asynchrone, pour que la SESSION n'attende pas la NVS : une écriture NVS peut effacer une page de flash, des dizaines de
 * millisecondes, et l'API NVS est sous un verrou : une lecture faite par la SESSION attendrait une écriture en cours.
 * Toutes les opérations passent donc par une file, servie dans l'ordre ; l'ordre de la file garantit qu'une lecture
 * déposée après une écriture de la même clé la voit. */
#ifndef BSK_STORE_H
#define BSK_STORE_H

#include <stdbool.h>
#include <stdint.h>

#define BSK_STORE_KEY_MAX 16u   /* NUL compris : une clé NVS a 15 caractères au plus ; celle de la marque en a 13 */

typedef enum { BSK_STORE_GET, BSK_STORE_PUT, BSK_STORE_DEL } bsk_store_op_t;

typedef struct {
    bsk_store_op_t op;
    char           key[BSK_STORE_KEY_MAX];
    bool           ok;       /* GET : une valeur est rangée sous la clé ; PUT, DEL : fait (DEL d'une clé absente compris) */
    uint32_t       value;    /* GET trouvée : la valeur */
} bsk_store_res_t;

/* Sur la carte : la NVS (nvs_flash_init) et la tâche du magasin ; avant bsk_session_init. */
void bsk_store_init(void);

/* Dépose une opération (value : celle de PUT), sans attendre. Faux si la file est pleine : rien n'est déposé. */
bool bsk_store_request(bsk_store_op_t op, const char *key, uint32_t value);

/* Le résultat suivant, dans l'ordre des dépôts ; faux s'il n'y en a pas encore. Chaque dépôt accepté en rend un. */
bool bsk_store_result(bsk_store_res_t *r);

#endif
