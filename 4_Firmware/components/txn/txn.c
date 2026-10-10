/* SOURCE: spec de l'atelier § 3.3 — TRANSACTION (voir include/bsk_txn.h)
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — C pur, sans ESP-IDF : compilé aussi par les tests hôte
 *
 * La boucle 0x03/0x04 et les requêtes, émises par bench_core. */
#include "bsk_txn.h"

#include <string.h>

#include "bsk_bench_core.h"
#include "bsk_journal.h"

/* Un seul appelant, sur une seule tâche : ni verrou, ni quarantaine des réponses tardives. Une réponse tardive de
 * l'objectif précédent, au type de la première requête de la session suivante, la terminerait. Aucune attente que
 * LENS_CS retombe avant d'émettre.
 *
 * La paire est une unité (bsk_txn.h) : ce qui est émis entre son 0x03 et son 0x04 est retenu (held) et part derrière le
 * 0x04, dans l'ordre, avant le compte rendu ; derrière la place du 0x04 aussi quand il est jeté, hors de sa fenêtre ; la
 * boucle arrêtée n'en émet rien. Elle ne s'arrête qu'à l'oubli de la
 * session et à la mise au repos des lignes (session.c) : ce qui était retenu, une écriture CUSTOM comprise, ne part pas
 * vers un objectif qu'on abandonne.
 *
 * Les réponses du 135 et du Tamron partent seules dans leur trame (classe 2) : le pavage des messages de l'objectif,
 * dont les tailles dépendent de la mise en page du flux (0x05, 0x06), n'est pas nécessaire pour les reconnaître. */

/* La phase de la paire, celle du NEX-7 : médianes mesurées sur le boîtier, 0x03 à VD + 8,58 ms et 0x04 à VD + 10,10 ms.
 * Ici et nulle part ailleurs. */
#define PAIR03_US 8600u
#define PAIR04_US 10100u
#define HELD_MAX  4u   /* émissions retenues entre le 0x03 et le 0x04 : au-delà, refusées (E_BUSY) */
/* La fenêtre d'une trame de la paire : elle n'est émise que si le pas qui la sert a commencé au plus 2 ms après son
 * échéance (décision de l'humain du 2026-10-07 : pas de rattrapage). Deux tours de la boucle de la carte, qui rend la main
 * un tick (1 ms) entre deux pas (main.c) : un pas qui commence jusqu'à environ 1 ms après l'échéance est la marche
 * ordinaire ; au-delà de 2 ms, le pas a été retenu ailleurs (une écriture en flash, la file de la PHY en retard), et la
 * trame partirait hors phase, en rafale avec celles d'autres fronts. La fenêtre se juge au début du pas (on_time), pas à
 * l'émission : le temps passé dans le pas avant la trame (jusqu'à STEP_EVENTS événements, session.c ; l'attente de fin
 * d'émission de chaque trame émise avant elle, phy.c) n'est pas compté, et aucune borne de l'instant d'émission n'en
 * découle. Ce qui en découle : d'un front daté avant le début du pas et plus ancien qu'un autre front ainsi daté, la
 * paire est toujours hors de sa fenêtre (16,7 ms entre deux fronts à 60 Hz, plus que 10,1 + 2 ms) ; de ces fronts, seul
 * le plus récent est servi. */
#define PAIR_WINDOW_US 2000u

/* Au démarrage : IDLE, boucle arrêtée, séquence 0, le 0x03 de repos, rien d'accroché, aucun observateur. Les champs à
 * zéro de la paire sont posés au-dessus : offsets 3-6 du 0x03 (la consigne d'ouverture) par SESSION, offset 12 (le bit
 * AF) par MOUVEMENT ; le bit 1 de l'offset 3 du 0x04 (le mode AF du boîtier) par SESSION. Pas d'écho 0x2F derrière le
 * 0x03 : bench_core le refuse. */
