/* SOURCE: 7_Docs/E-Mount/tamron.md et protocol.md ; analyse statique privée du firmware 3.01 du Tamron F051
 * AUTHOR: engineer
 * DATE: 2026-09-29
 * STATUS: actif — joué par test_lens_std, et par test_std à travers lens_sim_std.c
 *
 * Le faux objectif « standard », le Tamron F051 (interface et paramètres : lens_std.h).
 *
 * Citations : « § n » = section de 7_Docs/E-Mount/tamron.md ; « protocol.md § n » = section de
 * 7_Docs/E-Mount/protocol.md. Ce que seule l'analyse statique privée du firmware établit est dit tel, sans renvoi.
 * Marquage : [É] établi par l'analyse, [I] inféré, [NÉ] non établi. Ce qui est [NÉ] et que le modèle doit fixer
 * est un paramètre (lens_std.h).
 *
 * Le modèle suit l'objectif tel que tamron.md § 2 le décrit : des tâches qu'une boucle de fond exécute (réception
 * des trames, réponses différées, homing, émission), une tâche que parcourt l'interruption de trame (routine de
 * trame, construction du flux, fin des travaux et accusés), deux interruptions qui mettent le 0x05 puis le 0x06 en
 * file, et le séquenceur de démarrage (poignée de main, mise en service).
 *
 * ── Couche physique (§ 2.8, ce que le modèle en suit) ──
 *   Poignée de main, en deux temps [É] :
 *     1. au démarrage, l'objectif attend BODY_CS haute, lève LENS_CS, configure son port série, attend sa ligne
 *        de réception haute ;
 *     2. à une étape suivante de son séquenceur, il attend BODY_CS basse, sans échéance, arme la réception et
 *        rabaisse LENS_CS aussitôt : la liaison est ouverte.
 *   Débit : 750 000 bauds, 8N1, hors 0x0C (protocol.md § 1.2) [É].
 *   Émission [É] : une trame mise en file ne part que lorsque LENS_CS est basse depuis plus de 500 µs et BODY_CS
 *   basse depuis plus de 50 µs. **Il n'y a qu'une trame en file** : une trame mise en file avant que la
 *   précédente ne parte la remplace (compté dans `replaced`). Le départ : LENS_CS levée, des boucles vides,
 *   puis le premier octet. L'émission lit le tampon octet par octet : le modèle lit chaque octet dans le tampon
 *   au moment où il part. Fin : la tâche de fond abaisse LENS_CS quand elle voit le dernier octet sorti, ou
 *   200 µs après l'avoir trouvé pas encore sorti [É].
 *   Réception [É] : tout octet reçu est écrit dans un anneau de 3072 octets, BODY_CS haute ou basse ; le front
 *   montant de BODY_CS ouvre l'emplacement, le front descendant le clôt : la trame y commence au début de
 *   l'emplacement et sa longueur est son champ de longueur, pas le compte d'octets reçus ; l'écriture repart à
 *   la fin de cette longueur (au début de l'anneau s'il en reste moins de 501 octets). Aucune échéance de
 *   réception.
 *   Séquence de la classe 1 (§ 2.5) [É] : une trame reçue de classe 1 pose le compteur du flux à sa séquence ;
 *   chaque routine de trame en flux l'incrémente, et le 0x05/0x06 en porte l'octet bas. Valeur initiale 0xffff.
 *   PARAMÉTRÉ ([NÉ]) : boot_us, hs_us (délais du séquenceur), lens_cs_lead_us (les boucles vides ont un
 *   compte établi, pas une durée), lens_cs_tail_us (latence de la boucle de fond, plafonnée par le repli de
 *   200 µs compté à partir du premier passage qui trouve l'octet pas encore sorti), tx_poll_us, loop_us, defer_us.
 *
 * SIMPLIFICATIONS DÉCLARÉES :
 *   S1 — une latence par tâche de fond (loop_us, defer_us, tx_poll_us) ; l'ordre des tâches de la boucle n'est
 *        pas suivi. Les tâches de fond passent dans l'ordre : émission, réception, réponse différée, homing.
 *   S2 — poignée de main : l'objectif est dans le mode de liaison série avec le boîtier (§ 6.2, § 10 : le niveau d'une
 *        broche au démarrage, bas [I fort] ; cette broche est sa ligne de réception série [É]) et sa ligne de
 *        réception est haute dès qu'il l'attend (l'interface ne porte pas son niveau). Les échéances de la première
 *        étape (de l'ordre de 15 s [I]) ne sont pas suivies : sans BODY_CS haute, il attend.
 *   S3 — le moteur de mise au point va à vitesse constante (steps_per_s) vers sa cible, sans rampe ; le servo
 *        est « posé » quand la position atteint la cible. La cible du moteur est celle du travail, bornée
 *        par les butées dures. Le travail mis au repos après son accusé arrête le moteur là où il est (sa
 *        vitesse commandée mise à 0, § 4.1) [I] ; le travail d'arrêt (0x1C) garde la position (§ 4.5).
 *   S4 — ce que le flux publie : seuls les champs de mise au point que tamron.md § 2.5 nomme sont calculés —
 *        0x05 offset 22 (bits 6 et 7), 0x06 offsets 0 et 1 (bloc 1), 0x06 offsets 2-10 (bloc 2 : position,
 *        offset 6, limites logicielles). Tout autre octet des blocs garde la valeur de la RAM remise à zéro :
 *        non modélisé (§ 9). La position publiée est la position mécanique, pas « la position commandée de deux
 *        ticks plus tôt » (§ 2.5).
 *   S5 — l'iris n'est pas modélisé : il se réduit à ce que le flux publie, et le modèle n'en publie aucun
 *        champ. Le 0x03 est accepté sans effet ; le 0x1B pose l'état « en attente », et la condition sur le
 *        module d'iris qui rend sa réponse prête est un délai (iris_1b_us) ; sa réponse porte r1b (§ 5.4).
 *   S6 — le 0x1D n'est modélisé qu'en unités 0 (pas, absolu ou relatif), et refusées (1 et 2 en absolu,
 *        1 et 3 en relatif) ; sans attente du cycle (bit 3), sans oscillation (bits 4-5, offset 2), arrivée
 *        directe (bits 6-7 à 0 ou 3). Le reste (unité 3 en absolu, 2 en relatif, attente du cycle, oscillation,
 *        arrivée imposée) est compté non modélisé : le travail est posé (il évince le précédent, § 4.4) puis
 *        abandonné, sans mouvement ni accusé.
 *   S7 — dans une interruption de trame, la tâche de construction du flux passe avant la routine de trame,
 *        et la prise en charge d'un travail (tick moteur) après les deux, au même instant.
 *   S8 — le homing ne lance aucun travail : son référencement dure homing_ref_us, puis la position saute à
 *        home_pos si la mise au point en était (bit 3 du 0x10). Un 0x1C ou un 0x1D pendant un homing met le
 *        modèle hors modèle (§ 3 : interaction [NÉ]).
 *   S9 — l'objectif ne reçoit pas sa propre émission (émission et réception sur deux lignes ; s'ils
 *        partageaient un fil, chaque réponse décalerait la trame suivante du boîtier, et aucune ne passerait) [I]. */
