/* SOURCE: include/bsk_phy.h ; 7_Docs/E-Mount/protocol.md § 2 (la poignée de main)
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — implémente include/bsk_phy.h à la place de sim/phy_sim.c pour sim/test/test_session_script.c
 * Un répondeur minimal derrière bsk_phy.h, sans le verrou de XDETECT.
 *
 * Il joue un objectif réduit à ce que le script lui donne, sur une horloge virtuelle en µs :
 *   - à chaque requête de classe 2 dont le type a une réponse, il rend ce message (classe 2), après
 *     son délai ; sans réponse, il se tait ;
 *   - avec `flow`, après chaque 0x04 reçu, il émet un 0x05 puis un 0x06 (classe 1) ; `flow_on_0a`
 *     allume `flow` quand il répond au 0x0A ;
 *   - avec `handshake`, il lève LENS_CS 1,1 ms après BODY_CS haute, et la rabaisse 1,1 ms après BODY_CS
 *     basse (« plus d'une milliseconde », protocol.md § 2) ; puis la poignée de main est
 *     faite. Une LENS_CS déjà haute (ps_lens_cs_start) n'est pas relevée, et ne retombe qu'à une
 *     BODY_CS basse qui suit une BODY_CS haute ;
 *   - avec `mute_cs_high`, il ne répond à rien de ce qu'il reçoit tant que sa LENS_CS de poignée
 *     de main n'est pas retombée ;
 *   - la VD : un BSK_PHY_VD à chaque période demandée ;
 *   - LENS_CS lue (bsk_phy_lens_cs) : le niveau du dernier BSK_PHY_LENS_CS remis par bsk_phy_poll, ou
 *     celui de ps_lens_cs_start (`seen`) avant le premier ;
 *   - avec `on_send`, le test voit chaque trame reçue avant que le répondeur ne la serve :
 *     il peut y changer m05 et m06 (une position qui bouge, l'octet de mouvement, un accusé 1D après le
 *     0x06), que le flux émet ensuite ; avec `fail_sends`, les émissions suivantes échouent (E_BUS).
 * Les messages rendus sont ceux que le test lui donne : le test cite leur source. Il note tout ce que
 * la carte fait sur le fil (trames, BODY_CS, rails, VD, lignes pilotées ou relâchées), horodaté. Relâcher les
 * lignes pose BODY_CS basse pour l'objectif (le tirage bas de la carte). D2 et les erreurs de PHY sont
 * injectés tels que PHY les rapporte (présence déjà filtrée par l'anti-rebond, E_FRAMING, E_BUS). Il n'a pas le verrou
 * de XDETECT : bsk_phy_rail, bsk_phy_lines et bsk_phy_vd sont notés et rendent true ; le verrou se joue
 * dans la PHY simulée (sim/phy_sim.c, joué par test_phy et test_session135). */
#ifndef PHY_SCRIPT_H
#define PHY_SCRIPT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsk_phy.h"

typedef struct {
    uint8_t  msg[BSK_MSG_MAX];
    uint16_t len;          /* 0 : pas de réponse à ce type */
    uint64_t delay_us;
} ps_reply_t;

typedef struct {
    ps_reply_t reply[256]; /* par type de requête */
    uint8_t    skip[256];  /* autant de requêtes de ce type laissées sans réponse avant de répondre */
    bool       handshake;
    bool       mute_cs_high;  /* rien n'est servi tant que sa LENS_CS de poignée de main est haute */
    bool       flow;
    bool       flow_on_0a;
    uint8_t    m05[BSK_MSG_MAX], m06[BSK_MSG_MAX];
    uint16_t   n05, n06;
    void     (*on_send)(const bsk_frame_t *f, uint64_t t);
    uint8_t    fail_sends; /* autant d'émissions suivantes rendent E_BUS sans rien émettre */
} ps_lens_t;

typedef enum { PS_SEND, PS_BODY_CS, PS_RAIL, PS_VD, PS_LINES } ps_act_kind_t;

typedef struct {
    ps_act_kind_t kind;
    uint64_t      t;
    bsk_frame_t   frame;   /* PS_SEND */
    bool          on;      /* PS_BODY_CS, PS_RAIL, PS_LINES (true : pilotées) */
    bsk_rail_t    rail;    /* PS_RAIL */
    uint16_t      hz;      /* PS_VD */
} ps_act_t;

#define PS_LOG_CAP 8192u

extern ps_lens_t g_ps;
extern ps_act_t  g_ps_log[PS_LOG_CAP];
extern size_t    g_ps_n;

void     ps_init(uint64_t t0);             /* objectif muet, sans poignée de main, horloge à t0, journal vide */
void     ps_answer(uint8_t type, const uint8_t *msg, size_t len, uint64_t delay_us);
uint64_t ps_now(void);
uint64_t ps_next(void);                    /* prochain événement, UINT64_MAX s'il n'y en a aucun */
void     ps_run(uint64_t t);               /* l'horloge avance jusqu'à t */
void     ps_presence(bool present);        /* BSK_PHY_PRESENCE, à l'instant courant */

/* Un événement rétrodaté, comme la PHY de la carte peut en rendre (L01-01 de l'audit : une trame datée à son traitement
 * poussée avant un front daté plus tôt, à son interruption) : posé derrière la file, sans tri, il est rendu après tout ce
 * qui y est, même daté plus tard. Le reste de la file n'est plus trié : à jouer sur une file vide, sans autre émission
 * avant sa lecture. */
void     ps_push_back(const bsk_phy_event_t *e);
void     ps_error(bsk_err_t err);          /* BSK_PHY_ERROR, à l'instant courant */
void     ps_frame(uint8_t cls, const uint8_t *msg, size_t len, uint64_t at);   /* une trame de l'objectif */
void     ps_lens_cs(bool high, uint64_t at);                                   /* LENS_CS, hors poignée de main */
/* LENS_CS à ce niveau dès maintenant, SANS événement, comme phy.c la trouve à bsk_phy_init sans rien
 * publier ; haute, c'est la LENS_CS de poignée de main de l'objectif, levée avant que la carte ne démarre.
 * `seen` : bsk_phy_lens_cs rend ce niveau ; sans `seen`, LENS_CS ne se connaît que par ses fronts
 * (BSK_PHY_LENS_CS) : bsk_phy_lens_cs la rend basse jusqu'au premier. */
void     ps_lens_cs_start(bool high, bool seen);

#endif