static struct {
    bsk_txn_state_t state;
    uint8_t         type;       /* type attendu en WAIT */
    uint64_t        deadline;
    bsk_err_t       err;
    bsk_frame_t     reply;
    bool            loop;
    bsk_bench_lens_t lens;                     /* la déclaration de SESSION, transmise à bench_core */
    uint8_t         seq;
    uint8_t         m03[BSK_TXN_LOOP03_LEN];   /* le 0x03, avec les champs que l'étage du dessus y a posés */
    uint8_t         m04[BSK_MSG_MAX];          /* le 0x04 (BSK_TXN_LOOP04_LEN), puis le message accroché */
    uint16_t        att_len;                   /* 0 : rien d'accroché */
    bool            att_failed;                /* un 0x04 qui le portait tenté et raté (bsk_txn_attach_failed) */
    bsk_txn_pair_fn observer;
    uint64_t        at03, at04;                /* les échéances de la paire, UINT64_MAX sans paire */
    struct {
        uint8_t  cls;                          /* 1 : bsk_txn_send ; 2 : la requête, son échéance comptée au départ */
        uint16_t len;
        uint32_t timeout_ms;
        uint8_t  msg[BSK_MSG_MAX];
    } held[HELD_MAX];                          /* ce qui attend le 0x04, dans l'ordre */
    uint8_t         n_held;
    bool            req_held;                  /* la requête en WAIT n'est pas encore partie */
    uint64_t        now;                       /* le dernier instant reçu (tick, événement, requête) */
    uint64_t        real;                      /* l'instant réel du pas de l'appelant (bsk_txn_real) */
    bsk_txn_late_t  late;                      /* DEBUG : les paires jetées, hors de leur fenêtre sur `real` */
} x = {.at03 = UINT64_MAX, .at04 = UINT64_MAX,
       .m03 = {   /* le 0x03 de repos, puis le 0x04 */
                   0x03, 0xC2, 0x2E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00,
                   0x06, 0x00, 0x00, 0x02, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00},
       .m04 = {0x04, 0x00, 0x00, 0x19, 0x81, 0x00, 0x00, 0x3D, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00}};

static void seq_advance(void) { x.seq = (uint8_t)(x.seq >= 0xEF ? 0 : x.seq + 1); }

static bsk_err_t emit(uint8_t cls, const uint8_t *msg, uint16_t len)
{
    bsk_frame_t f = {.cls = cls, .seq = x.seq, .len = len};
    bsk_err_t e;
    memcpy(f.msg, msg, len);
    e = bsk_bench_send(&f, x.lens);
    if (e == E_OK) bsk_journal_tx(&f);   /* ce qui est parti sur le fil */
    else if (e == E_FORBIDDEN) bsk_journal_refused(&f);   /* tout refus, retenu ou non (release n'en lit pas l'issue) */
    return e;
}

void bsk_txn_samyang(bsk_bench_lens_t lens) { x.lens = lens; }

/* La requête part : WAIT, l'échéance comptée depuis l'émission ; ou FAILED. */
static void request_out(const uint8_t *msg, uint16_t len, uint32_t timeout_ms, uint64_t now_us)
{
    bsk_err_t e = emit(2, msg, len);
    x.req_held = false;
    if (e != E_OK) {
        x.state = TXN_FAILED;
        x.err = e;
        return;
    }
    seq_advance();
    x.type = msg[0];
    x.deadline = now_us + (uint64_t)timeout_ms * 1000u;
    x.state = TXN_WAIT;
}

/* Le 0x03 est parti et le 0x04 pas encore : rien ne passe entre eux. */
static bool between(void) { return x.at03 == UINT64_MAX && x.at04 != UINT64_MAX; }

/* Retenue jusqu'au 0x04 ; une requête retenue remplace la précédente, retenue ou non (bsk_txn_request). Faux : file pleine. */
static bool hold(uint8_t cls, const uint8_t *msg, uint16_t len, uint32_t timeout_ms)
{
    uint8_t i = x.n_held;
    if (cls == 2)
        for (uint8_t k = 0; k < x.n_held; k++)
            if (x.held[k].cls == 2) i = k;
    if (i == HELD_MAX) return false;
    x.held[i].cls = cls;
    x.held[i].len = len;
    x.held[i].timeout_ms = timeout_ms;
    memcpy(x.held[i].msg, msg, len);
    if (i == x.n_held) x.n_held++;
    return true;
}

