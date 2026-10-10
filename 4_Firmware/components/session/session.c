/* SOURCE: spec de l'atelier § 3.1 et § 8 — SESSION
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — C pur, sans ESP-IDF : compilé aussi par les tests hôte
 *
 * Le superviseur de la session (bsk_session.h), en machine à états explicite : une variable d'état publiée
 * (bsk_session_state_t), une étape dans l'état (step_t, interne), des transitions nommées (to_off, to_powering,
 * to_identifying, to_homing, to_ready, to_recovering, to_fault). Les bornes ne sont pas mesurées : l'objectif les publie
 * dans son 0x06, offsets 7-10. */
#include "bsk_session.h"

#include <string.h>

#include "bsk_journal.h"
#include "bsk_phy.h"
#include "bsk_txn.h"
#include "cmd.h"
#include "drive.h"
#include "init.h"
#include "lens_rx.h"
#include "motion.h"
#include "mark.h"
#include "restore.h"
#include "ring.h"
#include "still.h"

#define MS 1000u

#define RAIL_MS        50u
#define HS_LOW_MS      15u    /* l'état 1 de l'objectif se réinitialise après 10 ms */
#define HS_HOLD_MS     3u
#define AF_STOP_MS     100u
#define MAX_FAILS      4
#define VD_HZ          60u
#define ACK_CAP        8u     /* bsk_session.h : au plus quatre accusés non lus */
#define AP_OFF         3u     /* la consigne d'ouverture : offsets 3-4 et 5-6 du 0x03, octet de type exclu */
#define AP_F18         0x11B2u /* f/1,8, la consigne de chaque session */
#define STEP_EVENTS    64u    /* les événements de PHY traités par pas, au plus (audit M10) */
#define MARK_WAIT_MS   500u   /* la lecture de la marque attendue au plus ; choisie, non mesurée (le magasin de la carte lit
                                 un u32 en NVS ; SESSION lui rend la main à chaque tour) */

static const uint8_t HOME10[] = {0x10, 0x08};   /* bit 3 : la mise au point seule */

/* La poignée de main, sur les niveaux de LENS_CS : BODY_CS basse 15 ms, haute, LENS_CS attendue haute (déjà haute
 * comprise), 3 ms, BODY_CS basse, LENS_CS attendue basse, puis le 0x01 ; une échéance manquée mène aussi au 0x01, qui
 * tranche. Le niveau est lu au POWERING, puis suivi par ses changements : PHY ne publie que des fronts, et un Sony resté
 * alimenté tient LENS_CS haute avant la poignée de main. */
typedef enum {
    S_NONE,
    /* POWERING */
    S_RAIL_LOGIC,   /* rail logique posé : 50 ms */
    S_RAIL_MOTOR,   /* rail moteur posé : 50 ms */
    S_HS_LOW,       /* poignée de main : BODY_CS basse, 15 ms */
    S_HS_UP,        /* BODY_CS haute : LENS_CS attendue haute */
    S_HS_HOLD,      /* LENS_CS haute : 3 ms */
    S_HS_DOWN,      /* BODY_CS basse : LENS_CS attendue basse */
    /* IDENTIFYING */
    S_INIT,         /* une requête de l'init en vol (init.h) */
    S_FIRST05,      /* boucle lancée après l'init : premier 0x05, sans échec */
    S_PROBE05,      /* sonde standard : un 0x05 */
    /* HOMING */
    S_HOME_PAUSE,   /* 0x1C émis : 100 ms */
    S_HOME10,       /* 0x10 08 en vol */
    S_STILL,        /* immobilité */
    /* IDENTIFYING ou HOMING, la fin du démarrage */
    S_MARK,         /* la lecture de la marque attendue */
    /* READY, RESTORING */
    S_WATCH,        /* détecteur de perte armé */
    /* RECOVERING */
    S_REC_WAIT,     /* attente entre deux essais */
    S_REC_CUT,      /* rails coupés */
} step_t;

