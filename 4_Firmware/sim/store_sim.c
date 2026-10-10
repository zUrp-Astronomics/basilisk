/* SOURCE: le magasin de la carte, include/bsk_store.h
 * AUTHOR: engineer
 * DATE: 2026-09-30
 * STATUS: actif — C pur, compilé avec la SESSION par les tests hôte (sim/test/programmes.sh), jamais par ESP-IDF
 * Le magasin de la marque en mémoire (voir sim/store_sim.h).
 *
 * Une table de clés et de valeurs de 32 bits, comme l'espace `mark` de la NVS ; 32 entrées suffisent aux tests (un
 * objectif par focale touchée). Pleine, une écriture échoue (ok faux), comme une NVS pleine. */
#include "store_sim.h"

#include <string.h>

#include "bsk_store.h"

#define ENTRIES 32u
#define DEPTH   8u   /* la profondeur des files du magasin de la carte (DEPTH, components/store/store.c) : dépôts, résultats */

store_sim_t g_store_sim;

static struct {
    bool     used;
    char     key[BSK_STORE_KEY_MAX];
    uint32_t v;
} tab[ENTRIES];

static bsk_store_res_t res[DEPTH];
static unsigned head, n;
static struct {
    bsk_store_op_t op;
    char           key[BSK_STORE_KEY_MAX];
    uint32_t       v;
} req[DEPTH];
static unsigned n_req;

static int find(const char *key)
{
    for (unsigned i = 0; i < ENTRIES; i++)
        if (tab[i].used && !strcmp(tab[i].key, key)) return (int)i;
    return -1;
}

static bool set(const char *key, uint32_t v)
{
    int i = find(key);
    for (unsigned k = 0; i < 0 && k < ENTRIES; k++)
        if (!tab[k].used) i = (int)k;
    if (i < 0) return false;
    tab[i].used = true;
    memcpy(tab[i].key, key, strlen(key) + 1);
    tab[i].v = v;
    return true;
}

void store_sim_reset(void)
{
    memset(tab, 0, sizeof tab);
    memset(&g_store_sim, 0, sizeof g_store_sim);
    head = n = n_req = 0;
}

bool store_sim_peek(const char *key, uint32_t *v)
{
    int i = find(key);
    if (i < 0) return false;
    *v = tab[i].v;
    return true;
}

void store_sim_poke(const char *key, uint32_t v) { (void)set(key, v); }

void bsk_store_init(void) {}

/* Le dépôt le plus ancien, fait ; son résultat à la suite des autres. Faux s'il n'y en a pas, ou si la file des résultats
 * est pleine (la tâche de la carte y attendrait). */
static bool one(void)
{
    bsk_store_res_t *r;
    int i;
    if (!n_req || n == DEPTH) return false;
    r = &res[(head + n++) % DEPTH];
    memset(r, 0, sizeof *r);
    r->op = req[0].op;
    memcpy(r->key, req[0].key, sizeof r->key);
    if (r->op == BSK_STORE_GET) {
        r->ok = store_sim_peek(r->key, &r->value);
    } else if (g_store_sim.fail) {
        r->ok = false;
    } else if (r->op == BSK_STORE_PUT) {
        r->ok = set(r->key, req[0].v);
    } else {
        if ((i = find(r->key)) >= 0) tab[i].used = false;
        r->ok = true;
    }
    memmove(req, req + 1, --n_req * sizeof req[0]);
    return true;
}

void store_sim_flush(void)
{
    while (one()) {}
}

bool bsk_store_request(bsk_store_op_t op, const char *key, uint32_t value)
{
    if (g_store_sim.refuse || n_req == DEPTH || strlen(key) >= BSK_STORE_KEY_MAX) return false;
    req[n_req].op = op;
    memcpy(req[n_req].key, key, strlen(key) + 1);
    req[n_req++].v = value;
    g_store_sim.requests++;
    if (op != BSK_STORE_GET) g_store_sim.writes++;
    if (!g_store_sim.hold) store_sim_flush();
    return true;
}

bool bsk_store_result(bsk_store_res_t *r)
{
    if (!g_store_sim.hold) store_sim_flush();
    if (!n) return false;
    *r = res[head];
    head = (head + 1) % DEPTH;
    n--;
    return true;
}
