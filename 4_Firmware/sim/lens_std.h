/* SOURCE: 7_Docs/E-Mount/tamron.md et protocol.md ; analyse statique privée du firmware 3.01 du Tamron F051, écrit à la main
 * AUTHOR: engineer
 * DATE: 2026-09-29
 * STATUS: actif — joué par test_lens_std, et par test_std à travers lens_sim_std.c
 *
 * Le faux objectif « standard », le Tamron F051 vu du fil : il reçoit ce qu'un boîtier enverrait (octets,
 * fronts VD, BODY_CS, alimentation) et rend ce que le F051 rendrait (octets, LENS_CS), sur une horloge
 * virtuelle en microsecondes. Même forme d'interface que le faux 135, préfixe lstd_.
 *
 * Le comportement est celui que décrit 7_Docs/E-Mount/tamron.md (« § n » dans lens_std.c et ses tests), cité à
 * chaque mécanisme ; ce que seule l'analyse statique privée du firmware établit est dit tel, sans renvoi. Ce que
 * l'analyse laisse [NÉ] et que le modèle doit pourtant fixer est un PARAMÈTRE ci-dessous ; ce qu'il ne suit pas est
 * une simplification déclarée en tête de lens_std.c.
 *
 * Ce qui est servi : la liste blanche de la carte (spec § 7.1.3 : 0x01, 0x03, 0x04, 0x07, 0x08, 0x09, 0x0A,
 * 0x0B, 0x0D, 0x10, 0x1C, 0x1D, 0x3F), plus 0x19 et 0x1B pour les états de réponse (§ 2.6, § 5.4). Les gardes
 * de phase et d'état (§ 2.2) valent pour TOUS les types : un type refusé reçoit son 0x02 (§ 2.3), servi ou
 * non. Un type accepté hors de ce qui est servi est consommé selon sa longueur (§ 2.2) et compté « non
 * modélisé » (lstd_unmodelled), sans réponse ni effet, s'il ne change aucun état que le boîtier observe ;
 * sinon il met le modèle « hors modèle » (lstd_out_of_model) : il se tait, et un test doit le voir. Le 0x14
 * (sortie de l'application, § 6.1) le « bloque » (lstd_blocked) jusqu'à la coupure. */
#ifndef LENS_STD_H
#define LENS_STD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ── Paramètres : chaque délai ou valeur que le code ne donne pas est ici ; la source de la valeur par
 *    défaut est dans lstd_params_default ── */
typedef struct {
    /* Couche physique (lens_std.c, « Couche physique ») */
    uint64_t byte_us;            /* durée d'un octet : 10 bits à 750 000 bauds [É] */
    uint64_t lens_cs_lead_us;    /* LENS_CS levée -> début du premier octet : des boucles vides (§ 2.8) [NÉ] */
    uint64_t lens_cs_tail_us;    /* fin du dernier octet -> LENS_CS rabaissée : latence de la boucle de fond,
                                  * plafonnée par un repli de 200 µs [NÉ] ; 0 (Sony réel) à des centaines de µs */
    uint64_t tx_poll_us;         /* conditions d'émission réunies -> LENS_CS levée : latence de la tâche [NÉ] */
    uint64_t boot_us;            /* mise sous tension -> début de la poignée de main [NÉ] */
    uint64_t hs_us;              /* LENS_CS levée par la poignée de main -> sa seconde étape (§ 2.8) [NÉ] */
    /* Boucle de fond (§ 2.5, § 2.6) */
    uint64_t loop_us;            /* BODY_CS retombée -> traitement de la trame par la boucle de fond [NÉ] */
    uint64_t defer_us;           /* réponse différée prête -> son émission par la tâche de fond (§ 2.6) [NÉ] */
    uint64_t service_us;         /* liaison ouverte -> module de mise au point en service [NÉ] */
    uint64_t frame_period_us;    /* 0 : la routine de trame ne tourne qu'aux fronts VD ; sinon, aussi sur une
                                  * horloge interne de cette période (§ 2.5) [NÉ] */
    uint64_t q05_us, q06_us;     /* routine de trame -> 0x05 / 0x06 mis en file par leurs interruptions (§ 2.5) [NÉ] */
    /* Homing (§ 3) */
    uint64_t homing_busy_us;     /* attente active de l'étape 3, boucle de fond bloquée [NÉ] */
    uint64_t homing_ref_us;      /* étape 1 -> servo posé (étape 4) : le référencement des modules [NÉ] */
    bool     homing_fails;       /* un indicateur d'erreur de la mise au point ou de l'iris : `10 01` au lieu de `10 00` [NÉ] */
    int32_t  home_pos;           /* position après un homing de la mise au point (bit 3 du 0x10) [NÉ] */
    /* 0x1B (§ 5.4) */
    uint64_t iris_1b_us;         /* 0x1B -> la condition sur le module d'iris vraie (attente -> prête) [NÉ] ; UINT64_MAX : jamais */
    uint8_t  r1b[10];            /* les 10 octets de la réponse 1B après le type, tirés du module d'iris [NÉ] */
    /* Valeurs de l'objectif, lues dans sa mémoire ou calculées à l'exécution */
    uint32_t serial;             /* 0x08, offsets 27-30 : numéro de série de l'exemplaire [NÉ] */
    uint32_t steps_per_s;        /* vitesse du moteur de mise au point [NÉ] */
    uint16_t lim_low, lim_high;  /* bornes de la cible (§ 4.3) [I] */
    uint16_t soft_low, soft_high;/* limites logicielles (§ 4.3) [NÉ] */
    uint16_t soft_margin;        /* la marge des drapeaux de limite logicielle [NÉ] */
    uint16_t hard_low, hard_high;/* butées dures : deux grandeurs du module de mise au point moins un décalage
                                  * (§ 4.3, § 4.6) [I] */
    int32_t  initial_position;   /* position mécanique à la mise sous tension [NÉ] */
} lstd_params_t;