static struct {
    bsk_session_params_t p;
    bsk_session_state_t  state;
    step_t               step;
    uint64_t             now;
    uint64_t             due;         /* échéance de l'étape, UINT64_MAX sinon */
    bool                 vd_on;
    bool                 lens_cs;     /* lue au POWERING, puis suivie par BSK_PHY_LENS_CS */
    uint64_t             hs_body_up;  /* poignée de main : BODY_CS levée, pour le journal */
    uint64_t             hs_up_us;    /* LENS_CS vue haute, après BODY_CS levée */
    int                  fails;       /* échecs comptés consécutifs */
    bsk_err_t            last_error;

    uint16_t             ap;          /* la consigne d'ouverture posée dans le 0x03 */
    uint64_t             t05, t06;    /* S_WATCH : le dernier 0x05, le dernier 0x06 lus, ou l'entrée dans l'état */

    /* cette session : oublié par forget(), avec ce que l'objectif a publié (lens_rx.h) */
    still_t  home_still;              /* l'immobilité du homing de démarrage (S_STILL) */

    /* D2 et CMD_DETACH */
    bool     present;                 /* dernier état de D2 rapporté par PHY */
    bool     held;                    /* CMD_DETACH : OFF tenu jusqu'au prochain CMD_ATTACH */

    /* les accusés, dans l'ordre (bsk_session_ack) */
    bsk_ack_t acks[ACK_CAP];
    unsigned  ack_head, ack_n;

    /* DEBUG (bsk_session_debug), à zéro au démarrage de la carte : les événements de PHY datés avant le précédent */
    uint64_t  ev_t;                   /* l'instant du dernier événement reçu */
    uint32_t  ev_back, ev_back_us;
} s;

/* Les transitions, publiées dans l'instantané et au journal ; celles de MOUVEMENT le sont par motion.c. La sortie de
 * RESTORING et sa cause s'écrivent après la ligne de la transition : sous le plafond de 30 lignes par seconde, la
 * transition passe avant son détail (le démarrage du 135 sous LOG ON en écrit 29 dans sa première seconde). */
static void enter(bsk_session_state_t st)
{
    bool left = s.state == SESSION_RESTORING && st != SESSION_RESTORING;
    s.state = st;
    bsk_journal_session((uint8_t)st, s.last_error);
    if (left) bsk_journal_restore_end(st == SESSION_READY ? restore_cause() : bsk_state_name((uint8_t)st));
}

/* La consigne d'ouverture, un code 256(Av+16) petit-boutiste, deux fois (offsets 3-4 et 5-6 du 0x03) : le F051 lit l'une
 * ou l'autre selon le bit 7 de l'offset 7. Tenue par TRANSACTION pour chaque paire suivante. Non bornée ici : bornée à la
 * plage du 0x08 par session_aperture (cmd.c) ; celle de chaque session, f/1,8, l'est à la réponse au 0x08 (on_frame). */
void session_aperture_hold(uint16_t code)
{
    s.ap = code;
    for (uint8_t i = 0; i < 4; i++) bsk_txn_loop03((uint8_t)(AP_OFF + i), 0xFF, (uint8_t)(i & 1 ? code >> 8 : code));
}

uint16_t session_aperture_target(void) { return s.ap; }

/* Rien de collant d'une session à l'autre. */
static void forget(void)
{
    bsk_txn_loop(false);
    lens_rx_forget();
    motion_forget();
    ring_forget();                    /* le rôle et le bit du 0x04 */
    mark_forget();                    /* la clé, le sens, l'appui en cours */
    session_aperture_hold(AP_F18);
    init_forget();                    /* le reset soft, l'init faite, la reconnaissance */
    cmd_forget();                     /* CMD_LENS_CUSTOM en vol, ACK_FAILED */
}

/* ─────────────────────────── les accusés ─────────────────────────── */

static void ack(uint32_t seq, bsk_ack_result_t r, bsk_err_t why)
{
    if (s.ack_n == ACK_CAP) {                       /* hors contrat (bsk_session.h) : le plus ancien est perdu */
        s.ack_head = (s.ack_head + 1) % ACK_CAP;
        s.ack_n--;
    }
    s.acks[(s.ack_head + s.ack_n) % ACK_CAP] = (bsk_ack_t){.seq = seq, .result = r, .reason = why};
    s.ack_n++;
}

/* motion.h : MOUVEMENT rend ici les accusés de ses commandes. */
void session_ack(uint32_t seq, bsk_ack_result_t r, bsk_err_t why) { ack(seq, r, why); }

