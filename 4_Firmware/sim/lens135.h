/* SOURCE: 7_Docs/E-Mount/samyang.md ; traces du 135 de l'humain (4_Firmware/traces, firmware 1.05) ; analyse statique privée du firmware 1.06
 * AUTHOR: engineer
 * DATE: 2026-09-26
 * STATUS: actif — lu par lens_sim_135.c, test_lens135, replay et les bancs du firmware sur le faux 135
 *
 * Le faux Samyang AF 135, vu du fil : il reçoit ce qu'un boîtier enverrait (octets, fronts VD, BODY_CS, alimentation)
 * et rend ce que le 135 rendrait (octets, LENS_CS), sur une horloge virtuelle en µs. Ce que ni samyang.md ni une trace ne fixent
 * est un PARAMÈTRE ci-dessous, avec la source de sa valeur par défaut, ou une simplification déclarée dans lens135.c.
 *
 * Ce qui est servi : la liste blanche de spec § 7.1.3 (0x01, 0x03, 0x04, 0x07, 0x08, 0x09, 0x0A,
 * 0x0B, 0x0D, 0x10, 0x1C, 0x1D, 0x3F ; 0x40 'V' 00, 'F' FA/FB/32, 'M' 00/31), plus 0x40 'P' 38 et
 * 'P' FA hors 0x53 (S13 de lens135.c). Tout autre type connu
 * du 135 est consommé selon son descripteur et compté « non modélisé » (l135_unmodelled), sans
 * réponse ni effet. Un type sans descripteur bloque l'objectif jusqu'à la coupure (l135_blocked). Une
 * entrée que le modèle ne sait pas suivre sans inventer (type hors table, pavage qui déborde de la
 * trame) le met « hors modèle » (l135_out_of_model) : il se tait, et un test doit le voir. */
#ifndef LENS135_H
#define LENS135_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ── Paramètres : chaque délai ou échéance est ici, avec la source de sa valeur par défaut ── */
typedef struct {
    uint64_t loop_us;            /* latence de la boucle de fond entre une trame reçue et son traitement */
    uint64_t boot_us;            /* mise sous tension -> première passe de la boucle de fond */
    uint64_t byte_us;            /* durée d'un octet sur la ligne */
    uint64_t lens_cs_tail_us;    /* LENS_CS tenue haute après le dernier octet */
    uint64_t hs_us;              /* durées de la poignée de main */
    uint64_t busy_0x10_us;       /* attente active du handler du 0x10 */
    uint64_t abort_pause_us;     /* pause de la boucle de fond sur un abandon de homing */
    uint64_t reply40_us;         /* délai minimal d'une réponse 0x40 */
    uint64_t wait_0a_us;         /* attente maximale de l'arrêt des moteurs par le 0x0A */
    uint64_t stop_1c_us;         /* échéance de l'arrêt 0x1C */
    uint64_t step_us;            /* échéance d'une étape de homing */
    uint64_t iris_search_max_us; /* échéance de la recherche de la fourche d'iris */
    uint64_t homing_pause_us;    /* attente en tête des étapes 2, 6 et 0x15 du homing */
    uint32_t steps_per_s;        /* vitesse lente du moteur de mise au point, celle de 'F' FB ; la rapide est le double */
    uint64_t iris_move_us;       /* durée d'un déplacement d'iris (moteur d'iris abstrait) */
    uint64_t iris_search_us;     /* durée de la recherche de la fourche d'iris */
    int32_t  fork_low;           /* front de la fourche de mise au point, en descendant (pas, après homing) */
    int32_t  fork_high;          /* le même front, en montant */
    int32_t  home_edge;          /* la position que le homing donne au front descendant de la fourche */
    int32_t  initial_position;   /* position mécanique au démarrage du modèle (pas, après homing) */
    bool     hs_in_high;         /* niveau de la ligne d'entrée lue à l'étape 3 de la poignée de main (protocol.md § 2) */
} l135_params_t;

void l135_params_default(l135_params_t *p);

/* ── Pannes émises, réglables (spec § 11) ── */
typedef struct {
    bool     silent;          /* l'objectif n'émet plus rien */
    uint64_t delay_us;        /* chaque réponse de classe 2/3 part d'autant plus tard */
    uint16_t truncate_after;  /* chaque réponse de classe 2/3 est coupée après k octets (0 : non) */
    bool     frozen_position; /* le moteur de mise au point est commandé mais la position ne change plus */
} l135_faults_t;

/* ── Entrées physiques posées par le test ── */
typedef struct {
    bool button;              /* le bouton du fût enfoncé : l'offset 64 du 0x05, bit 3 */
    bool m2;                  /* le commutateur Custom en position basse, M2 ; sinon M1, la haute (S14) */
} l135_inputs_t;