void lstd_params_default(lstd_params_t *p);

/* ── Pannes émises, réglables (comme l135_faults_t) ── */
typedef struct {
    bool     silent;          /* l'objectif n'émet plus aucune trame (ni octet, ni LENS_CS) ; la poignée de main reste */
    uint64_t delay_us;        /* chaque trame de classe 2/3 part d'autant plus tard */
    uint16_t truncate_after;  /* chaque trame de classe 2/3 est coupée après k octets (0 : non) */
    bool     frozen_position; /* le moteur de mise au point est commandé mais la position ne change plus */
} lstd_faults_t;

/* ── Sorties sur le fil, horodatées ── */
typedef enum { LSTD_OUT_BYTE, LSTD_OUT_LENS_CS } lstd_out_kind_t;
typedef struct {
    uint64_t        t;
    lstd_out_kind_t kind;
    uint8_t         v;        /* octet, ou niveau de LENS_CS (1 = haut) */
} lstd_out_t;

#define LSTD_OUT_CAP 8192u
#define LSTD_RING 3072u       /* anneau de réception (§ 2.8) */
#define LSTD_RX_SLOTS 8u      /* trames reçues en attente de la boucle de fond (§ 2.8) */
#define LSTD_BUF 512u         /* un tampon d'émission (analyse statique privée) */

/* Phase et état de réponse (§ 2.1). */
enum { LSTD_INIT = 1, LSTD_FLOW };
enum { LSTD_FREE = 1, LSTD_WAIT, LSTD_READY };

/* Les tampons d'émission : chacun est de la mémoire que le DMA lit pendant l'émission. */
enum { LSTD_B05, LSTD_B06, LSTD_BREP, LSTD_BERR, LSTD_NBUF };