/* cmd.h : le goto du bouton lit son premier accusé, le dernier déposé. */
const bsk_ack_t *session_ack_last(void) { return &s.acks[(s.ack_head + s.ack_n - 1) % ACK_CAP]; }

/* ─────────────────────────── transitions ─────────────────────────── */

static void wait_for(step_t st, uint32_t ms)
{
    s.step = st;
    s.due = s.now + (uint64_t)ms * MS;
}

/* TXD, BODY_CS et VD au repos (haute impédance, tirées bas), avant que les rails ne soient coupés : une ligne haute vers
 * un objectif non alimenté l'alimenterait. Relâchée, BODY_CS retombe : une poignée de main interrompue ne la laisse pas
 * haute. Les lignes ne sont pilotées de nouveau qu'une fois les deux rails établis (S_RAIL_MOTOR). */
static void lines_rest(void)
{
    bsk_txn_loop(false);              /* la paire en attente ne part pas vers des lignes au repos */
    bsk_phy_lines(false);
    bsk_phy_vd(0);
    s.vd_on = false;
}

/* D2 absent : lignes au repos, rails coupés, session oubliée, compteur à zéro. */
static void to_off(void)
{
    motion_leave(E_ABORTED);          /* la retombée de D2, ou CMD_CLEAR_FAULT et CMD_DETACH (rien en vol) */
    enter(SESSION_OFF);
    s.step = S_NONE;
    s.due = UINT64_MAX;
    forget();
    lines_rest();
    bsk_phy_rail(BSK_RAIL_MOTOR, false);
    bsk_phy_rail(BSK_RAIL_LOGIC, false);
    s.fails = 0;
    s.last_error = E_OK;
}

/* Oubli, rail logique, 50 ms, rail moteur, 50 ms, VD, puis la poignée de main ; les lignes pilotées après la VD, rails
 * établis. Un rail, la VD ou les lignes refusés par le verrou de XDETECT (bsk_phy.h) ne sont pas lus ici : la retombée qui
 * l'a posé remonte en BSK_PHY_PRESENCE au pas suivant, et mène en OFF (on_event). */
static void to_powering(void)
{
    enter(SESSION_POWERING);
    s.last_error = E_OK;              /* la raison d'un FAULT quitté par CMD_ATTACH ne vaut plus */
    forget();
    s.lens_cs = bsk_phy_lens_cs();    /* déjà haute, aucun front ne le dira */
    bsk_phy_rail(BSK_RAIL_LOGIC, true);
    wait_for(S_RAIL_LOGIC, RAIL_MS);
}

/* Le 0x01, dans la milliseconde qui suit la poignée de main ; l'init (init.h) en vol. */
static void to_identifying(void)
{
    enter(SESSION_IDENTIFYING);
    s.step = S_INIT;
    s.due = UINT64_MAX;
    init_start(s.now);
}

/* Le homing du pilote : 0x1C, 100 ms, 0x10 08, immobilité. */
static void to_homing(void)
{
    enter(SESSION_HOMING);
    (void)drive_stop();
    wait_for(S_HOME_PAUSE, AF_STOP_MS);
}

/* Un flux est arrêté à son dernier message plus lost_ms, plus 1 µs : constaté après lost_ms passés sans lui. */
static uint64_t lost_us(void) { return (uint64_t)s.p.lost_ms * MS + 1; }

/* L'échéance du détecteur de perte : celle du premier des deux flux qui s'arrêterait, tant qu'elle n'est pas passée ;
 * passée (ce flux arrêté, ses données oubliées), celle de l'autre, la perte. */
static void watch_due(void)
{
    uint64_t d05 = s.t05 + lost_us(), d06 = s.t06 + lost_us();
    uint64_t lo = d05 < d06 ? d05 : d06, hi = d05 < d06 ? d06 : d05;
    s.due = lo > s.now ? lo : hi;
}