/* ── Sorties sur le fil, horodatées ── */
typedef enum { L135_OUT_BYTE, L135_OUT_LENS_CS } l135_out_kind_t;
typedef struct {
    uint64_t        t;
    l135_out_kind_t kind;
    uint8_t         v;        /* octet, ou niveau de LENS_CS (1 = haut) */
} l135_out_t;

#define L135_OUT_CAP 8192u
#define L135_RX_RING 2048u    /* anneau de réception : taille neutre, aucun banc ne le remplit */
#define L135_TX_CAP 2048u
#define L135_PAYLOAD_CAP 256u /* tampon des sous-messages reçus : une trame de 255 octets au plus (protocol.md § 3.2) */

typedef struct {
    l135_params_t p;
    l135_faults_t f;
    l135_inputs_t in;
    uint64_t now;
    uint8_t  custom;              /* la configuration du commutateur Custom (haut << 4 | bas),
                                     en flash : hors de la RAM remise à zéro, elle survit à la coupure (S13) */

    /* ── alimentation, blocage, hors modèle ── */
    bool powered;
    bool blocked;                 /* répartiteur bloqué (samyang.md § 2.6) */
    bool oom;                     /* hors modèle */
    const char *oom_why;
    uint32_t unmodelled[256];     /* par type : types connus hors liste, variantes non modélisées */
    const char *unmodelled_why;   /* la dernière raison */
    uint32_t dropped;             /* réponses jetées par le verrou d'émission (samyang.md § 2.5) */
    uint32_t silenced;            /* trames tues par la panne « silence » */
    uint64_t busy_until;          /* la boucle de fond ne tourne pas avant cet instant */

    /* ── broches et liaison ── */
    bool body_cs, lens_cs, uart_open;
    int  link;                    /* état de liaison : 0 poignée de main, 10 normal */
    int  hs;                      /* étape de la poignée de main */
    uint64_t t_hs;

    /* ── réception ── */
    uint8_t  ring[L135_RX_RING];
    uint16_t head, tail;
    int      rx_state;            /* état de l'analyseur : 9 = trame valide en attente du répartiteur */
    uint8_t  rx_len_lo, rx_cls, rx_seq, ck_lo, ck_hi;
    uint16_t rx_len, rx_i;
    uint8_t  rx_payload[L135_PAYLOAD_CAP];
    uint64_t t_dispatch;

    /* ── émission ── */
    uint8_t  txq[L135_TX_CAP];
    uint16_t txq_head, txq_n;
    int      tx_state;            /* étape de la tâche d'émission */
    uint64_t t_tx, tx_hold_until;
    bool     tx_isr;              /* interruption d'émission active */
    uint64_t t_next_byte;
    bool     lock;                /* le verrou d'émission (samyang.md § 2.5) */
    uint8_t  seq;                 /* la séquence du flux (protocol.md § 3.4) */

    /* ── flux et créneaux ── */
    bool     flow;                /* le flux (samyang.md § 2.1) */
    uint8_t  mask[16];            /* octets 1 à 16 du dernier 0x0A traité */
    uint16_t len05, len06;        /* mise en page recalculée, octet de type compris */
    bool     layout_traced;       /* le masque est celui des traces : les gabarits valent */
    int      slot;                /* le créneau courant (samyang.md § 2.4) */
    bool     slots_on;
    uint64_t t_slot, slot_us;
    bool     got03, got04, send05, send06;
    bool     do_iris, do_frame;

    uint8_t  buf0b;               /* offset 0 du dernier 0x0B, que sa réponse reprend */

    /* ── le commutateur Custom (S14) ── */
    bool     got08;               /* un 0x08 reçu */
    bool     old_body;            /* posé par le 0x01 en FF 01 00, levé par le bit 0x04 du 0x08 */
    bool     body08;              /* le dernier 0x08 portait le bit 0x02 ou 0x04 */
    uint8_t  pos_up, pos_dn;      /* la configuration de M1 et de M2, en RAM */
    bool     sw_mf;               /* l'offset 62 du 0x05, bit 1 (position MF) */
    bool     body_af;             /* le bit AF du boîtier, offset 3 du dernier 0x04, bit 1 */

    /* ── la bague (S12, S15) ── */
    uint8_t  o60;                 /* l'offset 60 du 0x05 */
    int32_t  ring_cnt;            /* le compteur de la bague */
    int32_t  ring_ref;            /* le compteur au dernier pas rendu */

    /* ── l'ouverture native (S15) ── */
    bool     ap_on;               /* le mode, au dernier 0x03 */
    bool     ap_sync;             /* la resynchronisation attendue */
    uint8_t  ap_seq;              /* le numéro du dernier 0x03 au bit 0x10 */
    uint8_t  ap_hist_i;           /* la prochaine case de l'historique */
    uint16_t ap_hist[4];          /* les dernières consignes */
    bool     ap_zeroed;           /* le compteur remis à 0 depuis l'entrée */
    bool     ap_was;              /* le mode, au 0x03 d'avant */
    int32_t  ap_idx;              /* l'index dans la table */
    uint16_t ap_pub;              /* offsets 17-18 du 0x05 */
    bool     ap_valid;            /* offset 19 du 0x05, bit 0 */

    /* ── 0x0A ── */
    int      step0a;
    uint64_t t0a;
    uint8_t  req0a[17];

    /* ── canal 0x40 ── */
    uint8_t  buf40[19];           /* tampon de réponse, partagé avec les notifications */
    int      reply40;             /* étape de la réponse différée */
    uint64_t t40;
    bool     wait40_focus;        /* la réponse attend l'arrêt du moteur */
    bool     service, notify, notify_off; /* mode service, notifications armées, 'H'/'L' coupées par 'M' 31 */

    /* ── homing ── */
    int      homing;              /* étape du homing, 0 hors homing */
    uint64_t t_home;
    uint8_t  msg10;               /* offset 0 du dernier 0x10 */
    int      iris_tries;          /* essais de la fourche d'iris */
    bool     retry_iris, retry_focus; /* un premier échec noté (samyang.md § 3.1) */
    int      mode;                /* 0 avant le homing, 1 le mode normal (S7) */
    int32_t  edge;                /* la position au dernier front de la fourche */
    int32_t  edge_home;           /* le front pris par le homing */
    uint64_t t_iris_search;

    /* ── moteur de mise au point ── */
    int64_t  m0;                  /* position mécanique au début du segment courant */
    uint64_t t_m0;
    int32_t  k;                   /* position publiée = mécanique + k */
    bool     moving;
    int      dir;                 /* +1 / -1 */
    int32_t  target;              /* pendant un mouvement : la cible, en position mécanique */
    uint32_t speed;               /* vitesse : SPEED_SLOW ou SPEED_FAST de lens135.c */
    bool     armed;               /* un déplacement armé pour le front VD suivant */
    int32_t  armed_target;
    uint32_t armed_speed;
    int      pending;             /* 0, 1 = déplacement demandé, 2 = arrêt demandé */
    int32_t  pend_target;
    uint32_t pend_speed;
    bool     pi;                  /* fourche : true = côté bas */
    int32_t  published_prev;      /* la position du 0x06 précédent */

    /* ── iris (moteur abstrait) ── */
    bool     iris_armed, iris_moving;
    uint64_t t_iris_end;
    uint16_t iris_setpoint;       /* offsets 3-4 du dernier 0x03 */

    /* ── travaux de mise au point ── */
    int      job1d, job1c;
    uint8_t  msg1d[5];
    uint64_t t1c;
    bool     ack1d, ack1c;

    /* ── sorties ── */
    l135_out_t out[L135_OUT_CAP];
    uint32_t   out_head, out_n;
} l135_t;

