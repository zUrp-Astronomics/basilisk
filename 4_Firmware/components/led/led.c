/* SOURCE: PROTOCOL.md § 2 (`k`) — la LED de statut (voir include/bsk_led.h)
 * AUTHOR: engineer
 * DATE: 2026-10-03
 * STATUS: actif — C pur, sans ESP-IDF : compilé aussi par les tests hôte
 *
 * L'intensité de la LED, calculée de l'instantané courant et du précédent. Les motifs et leurs valeurs sont ici, en tête. */
#include "bsk_led.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define MS     1000u
#define L(pct) ((pct) * 100)     /* un niveau : pct % du plafond, en dix-millièmes */

/* ─────────────────────────── les motifs : toutes les valeurs, ici ─────────────────────────── */

/* Un motif : des segments joués à la suite, chacun une rampe linéaire de `from` à `to` en `ms` (un palier si from == to). */
typedef struct { uint16_t ms; int16_t from, to; } seg_t;
typedef struct { const seg_t *seg; size_t n; } motif_t;
#define MOTIF(a) {a, sizeof a / sizeof a[0]}

#define ON_MS      1000          /* carte sous tension : le fixe « ON », 100 % */
#define HOLD_MS    300           /* l'activité tenue après le dernier pas */
#define BREATH_LO  L(10)         /* READY : la respiration, plancher et plafond */
#define BREATH_HI  L(50)
#define BREATH_MS  6000          /*     sa période */
#define LIFE_HI    L(50)         /* OFF : le signe de vie, un souffle de LIFE_MS toutes les LIFE_EVERY_MS, en */
#define LIFE_MS    2000          /*     fin de période : le premier après le « ON » */
#define LIFE_EVERY 10000
#define FAULT_ON   300           /* FAULT : n éclats, une longue pause, en boucle */
#define FAULT_OFF  300
#define FAULT_REST 2000
#define FAULT_LOST 1             /*     n : objectif perdu (E_LOST) */
#define FAULT_HOME 3             /*     n : homing échoué (E_HOME_FAILED) */

/* Ponctuels : joués une fois. */
static const seg_t S_SEEN[] = {{400, L(100), L(100)}};                    /* objectif vu : un éclat long */
static const seg_t S_INIT_OK[] = {{500, 0, L(100)}};                      /* init OK : montée franche, puis rien */
static const seg_t S_ARRIVED[] = {{1000, L(100), L(100)}};                /* arrivé à la marque : fixe */
static const seg_t S_APERTURE[] = {{80, L(100), L(100)}};                 /* ouverture changée : un éclat bref */
static const seg_t S_MARK_SET[] = {{100, L(100), L(100)}, {150, 0, 0},    /* marque enregistrée : trois éclats */
                                   {100, L(100), L(100)}, {150, 0, 0},
                                   {100, L(100), L(100)}, {150, 0, 0}};
static const seg_t S_MARK_CLEAR[] = {{500, L(100), 0}};                   /* marque effacée : la descente d'init OK */
/* Activité, en boucle. */
static const seg_t S_IN[] = {{60, L(100), L(100)}, {140, 0, 0}};          /* IN, position croissante : courts, rapides */
static const seg_t S_OUT[] = {{350, L(100), L(100)}, {350, 0, 0}};        /* OUT, décroissante : longs, lents */
static const seg_t S_INIT[] = {{100, L(30), L(30)}, {900, 0, 0}};         /* init en cours : lent, faible */
static const seg_t S_RESTORE[] = {{40, L(100), L(100)}, {60, 0, 0}};      /* vers la marque : scintillement */
static const seg_t S_RECOVER[] = {{100, L(100), L(100)}, {100, 0, 0},     /* RECOVERING : double éclat, toutes les 2 s */
                                  {100, L(100), L(100)}, {1700, 0, 0}};
/* Fond, en boucle. */
static const seg_t S_BREATH[] = {{BREATH_MS / 2, BREATH_LO, BREATH_HI}, {BREATH_MS / 2, BREATH_HI, BREATH_LO}};
static const seg_t S_LIFE[] = {{LIFE_EVERY - LIFE_MS, 0, 0}, {LIFE_MS / 2, 0, LIFE_HI}, {LIFE_MS / 2, LIFE_HI, 0}};

static const motif_t M_SEEN = MOTIF(S_SEEN), M_INIT_OK = MOTIF(S_INIT_OK), M_ARRIVED = MOTIF(S_ARRIVED),
                     M_APERTURE = MOTIF(S_APERTURE), M_MARK_SET = MOTIF(S_MARK_SET), M_MARK_CLEAR = MOTIF(S_MARK_CLEAR),
                     M_IN = MOTIF(S_IN), M_OUT = MOTIF(S_OUT), M_INIT = MOTIF(S_INIT), M_RESTORE = MOTIF(S_RESTORE),
                     M_RECOVER = MOTIF(S_RECOVER), M_BREATH = MOTIF(S_BREATH), M_LIFE = MOTIF(S_LIFE);