typedef struct {
    lstd_params_t p;
    lstd_faults_t f;
    uint64_t now;

    /* ── alimentation, blocage, hors modèle ── */
    bool powered;
    bool blocked;                 /* hors de l'application (0x14, § 6.1) */
    bool oom;                     /* hors modèle */
    const char *oom_why;
    uint32_t unmodelled[256];     /* par type : types acceptés hors de ce qui est servi, variantes non modélisées */
    const char *unmodelled_why;
    uint32_t silenced;            /* trames tues par la panne « silence » */
    uint32_t replaced;            /* trames remplacées dans la file d'émission avant d'être parties */
    uint64_t t_power;
    uint64_t busy_until;          /* la boucle de fond ne tourne pas avant cet instant (homing, étape 3) */

    /* ── broches, poignée de main, liaison ── */
    bool     body_cs, lens_cs;
    uint64_t t_body_cs, t_lens_cs;/* instant du dernier changement de niveau */
    int      hs;                  /* 0 attente, 1 attend BODY_CS haute, 2 LENS_CS haute, 3 attend BODY_CS basse, 4 ouverte */
    uint64_t t_hs;
    bool     link;                /* réception armée */
    bool     in_service;          /* module de mise au point en service */
    uint64_t t_link;

    /* ── réception (§ 2.8) ── */
    uint8_t  ring[LSTD_RING];
    uint32_t dma;                 /* prochaine écriture du DMA dans l'anneau */
    uint32_t off;                 /* début de l'emplacement courant */
    bool     rx_open;             /* un front montant de BODY_CS a ouvert l'emplacement */
    uint32_t q_off[LSTD_RX_SLOTS];/* trames closes, en attente de la boucle de fond */
    uint64_t q_t[LSTD_RX_SLOTS];
    uint8_t  q_n;

    /* ── session (§ 2.1) ── */
    int      phase;               /* LSTD_INIT, LSTD_FLOW */
    int      rstate;              /* LSTD_FREE, LSTD_WAIT, LSTD_READY (réponse différée prête) */
    uint8_t  rtype;               /* type de la réponse en attente */
    uint64_t t_rstate;

    /* ── flux (§ 2.4, § 2.5) ── */
    uint16_t pos05[19], pos06[9]; /* offset de chaque bloc dans son tampon ; 0 : bloc non accordé */
    uint16_t end05, end06;        /* fin des blocs accordés */
    uint16_t w06;                 /* pointeur d'écriture du 0x06 : les accusés s'y ajoutent */
    uint16_t seq;                 /* séquence de la classe 1 */
    bool     arm05, arm06;        /* interruptions qui mettent le 0x05 / le 0x06 en file, armées */
    uint64_t t_q05, t_q06;
    uint64_t t_frame;             /* prochaine routine de trame sur l'horloge interne */
    bool     referenced;          /* référencé par un homing (après l'étape 2) */

    /* ── homing (§ 3) ── */
    int      hstep;               /* -1 : pas de tâche ; 0 à 4 */
    uint8_t  hmask;               /* l'offset 0 de la requête, bits 3 (mise au point) et 2 (iris) */
    uint64_t t_hstep;

    /* ── travail de mise au point (§ 4.1) ── */
    int      jtype;               /* arrêt (0x1C), 0x1D, repos : J_STOP, J_GOTO, J_REST de lens_std.c */
    bool     jdone, jnew, jengaged, jerr;
    uint16_t jtarget;
    uint8_t  jmsg[5];
    /* moteur (simplification S3) */
    int64_t  m0;
    uint64_t t_m0;
    bool     moving;
    int      dir;
    int32_t  target;

    /* ── émission (§ 2.8) ── */
    uint8_t  buf[LSTD_NBUF][LSTD_BUF];
    int      pend;                /* tampon en file, -1 aucun */
    uint16_t pend_len;
    uint64_t t_pend;
    int      tx;                  /* tampon en cours d'émission, -1 aucun */
    uint16_t tx_len, tx_i;
    int      tx_state;            /* 0 libre, 1 LENS_CS haute, 2 octets, 3 dernier octet fini */
    uint64_t t_tx;

    /* ── sorties ── */
    lstd_out_t out[LSTD_OUT_CAP];
    uint32_t   out_head, out_n;
} lstd_t;

/* ── API (même forme que lens135.h) ── */
void lstd_init(lstd_t *l, const lstd_params_t *p);    /* hors tension, horloge à 0 */
void lstd_power(lstd_t *l, uint64_t t, bool on);       /* mise sous tension (état initial, § 2.1) ou coupure */
/* Objectif resté alimenté (§ 8) : la liaison est ouverte depuis longtemps, les modules en service et
 * référencés ; `flow` : il est en flux, la mise en page tracée par un premier 0x0A dont `grant` porte les
 * octets 1 à 16 de la réponse ; sinon en phase init, sans mise en page. */
void lstd_start_powered(lstd_t *l, uint64_t t, bool flow, const uint8_t grant[16]);
void lstd_body_cs(lstd_t *l, uint64_t t, bool high);
void lstd_byte(lstd_t *l, uint64_t t, uint8_t b);      /* un octet reçu (fin de son dernier bit) */
void lstd_vd(lstd_t *l, uint64_t t);                   /* un front VD */
void lstd_advance(lstd_t *l, uint64_t t);              /* fait tourner le modèle jusqu'à t */
uint64_t lstd_next_event(const lstd_t *l);             /* prochain instant où le modèle agit seul */
bool lstd_out(lstd_t *l, lstd_out_t *o);               /* sortie suivante, dans l'ordre */

bool lstd_blocked(const lstd_t *l);
bool lstd_out_of_model(const lstd_t *l);
uint32_t lstd_unmodelled(const lstd_t *l, uint8_t type);
int32_t lstd_position(const lstd_t *l);                /* position mécanique, pour les tests de pannes */

#endif