/* Le détecteur de perte, armé en READY et en RESTORING, nulle part ailleurs. Chaque flux a son échéance, réarmée à chaque
 * 0x05 ou 0x06 lu (on_frame). À l'armement, elles partent de l'entrée, jamais de la dernière télémétrie : elle précède
 * toujours l'entrée, et plus vieille que lost_ms (une fin de démarrage à l'échéance du homing), elle ferait échoir le
 * détecteur dans le passé. Un flux arrêté lost_ms dans l'état rend ses données invalides (lens_rx_stale), l'autre
 * courant ; une perte n'est constatée qu'après lost_ms passés dans l'état sans 0x05 ni 0x06. */
static void watch(void)
{
    s.step = S_WATCH;
    s.t05 = s.t06 = s.now;
    watch_due();
}

/* READY : le compteur revient à zéro. */
static void to_ready(void)
{
    enter(SESSION_READY);
    s.fails = 0;
    watch();
}

/* FAULT, terminal : la boucle et la VD restent ce qu'elles étaient ; seule la retombée de D2 en sort ici. */
static void to_fault(bsk_err_t why)
{
    s.last_error = why;
    enter(SESSION_FAULT);
    s.step = S_NONE;
    s.due = UINT64_MAX;
}

static void to_recovering(void)
{
    enter(SESSION_RECOVERING);
    wait_for(S_REC_WAIT, s.p.retry_wait_ms);
}

/* Un échec compté ; au 4e consécutif, FAULT avec E_LOST. */
static void fail_counted(void)
{
    if (++s.fails >= MAX_FAILS) to_fault(E_LOST);
    else to_recovering();
}

/* La fin d'un démarrage (l'init faite, ou le homing du pilote immobile) : la lecture de la marque de la clé courante
 * attendue (le magasin est asynchrone), et la position et les bornes d'un 0x06 (S_MARK, advance), MARK_WAIT_MS au plus,
 * dans l'état où elle est ; à l'échéance, pas de marque. */
static void boot_end(void) { wait_for(S_MARK, MARK_WAIT_MS); }

/* La lecture de la marque rendue ou échue : READY, ou RESTORING sur le trajet que trace restore.h, où une perte est un
 * échec compté comme en READY. `nomark` : le jeton du journal s'il n'y a pas de marque (`nomark`, ou `timeout` à
 * l'échéance de la lecture). */
static void boot_decide(const char *nomark)
{
    if (!restore_plan()) {
        to_ready();
        restore_skip(nomark);                       /* après la transition (enter) */
        return;
    }
    enter(SESSION_RESTORING);
    watch();                                        /* la perte, comme en READY : un échec compté (on_due) */
    restore_start(s.now);                           /* après la transition : la ligne `* restore`, le premier goto */
}

/* La sonde standard : un 0x05 est arrivé. Servi quelle que soit sa marque. */
static void served(void)
{
    if (init_done()) boot_end();                  /* pas de second homing */
    else to_homing();
}

/* Après une init commencée par un 0x01 répondu : l'objectif parle (un 0x01 muet ne mène jamais ici). */
static void probe(void)
{
    wait_for(S_PROBE05, s.p.probe05_ms);            /* un 0x05 déjà reçu sert tout de suite (advance) ;
                                                       sinon un échec compté à l'échéance */
}

/* Ce que l'init (init.h) dit de faire après une réponse ; INIT_ASKING : une requête de l'init en vol, rien. */
static void on_init(init_next_t n)
{
    switch (n) {
    case INIT_END:                                  /* la boucle lancée, premier 0x05 attendu */
        wait_for(S_FIRST05, s.p.first05_ms);
        break;
    case INIT_FAIL:                                 /* muet après le reset soft */
        fail_counted();
        break;
    case INIT_HOME_FAILED:
        to_fault(E_HOME_FAILED);
        break;
    default:
        break;
    }
}

/* HOMING : l'immobilité après le 0x10 08 mène à la fin du démarrage (still.h, le suivi du homing). */
static void still_check(void)
{
    if (still(&s.home_still, s.now)) boot_end();
}

/* ─────────────────────────── les réponses ─────────────────────────── */

/* Appelée en S_INIT ou en S_HOME10 seulement (asking). */
static void on_reply(bool ok, const bsk_frame_t *r, bsk_err_t why)
{
    if (s.step == S_INIT) {
        on_init(init_reply(ok, r, why, s.now));
    } else if (ok && !lens_rx_full(r)) {
        fail_counted();                              /* 0x10 08 sans son résultat : comme sans réponse */
    } else if (ok && lens_rx_home_failed(r)) {
        to_fault(E_HOME_FAILED);
    } else if (ok) {
        s.home_still.have = false;
        wait_for(S_STILL, s.p.still_ms);
        still_check();
    } else {
        fail_counted();                              /* 0x10 sans réponse en 20 s */
    }
}

