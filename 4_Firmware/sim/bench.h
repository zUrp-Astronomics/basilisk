/* SOURCE: spec de l'atelier § 3.3 (la boucle cadencée) ; traces full:67-68
 * AUTHOR: engineer
 * DATE: 2026-09-26
 * STATUS: actif — outil de test hôte, au-dessus de bsk_phy.h (phy_sim.c) et d'un faux objectif (lens_sim.h)
 * Le banc commun du rejeu et des tests de pannes.
 *
 * Il joue le rôle de la carte, rien de plus : il émet des trames par bsk_phy_send, peut émettre la
 * paire 0x03/0x04 à chaque front VD (la boucle cadencée, spec § 3.3), et note tout ce que PHY
 * remonte : trames, erreurs, niveaux de LENS_CS. Il ne conclut sur rien : les tests le font. */
#ifndef BENCH_H
#define BENCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsk_phy.h"
#include "lens_sim.h"

/* Journal en anneau : l'entrée i (numérotée depuis bench_init) est BENCH_EV(i) tant que
 * i >= n - BENCH_LOG_CAP ; une recherche plus ancienne commence au plus vieux gardé. */
#define BENCH_LOG_CAP 16384u
#define BENCH_EV(i) (g_bench.ev[(size_t)(i) % BENCH_LOG_CAP])
#define BENCH_SENT(i) (g_bench.sent[(size_t)(i) % BENCH_LOG_CAP])

typedef struct {
    bsk_phy_event_kind_t kind;
    uint64_t             t;
    bsk_frame_t          frame;   /* BSK_PHY_FRAME */
    bsk_err_t            err;     /* BSK_PHY_ERROR */
    bool                 level;   /* BSK_PHY_LENS_CS ; BSK_PHY_PRESENCE : true = présent */
} bench_ev_t;

typedef struct {
    uint64_t    t;
    bsk_frame_t frame;
} bench_sent_t;

typedef struct {
    bench_ev_t ev[BENCH_LOG_CAP];
    size_t     n;
    bench_sent_t sent[BENCH_LOG_CAP];   /* ce que la carte a émis, à l'instant de l'émission demandée */
    size_t     n_sent;
    bool       loop;             /* la paire 0x03/0x04 part à chaque front VD */
    uint8_t    m03[64], m04[64];
    size_t     n03, n04;
    uint8_t    seq;              /* séquence des trames de classe 1 de la carte */
    bsk_phy_raw_t raw;           /* les octets rejetés de la dernière BSK_PHY_ERROR notée */
} bench_t;

extern bench_t g_bench;

/* Le faux objectif, que l'appelant a initialisé hors tension (l135_init, lstd_init), avancé à t0 ; horloge à t0,
 * journal vide, boucle arrêtée, paire par défaut. */
void bench_init(lens_sim_t lens, uint64_t t0);

/* La paire de la boucle. Par défaut, le 0x03 de full:67 sans le 0x2F qui le suit (la liste blanche
 * ne l'a pas : spec § 7.1.3) et le 0x04 de full:68, tels que tracés. */
void bench_loop_msgs(const uint8_t *m03, size_t n03, const uint8_t *m04, size_t n04);
void bench_loop(bool on);

/* Fait tout avancer jusqu'à t en notant chaque événement ; émet la paire aux fronts VD. */
void bench_run(uint64_t t);
void bench_run_for(uint64_t us);

/* Une trame de la carte : classe 2 et séquence de la carte pour une requête, classe 1 dans la boucle. */
void bench_send(uint8_t cls, const uint8_t *msg, size_t n);

/* Première trame reçue à partir de l'entrée `from` du journal dont le premier sous-message est
 * `type` (et, pour 0x40, dont les deux octets suivants valent main/sub si main != 0). -1 sinon. */
long bench_find(size_t from, uint8_t type, uint8_t main, uint8_t sub);
size_t bench_count(size_t from, uint8_t type);
size_t bench_count_kind(size_t from, bsk_phy_event_kind_t kind);

#endif
