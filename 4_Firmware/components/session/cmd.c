/* SOURCE: spec de l'atelier § 4.1 et § 4.2 — la boîte de commandes et le bouton du fût
 * AUTHOR: engineer
 * DATE: 2026-10-03
 * STATUS: actif — C pur, sans ESP-IDF : compilé aussi par les tests hôte
 *
 * L'admission des commandes de l'hôte et du bouton du fût (cmd.h). Une seule commande en vol ; q passe toujours. */
#include "cmd.h"

#include <string.h>

#include "bsk_journal.h"
#include "bsk_txn.h"
#include "init.h"
#include "lens_rx.h"
#include "mark.h"
#include "motion.h"
#include "ring.h"

static void reject(const bsk_cmd_t *c, bsk_err_t why) { session_ack(c->seq, ACK_REJECTED, why); }

/* L'échéance de la réponse à CMD_LENS_CUSTOM. Le 135 traite la commande à sa réception, 'P' 0x38 rangeant la
 * configuration en flash, et répond au moins 50 ms après (7_Docs/E-Mount/samyang.md § 6.1, § 6.3). Choisie pour
 * l'écriture de la flash, non mesurée. */
#define CUSTOM_MS 1000u

/* CMD_LENS_CUSTOM en vol, la sous-commande émise, et les données de la dernière réponse. */
static struct {
    bool     wait;
    uint32_t seq;
    uint8_t  sub;
    uint8_t  data[BSK_CUSTOM_DATA];
} cu;

/* Hors READY : E_BUSY dans les états de séquence et en OFF, la raison du FAULT en FAULT. La couche HOTE répond d'après
 * l'état publié, pas d'après la raison. */
static bsk_err_t not_ready(void) { return session_state() == SESSION_FAULT ? session_last_error() : E_BUSY; }

/* CMD_FOCUS_STOP, dans chaque état : jamais E_BUSY à cause d'une commande en vol. Un homing ne s'interrompt pas : celui
 * du pilote (HOMING), et le 0x10 de l'init en vol (IDENTIFYING, init_homing) ; l'état est lu d'abord, init_homing ne vaut
 * qu'en IDENTIFYING (init.h). */
static void stop(const bsk_cmd_t *c)
{
    if (session_state() == SESSION_HOMING ||
        (session_state() == SESSION_IDENTIFYING && init_homing())) {   /* refusé : rien n'est arrêté, rien n'est émis */
        reject(c, E_BUSY);
        return;
    }
    bsk_err_t e = E_OK;
    session_ack(c->seq, ACK_ACCEPTED, E_OK);
    if (session_state() == SESSION_READY) {         /* le 0x1C sans condition, même sans mouvement */
        e = motion_stop(session_now());             /* le mouvement en cours, ABORTED */
    } else if (session_state() == SESSION_RESTORING) {   /* le retour à la marque arrêté : le 0x1C, READY */
        e = motion_stop(session_now());             /* READY même si le 0x1C n'est pas parti : la consigne est retirée */
        session_restore_stop();
    }                                               /* ailleurs rien n'est émis : aucun mouvement à arrêter */
    if (e == E_OK) session_ack(c->seq, ACK_COMPLETED, E_OK);
    else session_ack(c->seq, ACK_FAILED, e);        /* le 0x1C pas émis */
}

/* ring.h : la consigne, bornée à la plage du 0x08, posée pour le prochain 0x03 : le chemin de CMD_APERTURE_SET, que la
 * bague prend aussi. Sans plage, rien : une plage inconnue (0, 0) poserait le code 0. */
void session_aperture(int32_t code)
{
    const lens_rx_t *l = lens_rx();
    if (!l->have08) return;
    if (code < l->ap_min) code = l->ap_min;
    if (code > l->ap_max) code = l->ap_max;
    session_aperture_hold((uint16_t)code);
}

/* CMD_APERTURE_SET, arg : le code. READY seulement, où la boucle tourne ; sans plage (0x08), E_NOCAP ; finie tout de
 * suite. */
static void aperture_cmd(const bsk_cmd_t *c)
{
    const lens_rx_t *l = lens_rx();
    if (session_state() != SESSION_READY) {
        reject(c, not_ready());
        return;
    }
    if (!l->have08) {
        reject(c, E_NOCAP);
        return;
    }
    session_ack(c->seq, ACK_ACCEPTED, E_OK);
    session_aperture(c->arg);
    session_ack(c->seq, ACK_COMPLETED, E_OK);
}