/* Ce qui attendait le 0x04 part, dans l'ordre. Une émission retenue ratée n'est pas reprise ; celle d'une requête la met
 * en FAILED, à lire au pas suivant comme une échéance. */
static void release(uint64_t now_us)
{
    for (uint8_t k = 0; k < x.n_held; k++) {
        if (x.held[k].cls == 2) request_out(x.held[k].msg, x.held[k].len, x.held[k].timeout_ms, now_us);
        else if (emit(1, x.held[k].msg, x.held[k].len) == E_OK) seq_advance();
    }
    x.n_held = 0;
}

static void at(uint64_t now_us)
{
    if (now_us > x.now) x.now = now_us;
}

bsk_txn_state_t bsk_txn_request(const uint8_t *msg, uint16_t len, uint32_t timeout_ms, uint64_t now_us)
{
    at(now_us);
    if (between()) {                    /* elle part derrière le 0x04 */
        if (!hold(2, msg, len, timeout_ms)) {
            x.state = TXN_FAILED;
            x.err = E_BUSY;
            return x.state;
        }
        x.req_held = true;
        x.type = msg[0];
        x.state = TXN_WAIT;
        return x.state;
    }
    request_out(msg, len, timeout_ms, now_us);
    return x.state;
}

bsk_err_t bsk_txn_send(const uint8_t *msg, uint16_t len)
{
    bsk_err_t e;
    if (between()) return hold(1, msg, len, 0) ? E_OK : E_BUSY;   /* il part derrière le 0x04 */
    e = emit(1, msg, len);
    if (e == E_OK) seq_advance();
    return e;
}

void bsk_txn_loop03(uint8_t off, uint8_t mask, uint8_t bits)
{
    x.m03[1 + off] = (uint8_t)((x.m03[1 + off] & ~mask) | (bits & mask));
}

void bsk_txn_loop04(uint8_t off, uint8_t mask, uint8_t bits)
{
    x.m04[1 + off] = (uint8_t)((x.m04[1 + off] & ~mask) | (bits & mask));
}

void bsk_txn_attach(const uint8_t *msg, uint16_t len)
{
    if (len) memcpy(x.m04 + BSK_TXN_LOOP04_LEN, msg, len);   /* len 0 : rien d'accroché, msg peut être NULL */
    x.att_len = len;
    x.att_failed = false;
}

bool bsk_txn_attach_failed(void) { return x.att_failed; }

void bsk_txn_loop_observer(bsk_txn_pair_fn fn) { x.observer = fn; }

/* Boucle arrêtée : la paire en attente ne part pas, ni rien de ce qui attendait son 0x04 ; une requête retenue finit en
 * FAILED, E_ABORTED, sans avoir été émise. Le message accroché reste à l'étage du dessus (drive_forget). */
void bsk_txn_loop(bool on)
{
    x.loop = on;
    if (on) return;
    x.at03 = x.at04 = UINT64_MAX;
    x.n_held = 0;
    if (x.req_held) {
        x.req_held = false;
        x.state = TXN_FAILED;
        x.err = E_ABORTED;
    }
}

/* La paire finie, partie ou non : ce qui attendait le 0x04 émis, puis le compte rendu si elle est partie. */
static void pair_end(bool sent, uint64_t now_us)
{
    x.at03 = x.at04 = UINT64_MAX;
    release(now_us);
    if (sent && x.observer) x.observer();
}

/* La trame de la paire due à `due` est-elle dans sa fenêtre ? Jugée contre l'instant réel du pas, pas contre `now_us` de
 * bsk_txn_tick : la SESSION y pose l'échéance même qu'elle rattrape (session.c, run), et toute trame y paraît à l'heure. Un
 * instant réel avant l'échéance (un événement daté après le début du pas) : à l'heure. Hors de sa fenêtre, la paire est
 * jetée : comptée, son retard relevé. */