static bool asking(void)
{
    return s.step == S_INIT || s.step == S_HOME10;
}

/* ─────────────────────────── les échéances ─────────────────────────── */

static void on_due(void)
{
    switch (s.step) {
    case S_RAIL_LOGIC:
        bsk_phy_rail(BSK_RAIL_MOTOR, true);
        wait_for(S_RAIL_MOTOR, RAIL_MS);
        break;
    case S_RAIL_MOTOR:
        if (!s.vd_on) {
            bsk_phy_vd(VD_HZ);
            s.vd_on = true;
        }
        bsk_phy_lines(true);                        /* rails établis ; TXD routée avant le 0x01 */
        bsk_phy_body_cs(false);
        wait_for(S_HS_LOW, HS_LOW_MS);
        break;
    case S_HS_LOW:
        bsk_phy_body_cs(true);
        s.hs_body_up = s.now;
        wait_for(S_HS_UP, s.p.handshake_ms);
        break;
    case S_HS_UP:                                   /* LENS_CS jamais haute : le 0x01 tranchera */
        bsk_journal_handshake(BSK_HS_NEVER_HIGH, 0);
        bsk_phy_body_cs(false);
        to_identifying();
        break;
    case S_HS_HOLD:
        bsk_phy_body_cs(false);
        wait_for(S_HS_DOWN, s.p.handshake_ms);
        break;
    case S_HS_DOWN:                                 /* LENS_CS restée haute : le 0x01 aussi */
        bsk_journal_handshake(BSK_HS_STUCK_HIGH, s.hs_up_us);
        to_identifying();
        break;
    case S_FIRST05:                                 /* pas un échec */
        probe();
        break;
    case S_PROBE05:                                 /* compté */
        fail_counted();
        break;
    case S_HOME_PAUSE:
        s.step = S_HOME10;
        s.due = UINT64_MAX;
        (void)bsk_txn_request(HOME10, sizeof HOME10, s.p.home_ms, s.now);   /* FAILED se lit au pas suivant */
        break;
    case S_STILL:                                   /* l'objectif ne s'immobilise pas */
        if (still(&s.home_still, s.now)) boot_end();
        else fail_counted();
        break;
    case S_MARK:                                    /* la lecture n'est pas rendue (pas de marque), ou pas de 0x06 */
        boot_decide("timeout");
        break;
    case S_WATCH:
        if (s.now < s.t05 + lost_us()) {   /* le 0x06 arrêté, le 0x05 court */
            lens_rx_stale(0x06);
            watch_due();
            break;
        }
        if (s.now < s.t06 + lost_us()) {   /* le 0x05 arrêté, le 0x06 court */
            lens_rx_stale(0x05);
            mark_frame(0x05);                       /* la clé de la marque, sans 0x05 : aucune */
            watch_due();
            break;
        }
        motion_leave(E_LOST);                       /* perte : la session est oubliée, échec compté ; un goto en vol */
        forget();
        fail_counted();
        break;
    /* La reprise ne coupe les rails que si cut_rails est vrai, faux par défaut (décision de l'humain, jusqu'à la carte à
     * rails commutés) : sinon elle repart de POWERING, qui repose les lignes au même niveau. Quand elle coupe, la VD est
     * arrêtée avec les lignes et relancée au POWERING suivant, après les rails : une VD haute pendant la coupure
     * alimenterait l'objectif par ses diodes de protection et annulerait la coupure. */
    case S_REC_WAIT:                                /* les rails : sans effet s'ils ne sont pas commutés */
        if (!s.p.cut_rails) {                       /* la coupure désactivée, POWERING tout de suite */
            to_powering();
            break;
        }
        lines_rest();                               /* VD comprise, relancée au POWERING suivant */
        bsk_phy_rail(BSK_RAIL_MOTOR, false);
        bsk_phy_rail(BSK_RAIL_LOGIC, false);
        wait_for(S_REC_CUT, s.p.cut_ms);
        break;
    case S_REC_CUT:
        to_powering();
        break;
    default:                                        /* S_NONE, S_INIT, S_HOME10 : sans échéance, jamais échus */
        break;
    }
}