/* ─────────────────────────── le calcul ─────────────────────────── */

/* Les familles de motif de l'état : POWERING, IDENTIFYING et HOMING n'en font qu'une, l'init. */
typedef enum { F_OFF, F_INIT, F_RESTORE, F_READY, F_RECOVER, F_FAULT } fam_t;

static struct {
    uint8_t        ceiling;        /* le plafond, en pour cent */
    uint64_t       t_boot;         /* le fixe « ON » */
    bool           seen;           /* un instantané précédent */
    bsk_status_t   prev;
    fam_t          fam;            /* la famille de l'état, depuis t_fam */
    uint64_t       t_fam;
    const motif_t *punct;          /* le ponctuel en cours, depuis t_punct ; NULL : aucun */
    uint64_t       t_punct;
    int8_t         dir;            /* le sens du dernier pas : 1 IN, -1 OUT, 0 aucun pas vu */
    uint64_t       t_act, t_step;  /* le début du motif d'activité, le dernier pas */
} d = {.ceiling = 100};            /* 100 % avant même bsk_led_init : `k` le lit tel quel */

static fam_t family(uint8_t s)
{
    switch (s) {
    case SESSION_POWERING:
    case SESSION_IDENTIFYING:
    case SESSION_HOMING: return F_INIT;
    case SESSION_RESTORING: return F_RESTORE;
    case SESSION_READY: return F_READY;
    case SESSION_RECOVERING: return F_RECOVER;
    case SESSION_FAULT: return F_FAULT;
    default: return F_OFF;
    }
}

/* Le niveau du motif `e` ms après son début ; un motif joué une fois (!loop) est fini au-delà de sa durée (*over). */
static int32_t level(const motif_t *m, uint64_t e, bool loop, bool *over)
{
    uint64_t total = 0;
    for (size_t i = 0; i < m->n; i++) total += m->seg[i].ms;
    if (loop) {
        e %= total;
    } else if (e >= total) {
        *over = true;
        return 0;
    }
    for (size_t i = 0; i < m->n; i++) {
        const seg_t *s = &m->seg[i];
        if (e < s->ms) return s->from + (int32_t)(s->to - s->from) * (int32_t)e / s->ms;
        e -= s->ms;
    }
    return 0;
}

/* FAULT : n éclats, la pause, en boucle. En FAULT, last_error vaut E_LOST ou E_HOME_FAILED, les deux seules raisons de
 * to_fault (session.c). */
static int32_t fault(bsk_err_t why, uint64_t e)
{
    uint32_t n = why == E_LOST ? FAULT_LOST : FAULT_HOME;
    e %= n * (FAULT_ON + FAULT_OFF) + FAULT_REST;
    return e < n * (FAULT_ON + FAULT_OFF) && e % (FAULT_ON + FAULT_OFF) < FAULT_ON ? L(100) : 0;
}

static bool active(uint64_t now) { return d.dir != 0 && now - d.t_step < (uint64_t)HOLD_MS * MS; }

static void punct(const motif_t *m, uint64_t now)
{
    d.punct = m;
    d.t_punct = now;
}

static bool measured(const bsk_status_t *s) { return (s->capabilities & CAP_LIMITS_REPORTED) != 0; }

/* Ce que la LED lit, et pourquoi :
 *   - la position n'est mesurée que si l'instantané porte CAP_LIMITS_REPORTED, posé exactement quand un 0x06 a été lu ;
 *     une variation de focus_position entre deux instantanés mesurés est un pas, IN si elle croît, OUT si elle décroît
 *     (tous les objectifs), dans tout état, goto, homing ou bague ; le passage depuis ou vers l'absence de mesure n'en est
 *     pas un ;
 *   - l'ouverture n'est lue que non nulle (le code vaut 256 × (Av + 16)) ; son changement, en READY ;
 *   - la marque : seulement les compteurs de gestes de l'instantané, jamais mark_valid ni mark_position, qu'un zoom ou une
 *     relecture changent sans geste ; en READY ;
 *   - la fin de RESTORING : « arrivé » si le premier instantané en READY porte MOTION_ARRIVED, sinon « init OK », comme
 *     toute autre entrée en READY ;
 *   - l'objectif vu : OFF -> POWERING (la présence n'est pas publiée ; c'est aussi le départ d'un `p1` ou d'un `b` depuis
 *     OFF, l'objectif présent) ; l'objectif retiré n'a pas de motif ;
 *   - FAULT : le code d'après last_error (FAULT_LOST, FAULT_HOME), pas la valeur de l'énumération. Le refus de
 *     bench_core n'a pas de motif : last_error n'est publié qu'en FAULT.
 * Ce qui a changé entre l'instantané précédent `p` et le courant `c` : plus loin dans la fonction, plus prioritaire, le
 * dernier ponctuel posé gagne. */