#include "lens_std.h"

#include <string.h>

#define MS 1000u

/* ── Les réponses constantes, reconstruites des champs que tamron.md § 1.2 publie ; tout octet que tamron.md
 *    déclare non publié vaut zéro ── */
/* 0x01 : la carte des types de tamron.md § 1.2 (01-0D, 10, 14-17, 19, 1B-1D, 1F, 22, 28, 2E, 2F, 34, 35, 3A-3D, 3F),
 * offsets 0-7, par la règle de protocol.md § 7.1 */
static const uint8_t R01[33] = {0x01, 0xFF, 0x9F, 0x78, 0x5D, 0x82, 0x60, 0x18, 0x5E};
/* 0x07 : offset 0 = 01, offsets 1-2 = 03 70 (protocol.md § 7.7), 5-6 = la version 3.01 en petit-boutiste,
 * 8 = A0, 9-10 = LensType2 0xC134, 15-18 = la constante de protocol.md § 7.7 (tamron.md § 1.2) */
static const uint8_t R07[35] = {
    0x07, 0x01, 0x03, 0x70, 0x00, 0x00, 0x01, 0x03, 0x00, 0xA0, 0x34, 0xC1, 0x00, 0x00, 0x00, 0x00, 0x60, 0x92,
    0x86, 0x5E};
/* 0x08 : offsets 0-3 = la plage d'ouverture, f/2,9 (0x1312) à f/22,6 (0x1900) ; 27-30 et 85 bit 7, écrits par
 * handle() (tamron.md § 1.2) */
static const uint8_t R08[202] = {0x08, 0x12, 0x13, 0x00, 0x19};
static const char NAME3F[] = "E 24mm F2.8 F051"; /* 0x3F, offsets 1-64, puis des zéros (§ 1.1, § 1.2) */
/* 0x0A : le masque de capacités, champ par champ (§ 1.2, protocol.md § 7.10) : offsets 0-2, les 18 blocs du 0x05
 * (bits 0-17) ; offset 8, les 8 blocs du 0x06 ; tout autre offset nul */
static const uint8_t MASK0A[17] = {0x0A, 0xFF, 0xFF, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF};
static const uint8_t SIZE05[19] = {1, 8, 1, 8, 3, 10, 28, 2, 6, 1, 2, 8, 6, 6, 6, 1, 6, 3, 3}; /* protocol.md § 7.5 */
static const uint8_t SIZE06[9] = {1, 2, 11, 9, 4, 6, 7, 45, 4};  /* protocol.md § 7.6 */

/* ── Gardes (§ 2.2) et longueurs consommées (protocol.md § 4.1) ── */
enum { G_ZERO, G_INIT, G_FREE, G_FLOW, G_NONE };
static uint8_t guard(uint8_t t)
{
    switch (t) {
    case 0x01: case 0x07: case 0x08: case 0x09: case 0x0B: case 0x0C: case 0x0D: case 0x10: case 0x14:
    case 0x15: case 0x16: case 0x19: case 0x1B: case 0x26: case 0x28: case 0x34: case 0x3D: case 0x3E:
    case 0x3F:
        return G_INIT;                    /* phase init et état libre */
    case 0x0A: case 0x35:
        return G_FREE;                    /* état libre, toute phase */
    case 0x03: case 0x1C: case 0x1D: case 0x1F: case 0x22: case 0x2E: case 0x2F: case 0x3C:
        return G_FLOW;                    /* phase de flux */
    case 0x04: case 0x3A: case 0x3B:
        return G_NONE;                    /* aucune garde */
    default:
        return G_ZERO;                    /* toujours refusés */
    }
}

static uint16_t consumed(uint8_t t)
{
    switch (t) {
    case 0x01: return 33;  case 0x03: return 21;   case 0x04: return 14; case 0x07: return 2;
    case 0x08: return 9;   case 0x09: return 5;    case 0x0A: return 17; case 0x0B: return 3;
    case 0x0C: return 2;   case 0x0D: return 2;    case 0x10: return 2;  case 0x15: return 1032;
    case 0x16: return 2;   case 0x19: return 2;    case 0x1B: return 6;  case 0x1C: return 1;
    case 0x1D: return 5;   case 0x1F: return 14;   case 0x22: return 3;  case 0x26: return 2;
    case 0x28: return 2;   case 0x2E: return 3;    case 0x2F: return 3;  case 0x34: return 24;
    case 0x35: return 2;   case 0x3A: return 24;   case 0x3B: return 8;  case 0x3C: return 8;
    case 0x3D: return 24;  case 0x3E: return 9;    case 0x3F: return 2;
    default:   return 0;   /* 0x14 : ne revient pas (§ 6.1) */
    }
}

/* ─────────────────────────── paramètres ─────────────────────────── */