/* Un pas de la machine à l'instant s.now : une condition remplie, une réponse, ou une échéance. */
static bool advance(void)
{
    if (s.state == SESSION_OFF && s.present && !s.held) {   /* D2 présent ; pas après CMD_DETACH */
        to_powering();
        return true;
    }
    bsk_txn_tick(s.now);
    switch (s.step) {
    case S_HS_UP:
        if (s.lens_cs) {
            s.hs_up_us = s.now - s.hs_body_up;      /* 0 : déjà haute */
            wait_for(S_HS_HOLD, HS_HOLD_MS);
            return true;
        }
        break;
    case S_HS_DOWN:
        if (!s.lens_cs) {
            bsk_journal_handshake(BSK_HS_OK, s.hs_up_us);
            to_identifying();
            return true;
        }
        break;
    case S_FIRST05:
        if (lens_rx()->have05) {
            probe();
            return true;
        }
        break;
    case S_PROBE05:
        if (lens_rx()->have05) {
            served();
            return true;
        }
        break;
    case S_MARK:                                    /* la lecture de la marque rendue, et un 0x06 : */
        mark_poll();                                /* le premier 0x05 après l'init arrive avant le 0x06 de sa paire */
        if (!mark_loading() && lens_rx()->have06) {
            boot_decide("nomark");
            return true;
        }
        break;
    case S_WATCH:
        if (s.state == SESSION_RESTORING) {             /* restore.h : le dépassement arrivé, ou la sortie */
            restore_step_t r = restore_track(s.now);
            if (r == RESTORE_END) to_ready();
            if (r != RESTORE_WAIT) return true;
        }
        if (cmd_custom_poll()) return true;             /* la réponse à CMD_LENS_CUSTOM, en READY */
        break;
    default:
        if (asking() && (bsk_txn_state() == TXN_DONE || bsk_txn_state() == TXN_FAILED)) {
            bool ok = bsk_txn_state() == TXN_DONE;
            bsk_err_t why = bsk_txn_error();        /* la cause, en FAILED : lue avant l'accusé (bsk_txn.h) */
            bsk_txn_ack();
            on_reply(ok, bsk_txn_reply(), why);
            return true;
        }
        break;
    }
    if (motion_expire(s.now)) return true;          /* -> STALLED (motion.c) */
    if (s.now >= s.due) {
        on_due();
        return true;
    }
    return false;
}

static uint64_t next_due(void)
{
    uint64_t t = bsk_txn_next();                    /* les trames de la paire aussi */
    if (motion_due() < t) t = motion_due();
    return s.due < t ? s.due : t;
}

static void run(uint64_t t)
{
    for (;;) {
        uint64_t d;
        if (advance()) continue;
        d = next_due();
        if (d > t || d <= s.now) break;
        s.now = d;
    }
    if (t > s.now) s.now = t;
}

/* ─────────────────────────── les événements de PHY ─────────────────────────── */

/* La trame est décodée par lens_rx.c ; la SESSION lit ce qu'elle a publié (lens_rx.h). Un 0x07 n'y change rien : l'identité
 * et la reconnaissance ne se font que dans l'init (init.h). */
static void on_frame(const bsk_frame_t *f, uint64_t t)
{
    uint8_t type = lens_rx_frame(f);
    mark_frame(type);                               /* le sens, la clé */
    if (type == 0x08) session_aperture(s.ap);       /* la consigne de départ, f/1,8, ramenée à la plage connue */
    if (s.step == S_WATCH && (type == 0x05 || type == 0x06)) {
        if (type == 0x05) s.t05 = t;
        else s.t06 = t;
        watch_due();
    }
    if (s.step == S_STILL && (type == 0x05 || type == 0x06)) still_check();
    ring_frame(type, t, s.state == SESSION_READY && !motion_busy());   /* la bague servie en READY hors mouvement */
    motion_frame(type, s.now);
    if (type == 0x05) cmd_button(t);                /* après MOUVEMENT : une commande finie sur cette trame */
}