/* CMD_SET_MARK et CMD_CLEAR_MARK : en READY, finies tout de suite ; mark.c décide (E_NOCAP sans identité, E_LIMIT,
 * E_BUSY si le magasin ne prend plus rien). */
static void mark_cmd(const bsk_cmd_t *c)
{
    bsk_err_t e;
    if (session_state() != SESSION_READY) {
        reject(c, not_ready());
        return;
    }
    e = c->op == CMD_SET_MARK ? mark_set(c->arg) : mark_clear();
    if (e != E_OK) {
        reject(c, e);
        return;
    }
    session_ack(c->seq, ACK_ACCEPTED, E_OK);
    session_ack(c->seq, ACK_COMPLETED, E_OK);
}

/* CMD_FOCUS_GOTO et CMD_FOCUS_MOVE : en READY, MOUVEMENT les vérifie (les bornes, E_LIMIT) et les tient jusqu'à leur accusé
 * final. */
static void focus(const bsk_cmd_t *c)
{
    if (session_state() != SESSION_READY) {
        reject(c, not_ready());
        return;
    }
    motion_command(c, session_now());
}

/* CMD_LENS_CUSTOM : le message de service du 135, [0x40]['P'][sous-commande][16 octets de données], en requête de
 * classe 2 (samyang.md § 6.1). arg < 0 : la lecture, 'P' 0xFA, octet de données 0x00 (la branche « JIG » ; jamais 0x53,
 * le drapeau « MTF », § 6.3) ; sinon l'écriture, 'P' 0x38, octet de données 0x30 + arg (§ 6.3). La valeur
 * n'est pas vérifiée ici : bench_core vérifie l'octet émis et refuse ce que le 135 n'écrirait pas. En vol jusqu'à la réponse
 * (cmd_custom_poll). */
static void custom_cmd(const bsk_cmd_t *c)
{
    uint8_t m[19] = {0x40, 'P', 0xFA};
    if (session_state() != SESSION_READY) {
        reject(c, not_ready());
        return;
    }
    if (init_lens() != BSK_BENCH_SAMYANG135) {     /* le 135 seul : bench_core ne laisse qu'à lui cette exception */
        reject(c, E_NOCAP);
        return;
    }
    if (c->arg >= 0) {
        m[2] = 0x38;
        m[3] = (uint8_t)(0x30 + c->arg);
    }
    if (bsk_txn_request(m, sizeof m, CUSTOM_MS, session_now()) == TXN_FAILED) {   /* E_FORBIDDEN, E_BUS : rien d'émis */
        reject(c, bsk_txn_error());
        bsk_txn_ack();
        return;
    }
    session_ack(c->seq, ACK_ACCEPTED, E_OK);
    cu.wait = true;
    cu.seq = c->seq;
    cu.sub = m[2];
}

/* La réponse du 135 : 19 octets, l'écho de MainCmd et SubCmd puis 16 octets de données, différée (samyang.md § 6.1).
 * Une autre 0x40 n'est pas la réponse : E_FRAMING, ses octets ne sont pas rendus. Le 135 n'en émet pas d'autre à cette
 * carte, qui ne lui envoie que 'P' FA et 'P' 38 : ses notifications 'W' ne partent que si 'M' les a armées, ses 'H'/'L'
 * qu'en mode service, que seuls 'I', 'M' ou 'X' posent (§ 6.4, § 6.5), et l'écho immédiat de 'F' FB ne répond qu'à lui
 * (§ 6.3).
 * Ces drapeaux vivent dans sa RAM : seul un 135 qui les a reçus d'un autre maître sans être coupé depuis en émet (le
 * firmware de la carte au temps des traces envoyait 'M' à chaque démarrage, samyang.md § 6.5) ; ce cas finit ici. */
bool cmd_custom_poll(void)
{
    const bsk_frame_t *r = bsk_txn_reply();
    bsk_txn_state_t t = bsk_txn_state();
    if (!cu.wait || (t != TXN_DONE && t != TXN_FAILED)) return false;
    cu.wait = false;
    if (t == TXN_FAILED) {
        session_ack(cu.seq, ACK_FAILED, bsk_txn_error());
    } else if (r->len < 19 || r->msg[1] != 'P' || r->msg[2] != cu.sub) {
        session_ack(cu.seq, ACK_FAILED, E_FRAMING);
    } else {
        memcpy(cu.data, r->msg + 3, sizeof cu.data);
        session_ack(cu.seq, ACK_COMPLETED, E_OK);
    }
    bsk_txn_ack();
    return true;
}