void lstd_params_default(lstd_params_t *p)
{
    memset(p, 0, sizeof *p);
    p->byte_us = 13;              /* 10 bits (8N1) à 750 000 bauds = 13,3 µs (protocol.md § 1.2) */
    /* Des boucles vides d'un compte établi (§ 2.8) : environ 50 à 65 µs si un tour vaut 1,3 à 1,6 µs [I]. */
    p->lens_cs_lead_us = 60;
    /* La tâche de fond rabaisse LENS_CS au premier passage qui voit le dernier octet sorti : sa latence n'est pas
     * lisible [NÉ]. 0 est ce qu'un Sony réel montre (protocol.md § 1.4) ; les tests jouent aussi plusieurs
     * centaines de µs, comme le 135. */
    p->lens_cs_tail_us = 0;
    p->tx_poll_us = 0;            /* [NÉ] */
    p->boot_us = 20 * MS;         /* [NÉ] */
    p->hs_us = 2 * MS;            /* [NÉ] : des boucles vides et la configuration du port série */
    p->loop_us = 500;             /* [NÉ] ; ordre de grandeur des réponses d'init d'autres objectifs */
    p->defer_us = 500;            /* [NÉ] */
    p->service_us = 50 * MS;      /* [NÉ] */
    p->frame_period_us = 0;       /* le front descendant d'une entrée, la VD [I] ; l'horloge interne [NÉ] (§ 2.5) */
    p->q05_us = 1 * MS;           /* [NÉ] : les instants de ces interruptions ne sont pas lus */
    p->q06_us = 3 * MS;           /* [NÉ] ; plus tard que le 0x05 et son émission (97 octets, 1,3 ms) */
    p->homing_busy_us = 5 * MS;   /* [NÉ] : une attente active d'un nombre fixe de tours (§ 3) */
    /* [NÉ] ; le Sony 55 mm répond 10 00 environ 0,9 s après le 0x10 (nex7-sony-55mm-init.txt de LexOptical, lignes
     * 20-21, protocol.md § 0) */
    p->homing_ref_us = 600 * MS;
    p->homing_fails = false;      /* [NÉ] */
    p->iris_1b_us = 30 * MS;      /* [NÉ] */
    p->serial = 0;                /* [NÉ] : calculé de huit chiffres ASCII de la mémoire de l'objectif (§ 1.2) */
    p->steps_per_s = 20000;       /* [NÉ] : 350 pas par tick moteur (§ 4.1), tick [NÉ] */
    p->lim_low = 16384;           /* [I] : lus dans la flash, jamais mesurés (§ 4.3) */
    p->lim_high = 22743;
    p->soft_low = 16384;          /* [NÉ] : pris égal aux bornes de la cible (§ 4.3) */
    p->soft_high = 22743;
    p->soft_margin = 0;           /* [NÉ] */
    p->hard_low = 15027;          /* [I] : § 4.3 */
    p->hard_high = 24216;
    p->initial_position = 16384;  /* [NÉ] */
    p->home_pos = 16384;          /* [NÉ] */
}

/* ─────────────────────────── sorties ─────────────────────────── */

static void out_push(lstd_t *l, lstd_out_kind_t kind, uint8_t v)
{
    if (l->out_n == LSTD_OUT_CAP) {
        l->oom = true;
        l->oom_why = "file de sorties pleine (le banc ne la vide pas)";
        return;
    }
    l->out[(l->out_head + l->out_n) % LSTD_OUT_CAP] = (lstd_out_t){l->now, kind, v};
    l->out_n++;
}

static void set_lens_cs(lstd_t *l, bool high)
{
    if (l->lens_cs == high) return;
    l->lens_cs = high;
    l->t_lens_cs = l->now;
    out_push(l, LSTD_OUT_LENS_CS, high ? 1 : 0);
}

static void unmodelled(lstd_t *l, uint8_t type, const char *why)
{
    l->unmodelled[type]++;
    l->unmodelled_why = why;
}

static void out_of_model(lstd_t *l, const char *why)
{
    l->oom = true;
    l->oom_why = why;
}

static bool running(const lstd_t *l) { return l->powered && !l->blocked && !l->oom; }

/* ─────────────────────────── moteur (S3) ─────────────────────────── */

static int64_t mech(const lstd_t *l)
{
    uint64_t dist, span;
    if (!l->moving || l->f.frozen_position) return l->m0;
    span = (uint64_t)(l->dir > 0 ? l->target - l->m0 : l->m0 - l->target);
    dist = (l->now - l->t_m0) * l->p.steps_per_s / 1000000u;
    if (dist > span) dist = span;
    return l->m0 + l->dir * (int64_t)dist;
}

static void freeze(lstd_t *l)
{
    l->m0 = mech(l);
    l->t_m0 = l->now;
}

static void motor_to(lstd_t *l, int32_t t)
{
    freeze(l);
    if (t == l->m0) { l->moving = false; return; }
    l->moving = true;
    l->dir = t > l->m0 ? 1 : -1;
    l->target = t;
}

static uint64_t t_arrival(const lstd_t *l)
{
    uint64_t d;
    if (!l->moving || l->f.frozen_position || l->p.steps_per_s == 0) return UINT64_MAX;
    d = (uint64_t)(l->target > l->m0 ? l->target - l->m0 : l->m0 - l->target);
    return l->t_m0 + (d * 1000000u + l->p.steps_per_s - 1) / l->p.steps_per_s;
}

/* Drapeaux de limite (§ 2.5, 0x06 offset 0) : limite logicielle basse / haute (à la marge près), butée dure
 * basse / haute. */
static bool f_soft_low(const lstd_t *l, int64_t pos) { return pos <= (int64_t)l->p.soft_low + l->p.soft_margin; }
static bool f_soft_high(const lstd_t *l, int64_t pos) { return pos >= (int64_t)l->p.soft_high - l->p.soft_margin; }
static bool f_hard_low(const lstd_t *l, int64_t pos) { return pos <= l->p.hard_low; }
static bool f_hard_high(const lstd_t *l, int64_t pos) { return pos >= l->p.hard_high; }

/* ─────────────────────────── émission ─────────────────────────── */

/* Le constructeur de trame : en-tête, somme, 55, et l'unique trame en file (§ 2.8). Codes : 1 et 2 -> classe 1,
 * la séquence du flux (§ 2.5) ; 3 -> classe 2 ; 4 -> classe 3. */
static void queue(lstd_t *l, int b, int code, uint16_t len)
{
    uint8_t *f = l->buf[b];
    uint16_t sum = 0;
    if (len > LSTD_BUF) {
        out_of_model(l, "trame émise plus longue que son tampon");
        return;
    }
    f[0] = 0xF0;
    f[1] = (uint8_t)len;
    f[2] = (uint8_t)(len >> 8);
    f[3] = (uint8_t)(code <= 2 ? 1 : code - 1);
    f[4] = code <= 2 ? (uint8_t)l->seq : 0;
    for (uint16_t i = 1; i + 3 < len; i++) sum = (uint16_t)(sum + f[i]);
    f[len - 3] = (uint8_t)sum;
    f[len - 2] = (uint8_t)(sum >> 8);
    f[len - 1] = 0x55;
    if (l->pend >= 0) l->replaced++;           /* une seule trame en file : la précédente ne partira pas */
    l->pend = b;
    l->pend_len = len;
    l->t_pend = l->now;
}

