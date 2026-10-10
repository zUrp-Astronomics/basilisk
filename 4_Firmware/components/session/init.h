/* SOURCE: spec de l'atelier § 3.1 — IDENTIFYING, la séquence d'init
 * AUTHOR: engineer
 * DATE: 2026-10-03
 * STATUS: actif — en-tête privé de la SESSION, inclus par session.c, init.c et cmd.c
 *
 * Quelle requête d'init envoyer, quand la renvoyer, la reconnaissance Samyang (aux réponses au 0x07 et au 0x3F, et à
 * l'oubli de la session seulement) et le lancement de la boucle. Le superviseur
 * garde l'état, ses transitions et ses échéances : il appelle init_start à l'entrée en IDENTIFYING, passe chaque réponse
 * (ou échec) d'une requête de l'init à init_reply, et fait ce qu'elle lui rend. */
#ifndef BSK_SESSION_INIT_H
#define BSK_SESSION_INIT_H

#include <stdbool.h>
#include <stdint.h>

#include "bsk_bench_core.h"
#include "bsk_err.h"
#include "bsk_phy.h"
#include "bsk_session.h"

/* Ce que le superviseur fait après une réponse de l'init. */
typedef enum {
    INIT_ASKING,       /* une requête de l'init en vol (la suivante, ou la même renvoyée) : sa réponse est attendue */
    INIT_END,          /* l'init faite, ou arrêtée au premier exigé qui manque ; la boucle lancée, premier 0x05 attendu */
    INIT_FAIL,         /* muet après le reset soft : un échec compté */
    INIT_HOME_FAILED,  /* le 0x10 de l'init a répondu 10 01 : FAULT (E_HOME_FAILED) */
} init_next_t;

/* À bsk_session_init : les échéances des requêtes (init_ms, name_ms, info_ms, init_home_ms), rien d'autre ; tout oublié. */
void init_params(const bsk_session_params_t *p);

/* L'entrée en IDENTIFYING : le 0x01, premier essai, envoyé à `now` (µs). */
void init_start(uint64_t now);

/* La requête en vol finie à `now` : répondue (`ok`, la réponse `r`), ou en échec (`why` : l'échéance ou l'erreur de
 * réception). Envoie la suivante ou renvoie la même (INIT_ASKING), sinon dit au superviseur quoi faire. */
init_next_t init_reply(bool ok, const bsk_frame_t *r, bsk_err_t why, uint64_t now);

/* Avec la session, après lens_rx_forget : le reset soft et l'init faite oubliés, l'objectif reconnu de nouveau sur ce qui
 * reste publié. */
void init_forget(void);

/* La dernière déclaration faite à bench_core (BSK_BENCH_SAMYANG135 : le Samyang AF 135 reconnu). */
bsk_bench_lens_t init_lens(void);

/* L'init de cette session finie par un 0x0A répondu : pas de second homing. */
bool init_done(void);

/* Le 0x10 de l'init en vol : un homing, dont la réponse n'arrive qu'à la fin du mouvement ; q ne l'interrompt pas (cmd.c),
 * comme en HOMING. Faux dès que sa réponse ou son échéance est lue. À ne lire qu'en IDENTIFYING : l'oubli de la session
 * ne le remet pas à faux. */
bool init_homing(void);

#endif