void cmd_forget(void)
{
    if (cu.wait) session_ack(cu.seq, ACK_FAILED, E_ABORTED);
    cu.wait = false;
}

void bsk_session_custom_data(uint8_t data[BSK_CUSTOM_DATA]) { memcpy(data, cu.data, sizeof cu.data); }

void bsk_session_command(const bsk_cmd_t *c)
{
    if (c->op == CMD_FOCUS_STOP) {
        stop(c);
        return;
    }
    if (motion_busy() || cu.wait) {                 /* une commande en vol : un goto pendant un goto aussi */
        reject(c, E_BUSY);
        return;
    }
    switch (c->op) {
    case CMD_FOCUS_GOTO:
    case CMD_FOCUS_MOVE:
        focus(c);
        break;
    case CMD_APERTURE_SET:
        aperture_cmd(c);
        break;
    case CMD_SET_MARK:
    case CMD_CLEAR_MARK:
        mark_cmd(c);
        break;
    case CMD_ATTACH:                                /* fini tout de suite : l'issue du démarrage est publiée */
        session_ack(c->seq, ACK_ACCEPTED, E_OK);
        session_attach();
        session_ack(c->seq, ACK_COMPLETED, E_OK);
        break;
    case CMD_DETACH:                                /* OFF tenu ; pas pendant un démarrage */
        if (session_state() != SESSION_READY && session_state() != SESSION_FAULT && session_state() != SESSION_OFF) {
            reject(c, E_BUSY);
            break;
        }
        session_ack(c->seq, ACK_ACCEPTED, E_OK);
        session_detach();
        session_ack(c->seq, ACK_COMPLETED, E_OK);
        break;
    case CMD_LENS_CUSTOM:
        custom_cmd(c);
        break;
    case CMD_CLEAR_FAULT:                           /* OFF non tenu, puis POWERING si D2 */
        session_ack(c->seq, ACK_ACCEPTED, E_OK);
        session_clear_fault();
        session_ack(c->seq, ACK_COMPLETED, E_OK);
        break;
    default:                                        /* CMD_HOME, CMD_RING_SET : aucun appelant ne les dépose */
        break;
    }
}

/* ─────────────────────────── le bouton du fût ─────────────────────────── */

/* Le goto du bouton : déposé comme un `f<n>` de l'hôte (une commande en vol, READY, les bornes), sous le seq de la carte
 * (BSK_SEQ_BOARD) ; rend la raison de son refus, E_OK s'il est accepté. */
static bsk_err_t board_goto(int32_t pos)
{
    bsk_cmd_t c = {.seq = BSK_SEQ_BOARD, .op = CMD_FOCUS_GOTO, .arg = pos};
    const bsk_ack_t *a;
    bsk_session_command(&c);
    a = session_ack_last();                         /* son premier accusé, le dernier déposé */
    return a->result == ACK_REJECTED ? a->reason : E_OK;
}

/* Au relâchement : court, aller à la marque telle quelle ; long, la poser ici ; 4 s ou plus, rien. Un appui commencé hors
 * de READY est oublié, et un appui pendant une commande en vol (un mouvement, ou CMD_LENS_CUSTOM : la même exclusion que
 * bsk_session_command) est ignoré, long compris ; chaque relâchement est
 * journalisé, 4 s ou plus aussi. Le relâchement a lieu en READY quand l'appui y a commencé : toute sortie de READY oublie
 * la session, l'appui avec elle. */
void cmd_button(uint64_t t)
{
    bsk_btn_t b;
    uint8_t at;
    bsk_mark_t m;
    bsk_err_t e;
    if (!mark_button(t, (uint8_t)session_state(), &b, &at)) return;
    if (b == BSK_BTN_HELD) {
        bsk_journal_btn(b, BSK_BTN_IGNORE, NULL);
        return;
    }
    if (at != SESSION_READY) {
        bsk_journal_btn(b, BSK_BTN_IGNORE, bsk_state_name(at));
        return;
    }
    if (motion_busy() || cu.wait) {
        bsk_journal_btn(b, BSK_BTN_IGNORE, bsk_err_token(E_BUSY));
        return;
    }
    if (b == BSK_BTN_LONG) {
        e = mark_set(BSK_MARK_HERE);
    } else if (!mark_get(&m)) {
        bsk_journal_btn(b, BSK_BTN_ER, "nomark");
        return;
    } else {
        e = board_goto(m.position);
    }
    bsk_journal_btn(b, e == E_OK ? BSK_BTN_OK : BSK_BTN_ER, e == E_OK ? NULL : bsk_err_token(e));
}