static void reply(lstd_t *l, uint16_t n) { queue(l, LSTD_BREP, 3, (uint16_t)(n + 8)); }

/* Instant où la tâche d'émission peut lever LENS_CS pour la trame en file (§ 2.8). */
static uint64_t t_tx_start(const lstd_t *l)
{
    uint64_t t;
    if (l->pend < 0 || l->tx_state != 0 || l->lens_cs || l->body_cs) return UINT64_MAX;
    t = l->t_pend;
    if (l->buf[l->pend][3] != 1 && l->t_pend + l->f.delay_us > t) t = l->t_pend + l->f.delay_us;  /* panne */
    if (l->t_lens_cs + 501 > t) t = l->t_lens_cs + 501;      /* LENS_CS basse depuis plus de 500 µs */
    if (l->t_body_cs + 51 > t) t = l->t_body_cs + 51;        /* BODY_CS basse depuis plus de 50 µs */
    t += l->p.tx_poll_us;
    return t < l->busy_until ? l->busy_until : t;
}

static uint64_t t_byte_end(const lstd_t *l)  /* fin de l'octet tx_i */
{
    return l->t_tx + l->p.lens_cs_lead_us + (uint64_t)(l->tx_i + 1) * l->p.byte_us;
}

static uint64_t t_lens_cs_fall(const lstd_t *l)
{
    uint64_t t = l->t_tx + l->p.lens_cs_tail_us;  /* t_tx : fin du dernier octet, en état 3 */
    return t < l->busy_until ? l->busy_until : t;
}

static void tx_task(lstd_t *l)
{
    if (l->tx_state == 0 && l->pend >= 0 && l->now >= t_tx_start(l)) {
        int b = l->pend;
        l->pend = -1;
        if (l->f.silent) {
            l->silenced++;
            return;
        }
        l->tx = b;
        l->tx_len = l->pend_len;
        if (l->buf[b][3] != 1 && l->f.truncate_after && l->f.truncate_after < l->tx_len) l->tx_len = l->f.truncate_after;
        l->tx_i = 0;
        l->t_tx = l->now;
        set_lens_cs(l, true);
        l->tx_state = 1;
    } else if (l->tx_state == 3 && l->now >= t_lens_cs_fall(l)) {
        set_lens_cs(l, false);
        l->tx = -1;
        l->tx_state = 0;
    }
}

static void tx_byte(lstd_t *l)                  /* l'octet est lu dans le tampon quand il part */
{
    out_push(l, LSTD_OUT_BYTE, l->buf[l->tx][l->tx_i]);
    l->tx_i++;
    if (l->tx_i == l->tx_len) {
        l->t_tx = l->now;
        l->tx_state = 3;
    }
}

/* ─────────────────────────── flux (§ 2.4, § 2.5) ─────────────────────────── */

static bool granted(const uint8_t *r, int first, int k) { return (r[first + (k - 1) / 8] >> ((k - 1) & 7)) & 1; }

/* § 2.4 : au premier 0x0A seulement (octet de type encore nul), les blocs accordés sont posés bout à bout après
 * l'octet de type ; la fin des blocs est le pointeur d'écriture. */
static void layout(lstd_t *l, const uint8_t *r)
{
    uint16_t pos;
    if (l->buf[LSTD_B05][5] != 0x05) {
        l->buf[LSTD_B05][5] = 0x05;
        pos = 6;
        for (int k = 1; k < 19; k++) {
            l->pos05[k] = granted(r, 1, k) ? pos : 0;
            if (l->pos05[k]) pos = (uint16_t)(pos + SIZE05[k]);
        }
        l->end05 = pos;
    }
    if (l->buf[LSTD_B06][5] != 0x06) {
        l->buf[LSTD_B06][5] = 0x06;
        pos = 6;
        for (int k = 1; k < 9; k++) {
            l->pos06[k] = granted(r, 9, k) ? pos : 0;
            if (l->pos06[k]) pos = (uint16_t)(pos + SIZE06[k]);
        }
        l->end06 = pos;
        l->w06 = pos;                         /* les accusés écrits avant sont perdus */
    }
}

/* Ce que le flux publie (§ 2.5), réduit par S4. */
static void build_content(lstd_t *l)
{
    int64_t pos = mech(l);
    int sp = l->moving ? l->dir : 0;           /* le signe de la vitesse du travail [I] */
    uint8_t *b;
    if (l->pos05[5]) {                        /* bloc 5, octet 2 : 0x05 offset 22 */
        b = &l->buf[LSTD_B05][l->pos05[5] + 2];
        *b = (uint8_t)((*b & 0x3C) | (sp != 0) << 6 | (l->in_service) << 7);
    }
    if (l->pos06[1]) {                        /* bloc 1 : 0x06 offsets 0-1 */
        b = &l->buf[LSTD_B06][l->pos06[1]];
        b[0] = (uint8_t)((l->in_service ? (l->referenced ? 2 : 1) : 0) |
                         f_soft_high(l, pos) << 3 | f_soft_low(l, pos) << 4 |
                         f_hard_high(l, pos) << 5 | f_hard_low(l, pos) << 6);
        b[1] = (uint8_t)((sp > 0) << 1 | (sp < 0) << 2);   /* sens : croissant = vitesse positive [I] */
    }
    if (l->pos06[2]) {                        /* bloc 2 : 0x06 offsets 2-10 */
        b = &l->buf[LSTD_B06][l->pos06[2]];
        b[0] = (uint8_t)pos;
        b[1] = (uint8_t)(pos >> 8);
        b[2] = 0;
        b[3] = 0;
        b[4] = (uint8_t)((b[4] & 0xF0) | 0x10);
        b[5] = (uint8_t)l->p.soft_low;
        b[6] = (uint8_t)(l->p.soft_low >> 8);
        b[7] = (uint8_t)l->p.soft_high;
        b[8] = (uint8_t)(l->p.soft_high >> 8);
    }
}

/* L'accusé est écrit au pointeur d'écriture du 0x06 : après le 0x06 (§ 2.5, § 4.4). */
static void ack(lstd_t *l, uint8_t type, uint8_t v)
{
    if (l->w06 + 5u > LSTD_BUF) {
        out_of_model(l, "accusés en attente plus longs que le tampon du 0x06");
        return;
    }
    l->buf[LSTD_B06][l->w06] = type;
    l->buf[LSTD_B06][l->w06 + 1] = v;
    l->w06 = (uint16_t)(l->w06 + 2);
}