static void on_event(const bsk_phy_event_t *e)
{
    bsk_txn_on_event(e);
    if (e->kind == BSK_PHY_FRAME) bsk_journal_rx(&e->u.frame, e->t_us);
    else if (e->kind == BSK_PHY_ERROR) bsk_journal_error(e->u.err, &e->u.raw, e->t_us);
    switch (e->kind) {
    case BSK_PHY_PRESENCE:                          /* le démarrage est dans advance() */
        bsk_journal_xdetect(e->u.present, e->t_us);  /* avant la ligne `* session` qu'il cause */
        s.present = e->u.present;
        if (!s.present && s.state != SESSION_OFF) to_off();
        break;
    case BSK_PHY_BOUNCE:                            /* le journal seul, rien ne change */
        bsk_journal_xdetect_bounce(e->t_us);
        break;
    case BSK_PHY_LENS_CS:
        s.lens_cs = e->u.level;
        break;
    case BSK_PHY_FRAME:
        on_frame(&e->u.frame, e->t_us);
        break;
    default:
        break;
    }
}

/* ─────────────────────────── bsk_session.h ─────────────────────────── */

void bsk_session_params_default(bsk_session_params_t *p)
{
    p->handshake_ms = 500;
    p->init_ms = 300;
    p->name_ms = 300;
    p->info_ms = 400;
    p->init_home_ms = 8000;
    p->home_ms = 20000;
    p->still_ms = 3000;
    p->first05_ms = 500;
    p->probe05_ms = 300;
    p->retry_wait_ms = 3000;
    p->cut_ms = 500;
    p->cut_rails = false;      /* décision de l'humain, jusqu'à la carte à rails commutés */
    p->lost_ms = 2000;
}

void bsk_session_init(const bsk_session_params_t *p)
{
    memset(&s, 0, sizeof s);
    s.p = *p;
    s.state = SESSION_OFF;   /* rien n'est émis, lignes au repos, VD arrêtée (bsk_phy_init les a laissées ainsi) */
    s.due = UINT64_MAX;
    motion_init();
    ring_init();
    mark_init();
    init_params(p);
    forget();
    bsk_txn_late_clear();
}

/* La PHY de la carte date une trame à son traitement et un front à son interruption, si bien qu'une trame peut entrer
 * dans la file avant un front daté plus tôt. Un événement daté avant le
 * précédent est compté, avec le plus grand écart vu, pour DEBUG ; rien d'autre n'en change. */
static void ev_order(uint64_t t)
{
    if (t < s.ev_t) {
        s.ev_back++;
        if (s.ev_t - t > s.ev_back_us) s.ev_back_us = (uint32_t)(s.ev_t - t);
    }
    s.ev_t = t;
}

/* Au plus STEP_EVENTS événements de PHY par pas : un flux de trames plus rapide que leur traitement (une tempête de 0x02
 * d'un Sony sous LOG ALL) ne retient pas le pas, qui rend la main à HOTE et à la LED. Le reste attend le pas suivant, une
 * milliseconde plus tard sur la carte, et ce qui est échu après le dernier événement traité aussi : une échéance n'est
 * jamais jugée avant un événement plus ancien (une réponse resterait derrière l'échéance de sa requête). La file de la
 * carte en tient 32 : un pas la vide, sauf si la PHY la remplit pendant qu'il la lit. */
void bsk_session_step(uint64_t now_us)
{
    bsk_phy_event_t e;
    unsigned n = 0;
    bsk_txn_real(now_us);    /* la fenêtre de la paire se juge contre lui (bsk_txn.h) */
    while (n < STEP_EVENTS && bsk_phy_poll(&e)) {
        n++;
        ev_order(e.t_us);
        run(e.t_us);         /* ce qui est échu avant l'événement */
        if (e.t_us > s.now) s.now = e.t_us;
        on_event(&e);
        run(e.t_us);
    }
    if (n < STEP_EVENTS) run(now_us);
    mark_poll();             /* ce que le magasin a rendu */
}

uint64_t bsk_session_next(void)
{
    uint64_t d = s.state == SESSION_OFF && s.present && !s.held ? s.now : next_due();   /* après une commande */
    return d < s.now ? s.now : d;
}

