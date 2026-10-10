/* SOURCE: spec de l'atelier § 3.1 — IDENTIFYING, la séquence d'init
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — C pur, sans ESP-IDF : compilé aussi par les tests hôte
 *
 * Les requêtes d'init, leurs renvois, la reconnaissance Samyang et le lancement de la boucle (init.h). */
#include "init.h"

#include <string.h>

#include "bsk_journal.h"
#include "bsk_txn.h"
#include "lens_rx.h"

#define TRIES01        3      /* le 0x01, trois essais au total, avant le reset soft */
#define SENDS          3      /* un message ponctuel, trois envois au total au plus (décision de l'humain) */
#define LT2_135        8u     /* le LensType2 du Samyang AF 135, tracé dans son 0x07 */

/* Ce que le NEX-7 envoie à l'init. */
static const uint8_t INIT01[] = {0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01,
                                 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t INIT07[] = {0x07, 0x00};
static const uint8_t INIT3F[] = {0x3F, 0x00};
static const uint8_t INIT0B[] = {0x0B, 0x60, 0x00};
static const uint8_t INIT09[] = {0x09, 0x00, 0x00, 0x00, 0x00};
static const uint8_t INIT0D[] = {0x0D, 0x00};   /* 60 Hz */
static const uint8_t INIT10[] = {0x10, 0x1F};   /* homing : tout */
static const uint8_t INIT0A[] = {0x0A, 0xFF, 0x7F, 0, 0, 0, 0, 0, 0, 0x3F, 0, 0, 0, 0, 0, 0, 0};
/* Le reset soft émet INIT0A tel quel : le Tamron accepte un 0x0A, son état de réponse libre, dans toute phase, et un 0x0A
 * en flux le ramène en init, flux coupé, où l'init est acceptée (tamron.md § 2.2, § 2.4) ; le 135 suspend son flux,
 * arrête ses moteurs, répond l'écho et rallume le flux, ce masque étant non nul (samyang.md § 2.3). Les documents cités
 * ici et plus bas sont ceux de 7_Docs/E-Mount/. */

/* La requête de l'init en vol. */
typedef enum {
    S_Q01, S_Q07, S_Q3F, S_Q08, S_Q0B, S_Q09, S_Q0D, S_Q10, S_Q0A,
    S_RESET0A,      /* reset soft : le 0x0A en vol */
} q_t;

static struct {
    bsk_session_params_t p;           /* les échéances des requêtes (init_params) */
    uint64_t             now;         /* l'instant que le superviseur a passé */
    q_t                  step;        /* la requête en vol */
    init_next_t          next;        /* ce que le superviseur fait après cette réponse (init_reply) */

    int                  tries01;     /* essais du 0x01 depuis le dernier démarrage de l'init */
    uint8_t              q[sizeof INIT01];   /* la dernière requête demandée (ask), telle quelle, */
    uint16_t             q_len;              /* ... sa longueur, */
    uint32_t             q_ms;               /* ... son échéance, */
    int                  sends;              /* ... et ses envois */

    /* cette session : oublié par init_forget(), avec ce que l'objectif a publié (lens_rx.h) */
    bool     reset_done;              /* le reset soft de cette session a eu lieu */
    bool     init_done;
    bool     samyang;
    bsk_bench_lens_t lens;            /* la déclaration à bench_core */
} in;

/* ─────────────────────────── la reconnaissance ─────────────────────────── */

/* Le nom 0x3F, ou le LensType2 dans la liste fermée des Samyang lus. Le Samyang AF 135, reconnu et de LensType2 8, est
 * déclaré comme tel : bench_core ne laisse qu'à lui l'exception du commutateur Custom. Un autre Samyang (le 24 mm, 0xC93A)
 * reste un Samyang reconnu. Refaite aux réponses de l'init au 0x07 et au 0x3F, et à l'oubli de la session, nulle part
 * ailleurs : la déclaration ne change pas en cours de session (un 0x07 non demandé n'est pas lu, lens_rx.h). */
static void init_recognize(void)
{
    static const uint16_t codes[] = {8, 9, 12, 13, 20, 21, 23, 0xC938, 0xC93A};
    const lens_rx_t *l = lens_rx();
    bool y = !strncmp(l->name, "SAMYANG ", 8) || !strncmp(l->name, "LK SAMYANG ", 11);
    for (size_t i = 0; !y && i < sizeof codes / sizeof codes[0]; i++) y = codes[i] == l->lens_type2;
    in.samyang = y;
    in.lens = !y ? BSK_BENCH_OTHER : l->lens_type2 == LT2_135 ? BSK_BENCH_SAMYANG135 : BSK_BENCH_SAMYANG;
    bsk_txn_samyang(in.lens);   /* la déclaration à bench_core */
}

bsk_bench_lens_t init_lens(void) { return in.lens; }

/* Le drapeau du 0x08. La carte se présente au Samyang comme un boîtier moderne (décision de l'humain) :
 *   - bit 0x04 : il remet à zéro le drapeau que notre 0x01 d'init pose (FF 01 00 aux offsets 6-8, samyang.md § 5.3) ;
 *     sans lui, le 135 impose en RAM M1 = AF, M2 = MF, quelle que soit sa flash ; avec lui, il applique sa propre
 *     configuration du commutateur, celle de sa flash (usine M1 = AF, M2 = APERTURE ; samyang.md § 5.2). Le
 *     rôle de la bague suit déjà l'offset 62 du 0x05 (ring.c), quel que soit le mode de la position ;
 *   - bit 0x02 : il règle aussi des champs du 0x05.
 * Aucun autre objectif ne reçoit de drapeau. */
static uint8_t body08_flags(void) { return in.samyang ? 0x06 : 0x00; }

/* ─────────────────────────── les requêtes ─────────────────────────── */

static void ask(q_t st, const uint8_t *msg, uint16_t len, uint32_t ms)
{
    in.step = st;
    memcpy(in.q, msg, len);                         /* pour la renvoyer telle quelle (resend) */
    in.q_len = len;
    in.q_ms = ms;
    in.sends = 1;
    (void)bsk_txn_request(msg, len, ms, in.now);   /* FAILED se lit au pas suivant, comme une échéance */
}

/* Le 0x01, premier essai : l'init commence (ou recommence, après le reset soft). */
static void ask01(void)
{
    in.tries01 = 1;
    ask(S_Q01, INIT01, sizeof INIT01, in.p.init_ms);
}

/* La boucle de la session lancée. Seul l'oubli de la session l'arrête : un Samyang la lance avec le 0x10 (ses 0x05 pendant
 * le homing sont de cette boucle-ci), et le second appel, à la fin de l'init, ne change rien. Un 0x05 ou un 0x06 reçu avant
 * le lancement n'est pas lu (lens_rx_loop) : celui de la dernière paire d'une session précédente, arrivé après l'oubli, ou
 * d'un objectif déjà en flux. Le détecteur de perte n'en lit rien : il est armé à l'entrée dans READY ou RESTORING. */
static void loop_start(void)
{
    lens_rx_loop();
    bsk_txn_loop(true);
}

/* Init faite ou arrêtée : la boucle est lancée, premier 0x05 attendu. */
static void init_end(void)
{
    loop_start();
    in.next = INIT_END;
}

/* ─────────────────────────── les réponses ─────────────────────────── */

/* Les renvois. Le protocole E n'a aucune retransmission et une ligne série perd des trames : un message ponctuel de
 * l'init est renvoyé tel quel, trois envois au total au plus, dès qu'il échoue (son échéance, une réponse trop courte, son
 * émission ratée ; une erreur de réception ne le fait pas échouer, bsk_txn.h),
 * et seulement si un second envoi ne peut rien changer quand le premier a été reçu :
 *   - renvoyés : 0x07, 0x3F, 0x08, 0x0B, 0x09, 0x0D. Sur le Tamron et le Samyang, leur traitement ne fait que mémoriser
 *     la requête, ou poser un réglage qui ne dépend que d'elle : reçue deux fois, même état, même réponse ;
 *   - jamais renvoyés : le 0x0A (une bascule chez Tamron : un second remettrait l'objectif en init alors que la carte se
 *     croit en flux ; chez Samyang il arrête les moteurs et annule le 0x1D), le 0x10 (il relance le homing, sa réponse
 *     n'arrive qu'à la fin du mouvement) ; le 0x01 et le reset soft ont leurs propres essais.
 * La partie cyclique (la paire 0x03/0x04, les 0x05/0x06) se remet seule à la paire suivante ; la perte de liaison reste
 * celle du détecteur de perte du superviseur. */
static bool idempotent(q_t st)
{
    return st == S_Q07 || st == S_Q3F || st == S_Q08 || st == S_Q0B || st == S_Q09 || st == S_Q0D;
}

/* La requête idempotente en échec renvoyée telle quelle, même échéance, tant qu'elle n'est pas partie trois fois : vrai.
 * Au troisième échec, l'abandon au journal : faux. */
static bool resend(bsk_err_t why)
{
    if (!idempotent(in.step)) return false;
    if (in.sends >= SENDS) {
        bsk_journal_giveup(in.q[0], why);
        return false;
    }
    in.sends++;
    bsk_journal_resend(in.q[0], (uint8_t)in.sends, why);
    (void)bsk_txn_request(in.q, in.q_len, in.q_ms, in.now);
    return true;
}

init_next_t init_reply(bool ok, const bsk_frame_t *r, bsk_err_t why, uint64_t now)
{
    uint8_t m08[9] = {0x08, 0, 0, 0, 0, 0, 0, 0, 0};
    in.now = now;
    in.next = INIT_ASKING;
    if (ok && !lens_rx_full(r)) {                   /* trop courte pour ce que la séquence en lit : comme sans réponse */
        ok = false;
        why = E_FRAMING;
    }
    if (!ok && resend(why)) return in.next;
    switch (in.step) {
    case S_Q01:                                     /* sans réponse, une perte de communication : */
        if (ok) ask(S_Q07, INIT07, sizeof INIT07, in.p.init_ms);
        else if (in.tries01 < TRIES01) {            /* un octet perdu : le même 0x01, même échéance */
            in.tries01++;
            ask(S_Q01, INIT01, sizeof INIT01, in.p.init_ms);
        } else if (!in.reset_done) {                /* le reset soft, une fois par session */
            in.reset_done = true;
            ask(S_RESET0A, INIT0A, sizeof INIT0A, in.p.info_ms);
        } else {
            in.next = INIT_FAIL;                    /* muet après le reset soft */
        }
        break;
    case S_RESET0A:                                 /* répondu ou non : l'init complète, comme un démarrage */
        ask01();
        break;
    case S_Q07:                                     /* toléré, sans réponse : pas d'identité */
        if (ok) {
            lens_rx_id(r);
            init_recognize();
        }
        ask(S_Q3F, INIT3F, sizeof INIT3F, in.p.name_ms);
        break;
    case S_Q3F:                                     /* toléré, sans réponse : pas de nom */
        if (ok && lens_rx_name(r)) init_recognize();
        m08[1] = body08_flags();
        bsk_journal_body08(m08[1], in.samyang);    /* une fois par init (pas aux renvois) */
        ask(S_Q08, m08, sizeof m08, in.p.info_ms);
        break;
    case S_Q08:                                     /* toléré */
        ask(S_Q0B, INIT0B, sizeof INIT0B, in.p.init_ms);
        break;
    case S_Q0B:                                     /* exigés : la séquence s'arrête au premier qui manque */
        if (ok) ask(S_Q09, INIT09, sizeof INIT09, in.p.init_ms);
        else init_end();
        break;
    case S_Q09:
        if (ok) ask(S_Q0D, INIT0D, sizeof INIT0D, in.p.init_ms);
        else init_end();
        break;
    case S_Q0D:
        if (!ok) {
            init_end();
            break;
        }
        ask(S_Q10, INIT10, sizeof INIT10, in.p.init_home_ms);
        /* la boucle avec le 0x10, pour un Samyang reconnu */
        if (in.samyang && bsk_txn_state() == TXN_WAIT) loop_start();
        break;
    case S_Q10:
        if (ok && lens_rx_home_failed(r)) in.next = INIT_HOME_FAILED;
        else if (ok) ask(S_Q0A, INIT0A, sizeof INIT0A, in.p.info_ms);
        else init_end();
        break;
    case S_Q0A:
        in.init_done = ok;
        init_end();
        break;
    }
    return in.next;
}

/* ─────────────────────────── init.h ─────────────────────────── */

void init_params(const bsk_session_params_t *p)
{
    memset(&in, 0, sizeof in);
    in.p = *p;
}

/* Le 0x01, dans la milliseconde qui suit la poignée de main. */
void init_start(uint64_t now)
{
    in.now = now;
    ask01();
}

/* Rien de collant d'une session à l'autre. */
void init_forget(void)
{
    in.reset_done = in.init_done = false;
    init_recognize();
}

bool init_done(void) { return in.init_done; }

bool init_homing(void) { return in.step == S_Q10 && in.next == INIT_ASKING; }