/* ─────────────────────────── travail de mise au point (§ 4) ─────────────────────────── */

enum { J_STOP, J_GOTO, J_REST };   /* le travail : arrêt (0x1C), 0x1D, repos */

static void job_rest(lstd_t *l)
{
    freeze(l);                                  /* vitesse commandée à 0 (S3) */
    l->moving = false;
    l->jtype = J_REST;
    l->jdone = true;
    l->jnew = l->jengaged = l->jerr = false;
}

/* Condition de fin (§ 4.1, § 4.3). */
static void finish_check(lstd_t *l)
{
    int64_t pos = mech(l);
    if (l->jdone) return;
    if (l->jengaged) {
        if (!l->moving && pos == l->jtarget) {
            l->jdone = true;                    /* servo posé et position = cible */
        } else if (l->jtype != J_REST) {
            if (f_hard_high(l, pos) || f_hard_low(l, pos)) l->jdone = true;       /* butée dure, tout sens */
            if (pos < l->jtarget && f_soft_high(l, pos)) l->jdone = true;         /* vers la limite haute */
            if (l->jtarget < pos && f_soft_low(l, pos)) l->jdone = true;          /* vers la limite basse */
        }
    }
    if (l->jtype == J_STOP) l->jdone = true;    /* l'arrêt est fini au premier contrôle */
}

/* Accusés (§ 4.4), au passage de la tâche de l'interruption de trame : le travail courant fini est accusé puis
 * mis au repos. Les accusés en attente et l'origine du travail ne sont pas suivis : avec un seul travail, venu du
 * boîtier, et sans la sauvegarde du travail par un 0x3C (non modélisé), ils ne décident rien d'autre que « le
 * travail courant est accusé ». */
static void post_acks(lstd_t *l)
{
    if (l->jtype == J_STOP && l->jdone) {
        ack(l, 0x1C, 0x01);
        job_rest(l);
    }
    if (l->jtype == J_GOTO && l->jdone) {
        ack(l, 0x1D, l->jerr ? 0x01 : 0x00);
        job_rest(l);
    }
}

/* Prise en charge au tick moteur (§ 4.1) et décodage du 0x1D (§ 4.2). */
static void pickup(lstd_t *l)
{
    int64_t pos = mech(l);
    uint8_t fl = l->jmsg[4];
    uint16_t value = (uint16_t)(l->jmsg[1] | l->jmsg[2] << 8);
    uint8_t unit = fl & 3;
    int32_t t;
    if (l->jdone) return;
    if (l->jtype == J_STOP) {                   /* l'arrêt garde la position (§ 4.5) */
        motor_to(l, (int32_t)pos);
        l->jnew = false;
        return;
    }
    if (l->jtype != J_GOTO || !l->jnew) return;
    l->jnew = false;
    if ((fl & 0x38) || l->jmsg[3] || (fl >> 6) == 1 || (fl >> 6) == 2 ||
        (!(fl & 4) && unit == 3) || ((fl & 4) && value != 0 && unit == 2)) {
        unmodelled(l, 0x1D, "0x1D : attente du cycle, oscillation, arrivée imposée ou unité convertie (S6)");
        job_rest(l);
        return;
    }
    if (!(fl & 4)) {                            /* absolu */
        if (unit != 0) l->jerr = true;          /* 1 et 2 refusées */
        t = unit == 0 ? value : (int32_t)pos;
    } else if (value == 0) {                    /* relatif nul : la position, sans décodage */
        t = (int32_t)pos;
    } else {                                    /* relatif : 1 et 3 refusées */
        int16_t s = (int16_t)value;
        int32_t steps = s < 0 ? -(int32_t)s : s;
        if (unit != 0) l->jerr = true;
        t = s < 0 ? (int32_t)pos - steps : (int32_t)pos + steps;
        if (t < 0) t = 0;
        if (t > 0xFFFF) t = 0xFFFF;
    }
    if (l->jerr) {                              /* unité refusée : fini sans mouvement, 1D 01 */
        l->jdone = true;
        return;
    }
    if (t < l->p.lim_low) t = l->p.lim_low;     /* bornée sans erreur (§ 4.3) */
    if (t > l->p.lim_high) t = l->p.lim_high;
    l->jtarget = (uint16_t)t;
    l->jengaged = true;
    /* § 4.3 : en butée dure, ou en limite logicielle du côté de la cible, le travail finit au premier
     * contrôle sans mouvement [I fort] */
    if (f_hard_low(l, pos) || f_hard_high(l, pos) || (t > pos && f_soft_high(l, pos)) || (t < pos && f_soft_low(l, pos)))
        return;
    if (t < l->p.hard_low) t = l->p.hard_low;   /* S3 : la consigne du moteur bornée aux butées dures */
    if (t > l->p.hard_high) t = l->p.hard_high;
    motor_to(l, t);
}

/* ─────────────────────────── interruption de trame (§ 2.5) ─────────────────────────── */

static void frame_tick(lstd_t *l)
{
    if (!running(l) || !l->link) return;
    if (l->in_service) {                        /* la tâche de l'interruption de trame (S7) */
        finish_check(l);
        build_content(l);
        post_acks(l);
    }
    if (l->phase == LSTD_FLOW) {                /* la routine de trame, en flux */
        l->seq++;
        l->arm05 = l->arm06 = true;
        l->t_q05 = l->now + l->p.q05_us;
        l->t_q06 = l->now + l->p.q06_us;
    }
    pickup(l);                                  /* le tick moteur, armé en fin de routine */
}

/* Les interruptions qui mettent le flux en file : longueur = pointeur d'écriture + 3, puis le pointeur revient à
 * la fin des blocs accordés (§ 2.5). Celle du 0x05 se coupe elle-même, celle du 0x06 coupe les deux [É]. */
static void isr05(lstd_t *l)
{
    l->arm05 = false;
    queue(l, LSTD_B05, 1, (uint16_t)(l->end05 + 3));
}

static void isr06(lstd_t *l)
{
    l->arm05 = l->arm06 = false;
    queue(l, LSTD_B06, 2, (uint16_t)(l->w06 + 3));
    l->w06 = l->end06;
}

/* ─────────────────────────── le 0x02 (§ 2.3) ─────────────────────────── */