static void observe(const bsk_status_t *p, const bsk_status_t *c, uint64_t now)
{
    fam_t f = family(c->session_state);
    if (f != d.fam) {
        d.fam = f;
        d.t_fam = now;
    }
    if (f == F_FAULT) {
        d.punct = NULL;
        return;
    }
    if (p->session_state == SESSION_OFF && c->session_state == SESSION_POWERING) punct(&M_SEEN, now);
    if (p->session_state != SESSION_READY && c->session_state == SESSION_READY)
        punct(p->session_state == SESSION_RESTORING && c->motion_state == MOTION_ARRIVED ? &M_ARRIVED : &M_INIT_OK, now);
    if (measured(p) && measured(c) && c->focus_position != p->focus_position) {
        int8_t dir = c->focus_position > p->focus_position ? 1 : -1;
        if (!active(now) || dir != d.dir) d.t_act = now;
        d.dir = dir;
        d.t_step = now;
    }
    if (p->session_state == SESSION_READY && c->session_state == SESSION_READY) {
        if (p->aperture_current && c->aperture_current && c->aperture_current != p->aperture_current) punct(&M_APERTURE, now);
        if (c->mark_clears != p->mark_clears) punct(&M_MARK_CLEAR, now);
        if (c->mark_sets != p->mark_sets) punct(&M_MARK_SET, now);
    }
}

/* Les priorités, de la plus forte à la plus faible :
 *   0. le fixe « ON » du démarrage de la carte, puis on entre dans les motifs ;
 *   1. FAULT, exclusif : rien ne passe par-dessus ; un ponctuel en cours est oublié, aucun ne naît en FAULT ;
 *   2. un ponctuel : il interrompt le motif en cours, qui reprend à sa phase ; pas de file, le dernier gagne ; le même,
 *      ou un autre, pendant qu'il joue le relance depuis son début (une rafale se fusionne) ;
 *   3. l'activité de la mise au point, tenue HOLD_MS après le dernier pas ; son motif repart au premier pas, et à chaque
 *      changement de sens ;
 *   4. le motif de l'état, chacun depuis l'entrée dans sa famille : l'init (POWERING à HOMING, une seule famille), le
 *      retour à la marque (RESTORING), RECOVERING, la respiration (READY), le signe de vie (OFF).
 * Les niveaux sont en dix-millièmes du plafond (L(pct)) ; la sortie, en dix-millièmes du rapport cyclique plein, est le
 * niveau mis à l'échelle par le plafond. */
static int32_t intensity(const bsk_status_t *st, uint64_t now)
{
    bool over = false;
    int32_t v;
    if (now - d.t_boot < (uint64_t)ON_MS * MS) return L(100);
    if (d.fam == F_FAULT) return fault(st->last_error, (now - d.t_fam) / MS);
    if (d.punct) {
        v = level(d.punct, (now - d.t_punct) / MS, false, &over);
        if (!over) return v;
        d.punct = NULL;
    }
    if (active(now)) return level(d.dir > 0 ? &M_IN : &M_OUT, (now - d.t_act) / MS, true, &over);
    switch (d.fam) {
    case F_INIT: return level(&M_INIT, (now - d.t_fam) / MS, true, &over);
    case F_RESTORE: return level(&M_RESTORE, (now - d.t_fam) / MS, true, &over);
    case F_RECOVER: return level(&M_RECOVER, (now - d.t_fam) / MS, true, &over);
    case F_READY: return level(&M_BREATH, (now - d.t_fam) / MS, true, &over);
    default: return level(&M_LIFE, (now - d.t_fam) / MS, true, &over);
    }
}

void bsk_led_init(uint64_t now_us)
{
    memset(&d, 0, sizeof d);
    d.ceiling = 100;
    d.t_boot = now_us;
}

uint16_t bsk_led_eval(const bsk_status_t *st, uint64_t now_us)
{
    if (d.seen) {
        observe(&d.prev, st, now_us);
    } else {
        d.fam = family(st->session_state);
        d.t_fam = now_us;
        d.seen = true;
    }
    d.prev = *st;
    return (uint16_t)((uint32_t)intensity(st, now_us) * d.ceiling / 100u);
}

bool bsk_led_ceiling_set(unsigned pct)
{
    if (pct > 100) return false;
    d.ceiling = (uint8_t)pct;
    return true;
}

uint8_t bsk_led_ceiling(void) { return d.ceiling; }