/* ── API ── */
void l135_init(l135_t *l, const l135_params_t *p);   /* hors tension, horloge à 0 */
void l135_power(l135_t *l, uint64_t t, bool on);      /* mise sous tension (RAM initiale) ou coupure */
/* Panne « objectif resté alimenté » (samyang.md § 2.7) : la liaison est établie depuis longtemps, il
 * ne lèvera plus LENS_CS ; le flux est `flow` (masque de la carte), le mode service `service`. */
void l135_start_powered(l135_t *l, uint64_t t, bool flow, bool service);
void l135_body_cs(l135_t *l, uint64_t t, bool high);
void l135_byte(l135_t *l, uint64_t t, uint8_t b);     /* un octet reçu (fin de son dernier bit) */
void l135_vd(l135_t *l, uint64_t t);                  /* un front VD */
/* Un front de la bague à t ; `up` : le compteur de la bague avance (offset 60 à 01), sinon recule (FF). L'offset 60
 * n'est posé que si la position du commutateur est configurée AF ; remis à 0 par le 0x04 suivant (S12). Le compteur
 * avance dans toutes les positions ; l'ouverture native le lit (S15). */
void l135_ring_edge(l135_t *l, uint64_t t, bool up);
void l135_advance(l135_t *l, uint64_t t);             /* fait tourner le modèle jusqu'à t */
uint64_t l135_next_event(const l135_t *l);            /* prochain instant où le modèle agit seul */
bool l135_out(l135_t *l, l135_out_t *o);              /* sortie suivante, dans l'ordre */

bool l135_blocked(const l135_t *l);
bool l135_out_of_model(const l135_t *l);
uint32_t l135_unmodelled(const l135_t *l, uint8_t type);
int32_t l135_position(const l135_t *l);               /* position publiée, pour les tests de pannes */

#endif