static void send_err(lstd_t *l, const uint8_t *f, uint8_t code)
{
    uint8_t *e = &l->buf[LSTD_BERR][5];
    e[0] = 0x02;
    e[1] = f[3];                              /* classe reçue, FF pour les codes 1 à 3 */
    e[2] = f[5];                              /* type du premier sous-message, FF s'il est le fautif */
    e[3] = code;
    e[4] = e[5] = e[6] = e[7] = 0;
    queue(l, LSTD_BERR, 4, 16);
    l->rstate = LSTD_FREE;                    /* efface une réponse différée (§ 2.3) */
}

/* ─────────────────────────── handlers (§ 2.2) ─────────────────────────── */

static void start_job(lstd_t *l, int type, const uint8_t *m)
{
    if (l->hstep >= 0) {
        out_of_model(l, "0x1C ou 0x1D pendant un homing : interaction non établie (§ 3, S8)");
        return;
    }
    /* Le nouveau travail écrase le précédent sans le consulter ; l'évincé n'a pas d'accusé (§ 4.1, § 4.4). */
    l->jtype = type;
    l->jdone = l->jengaged = l->jerr = false;
    l->jnew = true;
    if (type == J_GOTO) memcpy(l->jmsg, m, 5);
}

/* Rend le nombre d'octets consommés, 0 pour un refus (0x02 code 4). */
static uint16_t handle(lstd_t *l, uint8_t *m, uint32_t rem)
{
    uint8_t t = m[0], g = guard(t);
    uint8_t *r = &l->buf[LSTD_BREP][5];
    uint16_t n;
    if (g == G_ZERO || (g == G_INIT && (l->phase != LSTD_INIT || l->rstate != LSTD_FREE)) ||
        (g == G_FREE && l->rstate != LSTD_FREE) || (g == G_FLOW && l->phase != LSTD_FLOW))
        return 0;
    if (t == 0x14) {                          /* quitte l'application (§ 6.1) */
        l->blocked = true;
        return 0;
    }
    n = consumed(t);
    if (n > rem) {
        out_of_model(l, "sous-message plus long que la trame : le pavage lit hors de la trame (§ 2.2)");
        return 0;
    }
    switch (t) {
    case 0x01: memcpy(r, R01, 33); reply(l, 33); break;
    case 0x03: break;                         /* iris (S5) */
    case 0x04: break;                         /* range sept champs (§ 4.9), aucun publié par le modèle */
    case 0x07: memcpy(r, R07, 35); reply(l, 35); break;
    case 0x08:                                /* § 1.2 */
        memcpy(r, R08, 202);
        r[86] = (uint8_t)((r[86] & 0x7F) | (m[1] & 0x80));      /* offset 85 : bit 7 de l'offset 0 de la requête */
        r[28] = (uint8_t)l->p.serial;                           /* offsets 27-30 */
        r[29] = (uint8_t)(l->p.serial >> 8);
        r[30] = (uint8_t)(l->p.serial >> 16);
        r[31] = (uint8_t)(l->p.serial >> 24);
        reply(l, 202);
        break;
    case 0x09: memset(r, 0, 12); r[0] = 0x09; reply(l, 12); break;     /* § 1.2 : rien de publié, zéros */
    case 0x0A: {                              /* § 2.4 */
        if (l->phase == LSTD_INIT) {
            l->phase = LSTD_FLOW;
        } else {
            l->arm05 = l->arm06 = false;      /* le flux s'arrête */
            l->phase = LSTD_INIT;
        }
        r[0] = MASK0A[0];
        for (int i = 1; i < 17; i++) r[i] = MASK0A[i] & m[i];
        layout(l, r);
        reply(l, 17);
        break;
    }
    case 0x0B: r[0] = 0x0B; r[1] = m[1]; r[2] = 0; reply(l, 3); break;  /* § 1.2 */
    case 0x0D: r[0] = 0x0D; r[1] = 0x00; reply(l, 2); break;           /* § 1.2 */
    case 0x10:                                /* § 3 */
        if (m[1] & 0x01) unmodelled(l, 0x10, "0x10 bit 0 : le troisième module (§ 3), non modélisé");
        l->hmask = m[1] & 0x0C;
        l->hstep = 0;
        break;
    case 0x19:                                /* § 5.4 : 19 00, différée */
        r[0] = 0x19; r[1] = 0x00;
        l->rstate = LSTD_READY; l->rtype = 0x19; l->t_rstate = l->now;
        break;
    case 0x1B:                                /* § 5.4 : en attente */
        l->rstate = LSTD_WAIT; l->rtype = 0x1B; l->t_rstate = l->now;
        break;
    case 0x1C: start_job(l, J_STOP, m); break;
    case 0x1D: start_job(l, J_GOTO, m); break;
    case 0x3F:                                /* § 1.2 : l'offset 0 n'est pas écrit */
        r[0] = 0x3F;
        memset(r + 2, 0, 64);
        memcpy(r + 2, NAME3F, sizeof NAME3F - 1);
        reply(l, 66);
        break;
    case 0x0C: out_of_model(l, "0x0C : changement de débit de la liaison (§ 5.3)"); break;
    case 0x16: out_of_model(l, "0x16 : fin de session, effets [NÉ] (§ 5.6)"); break;
    case 0x1F: case 0x3C: case 0x34:
        out_of_model(l, "travail de mise au point 0x1F, 0x34 ou 0x3C : non modélisé (§ 4)"); break;
    case 0x35: out_of_model(l, "0x35 : arrête le flux sans changer de phase (§ 5.5)"); break;
    default:
        unmodelled(l, t, "type accepté hors de ce qui est servi, sans état observable");
        break;
    }
    return n;
}