static bool on_time(uint64_t due)
{
    uint64_t d = x.real > due ? x.real - due : 0;
    if (d <= PAIR_WINDOW_US) return true;
    x.late.n++;
    if (d > x.late.max_us) x.late.max_us = (uint32_t)d;   /* tronqué au-delà de 2^32 µs, 71 minutes */
    return false;
}

/* Les trames de la paire échues à `now_us`, sous un seul numéro. Le 0x03 hors de sa fenêtre ou raté : pas de 0x04. Le 0x04
 * hors de sa fenêtre : jeté, le 0x03 parti seul. Une émission ratée n'est pas reprise. Le message accroché n'est retiré que
 * par un 0x04 parti : sinon, la paire suivante le porte. Un 0x04 qui le portait, tenté et raté, a pu partir quand même
 * (E_BUS de PHY : l'attente de fin d'émission dépassée, phy.c ; toute émission ratée est tenue pour telle) : c'est noté
 * (bsk_txn_attach_failed) ; un 0x04 jeté hors de sa fenêtre, ou jamais tenté derrière un 0x03 raté, ne l'est pas. */
static void pair_due(uint64_t now_us)
{
    if (x.at03 <= now_us) {
        bool out = on_time(x.at03) && emit(1, x.m03, sizeof x.m03) == E_OK;
        x.at03 = UINT64_MAX;
        if (!out) pair_end(false, now_us);
    }
    if (x.at04 <= now_us) {
        bool tried = on_time(x.at04);
        bool sent = tried && emit(1, x.m04, (uint16_t)(BSK_TXN_LOOP04_LEN + x.att_len)) == E_OK;
        if (sent) {
            seq_advance();
            x.att_len = 0;
        } else if (tried && x.att_len) {
            x.att_failed = true;
        }
        pair_end(sent, now_us);
    }
}
bool bsk_txn_loop_on(void) { return x.loop; }

void bsk_txn_on_event(const bsk_phy_event_t *e)
{
    at(e->t_us);
    switch (e->kind) {
    case BSK_PHY_VD:                    /* la paire de ce front (la précédente est finie : bsk_txn.h) */
        if (x.loop) {
            x.at03 = e->t_us + PAIR03_US;
            x.at04 = e->t_us + PAIR04_US;
        }
        break;
    case BSK_PHY_FRAME:
        if (x.state == TXN_WAIT && !x.req_held && e->u.frame.msg[0] == x.type) {
            x.reply = e->u.frame;
            x.state = TXN_DONE;
        }
        break;
    default:                            /* une erreur de réception ne coûte que la trame : la requête attend son échéance */
        break;
    }
}

void bsk_txn_tick(uint64_t now_us)
{
    at(now_us);
    pair_due(now_us);
    if (x.state == TXN_WAIT && !x.req_held && now_us >= x.deadline) {
        x.err = E_TIMEOUT;
        x.state = TXN_FAILED;
    }
}

uint64_t bsk_txn_deadline(void) { return x.state == TXN_WAIT && !x.req_held ? x.deadline : UINT64_MAX; }

uint64_t bsk_txn_next(void)
{
    uint64_t t = bsk_txn_deadline();
    if (x.at03 < t) t = x.at03;
    return x.at04 < t ? x.at04 : t;
}
bsk_txn_state_t bsk_txn_state(void) { return x.state; }
bsk_err_t bsk_txn_error(void) { return x.err; }
const bsk_frame_t *bsk_txn_reply(void) { return &x.reply; }
void bsk_txn_ack(void) { x.state = TXN_IDLE; }

void bsk_txn_real(uint64_t now_us) { x.real = now_us; }
bsk_txn_late_t bsk_txn_late(void) { return x.late; }
void bsk_txn_late_clear(void) { x.late = (bsk_txn_late_t){0}; }