/* L'instantané. La marque : celle de l'objectif et de la focale courants, une fois relue. L'ouverture, en unités
 * objectif : le code relu au 0x05, la plage de la réponse au 0x08 ; sans 0x08, pas d'ouverture. CAP_LIMITS_REPORTED : les
 * bornes sont celles que l'objectif annonce dans son 0x06, jamais mesurées ; posé dès qu'un 0x06 est lu dans la session,
 * retiré quand leur flux s'arrête (lens_rx_stale). */
void bsk_session_status(bsk_status_t *st)
{
    const lens_rx_t *l = lens_rx();
    memset(st, 0, sizeof *st);
    st->session_state = (uint8_t)s.state;
    st->motion_state = (uint8_t)motion_state();
    st->position_valid = s.state == SESSION_READY && l->have06;
    st->focus_position = l->pos;                      /* 0 sans 0x06 */
    st->focus_min = l->fmin;                          /* valides comme la position (bsk_contract.h) ; 0 sans 0x06 */
    st->focus_max = l->fmax;
    st->lens_id_product = l->lens_type2;              /* le LensType2 du 0x07 ; 0 sans lui */
    st->aperture_current = l->ap;                     /* 0 sans 0x05 */
    st->aperture_min = l->ap_min;                     /* 0 sans 0x08 */
    st->aperture_max = l->ap_max;
    st->capabilities = l->have08 ? CAP_APERTURE | CAP_APERTURE_READBACK : 0;
    if (l->have06) st->capabilities |= CAP_LIMITS_REPORTED;
    memcpy(st->lens_name, l->name, sizeof st->lens_name);
    st->last_error = s.last_error;
    {
        bsk_mark_t m;
        st->mark_valid = mark_get(&m);
        st->mark_position = st->mark_valid ? m.position : 0;
    }
    st->mark_sets = mark_sets();
    st->mark_clears = mark_clears();
    st->stop_unconfirmed = motion_unconfirmed();
    st->ring = (uint8_t)ring_role();
    /* bits 0-1 de l'offset 62, 03 MF, 01 AF ; autre valeur (00 sans offset 62), inconnu. Offset 64, bit 4 : le
     * stabilisateur. */
    st->mf = (l->o62 & 3) == 3 ? BSK_YES : (l->o62 & 3) == 1 ? BSK_NO : BSK_UNKNOWN;
    st->oss = !l->have64 ? BSK_UNKNOWN : l->o64 & 0x10 ? BSK_YES : BSK_NO;
}

void bsk_session_debug(bsk_session_debug_t *d)
{
    bsk_phy_stats_t p;
    bsk_txn_late_t l = bsk_txn_late();
    bsk_phy_stats(&p);
    d->evq_drop = p.evq_drop;
    d->ev_back = s.ev_back;
    d->ev_back_us = s.ev_back_us;
    d->tx_wait_us = p.tx_wait_us;
    d->tx_timeout = p.tx_timeout;
    d->pair_late = l.n;
    d->pair_late_us = l.max_us;
}

/* ─────────────────────────── cmd.h : ce que la boîte de commandes lit et demande ─────────────────────────── */

bsk_session_state_t session_state(void) { return s.state; }
bsk_err_t session_last_error(void) { return s.last_error; }
uint64_t session_now(void) { return s.now; }

void session_attach(void)
{
    s.held = false;
    s.fails = 0;                                /* à tout démarrage demandé */
    if (s.present) to_powering();               /* D2 absent : OFF, le démarrage part avec D2 (advance) */
}

/* OFF tenu jusqu'au prochain CMD_ATTACH. */
void session_detach(void)
{
    to_off();
    s.held = true;
}

/* OFF non tenu, puis POWERING si D2 (advance). */
void session_clear_fault(void)
{
    if (s.state == SESSION_FAULT) to_off();
}

/* q en RESTORING : READY, le 0x1C parti ou non. */
void session_restore_stop(void)
{
    restore_stop();
    to_ready();
}

bool bsk_session_ack(bsk_ack_t *a)
{
    if (s.ack_n == 0) return false;
    *a = s.acks[s.ack_head];
    s.ack_head = (s.ack_head + 1) % ACK_CAP;
    s.ack_n--;
    return true;
}