/* Le répartiteur (§ 2.2, § 2.3), sur la trame en place dans l'anneau. */
static void dispatch(lstd_t *l, uint32_t off)
{
    uint8_t *f = &l->ring[off];
    uint16_t len = (uint16_t)(f[1] | f[2] << 8), sum = 0;
    uint8_t code = 0;
    int32_t rem;
    uint32_t p = 5;
    if (len < 8 || off + len > LSTD_RING) {
        out_of_model(l, "longueur de trame hors de l'emplacement : le répartiteur lit hors de la trame (§ 2.2)");
        return;
    }
    for (uint16_t i = 1; i + 3 < len; i++) sum = (uint16_t)(sum + f[i]);
    if (f[len - 1] != 0x55) code = 1;                                    /* dans cet ordre (§ 2.3) */
    else if (f[0] != 0xF0) code = 2;
    else if ((f[len - 3] | f[len - 2] << 8) != sum) code = 3;
    if (code) {
        f[3] = 0xFF;                                                     /* classe et type FF */
        f[5] = 0xFF;
        send_err(l, f, code);
        return;
    }
    if (f[3] == 1) l->seq = f[4];                                        /* § 2.5 */
    rem = len - 8;
    while (rem != 0) {
        uint16_t n;
        if (f[p] == 0 || f[p] > 0x3F) {                                  /* type 0, ou au-delà de la borne */
            f[p] = 0xFF;
            send_err(l, f, 4);
            return;
        }
        n = handle(l, &f[p], (uint32_t)rem);
        if (!running(l)) return;
        if (n == 0) {                                                    /* le handler refuse */
            f[p] = 0xFF;
            send_err(l, f, 4);
            return;
        }
        rem -= n;
        p += n;
    }
}

/* ─────────────────────────── réponses différées (§ 2.6) ─────────────────────────── */

static void continuation(lstd_t *l)
{
    uint8_t *r = &l->buf[LSTD_BREP][5];
    if (l->rstate == LSTD_WAIT && l->rtype == 0x1B && l->p.iris_1b_us != UINT64_MAX &&
        l->now >= l->t_rstate + l->p.iris_1b_us) {
        l->rstate = LSTD_READY;                 /* la condition sur le module d'iris (S5) */
        l->t_rstate = l->now;
    }
    if (l->rstate != LSTD_READY || l->now < l->t_rstate + l->p.defer_us) return;
    switch (l->rtype) {
    case 0x10:                                  /* § 3 : 10 00, bit 0 = erreur */
        r[0] = 0x10;
        r[1] = (uint8_t)(l->p.homing_fails ? 0x01 : 0x00);
        reply(l, 2);
        break;
    case 0x19:                                  /* § 5.4 : n'écrit que l'offset 0 */
        r[1] &= 0xF8;
        reply(l, 2);
        break;
    case 0x1B:                                  /* § 5.4 (S5) : l'offset 9 réduit à ses bits 0-2 */
        r[0] = 0x1B;
        memcpy(r + 1, l->p.r1b, 10);
        r[10] &= 0x07;
        reply(l, 11);
        break;
    default:
        break;
    }
    l->rstate = LSTD_FREE;
}

/* ─────────────────────────── homing (§ 3) ─────────────────────────── */

static void homing_task(lstd_t *l)
{
    switch (l->hstep) {
    case 0:                                     /* attend que les modules soient en service */
        if (l->in_service) l->hstep = 1;
        break;
    case 1:                                     /* lance le référencement des modules du masque */
        l->t_hstep = l->now;
        l->hstep = 2;
        break;
    case 2:                                     /* le module est référencé (§ 2.5, 0x06 offset 0) */
        l->referenced = true;
        l->hstep = 3;
        break;
    case 3:                                     /* attente active : la boucle de fond est bloquée */
        l->busy_until = l->now + l->p.homing_busy_us;
        l->hstep = 4;
        break;
    case 4:                                     /* servo posé, puis la réponse prête (S8) */
        if (l->now < l->t_hstep + l->p.homing_ref_us || l->moving) break;
        if (l->hmask & 0x08) {
            l->m0 = l->p.home_pos;
            l->t_m0 = l->now;
        }
        l->rstate = LSTD_READY;                 /* sans regarder l'état précédent (§ 2.6) */
        l->rtype = 0x10;
        l->t_rstate = l->now;
        l->hstep = -1;
        break;
    default:
        break;
    }
}

/* ─────────────────────────── boucle de fond ─────────────────────────── */

static void pass(lstd_t *l)
{
    if (!running(l) || l->now < l->busy_until) return;
    tx_task(l);
    if (l->q_n && l->now >= l->q_t[0] + l->p.loop_us) {
        uint32_t off = l->q_off[0];
        l->q_n--;
        memmove(l->q_off, l->q_off + 1, l->q_n * sizeof l->q_off[0]);
        memmove(l->q_t, l->q_t + 1, l->q_n * sizeof l->q_t[0]);
        dispatch(l, off);
        if (!running(l) || l->now < l->busy_until) return;
    }
    continuation(l);
    homing_task(l);
}

static void settle(lstd_t *l)
{
    for (int i = 0; i < 12; i++) pass(l);
}

/* Le séquenceur de démarrage : poignée de main et mise en service. */
static void sequencer(lstd_t *l)
{
    switch (l->hs) {
    case 0:
        if (l->now >= l->t_power + l->p.boot_us) l->hs = 1;
        break;
    case 1:                                     /* attend BODY_CS haute, lève LENS_CS (§ 2.8) */
        if (l->body_cs) {
            set_lens_cs(l, true);
            l->t_hs = l->now;
            l->hs = 2;
        }
        break;
    case 2:
        if (l->now >= l->t_hs + l->p.hs_us) l->hs = 3;
        break;
    case 3:                                     /* BODY_CS basse -> réception armée, LENS_CS basse */
        if (!l->body_cs) {
            l->link = true;
            l->dma = l->off = 0;
            set_lens_cs(l, false);
            l->t_link = l->now;
            l->hs = 4;
        }
        break;
    default:
        break;
    }
    if (l->link && !l->in_service && l->now >= l->t_link + l->p.service_us) l->in_service = true;
}

static void fire_due(lstd_t *l)
{
    if (!running(l)) return;
    sequencer(l);
    if (l->tx_state == 1 && l->now >= t_byte_end(l)) {
        l->tx_state = 2;
        tx_byte(l);
    } else if (l->tx_state == 2 && l->now >= t_byte_end(l)) {
        tx_byte(l);
    }
    if (l->arm05 && l->now >= l->t_q05) isr05(l);
    if (l->arm06 && l->now >= l->t_q06) isr06(l);
    if (l->p.frame_period_us && l->link && l->now >= l->t_frame) {
        l->t_frame = l->now + l->p.frame_period_us;
        frame_tick(l);
    }
    if (t_arrival(l) <= l->now) {
        freeze(l);
        l->moving = false;
    }
}

/* ─────────────────────────── API ─────────────────────────── */

uint64_t lstd_next_event(const lstd_t *l)
{
    uint64_t t = UINT64_MAX;
#define CAND(x) do { uint64_t c_ = (x); if (c_ > l->now && c_ < t) t = c_; } while (0)
#define CAND_DUE(x) do { uint64_t c_ = (x); if (c_ < t) t = c_ < l->now ? l->now : c_; } while (0)
    if (!running(l)) return t;
    if (l->hs == 0) CAND(l->t_power + l->p.boot_us);
    if (l->hs == 2) CAND(l->t_hs + l->p.hs_us);
    if (l->link && !l->in_service) CAND(l->t_link + l->p.service_us);
    if (l->tx_state == 1 || l->tx_state == 2) CAND_DUE(t_byte_end(l));
    if (l->arm05) CAND_DUE(l->t_q05);
    if (l->arm06) CAND_DUE(l->t_q06);
    if (l->p.frame_period_us && l->link) CAND_DUE(l->t_frame);
    CAND_DUE(t_arrival(l));
    CAND(l->busy_until);
    CAND(t_tx_start(l));
    if (l->tx_state == 3) CAND(t_lens_cs_fall(l));
    if (l->q_n) CAND(l->q_t[0] + l->p.loop_us);
    if (l->rstate == LSTD_READY) CAND(l->t_rstate + l->p.defer_us);
    if (l->rstate == LSTD_WAIT && l->rtype == 0x1B && l->p.iris_1b_us != UINT64_MAX) CAND(l->t_rstate + l->p.iris_1b_us);
    if (l->hstep == 4) CAND(l->t_hstep + l->p.homing_ref_us);
#undef CAND
#undef CAND_DUE
    return t;
}

void lstd_advance(lstd_t *l, uint64_t t)
{
    for (;;) {
        uint64_t te = lstd_next_event(l);
        if (te > t || te == UINT64_MAX) break;
        l->now = te;
        fire_due(l);
        settle(l);
        sequencer(l);
    }
    if (t > l->now) l->now = t;
}

/* Tout l'état entre `powered` et les sorties vaut 0 au démarrage : l'objectif remet sa RAM à zéro (analyse
 * statique privée) ; les valeurs non nulles sont posées ici (§ 2.1, § 2.5). BODY_CS est une entrée : son niveau
 * reste. */
static void reset_ram(lstd_t *l)
{
    bool body = l->body_cs;
    memset((char *)l + offsetof(lstd_t, powered), 0, offsetof(lstd_t, out) - offsetof(lstd_t, powered));
    l->body_cs = body;
    l->t_body_cs = l->t_lens_cs = l->now;
    l->phase = LSTD_INIT;                       /* phase init, état libre (§ 2.1) */
    l->rstate = LSTD_FREE;
    l->seq = 0xFFFF;                            /* § 2.5 */
    l->hstep = -1;
    job_rest(l);
    l->pend = -1;
    l->tx = -1;
    l->m0 = l->p.initial_position;
    l->t_m0 = l->now;
}

void lstd_init(lstd_t *l, const lstd_params_t *p)
{
    memset(l, 0, sizeof *l);
    l->p = *p;
    reset_ram(l);
}

void lstd_power(lstd_t *l, uint64_t t, bool on)
{
    lstd_advance(l, t);
    if (on && !l->powered) {
        reset_ram(l);
        l->powered = true;
        l->t_power = t;
        l->t_frame = t;
    } else if (!on && l->powered) {
        set_lens_cs(l, false);
        l->powered = false;
    }
    sequencer(l);
    settle(l);
}

void lstd_start_powered(lstd_t *l, uint64_t t, bool flow, const uint8_t grant[16])
{
    lstd_advance(l, t);
    reset_ram(l);
    l->powered = true;
    l->t_power = t;
    l->t_frame = t;
    l->hs = 4;
    l->link = true;
    l->t_link = t;
    l->in_service = true;
    l->referenced = true;                       /* un homing a eu lieu */
    if (flow) {
        uint8_t r[17] = {0x0A};
        for (int i = 1; i < 17; i++) r[i] = MASK0A[i] & grant[i - 1];
        layout(l, r);
        l->phase = LSTD_FLOW;
    }
}

void lstd_body_cs(lstd_t *l, uint64_t t, bool high)
{
    lstd_advance(l, t);
    if (l->body_cs != high) {
        l->body_cs = high;
        l->t_body_cs = l->now;
        if (running(l) && l->link) {
            if (high) {
                l->rx_open = true;              /* front montant : ouvre l'emplacement (§ 2.8) */
            } else if (l->rx_open) {            /* front descendant : le clôt */
                uint16_t len = (uint16_t)(l->ring[l->off + 1] | l->ring[l->off + 2] << 8);
                l->rx_open = false;
                if (l->q_n == LSTD_RX_SLOTS) {
                    out_of_model(l, "huit trames reçues en attente : l'emplacement réutilisé n'est pas établi");
                } else if (len & 0x8000) {
                    out_of_model(l, "longueur négative : l'écriture repartirait avant l'anneau");
                } else {
                    l->q_off[l->q_n] = l->off;
                    l->q_t[l->q_n] = l->now;
                    l->q_n++;
                    l->off += len;
                    if (l->off <= LSTD_RING && LSTD_RING - l->off < 501) l->off = 0;   /* § 2.8 */
                    l->dma = l->off;            /* l'écriture repart ici */
                }
            }
        }
    }
    sequencer(l);
    settle(l);
}

void lstd_byte(lstd_t *l, uint64_t t, uint8_t b)
{
    lstd_advance(l, t);
    if (running(l) && l->link) {                /* l'octet est écrit, BODY_CS haute ou basse */
        if (l->dma >= LSTD_RING) out_of_model(l, "écriture au-delà de l'anneau de réception [I]");
        else l->ring[l->dma++] = b;
    }
    settle(l);
}

void lstd_vd(lstd_t *l, uint64_t t)             /* le front VD : l'interruption de trame (§ 2.5) */
{
    lstd_advance(l, t);
    frame_tick(l);
    settle(l);
}

bool lstd_out(lstd_t *l, lstd_out_t *o)
{
    if (l->out_n == 0) return false;
    *o = l->out[l->out_head];
    l->out_head = (l->out_head + 1) % LSTD_OUT_CAP;
    l->out_n--;
    return true;
}

bool lstd_blocked(const lstd_t *l) { return l->blocked; }
bool lstd_out_of_model(const lstd_t *l) { return l->oom; }
uint32_t lstd_unmodelled(const lstd_t *l, uint8_t type) { return l->unmodelled[type]; }
int32_t lstd_position(const lstd_t *l) { return (int32_t)mech(l); }
