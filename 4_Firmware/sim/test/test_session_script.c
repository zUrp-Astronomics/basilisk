/* SOURCE: spec de l'atelier ; traces de traces/, captures de l'humain (7_Docs/E-Mount/traces/) ; 7_Docs/E-Mount/samyang.md
 * AUTHOR: engineer
 * DATE: 2026-09-27
 * STATUS: actif — joué par 4_Firmware/run.sh (job `firmware` de la CI), chaque transition tenue par sim/test/mutations.txt
 *
 * SESSION, TRANSACTION, MOUVEMENT et HOTE contre un répondeur minimal (sim/test/phy_script.c), chaque transition isolée ;
 * le mouvement contre un moteur scripté (motor()).
 *
 * Le répondeur ne rend que les messages que ce fichier lui donne, et chacun est cité :
 *   - le Samyang 135 : les gabarits de sim/sources135.c, copiés des traces de l'humain (traces/, fichier:ligne dans
 *     sources135.c) ; délai de 5 ms, la réponse d'init la plus rapide des traces (full:8-9), 700 ms pour la fin du
 *     homing (astro-normal:241-251) ;
 *   - le Tamron F051 : son 0x07, `07 01 03 70 00 00 01 03 00 A0 34 C1` (7_Docs/E-Mount/tamron.md § 1.2 et
 *     protocol.md § 7.7, les champs publiés de ses douze premiers octets, les autres à zéro ; le LensType2 est aux
 *     offsets 9-10), et `10 01` (tamron.md § 3) ;
 *   - le nom 0x3F du 135, pour la seule reconnaissance par le nom (t_name_3f) : 7_Docs/E-Mount/samyang.md § 1.1
 *     (7_Docs/E-Mount/traces/firmware2_ring.txt lignes 17-18), tel que le faux 135 le rend.
 * Sinon aucun 0x3F : les traces n'en ont pas, et la session le tolère absent.
 * Le Sony FE 24-105 G : ses trames copiées des captures de l'humain (voir la section du Sony, plus bas). */
#define _POSIX_C_SOURCE 200809L   /* alarm, la garde de durée */

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bsk_bench_core.h"
#include "bsk_host.h"
#include "bsk_journal.h"
#include "bsk_session.h"
#include "bsk_txn.h"
#include "../../components/session/drive.h"   /* 0x1D, bit AF, 0x1C */
#include "../../components/session/mark.h"    /* le sens de la marque, que l'instantané ne publie pas */
#include "frame.h"
#include "phy_script.h"
#include "sources135.h"
#include "store_sim.h"

#define MS 1000u
#define T0 (1000u * MS)

static int checks, fails;

/* Une machine qui ne rend plus la main est un échec, pas un plantage : la garde de durée le dit comme
 * une vérification (sortie 1), et mutants.sh le compte tué. */
static void too_long(int sig)
{
    static const char m[] = "  ECHEC : le programme ne termine pas en 10 s (boucle sans fin)\n";
    (void)sig;
    (void)!write(1, m, sizeof m - 1);
    _exit(1);
}

#define CHECK(c, ...)                                                   \
    do {                                                                \
        checks++;                                                       \
        if (!(c)) {                                                     \
            fails++;                                                    \
            printf("  ECHEC %s:%d : ", __FILE__, __LINE__);             \
            printf(__VA_ARGS__);                                        \
            printf("\n");                                               \
        }                                                               \
    } while (0)

static const uint8_t TAMRON07[] = {0x07, 0x01, 0x03, 0x70, 0x00, 0x00, 0x01, 0x03, 0x00, 0xA0, 0x34, 0xC1};
static const uint8_t TAMRON10[] = {0x10, 0x01};
static const char NAME135[] = "SAMYANG AF 135mm F1.8";   /* samyang.md § 1.1 */

static bsk_session_params_t P;
static uint32_t refused0;

/* Les changements d'état publiés, horodatés. */
typedef struct { uint64_t t; uint8_t state; } chg_t;
static chg_t chg[512];
static size_t n_chg;
static uint8_t last_state;
static chg_t mchg[512];     /* motion_state */
static size_t n_mchg;
static uint8_t last_mv;

typedef struct { bsk_ack_t a; uint64_t t; } ack_rec_t;
static ack_rec_t ACKS[256];
static size_t n_acks;
static uint32_t seq_n;

static bsk_status_t status(void)
{
    bsk_status_t s;
    bsk_session_status(&s);
    return s;
}

static void note_state(uint64_t t)
{
    bsk_ack_t a;
    bsk_status_t st = status();
    uint8_t s = st.session_state;
    if (s != last_state && n_chg < sizeof chg / sizeof chg[0]) chg[n_chg++] = (chg_t){t, s};
    last_state = s;
    while (bsk_session_ack(&a))
        if (n_acks < sizeof ACKS / sizeof ACKS[0]) ACKS[n_acks++] = (ack_rec_t){a, t};
    if (st.motion_state != last_mv && n_mchg < sizeof mchg / sizeof mchg[0]) mchg[n_mchg++] = (chg_t){t, st.motion_state};
    last_mv = st.motion_state;
}

static void run_to(uint64_t t_end)
{
    uint64_t prev = UINT64_MAX;
    unsigned same = 0;
    for (;;) {
        uint64_t t = ps_next(), ts = bsk_session_next();
        if (ts < t) t = ts;
        if (t > t_end) t = t_end;
        ps_run(t);
        bsk_session_step(t);
        note_state(t);
        if (t >= t_end) break;
        same = t == prev ? same + 1 : 0;
        prev = t;
        if (same > 100000) {   /* le temps n'avance plus : une échéance jamais traitée */
            CHECK(0, "le temps n'avance plus à %llu µs", (unsigned long long)t);
            break;
        }
    }
}

static void run_for(uint64_t us) { run_to(ps_now() + us); }

/* Instant de la première entrée dans `state` à partir du changement `from`, 0 sinon. */
static uint64_t entered(uint8_t state, size_t from)
{
    for (size_t i = from; i < n_chg; i++)
        if (chg[i].state == state) return chg[i].t;
    return 0;
}

static size_t chg_index_after(uint64_t t)
{
    size_t i = 0;
    while (i < n_chg && chg[i].t < t) i++;
    return i;
}

static long sent(size_t from, uint8_t cls, uint8_t type)
{
    for (size_t i = from; i < g_ps_n && i < PS_LOG_CAP; i++)
        if (g_ps_log[i].kind == PS_SEND && g_ps_log[i].frame.cls == cls && g_ps_log[i].frame.msg[0] == type) return (long)i;
    return -1;
}

static size_t count_sent(uint64_t t_from, uint64_t t_to, uint8_t type)
{
    size_t n = 0;
    for (size_t i = 0; i < g_ps_n && i < PS_LOG_CAP; i++)
        if (g_ps_log[i].kind == PS_SEND && g_ps_log[i].t >= t_from && g_ps_log[i].t < t_to && g_ps_log[i].frame.msg[0] == type) n++;
    return n;
}

static long act(size_t from, ps_act_kind_t kind)
{
    for (size_t i = from; i < g_ps_n && i < PS_LOG_CAP; i++)
        if (g_ps_log[i].kind == kind) return (long)i;
    return -1;
}

static uint64_t t_of(long i) { return i < 0 ? 0 : g_ps_log[i].t; }

/* Les 0x03 émis depuis l'entrée `from` du journal portent tous la consigne d'ouverture `code` aux offsets 3-6,
 * petit-boutiste, deux fois ; au moins un 0x03. */
static bool ap03_all(size_t from, uint16_t code)
{
    size_t n = 0;
    for (size_t i = from; i < g_ps_n && i < PS_LOG_CAP; i++) {
        const bsk_frame_t *f = &g_ps_log[i].frame;
        if (g_ps_log[i].kind != PS_SEND || f->msg[0] != 0x03) continue;
        if (f->msg[4] != (uint8_t)code || f->msg[5] != code >> 8 || f->msg[6] != (uint8_t)code || f->msg[7] != code >> 8)
            return false;
        n++;
    }
    return n > 0;
}

/* Un objectif muet, D2 présent à T0 : la session démarre. `cut` : la coupure des rails de RECOVERING activée
 * (cut_rails, désactivée par défaut). */
static void boot_cut(bool cut)
{
    ps_init(T0);
    bsk_session_params_default(&P);
    if (cut) P.cut_rails = true;                   /* sinon la valeur par défaut, jouée telle quelle */
    store_sim_reset();                             /* aucune marque rangée */
    bsk_session_init(&P);
    n_chg = 0;
    n_mchg = 0;
    n_acks = 0;
    last_state = SESSION_OFF;
    last_mv = MOTION_IDLE;
    refused0 = bsk_bench_refused();
}

static void boot(void) { boot_cut(false); }

static void answer_src(uint8_t type, src_id_t id, uint64_t delay_us)
{
    uint8_t m[BSK_MSG_MAX];
    size_t n = sources135_msg(id, m, sizeof m);
    ps_answer(type, m, n, delay_us);
}

/* Le 135 des traces : réponses d'init, flux après le 0x0A, poignée de main. */
static void lens_135(void)
{
    answer_src(0x01, SRC_R01, 5 * MS);
    answer_src(0x07, SRC_R07, 5 * MS);
    answer_src(0x08, SRC_R08, 5 * MS);
    answer_src(0x0B, SRC_R0B, 5 * MS);
    answer_src(0x09, SRC_R09, 5 * MS);
    answer_src(0x0D, SRC_R0D, 5 * MS);
    answer_src(0x10, SRC_R10, 700 * MS);
    answer_src(0x0A, SRC_R0A, 5 * MS);
    g_ps.n05 = (uint16_t)sources135_msg(SRC_T05, g_ps.m05, sizeof g_ps.m05);
    g_ps.n06 = (uint16_t)sources135_msg(SRC_T06, g_ps.m06, sizeof g_ps.m06);
    g_ps.flow_on_0a = true;
    g_ps.handshake = true;
}

static void no_answer(uint8_t type) { g_ps.reply[type].len = 0; }

/* La position du 0x06 du flux, offsets 2-3. */
static void set_pos(uint16_t pos)
{
    g_ps.m06[3] = (uint8_t)pos;
    g_ps.m06[4] = (uint8_t)(pos >> 8);
}

/* Les bornes du 0x06 du flux, offsets 7-8 et 9-10. */
static void set_limits(uint16_t lo, uint16_t hi)
{
    g_ps.m06[8] = (uint8_t)lo;
    g_ps.m06[9] = (uint8_t)(lo >> 8);
    g_ps.m06[10] = (uint8_t)hi;
    g_ps.m06[11] = (uint8_t)(hi >> 8);
}

/* ─────────────────────────── TRANSACTION ─────────────────────────── */

static void pump(uint64_t t)
{
    bsk_phy_event_t e;
    ps_run(t);
    while (bsk_phy_poll(&e)) {
        bsk_txn_tick(e.t_us);         /* ce qui est échu avant l'événement d'abord, comme la session */
        bsk_txn_on_event(&e);
    }
    bsk_txn_tick(t);
}

/* TRANSACTION seule, sans SESSION : aucun instant réel donné (bsk_txn_real à 0), aucune trame de la paire n'est en retard,
 * sauf dans les cas qui le donnent eux-mêmes. */
static void txn_boot(void)
{
    ps_init(T0);
    bsk_txn_loop(false);
    bsk_txn_ack();
    bsk_txn_samyang(false);
    bsk_txn_real(0);
}

static void t_txn_done(void)
{
    const uint8_t q[] = {0x07, 0x00};
    uint8_t other[] = {0x05, 0, 0};
    printf("TRANSACTION : IDLE -> WAIT -> DONE -> IDLE\n");
    txn_boot();
    answer_src(0x07, SRC_R07, 5 * MS);
    CHECK(bsk_txn_state() == TXN_IDLE, "au départ : IDLE");
    CHECK(bsk_txn_request(q, sizeof q, 300, ps_now()) == TXN_WAIT && bsk_txn_state() == TXN_WAIT, "requête émise : WAIT");
    CHECK(g_ps_n == 1 && g_ps_log[0].frame.cls == 2 && g_ps_log[0].frame.len == 2 && g_ps_log[0].frame.msg[0] == 0x07,
          "une trame de classe 2, le message tel quel");
    CHECK(bsk_txn_deadline() == T0 + 300 * MS, "échéance à 300 ms de l'émission");
    ps_frame(1, other, sizeof other, T0 + 1 * MS);
    pump(T0 + 2 * MS);
    CHECK(bsk_txn_state() == TXN_WAIT, "une trame d'un autre type ne termine pas la requête");
    pump(T0 + 5 * MS);
    CHECK(bsk_txn_state() == TXN_DONE && bsk_txn_reply()->msg[0] == 0x07 && bsk_txn_reply()->len == 35,
          "la réponse du même type : DONE, la réponse lisible");
    CHECK(bsk_txn_deadline() == UINT64_MAX, "hors WAIT, pas d'échéance");
    bsk_txn_ack();
    CHECK(bsk_txn_state() == TXN_IDLE, "lue : IDLE");
}

static void t_txn_timeout(void)
{
    const uint8_t q[] = {0x07, 0x00};
    printf("TRANSACTION : WAIT -> FAILED (E_TIMEOUT) à l'échéance\n");
    txn_boot();
    (void)bsk_txn_request(q, sizeof q, 300, ps_now());
    pump(T0 + 300 * MS - 1);
    CHECK(bsk_txn_state() == TXN_WAIT, "300 ms moins 1 µs : toujours WAIT");
    pump(T0 + 300 * MS);
    CHECK(bsk_txn_state() == TXN_FAILED && bsk_txn_error() == E_TIMEOUT, "à 300 ms : FAILED, E_TIMEOUT");
}

/* Une erreur de réception ne coûte que la trame (audit M6, décision de l'humain du 2026-10-07) : la requête attend sa
 * réponse jusqu'à son échéance. Seule son émission ratée la termine avant. */
static void t_txn_phy_error(void)
{
    const uint8_t q[] = {0x07, 0x00};
    printf("TRANSACTION : une erreur de réception ne termine pas la requête, son émission ratée oui\n");
    txn_boot();
    answer_src(0x07, SRC_R07, 5 * MS);
    (void)bsk_txn_request(q, sizeof q, 300, ps_now());
    ps_run(T0 + 2 * MS);
    ps_error(E_FRAMING);
    pump(T0 + 2 * MS);
    CHECK(bsk_txn_state() == TXN_WAIT && bsk_txn_deadline() == T0 + 300 * MS, "E_FRAMING pendant WAIT : toujours WAIT");
    ps_error(E_BUS);
    pump(T0 + 3 * MS);
    CHECK(bsk_txn_state() == TXN_WAIT && bsk_txn_deadline() == T0 + 300 * MS, "E_BUS pendant WAIT : toujours WAIT");
    pump(T0 + 5 * MS);
    CHECK(bsk_txn_state() == TXN_DONE && bsk_txn_reply()->msg[0] == 0x07, "puis sa réponse : DONE");
    bsk_txn_ack();
    no_answer(0x07);
    (void)bsk_txn_request(q, sizeof q, 300, ps_now());
    ps_error(E_FRAMING);
    pump(T0 + 300 * MS + 5 * MS - 1);
    CHECK(bsk_txn_state() == TXN_WAIT, "sans réponse, une erreur reçue : WAIT jusqu'à son échéance");
    pump(T0 + 300 * MS + 5 * MS);
    CHECK(bsk_txn_state() == TXN_FAILED && bsk_txn_error() == E_TIMEOUT, "à son échéance : FAILED, E_TIMEOUT");
    bsk_txn_ack();
    g_ps.fail_sends = 1;
    CHECK(bsk_txn_request(q, sizeof q, 300, ps_now()) == TXN_FAILED && bsk_txn_error() == E_BUS && g_ps.fail_sends == 0,
          "son émission ratée : FAILED, E_BUS, tout de suite");
}

/* Pas de réessai de lien : une requête sans réponse part une fois, et finit à son échéance ; le réessai de politique
 * est celui de SESSION. */
static void t_txn_no_retry(void)
{
    const uint8_t q[] = {0x07, 0x00};
    size_t n = 0;
    printf("TRANSACTION : pas de réessai de lien, une requête sans réponse part une seule fois\n");
    txn_boot();
    (void)bsk_txn_request(q, sizeof q, 300, ps_now());
    pump(T0 + 300 * MS);
    pump(T0 + 700 * MS);
    pump(T0 + 2000 * MS);
    for (size_t i = 0; i < g_ps_n; i++) n += g_ps_log[i].kind == PS_SEND && g_ps_log[i].frame.msg[0] == 0x07;
    CHECK(n == 1, "une seule émission du 0x07 en 2 s (%zu)", n);
    CHECK(bsk_txn_state() == TXN_FAILED && bsk_txn_error() == E_TIMEOUT, "FAILED, E_TIMEOUT, sans nouvel essai");
}

static void t_txn_forbidden(void)
{
    const uint8_t q[] = {0x0C, 0x00};
    uint32_t r0 = bsk_bench_refused();
    printf("TRANSACTION : IDLE -> FAILED, refus de bench_core\n");
    txn_boot();
    CHECK(bsk_txn_request(q, sizeof q, 300, ps_now()) == TXN_FAILED && bsk_txn_error() == E_FORBIDDEN,
          "0x0C (hors liste) : FAILED, E_FORBIDDEN");
    CHECK(g_ps_n == 0 && bsk_bench_refused() == r0 + 1, "rien d'émis, le refus compté");
}

static void t_txn_loop(void)
{
    printf("TRANSACTION : la boucle 0x03/0x04 à chaque VD, sous un numéro ; le 0x1C en classe 1\n");
    txn_boot();
    bsk_phy_vd(60);
    g_ps_n = 0;
    /* Un front tous les 16,666 ms depuis T0, sa paire finie 10,1 ms après lui ; chaque relevé tombe après la paire d'un
     * front et avant le suivant. */
    pump(T0 + 27 * MS);
    CHECK(g_ps_n == 0, "boucle arrêtée : rien au front VD");
    bsk_txn_loop(true);
    CHECK(bsk_txn_loop_on(), "boucle lancée");
    pump(T0 + 44 * MS);
    CHECK(g_ps_n == 2 && g_ps_log[0].frame.msg[0] == 0x03 && g_ps_log[1].frame.msg[0] == 0x04 &&
              g_ps_log[0].frame.cls == 1 && g_ps_log[1].frame.cls == 1 && g_ps_log[0].frame.len == 21 &&
              g_ps_log[1].frame.len == 14,
          "un front : 0x03 (21 octets, sans l'écho 0x2F) puis 0x04 (14 octets), classe 1");
    CHECK(g_ps_n == 2 && g_ps_log[0].frame.seq == g_ps_log[1].frame.seq, "la paire sous un seul numéro");
    pump(T0 + 61 * MS);
    CHECK(g_ps_n == 4 && g_ps_log[2].frame.seq == (uint8_t)(g_ps_log[0].frame.seq + 1), "front suivant : numéro suivant");
    CHECK(drive_stop() == E_OK && g_ps_n == 5 && g_ps_log[4].frame.cls == 1 && g_ps_log[4].frame.len == 1 &&
              g_ps_log[4].frame.msg[0] == 0x1C && bsk_txn_state() == TXN_IDLE,
          "0x1C : une trame de classe 1, aucune requête en vol");
    bsk_txn_loop(false);
    pump(T0 + 78 * MS);
    CHECK(g_ps_n == 5, "boucle arrêtée : plus rien");
    bsk_phy_vd(0);
}

/* Un champ du 0x04 posé par l'étage du dessus, les autres bits et octets inchangés, tenu d'une paire à l'autre
 * jusqu'à ce qu'il le change. Le 0x04 de la boucle, octet de type compris : 04 00 00 19 81 00 00 3D 00 00 00 01 00 00 ;
 * l'octet 3 (offset 3, type exclu) avec son bit 1 : 0x83. */
static void t_txn_loop04(void)
{
    static const uint8_t m04[] = {0x04, 0x00, 0x00, 0x19, 0x83, 0x00, 0x00, 0x3D, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
    printf("TRANSACTION : l'octet 3 du 0x04 posé par l'étage du dessus, bit par bit, tenu\n");
    txn_boot();
    bsk_phy_vd(60);
    bsk_txn_loop(true);
    bsk_txn_loop04(3, 0x02, 0xFF);                   /* le masque seul compte : le bit 1, pas les autres */
    g_ps_n = 0;
    pump(T0 + 27 * MS);                              /* après la paire du premier front (26,8 ms) */
    CHECK(g_ps_n == 2 && g_ps_log[1].frame.len == 14 && !memcmp(g_ps_log[1].frame.msg, m04, sizeof m04),
          "le 0x04 suivant : 04 00 00 19 83 00 00 3D 00 00 00 01 00 00 (octet 3 : 0x%02X)", g_ps_log[1].frame.msg[4]);
    pump(T0 + 44 * MS);
    CHECK(g_ps_n == 4 && g_ps_log[3].frame.msg[4] == 0x83, "tenu à la paire suivante");
    bsk_txn_loop04(3, 0x02, 0x00);
    pump(T0 + 61 * MS);
    CHECK(g_ps_n == 6 && g_ps_log[5].frame.msg[4] == 0x81, "retiré : 0x81, le 0x04 de la v1 (0x%02X)", g_ps_log[5].frame.msg[4]);
    bsk_txn_loop(false);
    bsk_phy_vd(0);
}

/* La paire à la phase du NEX-7. Le premier front VD à T0 + 16666 µs (bsk_phy_vd(60) à T0), le 0x03 à 8600 µs de lui,
 * le 0x04 à 10100 µs (7_Docs/E-Mount/protocol.md § 6.2), instants écrits à la main. */
#define VD1 (T0 + 16666u)

static size_t obs_n, obs_at;          /* comptes rendus reçus ; le journal de la PHY scriptée au dernier */
static void obs(void)
{
    obs_n++;
    obs_at = g_ps_n;
}

static void t_txn_phase(void)
{
    printf("TRANSACTION : le 0x03 à VD + 8,6 ms, le 0x04 à VD + 10,1 ms, pas avant\n");
    txn_boot();
    bsk_phy_vd(60);
    bsk_txn_loop(true);
    obs_n = 0;
    bsk_txn_loop_observer(obs);
    g_ps_n = 0;
    pump(VD1);
    CHECK(g_ps_n == 0 && bsk_txn_next() == VD1 + 8600, "au front : rien, la prochaine échéance à +8600 µs");
    pump(VD1 + 8599);
    CHECK(g_ps_n == 0, "à +8599 µs : rien");
    pump(VD1 + 8600);
    CHECK(g_ps_n == 1 && g_ps_log[0].frame.msg[0] == 0x03 && g_ps_log[0].t == VD1 + 8600 && bsk_txn_next() == VD1 + 10100,
          "à +8600 µs : le 0x03 seul ; la prochaine échéance à +10100 µs");
    pump(VD1 + 10099);
    CHECK(g_ps_n == 1 && obs_n == 0, "à +10099 µs : rien de plus, pas de compte rendu");
    pump(VD1 + 10100);
    CHECK(g_ps_n == 2 && g_ps_log[1].frame.msg[0] == 0x04 && g_ps_log[1].t == VD1 + 10100 &&
              g_ps_log[1].frame.seq == g_ps_log[0].frame.seq,
          "à +10100 µs : le 0x04, sous le numéro du 0x03");
    CHECK(obs_n == 1 && obs_at == 2 && bsk_txn_next() == UINT64_MAX, "le compte rendu après lui ; plus d'échéance");
    pump(VD1 + 16666 + 8599);
    CHECK(g_ps_n == 2, "front suivant, +8599 µs : rien");
    pump(VD1 + 16666 + 8600);
    pump(VD1 + 16666 + 10100);
    CHECK(g_ps_n == 4 && g_ps_log[2].t == VD1 + 16666 + 8600 && g_ps_log[3].t == VD1 + 16666 + 10100,
          "front suivant : 0x03 à +8600, 0x04 à +10100");
    bsk_txn_loop(false);
    bsk_txn_loop_observer(NULL);
    bsk_phy_vd(0);
}

/* Entre le 0x03 et le 0x04, rien ne part : le 0x1C (bsk_txn_send) et une requête retenus, le message accroché sur le
 * 0x04 en attente ; tous derrière le 0x04, dans l'ordre, avant le compte rendu. */
static void t_txn_held(void)
{
    static const uint8_t q[] = {0x07, 0x00}, stop[] = {0x1C}, m1d[] = {0x1D, 0x56, 0x34, 0x00, 0x00};
    uint8_t r07[35] = {0x07};
    printf("TRANSACTION : entre le 0x03 et le 0x04, le 0x1C et une requête retenus, partis derrière le 0x04\n");
    txn_boot();
    bsk_phy_vd(60);
    bsk_txn_loop(true);
    obs_n = 0;
    bsk_txn_loop_observer(obs);
    g_ps_n = 0;
    pump(VD1 + 9000);
    CHECK(g_ps_n == 1 && g_ps_log[0].frame.msg[0] == 0x03, "+9000 µs : le 0x03 est parti");
    CHECK(bsk_txn_send(stop, sizeof stop) == E_OK && g_ps_n == 1, "le 0x1C : E_OK, retenu, rien d'émis");
    CHECK(bsk_txn_request(q, sizeof q, 300, ps_now()) == TXN_WAIT && g_ps_n == 1 && bsk_txn_deadline() == UINT64_MAX,
          "la requête : WAIT, retenue, sans échéance tant qu'elle n'est pas partie");
    bsk_txn_attach(m1d, sizeof m1d);
    ps_frame(2, r07, sizeof r07, ps_now() + 100);
    pump(VD1 + 9500);
    CHECK(bsk_txn_state() == TXN_WAIT, "une trame de son type avant son départ ne la termine pas");
    ps_error(E_FRAMING);
    pump(VD1 + 9600);
    CHECK(bsk_txn_state() == TXN_WAIT && g_ps_n == 1 && obs_n == 0, "une erreur de PHY non plus ; toujours rien d'émis");
    pump(VD1 + 10100);
    CHECK(g_ps_n == 4 && g_ps_log[1].frame.msg[0] == 0x04 && g_ps_log[1].frame.len == 19 &&
              !memcmp(g_ps_log[1].frame.msg + 14, m1d, sizeof m1d) && g_ps_log[1].frame.seq == g_ps_log[0].frame.seq,
          "+10100 µs : le 0x04 et le 1D accroché dans l'intervalle, sous le numéro du 0x03");
    CHECK(g_ps_n == 4 && g_ps_log[2].frame.msg[0] == 0x1C && g_ps_log[2].frame.cls == 1 &&
              g_ps_log[2].frame.seq == (uint8_t)(g_ps_log[0].frame.seq + 1) && g_ps_log[3].frame.msg[0] == 0x07 &&
              g_ps_log[3].frame.cls == 2 && g_ps_log[3].frame.seq == (uint8_t)(g_ps_log[0].frame.seq + 2) &&
              g_ps_log[2].t == VD1 + 10100 && g_ps_log[3].t == VD1 + 10100,
          "puis le 0x1C, puis la requête, dans l'ordre de leur dépôt, numéros suivants");
    CHECK(obs_n == 1 && obs_at == 4, "le compte rendu après eux");
    CHECK(bsk_txn_state() == TXN_WAIT && bsk_txn_deadline() == VD1 + 10100 + 300 * MS, "l'échéance comptée depuis son départ");
    ps_frame(2, r07, sizeof r07, ps_now() + 5 * MS);
    pump(VD1 + 10100 + 5 * MS);
    CHECK(bsk_txn_state() == TXN_DONE, "partie, sa réponse la termine");
    bsk_txn_ack();
    CHECK(bsk_txn_send(stop, sizeof stop) == E_OK && g_ps_n == 5, "hors de l'intervalle, le 0x1C part tout de suite");

    /* Une requête retenue remplace la précédente, retenue ou en vol ; le 0x1C retenu ne la remplace pas. */
    pump(VD1 + 16666 + 9000);
    {
        const uint8_t q2[] = {0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        size_t n0 = g_ps_n;
        (void)bsk_txn_request(q, sizeof q, 300, ps_now());
        (void)bsk_txn_send(stop, sizeof stop);
        (void)bsk_txn_request(q2, sizeof q2, 400, ps_now());
        CHECK(g_ps_n == n0, "rien d'émis dans l'intervalle");
        pump(VD1 + 16666 + 10100);
        CHECK(g_ps_n == n0 + 3 && g_ps_log[n0].frame.msg[0] == 0x04 && g_ps_log[n0 + 1].frame.msg[0] == 0x08 &&
                  g_ps_log[n0 + 2].frame.msg[0] == 0x1C && bsk_txn_deadline() == VD1 + 16666 + 10100 + 400 * MS,
              "le 0x04, la dernière requête à la place de la première, le 0x1C ; son échéance à elle");
    }
    /* La file des retenues : quatre, au-delà refusées (E_BUSY), rien de perdu de ce qui est retenu. */
    pump(VD1 + 2 * 16666 + 9000);
    {
        size_t n0 = g_ps_n;
        bsk_err_t e[5];
        for (int i = 0; i < 5; i++) e[i] = bsk_txn_send(stop, sizeof stop);
        CHECK(e[0] == E_OK && e[3] == E_OK && e[4] == E_BUSY, "quatre 0x1C retenus, le cinquième refusé, E_BUSY");
        CHECK(bsk_txn_request(q, sizeof q, 300, ps_now()) == TXN_FAILED && bsk_txn_error() == E_BUSY,
              "une requête, file pleine : FAILED, E_BUSY");
        pump(VD1 + 2 * 16666 + 10100);
        CHECK(g_ps_n == n0 + 5 && g_ps_log[n0 + 4].frame.msg[0] == 0x1C, "le 0x04 puis les quatre 0x1C");
    }
    bsk_txn_loop(false);
    bsk_txn_loop_observer(NULL);
    bsk_phy_vd(0);
}

/* Pas de paire qui n'a plus lieu d'être : la boucle arrêtée avant le 0x03, puis entre le 0x03 et le 0x04 (rien de ce qui
 * attendait le 0x04 ne part, une requête retenue finit en E_ABORTED sans avoir été émise) ; le 0x03 raté, pas de 0x04,
 * pas de compte rendu, et le message accroché reporté à la paire suivante (bsk_txn_attach). */
static void t_txn_cancel(void)
{
    static const uint8_t stop[] = {0x1C}, m1d[] = {0x1D, 0x56, 0x34, 0x00, 0x00}, q[] = {0x07, 0x00};
    printf("TRANSACTION : boucle arrêtée avant l'échéance, la paire ne part pas ; le 0x03 raté, pas de 0x04\n");
    txn_boot();
    bsk_phy_vd(60);
    bsk_txn_loop(true);
    obs_n = 0;
    bsk_txn_loop_observer(obs);
    g_ps_n = 0;
    pump(VD1 + 5000);
    bsk_txn_loop(false);
    CHECK(bsk_txn_next() == UINT64_MAX, "arrêtée avant le 0x03 : plus d'échéance");
    pump(VD1 + 16000);
    CHECK(g_ps_n == 0 && obs_n == 0, "ni 0x03 ni 0x04, pas de compte rendu");

    bsk_txn_loop(true);
    pump(VD1 + 16666 + 9000);
    CHECK(g_ps_n == 1 && g_ps_log[0].frame.msg[0] == 0x03, "relancée : le 0x03 du front suivant");
    (void)bsk_txn_send(stop, sizeof stop);
    CHECK(g_ps_n == 1, "le 0x1C retenu");
    CHECK(bsk_txn_request(q, sizeof q, 300, ps_now()) == TXN_WAIT && g_ps_n == 1, "la requête retenue, WAIT");
    bsk_txn_loop(false);
    CHECK(g_ps_n == 1, "arrêtée entre les deux : ni le 0x1C ni la requête retenus ne partent, aucun 0x04 ne suivra");
    CHECK(bsk_txn_state() == TXN_FAILED && bsk_txn_error() == E_ABORTED && bsk_txn_deadline() == UINT64_MAX,
          "la requête retenue : FAILED, E_ABORTED, sans échéance");
    bsk_txn_ack();
    pump(VD1 + 2 * 16666);
    CHECK(g_ps_n == 1 && obs_n == 0, "pas de 0x04, pas de compte rendu, rien de retenu parti ensuite");

    bsk_txn_loop(true);
    bsk_txn_attach(m1d, sizeof m1d);
    g_ps.fail_sends = 1;
    pump(VD1 + 3 * 16666 + 8600);                    /* le front de VD1 + 2 × 16666, boucle arrêtée : pas de paire */
    CHECK(g_ps_n == 1 && obs_n == 0 && bsk_txn_next() == UINT64_MAX,
          "le 0x03 raté : pas de compte rendu, rien de l'arrêt précédent relâché ; plus d'échéance");
    pump(VD1 + 4 * 16666);
    CHECK(g_ps_n == 1 && obs_n == 0, "et pas de 0x04 (std.c:426-427)");
    pump(VD1 + 4 * 16666 + 8600);
    pump(VD1 + 4 * 16666 + 10100);
    CHECK(g_ps_n == 3 && g_ps_log[2].frame.len == 19 && !memcmp(g_ps_log[2].frame.msg + 14, m1d, sizeof m1d) && obs_n == 1,
          "la paire suivante porte le message accroché, tel quel ; son compte rendu");
    CHECK(!bsk_txn_attach_failed(), "N2 : le 0x03 raté, son 0x04 jamais tenté : un report sans émission, rien de noté");
    pump(VD1 + 5 * 16666 + 10100);
    CHECK(g_ps_n == 5 && g_ps_log[4].frame.len == 14, "parti une fois : la paire d'après, sans lui");
    pump(VD1 + 6 * 16666 + 8600);
    g_ps.fail_sends = 1;                             /* le 0x04 du front de VD1 + 6 × 16666, sans message accroché */
    pump(VD1 + 6 * 16666 + 10100);
    CHECK(g_ps_n == 6 && g_ps_log[5].frame.msg[0] == 0x03 && g_ps.fail_sends == 0 && !bsk_txn_attach_failed(),
          "N2 : un 0x04 raté qui ne porte aucun message : rien de noté pour le message déjà parti");
    bsk_txn_loop(false);
    bsk_txn_loop_observer(NULL);
    bsk_phy_vd(0);
}

/* Le 0x03 parti, le 0x04 jeté hors de sa fenêtre (R2) : l'instant réel du pas donné à la main (bsk_txn_real), 2001 µs
 * après l'échéance du 0x04. Ce qui attendait le 0x04 (le 0x1C, une requête) part à sa place, dans l'ordre, rien de perdu,
 * rien entre le 0x03 et elle ; pas de compte rendu ; le message accroché reste, la paire suivante le porte. */
static void t_txn_window(void)
{
    static const uint8_t stop[] = {0x1C}, m1d[] = {0x1D, 0x56, 0x34, 0x00, 0x00}, q[] = {0x07, 0x00};
    bsk_txn_late_t l0 = bsk_txn_late(), l;
    printf("TRANSACTION : le 0x04 hors de sa fenêtre, jeté ; ce qui l'attendait part, le message accroché reste\n");
    txn_boot();
    bsk_phy_vd(60);
    bsk_txn_loop(true);
    obs_n = 0;
    bsk_txn_loop_observer(obs);
    bsk_txn_attach(m1d, sizeof m1d);
    g_ps_n = 0;
    bsk_txn_real(VD1 + 8600);
    pump(VD1 + 8600);
    CHECK(g_ps_n == 1 && g_ps_log[0].frame.msg[0] == 0x03, "le 0x03 à son échéance");
    CHECK(bsk_txn_send(stop, sizeof stop) == E_OK && bsk_txn_request(q, sizeof q, 300, ps_now()) == TXN_WAIT && g_ps_n == 1,
          "le 0x1C et la requête retenus");
    bsk_txn_real(VD1 + 12101);
    pump(VD1 + 12101);
    l = bsk_txn_late();
    CHECK(g_ps_n == 3 && g_ps_log[1].frame.msg[0] == 0x1C && g_ps_log[2].frame.msg[0] == 0x07 && g_ps_log[2].frame.cls == 2 &&
              g_ps_log[1].frame.seq == g_ps_log[0].frame.seq && g_ps_log[2].frame.seq == (uint8_t)(g_ps_log[0].frame.seq + 1),
          "le 0x04 2001 µs après son échéance : jeté ; le 0x1C puis la requête partent, dans l'ordre, sans 0x04");
    CHECK(l.n == l0.n + 1 && l.max_us >= 2001 && obs_n == 0, "une paire jetée, comptée ; pas de compte rendu");
    CHECK(!bsk_txn_attach_failed(), "N2 : le 0x04 jeté hors de sa fenêtre n'a pas été tenté : un report sans émission");
    CHECK(bsk_txn_state() == TXN_WAIT && bsk_txn_deadline() == VD1 + 12101 + 300 * MS, "la requête partie, son échéance de là");
    CHECK(bsk_txn_send(stop, sizeof stop) == E_OK && g_ps_n == 4 && g_ps_log[3].frame.msg[0] == 0x1C,
          "un 0x1C demandé ensuite part derrière eux, tout de suite");
    bsk_txn_real(VD1 + 16666 + 10100);
    pump(VD1 + 16666 + 10100);
    CHECK(g_ps_n == 6 && g_ps_log[5].frame.msg[0] == 0x04 && g_ps_log[5].frame.len == 19 &&
              !memcmp(g_ps_log[5].frame.msg + 14, m1d, sizeof m1d) && obs_n == 1 && bsk_txn_late().n == l.n,
          "la paire suivante, à l'heure, porte le message accroché ; son compte rendu");
    bsk_txn_loop(false);
    bsk_txn_loop_observer(NULL);
    bsk_phy_vd(0);
    bsk_txn_ack();
}

/* Les 0x03 et 0x04 émis depuis l'entrée `from` du journal : combien portent le bit AF, combien de 0x04
 * portent un 0x1D. */
static void count_af_1d(size_t from, size_t *af, size_t *n1d)
{
    *af = *n1d = 0;
    for (size_t i = from; i < g_ps_n; i++) {
        const bsk_frame_t *f = &g_ps_log[i].frame;
        if (g_ps_log[i].kind != PS_SEND) continue;
        if (f->msg[0] == 0x03 && (f->msg[13] & 0x10)) (*af)++;
        if (f->msg[0] == 0x04 && f->len == 19 && f->msg[14] == 0x1D) (*n1d)++;
    }
}

static void t_txn_goto(void)
{
    static const uint8_t m1d[] = {0x1D, 0x56, 0x34, 0x00, 0x00};
    size_t af, n1d, from;
    uint32_t r0 = bsk_bench_refused();
    printf("TRANSACTION : 0x1D accroché au 0x04, bit AF tenu 30 paires, arrêt 0x1C, paire ratée reportée, boucle arrêtée\n");
    txn_boot();
    bsk_phy_vd(60);
    bsk_txn_loop(true);
    pump(T0 + 17 * MS);
    from = g_ps_n;
    drive_goto(0x3456);
    CHECK(g_ps_n == from, "rien n'est émis au dépôt");
    pump(T0 + 34 * MS);
    CHECK(g_ps_n == from + 2 && g_ps_log[from].frame.msg[0] == 0x03 && (g_ps_log[from].frame.msg[13] & 0x10) &&
              g_ps_log[from].frame.len == 21,
          "la paire suivante : 0x03 avec le bit AF (offset 12, bit 4, std.c:407-408)");
    CHECK(g_ps_n == from + 2 && g_ps_log[from + 1].frame.len == 19 && g_ps_log[from + 1].frame.msg[0] == 0x04 &&
              g_ps_log[from + 1].frame.cls == 1 && !memcmp(g_ps_log[from + 1].frame.msg + 14, m1d, sizeof m1d) &&
              g_ps_log[from + 1].frame.seq == g_ps_log[from].frame.seq,
          "0x04 puis 1D 56 34 00 00 dans la même trame (unité 0, mode 0, std.c:568), sous le numéro du 0x03");
    pump(T0 + 700 * MS);
    count_af_1d(from, &af, &n1d);
    CHECK(af == 30 && n1d == 1, "bit AF sur 30 paires exactement (%zu), un seul 0x1D (%zu)", af, n1d);
    CHECK(bsk_bench_refused() == r0, "la trame 0x04 + 0x1D passe bench_core");

    from = g_ps_n;
    drive_goto(0x3456);
    CHECK(drive_stop() == E_OK && g_ps_n == from + 1 && g_ps_log[from].frame.len == 1 &&
              g_ps_log[from].frame.msg[0] == 0x1C && g_ps_log[from].frame.cls == 1,
          "arrêt : 0x1C seul, en classe 1");
    pump(T0 + 800 * MS);
    count_af_1d(from, &af, &n1d);
    CHECK(af == 0 && n1d == 0, "après l'arrêt : ni la consigne pas partie, ni le bit AF (%zu, %zu)", n1d, af);

    /* La paire qui porte le 0x1D ratée, deux fois : son 0x03 (le 0x04 ne part pas sans lui), puis le 0x04 de la suivante
     * (E_BUS : parti peut-être en partie). Ni 0x1C ni consigne perdue : le même 0x1D dans la paire d'après (décision de
     * l'humain du 2026-10-07 : une position absolue se renvoie). Le bit AF n'est décompté que par les paires parties : 30,
     * et le 0x03 de la paire dont le 0x04 a raté le porte aussi, 31 en tout. Un front toutes les 16666 µs depuis T0 : à
     * T0 + 799,97 ms (0x03 à 808,57), 816,63 ms (0x03 à 825,23, 0x04 à 826,73), 833,30 ms (0x04 à 843,40). */
    from = g_ps_n;
    drive_goto(0x3456);
    g_ps.fail_sends = 1;                              /* le 0x03 du front de 799,97 ms */
    pump(T0 + 826 * MS);                              /* le 0x03 du front de 816,63 ms part */
    CHECK(g_ps.fail_sends == 0 && g_ps_n == from + 1 && g_ps_log[from].frame.msg[0] == 0x03, "un 0x03 raté, le suivant parti");
    CHECK(!bsk_txn_attach_failed(), "N2 : le 0x03 raté, son 0x04 jamais tenté : rien de noté");
    g_ps.fail_sends = 1;                              /* son 0x04 */
    pump(T0 + 844 * MS);
    count_af_1d(from, &af, &n1d);
    CHECK(sent(from, 1, 0x1C) < 0 && g_ps.fail_sends == 0, "la paire qui porte le 0x1D ratée, deux fois : pas de 0x1C");
    CHECK(bsk_txn_attach_failed() && drive_goto_failed(),
          "N2 : le 0x04 qui porte le 0x1D tenté et raté (parti peut-être) : noté, lu par MOUVEMENT");
    CHECK(n1d == 1 && g_ps_n == from + 3 && g_ps_log[g_ps_n - 1].frame.msg[0] == 0x04 &&
              !memcmp(g_ps_log[g_ps_n - 1].frame.msg + 14, m1d, sizeof m1d),
          "la paire d'après, partie, porte le même 0x1D (std.c:568)");
    pump(T0 + 1400 * MS);
    count_af_1d(from, &af, &n1d);
    CHECK(af == 31 && n1d == 1, "bit AF : 30 paires parties et le 0x03 seul (%zu), un seul 0x1D parti (%zu)", af, n1d);
    CHECK(drive_goto_failed(), "N2 : toujours noté une fois le 0x1D reporté parti");

    from = g_ps_n;
    drive_goto(0x3456);
    CHECK(!drive_goto_failed(), "N2 : un nouveau dépôt part d'une émission unique");
    bsk_txn_loop(false);
    drive_forget();                   /* l'oubli de MOUVEMENT, après l'arrêt de la boucle, comme la session */
    bsk_txn_loop(true);
    pump(T0 + 1600 * MS);
    count_af_1d(from, &af, &n1d);
    CHECK(g_ps_n > from && af == 0 && n1d == 0, "boucle arrêtée puis relancée : la consigne et le bit AF sont oubliés");
    bsk_phy_vd(0);
}

static void t_txn_seq(void)
{
    const uint8_t q[] = {0x07, 0x00};
    int wrapped = 0;
    printf("TRANSACTION : numéro de séquence de 0 à 0xEF (link.c:seq_advance)\n");
    txn_boot();
    for (int i = 0; i < 0x200; i++) {
        (void)bsk_txn_request(q, sizeof q, 1, ps_now());
        bsk_txn_ack();
        if (i && g_ps_log[i].frame.seq != (uint8_t)(g_ps_log[i - 1].frame.seq + 1)) {
            wrapped += g_ps_log[i - 1].frame.seq == 0xEF && g_ps_log[i].frame.seq == 0;
            CHECK(g_ps_log[i - 1].frame.seq == 0xEF && g_ps_log[i].frame.seq == 0, "après 0x%02X : 0x%02X",
                  g_ps_log[i - 1].frame.seq, g_ps_log[i].frame.seq);
        }
    }
    CHECK(wrapped >= 2, "le numéro revient à 0 après 0xEF");
}

/* ─────────────────────────── SESSION ─────────────────────────── */

static uint32_t cmd(bsk_cmd_op_t op, int32_t arg);

static void t_off(void)
{
    printf("OFF : D2 absent, aucune communication (E1)\n");
    boot();
    run_for(10000 * MS);
    CHECK(g_ps_n == 0, "10 s sans D2 : ni trame, ni BODY_CS, ni rail, ni VD");
    CHECK(status().session_state == SESSION_OFF, "OFF");
}

/* Démarrage d'un objectif fraîchement alimenté, 135 des traces, sans 0x3F. L'init faite, READY sans homing du
 * pilote ; les bornes sont celles du 0x06 des traces, offsets 7-10 : 31 36 12 78, 13873 / 30738 (dump05:11),
 * resserrées de 5 pas : 13878 / 30733. */
static void t_cold_start(void)
{
    long i01, i10, i10r, i, i1c;
    uint64_t t01;
    bsk_status_t s;
    printf("démarrage : POWERING, poignée de main, IDENTIFYING, init, boucle avec le 0x10, READY\n");
    boot();
    lens_135();
    ps_presence(true);
    run_for(1 * MS);
    CHECK(entered(SESSION_POWERING, 0) == T0, "D2 présent : POWERING tout de suite");
    i = act(0, PS_RAIL);
    CHECK(i >= 0 && g_ps_log[i].rail == BSK_RAIL_LOGIC && g_ps_log[i].on && t_of(i) == T0, "rail logique d'abord");
    run_for(3000 * MS);
    i = act((size_t)(i + 1), PS_RAIL);
    CHECK(i >= 0 && g_ps_log[i].rail == BSK_RAIL_MOTOR && g_ps_log[i].on && t_of(i) == T0 + 50 * MS, "rail moteur 50 ms après");
    i = act(0, PS_VD);
    CHECK(i >= 0 && g_ps_log[i].hz == 60 && t_of(i) == T0 + 100 * MS, "VD à 60 Hz 50 ms après");
    i = act(0, PS_BODY_CS);
    CHECK(i >= 0 && !g_ps_log[i].on && t_of(i) == T0 + 100 * MS, "poignée de main : BODY_CS basse");
    i = act((size_t)(i + 1), PS_BODY_CS);
    CHECK(i >= 0 && g_ps_log[i].on && t_of(i) == T0 + 115 * MS, "BODY_CS haute 15 ms après");
    i = act((size_t)(i + 1), PS_BODY_CS);
    CHECK(i >= 0 && !g_ps_log[i].on && t_of(i) == T0 + 115 * MS + 1100 + 3 * MS, "BODY_CS basse 3 ms après LENS_CS haute");
    i01 = sent(0, 2, 0x01);
    t01 = t_of(i01);
    CHECK(i01 >= 0 && t01 == T0 + 115 * MS + 2200 + 3 * MS, "0x01 à la retombée de LENS_CS");
    CHECK(entered(SESSION_IDENTIFYING, 0) == t01, "IDENTIFYING avec le 0x01");
    CHECK(count_sent(0, t01, 0x03) == 0, "aucune boucle avant le 0x01");
    {
        /* le 0x3F sans réponse, renvoyé tel quel, trois envois au total */
        static const uint8_t order[] = {0x01, 0x07, 0x3F, 0x3F, 0x3F, 0x08, 0x0B, 0x09, 0x0D, 0x10, 0x0A};
        size_t k = 0;
        for (size_t j = 0; j < g_ps_n; j++)
            if (g_ps_log[j].kind == PS_SEND && g_ps_log[j].frame.cls == 2) {
                CHECK(k < sizeof order && g_ps_log[j].frame.msg[0] == order[k], "requête %zu : 0x%02X", k,
                      g_ps_log[j].frame.msg[0]);
                k++;
            }
        CHECK(k == sizeof order, "les requêtes d'init, dans l'ordre de la v1, le 0x3F trois fois, et rien d'autre (%zu)", k);
    }
    i = sent(0, 2, 0x08);
    CHECK(i >= 0 && g_ps_log[i].frame.msg[1] == 0x06, "0x08 : drapeaux 06, Samyang reconnu par son LensType2");
    CHECK(i >= 0 && sent(0, 2, 0x3F) >= 0 && t_of(i) == t_of(sent(0, 2, 0x3F)) + 3 * 300 * MS,
          "0x3F sans réponse : renvoyé à chaque échéance, 300 ms, puis 0x08 à l'échéance du troisième");
    i10 = sent(0, 2, 0x10);
    i10r = sent(0, 2, 0x0A);
    CHECK(i10 >= 0 && g_ps_log[i10].frame.msg[1] == 0x1F, "0x10 1F");
    CHECK(i10 >= 0 && i10r >= 0 && count_sent(t_of(i10), t_of(i10r), 0x03) >= 30,
          "la boucle tourne pendant le 0x10, lancée avec lui (Samyang reconnu)");
    i1c = sent(0, 1, 0x1C);
    CHECK(i10r >= 0 && i1c < 0 && entered(SESSION_HOMING, 0) == 0 && entered(SESSION_READY, 0) > t_of(i10r) &&
              entered(SESSION_READY, 0) < t_of(i10r) + 100 * MS,
          "init faite : READY au premier 0x05, sans homing du pilote (ni 0x1C, ni HOMING ; lensctl.c:232)");
    CHECK(sent(0, 2, 0x40) < 0 && sent(0, 1, 0x40) < 0, "aucune trame 0x40");
    CHECK(ap03_all(0, 0x11BF), "chaque 0x03 porte BF 11 BF 11 aux offsets 3-6 : f/1,8 de chaque session (std.c:123, B2 11), "
          "ramenée au minimum de la plage du 0x08, BF 11");
    s = status();
    CHECK(s.session_state == SESSION_READY && s.last_error == E_OK, "READY");
    CHECK(s.focus_min == 13878 && s.focus_max == 30733, "bornes : le 0x06, offsets 7-10, 31 36 12 78, ± 5 pas (%d / %d)",
          (int)s.focus_min, (int)s.focus_max);
    CHECK(s.capabilities & CAP_LIMITS_REPORTED, "CAP_LIMITS_REPORTED : les bornes annoncées par l'objectif, lues au 0x06");
    CHECK(s.position_valid && s.focus_position == 14623, "position publiée : celle du 0x06 (dump05:11, 14623)");
    CHECK(s.lens_id_product == 8 && s.lens_name[0] == 0, "identité : LensType2 8 ; pas de nom sans 0x3F");
    CHECK(bsk_bench_refused() == refused0, "aucune trame refusée par bench_core");
}

/* Les bornes relues à chaque 0x06 valide, offsets 7-10, avec la position ; un 0x06 de moins de 12 octets ne rafraîchit
 * rien ; sans 0x06, toute cible est refusée. Chaque borne resserrée de 5 pas ; une borne publiée à 0 ou à 65535 ne
 * déborde pas. Valeurs de test, sans autre source. */
static void t_limits_06(void)
{
    bsk_status_t s;
    uint32_t q;
    printf("bornes : le 0x06, offsets 7-10, suivies quand elles changent ; 0x06 trop court ignoré ; sans 0x06, tout refusé\n");
    boot();
    lens_135();
    ps_presence(true);
    run_for(3000 * MS);
    s = status();
    CHECK(s.session_state == SESSION_READY && s.position_valid && s.focus_min == 13878 && s.focus_max == 30733,
          "READY, bornes du 0x06 des traces, ± 5 pas (%d / %d)", (int)s.focus_min, (int)s.focus_max);
    set_limits(15000, 20000);
    set_pos(16000);
    run_for(100 * MS);
    s = status();
    CHECK(s.focus_min == 15005 && s.focus_max == 19995 && s.focus_position == 16000, "bornes et position suivies (%d / %d, %d)",
          (int)s.focus_min, (int)s.focus_max, (int)s.focus_position);
    q = cmd(CMD_FOCUS_GOTO, 15000);
    CHECK(n_acks > 0 && ACKS[n_acks - 1].a.seq == q && ACKS[n_acks - 1].a.reason == E_LIMIT, "à la borne publiée 15000 : E_LIMIT");
    q = cmd(CMD_FOCUS_GOTO, 20000);
    CHECK(n_acks > 0 && ACKS[n_acks - 1].a.seq == q && ACKS[n_acks - 1].a.reason == E_LIMIT, "à la borne publiée 20000 : E_LIMIT");
    set_limits(15100, 20100);
    set_pos(16100);
    g_ps.n06 = 11;                                  /* la seconde borne coupée : trop court */
    run_for(100 * MS);
    s = status();
    CHECK(s.focus_min == 15005 && s.focus_max == 19995 && s.focus_position == 16000,
          "0x06 de 11 octets : ni bornes ni position rafraîchies (%d / %d, %d)", (int)s.focus_min, (int)s.focus_max,
          (int)s.focus_position);
    g_ps.n06 = 12;
    run_for(100 * MS);
    s = status();
    CHECK(s.focus_min == 15105 && s.focus_max == 20095 && s.focus_position == 16100, "0x06 de 12 octets : lu (%d / %d, %d)",
          (int)s.focus_min, (int)s.focus_max, (int)s.focus_position);
    set_limits(0, 65535);
    run_for(100 * MS);
    s = status();
    CHECK(s.focus_min == 5 && s.focus_max == 65530, "bornes publiées 0 / 65535 : 5 / 65530, sans débordement (%d / %d)",
          (int)s.focus_min, (int)s.focus_max);
    set_limits(0, 0);
    run_for(100 * MS);
    s = status();
    q = cmd(CMD_FOCUS_GOTO, 0);
    CHECK(s.focus_min == 5 && s.focus_max == -5 && n_acks > 0 && ACKS[n_acks - 1].a.seq == q &&
              ACKS[n_acks - 1].a.reason == E_LIMIT,
          "bornes publiées 0 / 0 : 5 / -5, aucune cible admise (%d / %d)", (int)s.focus_min, (int)s.focus_max);

    boot();                                         /* un objectif qui n'émet que des 0x05 : ni position ni bornes */
    lens_135();
    g_ps.n06 = 0;
    ps_presence(true);
    run_for(3000 * MS);
    s = status();
    CHECK(s.session_state == SESSION_READY && !s.position_valid, "READY sans 0x06 : position invalide");
    CHECK(!(s.capabilities & CAP_LIMITS_REPORTED), "sans 0x06 : aucune borne annoncée, pas de CAP_LIMITS_REPORTED");
    q = cmd(CMD_SET_MARK, BSK_MARK_HERE);           /* ni position ni bornes, pas de marque */
    CHECK(n_acks > 0 && ACKS[n_acks - 1].a.seq == q && ACKS[n_acks - 1].a.reason == E_LIMIT && !status().mark_valid,
          "sans 0x06, CMD_SET_MARK (ms) : E_LIMIT, pas de marque");
    for (int32_t t = 0; t <= 20000; t += 20000) {   /* 0 aussi : sans 0x06, les bornes valent 0 */
        q = cmd(CMD_FOCUS_GOTO, t);
        CHECK(n_acks > 0 && ACKS[n_acks - 1].a.seq == q && ACKS[n_acks - 1].a.result == ACK_REJECTED &&
                  ACKS[n_acks - 1].a.reason == E_LIMIT && status().motion_state == MOTION_IDLE,
              "sans 0x06, un goto vers %d : E_LIMIT, rien déposé", (int)t);
    }
}

static void t_not_samyang(void)
{
    long i10, i10r;
    printf("objectif non Samyang (0x07 Tamron) : READY, sans boucle avec le 0x10, sans 0x40\n");
    boot();
    lens_135();
    ps_answer(0x07, TAMRON07, sizeof TAMRON07, 5 * MS);
    ps_presence(true);
    run_for(4000 * MS);
    i10 = sent(0, 2, 0x10);
    i10r = sent(0, 2, 0x0A);
    CHECK(status().session_state == SESSION_READY, "READY");
    CHECK(sent(0, 2, 0x08) >= 0 && g_ps_log[sent(0, 2, 0x08)].frame.msg[1] == 0x00, "0x08 : drapeaux 00");
    CHECK(i10 >= 0 && i10r >= 0 && count_sent(0, t_of(i10r), 0x03) == 0, "aucune boucle avant la fin de l'init");
    CHECK(count_sent(t_of(i10r), ps_now(), 0x03) > 0, "la boucle après l'init (lensctl.c:151)");
    CHECK(sent(0, 2, 0x40) < 0 && sent(0, 1, 0x40) < 0, "aucune trame 0x40");
    CHECK(status().lens_id_product == 0xC134, "identité : LensType2 0xC134");
    CHECK(bsk_bench_refused() == refused0, "aucune trame refusée par bench_core");
}

static void t_home_failed_init(void)
{
    printf("10 01 au 0x10 de l'init : FAULT, E_HOME_FAILED (E3)\n");
    boot();
    lens_135();
    ps_answer(0x07, TAMRON07, sizeof TAMRON07, 5 * MS);
    ps_answer(0x10, TAMRON10, sizeof TAMRON10, 700 * MS);
    ps_presence(true);
    run_for(10000 * MS);
    CHECK(status().session_state == SESSION_FAULT && status().last_error == E_HOME_FAILED, "FAULT, E_HOME_FAILED");
    CHECK(sent(0, 2, 0x0A) < 0, "pas de 0x0A");
    CHECK(entered(SESSION_RECOVERING, 0) == 0, "sans passer par la reprise");
}

static void t_10_late(void)
{
    long i10, i0a;
    printf("10 xx retardé : le 0x0A ne part qu'après lui\n");
    boot();
    lens_135();
    ps_answer(0x10, (const uint8_t[]){0x10, 0x00}, 2, 7000 * MS);
    ps_presence(true);
    run_for(9000 * MS);
    i10 = sent(0, 2, 0x10);
    i0a = sent(0, 2, 0x0A);
    CHECK(i10 >= 0 && i0a >= 0 && t_of(i0a) == t_of(i10) + 7000 * MS, "0x0A à la réponse, 7 s après le 0x10");
    CHECK(status().session_state == SESSION_READY, "READY");
}

/* Les requêtes de classe 2 émises avant `t_end`, dans l'ordre, comparées à `want` (n types). */
static bool requests_are(const uint8_t *want, size_t n, uint64_t t_end)
{
    size_t k = 0;
    for (size_t j = 0; j < g_ps_n && j < PS_LOG_CAP; j++)
        if (g_ps_log[j].kind == PS_SEND && g_ps_log[j].frame.cls == 2 && g_ps_log[j].t < t_end) {
            if (k >= n || g_ps_log[j].frame.msg[0] != want[k]) return false;
            k++;
        }
    return k == n;
}

/* Un 0x01 sans réponse est renvoyé, tel quel, à son échéance (init_ms, 300 ms) ; répondu au deuxième essai, l'init
 * continue sans reset soft. */
static void t_01_once(void)
{
    static const uint8_t order[] = {0x01, 0x01, 0x07, 0x3F, 0x3F, 0x3F, 0x08, 0x0B, 0x09, 0x0D, 0x10, 0x0A};   /* le 0x3F, trois envois */
    long i01, i01b, i07;
    printf("0x01 sans réponse une fois, puis répondu : le même 0x01 300 ms après, l'init, READY, sans reset soft\n");
    boot();
    lens_135();
    g_ps.skip[0x01] = 1;
    ps_presence(true);
    run_for(4000 * MS);
    i01 = sent(0, 2, 0x01);
    i01b = i01 >= 0 ? sent((size_t)i01 + 1, 2, 0x01) : -1;
    i07 = sent(0, 2, 0x07);
    CHECK(i01b >= 0 && t_of(i01b) == t_of(i01) + 300 * MS && g_ps_log[i01b].frame.len == g_ps_log[i01].frame.len &&
              !memcmp(g_ps_log[i01b].frame.msg, g_ps_log[i01].frame.msg, g_ps_log[i01].frame.len),
          "le deuxième essai : le même 0x01, à l'échéance du premier (300 ms)");
    CHECK(i07 > i01b && t_of(i07) == t_of(i01b) + 5 * MS, "répondu : le 0x07 à sa réponse");
    CHECK(requests_are(order, sizeof order, UINT64_MAX), "deux 0x01, puis l'init ; le seul 0x0A est celui de l'init : pas de reset soft");
    CHECK(status().session_state == SESSION_READY && entered(SESSION_RECOVERING, 0) == 0, "READY, sans reprise");
}

/* Trois 0x01 sans réponse, puis le reset soft : le 0x0A de l'init, 400 ms d'échéance (info_ms), puis l'init complète
 * depuis le 0x01, comme un démarrage. */
static void t_01_reset(void)
{
    static const uint8_t order[] = {0x01, 0x01, 0x01, 0x0A, 0x01, 0x07, 0x3F, 0x3F, 0x3F, 0x08, 0x0B, 0x09, 0x0D, 0x10,
                                    0x0A};   /* le 0x3F, trois envois */
    static const uint8_t m0a[] = {0x0A, 0xFF, 0x7F, 0, 0, 0, 0, 0, 0, 0x3F, 0, 0, 0, 0, 0, 0, 0};
    long i01, i0a, i01r;
    printf("0x01 sans réponse trois fois : reset soft (0x0A), puis l'init depuis le 0x01, READY, sans échec compté\n");
    boot();
    lens_135();
    g_ps.skip[0x01] = 3;
    ps_presence(true);
    run_for(5000 * MS);
    i01 = sent(0, 2, 0x01);
    i0a = sent(0, 2, 0x0A);
    i01r = i0a >= 0 ? sent((size_t)i0a + 1, 2, 0x01) : -1;
    CHECK(requests_are(order, sizeof order, UINT64_MAX), "trois 0x01, le 0x0A du reset soft, puis l'init entière");
    CHECK(i01 >= 0 && i0a >= 0 && t_of(i0a) == t_of(i01) + 900 * MS, "le 0x0A à l'échéance du troisième 0x01 (3 x 300 ms)");
    CHECK(i0a >= 0 && g_ps_log[i0a].frame.len == sizeof m0a && !memcmp(g_ps_log[i0a].frame.msg, m0a, sizeof m0a),
          "le 0x0A du reset soft : celui de l'init, 0A FF 7F 00 00 00 00 00 00 3F 00…");
    CHECK(i01r >= 0 && t_of(i01r) == t_of(i0a) + 5 * MS, "le 0x01 à la réponse au 0x0A");
    CHECK(count_sent(0, t_of(i01r), 0x03) == 0, "aucune boucle avant l'init");
    CHECK(status().session_state == SESSION_READY && entered(SESSION_RECOVERING, 0) == 0 &&
              entered(SESSION_POWERING, 1) == 0,
          "READY, sans échec compté ni nouveau POWERING");
}

/* Un objectif muet. Trois 0x01, le reset soft (sans réponse, 400 ms), trois 0x01 : l'échec est compté (E5),
 * RECOVERING ; aucune boucle, jamais. La coupure désactivée (par défaut) : 3 s d'attente, puis POWERING, sans couper
 * les rails, sans mettre les lignes au repos, VD en marche. */
static void t_01_silent(void)
{
    static const uint8_t order[] = {0x01, 0x01, 0x01, 0x0A, 0x01, 0x01, 0x01};
    long i01, i;
    uint64_t tr;
    printf("objectif muet : 3 x 0x01, reset soft, 3 x 0x01, échec compté (E5), RECOVERING, POWERING 3 s après, sans coupure\n");
    boot();
    g_ps.handshake = true;
    ps_presence(true);
    run_for(7000 * MS);
    i01 = sent(0, 2, 0x01);
    tr = entered(SESSION_RECOVERING, 0);
    CHECK(tr && requests_are(order, sizeof order, tr + 1), "trois 0x01, le 0x0A, trois 0x01, puis l'échec");
    CHECK(i01 >= 0 && tr == t_of(i01) + 2200 * MS, "RECOVERING 3 x 300 + 400 + 3 x 300 ms après le premier 0x01 (%llu)",
          (unsigned long long)(tr - t_of(i01)));
    CHECK(count_sent(0, ps_now(), 0x03) == 0, "aucune boucle");
    CHECK(entered(SESSION_POWERING, chg_index_after(tr)) == tr + 3000 * MS, "POWERING 3 s après (retry_wait_ms)");
    for (i = act(0, PS_RAIL); i >= 0; i = act((size_t)i + 1, PS_RAIL))
        CHECK(g_ps_log[i].on, "aucun rail coupé (%llu µs)", (unsigned long long)t_of(i));
    for (i = act(0, PS_LINES); i >= 0; i = act((size_t)i + 1, PS_LINES))
        CHECK(g_ps_log[i].on, "aucune ligne mise au repos (%llu µs)", (unsigned long long)t_of(i));
    for (i = act(0, PS_VD); i >= 0; i = act((size_t)i + 1, PS_VD))
        CHECK(g_ps_log[i].hz == 60, "la VD jamais arrêtée (%llu µs)", (unsigned long long)t_of(i));
    i = act(0, PS_BODY_CS);
    while (i >= 0 && !(t_of(i) > tr && g_ps_log[i].on)) i = act((size_t)i + 1, PS_BODY_CS);
    CHECK(i >= 0 && t_of(i) == tr + 3000 * MS + 115 * MS, "la poignée de main est ré-émise");
}

/* La même reprise, la coupure activée (cut_rails) : RECOVERING, 3 s, rails coupés 500 ms, POWERING. */
static void t_01_silent_cut(void)
{
    long i;
    uint64_t tr;
    printf("objectif muet, coupure activée (cut_rails) : RECOVERING, 3 s, rails coupés 500 ms, POWERING, poignée de main\n");
    boot_cut(true);
    g_ps.handshake = true;
    ps_presence(true);
    run_for(7000 * MS);
    tr = entered(SESSION_RECOVERING, 0);
    i = act(0, PS_RAIL);
    while (i >= 0 && t_of(i) < tr) i = act((size_t)i + 1, PS_RAIL);
    CHECK(i >= 0 && g_ps_log[i].rail == BSK_RAIL_MOTOR && !g_ps_log[i].on && t_of(i) == tr + 3000 * MS,
          "3 s après : rail moteur coupé");
    CHECK(i >= 0 && g_ps_log[i + 1].kind == PS_RAIL && g_ps_log[i + 1].rail == BSK_RAIL_LOGIC && !g_ps_log[i + 1].on,
          "puis le rail logique");
    CHECK(entered(SESSION_POWERING, chg_index_after(tr)) == tr + 3500 * MS, "POWERING 500 ms plus tard");
    i = act(0, PS_BODY_CS);
    while (i >= 0 && !(t_of(i) > tr && g_ps_log[i].on)) i = act((size_t)i + 1, PS_BODY_CS);
    CHECK(i >= 0 && t_of(i) == tr + 3500 * MS + 115 * MS, "la poignée de main est ré-émise");
    run_for(30000 * MS);
    CHECK(status().session_state == SESSION_FAULT && status().last_error == E_LOST && count_sent(0, ps_now(), 0x01) == 24 &&
              entered(SESSION_OFF, 0) == 0,
          "au 4e échec : FAULT, E_LOST, sans passer par OFF (compteur tenu à travers la coupure)");
}

static void t_nocap(void)
{
    long i0a;
    uint64_t tr;
    printf("init servie, aucun 0x05 : non servi (NOCAP), échec compté, RECOVERING\n");
    boot();
    lens_135();
    g_ps.flow_on_0a = false;
    ps_presence(true);
    run_for(6000 * MS);
    i0a = sent(0, 2, 0x0A);
    tr = entered(SESSION_RECOVERING, 0);
    CHECK(i0a >= 0 && tr == t_of(i0a) + 5 * MS + 500 * MS + 300 * MS,
          "RECOVERING 500 ms (premier 0x05) + 300 ms (sonde) après la réponse au 0x0A");
    CHECK(entered(SESSION_HOMING, 0) == 0, "jamais servi");
    CHECK(count_sent(tr + 2900 * MS, tr + 3000 * MS, 0x03) > 0, "la boucle tourne pendant l'attente, comme la v1");
    /* Sans coupure, la VD tourne toujours : la fenêtre part de POWERING, 3 s après RECOVERING. */
    CHECK(count_sent(tr + 3000 * MS, tr + 3100 * MS, 0x03) == 0,
          "POWERING oublie la session : boucle arrêtée, la VD en marche (lensctl.c:114)");
}

/* La coupure de RECOVERING (cut_rails) relâche les lignes la boucle encore lancée (la sonde a échoué, rien n'a oublié
 * la session) ; la paire du dernier front, pas encore partie, ne part pas vers des lignes au repos. L'attente est
 * choisie pour que la coupure tombe entre un front et son 0x04 : vérifié ci-dessous. */
static void t_cut_pending_pair(void)
{
    long ivd, ilines;
    uint64_t tr, tcut, tv;
    printf("RECOVERING, coupure : la paire en attente ne part pas vers des lignes au repos\n");
    boot_cut(true);
    P.retry_wait_ms = 3004;                         /* la coupure 9418 µs après un front : entre son 0x03 et son 0x04 */
    bsk_session_init(&P);
    lens_135();
    g_ps.flow_on_0a = false;
    ps_presence(true);
    run_for(6000 * MS);
    tr = entered(SESSION_RECOVERING, 0);
    tcut = tr + 3004 * MS;
    ivd = -1;                                       /* la VD lancée en dernier avant la coupure */
    for (long i = act(0, PS_VD); i >= 0 && t_of(i) < tcut; i = act((size_t)i + 1, PS_VD))
        if (g_ps_log[i].hz) ivd = i;
    tv = t_of(ivd) + 16666 * ((tcut - t_of(ivd)) / 16666);   /* le dernier front avant la coupure */
    ilines = act(0, PS_LINES);
    while (ilines >= 0 && (g_ps_log[ilines].on || g_ps_log[ilines].t < tcut)) ilines = act((size_t)ilines + 1, PS_LINES);
    CHECK(tr && ivd >= 0 && t_of(ilines) == tcut && tcut > tv + 8600 && tcut < tv + 10100 && count_sent(tv, tcut, 0x03) == 1 &&
              count_sent(tv, tcut, 0x04) == 0,
          "la coupure (%llu µs après le front) tombe entre le 0x03 et le 0x04 de ce front",
          (unsigned long long)(tcut - tv));
    CHECK(count_sent(tcut, tv + 16666, 0x04) == 0, "pas de 0x04 après les lignes relâchées");
}

/* 0x0B, 0x09, 0x0D exigés : la séquence s'arrête au premier qui manque, après ses trois envois ; 0x0A manquant : init
 * non faite, sans renvoi. Dans les quatre cas : la boucle, la sonde, le homing standard, READY. */
static void t_required_missing(void)
{
    static const uint8_t req[] = {0x0B, 0x09, 0x0D, 0x0A};
    for (size_t k = 0; k < sizeof req; k++) {
        long im, i1c;
        uint64_t tm;
        unsigned sends = req[k] == 0x0A ? 1 : 3;    /* le 0x0A n'est jamais renvoyé */
        printf("0x%02X manquant : on continue sans, la boucle, homing standard, READY\n", req[k]);
        boot();
        lens_135();
        no_answer(req[k]);
        g_ps.flow = true;
        ps_presence(true);
        run_for(7000 * MS);
        im = sent(0, 2, req[k]);
        tm = t_of(im) + (req[k] == 0x0A ? 400 * MS : 3 * 300 * MS);
        CHECK(im >= 0 && count_sent(0, UINT64_MAX, req[k]) == sends,
              "0x%02X : %u envoi(s)", req[k], sends);
        for (unsigned n = 1; im >= 0 && n < sends; n++)
            CHECK(count_sent(t_of(im) + n * 300 * MS, t_of(im) + n * 300 * MS + 1, req[k]) == 1,
                  "0x%02X : l'envoi %u à l'échéance du précédent, %u ms après le premier", req[k], n + 1, n * 300);
        {
            static const uint8_t init[] = {0x07, 0x3F, 0x08, 0x0B, 0x09, 0x0D, 0x0A};
            size_t other = 0;
            for (size_t j = 0; im >= 0 && j < sizeof init; j++)
                if (init[j] != req[k]) other += count_sent(t_of(im) + 1, ps_now(), init[j]);
            CHECK(im >= 0 && other == 0, "0x%02X : plus aucune autre requête d'init après lui", req[k]);
        }
        if (req[k] != 0x0A)   /* le 0x0A part après le 0x10, qui a déjà lancé la boucle (Samyang reconnu) */
            /* le premier 0x03, 8,6 ms après le premier front VD suivant (16,7 ms au plus) */
            CHECK(count_sent(t_of(im), tm, 0x03) == 0 && count_sent(tm, tm + 26 * MS, 0x03) >= 1,
                  "0x%02X : la boucle se lance à l'échéance exacte du troisième envoi, 900 ms, pas avant", req[k]);
        i1c = sent(0, 1, 0x1C);
        CHECK(i1c >= 0 && (req[k] == 0x0A ? t_of(i1c) == tm : t_of(i1c) >= tm) && sent((size_t)i1c, 2, 0x10) >= 0 &&
                  g_ps_log[sent((size_t)i1c, 2, 0x10)].frame.msg[1] == 0x08,
              "0x%02X : init non faite, homing standard : 0x1C puis 0x10 08 (0x0A : 0x1C à son échéance exacte, 400 ms)",
              req[k]);
        CHECK(status().session_state == SESSION_READY, "0x%02X : READY", req[k]);
    }
}

static void t_optional_missing(void)
{
    printf("0x07, 0x3F et 0x08 sans réponse : tolérés, l'init continue, READY sans homing du pilote\n");
    boot();
    lens_135();
    no_answer(0x07);
    no_answer(0x08);
    ps_presence(true);
    run_for(6000 * MS);
    CHECK(sent(0, 2, 0x0B) >= 0 && sent(0, 2, 0x0A) >= 0, "0x0B et la suite émis");
    CHECK(count_sent(0, UINT64_MAX, 0x07) == 3 && count_sent(0, UINT64_MAX, 0x08) == 3 && count_sent(0, UINT64_MAX, 0x3F) == 3,
          "0x07, 0x3F et 0x08 sans réponse : trois envois chacun");
    CHECK(sent(0, 2, 0x07) >= 0 && sent(0, 2, 0x3F) >= 0 &&
              t_of(sent(0, 2, 0x3F)) == t_of(sent(0, 2, 0x07)) + 3 * 300 * MS,
          "0x07 sans réponse : 0x3F à l'échéance du troisième, 3 x 300 ms");
    CHECK(sent(0, 2, 0x08) >= 0 && t_of(sent(0, 2, 0x0B)) == t_of(sent(0, 2, 0x08)) + 3 * 400 * MS,
          "0x08 sans réponse : 0x0B à l'échéance du troisième, 3 x 400 ms");
    CHECK(sent(0, 2, 0x0A) >= 0 && sent(0, 1, 0x1C) < 0 && entered(SESSION_HOMING, 0) == 0 &&
              status().session_state == SESSION_READY,
          "init faite : READY sans homing du pilote (ni 0x1C ni HOMING)");
    CHECK(status().lens_id_product == 0, "sans 0x07 : pas d'identité");
}

static void t_10_missing(void)
{
    long i10, ih;
    printf("0x10 de l'init sans réponse en 8 s : on continue sans, homing standard, READY\n");
    boot();
    lens_135();
    g_ps.skip[0x10] = 1;
    g_ps.flow = true;
    ps_presence(true);
    run_for(12000 * MS);
    i10 = sent(0, 2, 0x10);
    ih = i10 >= 0 ? sent((size_t)i10 + 1, 2, 0x10) : -1;
    CHECK(sent(0, 2, 0x0A) < 0, "pas de 0x0A");
    CHECK(i10 >= 0 && sent(0, 1, 0x1C) >= 0 && t_of(sent(0, 1, 0x1C)) == t_of(i10) + 8000 * MS,
          "à l'échéance de 8 s exactement, homing standard : 0x1C (le 0x05 est déjà là, servi tout de suite)");
    CHECK(i10 >= 0 && ih >= 0 && g_ps_log[ih].frame.msg[1] == 0x08 && t_of(ih) == t_of(i10) + 8100 * MS,
          "0x10 08 100 ms après");
    CHECK(status().session_state == SESSION_READY, "READY");
}

/* Une émission ratée (E_BUS de la PHY) est la seule erreur du bus qui termine une requête avant son échéance (bsk_txn.h) :
 * le 0x07 de l'init raté (on_send : le 0x01 vu, l'émission suivante ratée, rien n'en part), renvoyé tout de suite, à
 * l'instant de la réponse au 0x01 (5 ms après lui), `why=bus`. */
static void fail_after_01(const bsk_frame_t *f, uint64_t t)
{
    (void)t;
    if (f->msg[0] == 0x01) g_ps.fail_sends = 1;
}

static void t_resend_bus(void)
{
    long a, b;
    int r = 0;
    printf("une émission ratée termine la requête : le 0x07 de l'init raté, renvoyé tout de suite, why=bus\n");
    boot();
    lens_135();
    g_ps.on_send = fail_after_01;
    bsk_journal_set(true, false);
    ps_presence(true);
    run_for(300 * MS);
    g_ps.on_send = NULL;
    {
        char l[400];
        uint8_t g;
        size_t n;
        while ((n = bsk_journal_take(l, sizeof l - 1, &g)) > 0) {
            l[n] = 0;
            r += !strncmp(l, "* resend ", 9) && strstr(l, " msg=07 n=2 why=bus") != NULL;
        }
    }
    bsk_journal_set(false, false);
    a = sent(0, 2, 0x01);
    b = sent(0, 2, 0x07);
    CHECK(r == 1 && a >= 0 && b >= 0 && count_sent(0, UINT64_MAX, 0x07) == 1 && t_of(b) == t_of(a) + 5 * MS &&
              g_ps.fail_sends == 0,
          "le 0x07 raté : renvoyé à l'instant même, un seul sur le fil ; au journal, « msg=07 n=2 why=bus » (%d)", r);
}

/* Une réponse trop courte pour ce que la séquence en lit est l'absence de réponse de cette étape (L03-04). Le 135 des
 * traces, chaque réponse tronquée à un octet de moins que ce qui est lu : le 0x10 sans son octet de résultat, `10`
 * (l'init comme le homing du pilote, 700 ms) ; le 0x07 de 3 octets, `07 01 03` (l'identité en lit 4) ; le 0x08 de 5,
 * `08 BF 11 00 19` (la plage, octets 1-4). Attendus, à la main, comme sans réponse :
 *   - le 0x07 et le 0x08 renvoyés, trois envois chacun (renvois, `* resend`), puis tolérés : pas d'identité, pas de plage ;
 *   - le 0x10 de l'init : la séquence s'arrête là (init_end), pas de 0x0A ; le homing du pilote suit (0x1C, puis 0x10 08
 *     100 ms après) ;
 *   - le 0x10 08 du homing : un échec compté, RECOVERING à sa réponse, 700 ms après lui.
 * Sans la règle : identité et homing pris pour faits, le 0x0A émis, READY. */
static void t_short_replies(void)
{
    long i10, ih;
    printf("une réponse trop courte pour ce que la séquence en lit : comme sans réponse (0x07, 0x08, 0x10 de l'init et du "
           "homing)\n");
    boot();
    lens_135();
    ps_answer(0x07, (const uint8_t[]){0x07, 0x01, 0x03}, 3, 5 * MS);
    ps_answer(0x08, (const uint8_t[]){0x08, 0xBF, 0x11, 0x00, 0x19}, 5, 5 * MS);
    ps_answer(0x10, (const uint8_t[]){0x10}, 1, 700 * MS);
    g_ps.flow = true;                               /* le flux sans le 0x0A, comme après un 0x10 sans réponse (t_10_missing) */
    bsk_journal_set(true, false);
    ps_presence(true);
    run_for(6000 * MS);
    {
        char b[400];
        uint8_t g;
        size_t n;
        int r07 = 0, r08 = 0;
        while ((n = bsk_journal_take(b, sizeof b - 1, &g)) > 0) {
            b[n] = 0;
            r07 += !strncmp(b, "* resend ", 9) && (strstr(b, " msg=07 n=2 why=framing") || strstr(b, " msg=07 n=3 why=framing") ||
                                                    strstr(b, " msg=07 giveup why=framing"));
            r08 += !strncmp(b, "* resend ", 9) && (strstr(b, " msg=08 n=2 why=framing") || strstr(b, " msg=08 n=3 why=framing") ||
                                                    strstr(b, " msg=08 giveup why=framing"));
        }
        CHECK(r07 == 3 && r08 == 3, "au journal : deux renvois et l'abandon de chacun, why=framing (%d, %d)", r07, r08);
    }
    bsk_journal_set(false, false);
    i10 = sent(0, 2, 0x10);
    ih = i10 >= 0 ? sent((size_t)i10 + 1, 2, 0x10) : -1;
    CHECK(count_sent(0, UINT64_MAX, 0x07) == 3 && count_sent(0, UINT64_MAX, 0x08) == 3,
          "le 0x07 de 3 octets, le 0x08 de 5 : trois envois chacun (%zu, %zu)", count_sent(0, UINT64_MAX, 0x07),
          count_sent(0, UINT64_MAX, 0x08));
    CHECK(status().lens_id_product == 0 && !(status().capabilities & CAP_APERTURE), "ni identité, ni plage");
    CHECK(i10 >= 0 && g_ps_log[i10].frame.msg[1] == 0x1F && sent(0, 2, 0x0A) < 0,
          "le 0x10 de l'init sans son résultat : pas de 0x0A");
    CHECK(ih >= 0 && g_ps_log[ih].frame.msg[1] == 0x08 && sent((size_t)i10, 1, 0x1C) >= 0, "le homing du pilote : 0x1C, 0x10 08");
    CHECK(ih >= 0 && entered(SESSION_RECOVERING, 0) == t_of(ih) + 700 * MS && entered(SESSION_READY, 0) == 0,
          "le 0x10 08 sans son résultat : RECOVERING à sa réponse, jamais READY (%lld µs)",
          (long long)(entered(SESSION_RECOVERING, 0) - t_of(ih)));
}

static void t_name_3f(void)
{
    uint8_t m3f[66] = {0x3F, 0x00};
    long i10, i10r, i08;
    printf("reconnaissance par le nom 0x3F (LensType2 hors liste) : drapeaux 06, boucle avec le 0x10\n");
    boot();
    lens_135();
    memcpy(m3f + 2, NAME135, sizeof NAME135 - 1);
    ps_answer(0x07, TAMRON07, sizeof TAMRON07, 5 * MS);
    ps_answer(0x3F, m3f, sizeof m3f, 6 * MS);
    ps_presence(true);
    run_for(4000 * MS);
    i08 = sent(0, 2, 0x08);
    i10 = sent(0, 2, 0x10);
    i10r = sent(0, 2, 0x0A);
    CHECK(i08 >= 0 && g_ps_log[i08].frame.msg[1] == 0x06, "0x08 : drapeaux 06");
    CHECK(i10 >= 0 && i10r >= 0 && count_sent(t_of(i10), t_of(i10r), 0x03) > 0, "la boucle avec le 0x10");
    CHECK(!strcmp(status().lens_name, NAME135) && status().lens_id_product == 0xC134, "nom et LensType2 publiés");
    CHECK(status().session_state == SESSION_READY, "READY");
}

static void t_home_timeout(void)
{
    long ih;
    printf("homing standard sans réponse en 20 s : échec compté, RECOVERING\n");
    boot();
    lens_135();
    no_answer(0x0B);
    no_answer(0x10);
    g_ps.flow = true;
    ps_presence(true);
    run_for(25000 * MS);
    ih = sent(0, 2, 0x10);
    CHECK(ih >= 0 && g_ps_log[ih].frame.msg[1] == 0x08, "0x10 08 émis");
    CHECK(entered(SESSION_RECOVERING, 0) == t_of(ih) + 20000 * MS, "RECOVERING à 20 s");
}

/* M6 pour le 0x10, qu'on ne renvoie jamais (PROTOCOL.md § 7) : une erreur de réception pendant son attente ne coûte que
 * la trame. Le 135 des traces répond au 0x10 700 ms après lui ; un E_FRAMING 100 ms après l'envoi, un E_BUS 200 ms après.
 *   - le 0x10 1F de l'init : un seul envoi, sa réponse attendue, puis le 0x0A à cette réponse (l'init complète), READY sans
 *     reprise ;
 *   - le 0x10 08 du homing du pilote (le 0x0B sans réponse : l'init s'arrête, le homing suit) : un seul envoi, ni échec
 *     compté ni RECOVERING, READY.
 * Avant le 2026-10-07, l'erreur terminait la requête : l'init s'arrêtait sans 0x0A, le homing comptait un échec. */
static uint64_t until_sent10(uint8_t sub)
{
    for (int k = 0; k < 10000; k++) {
        for (long i = sent(0, 2, 0x10); i >= 0; i = sent((size_t)i + 1, 2, 0x10))
            if (g_ps_log[i].frame.msg[1] == sub) return t_of(i);
        run_for(1 * MS);
    }
    return 0;
}

static void rx_errors_after(uint64_t t)
{
    run_to(t + 100 * MS);
    ps_error(E_FRAMING);
    run_to(t + 200 * MS);
    ps_error(E_BUS);
}

static void t_home_rx_error(void)
{
    uint64_t t10;
    long i0a;
    printf("M6 : une erreur de réception pendant un 0x10 (l'init, le homing du pilote) ne le fait pas échouer\n");
    boot();
    lens_135();
    ps_presence(true);
    t10 = until_sent10(0x1F);
    rx_errors_after(t10);
    run_for(3000 * MS);
    i0a = sent(0, 2, 0x0A);
    CHECK(t10 && count_sent(0, UINT64_MAX, 0x10) == 1 && i0a >= 0 && t_of(i0a) == t10 + 700 * MS,
          "le 0x10 1F de l'init, deux erreurs reçues : un seul envoi, le 0x0A à sa réponse (700 ms)");
    CHECK(status().session_state == SESSION_READY && entered(SESSION_RECOVERING, 0) == 0, "READY, sans reprise");

    boot();
    lens_135();
    no_answer(0x0B);
    g_ps.flow = true;
    ps_presence(true);
    t10 = until_sent10(0x08);
    rx_errors_after(t10);
    run_for(5000 * MS);
    CHECK(t10 && count_sent(t10, UINT64_MAX, 0x10) == 1 && entered(SESSION_RECOVERING, 0) == 0 &&
              status().session_state == SESSION_READY,
          "le 0x10 08 du homing, deux erreurs reçues : un seul envoi, ni échec compté ni reprise, READY");
}

static void t_home_failed_std(void)
{
    printf("10 01 au homing standard : FAULT, E_HOME_FAILED (E3)\n");
    boot();
    lens_135();
    no_answer(0x0B);
    ps_answer(0x10, TAMRON10, sizeof TAMRON10, 700 * MS);
    g_ps.flow = true;
    ps_presence(true);
    run_for(5000 * MS);
    CHECK(status().session_state == SESSION_FAULT && status().last_error == E_HOME_FAILED, "FAULT, E_HOME_FAILED");
}

static void t_still_fails(void)
{
    uint64_t th;
    long ih;
    printf("homing standard qui ne s'immobilise pas en 3 s : échec compté\n");
    boot();
    lens_135();
    no_answer(0x0B);
    g_ps.flow = true;
    g_ps.m05[61] = 0x01;   /* octet de mouvement non nul : l'objectif se dit en mouvement */
    ps_presence(true);
    run_for(8000 * MS);
    ih = sent(0, 2, 0x10);
    th = t_of(ih) + 700 * MS;
    CHECK(ih >= 0 && entered(SESSION_RECOVERING, 0) == th + 3000 * MS, "RECOVERING 3 s après la réponse au 0x10");
}

static void t_still_stale(void)
{
    long ih;
    uint64_t tres, trdy;
    printf("homing standard, 0x06 interrompus puis repris : l'immobilité veut dix 0x06 frais\n");
    boot();
    lens_135();
    no_answer(0x0B);
    g_ps.flow = true;
    ps_presence(true);
    for (ih = -1; ih < 0 && ps_now() < T0 + 5000 * MS; ih = sent(0, 2, 0x10)) run_for(1 * MS);
    g_ps.flow = false;
    run_to(t_of(ih) + 700 * MS + 1000 * MS);   /* réponse au 0x10, puis 1 s sans télémétrie */
    tres = ps_now();
    g_ps.flow = true;
    run_for(2000 * MS);
    trdy = entered(SESSION_READY, 0);
    CHECK(ih >= 0 && trdy > tres + 9 * 16 * MS && trdy < tres + 200 * MS,
          "READY après dix 0x06 repris (%lld µs), pas au premier", (long long)(trdy - tres));
}

/* E7 : la perte n'est armée qu'en READY. Un homing standard dont la télémétrie se tait 5 s pendant que le 0x10 08
 * est en vol reste en HOMING, puis finit en READY. */
static void t_no_loss_in_homing(void)
{
    long ih;
    uint64_t th;
    printf("HOMING sans télémétrie 5 s : pas de perte, la perte n'est armée qu'en READY (E7)\n");
    boot();
    lens_135();
    no_answer(0x0B);
    answer_src(0x10, SRC_R10, 6000 * MS);
    g_ps.flow = true;
    ps_presence(true);
    for (ih = -1; ih < 0 && ps_now() < T0 + 5000 * MS; ih = sent(0, 2, 0x10)) run_for(1 * MS);
    th = ps_now();
    g_ps.flow = false;
    run_to(th + 5000 * MS);
    CHECK(ih >= 0 && status().session_state == SESSION_HOMING && entered(SESSION_RECOVERING, 0) == 0,
          "5 s sans 0x05 ni 0x06 en HOMING : toujours HOMING, aucune perte");
    g_ps.flow = true;
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY && entered(SESSION_RECOVERING, 0) == 0, "la réponse au 0x10, puis READY");
}

static void t_d2_homing(void)
{
    long i;
    uint64_t td;
    printf("D2 retombe dans HOMING : OFF ; q pendant ce homing : E_BUSY\n");
    boot();
    lens_135();
    no_answer(0x0B);
    g_ps.flow = true;
    ps_presence(true);
    while (status().session_state != SESSION_HOMING && ps_now() < T0 + 5000 * MS) run_for(1 * MS);
    run_for(200 * MS);
    CHECK(status().session_state == SESSION_HOMING, "HOMING, 0x10 en vol");
    CHECK(!status().position_valid, "HOMING : position invalide, le flux tourne pourtant");
    {
        size_t from = g_ps_n;
        uint32_t q = cmd(CMD_FOCUS_STOP, 0);
        CHECK(n_acks > 0 && ACKS[n_acks - 1].a.seq == q && ACKS[n_acks - 1].a.result == ACK_REJECTED &&
                  ACKS[n_acks - 1].a.reason == E_BUSY && g_ps_n == from && status().session_state == SESSION_HOMING,
              "q pendant le homing du démarrage : E_BUSY, rien d'émis, toujours HOMING");
    }
    td = ps_now();
    ps_presence(false);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_OFF, "OFF");
    i = act(0, PS_RAIL);
    while (i >= 0 && t_of(i) < td) i = act((size_t)i + 1, PS_RAIL);
    CHECK(i >= 0 && !g_ps_log[i].on && i + 1 < (long)g_ps_n && g_ps_log[i + 1].kind == PS_RAIL && !g_ps_log[i + 1].on,
          "rails coupés");
    i = act(0, PS_VD);
    while (i >= 0 && t_of(i) < td) i = act((size_t)i + 1, PS_VD);
    CHECK(i >= 0 && g_ps_log[i].hz == 0, "VD arrêtée");
    CHECK(count_sent(td, ps_now(), 0x03) == 0 && count_sent(td, ps_now(), 0x10) == 0, "plus aucune trame");
    CHECK(entered(SESSION_READY, 0) == 0, "ni la réponse du 0x10 ni rien d'autre ne ramène la session");
}

/* D2 qui retombe en READY : OFF, rails coupés, VD arrêtée, plus rien ; ni perte ni reprise ensuite. */
static void t_d2_ready(void)
{
    long i;
    uint64_t td;
    printf("D2 retombe en READY : OFF, et rien ne repart\n");
    boot();
    lens_135();
    ps_presence(true);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY, "READY");
    td = ps_now();
    ps_presence(false);
    run_for(10000 * MS);
    CHECK(entered(SESSION_OFF, chg_index_after(td)) == td, "OFF à la retombée de D2");
    CHECK(status().session_state == SESSION_OFF && entered(SESSION_RECOVERING, 0) == 0 &&
              entered(SESSION_POWERING, chg_index_after(td + 1)) == 0,
          "10 s après : toujours OFF, ni perte comptée ni nouvel essai");
    i = act(0, PS_RAIL);
    while (i >= 0 && t_of(i) < td) i = act((size_t)i + 1, PS_RAIL);
    CHECK(i >= 0 && t_of(i) == td && !g_ps_log[i].on && i + 1 < (long)g_ps_n && g_ps_log[i + 1].kind == PS_RAIL &&
              !g_ps_log[i + 1].on && act((size_t)i + 2, PS_RAIL) < 0,
          "rails coupés à la retombée, jamais rallumés");
    i = act(0, PS_VD);
    while (i >= 0 && t_of(i) < td) i = act((size_t)i + 1, PS_VD);
    CHECK(i >= 0 && t_of(i) == td && g_ps_log[i].hz == 0, "VD arrêtée");
    CHECK(count_sent(td + 1, ps_now(), 0x03) == 0 && count_sent(td + 1, ps_now(), 0x01) == 0, "plus aucune trame");
    CHECK(bsk_bench_refused() == refused0, "aucune trame refusée par bench_core");
}

/* D2 qui retombe en RECOVERING, pendant l'attente puis pendant la coupure : OFF, et les rails ne sont
 * jamais rallumés sur une monture vide (la reprise ne va pas jusqu'à POWERING). */
static void t_d2_recovering(void)
{
    static const uint32_t after_ms[] = {1000, 3200};   /* dans l'attente de 3 s ; dans la coupure de 500 ms (activée) */
    for (size_t k = 0; k < sizeof after_ms / sizeof after_ms[0]; k++) {
        uint64_t tr, td;
        long i;
        printf("D2 retombe en RECOVERING, %u ms après son entrée : OFF, rails jamais rallumés\n", after_ms[k]);
        boot_cut(k == 1);
        ps_presence(true);
        while (status().session_state != SESSION_RECOVERING && ps_now() < T0 + 5000 * MS) run_for(1 * MS);
        tr = entered(SESSION_RECOVERING, 0);
        CHECK(tr != 0, "un objectif muet : RECOVERING");
        run_to(tr + after_ms[k] * MS);
        td = ps_now();
        ps_presence(false);
        run_for(10000 * MS);
        CHECK(entered(SESSION_OFF, chg_index_after(td)) == td, "OFF à la retombée de D2");
        CHECK(status().session_state == SESSION_OFF && entered(SESSION_POWERING, chg_index_after(td + 1)) == 0,
              "10 s après : toujours OFF, pas de POWERING");
        i = act(0, PS_RAIL);
        while (i >= 0 && !(t_of(i) >= td && g_ps_log[i].on)) i = act((size_t)i + 1, PS_RAIL);
        CHECK(i < 0, "aucun rail rallumé après la retombée");
        i = act(0, PS_VD);
        while (i >= 0 && t_of(i) < td) i = act((size_t)i + 1, PS_VD);
        CHECK(i >= 0 && t_of(i) == td && g_ps_log[i].hz == 0, "VD arrêtée");
        CHECK(count_sent(td + 1, ps_now(), 0x01) == 0 && count_sent(td + 1, ps_now(), 0x03) == 0, "plus aucune trame");
    }
}

static void t_d2_handshake(void)
{
    long i, il;
    printf("D2 retombe pendant la poignée de main : BODY_CS rabaissée (lignes relâchées)\n");
    boot();
    ps_presence(true);
    run_for(116 * MS);
    ps_presence(false);
    run_for(10 * MS);
    i = act(0, PS_BODY_CS);
    for (long j = i; j >= 0; j = act((size_t)j + 1, PS_BODY_CS)) i = j;
    il = act(0, PS_LINES);
    for (long j = il; j >= 0; j = act((size_t)j + 1, PS_LINES)) il = j;
    CHECK(i >= 0 && g_ps_log[i].on && t_of(i) == T0 + 115 * MS && il > i && !g_ps_log[il].on && t_of(il) == T0 + 116 * MS,
          "BODY_CS levée par la poignée de main, puis les lignes relâchées : BODY_CS retombe par le tirage bas");
    CHECK(status().session_state == SESSION_OFF, "OFF");
}

static void t_hs_stuck(void)
{
    long i01;
    printf("poignée de main : LENS_CS déjà haute puis restée haute : 0x01 à l'échéance (E9)\n");
    boot();
    lens_135();
    g_ps.handshake = false;
    ps_lens_cs(true, T0 + 10 * MS);
    ps_presence(true);
    run_for(4000 * MS);
    i01 = sent(0, 2, 0x01);
    CHECK(i01 >= 0 && t_of(i01) == T0 + 115 * MS + 3 * MS + 500 * MS, "0x01 500 ms après BODY_CS rabaissée");
    CHECK(status().session_state == SESSION_READY, "READY");
}

/* ─────────────────────────── le Sony resté alimenté ───────────────────────────
 * Le Sony FE 24-105 G de l'humain, tel que ses captures le montrent :
 *   - 7_Docs/E-Mount/traces/emount-bench-2026-09-28T17-45-01-034Z.txt:15 : `lens_cs_up_us=14`, LENS_CS déjà haute quand BODY_CS monte ;
 *     la carte attend 3 ms, rabaisse BODY_CS, attend que LENS_CS retombe, et son 0x01 est servi (:16-17). Les réponses
 *     d'init sont celles de :17-47, octets copiés, fragments concaténés, chacune après le délai que la capture
 *     montre de l'émission à la réception (en ms) ;
 *   - une capture de la même séance, non publiée (7_Docs/E-Mount/provenance.md § 3) : une carte qui ignore LENS_CS déjà
 *     haute va au bout des 500 ms de la poignée de main, et son 0x01, émis LENS_CS encore haute, reste sans réponse ;
 *   - le flux, 0x05 et 0x06 : emount-bench-2026-09-28T17-45-01-034Z.txt:65-66 et :68.
 * Le 0x02 que le Sony émet en continu n'est pas joué : rien ici n'en dépend, TRANSACTION reconnaît une réponse à
 * son type. Aucun refus n'est inventé : un message reçu LENS_CS haute est sans réponse (mute_cs_high).
 * Le délai de la retombée de LENS_CS après BODY_CS basse n'est dans aucune capture : 1,1 ms, celui du répondeur
 * (HS_US). */
typedef struct {
    uint8_t     type;
    uint32_t    delay_ms;
    const char *hex[4];
} capture_t;

static const capture_t SONY[] = {
    {0x01, 6, {"F0 29 00 02 00 01 FF 9F FF 5D EE 60 18 DE FF 0F F8 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
               "00 00 00 00 71 07 55"}},                                                       /* :16-17, 6955 -> 6961 */
    {0x07, 13, {"F0 2B 00 02 00 07 01 07 60 01 00 03 01 17 A0 25 80 00 00 00 00 60 92 86 5E 00 00 00 00 00 00 00 42 "
                "0D 03 00 00 00 00 00 25 04 55"}},                                             /* :19-20, 6962 -> 6975 */
    {0x3F, 6, {"F0 4A 00 02 00 3F 00 46 45 20 32 34 2D 31 30 35 6D 6D 20 46 34 20 47 20 4F 53 53 00 00 00 00 00 00 00 "
               "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00",
               "00 00 00 00 00 00 00 4F 05 55"}},                                              /* :22-24, 6976 -> 6982 */
    {0x08, 10, {"F0 D2 00 02 00 08 05 14 00 19 04 00 00 80 00 14 01 01 12 E1 01 82 00 24 01 05 40 00 08 00 19 00 10 "
                "31 A8 1D 00 16 00 00 00 00 00 00 00 00 E0 74 00 F0 FF 84 38 00 00 08 32 35 40 2A 00 00 00 00",
                "00 00 00 74 35 04 32 00 00 00 00 00 00 00 F3 F3 07 FB FB 00 08 01 04 04 24 BD 00 80 01 00 00 00 00 "
                "00 00 00 00 01 01 01 09 09 00 00 00 00 00 E6 80 DD 52 00 D0 DD DF F0 00 08 A0 DD E0 00 52 00",
                "10 10 00 03 00 00 00 00 00 00 00 84 64 27 00 06 10 FF 33 00 00 00 00 00 03 00 00 00 00 00 00 00 00 "
                "00 00 00 00 0C 0C 00 00 00 00 00 00 00 05 00 01 00 40 42 00 00 07 07 00 00 07 00 00 00 00 00",
                "00 00 00 00 00 00 00 00 00 13 00 F6 00 0A 01 F5 1C 55"}},                     /* :26-30, 6984 -> 6994 */
    {0x0B, 13, {"F0 0B 00 02 00 0B 60 00 78 00 55"}},                                          /* :32-33, 6996 -> 7009 */
    {0x09, 5, {"F0 14 00 02 00 09 10 00 00 00 00 00 00 00 00 00 00 2F 00 55"}},               /* :34-35, 7010 -> 7015 */
    {0x0D, 33, {"F0 0A 00 02 00 0D 01 1A 00 55"}},                                             /* :36-37, 7016 -> 7049 */
    {0x10, 414, {"F0 0A 00 02 00 10 00 1C 00 55"}},                                            /* :39-43, 7050 -> 7464 */
    {0x0A, 10, {"F0 19 00 02 00 0A FF 7F 00 00 00 00 00 00 3F 00 00 00 00 00 00 00 E2 01 55"}}, /* :45-47, 7465 -> 7475 */
};

static const char *const SONY05[] = {   /* 17-45-01-034Z.txt:65-66 */
    "F0 69 00 01 53 05 05 14 05 14 01 00 00 00 02 AA 00 AA 00 54 01 54 01 00 00 00 00 07 80 FF F2 00 F0 00 2E 01 00 00 "
    "A0 36 1A 0C 09 05 25 D4 30 08 04 04 00 00 00 00 00 00 00 00 00 00 00 00 00 00",
    "00 00 00 00 01 00 10 00 10 07 00 00 00 00 00 00 00 00 00 DD DD 00 00 F3 00 70 00 00 00 00 00 70 00 00 00 00 00 11 "
    "95 0C 55",
    NULL};
static const char *const SONY06[] = {   /* 17-45-01-034Z.txt:68 */
    "F0 30 00 01 53 06 82 00 CE 3F D8 0F 10 EE 3E 51 44 5E 00 01 00 1C 28 28 1B 00 CD 3F 00 00 87 00 45 02 00 00 00 00 "
    "00 00 00 00 00 00 00 91 07 55",
    NULL};

/* Une trame de capture, fragments concaténés, décodée par fr_decode : une copie fautive ne passe pas la somme.
 * Rend la longueur du message, type compris ; 0 si la trame ne se décode pas. */
static uint16_t capture_msg(const char *const *hex, size_t n_hex, uint8_t *msg, size_t cap)
{
    uint8_t b[BSK_FRAME_MAX];
    size_t n = 0;
    bsk_frame_t f;
    for (size_t i = 0; i < n_hex && hex[i]; i++) n += fr_hex(hex[i], b + n, sizeof b - n);
    if (fr_decode(b, n, &f) != E_OK || f.len > cap) return 0;
    memcpy(msg, f.msg, f.len);
    return f.len;
}

/* Le Sony, sa LENS_CS de poignée de main haute avant que la carte ne démarre ; `seen` : la PHY la lit
 * (ps_lens_cs_start). */
static void sony(bool seen)
{
    bool ok = true;
    for (size_t i = 0; i < sizeof SONY / sizeof SONY[0]; i++) {
        uint8_t m[BSK_MSG_MAX];
        uint16_t n = capture_msg(SONY[i].hex, 4, m, sizeof m);
        ok = ok && n && m[0] == SONY[i].type;
        ps_answer(SONY[i].type, m, n, (uint64_t)SONY[i].delay_ms * MS);
    }
    g_ps.n05 = capture_msg(SONY05, 4, g_ps.m05, sizeof g_ps.m05);
    g_ps.n06 = capture_msg(SONY06, 4, g_ps.m06, sizeof g_ps.m06);
    CHECK(ok && g_ps.n05 && g_ps.m05[0] == 0x05 && g_ps.n06 && g_ps.m06[0] == 0x06,
          "les trames du Sony se décodent (somme, longueur), chacune du type attendu");
    g_ps.flow_on_0a = true;
    g_ps.handshake = true;
    g_ps.mute_cs_high = true;
    ps_lens_cs_start(true, seen);
}

static void t_sony_cs_high(void)
{
    long iu, id, i01, i07;
    bsk_status_t s;
    printf("Sony resté alimenté, LENS_CS déjà haute : vue, 3 ms, BODY_CS basse, 0x01 à la retombée, READY\n");
    boot();
    sony(true);
    ps_presence(true);
    run_for(3000 * MS);
    iu = act(0, PS_BODY_CS);
    while (iu >= 0 && !g_ps_log[iu].on) iu = act((size_t)iu + 1, PS_BODY_CS);
    id = iu >= 0 ? act((size_t)iu + 1, PS_BODY_CS) : -1;
    CHECK(iu >= 0 && t_of(iu) == T0 + 115 * MS, "BODY_CS haute 15 ms après la basse");
    CHECK(id >= 0 && !g_ps_log[id].on && t_of(id) == T0 + 118 * MS,
          "LENS_CS déjà haute vue au niveau : BODY_CS basse 3 ms après la haute, pas à l'échéance de 500 ms");
    i01 = sent(0, 2, 0x01);
    CHECK(i01 >= 0 && t_of(i01) == T0 + 118 * MS + 1100, "0x01 à la retombée de LENS_CS, 1,1 ms après BODY_CS basse (%llu)",
          (unsigned long long)(t_of(i01) - T0));
    CHECK(count_sent(0, ps_now(), 0x01) == 1, "un seul 0x01");
    i07 = sent(0, 2, 0x07);
    CHECK(i07 >= 0 && t_of(i07) == t_of(i01) + 6 * MS, "0x01 servi : le 0x07 à sa réponse, 6 ms après");
    s = status();
    CHECK(s.session_state == SESSION_READY && entered(SESSION_RECOVERING, 0) == 0, "READY, sans reprise");
    CHECK(!strcmp(s.lens_name, "FE 24-105mm F4 G OSS") && s.lens_id_product == 0x8025,
          "identité : le nom 0x3F et le LensType2 0x8025 de la capture (« %s », 0x%04X)", s.lens_name,
          (unsigned)s.lens_id_product);
    CHECK(s.position_valid && s.focus_position == 16334, "position : celle du 0x06 de la capture, 16334");
    CHECK(bsk_bench_refused() == refused0, "aucune trame refusée par bench_core");
}

/* Le même Sony, LENS_CS connue par ses seuls fronts (`seen` faux) : la SESSION la croit basse. Rejoue la capture
 * 17-37-41-369Z.txt:9-13 : la poignée de main va au bout de ses 500 ms, BODY_CS retombe, et le 0x01 part aussitôt,
 * LENS_CS encore haute : sans réponse. Le deuxième essai, 300 ms plus tard, LENS_CS retombée, est servi. */
static void t_sony_edges_only(void)
{
    long i01, i01b;
    printf("le même Sony, LENS_CS connue par ses seuls fronts (pas relevée au POWERING) : 0x01 à 500 ms, sans réponse ; le 2e servi\n");
    boot();
    sony(false);
    ps_presence(true);
    run_for(3000 * MS);
    i01 = sent(0, 2, 0x01);
    CHECK(i01 >= 1 && t_of(i01) == T0 + 615 * MS && g_ps_log[i01 - 1].kind == PS_BODY_CS && !g_ps_log[i01 - 1].on,
          "0x01 à l'échéance de la poignée de main, dans l'instant où BODY_CS retombe (capture : powering 4990, "
          "identifying 5605)");
    i01b = i01 >= 0 ? sent((size_t)i01 + 1, 2, 0x01) : -1;
    CHECK(i01b >= 0 && t_of(i01b) == t_of(i01) + 300 * MS && sent((size_t)i01 + 1, 2, 0x07) > i01b,
          "0x01 émis LENS_CS haute : sans réponse ; le deuxième, 300 ms après, servi");
    CHECK(status().session_state == SESSION_READY, "READY");
}

/* ─────────────────────────── la ligne de la poignée de main ───────────────────────────
 * Chaque issue de la poignée de main laisse une ligne `* std <t> step=handshake …` sous LOG ON. Les instants et les
 * durées sont écrits à la main : BODY_CS haute à T0 + 115 ms (rails 100 ms, BODY_CS basse 15 ms), LENS_CS levée et
 * rabaissée 1,1 ms après BODY_CS par le répondeur (HS_US), 3 ms de maintien, 500 ms par échéance. */

/* La ligne `* std … step=handshake` du journal depuis le dernier appel (la première), "" s'il n'y en a pas ; `n_std` :
 * combien (`* std … step=08` en est une autre, t_body08_journal). */
static const char *std_line(size_t *n_std)
{
    static char l[400], b[400];
    uint8_t g;
    size_t n;
    l[0] = 0;
    *n_std = 0;
    while ((n = bsk_journal_take(b, sizeof b - 1, &g)) > 0) {
        b[n] = 0;
        if (strncmp(b, "* std ", 6) || !strstr(b, " step=handshake ")) continue;
        if ((*n_std)++ == 0) memcpy(l, b, n + 1);
    }
    return l;
}

static void hs_boot(void)
{
    size_t n;
    boot();
    bsk_journal_init(ps_now);
    bsk_journal_set(true, false);
    (void)std_line(&n);
}

static void t_hs_journal(void)
{
    const char *l;
    size_t n;
    printf("poignée de main : son résultat au journal, dans chacune de ses issues\n");

    hs_boot();
    lens_135();
    ps_presence(true);
    run_for(1000 * MS);
    l = std_line(&n);
    CHECK(n == 1 && !strcmp(l, "* std 1120 step=handshake result=ok lens_cs_up_us=1100"),
          "LENS_CS levée 1,1 ms après BODY_CS, retombée : ok, lens_cs_up_us=1100, à la retombée (« %s », %zu)", l, n);

    hs_boot();
    sony(true);
    ps_presence(true);
    run_for(1000 * MS);
    l = std_line(&n);
    CHECK(n == 1 && !strcmp(l, "* std 1119 step=handshake result=ok lens_cs_up_us=0"),
          "le Sony, LENS_CS déjà haute : ok, lens_cs_up_us=0 (« %s », %zu)", l, n);

    hs_boot();
    sony(false);
    ps_presence(true);
    run_for(1000 * MS);
    l = std_line(&n);
    CHECK(n == 1 && !strcmp(l, "* std 1615 step=handshake result=LENS_CS_never_high"),
          "LENS_CS jamais vue haute : l'échéance de sa levée, 500 ms après BODY_CS haute (« %s », %zu)", l, n);

    hs_boot();
    lens_135();
    g_ps.handshake = false;
    ps_lens_cs(true, T0 + 117 * MS);
    ps_presence(true);
    run_for(1000 * MS);
    l = std_line(&n);
    CHECK(n == 1 && !strcmp(l, "* std 1620 step=handshake result=LENS_CS_stuck_high lens_cs_up_us=2000"),
          "LENS_CS levée 2 ms après BODY_CS, jamais retombée : l'échéance de sa retombée, 500 ms après BODY_CS basse "
          "(« %s », %zu)", l, n);
    bsk_journal_set(false, false);
}

static void t_loss(void)
{
    uint64_t tl, tr;
    printf("READY, plus de télémétrie : perte à 2 s (E6), RECOVERING, nouvel essai\n");
    boot();
    lens_135();
    ps_presence(true);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY, "READY");
    g_ps.flow = false;
    g_ps.handshake = false;
    tl = 0;
    for (size_t j = 0; j < g_ps_n; j++)
        if (g_ps_log[j].kind == PS_SEND && g_ps_log[j].frame.msg[0] == 0x04) tl = g_ps_log[j].t + 3 * MS;   /* le 0x06 */
    run_for(10000 * MS);
    tr = entered(SESSION_RECOVERING, 0);
    CHECK(tr == tl + 2000 * MS + 1, "RECOVERING 2 s après le dernier 0x06 (écart %lld µs)", (long long)(tr - tl));
    CHECK(count_sent(tr, tr + 3000 * MS, 0x03) == 0, "la session oubliée : boucle arrêtée");
    CHECK(entered(SESSION_POWERING, chg_index_after(tr)) == tr + 3000 * MS, "POWERING 3 s après (sans coupure)");
    CHECK(sent(0, 2, 0x01) >= 0 && count_sent(tr + 3000 * MS, ps_now(), 0x01) >= 1, "nouvel essai : 0x01");
}

static bsk_mark_t mark_now(bool *ok);

/* La télémétrie d'une session précédente, reçue après l'oubli, ne compte pas dans la suivante. Le 135 en READY, puis
 * CMD_ATTACH (`b`) : forget(), la boucle arrêtée ; le 0x05 puis le 0x06 de la dernière paire, encore en vol, arrivent 2 et
 * 3 ms après (les délais du flux, FLOW_05_US et FLOW_06_US du répondeur ; les trames du flux telles quelles). L'objectif
 * suivant rend le 0x07 du F051 : pas un Samyang, sa boucle n'est lancée qu'à la fin de l'init (le 0x0A répondu) ; il ne
 * répond ni au 0x3F ni au 0x08 (tolérés, trois envois chacun, 900 et 1200 ms) : de ces trames à la fin de l'init, plus de
 * 2 s. Son flux reprend à son 0x0A (flow_on_0a). Le 0x06 d'avant porte une autre position que ceux du flux suivant (1000
 * pas de plus). Attendus, calculés à la main : le premier 0x05 de la boucle arrive 2 ms après son premier 0x04 ; il sert
 * S_FIRST05 et S_PROBE05 ; la fin du démarrage attend le premier 0x06 de la boucle, 3 ms après ce 0x04 (S_MARK : la
 * marque, aucune, le magasin est vide, et un 0x06 de cette boucle-ci) : READY à cet instant, pas à la fin de l'init ni au
 * 0x05 ; READY tient tant que le flux court ; la marque posée ici est à la position du flux, sens inconnu (aucun
 * changement vu dans la session : le 0x06 d'avant ne compte pas) ; le flux arrêté, RECOVERING 2 s après son dernier 0x06
 * (plus 1 µs, on_frame), pas avant. Si les trames d'avant comptaient : READY à la réponse au 0x0A (S_FIRST05 et S_PROBE05
 * servis par le 0x05 d'avant, S_MARK par le 0x06 d'avant), et la perte au même instant (échéance armée sur le 0x06
 * d'avant, plus de 2 s plus tôt) ; le 0x06 d'avant lu, READY au premier 0x05 et la marque en décroissant. */
static void t_stale_telemetry(void)
{
    size_t from, ci;
    long i0a, i04;
    uint64_t tb, ta, t06, tl, tr;
    uint8_t old06[BSK_MSG_MAX];
    uint16_t pos;
    bsk_mark_t m;
    bool ok;
    printf("la télémétrie d'une session précédente, reçue après l'oubli, ne sert pas la suivante\n");
    boot();
    lens_135();
    ps_presence(true);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY, "le 135 : READY");
    ps_answer(0x07, TAMRON07, sizeof TAMRON07, 5 * MS);
    no_answer(0x08);
    g_ps.flow = false;                              /* rallumé par son 0x0A (flow_on_0a) */
    g_ps.handshake = true;
    tb = ps_now();
    from = g_ps_n;
    ci = n_chg;
    (void)cmd(CMD_ATTACH, 0);
    pos = (uint16_t)(g_ps.m06[3] | g_ps.m06[4] << 8);
    memcpy(old06, g_ps.m06, g_ps.n06);
    old06[3] = (uint8_t)(pos + 1000);               /* la position d'avant : 1000 pas de plus que celle du flux suivant */
    old06[4] = (uint8_t)((pos + 1000) >> 8);
    ps_frame(1, g_ps.m05, g_ps.n05, tb + 2 * MS);
    ps_frame(1, old06, g_ps.n06, tb + 3 * MS);
    run_for(6000 * MS);
    i0a = sent(from, 2, 0x0A);
    ta = t_of(i0a) + 5 * MS;                        /* la réponse au 0x0A : la fin de l'init */
    i04 = i0a >= 0 ? sent((size_t)i0a + 1, 1, 0x04) : -1;   /* le premier 0x04 de la boucle */
    t06 = t_of(i04) + 3 * MS;                       /* FLOW_06_US */
    CHECK(i0a >= 0 && ta > tb + 3 * MS + 2000 * MS, "l'init finit plus de 2 s après le 0x06 d'avant (%lld µs)",
          (long long)(ta - tb));
    CHECK(i04 > i0a && t_of(i04) >= ta && sent(from, 1, 0x04) == i04, "la boucle lancée à la fin de l'init, pas avant");
    CHECK(entered(SESSION_READY, ci) == t06,
          "READY au premier 0x06 de la boucle de la session (%lld µs après la fin de l'init), pas avant",
          (long long)(entered(SESSION_READY, ci) - ta));
    CHECK(status().session_state == SESSION_READY && entered(SESSION_RECOVERING, ci) == 0,
          "READY tient tant que le flux court : aucune perte");
    (void)cmd(CMD_SET_MARK, BSK_MARK_HERE);
    m = mark_now(&ok);
    CHECK(ok && m.position == pos && m.approach_dir == BSK_APPROACH_UNKNOWN,
          "la marque posée ici : %u, sens inconnu, le 0x06 d'avant ne compte pas (%u, sens %u)", (unsigned)pos,
          (unsigned)m.position, (unsigned)m.approach_dir);
    g_ps.flow = false;
    tl = 0;
    for (size_t j = 0; j < g_ps_n; j++)
        if (g_ps_log[j].kind == PS_SEND && g_ps_log[j].frame.msg[0] == 0x04) tl = g_ps_log[j].t + 3 * MS;   /* le 0x06 */
    run_for(3000 * MS);
    tr = entered(SESSION_RECOVERING, ci);
    CHECK(tr == tl + 2000 * MS + 1, "le flux arrêté : RECOVERING 2 s après son dernier 0x06 (écart %lld µs)",
          (long long)(tr - tl));
}

/* Le détecteur de perte, à son armement, n'échoit pas dans le passé. Le homing du pilote (l'init arrêtée au 0x0B), la
 * réponse au 0x10 08 à tr (700 ms, lens_135), le flux coupé 250 ms plus tard : l'immobilité est constatée à l'échéance du
 * homing, tr + 3 s (still_ms), et le démarrage finit là, sans marque (magasin vide) : READY, sa dernière télémétrie vieille
 * de 2,75 s. Attendus, à la main : READY à tr + 3 s ; RECOVERING 2 s après l'entrée, tr + 5 s plus 1 µs (watch), pas au
 * même instant. La même chose en RESTORING : t_restore_homing. */
static void t_loss_from_entry(void)
{
    long i10;
    uint64_t tr;
    printf("READY entrée avec une télémétrie de plus de 2 s : la perte 2 s après l'entrée, pas au même instant\n");
    boot();
    lens_135();
    g_ps.flow = true;
    no_answer(0x0B);
    ps_presence(true);
    for (i10 = -1; i10 < 0 && ps_now() < T0 + 5000 * MS; i10 = sent(0, 2, 0x10)) run_for(1 * MS);
    tr = t_of(i10) + 700 * MS;                      /* la réponse au 0x10 08 */
    CHECK(i10 >= 0 && g_ps_log[i10].frame.msg[1] == 0x08, "le homing du pilote : 0x10 08");
    run_to(tr + 250 * MS);
    g_ps.flow = false;
    run_to(tr + 5100 * MS);
    CHECK(entered(SESSION_READY, 0) == tr + 3000 * MS, "READY à l'échéance du homing (%lld µs après la réponse)",
          (long long)(entered(SESSION_READY, 0) - tr));
    CHECK(entered(SESSION_RECOVERING, 0) == tr + 5000 * MS + 1, "RECOVERING 2 s après l'entrée en READY (%lld µs après elle)",
          (long long)(entered(SESSION_RECOVERING, 0) - tr - 3000 * MS));
}

/* La garde de lens_rx couvre le 0x05 : aucune trame cyclique reçue avant le lancement de la boucle de la session n'est
 * lue. Le 135 en READY (bague en ouverture), puis CMD_ATTACH (`b`) : forget(), la boucle arrêtée ; le 0x05 de la dernière
 * paire arrive 2 ms après (FLOW_05_US), mais porte 01 à l'offset 62 (ouverture) et une focale nominale de 500 (50 mm) ; le
 * flux suivant porte 03 (focus) et la focale du 135 des traces (1350). L'objectif suivant est celui de t_stale_telemetry
 * (le 0x07 du F051, ni 0x3F ni 0x08) : sa boucle n'est lancée qu'à la fin de l'init, plus de 2 s après. Attendus, à la
 * main : jusqu'au premier 0x05 de la boucle (2 ms après son premier 0x04), le rôle de la bague reste inconnu
 * (ring_forget), le premier 0x04 de la boucle part sans le bit (04 00 00 19 81 : le 0x05 d'avant l'aurait posé, 83) et
 * aucune lecture n'est demandée au magasin (pas de clé : ni la focale ni le 0x05 d'avant ne comptent ; la clé d'avant,
 * l'identité du F051 et 50 mm, serait 010334c100032) ; au premier 0x05 de la boucle, le rôle focus, la clé demandée une
 * fois ; la marque posée en READY rangée sous la clé du flux, 01 03 34 C1, 00 (pas de 0x08), 135 mm : 010334c100087. Si
 * le 0x05 d'avant comptait : ouverture et bit posés dès lui, une lecture demandée sous la clé de 50 mm à la réponse au
 * 0x07. */
static bool body_all(size_t from, bool on);

static void t_stale_05(void)
{
    size_t from;
    long i04;
    uint32_t r0, v = 0;
    uint64_t tb, t05;
    uint8_t old05[BSK_MSG_MAX];
    printf("le 0x05 d'une session précédente, reçu après l'oubli : ni la bague, ni le bit du 0x04, ni la clé de la marque\n");
    boot();
    lens_135();
    ps_presence(true);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY && status().ring == RING_APERTURE, "le 135 : READY, bague en ouverture");
    ps_answer(0x07, TAMRON07, sizeof TAMRON07, 5 * MS);
    no_answer(0x08);
    g_ps.flow = false;                              /* rallumé par son 0x0A (flow_on_0a) */
    g_ps.handshake = true;
    memcpy(old05, g_ps.m05, g_ps.n05);
    old05[63] = 0x01;                               /* l'offset 62 d'avant : ouverture */
    old05[27] = (uint8_t)500;                       /* la focale nominale d'avant, offsets 26-27 : 50 mm */
    old05[28] = (uint8_t)(500 >> 8);
    g_ps.m05[63] = 0x03;                            /* le flux suivant : focus, 135 mm (m05[27-28] inchangés) */
    tb = ps_now();
    from = g_ps_n;
    (void)cmd(CMD_ATTACH, 0);
    r0 = g_store_sim.requests;
    ps_frame(1, old05, g_ps.n05, tb + 2 * MS);
    for (i04 = -1; i04 < 0 && ps_now() < tb + 6000 * MS; i04 = sent(from, 1, 0x04)) run_for(1 * MS);
    t05 = t_of(i04) + 2 * MS;                       /* FLOW_05_US : le premier 0x05 de la boucle */
    CHECK(i04 >= 0 && t_of(i04) > tb + 2000 * MS && ps_now() < t05, "la boucle lancée plus de 2 s après le 0x05 d'avant");
    CHECK(i04 >= 0 && g_ps_log[i04].frame.msg[4] == 0x81, "le premier 0x04 de la boucle sans le bit (0x%02X)",
          i04 >= 0 ? g_ps_log[i04].frame.msg[4] : 0);
    CHECK(status().ring == RING_UNKNOWN, "avant le premier 0x05 de la boucle : la bague inconnue (%u)", status().ring);
    CHECK(g_store_sim.requests == r0, "avant le premier 0x05 de la boucle : aucune clé, rien demandé au magasin (%u)",
          (unsigned)(g_store_sim.requests - r0));
    run_to(t05);
    CHECK(status().ring == RING_FOCUS && g_store_sim.requests == r0 + 1,
          "au premier 0x05 de la boucle : focus, la clé demandée une fois (%u, %u)", status().ring,
          (unsigned)(g_store_sim.requests - r0));
    from = g_ps_n;
    run_for(1000 * MS);
    CHECK(status().session_state == SESSION_READY && body_all(from, false), "READY, chaque 0x04 sans le bit");
    (void)cmd(CMD_SET_MARK, BSK_MARK_HERE);
    run_for(10 * MS);
    CHECK(store_sim_peek("010334c100087", &v) && !store_sim_peek("010334c100032", &v),
          "la marque rangée sous la clé du flux, 010334c100087, pas sous celle du 0x05 d'avant");
}

/* Un 0x05 ou un 0x06 trop court pour être lu n'est pas reçu (L03-01). lens_rx ne lit un 0x05 qu'à partir de 29 octets
 * (la focale, offsets 26-27, msg[27-28]) et un 0x06 qu'à partir de 12 (les bornes, offsets 7-10, msg[8-11]) : le flux en
 * rend de 28 et de 11 octets, les mêmes octets tronqués. Le 135 en READY, bague en ouverture (offset 62 à 01) ; la
 * dernière paire entière porte une impulsion vers le fermé (offset 60 à 01, msg[61]) : une seule, pas un tiers (deux
 * impulsions par tiers). Attendus, à la main : la consigne des 0x03 ne change pas (rejouer l'impulsion à chaque 0x05
 * tronqué ferait un tiers au suivant) ; RECOVERING 2 s après le dernier 0x06 entier, plus 1 µs (watch), 3 ms après le
 * 0x04 de sa paire (FLOW_06_US) ; s'ils comptaient, aucune perte tant que le flux tronqué court. */
static struct { unsigned k; uint64_t t04; } SH;
static bool consigne(uint16_t code);

static void short_hook(const bsk_frame_t *f, uint64_t t)
{
    if (f->cls != 1 || f->msg[0] != 0x04) return;
    if (SH.k++ == 0) {                              /* la dernière paire entière : une impulsion */
        g_ps.m05[61] = 0x01;
        SH.t04 = t;
        return;
    }
    g_ps.m05[61] = 0x00;
    g_ps.n05 = 28;                                  /* un octet de moins que la focale */
    g_ps.n06 = 11;                                  /* un octet de moins que les bornes */
}

static void t_short_05_06(void)
{
    long i03;
    uint16_t code;
    size_t ci;
    uint64_t tl;
    printf("un 0x05 ou un 0x06 trop court pour être lu : ni la perte réarmée, ni l'impulsion de la bague rejouée\n");
    boot();
    lens_135();
    ps_presence(true);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY && status().ring == RING_APERTURE, "READY, bague en ouverture");
    i03 = -1;
    for (size_t j = 0; j < g_ps_n; j++)
        if (g_ps_log[j].kind == PS_SEND && g_ps_log[j].frame.msg[0] == 0x03) i03 = (long)j;
    code = i03 >= 0 ? (uint16_t)(g_ps_log[i03].frame.msg[4] | g_ps_log[i03].frame.msg[5] << 8) : 0;
    ci = n_chg;
    SH.k = 0;
    g_ps.on_send = short_hook;
    run_for(1500 * MS);
    tl = SH.t04 + 3 * MS;                           /* le dernier 0x06 entier */
    CHECK(SH.k > 50 && consigne(code), "le flux tronqué : la consigne reste 0x%04X, l'impulsion n'est pas rejouée", code);
    run_for(1500 * MS);
    CHECK(entered(SESSION_RECOVERING, ci) == tl + 2000 * MS + 1,
          "RECOVERING 2 s après le dernier 0x06 entier, le flux tronqué ne réarme rien (écart %lld µs)",
          (long long)(entered(SESSION_RECOVERING, ci) - tl));
    g_ps.on_send = NULL;
}

/* L'instant du dernier 0x04 émis au plus tard à `t` : sa paire est servie par le flux tel qu'il était alors. */
static uint64_t last_04_at(uint64_t t)
{
    uint64_t r = 0;
    for (size_t j = 0; j < g_ps_n; j++)
        if (g_ps_log[j].kind == PS_SEND && g_ps_log[j].frame.cls == 1 && g_ps_log[j].frame.msg[0] == 0x04 && g_ps_log[j].t <= t)
            r = g_ps_log[j].t;
    return r;
}

/* Un flux qui s'arrête, l'autre courant (L03-02). Le 135 des traces en READY : 0x05 et 0x06 du flux 2 et 3 ms après chaque
 * 0x04 (FLOW_05_US, FLOW_06_US). Son 0x05 (SRC_T05) porte l'ouverture B2 11, l'offset 62 à 01 (AF : mf=0), l'offset 64
 * (oss connu), la focale nominale 1350 (la clé de la marque) ; son 0x06 la position et les bornes. Attendus, à la main :
 *   - le 0x06 seul arrêté : tout ce qu'il porte reste valide 2 s après son dernier, à la µs près, et ne l'est plus 1 µs
 *     plus tard (la durée de la perte, watch) : position non valide, position et bornes à 0, CAP_LIMITS_REPORTED retiré ;
 *     un goto et une pose de marque refusés (E_LIMIT) comme sans aucun 0x06 ; la session reste READY (le 0x05 court) ;
 *     le premier 0x06 qui revient rend tout valide ;
 *   - le 0x05 seul arrêté, une marque posée : 2 s plus 1 µs après son dernier, l'ouverture relue à 0 (aucune), mf et oss
 *     inconnus, la marque perdue (la focale, dans sa clé, inconnue), le rôle de la bague gardé (ring.c ne change que sur
 *     un offset 62 lu) ; READY ; le premier 0x05 qui revient rend la marque (relue au magasin) et mf. */
static void t_one_stream(void)
{
    uint64_t tl;
    bsk_status_t st;
    uint32_t q;
    printf("un flux arrêté, l'autre courant : ses données invalides au bout de la durée de la perte, sans perte\n");
    boot();
    lens_135();
    ps_presence(true);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY && status().position_valid, "READY, la position valide");
    tl = last_04_at(ps_now()) + 3 * MS;             /* le dernier 0x06 */
    g_ps.n06 = 0;
    run_to(tl + 2000 * MS);
    CHECK(status().position_valid, "le 0x06 arrêté : la position encore valide 2 s après son dernier");
    run_to(tl + 2000 * MS + 1);
    st = status();
    CHECK(st.session_state == SESSION_READY && !st.position_valid && st.focus_position == 0 && st.focus_min == 0 &&
              st.focus_max == 0 && !(st.capabilities & CAP_LIMITS_REPORTED),
          "1 µs plus tard : READY, ni position ni bornes (%u, %d, %ld, %ld, 0x%lx)", st.session_state, st.position_valid,
          (long)st.focus_position, (long)st.focus_max, (unsigned long)st.capabilities);
    q = cmd(CMD_FOCUS_GOTO, 20000);
    CHECK(n_acks > 0 && ACKS[n_acks - 1].a.seq == q && ACKS[n_acks - 1].a.result == ACK_REJECTED &&
              ACKS[n_acks - 1].a.reason == E_LIMIT,
          "un goto refusé comme sans aucun 0x06 : E_LIMIT");
    q = cmd(CMD_SET_MARK, BSK_MARK_HERE);
    CHECK(n_acks > 0 && ACKS[n_acks - 1].a.seq == q && ACKS[n_acks - 1].a.reason == E_LIMIT, "js refusé : E_LIMIT");
    run_for(1000 * MS);
    CHECK(status().session_state == SESSION_READY, "le 0x05 court : aucune perte");
    lens_135();                                     /* les deux flux */
    run_for(20 * MS);
    CHECK(status().position_valid && status().focus_max != 0, "le 0x06 revenu : la position et les bornes valides");

    boot();
    lens_135();
    ps_presence(true);
    run_for(3000 * MS);
    (void)cmd(CMD_SET_MARK, BSK_MARK_HERE);
    run_for(10 * MS);
    st = status();
    CHECK(st.mark_valid && st.aperture_current == 0x11B2 && st.mf == BSK_NO && st.oss != BSK_UNKNOWN,
          "une marque posée ; l'ouverture B2 11, mf=0, oss connu");
    tl = last_04_at(ps_now()) + 2 * MS;             /* le dernier 0x05 */
    g_ps.n05 = 0;
    run_to(tl + 2000 * MS);
    st = status();
    CHECK(st.aperture_current == 0x11B2 && st.mf == BSK_NO && st.mark_valid, "le 0x05 arrêté : valide 2 s après son dernier");
    run_to(tl + 2000 * MS + 1);
    st = status();
    CHECK(st.session_state == SESSION_READY && st.position_valid && st.aperture_current == 0 && st.mf == BSK_UNKNOWN &&
              st.oss == BSK_UNKNOWN && !st.mark_valid && st.ring == RING_APERTURE,
          "1 µs plus tard : READY, la position valide ; ni ouverture, ni mf, ni oss, ni marque ; la bague gardée "
          "(%u, %d, 0x%04X, %u, %u, %d, %u)", st.session_state, st.position_valid, st.aperture_current, st.mf, st.oss,
          st.mark_valid, st.ring);
    g_ps.n05 = (uint16_t)sources135_msg(SRC_T05, g_ps.m05, sizeof g_ps.m05);
    run_for(30 * MS);
    st = status();
    CHECK(st.aperture_current == 0x11B2 && st.mf == BSK_NO && st.mark_valid, "le 0x05 revenu : l'ouverture, mf, la marque");
}

static void t_counter(void)
{
    printf("plafond : au 4e échec consécutif FAULT (E_LOST) ; READY et D2 remettent le compte à zéro\n");
    boot();
    ps_presence(true);
    run_for(30000 * MS);
    CHECK(status().session_state == SESSION_FAULT && status().last_error == E_LOST, "4 échecs : FAULT, E_LOST");
    CHECK(count_sent(0, ps_now(), 0x01) == 24, "quatre essais de six 0x01 chacun (%zu)", count_sent(0, ps_now(), 0x01));
    ps_presence(false);
    run_for(10 * MS);
    CHECK(status().session_state == SESSION_OFF && status().last_error == E_OK, "FAULT, D2 retombe : OFF, erreur oubliée");
    ps_presence(true);
    run_for(10 * MS);
    CHECK(status().session_state == SESSION_POWERING, "D2 revient : POWERING");

    boot();
    ps_presence(true);
    run_for(19000 * MS);   /* trois échecs (2,815 s + 3 s chacun), le 4e essai en cours */
    CHECK(status().session_state == SESSION_POWERING || status().session_state == SESSION_IDENTIFYING,
          "trois échecs : un 4e essai");
    lens_135();
    g_ps.handshake = false;
    g_ps.flow = true;
    run_for(5000 * MS);
    CHECK(status().session_state == SESSION_READY, "le 4e essai réussit : READY");
    g_ps.flow = false;
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_RECOVERING, "perte après READY : un seul échec compté, RECOVERING");

    boot();
    ps_presence(true);
    run_for(16000 * MS);   /* trois échecs comptés (le 3e à 14,445 s), le 4e essai pas encore parti (17,445 s) */
    CHECK(status().session_state == SESSION_RECOVERING, "trois échecs : RECOVERING");
    ps_presence(false);
    run_for(10 * MS);
    CHECK(status().session_state == SESSION_OFF, "D2 retombe : OFF");
    ps_presence(true);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_RECOVERING, "D2 revient, un échec : RECOVERING, pas FAULT");
}

/* ─────────────────────────── MOUVEMENT ─────────────────────────── */

static uint32_t cmd(bsk_cmd_op_t op, int32_t arg)
{
    bsk_cmd_t c = {.seq = ++seq_n, .op = op, .arg = arg};
    bsk_session_command(&c);
    note_state(ps_now());
    return c.seq;
}

/* Le final de `seq` après son ACK_ACCEPTED : `r`, `why`, et son instant. */
static bool final_is(uint32_t seq, bsk_ack_result_t r, bsk_err_t why, uint64_t *t)
{
    size_t k = 0;
    const ack_rec_t *f[3] = {0};
    for (size_t i = 0; i < n_acks && k < 3; i++)
        if (ACKS[i].a.seq == seq) f[k++] = &ACKS[i];
    if (k != 2 || f[0]->a.result != ACK_ACCEPTED || f[1]->a.result != r || f[1]->a.reason != why) return false;
    if (t) *t = f[1]->t;
    return true;
}

/* Instant de la première entrée dans l'état de mouvement `mv` à partir du changement `from`, 0 sinon. */
static uint64_t mv_entered(uint8_t mv, size_t from)
{
    for (size_t i = from; i < n_mchg; i++)
        if (mchg[i].state == mv) return mchg[i].t;
    return 0;
}

/* Un moteur scripté derrière le flux : il lit le 0x1D du 0x04 et avance de 250 pas par trame vers la
 * cible, publiée au 0x06 de la même paire (3 ms après le 0x04, FLOW_06_US du répondeur). Chaque
 * comportement est un réglage : l'accusé 1D 00 après le 0x06 de l'arrivée (samyang.md § 4.4, et § 4.3 si la cible
 * est la position), l'octet de mouvement du 0x05 (offset 60) pendant le
 * mouvement et `hold` trames après, un départ retardé d'une trame (`lag`, octet de mouvement seul), un
 * moteur qui ne bouge jamais (`frozen`), une position instable (`jitter`), et un octet de mouvement non
 * nul `nz_left` trames sans bouger, après `wait` trames immobiles. Valeurs de test, sans source. `evict` : le 135 qui
 * n'exécute pas encore un 0x1D l'abandonne pour le même reçu de nouveau, et accuse le premier par `1D 00` (samyang.md
 * § 4.4) ; joué ici en accusant `1D 00` chaque 0x1D reçu, sans bouger (avec `frozen`). */
static struct {
    bool     ack, move_byte, lag, frozen, jitter, evict;
    bool     ack1c, pend1c;                /* 1C 00 après le 0x06 qui suit un 0x1C (samyang.md § 4.4) */
    unsigned hold, wait;
    int32_t  pos, target;
    bool     run, fresh, flip;
    unsigned nz_left;
    uint8_t  prev_mv;
    uint64_t t_ack06, t_arrive06, t_nz05, t_zero05;
} M;

static void motor(const bsk_frame_t *f, uint64_t t)
{
    bool ack = false;
    uint8_t mv = 0;
    if (f->cls == 1 && f->msg[0] == 0x1C) {
        M.run = false;
        M.pend1c = M.ack1c;
    }
    if (!(f->cls == 1 && f->msg[0] == 0x04)) return;
    if (f->len == 19 && f->msg[14] == 0x1D && !M.frozen) {
        M.target = f->msg[15] | f->msg[16] << 8;
        M.run = M.target != M.pos;
        M.fresh = true;
        ack = !M.run && M.ack;
    }
    if (f->len == 19 && f->msg[14] == 0x1D && M.evict) ack = true;
    if (M.jitter) {
        M.flip = !M.flip;
        M.pos += M.flip ? 1 : -1;
    } else if (M.run && M.fresh && M.lag) {
        mv = M.move_byte;
    } else if (M.run) {
        int32_t d = M.target - M.pos;
        M.pos += d > 250 ? 250 : d < -250 ? -250 : d;
        mv = M.move_byte;
        if (M.pos == M.target) {
            M.run = false;
            ack = M.ack;
            M.nz_left = M.hold;
            M.t_arrive06 = t + 3 * MS;
        }
    } else if (M.wait) {
        M.wait--;
    } else if (M.nz_left) {
        M.nz_left--;
        mv = 1;
    }
    M.fresh = false;
    if (mv && !M.prev_mv && !M.t_nz05) M.t_nz05 = t + 2 * MS;
    if (!mv && M.prev_mv) M.t_zero05 = t + 2 * MS;
    M.prev_mv = mv;
    set_pos((uint16_t)M.pos);
    g_ps.m05[61] = mv;
    g_ps.n06 = 40;
    if (ack) {
        g_ps.m06[40] = 0x1D;
        g_ps.m06[41] = 0x00;
        g_ps.n06 = 42;
        M.t_ack06 = t + 3 * MS;
    }
    if (M.pend1c) {
        g_ps.m06[g_ps.n06] = 0x1C;
        g_ps.m06[g_ps.n06 + 1] = 0x00;
        g_ps.n06 = (uint16_t)(g_ps.n06 + 2);
        M.pend1c = false;
    }
}

/* Un objectif du flux en READY, puis le moteur scripté, réglé par l'appelant ensuite. */
static void ready_motor(void)
{
    boot();
    lens_135();
    g_ps.flow = true;
    ps_presence(true);
    run_for(2000 * MS);
    CHECK(status().session_state == SESSION_READY && status().focus_position == 14623, "READY en 14623");
    memset(&M, 0, sizeof M);
    M.pos = 14623;
    g_ps.on_send = motor;
}

static void t_mv_ack(void)
{
    uint64_t tf = 0;
    uint32_t q;
    size_t m0;
    printf("MOUVEMENT : MOVING -> SETTLING à l'accusé 1D, sur la trame de l'arrivée ; ARRIVED à l'immobilité\n");
    ready_motor();
    M.ack = true;
    m0 = n_mchg;
    q = cmd(CMD_FOCUS_GOTO, 16623);
    run_for(2000 * MS);
    CHECK(mv_entered(MOTION_MOVING, m0) != 0 && M.t_ack06 != 0 && mv_entered(MOTION_SETTLING, m0) == M.t_ack06,
          "SETTLING au 0x06 qui porte l'accusé, alors que la position vient de changer");
    CHECK(final_is(q, ACK_COMPLETED, E_OK, &tf) && mv_entered(MOTION_ARRIVED, m0) == tf && tf > M.t_ack06 + 300 * MS &&
              tf < M.t_ack06 + 350 * MS,
          "ARRIVED, ACK_COMPLETED, à l'immobilité : 300 ms et dix 0x06 (%lld µs)", (long long)(tf - M.t_ack06));
    CHECK(status().focus_position == 16623 && status().motion_state == MOTION_ARRIVED, "position finale publiée");
}

static void t_mv_fallback(void)
{
    uint32_t q;
    size_t m0;
    printf("MOUVEMENT : ni accusé ni octet de mouvement : SETTLING au premier 0x06 sans changement\n");
    ready_motor();
    m0 = n_mchg;
    q = cmd(CMD_FOCUS_GOTO, 16623);
    run_for(2000 * MS);
    CHECK(M.t_arrive06 != 0 && mv_entered(MOTION_SETTLING, m0) == M.t_arrive06 + 16666,
          "SETTLING une trame après l'arrivée (%lld µs)", (long long)(mv_entered(MOTION_SETTLING, m0) - M.t_arrive06));
    CHECK(final_is(q, ACK_COMPLETED, E_OK, NULL) && status().focus_position == 16623, "ARRIVED, ACK_COMPLETED");
}

static void t_mv_move_byte(void)
{
    uint64_t tf = 0;
    uint32_t q;
    size_t m0;
    printf("MOUVEMENT : octet de mouvement du 0x05 : démarrage, pas d'immobilité tant qu'il n'est pas revenu à 0\n");
    ready_motor();
    M.move_byte = true;
    M.lag = true;
    M.hold = 30;
    m0 = n_mchg;
    q = cmd(CMD_FOCUS_GOTO, 16623);
    run_for(3000 * MS);
    CHECK(M.t_nz05 != 0 && mv_entered(MOTION_MOVING, m0) == M.t_nz05,
          "COMMANDED -> MOVING au 0x05 non nul, la position n'a pas encore changé");
    CHECK(M.t_zero05 > M.t_arrive06 + 400 * MS && mv_entered(MOTION_SETTLING, m0) == M.t_zero05,
          "position arrêtée 30 trames : SETTLING seulement au retour à 0 de l'octet");
    CHECK(final_is(q, ACK_COMPLETED, E_OK, &tf) && tf > M.t_zero05 + 300 * MS && tf < M.t_zero05 + 350 * MS,
          "ARRIVED 300 ms après le retour à 0, pas avant");

    ready_motor();              /* cible = position : immobile 5 trames, puis l'octet non nul 30 trames, sans bouger */
    M.wait = 5;
    M.nz_left = 30;
    m0 = n_mchg;
    q = cmd(CMD_FOCUS_GOTO, 14623);
    run_for(3000 * MS);
    CHECK(mv_entered(MOTION_MOVING, m0) == M.t_nz05 && mv_entered(MOTION_SETTLING, m0) == M.t_zero05,
          "cible = position : MOVING à l'octet non nul, SETTLING à son retour à 0");
    CHECK(final_is(q, ACK_COMPLETED, E_OK, &tf) && tf > M.t_zero05 + 300 * MS && tf < M.t_zero05 + 350 * MS,
          "l'immobilité se compte depuis le démarrage, pas depuis le dépôt (%lld µs)", (long long)(tf - M.t_zero05));
}

static void t_mv_same(void)
{
    uint64_t tf = 0, t0;
    uint32_t q;
    size_t m0;
    printf("MOUVEMENT : cible égale à la position, sans accusé : ARRIVED dès l'immobilité, pas STALLED\n");
    ready_motor();
    m0 = n_mchg;
    t0 = ps_now();
    q = cmd(CMD_FOCUS_GOTO, 14623);
    run_for(2000 * MS);
    CHECK(final_is(q, ACK_COMPLETED, E_OK, &tf) && tf > t0 + 300 * MS && tf < t0 + 400 * MS,
          "ARRIVED, ACK_COMPLETED, à l'immobilité (%lld µs)", (long long)(tf - t0));
    CHECK(mv_entered(MOTION_STALLED, m0) == 0 && mv_entered(MOTION_MOVING, m0) == 0, "ni MOVING ni STALLED");
}

static void t_mv_stall_start(void)
{
    uint64_t tf, t0;
    uint32_t q;
    size_t from, af, n1d;
    printf("MOUVEMENT : ni démarrage ni accusé en 1 s, trois fois (le 0x1D renvoyé) : STALLED, E_STALL, la session reste READY\n");
    ready_motor();
    M.frozen = true;
    t0 = ps_now();
    from = g_ps_n;
    q = cmd(CMD_FOCUS_GOTO, 16623);
    run_for(4000 * MS);
    count_af_1d(from, &af, &n1d);
    CHECK(final_is(q, ACK_FAILED, E_STALL, &tf) && tf == t0 + 3000 * MS,
          "ACK_FAILED, E_STALL, à 3 s exactement : la fin de la troisième fenêtre");
    CHECK(n1d == 3, "trois 0x1D émis (%zu)", n1d);
    CHECK(status().motion_state == MOTION_STALLED && status().session_state == SESSION_READY, "STALLED, READY");
    q = cmd(CMD_FOCUS_GOTO, 14000);
    CHECK(status().motion_state == MOTION_COMMANDED, "un nouveau goto repart (STALLED -> IDLE -> COMMANDED)");
}

/* N2 (décision de l'humain du 2026-10-09) : le 0x04 qui porte le 0x1D est dit raté (fail_sends : E_BUS, rien sur le fil
 * ici ; sur la carte, l'attente de fin d'émission dépassée, les octets peut-être partis) ; TRANSACTION reporte le même 0x1D
 * à la paire suivante. Le 135 bloqué, qui aurait reçu les deux, accuse le premier, évincé, par `1D 00` (`evict`), sans
 * bouger. Cet accusé n'est pas une arrivée : jamais SETTLING ni ARRIVED, le 0x1D renvoyé par MOUVEMENT à 1 s et à 2 s,
 * STALLED à 3 s, ACK_FAILED E_STALL (g ne rend pas ok). Le même accusé derrière un report sans émission tentée (le 0x03 de la
 * paire raté, son 0x04 jamais tenté) compte comme une émission unique : SETTLING au 0x06 qui le porte, puis ARRIVED à
 * l'immobilité, comme avant. La commande déposée 1 ms après un 0x04 : le 0x1D et le bit AF partent dans la paire suivante. */
static void t_mv_resent_by_txn(void)
{
    uint64_t t4 = 0, t0, tf = 0;
    uint32_t q;
    size_t from, m0, af, n1d;
    printf("MOUVEMENT : le 0x1D reporté après son 0x04 raté, puis 1D 00 sans mouvement : pas une arrivée, STALLED\n");
    for (int k = 0; k < 2; k++) {
        bool fail04 = k == 0;
        ready_motor();
        M.frozen = true;
        M.evict = true;
        for (size_t j = 0; j < g_ps_n && j < PS_LOG_CAP; j++)
            if (g_ps_log[j].kind == PS_SEND && g_ps_log[j].frame.msg[0] == 0x04) t4 = g_ps_log[j].t;
        run_to(t4 + 1000);                           /* 1 ms après le dernier 0x04 : avant le 0x03 suivant, à t4 + 15166 */
        m0 = n_mchg;
        from = g_ps_n;
        t0 = ps_now();
        q = cmd(CMD_FOCUS_GOTO, 16623);
        if (fail04) run_to(t4 + 15166 + 500);        /* le 0x03 de la paire parti, son 0x04 à t4 + 16666 */
        g_ps.fail_sends = 1;                         /* l'émission suivante : le 0x04 (fail04) ou le 0x03 de la paire */
        run_for(4000 * MS);
        count_af_1d(from, &af, &n1d);
        if (fail04) {
            CHECK(g_ps.fail_sends == 0 && count_sent(t4 + 15166, t4 + 15167, 0x03) == 1 &&
                      count_sent(t4 + 16666, t4 + 16667, 0x04) == 0 && count_sent(t4 + 33332, t4 + 33333, 0x04) == 1,
                  "le 0x03 parti, son 0x04 raté ; le 0x04 de la paire suivante parti");
            CHECK(n1d == 3 && M.t_ack06 != 0, "le 0x1D reporté, puis renvoyé à 1 s et 2 s : trois sur le fil (%zu) ; 1D 00 reçu", n1d);
            CHECK(final_is(q, ACK_FAILED, E_STALL, &tf) && tf == t0 + 3000 * MS && status().motion_state == MOTION_STALLED,
                  "ACK_FAILED, E_STALL, à 3 s du dépôt (%lld µs) ; STALLED", (long long)(tf - t0));
            CHECK(mv_entered(MOTION_SETTLING, m0) == 0 && mv_entered(MOTION_ARRIVED, m0) == 0,
                  "1D 00 après le report d'un 0x04 tenté : jamais SETTLING ni ARRIVED");
        } else {
            CHECK(g_ps.fail_sends == 0 && count_sent(t4 + 15166, t4 + 16667, 0x03) == 0 &&
                      count_sent(t4 + 15166, t4 + 16667, 0x04) == 0,
                  "le 0x03 de la paire raté, pas de 0x04");
            CHECK(n1d == 1 && M.t_ack06 != 0 && mv_entered(MOTION_SETTLING, m0) == M.t_ack06,
                  "un report sans émission tentée : un seul 0x1D (%zu), SETTLING au 0x06 qui porte 1D 00", n1d);
            CHECK(final_is(q, ACK_COMPLETED, E_OK, &tf) && tf > M.t_ack06 + 300 * MS && tf < M.t_ack06 + 350 * MS,
                  "ARRIVED à l'immobilité, comme une émission unique (%lld µs)", (long long)(tf - M.t_ack06));
        }
    }
}

static void t_mv_stall_still(void)
{
    uint64_t tf = 0, tm;
    uint32_t q;
    size_t m0;
    printf("MOUVEMENT : position instable 10 s : STALLED, E_STALL\n");
    ready_motor();
    M.jitter = true;
    m0 = n_mchg;
    q = cmd(CMD_FOCUS_GOTO, 16623);
    run_for(12000 * MS);
    tm = mv_entered(MOTION_MOVING, m0);
    CHECK(tm != 0 && final_is(q, ACK_FAILED, E_STALL, &tf) && tf == tm + 10000 * MS,
          "MOVING, puis STALLED 10 s après le démarrage (%lld µs)", (long long)(tf - tm));
    CHECK(status().session_state == SESSION_READY, "READY");
}

/* STALLED arrête l'objectif : un 0x1C au même instant, surveillé comme celui de q (renvoyé à 1,5 s et à 3 s sans accusé ni
 * immobilité). Le moteur figé (frozen) est immobile : un seul 0x1C, l'immobilité éteint la surveillance. Le moteur instable
 * (jitter) ne l'est jamais : trois 0x1C ; un premier 0x1C qui ne part pas (E_BUS) est renvoyé de même. Aucun 0x1D ne part
 * après STALLED (drive_stop retire un 0x1D encore accroché : bsk_txn.h, bsk_txn_attach) ; un goto déposé ensuite éteint la
 * surveillance (ni 0x1C de plus, ni 0x1D d'avant). */
static void t_mv_stall_stop(void)
{
    uint64_t ts = 0, tm;
    uint32_t q;
    size_t from, m0, af, n1d;
    printf("MOUVEMENT : STALLED émet un 0x1C, surveillé comme celui de q ; aucun 0x1D accroché après lui\n");
    ready_motor();
    M.frozen = true;
    q = cmd(CMD_FOCUS_GOTO, 16623);
    run_for(8000 * MS);
    CHECK(final_is(q, ACK_FAILED, E_STALL, &ts), "STALLED, E_STALL");
    from = g_ps_n;
    while (from > 0 && g_ps_log[from - 1].t >= ts) from--;
    CHECK(count_sent(ts, ts + 1, 0x1C) == 1, "un 0x1C à l'instant de STALLED");
    CHECK(count_sent(ts + 1, ts + 5000 * MS, 0x1C) == 0, "le moteur immobile : l'arrêt confirmé, aucun renvoi");
    count_af_1d(from, &af, &n1d);
    CHECK(n1d == 0, "aucun 0x1D après STALLED (%zu)", n1d);

    ready_motor();
    M.jitter = true;
    m0 = n_mchg;
    q = cmd(CMD_FOCUS_GOTO, 16623);
    run_for(16000 * MS);
    tm = mv_entered(MOTION_MOVING, m0);
    CHECK(final_is(q, ACK_FAILED, E_STALL, &ts) && ts == tm + 10000 * MS, "instable : STALLED 10 s après le démarrage");
    CHECK(count_sent(ts, ts + 1, 0x1C) == 1 && count_sent(ts + 1500 * MS, ts + 1500 * MS + 1, 0x1C) == 1 &&
              count_sent(ts + 3000 * MS, ts + 3000 * MS + 1, 0x1C) == 1 && count_sent(ts, ts + 6000 * MS, 0x1C) == 3,
          "jamais immobile, sans accusé : trois 0x1C, à 0, 1,5 et 3 s de STALLED");

    ready_motor();                                  /* le premier 0x1C ne part pas (E_BUS) : surveillé quand même */
    M.jitter = true;
    m0 = n_mchg;
    q = cmd(CMD_FOCUS_GOTO, 16623);
    run_for(100 * MS);
    tm = mv_entered(MOTION_MOVING, m0);
    run_to(tm + 10000 * MS - 1);
    g_ps.fail_sends = 1;
    run_for(6000 * MS);
    CHECK(final_is(q, ACK_FAILED, E_STALL, &ts) && ts == tm + 10000 * MS && g_ps.fail_sends == 0, "instable : STALLED, son 0x1C raté");
    CHECK(count_sent(ts, ts + 1500 * MS, 0x1C) == 0 && count_sent(ts + 1500 * MS, ts + 1500 * MS + 1, 0x1C) == 1 &&
              count_sent(ts + 3000 * MS, ts + 3000 * MS + 1, 0x1C) == 1,
          "le 0x1C de STALLED pas parti : renvoyé à 1,5 s et à 3 s, comme un 0x1C sans accusé");

    ready_motor();
    M.jitter = true;
    q = cmd(CMD_FOCUS_GOTO, 16623);
    run_for(10200 * MS);
    CHECK(final_is(q, ACK_FAILED, E_STALL, &ts), "instable : STALLED");
    run_for(500 * MS);
    M.jitter = false;
    q = cmd(CMD_FOCUS_GOTO, 14000);
    run_for(5000 * MS);
    CHECK(count_sent(ts + 1, ts + 6000 * MS, 0x1C) == 0, "un goto déposé 0,5 s après STALLED éteint la surveillance : aucun renvoi");
}

static void t_mv_loss(void)
{
    uint32_t q;
    uint64_t tf;
    printf("MOUVEMENT : perte pendant un goto : ACK_FAILED, E_LOST, RECOVERING\n");
    ready_motor();
    M.jitter = true;                            /* en mouvement, jamais immobile */
    q = cmd(CMD_FOCUS_GOTO, 16623);
    run_for(100 * MS);
    CHECK(status().motion_state == MOTION_MOVING, "MOVING");
    g_ps.flow = false;
    run_for(3000 * MS);
    CHECK(final_is(q, ACK_FAILED, E_LOST, &tf) && tf == entered(SESSION_RECOVERING, 0), "ACK_FAILED, E_LOST, à l'entrée en RECOVERING");
    CHECK(status().motion_state == MOTION_IDLE, "le mouvement est oublié avec la session");
    run_for(12000 * MS);
    CHECK(mv_entered(MOTION_STALLED, 0) == 0 && status().motion_state == MOTION_IDLE, "et son échéance avec lui : jamais STALLED");
}

static void t_mv_forgotten(void)
{
    size_t af = 0, n1d = 0;
    long i01;
    printf("MOUVEMENT : D2 retombe avant que le 0x1D parte : rien ne repart dans la session suivante\n");
    ready_motor();
    (void)cmd(CMD_FOCUS_GOTO, 16623);
    ps_presence(false);
    run_for(10 * MS);
    CHECK(status().session_state == SESSION_OFF, "OFF");
    ps_presence(true);
    run_for(3000 * MS);
    i01 = sent(0, 2, 0x01);
    while (i01 >= 0 && sent((size_t)i01 + 1, 2, 0x01) >= 0) i01 = sent((size_t)i01 + 1, 2, 0x01);
    CHECK(i01 >= 0 && status().session_state == SESSION_READY, "nouvelle session, READY");
    count_af_1d(0, &af, &n1d);
    CHECK(n1d == 0 && af == 0, "aucun 0x1D ni bit AF, avant comme après (%zu, %zu)", n1d, af);
}

/* ─────────────────────────── l'ouverture ─────────────────────────── */

/* Le dernier accusé de `seq` : r, why ; `n` accusés en tout. */
static bool acks_are(uint32_t seq, size_t n, bsk_ack_result_t r, bsk_err_t why)
{
    size_t k = 0;
    const ack_rec_t *last = NULL;
    for (size_t i = 0; i < n_acks; i++)
        if (ACKS[i].a.seq == seq) {
            k++;
            last = &ACKS[i];
        }
    return k == n && last && last->a.result == r && last->a.reason == why;
}

/* CMD_APERTURE_SET contre le 135 des traces : sa plage, réponse au 0x08, BF 11 / 00 19 (SRC_R08, full:15) ; la consigne
 * part dans le 0x03 suivant, bornée à la plage (en codes), finie tout de suite ; une nouvelle session la ramène à f/1,8,
 * B2 11, bornée elle aussi à la plage : BF 11.
 * Codes de test : 0x14F9 (f/5,6), 0x1000 et 0x2000 (hors plage). */
static void t_aperture(void)
{
    bsk_status_t s;
    uint32_t q;
    size_t from;
    printf("ouverture : la plage du 0x08, la consigne dans le 0x03, bornée, finie tout de suite ; f/1,8 à chaque session\n");
    boot();
    lens_135();
    q = cmd(CMD_APERTURE_SET, 0x14F9);
    CHECK(acks_are(q, 1, ACK_REJECTED, E_BUSY), "OFF : ACK_REJECTED, E_BUSY");
    ps_presence(true);
    run_for(3000 * MS);
    s = status();
    CHECK(s.session_state == SESSION_READY && s.aperture_min == 0x11BF && s.aperture_max == 0x1900,
          "plage : le 0x08, octets 1-4, BF 11 00 19 (0x%04X, 0x%04X)", s.aperture_min, s.aperture_max);
    CHECK(s.aperture_current == 0x11B2, "relue : le 0x05 du flux, offsets 0-1, B2 11 (0x%04X)", s.aperture_current);
    CHECK((s.capabilities & (CAP_APERTURE | CAP_APERTURE_READBACK)) == (CAP_APERTURE | CAP_APERTURE_READBACK),
          "capacités : ouverture et relecture (0x%X)", (unsigned)s.capabilities);
    from = g_ps_n;
    q = cmd(CMD_APERTURE_SET, 0x14F9);
    CHECK(acks_are(q, 2, ACK_COMPLETED, E_OK) && g_ps_n == from, "ACK_ACCEPTED puis ACK_COMPLETED, rien d'émis dans l'appel");
    run_for(50 * MS);
    CHECK(ap03_all(from, 0x14F9), "le 0x03 suivant porte F9 14 F9 14");
    from = g_ps_n;
    q = cmd(CMD_APERTURE_SET, 0x1000);
    run_for(50 * MS);
    CHECK(acks_are(q, 2, ACK_COMPLETED, E_OK) && ap03_all(from, 0x11BF), "sous la plage : bornée au minimum, BF 11");
    from = g_ps_n;
    q = cmd(CMD_APERTURE_SET, 0x2000);
    run_for(50 * MS);
    CHECK(acks_are(q, 2, ACK_COMPLETED, E_OK) && ap03_all(from, 0x1900), "au-dessus : bornée au maximum, 00 19");
    CHECK(status().aperture_current == 0x11B2, "relue : toujours le 0x05 de l'objectif, pas la consigne");

    q = cmd(CMD_ATTACH, 0);                          /* une nouvelle session */
    from = g_ps_n;
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY && ap03_all(from, 0x11BF),
          "nouvelle session : la consigne revient à f/1,8, bornée à la plage, BF 11");

    ready_motor();                                   /* un goto en vol : CMD_APERTURE_SET refusée, E_BUSY */
    M.frozen = true;
    (void)cmd(CMD_FOCUS_GOTO, 16623);
    from = g_ps_n;
    q = cmd(CMD_APERTURE_SET, 0x14F9);
    run_for(50 * MS);
    CHECK(acks_are(q, 1, ACK_REJECTED, E_BUSY) && ap03_all(from, 0x11BF), "pendant un goto : E_BUSY, la consigne inchangée");
}

/* La plage d'ouverture se lit dans une réponse au 0x08 d'au moins 6 octets ; elle est celle de la session. Sans elle : ni
 * capacité ni consigne (E_NOCAP). Valeurs de test, sans autre source. */
static void t_aperture_08(void)
{
    uint32_t q;
    printf("ouverture : réponse au 0x08 de 6 octets lue, de 5 ignorée ; sans 0x08, E_NOCAP ; la plage oubliée avec la session\n");
    boot();
    lens_135();
    ps_answer(0x08, (const uint8_t[]){0x08, 0x01, 0x12, 0x02, 0x19, 0x00}, 6, 5 * MS);
    ps_presence(true);
    run_for(3000 * MS);
    CHECK(status().aperture_min == 0x1201 && status().aperture_max == 0x1902 && (status().capabilities & CAP_APERTURE),
          "0x08 de 6 octets : plage 0x1201 / 0x1902 (0x%04X, 0x%04X)", status().aperture_min, status().aperture_max);
    ps_answer(0x08, (const uint8_t[]){0x08, 0x01, 0x12, 0x02, 0x19}, 5, 5 * MS);
    (void)cmd(CMD_ATTACH, 0);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY && !(status().capabilities & (CAP_APERTURE | CAP_APERTURE_READBACK)) &&
              status().aperture_min == 0 &&
              status().aperture_max == 0,
          "nouvelle session, 0x08 de 5 octets : pas de plage, celle d'avant oubliée (0x%X)", (unsigned)status().capabilities);
    q = cmd(CMD_APERTURE_SET, 0x14F9);
    CHECK(acks_are(q, 1, ACK_REJECTED, E_NOCAP), "sans plage : ACK_REJECTED, E_NOCAP");
}


/* La consigne f/1,8 de chaque session ramenée à la plage du 0x08 dès qu'elle est connue (L03-05). Le 135 des traces dont la
 * réponse au 0x08 porte une plage f/4 - f/22 : 00 14 / EB 18, codes à la main, round(256 × (2·log2(f) + 16)) : f/4 -> 5120
 * = 0x1400, f/22 -> 6379 = 0x18EB. Attendus : chaque 0x03 de la session, depuis le premier (la boucle lancée avec le 0x10,
 * après la réponse au 0x08), porte f/4, 00 14, jamais f/1,8, B2 11. */
static void t_aperture_start(void)
{
    printf("la consigne f/1,8 du début de session ramenée à la plage du 0x08 : f/4 à un objectif qui ouvre à f/4\n");
    boot();
    lens_135();
    ps_answer(0x08, (const uint8_t[]){0x08, 0x00, 0x14, 0xEB, 0x18, 0x00}, 6, 5 * MS);
    ps_presence(true);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY && status().aperture_min == 0x1400, "READY, la plage f/4 - f/22");
    CHECK(ap03_all(0, 0x1400), "chaque 0x03 porte f/4, 00 14");
}

/* Une plage du 0x08 dont un code est hors du domaine que la carte convertit, 0x0E00 à 0x2000 (bsk_contract.h), est inconnue
 * (L07-03) : un 0x08 de 6 octets, une réponse, pas une absence. Le 135 des traces dont la réponse au 0x08 porte BF 11 / FF FF
 * (0xFFFF, au-delà de f/256), puis FF 0D / 00 19 (0x0DFF, en deçà de f/0,5). Attendus, à la main : le 0x08 envoyé une fois
 * (pas renvoyé) ; ni plage (0, 0) ni CAP_APERTURE ; la consigne de départ reste f/1,8, B2 11 (bornée à BF 11 / FF FF, elle
 * serait BF 11) ; CMD_APERTURE_SET refusée, E_NOCAP. */
static void t_aperture_absurd(void)
{
    static const uint8_t r08[2][6] = {{0x08, 0xBF, 0x11, 0xFF, 0xFF, 0x00}, {0x08, 0xFF, 0x0D, 0x00, 0x19, 0x00}};
    uint32_t q;
    bsk_status_t st;
    printf("une plage du 0x08 hors du domaine des codes : inconnue, sans renvoi ; la consigne de départ pas bornée à elle\n");
    for (int k = 0; k < 2; k++) {
        boot();
        lens_135();
        ps_answer(0x08, r08[k], sizeof r08[k], 5 * MS);
        ps_presence(true);
        run_for(3000 * MS);
        st = status();
        CHECK(st.session_state == SESSION_READY && count_sent(0, UINT64_MAX, 0x08) == 1,
              "0x%02X%02X / 0x%02X%02X : READY, le 0x08 envoyé une fois (%zu)", r08[k][2], r08[k][1], r08[k][4], r08[k][3],
              count_sent(0, UINT64_MAX, 0x08));
        CHECK(!(st.capabilities & (CAP_APERTURE | CAP_APERTURE_READBACK)) && st.aperture_min == 0 && st.aperture_max == 0,
              "pas de plage (0x%04X, 0x%04X, 0x%X)", st.aperture_min, st.aperture_max, (unsigned)st.capabilities);
        CHECK(ap03_all(0, 0x11B2), "la consigne de départ, f/1,8, B2 11, pas bornée à la plage absurde");
        q = cmd(CMD_APERTURE_SET, 0x14F9);
        CHECK(acks_are(q, 1, ACK_REJECTED, E_NOCAP), "CMD_APERTURE_SET : E_NOCAP");
    }
}
/* `a` et `o` à travers la couche HOTE, contre le Sony FE 24-105 G des captures (sony(), plus haut) : sa plage, réponse au
 * 0x08, 05 14 / 00 19 (17-45-01-034Z.txt:26-30), f/4,03 et f/22,6 ; son ouverture relue au 0x05, 05 14 (:65-66), f/4 — et
 * non la consigne f/1,8 de la carte. Après a5.6, le 0x03 porte le code de f/5,6, round(256 × (2·log2(5,6) + 16)) = 5369
 * = 0x14F9, calculé à la main ; le Sony relu reste à f/4 (sa trame est figée). */
static void t_aperture_sony(void)
{
    char r[BSK_HOST_REPLY_MAX];
    size_t from;
    printf("ouverture, HOTE contre le Sony : a -> 4.0-22, o -> 4.0 (relue), a5.6 -> ok et F9 14 F9 14 dans le 0x03\n");
    boot();
    sony(true);
    bsk_host_init("2.0.0-dev", "poweron");
    ps_presence(true);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY && status().aperture_current == 0x1405 && status().aperture_min == 0x1405 &&
              status().aperture_max == 0x1900,
          "READY ; relue 0x1405, plage 0x1405 / 0x1900 (0x%04X, 0x%04X, 0x%04X)", status().aperture_current,
          status().aperture_min, status().aperture_max);
    CHECK(bsk_host_line("a", r, sizeof r) && !strcmp(r, "4.0-22"), "a -> « %s », attendu « 4.0-22 »", r);
    CHECK(bsk_host_line("o", r, sizeof r) && !strcmp(r, "4.0"), "o -> « %s », attendu « 4.0 »", r);
    from = g_ps_n;
    CHECK(bsk_host_line("a5.6", r, sizeof r) && !strcmp(r, "ok"), "a5.6 -> « %s », attendu « ok »", r);
    run_for(50 * MS);
    CHECK(ap03_all(from, 0x14F9), "après a5.6 : chaque 0x03 porte F9 14 F9 14");
    CHECK(bsk_host_line("o", r, sizeof r) && !strcmp(r, "4.0"), "o -> « %s » : l'ouverture relue, pas la consigne", r);
    CHECK(bsk_host_line("a1.7", r, sizeof r) && !strcmp(r, "er range ap"), "a1.7 -> « %s », attendu « er range ap »", r);
}

/* ─────────────────────────── la bague ─────────────────────────── */

/* Les 0x04 émis depuis l'entrée `from` du journal ont tous le bit 1 de leur octet 3 (offset 3, type exclu) à `on`, le
 * reste de l'octet 0x81 ; au moins un 0x04. */
static bool body_all(size_t from, bool on)
{
    size_t n = 0;
    for (size_t i = from; i < g_ps_n && i < PS_LOG_CAP; i++) {
        const bsk_frame_t *f = &g_ps_log[i].frame;
        if (g_ps_log[i].kind != PS_SEND || f->msg[0] != 0x04) continue;
        if (((f->msg[4] & 0x02) != 0) != on || (f->msg[4] & ~0x02) != 0x81) return false;
        n++;
    }
    return n > 0;
}

/* Une ligne jouée à travers la couche HOTE : sa réponse est `want`. */
static bool host_is(const char *line, const char *want, char *r, size_t cap)
{
    return bsk_host_line(line, r, cap) && !strcmp(r, want);
}

#define HOST(line, want) CHECK(host_is(line, want, r, sizeof r), "%s -> « %s », attendu « %s »", line, r, want)

/* L'offset 62 du 0x05 du flux (m05[63], l'octet de type en m05[0]) ; le 0x05 suivant le porte, et le 0x04 d'après suit
 * le rôle : 50 ms, trois paires. Le 135 des traces y publie 01 (SRC_T05, dump05:7), son commutateur en M1. */
static void sw62(uint8_t v)
{
    g_ps.m05[63] = v;
    run_for(20 * MS);
}

/* Le rôle, en fin de la ligne `t` (` ring=ap|focus|-`). */
static bool t_ring_is(const char *want, char *r, size_t cap)
{
    char k[24];
    const char *p;
    snprintf(k, sizeof k, " ring=%s", want);
    return bsk_host_line("t", r, cap) && (p = strstr(r, k)) != NULL && !strcmp(p, k);
}

#define RING_T(want) CHECK(t_ring_is(want, r, sizeof r), "t : ring=%s (« %s »)", want, r)

/* Le rôle suit l'offset 62 (01 : ouverture, 03 : focus), le bit 1 de l'octet 3 du 0x04 avec lui ; aucune ligne de l'hôte
 * ne le force (`uf`, `ua`, `ux` -> er nocap et rien ne change) ; `t` le publie (`ring`) avec `mf` et `oss`. Le bit
 * retombe avec la session et revient au premier 0x05. */
static void t_ring_role(void)
{
    char r[BSK_HOST_REPLY_MAX];
    bsk_status_t s;
    size_t from;
    long i04;
    printf("bague : le rôle suit l'offset 62 du 0x05, le bit du 0x04 avec lui ; uf, ua, ux n'y touchent pas ; le bit retombe "
           "avec la session\n");
    boot();
    lens_135();
    bsk_host_init("2.0.0-dev", "poweron");
    ps_presence(true);
    run_for(3000 * MS);
    s = status();
    CHECK(s.session_state == SESSION_READY && s.ring == RING_APERTURE && s.mf == BSK_NO && s.oss == BSK_NO,
          "READY, offset 62 à 01 : ouverture, mf non ; offset 64 à 00 : oss non (%u, %u, %u)", s.ring, s.mf, s.oss);
    from = g_ps_n;
    run_for(50 * MS);
    CHECK(body_all(from, true), "offset 62 à 01 : chaque 0x04 porte 04 00 00 19 83");
    RING_T("ap");
    CHECK(bsk_host_line("t", r, sizeof r) && strstr(r, " mf=0 oss=0"), "t : mf=0 oss=0 (« %s »)", r);

    sw62(0x03);
    from = g_ps_n;
    run_for(50 * MS);
    s = status();
    CHECK(body_all(from, false) && s.ring == RING_FOCUS && s.mf == BSK_YES, "offset 62 à 03 : focus, bit retiré, mf oui");
    RING_T("focus");
    CHECK(bsk_host_line("t", r, sizeof r) && strstr(r, " mf=1 oss=0"), "t : mf=1 oss=0 (« %s »)", r);
    sw62(0x01);
    from = g_ps_n;
    run_for(50 * MS);
    CHECK(body_all(from, true), "de nouveau 01 : le bit revient");

    from = g_ps_n;
    HOST("uf", "er nocap");                          /* une ligne inconnue, rien ne change */
    run_for(50 * MS);
    CHECK(body_all(from, true) && status().ring == RING_APERTURE, "uf, en position AF : rien de forcé, ouverture, bit posé");
    sw62(0x03);
    from = g_ps_n;
    HOST("ua", "er nocap");
    HOST("ux", "er nocap");
    run_for(50 * MS);
    CHECK(body_all(from, false) && status().ring == RING_FOCUS, "ua, ux, en position MF : rien de forcé, focus, bit retiré");
    sw62(0x01);
    from = g_ps_n;
    run_for(50 * MS);
    CHECK(body_all(from, true) && status().ring == RING_APERTURE, "de nouveau 01 : ouverture, bit posé");

    (void)cmd(CMD_ATTACH, 0);                        /* une nouvelle session, en rôle ouverture */
    from = g_ps_n;
    run_for(3000 * MS);
    i04 = sent(from, 1, 0x04);
    CHECK(i04 >= 0 && g_ps_log[i04].frame.msg[4] == 0x81, "nouvelle session : le premier 0x04 sans le bit (0x%02X)",
          i04 >= 0 ? g_ps_log[i04].frame.msg[4] : 0);
    from = g_ps_n;
    run_for(50 * MS);
    CHECK(status().session_state == SESSION_READY && body_all(from, true) && status().ring == RING_APERTURE,
          "READY : le bit revenu avec le premier 0x05, ouverture");
}

/* Sans offset 62 (un 0x05 de 63 octets, type compris), le rôle est inconnu : `ring=-`, rien de posé ; l'offset 62
 * sans l'offset 64 (64 octets) : le rôle, pas le stabilisateur. Le Sony des captures (sony()) : offset 62 à 01, 64 à
 * 0x10 (17-45-01-034Z.txt:65-66), mf=0, ouverture, oss=1. */
static void t_ring_nocap(void)
{
    char r[BSK_HOST_REPLY_MAX];
    size_t from;
    printf("bague : sans offset 62, ring=- et rien de posé ; sans offset 64, oss=- ; le Sony, oss=1\n");
    boot();
    lens_135();
    g_ps.n05 = 63;
    bsk_host_init("2.0.0-dev", "poweron");
    ps_presence(true);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY && status().ring == RING_UNKNOWN && status().mf == BSK_UNKNOWN &&
              status().oss == BSK_UNKNOWN,
          "0x05 sans offset 62 : rôle, mf et oss inconnus");
    RING_T("-");
    HOST("u", "er nocap");
    CHECK(bsk_host_line("t", r, sizeof r) && strstr(r, " mf=- oss=-"), "t : mf=- oss=- (« %s »)", r);
    from = g_ps_n;
    run_for(50 * MS);
    CHECK(body_all(from, false), "le bit n'est pas posé");

    g_ps.n05 = 64;
    (void)cmd(CMD_ATTACH, 0);
    run_for(3000 * MS);
    CHECK(status().ring == RING_APERTURE && status().mf == BSK_NO && status().oss == BSK_UNKNOWN,
          "0x05 de 64 octets : l'offset 62 lu, pas l'offset 64");

    boot();
    sony(true);
    bsk_host_init("2.0.0-dev", "poweron");
    ps_presence(true);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY && bsk_host_line("t", r, sizeof r) && strstr(r, " mf=0 oss=1"),
          "le Sony en AF, stabilisateur en marche : t -> mf=0 oss=1 (« %s »)", r);
    RING_T("ap");
}

/* La bague tournée : ces octets à l'offset 60 (m05[61]) des 0x05 suivants, un par trame, puis 0 ; le temps de les jouer
 * et trois trames de plus. Valeurs de la capture 195_samyang.txt (23:02:14 à 19) : FF ou 01, une ou deux trames par
 * cran, puis 00. */
static struct {
    uint8_t v[16];
    size_t  n, i;
} PUL;

static void pulse_hook(const bsk_frame_t *f, uint64_t t)
{
    (void)t;
    if (f->cls == 1 && f->msg[0] == 0x04) g_ps.m05[61] = PUL.i < PUL.n ? PUL.v[PUL.i++] : 0;
}

static void turn(const uint8_t *v, size_t n)
{
    memcpy(PUL.v, v, n);
    PUL.n = n;
    PUL.i = 0;
    g_ps.on_send = pulse_hook;
    run_for((n + 3) * 17 * MS);
}

#define TURN(...) turn((const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}))

/* Le cap de la bague échu, 1/3 s sans tiers appliqué (écrit à la main, pas d'après la constante de ring.c),
 * pour un test qui juge une autre règle que lui. Le geste en cours est clos avec lui (la pause de 400 ms : TURN et consigne
 * en ont déjà passé une centaine de ms). */
static void cap_elapsed(void) { run_for(334 * MS); }

/* La consigne que portent les 0x03 émis dans les 50 ms qui suivent. */
static bool consigne(uint16_t code)
{
    size_t from = g_ps_n;
    run_for(50 * MS);
    return ap03_all(from, code);
}

/* La bague en rôle ouverture contre le 135 des traces (offset 62 à 01), sa plage BF 11 / 00 19 (SRC_R08) : deux impulsions
 * de même sens font un tiers dans la table `thirds`, la consigne bornée à la plage et posée dans le 0x03 comme
 * CMD_APERTURE_SET. Codes calculés à la main, round(256 × (2·log2(f) + 16)) : f/5,6 -> 5368,7 -> 5369 = 0x14F9 ; f/5,0 ->
 * 5284,8 -> 0x14A5 ; f/6,3 -> 5455,5 -> 5456 = 0x1550 ; f/4 -> 5120 = 0x1400 ; f/4,5 -> 5206,9 -> 0x1457 ; f/22 -> 6379,2
 * -> 0x18EB, le cran le plus proche de 0x1900 ; f/25 -> 6473,6 -> 0x194A ; f/1,6 -> 4442,6 -> 0x115B. */
static void t_ring_aperture(void)
{
    printf("bague en rôle ouverture : deux impulsions par tiers, un sens puis l'autre, bornée à la plage ; la pause, le "
           "changement de sens ; l'ouverture publiée\n");
    boot();
    lens_135();
    ps_presence(true);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY && status().ring == RING_APERTURE, "READY, bague en ouverture");
    (void)cmd(CMD_APERTURE_SET, 0x14F9);
    CHECK(consigne(0x14F9), "f/5,6 par CMD_APERTURE_SET");
    TURN(0xFF, 0xFF);
    CHECK(consigne(0x14A5), "deux impulsions FF : un tiers vers l'ouvert, f/5,0, A5 14");
    cap_elapsed();
    TURN(0x01, 0x01);
    CHECK(consigne(0x14F9), "deux impulsions 01 : un tiers vers le fermé, f/5,6, F9 14");
    TURN(0x01);
    CHECK(consigne(0x14F9), "une impulsion seule : rien");
    run_for(500 * MS);
    TURN(0x01);
    CHECK(consigne(0x14F9), "une deuxième après une pause de plus de 400 ms : le geste était clos, rien");
    run_for(500 * MS);
    TURN(0xFF, 0x01, 0x01);
    CHECK(consigne(0x1550), "FF puis 01 01 : le changement de sens repart de zéro, un tiers vers le fermé, f/6,3");
    (void)cmd(CMD_APERTURE_SET, 0x1900);
    TURN(0x01, 0x01);
    CHECK(consigne(0x1900), "au maximum de la plage : f/25 (0x194A) borné à 00 19");
    (void)cmd(CMD_APERTURE_SET, 0x11BF);
    TURN(0xFF, 0xFF);
    CHECK(consigne(0x11BF), "au minimum : f/1,6 (0x115B) borné à BF 11");

    (void)cmd(CMD_APERTURE_SET, 0x14F9);
    g_ps.m05[63] = 0x03;
    TURN(0x01, 0x01);
    CHECK(status().ring == RING_FOCUS && consigne(0x14F9), "bague en focus : les impulsions ne touchent pas l'ouverture");
    sw62(0x01);
    TURN(0x01);                                      /* une impulsion, puis une bascule aller-retour en moins de 400 ms */
    sw62(0x03);
    sw62(0x01);
    TURN(0x01);
    CHECK(consigne(0x14F9), "une impulsion de chaque côté d'une bascule : le geste a été oublié à la bascule, rien");
    run_for(500 * MS);

    g_ps.m05[20] = 0x01;                             /* offset 19, bit 0 : l'objectif publie son ouverture, offsets 17-18 */
    g_ps.m05[18] = 0x00;
    g_ps.m05[19] = 0x14;
    run_for(20 * MS);                                /* le 0x05 suivant la porte */
    CHECK(consigne(0x14F9), "la première ouverture publiée, 0x1400, n'est pas prise : elle n'a pas changé");
    g_ps.m05[18] = 0x57;
    run_for(20 * MS);
    CHECK(consigne(0x1457), "elle change, 0x1457 : prise");
    TURN(0x01, 0x01);
    CHECK(consigne(0x1457), "dans la seconde qui suit, les impulsions attendent");
    run_for(1100 * MS);
    TURN(0x01, 0x01);
    CHECK(consigne(0x14A5), "plus d'une seconde sans changement : les impulsions reprennent, f/5,0");
}

/* Sans plage d'ouverture (une réponse au 0x08 de 5 octets, t_aperture_08 : tolérée, READY atteint), la bague ne pose rien :
 * CMD_APERTURE_SET y est refusée (E_NOCAP), et borner à une plage inconnue (0, 0) poserait le code 0.
 * La consigne reste celle du début de la session, f/1,8, B2 11. */
static void t_ring_no_range(void)
{
    printf("bague en rôle ouverture sans plage du 0x08 : ni impulsion ni ouverture publiée ne posent de consigne\n");
    boot();
    lens_135();
    ps_answer(0x08, (const uint8_t[]){0x08, 0x01, 0x12, 0x02, 0x19}, 5, 5 * MS);
    ps_presence(true);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY && status().ring == RING_APERTURE && !(status().capabilities & CAP_APERTURE),
          "READY, bague en ouverture, pas de plage");
    TURN(0x01, 0x01);
    CHECK(consigne(0x11B2), "deux impulsions : la consigne reste B2 11, pas 00 00");
    g_ps.m05[20] = 0x01;
    g_ps.m05[18] = 0x00;
    g_ps.m05[19] = 0x14;
    run_for(50 * MS);
    g_ps.m05[18] = 0x57;
    run_for(20 * MS);
    CHECK(consigne(0x11B2), "une ouverture publiée qui change : la consigne reste B2 11");
}

/* L'offset 60 est aussi l'octet de mouvement (motion.c) : il n'est pas compté pendant un mouvement de focus ; ni hors de
 * READY, où CMD_APERTURE_SET n'est pas servie (le homing bouge aussi). */
static void pulse_until_ready(const bsk_frame_t *f, uint64_t t)
{
    static unsigned k;
    (void)t;
    if (f->cls == 1 && f->msg[0] == 0x04)          /* 01 01 00, en boucle : des crans, l'offset 60 revenu à 0 entre eux */
        g_ps.m05[61] = status().session_state == SESSION_READY || ++k % 3 == 0 ? 0 : 0x01;
}

/* Après `q` (ABORTED) ou STALLED, aucune commande de mouvement n'est en vol, mais l'objectif peut encore publier son
 * mouvement à l'offset 60 ; il n'est pas compté avant d'être revenu à 0. Ici l'objectif publie 01 dès le goto, sans
 * interruption (sans source : un mouvement tel que MOUVEMENT le lit), et cinq trames encore après l'arrêt ; puis la bague
 * tournée, 01 01 : f/1,8 bornée à la plage, BF 11 (le cran le plus proche, B2 11, f/1,8) -> f/2,0, round(256 × (2·log2(2)
 * + 16)) = 4608 = 0x1200, à la main. */
static void t_ring_motion_tail(void)
{
    printf("bague en rôle ouverture : après q ou STALLED, l'offset 60 n'est compté qu'une fois revenu à 0\n");
    boot();
    lens_135();
    ps_presence(true);
    run_for(3000 * MS);
    g_ps.m05[61] = 0x01;                             /* l'objectif bouge, le répondeur garde sa position */
    (void)cmd(CMD_FOCUS_GOTO, 16623);
    run_for(100 * MS);
    CHECK(status().motion_state == MOTION_MOVING, "l'offset 60 non nul : MOVING");
    (void)cmd(CMD_FOCUS_STOP, 0);
    CHECK(status().motion_state == MOTION_ABORTED, "q : ABORTED, plus rien en vol");
    TURN(0x01, 0x01, 0x01, 0x01, 0x01);
    CHECK(consigne(0x11BF), "cinq trames 01 à la suite de l'arrêt : la consigne reste f/1,8, bornée à la plage, BF 11");
    TURN(0x01, 0x01);
    CHECK(consigne(0x1200), "l'offset 60 revenu à 0, puis 01 01 : f/2,0, 00 12 — la bague reprend");

    g_ps.on_send = NULL;
    g_ps.m05[61] = 0x01;
    (void)cmd(CMD_FOCUS_GOTO, 16623);
    run_for(10100 * MS);
    CHECK(status().motion_state == MOTION_STALLED, "en mouvement sans jamais s'immobiliser : STALLED à 10 s");
    TURN(0x01, 0x01, 0x01, 0x01, 0x01);
    CHECK(consigne(0x1200), "cinq trames 01 à la suite : la consigne reste f/2,0");
    TURN(0x01, 0x01);
    CHECK(consigne(0x1246), "revenu à 0, puis 01 01 : f/2,2, 46 12");
}

/* La bague recompte ses impulsions après la fin d'un goto ARRIVED, comme après q ou STALLED (t_ring_motion_tail). Le
 * moteur scripté porte 01 à l'offset 60 pendant le goto et trois trames après l'arrivée (hold) ; ARRIVED, la consigne
 * reste f/1,8 bornée à la plage, BF 11 ; l'offset 60 revenu à 0, puis 01 01 : f/2,0, 00 12. */
static void t_ring_after_goto(void)
{
    printf("bague en rôle ouverture : après un goto arrivé, l'offset 60 revenu à 0, les impulsions recomptées\n");
    ready_motor();
    M.move_byte = true;
    M.hold = 3;
    (void)cmd(CMD_FOCUS_GOTO, 16623);
    run_for(2000 * MS);
    CHECK(M.t_nz05 != 0 && M.t_zero05 > M.t_nz05 && status().motion_state == MOTION_ARRIVED && status().ring == RING_APERTURE &&
              consigne(0x11BF),
          "le goto arrivé, l'offset 60 revenu à 0, la bague en ouverture : la consigne est restée f/1,8, bornée, BF 11");
    TURN(0x01, 0x01);
    CHECK(consigne(0x1200), "01 01 après la fin du goto : f/2,0, 00 12 — la bague recompte");
    cap_elapsed();
    TURN(0x01, 0x01);
    CHECK(consigne(0x1246), "puis 01 01 : f/2,2, 46 12");
}

static void t_ring_motion(void)
{
    printf("bague en rôle ouverture : l'offset 60 pendant un goto, puis avant READY, ne touche pas l'ouverture\n");
    ready_motor();
    M.move_byte = true;
    (void)cmd(CMD_FOCUS_GOTO, 16623);
    run_for(2000 * MS);
    CHECK(M.t_nz05 != 0 && status().motion_state == MOTION_ARRIVED && status().ring == RING_APERTURE,
          "un goto dont le 0x05 porte 01 à l'offset 60, arrivé, bague en ouverture");
    CHECK(consigne(0x11BF), "la consigne est restée f/1,8, bornée à la plage, BF 11");

    boot();
    lens_135();
    g_ps.flow = true;                                /* le flux dès l'init : des 0x05 avant READY */
    g_ps.on_send = pulse_until_ready;
    ps_presence(true);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY && status().ring == RING_APERTURE && consigne(0x11BF),
          "des impulsions avant READY : la consigne est restée f/1,8, bornée à la plage, BF 11");
}

/* Les lignes `* lens`, `* ring`, `* btn`, `* mark` et `* restore` du journal depuis le dernier appel. */
static char JL[32][160];
static size_t n_jl;
static void jdrain(void)
{
    char b[400];
    uint8_t g;
    size_t n;
    n_jl = 0;
    while ((n = bsk_journal_take(b, sizeof b - 1, &g)) > 0) {
        b[n] = 0;
        if ((strncmp(b, "* lens ", 7) && strncmp(b, "* ring ", 7) && strncmp(b, "* btn ", 6) && strncmp(b, "* mark ", 7) &&
             strncmp(b, "* restore ", 10) && strncmp(b, "* refused ", 10)) ||
            n_jl == sizeof JL / sizeof JL[0])
            continue;
        snprintf(JL[n_jl++], sizeof JL[0], "%.150s", b);
    }
}

static size_t jcount(const char *sub)
{
    size_t k = 0;
    for (size_t i = 0; i < n_jl; i++) k += strstr(JL[i], sub) != NULL;
    return k;
}

/* Sous LOG ON : une ligne à chaque changement des offsets 62, 64, 66 du 0x05, valeur brute ; une ligne à chaque
 * changement de rôle ; aucune quand rien ne change, jamais une par trame. Une nouvelle session les redit. */
static void t_ring_journal(void)
{
    printf("bague : le journal, une ligne par changement des offsets 62, 64, 66 et du rôle, aucune sinon\n");
    boot();
    lens_135();
    ps_presence(true);
    run_for(3000 * MS);
    bsk_journal_init(ps_now);
    bsk_journal_set(true, false);
    jdrain();
    run_for(500 * MS);
    jdrain();
    CHECK(n_jl == 0, "30 trames sans changement : aucune ligne (%zu)", n_jl);
    sw62(0x03);
    run_for(100 * MS);
    jdrain();
    CHECK(n_jl == 2 && strstr(JL[0], " 0x05[62]=03") && strstr(JL[1], " mode=focus source=0x05"),
          "offset 62 à 03 : « * lens <t> 0x05[62]=03 » puis « * ring <t> mode=focus source=0x05 » (%zu : « %s »)", n_jl,
          n_jl ? JL[0] : "");
    g_ps.m05[65] = 0x10;
    run_for(100 * MS);
    jdrain();
    CHECK(n_jl == 1 && jcount(" 0x05[64]=10"), "offset 64 à 10 : une ligne (%zu)", n_jl);
    g_ps.m05[67] = 0x30;
    run_for(100 * MS);
    jdrain();
    CHECK(n_jl == 1 && jcount(" 0x05[66]=30"), "offset 66 à 30 : une ligne (%zu)", n_jl);
    (void)cmd(CMD_ATTACH, 0);
    run_for(3000 * MS);
    jdrain();
    CHECK(jcount(" 0x05[62]=03") == 1 && jcount(" 0x05[64]=10") == 1 && jcount(" 0x05[66]=30") == 1,
          "nouvelle session : chaque offset redit une fois (%zu lignes)", n_jl);
    bsk_journal_set(false, false);
}

/* À la fin de chaque geste en rôle ouverture, close par la pause de 400 ms ou par un changement de sens, une ligne
 * `* ring <t> gesture pulses=<n> thirds=<m>` : les impulsions signées, les tiers qui ont changé la consigne. Le 135 des
 * traces, sa plage BF 11 / 00 19 (SRC_R08). Codes calculés à la main, round(256 × (2·log2(f) + 16)) : f/5,6 -> 0x14F9 ;
 * f/6,3 -> 0x1550 ; f/7,1 -> 5543,8 -> 0x15A8 ; f/8 -> 5632 = 0x1600 ; f/5,0 -> 0x14A5. */
static void t_ring_gesture(void)
{
    uint32_t d0;
    printf("bague en rôle ouverture : une ligne `* ring <t> gesture` par geste, ses impulsions signées et ses tiers appliqués ; "
           "un changement de sens, deux lignes ; hors du plafond\n");
    boot();
    lens_135();
    ps_presence(true);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY && status().ring == RING_APERTURE, "READY, bague en ouverture");
    bsk_journal_init(ps_now);
    bsk_journal_set(true, false);
    (void)cmd(CMD_APERTURE_SET, 0x14F9);
    jdrain();

    TURN(0x01, 0x01, 0x01, 0x01, 0x01);
    CHECK(consigne(0x1550), "cinq impulsions 01 : un tiers, f/5,6 -> f/6,3, 50 15 ; le second, dû 2 trames après, perdu");
    jdrain();
    CHECK(n_jl == 0, "le geste n'est pas clos avant la pause : aucune ligne (%zu)", n_jl);
    run_for(400 * MS);
    jdrain();
    CHECK(n_jl == 1 && strstr(JL[0], "* ring ") == JL[0] && strstr(JL[0], " gesture pulses=5 thirds=1 frames=5 capped=1"),
          "la pause : « * ring <t> gesture pulses=5 thirds=1 frames=5 capped=1 » (%zu : « %s »)", n_jl, n_jl ? JL[0] : "");

    TURN(0x01, 0x01, 0x01, 0xFF, 0xFF);
    run_for(500 * MS);
    jdrain();
    CHECK(n_jl == 2 && strstr(JL[0], " gesture pulses=3 thirds=1 frames=3 capped=0") &&
              strstr(JL[1], " gesture pulses=-2 thirds=0 frames=2 capped=1"),
          "01 01 01 puis FF FF : deux lignes, pulses=3 thirds=1 frames=3 capped=0 puis pulses=-2 thirds=0 frames=2 capped=1, le "
          "cap court d'un geste à l'autre (%zu : « %s »)", n_jl, n_jl ? JL[0] : "");
    CHECK(consigne(0x15A8), "f/6,3 -> f/7,1, le tiers vers l'ouvert 3 trames après perdu : A8 15");

    (void)cmd(CMD_APERTURE_SET, 0x1900);
    TURN(0x01, 0x01);
    run_for(500 * MS);
    jdrain();
    CHECK(n_jl == 1 && strstr(JL[0], " gesture pulses=2 thirds=0 frames=2"),
          "au maximum de la plage, 01 01 : le tiers ne change pas la consigne, pulses=2 thirds=0 frames=2 (« %s »)",
          n_jl ? JL[0] : "");

    (void)cmd(CMD_APERTURE_SET, 0x14F9);
    TURN(0x01);                                      /* une impulsion, puis une bascule aller-retour en moins de 400 ms */
    sw62(0x03);
    sw62(0x01);
    TURN(0x01);
    run_for(500 * MS);
    jdrain();
    CHECK(jcount(" gesture ") == 1 && jcount(" gesture pulses=1 thirds=0 frames=1") == 1,
          "une impulsion de chaque côté d'une bascule : le premier geste oublié sans ligne, pulses=1 thirds=0 (%zu)", n_jl);

    bsk_journal_set(true, true);                     /* LOG ALL : les trames ne prennent pas la fenêtre du plafond */
    jdrain();
    d0 = bsk_journal_dropped();
    TURN(0xFF, 0xFF);
    run_for(500 * MS);
    jdrain();
    CHECK(consigne(0x14A5) && jcount(" gesture pulses=-2 thirds=1 frames=2") == 1 && bsk_journal_dropped() == d0,
          "LOG ALL, rien de tu par le plafond : la ligne du geste sort, pulses=-2 thirds=1, f/5,0 (%zu)", n_jl);
    bsk_journal_set(false, false);
}

/* La bague compte la valeur signée de l'offset 60 de chaque 0x05 (un compte de fronts, comme le Tamron F051), le filtre
 * dans l'unité de la somme : deux unités de même sens par tiers, le reste gardé. Codes calculés à la main
 * (t_ring_gesture) : f/5,6 -> 0x14F9 ; f/6,3 -> 0x1550 ; f/5,0 -> 0x14A5. */
static void t_ring_sum(void)
{
    printf("bague en rôle ouverture : un 0x05 à 02 compte pour 2, un à FD pour -3, le reste gardé ; la ligne dit ses 0x05 non "
           "nuls\n");
    boot();
    lens_135();
    ps_presence(true);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY && status().ring == RING_APERTURE, "READY, bague en ouverture");
    bsk_journal_init(ps_now);
    bsk_journal_set(true, false);
    (void)cmd(CMD_APERTURE_SET, 0x14F9);
    jdrain();
    TURN(0x02);
    CHECK(consigne(0x1550), "un 0x05 à 02 : deux unités, un tiers, f/5,6 -> f/6,3, 50 15");
    run_for(500 * MS);
    jdrain();
    CHECK(n_jl == 1 && strstr(JL[0], " gesture pulses=2 thirds=1 frames=1"),
          "la ligne : pulses=2 thirds=1 frames=1 (« %s »)", n_jl ? JL[0] : "");
    TURN(0xFD);
    CHECK(consigne(0x14F9), "un 0x05 à FD : trois unités vers l'ouvert, un tiers, f/6,3 -> f/5,6, une unité gardée");
    run_for(250 * MS);                               /* le FF ~370 ms après le FD, cap échu, pause pas atteinte */
    TURN(0xFF);
    CHECK(consigne(0x14A5), "puis FF : avec l'unité gardée, un second tiers, f/5,0, A5 14");
    run_for(500 * MS);
    jdrain();
    CHECK(n_jl == 1 && strstr(JL[0], " gesture pulses=-4 thirds=2 frames=2 capped=0"),
          "la ligne : pulses=-4 thirds=2 frames=2 capped=0 (« %s »)", n_jl ? JL[0] : "");
    TURN(0x05);
    CHECK(consigne(0x14F9), "un 0x05 à 05 : deux tiers dus, un appliqué, f/5,0 -> f/5,6, F9 14 ; l'autre perdu");
    run_for(500 * MS);
    jdrain();
    CHECK(n_jl == 1 && strstr(JL[0], " gesture pulses=5 thirds=1 frames=1 capped=1"),
          "la ligne : pulses=5 thirds=1 frames=1 capped=1 (« %s »)", n_jl ? JL[0] : "");
    bsk_journal_set(false, false);
}

/* La bague tournée longtemps : `v` à l'offset 60 d'un 0x05 sur `every`, `n` 0x05 de suite (un par 0x04 émis, la VD à
 * 60 Hz : 16666 µs), puis 0 ; `last` : l'émission du 0x04 dont le 0x05 porte le dernier non nul. */
static struct {
    uint8_t  v;
    unsigned every, n, i;
    uint64_t last;
} SPIN;

static void spin_hook(const bsk_frame_t *f, uint64_t t)
{
    if (f->cls != 1 || f->msg[0] != 0x04) return;
    g_ps.m05[61] = SPIN.i < SPIN.n && SPIN.i % SPIN.every == 0 ? SPIN.v : 0;
    if (g_ps.m05[61]) SPIN.last = t;
    SPIN.i++;
}

static void spin(uint8_t v, unsigned every, unsigned n)
{
    SPIN.v = v;
    SPIN.every = every;
    SPIN.n = n;
    SPIN.i = 0;
    g_ps.on_send = spin_hook;
    run_for((n + 3) * 17 * MS);
}

/* Les changements de la consigne d'ouverture dans les 0x03 émis depuis `from` (offsets 3-4) : leur code et leur instant. */
static uint16_t CH_CODE[64];
static uint64_t CH_T[64];
static size_t ap03_changes(size_t from, uint16_t code0)
{
    size_t n = 0;
    uint16_t prev = code0;
    for (size_t i = from; i < g_ps_n && i < PS_LOG_CAP; i++) {
        const bsk_frame_t *f = &g_ps_log[i].frame;
        uint16_t c;
        if (g_ps_log[i].kind != PS_SEND || f->msg[0] != 0x03) continue;
        c = (uint16_t)(f->msg[4] | f->msg[5] << 8);
        if (c == prev) continue;
        prev = c;
        if (n < 64) {
            CH_CODE[n] = c;
            CH_T[n] = g_ps_log[i].t;
        }
        n++;
    }
    return n;
}

/* Deux changements successifs à 1/3 s au moins : 333334 µs, 1/3 s arrondi au-dessus (écrit à la main). */
static bool spaced(size_t n)
{
    for (size_t i = 1; i < n && i < 64; i++)
        if (CH_T[i] - CH_T[i - 1] < 333334u) {
            printf("  changements %zu et %zu à %llu µs\n", i - 1, i, (unsigned long long)(CH_T[i] - CH_T[i - 1]));
            return false;
        }
    return true;
}

/* Décision de l'humain : la bague n'applique pas plus de 3 tiers par seconde, l'excédent perdu et compté (`capped=`),
 * rien sur un 0x05 dont l'offset 60 est nul ; ni CMD_APERTURE_SET ni l'ouverture publiée ne passent par le cap. Le 135
 * des traces, sa plage BF 11 / 00 19. Oracle écrit à la main, la VD à 60 Hz (16666 µs) :
 *   - vite, 01 sur 120 0x05 de suite (2 s) : un tiers dû aux 0x05 pairs k = 2, 4, …, 120 (2 unités par tiers) ; appliqués
 *     k = 2, puis le premier pair à 1/3 s au moins, (k − 2) × 16666 ≥ 333334, soit 22 trames plus loin (20 font 333320 µs) :
 *     k = 2, 24, 46, 68, 90, 112, six tiers, 54 perdus ; f/5,6 -> f/6,3, 7,1, 8, 9, 10, 11 : 0x1550, 0x15A8, 0x1600, 0x1657,
 *     0x16A5, 0x16EB (round(256 × (2·log2(f) + 16)) : f/9 -> 5719, f/10 -> 5797, f/11 -> 5867) ;
 *   - puis un 01 seul, ~350 ms après le dernier (dans le geste, cap échu) : la somme ne garde rien des tiers perdus, rien ;
 *   - lent, 01 toutes les 12 trames (199992 µs), 10 fois : un tiers toutes les 24 trames (399984 µs), cinq, aucun perdu ;
 *     f/5,6 -> 0x16A5 ;
 *   - 04 04 (un compte de fronts, le Tamron) : quatre tiers dus en 17 ms, un appliqué, trois perdus, f/5,6 -> f/6,3. */
static void t_ring_cap(void)
{
    static const uint16_t FAST[] = {0x1550, 0x15A8, 0x1600, 0x1657, 0x16A5, 0x16EB};
    size_t from, n;
    bool same;
    printf("bague en rôle ouverture : 3 tiers par seconde au plus, l'excédent perdu et compté, rien après l'arrêt ; ni a<f> ni "
           "l'ouverture publiée ne passent par le cap\n");
    boot();
    lens_135();
    ps_presence(true);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY && status().ring == RING_APERTURE, "READY, bague en ouverture");
    bsk_journal_init(ps_now);
    bsk_journal_set(true, false);
    (void)cmd(CMD_APERTURE_SET, 0x14F9);
    run_for(500 * MS);
    jdrain();

    from = g_ps_n;
    spin(0x01, 1, 120);
    run_for(280 * MS);
    n = ap03_changes(from, 0x14F9);
    same = n == 6;
    for (size_t i = 0; same && i < 6; i++) same = CH_CODE[i] == FAST[i];
    CHECK(same, "vite, 120 0x05 à 01 : six tiers, f/5,6 -> f/11, 50 15 … EB 16 (%zu changements, le dernier %04X)", n,
          n && n <= 64 ? CH_CODE[n - 1] : 0);
    CHECK(spaced(n), "... espacés d'au moins 1/3 s");
    CHECK(n >= 1 && n <= 64 && CH_T[n - 1] <= SPIN.last + 16666u,
          "l'arrêt net : aucun tiers après le 0x03 qui suit le dernier 0x05 non nul");
    from = g_ps_n;
    TURN(0x01);
    CHECK(ap03_changes(from, 0x16EB) == 0 && consigne(0x16EB), "un 01 seul, dans le geste : les tiers perdus ne sont pas "
                                                               "différés, rien ; f/11, EB 16");
    run_for(500 * MS);
    jdrain();
    CHECK(n_jl == 1 && strstr(JL[0], " gesture pulses=121 thirds=6 frames=121 capped=54"),
          "la ligne : pulses=121 thirds=6 frames=121 capped=54 (« %s »)", n_jl ? JL[0] : "");

    (void)cmd(CMD_APERTURE_SET, 0x14F9);
    run_for(500 * MS);
    jdrain();
    from = g_ps_n;
    spin(0x01, 12, 109);
    run_for(500 * MS);
    n = ap03_changes(from, 0x14F9);
    CHECK(n == 5 && spaced(n) && CH_CODE[4] == 0x16A5,
          "lent, un 01 toutes les 200 ms, dix : cinq tiers, f/5,6 -> f/10, A5 16, le cap n'agit pas (%zu changements)", n);
    jdrain();
    CHECK(n_jl == 1 && strstr(JL[0], " gesture pulses=10 thirds=5 frames=10 capped=0"),
          "la ligne : pulses=10 thirds=5 frames=10 capped=0 (« %s »)", n_jl ? JL[0] : "");

    (void)cmd(CMD_APERTURE_SET, 0x14F9);
    run_for(500 * MS);
    jdrain();
    TURN(0x04, 0x04);
    CHECK(consigne(0x1550), "04 04, le Tamron : quatre tiers dus en une trame, un appliqué, f/5,6 -> f/6,3, 50 15");
    run_for(500 * MS);
    jdrain();
    CHECK(n_jl == 1 && strstr(JL[0], " gesture pulses=8 thirds=1 frames=2 capped=3"),
          "la ligne : pulses=8 thirds=1 frames=2 capped=3 (« %s »)", n_jl ? JL[0] : "");

    TURN(0x04);                                      /* un tiers perdu, puis une bascule aller-retour : le geste oublié */
    sw62(0x03);
    sw62(0x01);
    TURN(0x01);
    run_for(500 * MS);
    jdrain();
    CHECK(jcount(" gesture ") == 1 && jcount(" gesture pulses=1 thirds=0 frames=1 capped=0") == 1,
          "les tiers perdus oubliés avec le geste à la bascule : pulses=1 thirds=0 frames=1 capped=0 (%zu)", n_jl);

    TURN(0x02);
    (void)cmd(CMD_APERTURE_SET, 0x1600);
    CHECK(consigne(0x1600), "a<f> dans le tiers de seconde d'un tiers de la bague : servie, f/8, 00 16");
    g_ps.m05[20] = 0x01;                             /* offset 19, bit 0 : l'objectif publie son ouverture, offsets 17-18 */
    g_ps.m05[18] = 0x00;
    g_ps.m05[19] = 0x14;
    run_for(20 * MS);
    g_ps.m05[18] = 0x57;
    run_for(20 * MS);
    CHECK(consigne(0x1457), "l'ouverture publiée qui change, dans le tiers de seconde d'un tiers de la bague : prise, 0x1457");
    g_ps.m05[20] = 0x00;
    bsk_journal_set(false, false);
}

/* Les offsets 32 à 38 du 0x06 que le flux émet (g_ps.m06[33..39] : l'offset k est msg[k + 1]), une ligne de `row` par
 * 0x06, celui de la paire de chaque 0x04 émis, puis 0. */
static struct {
    const uint8_t (*row)[7];
    size_t n, i;
} D6;

static void d06_hook(const bsk_frame_t *f, uint64_t t)
{
    (void)t;
    if (f->cls != 1 || f->msg[0] != 0x04) return;
    if (D6.i < D6.n) memcpy(&g_ps.m06[33], D6.row[D6.i++], 7);
    else memset(&g_ps.m06[33], 0, 7);
}

static void d06_play(const uint8_t (*row)[7], size_t n)
{
    D6.row = row;
    D6.n = n;
    D6.i = 0;
    g_ps.on_send = d06_hook;
    run_for((n + 3) * 17 * MS);
}

/* Les offsets 32 à 38 des 60 0x06 que le Sony FE 24-105 G a émis de 33 914 à 34 897 ms pendant qu'on tournait sa bague en
 * position AF (une capture LOG ALL de l'humain, non publiée : 7_Docs/E-Mount/provenance.md § 3 ; une ligne sur quatre, chaque ligne
 * en différence reconstruite par la règle de PROTOCOL.md § « LOG ALL ») ; son offset 60 reste à 0. */
static const uint8_t SONY_RING[60][7] = {
    {0x00, 0x00, 0x00, 0x00, 0x01, 0xFF, 0x01}, {0x01, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x01, 0xFF, 0x00, 0x00, 0x00},   /* r0 */
    {0x01, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x01, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00},   /* r4 */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00}, {0x01, 0xFF, 0x00, 0x01, 0xFF, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   /* r8 */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x01, 0xFF, 0x00, 0x00, 0x00}, {0x00, 0x01, 0xFF, 0x00, 0x01, 0xFF, 0x00}, {0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   /* r12 */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   /* r16 */
    {0x00, 0x00, 0x00, 0x00, 0x01, 0xFF, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01},   /* r20 */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x01, 0xFF, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   /* r24 */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x01, 0xFF, 0x00, 0x01, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xFF}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   /* r28 */
    {0x00, 0x01, 0x00, 0x00, 0xFF, 0x00, 0x01}, {0x00, 0x00, 0x00, 0x01, 0xFF, 0x00, 0x00}, {0x00, 0x01, 0x00, 0xFF, 0x01, 0xFF, 0x00}, {0x00, 0x00, 0x01, 0x00, 0x00, 0xFF, 0x00},   /* r32 */
    {0xFF, 0x00, 0x01, 0xFF, 0x01, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0xFF, 0x01, 0xFF, 0x01, 0x00, 0x00, 0x00}, {0x01, 0xFF, 0x01, 0xFF, 0x00, 0x01, 0x00},   /* r36 */
    {0x00, 0x01, 0xFF, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x01, 0xFF, 0x01}, {0x00, 0x00, 0x01, 0x00, 0xFF, 0x00, 0x00}, {0x00, 0x01, 0xFF, 0x01, 0xFF, 0x00, 0x01},   /* r40 */
    {0x00, 0x00, 0x01, 0xFF, 0x00, 0x01, 0x00}, {0xFF, 0x00, 0x01, 0xFF, 0x00, 0x00, 0x00}, {0x01, 0xFF, 0x00, 0x01, 0x00, 0xFF, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   /* r44 */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x01, 0x00, 0xFF, 0x00},   /* r48 */
    {0x00, 0x01, 0xFF, 0x00, 0x00, 0x00, 0x00}, {0xFF, 0x00, 0x01, 0xFF, 0x00, 0x01, 0xFF}, {0x00, 0x01, 0xFF, 0x01, 0x00, 0xFF, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   /* r52 */
    {0x00, 0x00, 0x00, 0x01, 0x00, 0xFF, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x01, 0x00, 0xFF, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   /* r56 */
};

/* Un 0x06 dont les offsets 32 à 38 ne sont pas nuls ne change jamais l'ouverture. Ces sept octets sont la trace de vitesse
 * de la position mesurée du focus (weiziqian, https://github.com/weiziqian/E-mount-protocol-RE, docs/msg_0x06.md ;
 * le 135 les rend non nuls pendant un mouvement, samyang.md § 4.8) : le Sony les rend non nuls quand sa position mesurée tremble autour de sa consigne, la bague
 * tournée en AF ne déplaçant pas le focus. Lus comme la bague, ces 60 0x06 feraient trois tiers et quatre gestes. Attendu
 * ici : aucun changement de consigne, aucune ligne de geste, ni sur eux ni sur deux 0x06 de somme forte (00 E0 E0 E2 FD 00
 * 00 : une capture du 135, non publiée, l'accusé 1D 00 du 135 ; sept 01 : valeur de test). Puis la bague tournée par l'offset
 * 60, 01 01 : un tiers, f/5,6 -> f/6,3, 0x1550 (codes de t_ring_gesture, calculés à la main) — la bague est bien vivante
 * dans cette session. */
static void t_ring_sony06(void)
{
    static const uint8_t STRONG[2][7] = {{0x00, 0xE0, 0xE0, 0xE2, 0xFD, 0x00, 0x00}, {0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01}};
    size_t from, n;
    printf("bague en rôle ouverture : les offsets 32 à 38 du 0x06 (la trace de vitesse du focus) ne touchent jamais "
           "l'ouverture, les 0x06 de la capture du Sony FE 24-105 G rejoués\n");
    boot();
    sony(true);
    ps_presence(true);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY && status().ring == RING_APERTURE && g_ps.m05[61] == 0 && g_ps.n06 == 40,
          "READY, bague en ouverture, l'offset 60 du 0x05 à 0, un 0x06 de 40 octets");
    bsk_journal_init(ps_now);
    bsk_journal_set(true, false);
    (void)cmd(CMD_APERTURE_SET, 0x14F9);
    run_for(500 * MS);
    jdrain();

    from = g_ps_n;
    d06_play(SONY_RING, 60);
    d06_play(STRONG, 2);
    run_for(500 * MS);
    n = ap03_changes(from, 0x14F9);
    CHECK(n == 0 && consigne(0x14F9), "les 60 0x06 du Sony, puis deux de somme forte : la consigne reste f/5,6, F9 14 (%zu "
                                      "changements, le premier %04X)", n, n ? CH_CODE[0] : 0);
    jdrain();
    CHECK(jcount(" gesture ") == 0, "aucune ligne de geste (%zu : « %s »)", n_jl, n_jl ? JL[0] : "");

    TURN(0x01, 0x01);
    CHECK(consigne(0x1550), "puis l'offset 60, 01 01 : un tiers, f/6,3, 50 15");
    run_for(500 * MS);
    jdrain();
    CHECK(n_jl == 1 && strstr(JL[0], " gesture pulses=2 thirds=1 frames=2 capped=0"),
          "sa ligne : pulses=2 thirds=1 frames=2 capped=0 (« %s »)", n_jl ? JL[0] : "");
    bsk_journal_set(false, false);
}

/* Les lignes ` step=08 ` du journal depuis le dernier appel ; la première dans `first`. */
static size_t step08(char *first, size_t cap)
{
    char b[400];
    uint8_t g;
    size_t n, k = 0;
    first[0] = 0;
    while ((n = bsk_journal_take(b, sizeof b - 1, &g)) > 0) {
        b[n] = 0;
        if (!strstr(b, " step=08 ")) continue;
        if (!k++) snprintf(first, cap, "%.150s", b);
    }
    return k;
}

/* Le 0x08 de l'init au journal (LOG ON), une ligne par init, son octet 0 et la reconnaissance Samyang à son envoi. Le 135
 * des traces, reconnu par son LensType2 au 0x07 (8, dans la liste des Samyang) : 06, yes ; le Sony des captures,
 * LensType2 hors de la liste et sans 0x3F : 00, no. Puis une nouvelle session : sa ligne. */
static void t_body08_journal(void)
{
    char l[160];
    size_t n;
    printf("init : le drapeau du 0x08 et la reconnaissance Samyang au journal, une ligne par init\n");
    boot();
    lens_135();
    bsk_journal_init(ps_now);
    bsk_journal_set(true, false);
    (void)step08(l, sizeof l);
    ps_presence(true);
    run_for(3000 * MS);
    n = step08(l, sizeof l);
    CHECK(status().session_state == SESSION_READY && n == 1 && !strncmp(l, "* std ", 6) &&
              !strcmp(strstr(l, " step=08 "), " step=08 body_flags=06 samyang=yes"),
          "le 135 reconnu : une ligne « * std <t> step=08 body_flags=06 samyang=yes » (%zu : « %s »)", n, l);
    (void)cmd(CMD_ATTACH, 0);
    run_for(3000 * MS);
    n = step08(l, sizeof l);
    CHECK(n == 1 && strstr(l, " step=08 body_flags=06 samyang=yes"), "une nouvelle session, une nouvelle init : sa ligne (%zu)", n);

    boot();
    sony(true);
    bsk_journal_init(ps_now);
    bsk_journal_set(true, false);
    (void)step08(l, sizeof l);
    ps_presence(true);
    run_for(3000 * MS);
    n = step08(l, sizeof l);
    CHECK(status().session_state == SESSION_READY && n == 1 && strstr(l, " step=08 body_flags=00 samyang=no"),
          "le Sony : une ligne « * std <t> step=08 body_flags=00 samyang=no » (%zu : « %s »)", n, l);
    bsk_journal_set(false, false);
}

/* CMD_CLEAR_FAULT : OFF, puis POWERING au même instant, D2 présent, sans autre événement pour réveiller
 * la boucle (VD arrêtée par OFF) : bsk_session_next le dit. */
static void t_clear_fault_now(void)
{
    uint64_t tc;
    uint32_t q;
    printf("CMD_CLEAR_FAULT en FAULT : OFF, ACK_COMPLETED, puis POWERING au même instant (bsk_session_next)\n");
    boot();
    ps_presence(true);
    run_for(30000 * MS);
    CHECK(status().session_state == SESSION_FAULT, "objectif muet : FAULT");
    tc = ps_now();
    q = cmd(CMD_CLEAR_FAULT, 0);
    CHECK(final_is(q, ACK_COMPLETED, E_OK, NULL) && status().session_state == SESSION_OFF, "OFF, ACK_COMPLETED");
    CHECK(bsk_session_next() == tc, "quelque chose à faire tout de suite");
    run_for(1000 * MS);
    CHECK(entered(SESSION_POWERING, chg_index_after(tc)) == tc, "POWERING au même instant");
}

/* ─────────────────────────── la marque et le bouton du fût ─────────────────────────── */

/* Les clés de la marque (les octets 1-2 et 10-11 du 0x07, l'octet 1 de la réponse au 0x08, type compris, puis la
 * focale nominale du 0x05, offsets 26-27, (f + 5) / 10 mm sur trois chiffres hexa), calculées à la main :
 *   - le 135 des traces : 0x07 de full:12, 07 01 03 70 01 00 01 05 00 00 08 00 : 01 03, 08 00 ; réponse au 0x08 de full:15,
 *     08 BF 11 : BF ; 0x05 de dump05:7, offsets 26-27 46 05 : 1350, 135 mm, 087 -> 01030800bf087 ;
 *   - le Sony des captures : 0x07 de 17-44-34-484Z.txt:19-20, 07 01 07 60 01 00 03 01 17 A0 25 80 : 01 07, 25 80 ; réponse
 *     au 0x08 de :26-30, 08 05 14 : 05 ; 0x05 de 17-45-01-034Z.txt:65-66, offsets 26-27 F0 00 : 240, 24 mm, 018 ->
 *     0107258005018. */
#define KEY135  "01030800bf087"
#define KEYSONY "0107258005018"

/* Le bouton du fût : l'offset 64 du 0x05 du flux (m05[65]), bit 3 (195_samyang.txt:865, :875) ; le 0x05 suivant le porte. */
static void btn(bool down)
{
    if (down) g_ps.m05[65] |= 0x08;
    else g_ps.m05[65] &= (uint8_t)~0x08;
}

/* Un appui de `ms` millisecondes, puis 50 ms. */
static void press_for(uint32_t ms)
{
    btn(true);
    run_for((uint64_t)ms * MS);
    btn(false);
    run_for(50 * MS);
}

static size_t board_acks(void)
{
    size_t n = 0;
    for (size_t i = 0; i < n_acks; i++) n += ACKS[i].a.seq == BSK_SEQ_BOARD;
    return n;
}

/* La marque de la session, sens compris (mark.h) ; `ok` faux sans marque. */
static bsk_mark_t mark_now(bool *ok)
{
    bsk_mark_t m = {0};
    *ok = mark_get(&m);
    return m;
}

/* La marque posée à n, CMD_SET_MARK n, qu'aucune ligne ne dépose (`m<n>` est un déplacement), déposée directement ;
 * E_OK : servie (ACK_ACCEPTED puis ACK_COMPLETED), sinon refusée pour `why`. */
static bool mark_at(int32_t n, bsk_err_t why)
{
    uint32_t q = cmd(CMD_SET_MARK, n);
    return why == E_OK ? acks_are(q, 2, ACK_COMPLETED, E_OK) : acks_are(q, 1, ACK_REJECTED, why);
}

#define MARK_AT(n, why) CHECK(mark_at(n, why), "CMD_SET_MARK %d : %s attendu", n, (why) == E_OK ? "servie" : bsk_err_token(why))

/* j, js, jg, jx à travers HOTE (PROTOCOL.md, ligne `j`), CMD_SET_MARK n déposée directement ; les bornes du 0x06 du 135,
 * 13873 / 30738, resserrées de 5 ; la file du magasin pleine (er busy, rien de changé) ; une écriture ratée (acceptée,
 * puis la clé relue : ce qui est rangé) ; la marque rangée survit à la session. */
static void t_mark_letters(void)
{
    char r[BSK_HOST_REPLY_MAX];
    uint32_t v = 0, w0;
    printf("marque : j, js, jg, jx à travers HOTE, CMD_SET_MARK n ; la file pleine, une écriture ratée\n");
    ready_motor();
    bsk_host_init("2.0.0-dev", "poweron");
    bsk_journal_init(ps_now);
    bsk_journal_set(true, false);
    jdrain();
    HOST("j", "-");
    HOST("jg", "er range nomark");
    HOST("js", "ok");
    HOST("j", "14623");
    CHECK(store_sim_peek(KEY135, &v) && v == 14623, "js : 14623 rangé sous " KEY135 ", sens inconnu, aucun changement de "
          "position vu (0x%X)", v);
    jdrain();
    CHECK(n_jl == 1 && strstr(JL[0], " set key=" KEY135 " pos=14623 dir=unknown"), "* mark <t> set key=… pos=14623 "
          "dir=unknown (%zu : « %s »)", n_jl, n_jl ? JL[0] : "");
    MARK_AT(16340, E_OK);
    HOST("j", "16340");
    MARK_AT(13877, E_LIMIT);
    MARK_AT(30734, E_LIMIT);
    HOST("j", "16340");
    MARK_AT(13878, E_OK);
    MARK_AT(30733, E_OK);
    MARK_AT(16340, E_OK);
    HOST("jg", "ok");
    run_for(2000 * MS);
    CHECK(status().motion_state == MOTION_ARRIVED && status().focus_position == 16340, "jg : arrivé en 16340");
    jdrain();
    HOST("jx", "ok");
    HOST("j", "-");
    CHECK(!store_sim_peek(KEY135, &v), "jx : effacée du magasin");
    HOST("jx", "ok");
    HOST("jg", "er range nomark");
    jdrain();
    CHECK(jcount(" clear key=" KEY135) == 2, "* mark <t> clear key=… deux fois (%zu)", jcount(" clear key="));

    MARK_AT(16000, E_OK);
    g_store_sim.refuse = true;
    w0 = g_store_sim.writes;
    HOST("js", "er busy");
    MARK_AT(17000, E_BUSY);
    HOST("jx", "er busy");
    HOST("j", "16000");
    CHECK(g_store_sim.writes == w0, "la file pleine : rien de déposé");
    g_store_sim.refuse = false;

    g_store_sim.fail = true;
    jdrain();
    MARK_AT(17000, E_OK);
    HOST("j", "16000");
    jdrain();
    CHECK(jcount(" store=er key=" KEY135) == 1, "* mark <t> store=er key=… (%zu)", n_jl);
    g_store_sim.fail = false;

    (void)cmd(CMD_ATTACH, 0);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY, "nouvelle session : READY");
    HOST("j", "16000");
    bsk_journal_set(false, false);
}

/* La clé de la marque, calculée à la main (KEY135, KEYSONY) ; un 0x07 de 11 octets (son octet 3, puis 0), une
 * réponse au 0x08 de 3 octets (son octet 1), sans réponse au 0x08 (0) ; un 0x07 de 3 octets : pas d'identité, pas de
 * marque. */
static void t_mark_key(void)
{
    char r[BSK_HOST_REPLY_MAX];
    uint8_t m07[BSK_MSG_MAX];
    size_t n07 = sources135_msg(SRC_R07, m07, sizeof m07);
    uint32_t v = 0;
    printf("marque : la clé de la v1, à la main pour le 135 et le Sony, 0x07 et 0x08 courts\n");
    ready_motor();
    bsk_host_init("2.0.0-dev", "poweron");
    HOST("js", "ok");
    CHECK(g_store_sim.writes == 1 && store_sim_peek(KEY135, &v) && v == 14623, "le 135 : " KEY135 " -> 14623 (0x%X)", v);

    boot();
    sony(true);
    bsk_host_init("2.0.0-dev", "poweron");
    ps_presence(true);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY, "le Sony : READY");
    HOST("js", "ok");
    CHECK(g_store_sim.writes == 1 && store_sim_peek(KEYSONY, &v) && v == 16334, "le Sony : " KEYSONY " -> 16334, la position "
          "de son 0x06 (CE 3F) (0x%X)", v);

    boot();
    lens_135();
    ps_answer(0x07, m07, 11, 5 * MS);
    ps_answer(0x08, (const uint8_t[]){0x08, 0xBF, 0x11}, 3, 5 * MS);
    g_ps.flow = true;
    ps_presence(true);
    run_for(3000 * MS);
    HOST("js", "ok");
    CHECK(status().session_state == SESSION_READY && store_sim_peek("01037000bf087", &v),
          "0x07 de 11 octets : son octet 3, 70, puis 00 ; 0x08 de 3 octets : BF -> 01037000bf087");

    boot();
    lens_135();
    no_answer(0x08);
    g_ps.flow = true;
    ps_presence(true);
    run_for(3000 * MS);
    HOST("js", "ok");
    CHECK(status().session_state == SESSION_READY && store_sim_peek("0103080000087", &v), "sans réponse au 0x08 : 00 -> "
          "0103080000087");

    boot();
    lens_135();
    ps_answer(0x07, m07, 3, 5 * MS);
    g_ps.flow = true;
    ps_presence(true);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY && n07 >= 12, "0x07 de 3 octets : READY");
    HOST("j", "-");
    HOST("js", "er nocap");
    HOST("jx", "er nocap");
    CHECK(g_store_sim.requests == 0, "sans identité, rien n'est demandé au magasin");
}

/* Une valeur sans sens (la position seule, bits 16-23 nuls) se relit, sens inconnu ; une valeur qui porte son sens
 * (bits 16-23) ; un sens invalide se relit inconnu, la marque gardée ; une valeur dont les bits 24-31 ne sont pas nuls
 * est ignorée. Relue à chaque session. */
static void t_mark_v1(void)
{
    char r[BSK_HOST_REPLY_MAX];
    bsk_mark_t m;
    bool ok;
    printf("marque : une valeur de la v1 relue, sens inconnu ; le sens rangé ; un sens invalide lu inconnu ; "
           "une valeur invalide ignorée\n");
    boot();
    store_sim_poke(KEY135, 15000);
    lens_135();
    g_ps.flow = true;
    ps_presence(true);
    run_for(6000 * MS);                             /* RESTORING vers 15000, sans moteur : STALLED, READY */
    m = mark_now(&ok);
    CHECK(status().session_state == SESSION_READY && status().mark_valid && status().mark_position == 15000 && ok &&
              m.approach_dir == BSK_APPROACH_UNKNOWN,
          "15000, rangé par la v1 : marque 15000, sens inconnu");
    store_sim_poke(KEY135, 15000u | 2u << 16);
    (void)cmd(CMD_ATTACH, 0);
    run_for(6000 * MS);
    m = mark_now(&ok);
    CHECK(ok && m.position == 15000 && m.approach_dir == BSK_APPROACH_DECREASING, "0x23A98 : 15000, décroissant");
    store_sim_poke(KEY135, 15000u | 3u << 16);
    (void)cmd(CMD_ATTACH, 0);
    run_for(6000 * MS);
    m = mark_now(&ok);
    CHECK(status().session_state == SESSION_READY && status().mark_valid && status().mark_position == 15000 && ok &&
              m.position == 15000 && m.approach_dir == BSK_APPROACH_UNKNOWN,
          "0x33A98 : un sens invalide, la marque 15000, sens inconnu");
    store_sim_poke(KEY135, 15000u | 5u << 16);
    (void)cmd(CMD_ATTACH, 0);
    run_for(6000 * MS);
    m = mark_now(&ok);
    bsk_host_init("2.0.0-dev", "poweron");
    HOST("j", "15000");
    CHECK(ok && m.approach_dir == BSK_APPROACH_UNKNOWN, "0x53A98 : m -> 15000, sens inconnu");
    store_sim_poke(KEY135, 15000u | 1u << 24);
    (void)cmd(CMD_ATTACH, 0);
    run_for(6000 * MS);
    CHECK(status().session_state == SESSION_READY && !status().mark_valid, "0x1003A98 : ignorée");
}

/* La focale nominale (offsets 26-27, m05[27-28]) est dans la clé, arrondie au mm : 1345 à 1354 font 135 mm ; la focale
 * courante (offsets 24-25) n'y est pas ; l'identité (le 0x07) y est. */
static void focal(uint16_t nom)
{
    g_ps.m05[27] = (uint8_t)nom;
    g_ps.m05[28] = (uint8_t)(nom >> 8);
    run_for(50 * MS);
}

static void t_mark_focal(void)
{
    char r[BSK_HOST_REPLY_MAX];
    printf("marque : par focale nominale arrondie au mm, par objectif ; la focale courante n'y est pas\n");
    ready_motor();
    bsk_host_init("2.0.0-dev", "poweron");
    MARK_AT(16340, E_OK);
    focal(1354);
    HOST("j", "16340");
    focal(1345);
    HOST("j", "16340");
    focal(1344);
    HOST("j", "-");
    MARK_AT(15000, E_OK);
    focal(1355);
    HOST("j", "-");
    focal(1350);
    HOST("j", "16340");
    g_ps.m05[25] = 0x00;                           /* la focale courante, offsets 24-25 : 0x0C00 */
    g_ps.m05[26] = 0x0C;
    run_for(50 * MS);
    HOST("j", "16340");
    focal(1340);
    HOST("j", "15000");
    focal(1350);

    ps_answer(0x07, TAMRON07, sizeof TAMRON07, 5 * MS);   /* un autre objectif : 01 03 34 C1 */
    (void)cmd(CMD_ATTACH, 0);
    run_for(6000 * MS);
    CHECK(status().session_state == SESSION_READY, "l'identité du F051 : READY");
    HOST("j", "-");
    answer_src(0x07, SRC_R07, 5 * MS);
    (void)cmd(CMD_ATTACH, 0);
    run_for(6000 * MS);
    HOST("j", "16340");
}

/* Le sens de la marque : celui du dernier changement de position vu au 0x06 avant la pose, goto ou bague ; CMD_SET_MARK n :
 * inconnu ; oublié avec la session. */
static void t_mark_dir(void)
{
    bsk_mark_t m;
    bool ok;
    uint32_t v = 0;
    printf("marque : le sens du dernier changement de position au 0x06, goto ou bague ; CMD_SET_MARK n sans sens\n");
    ready_motor();
    (void)cmd(CMD_FOCUS_GOTO, 20000);
    run_for(2000 * MS);
    (void)cmd(CMD_SET_MARK, BSK_MARK_HERE);
    m = mark_now(&ok);
    CHECK(ok && m.position == 20000 && m.approach_dir == BSK_APPROACH_INCREASING && store_sim_peek(KEY135, &v) &&
              v == (20000u | 1u << 16),
          "après un goto croissant : 20000, croissant, rangé 0x14E20 (0x%X)", v);
    (void)cmd(CMD_FOCUS_GOTO, 18000);
    run_for(2000 * MS);
    (void)cmd(CMD_SET_MARK, BSK_MARK_HERE);
    m = mark_now(&ok);
    CHECK(ok && m.position == 18000 && m.approach_dir == BSK_APPROACH_DECREASING && store_sim_peek(KEY135, &v) &&
              v == (18000u | 2u << 16),
          "après un goto décroissant : 18000, décroissant (0x%X)", v);
    M.pos = 18500;                                 /* la bague tournée : l'objectif publie une autre position, sans goto */
    run_for(100 * MS);
    (void)cmd(CMD_SET_MARK, BSK_MARK_HERE);
    m = mark_now(&ok);
    CHECK(ok && m.position == 18500 && m.approach_dir == BSK_APPROACH_INCREASING, "la bague, croissante : 18500, croissant");
    (void)cmd(CMD_SET_MARK, 17000);
    m = mark_now(&ok);
    CHECK(ok && m.position == 17000 && m.approach_dir == BSK_APPROACH_UNKNOWN && store_sim_peek(KEY135, &v) && v == 17000,
          "CMD_SET_MARK 17000 : sens inconnu");
    M.pos = 18000;
    run_for(100 * MS);
    (void)cmd(CMD_CLEAR_MARK, 0);                   /* sans marque, pas de retour à elle, rien ne bouge */
    (void)cmd(CMD_ATTACH, 0);
    run_for(3000 * MS);
    (void)cmd(CMD_SET_MARK, BSK_MARK_HERE);
    m = mark_now(&ok);
    CHECK(status().session_state == SESSION_READY && ok && m.position == 18000 && m.approach_dir == BSK_APPROACH_UNKNOWN,
          "nouvelle session, aucun changement vu : sens inconnu");
}

/* Le magasin de la carte rend ses résultats plus tard (bsk_store.h ; `hold` les retient) : tant que la lecture de la clé
 * courante n'est pas rendue, pas de marque ; une lecture périmée n'est jamais prise : celle d'avant une écriture, d'une clé
 * quittée puis reprise, ou celle d'une autre clé quand la lecture de la courante n'a pas pu être déposée ; celle-ci est
 * redéposée ; les résultats sont relevés à chaque pas. 134 mm : la clé 01030800bf086. */
static void t_mark_async(void)
{
    char r[BSK_HOST_REPLY_MAX];
    printf("marque : le magasin asynchrone, lectures en attente, périmées, redéposées\n");
    boot();
    store_sim_poke(KEY135, 15000);
    store_sim_poke("01030800bf086", 12345);
    g_store_sim.hold = true;
    lens_135();
    g_ps.flow = true;
    ps_presence(true);
    run_for(3000 * MS);
    bsk_host_init("2.0.0-dev", "poweron");
    CHECK(status().session_state == SESSION_READY, "READY");
    HOST("j", "-");
    store_sim_flush();
    HOST("j", "-");
    run_for(20 * MS);
    HOST("j", "15000");

    focal(1340);                                    /* lue en attente : 134 mm, puis 135 mm de nouveau */
    focal(1350);
    MARK_AT(16000, E_OK);                           /* l'écriture après la lecture de 135 mm, avant qu'elle ne soit faite */
    focal(1340);
    focal(1350);
    HOST("j", "-");                                 /* relue : en attente */
    store_sim_flush();
    run_for(20 * MS);
    HOST("j", "16000");

    focal(1340);                                    /* la lecture de 134 mm en attente, celle de 135 mm refusée */
    g_store_sim.refuse = true;
    focal(1350);
    store_sim_flush();
    run_for(20 * MS);
    HOST("j", "-");
    g_store_sim.refuse = false;
    run_for(20 * MS);
    store_sim_flush();
    run_for(20 * MS);
    HOST("j", "16000");
    g_store_sim.hold = false;
}

/* Les gestes de marque comptés dans l'instantané, pour la LED. js, CMD_SET_MARK n, jx et l'appui long du bouton servis
 * comptent chacun un ; un refus (hors des bornes, la file du magasin pleine), un zoom (la clé change, la marque publiée
 * aussi), une relecture rendue après l'entrée en READY, l'appui court (un goto) et une nouvelle session ne comptent rien.
 * 134 mm : une clé sans marque. */
static void t_mark_gestures(void)
{
    char r[BSK_HOST_REPLY_MAX];
    bsk_status_t s;
    printf("marque : les gestes comptés dans l'instantané, ni refus, ni zoom, ni relecture, ni session\n");
    ready_motor();
    bsk_host_init("2.0.0-dev", "poweron");
    s = status();
    CHECK(s.mark_sets == 0 && s.mark_clears == 0, "READY : aucun geste (%u, %u)", s.mark_sets, s.mark_clears);
    HOST("js", "ok");
    MARK_AT(16340, E_OK);
    HOST("jx", "ok");
    s = status();
    CHECK(s.mark_sets == 2 && s.mark_clears == 1, "js, CMD_SET_MARK 16340, jx : deux poses, un effacement (%u, %u)", s.mark_sets,
          s.mark_clears);
    MARK_AT(13877, E_LIMIT);
    g_store_sim.refuse = true;
    HOST("js", "er busy");
    HOST("jx", "er busy");
    g_store_sim.refuse = false;
    s = status();
    CHECK(s.mark_sets == 2 && s.mark_clears == 1 && !s.mark_valid, "refusés : rien de compté (%u, %u)", s.mark_sets,
          s.mark_clears);

    MARK_AT(16340, E_OK);
    focal(1344);
    s = status();
    CHECK(!s.mark_valid && s.mark_sets == 3 && s.mark_clears == 1, "un zoom, 134 mm : plus de marque, rien de compté (%u, %u)",
          s.mark_sets, s.mark_clears);
    g_store_sim.hold = true;
    focal(1350);
    CHECK(!status().mark_valid, "135 mm : la relecture en attente");
    store_sim_flush();
    run_for(20 * MS);
    g_store_sim.hold = false;
    s = status();
    CHECK(s.session_state == SESSION_READY && s.mark_valid && s.mark_position == 16340 && s.mark_sets == 3 && s.mark_clears == 1,
          "la relecture rendue en READY : la marque 16340 revient, rien de compté (%u, %u)", s.mark_sets, s.mark_clears);

    press_for(1500);
    s = status();
    CHECK(s.mark_position == 14623 && s.mark_sets == 4 && s.mark_clears == 1, "l'appui long : la marque ici, une pose (%u, %u)",
          s.mark_sets, s.mark_clears);
    press_for(300);
    run_for(500 * MS);
    s = status();
    CHECK(board_acks() >= 1 && s.mark_sets == 4 && s.mark_clears == 1, "l'appui court : le goto du bouton, rien de compté");

    (void)cmd(CMD_ATTACH, 0);
    run_for(3000 * MS);
    s = status();
    CHECK(s.session_state == SESSION_READY && s.mark_valid && s.mark_sets == 4 && s.mark_clears == 1,
          "une nouvelle session : les compteurs gardés (%u, %u)", s.mark_sets, s.mark_clears);
}

/* CMD_SET_MARK hors de READY (ici HOMING, l'init arrêtée avant le 0x0A) : E_BUSY, comme un goto, pas de marque ; l'identité
 * et le 0x06 sont pourtant là. */
static void t_mark_homing(void)
{
    uint32_t q;
    printf("marque : CMD_SET_MARK en HOMING, E_BUSY\n");
    boot();
    lens_135();
    no_answer(0x0B);
    g_ps.flow = true;
    ps_presence(true);
    while (status().session_state != SESSION_HOMING && ps_now() < T0 + 5000 * MS) run_for(1 * MS);
    q = cmd(CMD_SET_MARK, BSK_MARK_HERE);
    CHECK(status().session_state == SESSION_HOMING && acks_are(q, 1, ACK_REJECTED, E_BUSY) && !status().mark_valid &&
              g_store_sim.writes == 0,
          "CMD_SET_MARK en HOMING : E_BUSY, pas de marque, rien d'écrit");
}

/* Pendant un goto (le moteur instable : il ne s'immobilise pas, la commande reste en vol jusqu'à STALLED, 10 s), un appui
 * court et un appui long sont ignorés et journalisés : ni goto, ni marque, rien d'écrit. */
static void t_btn_busy(void)
{
    uint32_t w0;
    bool ok;
    printf("bouton : pendant un goto, court et long ignorés, journalisés\n");
    ready_motor();
    bsk_journal_init(ps_now);
    bsk_journal_set(true, false);
    (void)cmd(CMD_SET_MARK, 16340);
    w0 = g_store_sim.writes;
    M.jitter = true;
    (void)cmd(CMD_FOCUS_GOTO, 20000);
    run_for(100 * MS);
    CHECK(status().motion_state == MOTION_MOVING, "le goto en vol");
    jdrain();
    press_for(300);
    press_for(2000);
    jdrain();
    CHECK(status().motion_state == MOTION_MOVING, "toujours en vol");
    CHECK(jcount("press=short result=ignore why=busy") == 1 && jcount("press=long result=ignore why=busy") == 1 && jcount("* btn ") == 2,
          "« * btn <t> press=short|long result=ignore why=busy » (%zu : « %s »)", n_jl, n_jl ? JL[0] : "");
    CHECK(mark_now(&ok).position == 16340 && ok && g_store_sim.writes == w0 && board_acks() == 0,
          "ni marque posée, ni goto du bouton");
    bsk_journal_set(false, false);
}

/* Un appui commencé en HOMING (l'init arrêtée avant le 0x0A : le homing du pilote) et relâché en READY, 1,5 s plus tard :
 * oublié, alors qu'un appui de cette durée né en READY poserait la marque. */
static void t_btn_homing(void)
{
    uint64_t t0;
    bool ok;
    printf("bouton : un appui commencé en HOMING et relâché en READY est oublié\n");
    boot();
    lens_135();
    no_answer(0x0B);
    g_ps.flow = true;
    ps_presence(true);
    while (status().session_state != SESSION_HOMING && ps_now() < T0 + 5000 * MS) run_for(1 * MS);
    CHECK(status().session_state == SESSION_HOMING, "HOMING");
    bsk_journal_init(ps_now);
    bsk_journal_set(true, false);
    jdrain();
    btn(true);
    t0 = ps_now();
    while (status().session_state != SESSION_READY && ps_now() < t0 + 1400 * MS) run_for(1 * MS);
    CHECK(status().session_state == SESSION_READY, "READY, le bouton toujours enfoncé");
    run_to(t0 + 1500 * MS);
    btn(false);
    run_for(50 * MS);
    jdrain();
    CHECK(jcount("press=long result=ignore why=homing") == 1 && jcount("* btn ") == 1,
          "« * btn <t> press=long result=ignore why=homing » (%zu : « %s »)", n_jl, n_jl ? JL[0] : "");
    mark_now(&ok);
    CHECK(!ok && g_store_sim.writes == 0 && board_acks() == 0, "ni marque posée, ni goto");
    press_for(1500);
    jdrain();
    CHECK(jcount("press=long result=ok") == 1 && mark_now(&ok).position == 14623 && ok,
          "le même appui né en READY : la marque en 14623");
    bsk_journal_set(false, false);
}

/* Un appui court sans marque : rien, journalisé. Une marque hors des bornes du 0x06 (une valeur sans sens,
 * 13000 < 13878) : le goto du bouton est refusé par l'admission d'un f<n> (E_LIMIT), journalisé, rien d'émis ; mg de même. */
static void t_btn_nomark_limit(void)
{
    char r[BSK_HOST_REPLY_MAX];
    size_t from;
    printf("bouton : court sans marque, rien ; court vers une marque hors des bornes, refusé (E_LIMIT)\n");
    ready_motor();
    bsk_journal_init(ps_now);
    bsk_journal_set(true, false);
    jdrain();
    press_for(300);
    jdrain();
    CHECK(jcount("press=short result=er why=nomark") == 1 && jcount("* btn ") == 1 && board_acks() == 0,
          "« * btn <t> press=short result=er why=nomark », aucun goto (%zu)", jcount("* btn "));

    boot();
    store_sim_poke(KEY135, 13000);
    lens_135();
    g_ps.flow = true;
    ps_presence(true);
    run_for(2000 * MS);
    bsk_host_init("2.0.0-dev", "poweron");
    bsk_journal_init(ps_now);
    bsk_journal_set(true, false);
    CHECK(status().session_state == SESSION_READY && status().mark_position == 13000, "READY, marque 13000");
    jdrain();
    from = g_ps_n;
    press_for(300);
    jdrain();
    CHECK(jcount("press=short result=er why=limit") == 1 && jcount("* btn ") == 1,
          "« * btn <t> press=short result=er why=limit » (%zu)", jcount("* btn "));
    CHECK(board_acks() == 1 && ACKS[n_acks - 1].a.seq == BSK_SEQ_BOARD && ACKS[n_acks - 1].a.result == ACK_REJECTED &&
              ACKS[n_acks - 1].a.reason == E_LIMIT,
          "le goto du bouton : ACK_REJECTED, E_LIMIT, sous BSK_SEQ_BOARD");
    {
        bool d1 = false;
        for (size_t i = from; i < g_ps_n && i < PS_LOG_CAP; i++)
            d1 = d1 || (g_ps_log[i].kind == PS_SEND && g_ps_log[i].frame.msg[0] == 0x04 && g_ps_log[i].frame.len == 19);
        CHECK(!d1 && status().motion_state == MOTION_IDLE, "aucun 0x1D émis");
    }
    HOST("jg", "er range limits");
    bsk_journal_set(false, false);
}

/* ─────────────────────────── RESTORING ─────────────────────────── */

/* Le 135 des traces, `v` rangé sous KEY135 (m07 : un autre 0x07, sa clé `key`), le moteur scripté dès le départ, en 14623 ;
 * D2 présent. Le démarrage : l'init faite, la boucle lancée avec le 0x10 (un Samyang reconnu), la fin du démarrage à la
 * réponse au 0x0A (5 ms), le flux déjà là. */
static void restore_script(const char *key, uint32_t v, const uint8_t *m07, size_t n07)
{
    boot();
    store_sim_poke(key, v);
    lens_135();
    if (m07) ps_answer(0x07, m07, n07, 5 * MS);
    memset(&M, 0, sizeof M);
    M.pos = 14623;
    g_ps.on_send = motor;
    g_ps.flow = true;
    bsk_journal_init(ps_now);
    ps_presence(true);
}

/* Jusqu'au 0x0A de l'init émis ; puis LOG ON (la première seconde du démarrage est pleine, voir t_journal de
 * test_session135.c). */
static long until_0a(void)
{
    while (sent(0, 2, 0x0A) < 0 && ps_now() < T0 + 3000 * MS) run_for(1 * MS);
    bsk_journal_set(true, false);
    jdrain();
    return sent(0, 2, 0x0A);
}

/* Les cibles des 0x1D émis depuis l'entrée `from`, dans l'ordre ; leur nombre. */
static size_t targets_1d(size_t from, uint16_t *t, size_t cap)
{
    size_t n = 0;
    for (size_t i = from; i < g_ps_n && i < PS_LOG_CAP; i++) {
        const bsk_frame_t *f = &g_ps_log[i].frame;
        if (g_ps_log[i].kind != PS_SEND || f->msg[0] != 0x04 || f->len != 19 || f->msg[14] != 0x1D) continue;
        if (n < cap) t[n] = (uint16_t)(f->msg[15] | f->msg[16] << 8);
        n++;
    }
    return n;
}

/* Un flux arrêté au 0x04 qui suit le premier vu : la paire du premier est la dernière qui le porte (avec, pour le 0x05,
 * une impulsion à l'offset 60, msg[61]) ; elle n'est plus émise ensuite. Le motor() est retiré : la position ne bouge plus. */
static struct { unsigned k; uint64_t t04; uint8_t type; } OS;

static void one_stream_hook(const bsk_frame_t *f, uint64_t t)
{
    if (f->cls != 1 || f->msg[0] != 0x04) return;
    if (OS.k++ == 0) {
        if (OS.type == 0x05) g_ps.m05[61] = 0x01;
        OS.t04 = t;
        return;
    }
    if (OS.type == 0x05) g_ps.n05 = 0;
    else g_ps.n06 = 0;
}

static void stop_stream(uint8_t type)
{
    memset(&OS, 0, sizeof OS);
    OS.type = type;
    g_ps.on_send = one_stream_hook;
    run_for(40 * MS);
    g_ps.on_send = NULL;
}

/* Le 0x05 arrêté en READY, l'offset 60 du dernier non nul et la bague en focus (L03-02, suite de t_one_stream). Le 135 des
 * traces, son offset 62 mis à 03 (MF : mf=1, la bague au focus, ring.c SW_MF). Attendus, à la main : 2 s plus 1 µs après
 * le dernier 0x05 (2 ms après son 0x04, FLOW_05_US), mf inconnu, la bague gardée au focus ; un goto sur la position
 * (14623) arrive à l'immobilité, 300 ms et dix 0x06 (still.c) : l'impulsion du 0x05 arrêté est oubliée avec lui (un
 * offset 60 gardé non nul, still() ne constate jamais l'immobilité, renvoi à 1 s). Puis un 0x05 de 40 octets revient,
 * lisible (29 au moins) sans offset 62 : le rôle de la bague ne change pas (ring.c ne le change que sur un offset 62
 * lu ; l'offset 62 oublié vaut 0, qui serait l'ouverture), mf reste inconnu. */
static void t_one_stream_05(void)
{
    uint64_t tl, t0, tf = 0;
    bsk_status_t st;
    uint32_t q;
    printf("le 0x05 arrêté : son offset 60 oublié (un goto sur place arrive), son offset 62 inconnu ne change pas la bague\n");
    boot();
    lens_135();
    ps_presence(true);
    run_for(3000 * MS);
    g_ps.m05[63] = 0x03;
    run_for(50 * MS);
    st = status();
    CHECK(st.session_state == SESSION_READY && st.focus_position == 14623 && st.ring == RING_FOCUS && st.mf == BSK_YES,
          "READY en 14623, l'offset 62 à 03 : la bague au focus, mf=1");
    stop_stream(0x05);
    tl = OS.t04 + 2 * MS;
    CHECK(OS.k >= 2, "le 0x05 arrêté après une impulsion");
    run_to(tl + 2000 * MS + 1);
    st = status();
    CHECK(st.session_state == SESSION_READY && st.mf == BSK_UNKNOWN && st.ring == RING_FOCUS,
          "2 s plus 1 µs après le dernier 0x05 : READY, mf inconnu, la bague gardée au focus (%u, %u, %u)",
          st.session_state, st.mf, st.ring);
    t0 = ps_now();
    q = cmd(CMD_FOCUS_GOTO, 14623);
    run_for(500 * MS);
    CHECK(final_is(q, ACK_COMPLETED, E_OK, &tf) && tf > t0 + 300 * MS && tf < t0 + 400 * MS,
          "un goto sur place : ARRIVED à l'immobilité, l'impulsion du 0x05 arrêté oubliée (%lld µs)",
          (long long)(tf ? tf - t0 : 0));
    g_ps.n05 = 40;
    run_for(50 * MS);
    st = status();
    CHECK(st.aperture_current == 0x11B2 && st.mf == BSK_UNKNOWN && st.ring == RING_FOCUS,
          "un 0x05 de 40 octets revient : l'ouverture relue, mf inconnu, la bague toujours au focus (0x%04X, %u, %u)",
          st.aperture_current, st.mf, st.ring);
}

/* Le 0x05 arrêté, la bague en rôle ouverture (le 135 des traces, offset 62 à 01, sa plage BF 11 / 00 19) : l'ouverture
 * qu'elle publie (offsets 17-19) reste la dernière, comme le rôle (PROTOCOL.md § 7) ; au retour du 0x05, une ouverture
 * publiée différente est un changement, prise (t_ring_aperture). Codes, à la main : f/4 -> 0x1400 ; f/4,5 -> 0x1457 ; la
 * consigne de départ, f/1,8 (B2 11) bornée à la plage : BF 11. Le 0x05 arrêté sans impulsion, comme t_one_stream. */
static void t_one_stream_ring(void)
{
    uint64_t tl;
    printf("le 0x05 arrêté : l'ouverture publiée par la bague gardée, un changement à son retour est pris\n");
    boot();
    lens_135();
    ps_presence(true);
    run_for(3000 * MS);
    g_ps.m05[20] = 0x01;                             /* offset 19, bit 0 : l'objectif publie son ouverture, offsets 17-18 */
    g_ps.m05[18] = 0x00;
    g_ps.m05[19] = 0x14;
    run_for(20 * MS);
    CHECK(status().session_state == SESSION_READY && status().ring == RING_APERTURE && consigne(0x11BF),
          "READY, bague en ouverture, 0x1400 publiée : la première, pas prise, BF 11");
    tl = last_04_at(ps_now()) + 2 * MS;             /* le dernier 0x05 */
    g_ps.n05 = 0;
    run_to(tl + 2000 * MS + 1);
    CHECK(status().session_state == SESSION_READY && status().aperture_current == 0 && consigne(0x11BF),
          "2 s plus 1 µs après le dernier 0x05 : READY, l'ouverture inconnue, la consigne BF 11");
    g_ps.n05 = (uint16_t)sources135_msg(SRC_T05, g_ps.m05, sizeof g_ps.m05);
    g_ps.m05[20] = 0x01;
    g_ps.m05[18] = 0x57;
    g_ps.m05[19] = 0x14;
    run_for(20 * MS);
    CHECK(status().ring == RING_APERTURE && consigne(0x1457),
          "le 0x05 revenu, 0x1457 publiée : un changement depuis 0x1400, pris");
}

/* Un flux arrêté en RESTORING (L03-02 : le détecteur est le même qu'en READY). La marque 20000 croissante, le moteur
 * scripté figé : le goto du retour ne part pas, renvoyé à 1 s et 2 s, STALLED à 3 s (t_restore_loss de test_session135.c).
 * Un flux est arrêté à l'entrée en RESTORING. Attendus, à la main : 2 s après son dernier message (2 ms après son 0x04
 * pour le 0x05, 3 ms pour le 0x06), ses données valides ; 1 µs plus tard, invalides, toujours RESTORING (l'autre flux
 * court : pas de perte) ; jamais RECOVERING. position_valid n'est vrai qu'en READY (l'instantané) : en RESTORING, le
 * 0x06 se lit à la position et aux bornes publiées (0 sans 0x06) et à CAP_LIMITS_REPORTED. */
static void t_one_stream_restoring(void)
{
    uint64_t tl;
    bsk_status_t st;
    printf("un flux arrêté en RESTORING : ses données invalides au bout de la durée de la perte, sans perte\n");
    for (int k = 0; k < 2; k++) {
        uint8_t type = k ? 0x05 : 0x06;
        restore_script(KEY135, 20000 | 1u << 16, NULL, 0);
        M.frozen = true;
        while (status().session_state != SESSION_RESTORING && ps_now() < T0 + 5000 * MS) run_for(1 * MS);
        stop_stream(type);
        tl = OS.t04 + (type == 0x05 ? 2 : 3) * MS;
        run_to(tl + 2000 * MS);
        st = status();
        CHECK(st.session_state == SESSION_RESTORING &&
                  (type == 0x06 ? st.focus_position == 14623 && st.focus_max != 0 && (st.capabilities & CAP_LIMITS_REPORTED)
                                : st.aperture_current == 0x11B2 && st.mf == BSK_NO),
              "0x%02X arrêté en RESTORING : ses données valides 2 s après son dernier", type);
        run_to(tl + 2000 * MS + 1);
        st = status();
        CHECK(st.session_state == SESSION_RESTORING &&
                  (type == 0x06 ? st.focus_position == 0 && st.focus_max == 0 && !(st.capabilities & CAP_LIMITS_REPORTED)
                                : st.focus_max != 0 && st.aperture_current == 0 && st.mf == BSK_UNKNOWN &&
                                      st.oss == BSK_UNKNOWN),
              "0x%02X arrêté en RESTORING : 1 µs plus tard invalides, toujours RESTORING (%u, %ld, %ld, 0x%04X, %u)", type,
              st.session_state, (long)st.focus_position, (long)st.focus_max, st.aperture_current, st.mf);
        run_for(2000 * MS);
        CHECK(entered(SESSION_RECOVERING, 0) == 0, "0x%02X arrêté en RESTORING, l'autre flux courant : aucune perte", type);
    }
}

/* Le homing du pilote (l'init arrêtée avant le 0x0B, puis 0x1C, 0x10 08) finit aussi par RESTORING : l'immobilité après la
 * réponse au 0x10 (700 ms), constatée sur les trames, puis la marque 20000 croissante, depuis 14623 : un goto, READY sur elle.
 * Puis la même immobilité constatée à l'échéance même du homing (3 s après la réponse) : quinze 0x06 immobiles, puis plus
 * rien ; la fin du démarrage aussi, pas un échec compté à la place : RESTORING, puis la perte, un échec compté par elle,
 * `* restore … end=recovering`. La dernière télémétrie a 2,75 s à l'entrée ; la perte n'échoit pas dans le passé, elle
 * est constatée 2 s après l'entrée (plus 1 µs, watch), pas au même instant. */
static void t_restore_homing(void)
{
    uint16_t t[4] = {0};
    size_t n, ih, ir;
    long i10;
    uint64_t tr;
    printf("RESTORING après le homing du pilote : l'immobilité sur les trames, puis à l'échéance même\n");
    restore_script(KEY135, 20000 | 1u << 16, NULL, 0);
    no_answer(0x0B);
    run_for(6000 * MS);
    n = targets_1d(0, t, 4);
    for (ih = 0; ih < n_chg && chg[ih].state != SESSION_HOMING; ih++) {}
    for (ir = ih; ir < n_chg && chg[ir].state != SESSION_RESTORING; ir++) {}
    CHECK(ih < n_chg && ir == ih + 1 && ir + 1 < n_chg && chg[ir + 1].state == SESSION_READY,
          "HOMING, RESTORING, READY (%zu changements)", n_chg);
    CHECK(n == 1 && t[0] == 20000 && status().session_state == SESSION_READY && status().focus_position == 20000,
          "un 0x1D, 20000 ; READY sur la marque (%zu : %u)", n, t[0]);

    restore_script(KEY135, 20000 | 1u << 16, NULL, 0);
    no_answer(0x0B);
    for (i10 = -1; i10 < 0 && ps_now() < T0 + 5000 * MS; i10 = sent(0, 2, 0x10)) run_for(1 * MS);
    bsk_journal_set(true, false);
    jdrain();
    tr = t_of(i10) + 700 * MS;                      /* la réponse au 0x10 08 */
    run_to(tr + 250 * MS);
    g_ps.flow = false;
    run_to(tr + 5100 * MS);
    jdrain();
    CHECK(i10 >= 0 && entered(SESSION_RESTORING, 0) == tr + 3000 * MS &&
              entered(SESSION_RECOVERING, 0) == tr + 5000 * MS + 1 && jcount(" path=direct") == 1 &&
              jcount(" end=recovering") == 1,
          "immobile à l'échéance : RESTORING (« path=direct »), perdu 2 s après l'entrée (« end=recovering »), RECOVERING "
          "(%lld µs après l'entrée, %zu)", (long long)(entered(SESSION_RECOVERING, 0) - tr - 3000 * MS), n_jl);
    bsk_journal_set(false, false);
}

/* La lecture de la marque est asynchrone (bsk_store.h) : « pas encore relue » n'est pas « absente ». La fin du démarrage
 * l'attend, dans l'état où elle est (IDENTIFYING ici), rien d'émis ; rendue, RESTORING. Jamais rendue : READY 500 ms après la
 * fin du démarrage (MARK_WAIT_MS), sans mouvement, `skip=timeout`. */
static void t_restore_wait(void)
{
    uint16_t t[4] = {0};
    uint64_t tf, tr, tend;
    long i0a;
    size_t n;
    printf("RESTORING : la lecture de la marque attendue, rendue puis jamais rendue\n");
    restore_script(KEY135, 20000, NULL, 0);
    g_store_sim.hold = true;
    i0a = until_0a();
    tend = t_of(i0a) + 5 * MS;                      /* la réponse au 0x0A : la fin du démarrage */
    run_to(tend + 300 * MS);
    CHECK(i0a >= 0 && status().session_state == SESSION_IDENTIFYING && entered(SESSION_RESTORING, 0) == 0 &&
              entered(SESSION_READY, 0) == 0 && targets_1d(0, t, 4) == 0,
          "la lecture en attente, 300 ms : toujours IDENTIFYING, aucun 0x1D");
    tf = ps_now();
    store_sim_flush();
    run_for(20 * MS);
    tr = entered(SESSION_RESTORING, 0);
    CHECK(tr > tf && tr <= tf + 20 * MS, "la lecture rendue : RESTORING au pas suivant (%lld µs)", (long long)(tr - tf));
    run_for(3000 * MS);
    n = targets_1d(0, t, 4);
    CHECK(n == 2 && t[0] == 20200 && t[1] == 20000 && status().session_state == SESSION_READY && status().focus_position == 20000,
          "puis 20200, 20000, READY sur la marque (%zu : %u, %u)", n, t[0], t[1]);
    g_store_sim.hold = false;

    restore_script(KEY135, 20000, NULL, 0);
    g_store_sim.hold = true;
    i0a = until_0a();
    tend = t_of(i0a) + 5 * MS;
    run_to(tend + 1000 * MS);
    jdrain();
    CHECK(entered(SESSION_READY, 0) == tend + 500 * MS && entered(SESSION_RESTORING, 0) == 0,
          "jamais rendue : READY 500 ms après la fin du démarrage, sans RESTORING (%lld µs)",
          (long long)(entered(SESSION_READY, 0) - tend));
    CHECK(targets_1d(0, t, 4) == 0 && status().focus_position == 14623 && status().motion_state == MOTION_IDLE,
          "aucun 0x1D, en 14623, mouvement au repos");
    CHECK(jcount("* restore ") == 1 && jcount(" skip=timeout") == 1, "« * restore <t> skip=timeout » (%zu : « %s »)", n_jl,
          n_jl ? JL[0] : "");
    bsk_journal_set(false, false);
    g_store_sim.hold = false;
}

/* Un blocage pendant le retour (le moteur ne bouge pas : ni démarrage ni accusé en 1 s, trois fois, STALLED) mène à
 * READY, position valide, lue sur l'objectif. */
static void t_restore_stall(void)
{
    uint64_t tr, ts;
    printf("RESTORING : bloqué (STALLED), READY, position valide\n");
    restore_script(KEY135, 20000 | 1u << 16, NULL, 0);
    M.frozen = true;
    until_0a();
    run_for(5000 * MS);
    jdrain();
    tr = entered(SESSION_RESTORING, 0);
    ts = mv_entered(MOTION_STALLED, 0);
    CHECK(tr != 0 && ts == tr + 3000 * MS && entered(SESSION_READY, 0) == ts,
          "STALLED 3 s après l'entrée en RESTORING (le 0x1D envoyé trois fois), READY au même instant");
    CHECK(count_sent(ts, ts + 1, 0x1C) == 1, "STALLED pendant le retour : un 0x1C à son instant, comme pour un goto de l'hôte");
    CHECK(status().session_state == SESSION_READY && status().position_valid && status().focus_position == 14623,
          "READY, position valide, 14623");
    CHECK(jcount(" end=stall") == 1 && jcount(" path=direct") == 1, "« * restore <t> … path=direct », puis « end=stall » (%zu)", n_jl);
    CHECK(n_acks == 0 && g_store_sim.writes == 0, "aucun accusé, rien d'écrit dans le magasin (la marque ne s'écrit que sur un geste)");
    bsk_journal_set(false, false);
}

/* X : 1 % de la course du 0x06, au moins 1, pour un objectif qui n'est pas le 135 ; 200 pour le 135. Les bornes du 0x06
 * posées à 14600 / 14690 : 14605 / 14685 appliquées, 80 pas de course. La marque 14650, sens inconnu : en décroissant, et
 * 14623 est dessous. Le F051 (TAMRON07, LensType2 0xC134, sa clé 010334c1bf087 : 01 03, 34 C1, puis BF du 0x08 du 135) : X =
 * 80 / 100 = 0, donc 1 : 14651 puis 14650. Le 135 : X = 200, 14850 au-delà de 14685 : 14685 puis 14650. */
static void t_restore_x(void)
{
    uint16_t t[4] = {0};
    size_t n;
    printf("RESTORING : X = 1 %% de la course, au moins 1 ; X = 200 pour le 135\n");
    restore_script("010334c1bf087", 14650, TAMRON07, sizeof TAMRON07);
    set_limits(14600, 14690);
    run_for(4000 * MS);
    n = targets_1d(0, t, 4);
    CHECK(n == 2 && t[0] == 14651 && t[1] == 14650 && status().focus_position == 14650,
          "le F051, 80 pas de course : X = 1, 14651 puis 14650 (%zu : %u, %u)", n, t[0], t[1]);
    restore_script(KEY135, 14650, NULL, 0);
    set_limits(14600, 14690);
    run_for(4000 * MS);
    n = targets_1d(0, t, 4);
    CHECK(n == 2 && t[0] == 14685 && t[1] == 14650 && status().focus_position == 14650,
          "le 135, les mêmes bornes : X = 200, borné à 14685, puis 14650 (%zu : %u, %u)", n, t[0], t[1]);
}

/* Le bouton du fût en RESTORING : un appui né et relâché pendant le retour est ignoré (il n'agit qu'en READY), journalisé ;
 * le retour continue jusqu'à la marque. */
static void t_restore_button(void)
{
    bool in_rs;
    printf("RESTORING : le bouton ignoré, journalisé ; le retour continue\n");
    restore_script(KEY135, 30000 | 1u << 16, NULL, 0);
    until_0a();
    while (status().session_state != SESSION_RESTORING && ps_now() < T0 + 5000 * MS) run_for(1 * MS);
    run_for(100 * MS);
    btn(true);
    run_for(300 * MS);
    btn(false);
    run_for(20 * MS);
    in_rs = status().session_state == SESSION_RESTORING;
    jdrain();
    CHECK(in_rs && jcount("press=short result=ignore why=restoring") == 1 && board_acks() == 0,
          "« * btn <t> press=short result=ignore why=restoring », aucun goto du bouton");
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY && status().focus_position == 30000 && status().mark_position == 30000 &&
              g_store_sim.writes == 0,
          "READY sur la marque, 30000, rien d'écrit");
    bsk_journal_set(false, false);
}

/* Le retour de la marque 20000, sens inconnu, depuis 14623 (20200 puis 20000, 250 pas par trame), LOG ON : jusqu'à `n` 0x1D
 * émis et la position publiée au-delà de `beyond` ; rend l'instant. */
static uint64_t rekey_until(size_t n, int32_t beyond)
{
    uint16_t t[4];
    restore_script(KEY135, 20000, NULL, 0);
    until_0a();
    while (!(status().session_state == SESSION_RESTORING && targets_1d(0, t, 4) >= n && M.pos > beyond) &&
           ps_now() < T0 + 8000 * MS)
        run_for(1 * MS);
    return ps_now();
}

/* Le retour abandonné après le changement posé à `tz` : READY à la trame qui le porte (deux périodes de VD au plus), le
 * 0x1C à cet instant et pas avant, `n` 0x1D en tout, la position lue sur l'objectif. */
static void rekey_abandoned(uint64_t tz, size_t n, const char *what)
{
    uint16_t t[4] = {0};
    uint64_t ty;
    bsk_status_t st;
    run_for(3000 * MS);
    jdrain();
    ty = entered(SESSION_READY, 0);
    st = status();
    CHECK(ty > tz && ty <= tz + 40 * MS, "%s : READY à la trame qui porte le changement (%lld µs après)", what,
          (long long)(ty - tz));
    CHECK(count_sent(tz, ty, 0x1C) == 0 && count_sent(ty, ty + 1, 0x1C) == 1, "%s : le 0x1C à l'entrée en READY, comme q", what);
    CHECK(targets_1d(0, t, 4) == n, "%s : %zu 0x1D au lieu de %zu, aucun après le changement (%u, %u)", what,
          targets_1d(0, t, 4), n, t[0], t[1]);
    CHECK(st.session_state == SESSION_READY && st.motion_state == MOTION_ABORTED && st.position_valid &&
              st.focus_position == M.pos,
          "%s : READY, ABORTED, la position lue sur l'objectif (%ld)", what, (long)st.focus_position);
    CHECK(jcount(" end=changed") == 1 && jcount(" end=") == 1, "%s : « * restore <t> end=changed » (%zu)", what, n_jl);
    bsk_journal_set(false, false);
}

/* L05-04 : la clé de la marque (sa focale nominale, mark.c:key_of) ou les bornes du 0x06, lues autres pendant le retour,
 * l'abandonnent (PROTOCOL.md § 7). Attendus, à la main :
 *   - une focale nominale de 1000 (100 mm, clé 01030800bf064) pendant le premier goto, au-delà de 16000 : un 0x1D, 20200 ;
 *   - les bornes lues 13873 / 30000 (30733 devient 29995) pendant l'immobilité du second goto (20200 -> 20000, une trame) :
 *     deux 0x1D, 20200 puis 20000 ;
 *   - les bornes lues 14000 / 30738 (13878 devient 14005) pendant le premier goto : un 0x1D, 20200 ;
 *   - une focale nominale de 1352 pendant le premier goto : 135 mm, la même clé (087), pas un changement ; 20200 puis 20000,
 *     READY sur la marque, `end=arrived`.
 * Une clé vidée ou des bornes à 0 par l'arrêt d'un flux ne sont pas un changement : t_one_stream_restoring. */
static void t_restore_rekey(void)
{
    uint16_t t[4] = {0};
    uint64_t tz;
    printf("RESTORING : la focale nominale ou les bornes lues autres pendant le retour l'abandonnent, comme q\n");
    tz = rekey_until(1, 16000);
    g_ps.m05[27] = (uint8_t)1000;
    g_ps.m05[28] = (uint8_t)(1000 >> 8);
    rekey_abandoned(tz, 1, "une autre focale nominale au premier goto");

    rekey_until(2, 0);
    run_for(50 * MS);
    CHECK(status().session_state == SESSION_RESTORING && M.pos == 20000, "témoin : le second goto en 20000, encore RESTORING");
    tz = ps_now();
    set_limits(13873, 30000);
    rekey_abandoned(tz, 2, "d'autres bornes au second goto");

    tz = rekey_until(1, 16000);
    set_limits(14000, 30738);
    rekey_abandoned(tz, 1, "une autre borne basse au premier goto");

    rekey_until(1, 16000);
    g_ps.m05[27] = (uint8_t)1352;
    g_ps.m05[28] = (uint8_t)(1352 >> 8);
    run_for(3000 * MS);
    jdrain();
    CHECK(targets_1d(0, t, 4) == 2 && t[0] == 20200 && t[1] == 20000 && status().session_state == SESSION_READY &&
              status().focus_position == 20000 && jcount(" end=arrived") == 1,
          "la focale 1352, la même clé : 20200 puis 20000, READY sur la marque, « end=arrived » (%u, %u)", t[0], t[1]);
    bsk_journal_set(false, false);
}

/* Le moteur scripté, et au premier 0x04 émis après l'armement : la focale nominale `focal` posée dans le 0x05 de sa paire
 * (2 ms après, FLOW_05_US), l'émission suivante ratée (fail_sends : rien sur le fil) ; sans autre trame de la carte d'ici
 * là, c'est le 0x1C de l'abandon. */
static struct { bool armed; uint16_t focal; uint64_t t04; } RK;

static void rekey_hook(const bsk_frame_t *f, uint64_t t)
{
    motor(f, t);
    if (!RK.armed || f->cls != 1 || f->msg[0] != 0x04) return;
    RK.armed = false;
    RK.t04 = t;
    g_ps.m05[27] = (uint8_t)RK.focal;
    g_ps.m05[28] = (uint8_t)(RK.focal >> 8);
    g_ps.fail_sends = 1;
}

/* L05-04, l'abandon comme q jusqu'au bout (cmd.c:stop, t_stop_link, t_stop_watch) :
 *   - le 0x1C qui ne part pas : READY quand même, au 0x05 qui porte la focale 1000 (2 ms après son 0x04), l'émission
 *     ratée étant celle du 0x1C ; aucun 0x1C sur le fil ni ensuite (pas parti, pas surveillé), aucun 0x1D de plus, ABORTED,
 *     `end=changed` ; le moteur scripté, qui n'a rien vu, va seul au bout de son goto (20200) : la position lue sur lui ;
 *   - le 0x1C parti, sans accusé ni immobilité (`jitter`, posé avec la focale) : surveillé comme celui de q, renvoyé à
 *     1,5 s et à 3 s, trois en tout. */
static void t_restore_rekey_stop(void)
{
    uint16_t t[4] = {0};
    uint64_t ty, tz;
    bsk_status_t st;
    printf("RESTORING : l'abandon sur changement, comme q : READY si le 0x1C ne part pas ; parti, il est surveillé\n");
    rekey_until(1, 16000);
    RK.armed = true;
    RK.focal = 1000;
    g_ps.on_send = rekey_hook;
    run_for(3000 * MS);
    jdrain();
    ty = entered(SESSION_READY, 0);
    st = status();
    CHECK(RK.t04 != 0 && ty == RK.t04 + 2 * MS && g_ps.fail_sends == 0,
          "le 0x1C raté : READY au 0x05 qui porte la focale, 2 ms après son 0x04 (%lld µs)", (long long)(ty - RK.t04));
    CHECK(count_sent(RK.t04, ps_now(), 0x1C) == 0, "aucun 0x1C sur le fil, ni renvoyé : pas parti, pas surveillé");
    CHECK(targets_1d(0, t, 4) == 1 && t[0] == 20200, "un seul 0x1D, 20200 : aucun goto après le changement");
    CHECK(st.session_state == SESSION_READY && st.motion_state == MOTION_ABORTED && st.position_valid &&
              st.focus_position == 20200 && M.pos == 20200,
          "READY, ABORTED, la position lue sur l'objectif, allé seul au bout (%ld)", (long)st.focus_position);
    CHECK(jcount(" end=changed") == 1 && jcount(" end=") == 1, "« * restore <t> end=changed » (%zu)", n_jl);
    bsk_journal_set(false, false);
    g_ps.on_send = motor;

    tz = rekey_until(1, 16000);
    M.jitter = true;
    g_ps.m05[27] = (uint8_t)1000;
    g_ps.m05[28] = (uint8_t)(1000 >> 8);
    run_for(6000 * MS);
    ty = entered(SESSION_READY, 0);
    CHECK(ty > tz && ty <= tz + 40 * MS && count_sent(ty, ty + 1, 0x1C) == 1 &&
              count_sent(ty + 1500 * MS, ty + 1500 * MS + 1, 0x1C) == 1 &&
              count_sent(ty + 3000 * MS, ty + 3000 * MS + 1, 0x1C) == 1 && count_sent(tz, ps_now(), 0x1C) == 3,
          "le 0x1C parti, sans accusé ni immobilité : à l'abandon, puis à 1,5 et 3 s, trois en tout (%zu)",
          count_sent(tz, ps_now(), 0x1C));
    bsk_journal_set(false, false);
}

/* L05-05 : sur la marque à la fin du démarrage, le jeu n'est repris dans le sens de l'arrivée (rangé avec la marque ;
 * inconnu : en décroissant) que si le dernier sens vu au 0x06 (mark.c) est celui-là ; un autre, ou inconnu : par le
 * dépassement, comme du mauvais côté. La marque 14623, la position de départ ; le dernier sens posé pendant le homing de
 * l'init (la boucle lancée au 0x10) : 14500 puis 14623, croissant ; 14700 puis 14623, décroissant ; 14623 seul, inconnu.
 * Cibles, à la main, X = 200 : en croissant 14423, en décroissant 14823, puis 14623. Puis du mauvais côté, le dernier sens
 * celui de l'arrivée : le dépassement quand même, la marque 14400 croissante, 14200 puis 14400. */
static void t_restore_equal(void)
{
    static const struct {
        uint32_t v;
        int32_t pre;
        const char *what;
        uint16_t t[2];
        size_t n;
    } C[] = {
        {14623 | 1u << 16, 14500, "croissante, dernier sens croissant : droit sur elle", {14623}, 1},
        {14623 | 1u << 16, 14700, "croissante, dernier sens décroissant : 14423 puis 14623", {14423, 14623}, 2},
        {14623 | 1u << 16, 14623, "croissante, dernier sens inconnu : 14423 puis 14623", {14423, 14623}, 2},
        {14623 | 2u << 16, 14700, "décroissante, dernier sens décroissant : droit sur elle", {14623}, 1},
        {14623 | 2u << 16, 14500, "décroissante, dernier sens croissant : 14823 puis 14623", {14823, 14623}, 2},
        {14623, 14700, "sens inconnu (en décroissant), dernier sens décroissant : droit sur elle", {14623}, 1},
        {14623, 14500, "sens inconnu, dernier sens croissant : 14823 puis 14623", {14823, 14623}, 2},
        {14623, 14623, "sens inconnu, dernier sens inconnu : 14823 puis 14623", {14823, 14623}, 2},
        {14400 | 1u << 16, 14500, "14400 croissante, depuis 14623 au-dessus, dernier sens croissant : 14200 puis 14400",
         {14200, 14400}, 2},
    };
    printf("RESTORING : sur la marque, le jeu repris par le dépassement sauf si le dernier sens est celui de l'arrivée\n");
    for (size_t k = 0; k < sizeof C / sizeof C[0]; k++) {
        uint16_t t[4] = {0};
        size_t n;
        long i10;
        restore_script(KEY135, C[k].v, NULL, 0);
        for (i10 = -1; i10 < 0 && ps_now() < T0 + 3000 * MS; i10 = sent(0, 2, 0x10)) run_for(1 * MS);
        run_for(100 * MS);
        M.pos = C[k].pre;
        run_for(100 * MS);
        M.pos = 14623;
        run_for(100 * MS);
        CHECK(i10 >= 0 && entered(SESSION_RESTORING, 0) == 0 && status().session_state == SESSION_IDENTIFYING,
              "%s : témoin, le sens posé pendant le homing de l'init", C[k].what);
        run_for(5000 * MS);
        n = targets_1d(0, t, 4);
        CHECK(entered(SESSION_RESTORING, 0) != 0 && n == C[k].n && t[0] == C[k].t[0] && (n < 2 || t[1] == C[k].t[1]),
              "%s : %zu 0x1D, %u %u", C[k].what, n, t[0], t[1]);
        CHECK(status().session_state == SESSION_READY && status().motion_state == MOTION_ARRIVED &&
                  status().focus_position == (int32_t)(C[k].v & 0xFFFF),
              "%s : READY, ARRIVED sur la marque", C[k].what);
    }
}

/* L'accusé 1C seul éteint la surveillance de l'arrêt. La bague tournée à la main après q (`jitter` : la position change à
 * chaque 0x06, jamais immobile) : le 0x1C accusé (1C 00, samyang.md § 4.4), jamais renvoyé ; sans accusé,
 * renvoyé à 1,5 s et à 3 s. Un 0x1C qui n'est pas parti (q le dit) n'est pas surveillé. */
static void t_stop_watch(void)
{
    size_t from;
    uint64_t tq;
    long a;
    printf("q : l'accusé 1C seul éteint la surveillance de l'arrêt ; un 0x1C pas parti n'est pas surveillé\n");
    ready_motor();
    M.jitter = true;
    M.ack1c = true;
    from = g_ps_n;
    (void)cmd(CMD_FOCUS_STOP, 0);
    run_for(5000 * MS);
    a = sent(from, 1, 0x1C);
    CHECK(a >= 0 && sent((size_t)a + 1, 1, 0x1C) < 0, "accusé, la position qui bouge encore : un seul 0x1C");

    ready_motor();
    M.jitter = true;
    from = g_ps_n;
    tq = ps_now();
    (void)cmd(CMD_FOCUS_STOP, 0);
    run_for(6000 * MS);
    CHECK(count_sent(tq, tq + 1, 0x1C) == 1 && count_sent(tq + 1500 * MS, tq + 1500 * MS + 1, 0x1C) == 1 &&
              count_sent(tq + 3000 * MS, tq + 3000 * MS + 1, 0x1C) == 1 && count_sent(tq, tq + 6000 * MS, 0x1C) == 3,
          "témoin, sans accusé ni immobilité : trois 0x1C, à 0, 1,5 et 3 s");

    ready_motor();
    M.jitter = true;
    g_ps.fail_sends = 1;
    tq = ps_now();
    (void)cmd(CMD_FOCUS_STOP, 0);
    run_for(5000 * MS);
    CHECK(count_sent(tq, tq + 5000 * MS, 0x1C) == 0, "le 0x1C pas parti : rien à surveiller, aucun 0x1C ensuite");
}

/* q rend le résultat de l'émission du 0x1C (PROTOCOL.md, ligne q : `er link …`). L'émission ratée (fail_sends : E_BUS,
 * rien sur le fil, le moteur scripté ne voit pas le 0x1C et continue) : ACK_FAILED, E_BUS, `er link bus` ; le goto en vol
 * est ABORTED quand même (la consigne est retirée) ; en RESTORING, READY quand même. Émis : ACK_COMPLETED, `ok`. */
static void t_stop_link(void)
{
    char r[BSK_HOST_REPLY_MAX];
    uint32_t g, q;
    size_t from;
    long i1c;
    printf("q : le 0x1C qui ne part pas, ACK_FAILED E_BUS, « er link bus » ; ABORTED ; READY depuis RESTORING\n");
    boot();                                         /* OFF, D2 absent : aucun 0x1C tenté, ok */
    bsk_host_init("2.0.0-dev", "poweron");
    g_ps.fail_sends = 1;
    from = g_ps_n;
    HOST("q", "ok");
    CHECK(status().session_state == SESSION_OFF && g_ps.fail_sends == 1 && g_ps_n == from, "OFF : q, ok, aucune émission tentée");
    g_ps.fail_sends = 0;
    ready_motor();
    bsk_host_init("2.0.0-dev", "poweron");
    g = cmd(CMD_FOCUS_GOTO, 16623);
    run_for(100 * MS);
    CHECK(status().motion_state == MOTION_MOVING, "un goto en cours");
    from = g_ps_n;
    g_ps.fail_sends = 1;
    q = cmd(CMD_FOCUS_STOP, 0);
    CHECK(final_is(q, ACK_FAILED, E_BUS, NULL), "q, le 0x1C pas émis : ACK_ACCEPTED puis ACK_FAILED, E_BUS");
    CHECK(final_is(g, ACK_FAILED, E_ABORTED, NULL) && status().motion_state == MOTION_ABORTED &&
              status().session_state == SESSION_READY && g_ps_n == from,
          "le goto : ACK_FAILED, E_ABORTED ; ABORTED, READY, rien sur le fil");
    run_for(100 * MS);
    CHECK(targets_1d(from, NULL, 0) == 0, "la consigne retirée : plus de 0x1D");
    g_ps.fail_sends = 1;
    HOST("q", "er link bus");
    from = g_ps_n;
    HOST("q", "ok");
    i1c = sent(from, 1, 0x1C);
    CHECK(i1c >= 0 && status().session_state == SESSION_READY, "émis : le 0x1C part, « ok »");

    restore_script(KEY135, 20000 | 1u << 16, NULL, 0);
    bsk_host_init("2.0.0-dev", "poweron");
    while (status().session_state != SESSION_RESTORING && ps_now() < T0 + 5000 * MS) run_for(1 * MS);
    run_for(100 * MS);
    CHECK(status().session_state == SESSION_RESTORING && status().motion_state == MOTION_MOVING, "RESTORING, en mouvement");
    from = g_ps_n;
    g_ps.fail_sends = 1;
    HOST("q", "er link bus");
    CHECK(status().session_state == SESSION_READY && status().motion_state == MOTION_ABORTED && g_ps_n == from,
          "RESTORING, le 0x1C pas émis : « er link bus », READY, ABORTED, rien sur le fil");
    run_for(100 * MS);
    CHECK(targets_1d(from, NULL, 0) == 0 && status().session_state == SESSION_READY, "plus de 0x1D, toujours READY");
    bsk_journal_set(false, false);
}

/* Le 0x10 de l'init est un homing (init.c, S_Q10 ; sa réponse n'arrive qu'à la fin du mouvement) : q y est refusé comme en
 * HOMING, E_BUSY, `er busy home`, rien d'émis, et le démarrage continue jusqu'à READY. Ailleurs dans IDENTIFYING (le 0x01
 * en vol ; le 0x10 resté sans réponse en 8 s, le premier 0x05 attendu, sans flux) et en OFF après une retombée de D2
 * pendant ce 0x10 : `ok`, rien d'émis. Le 135 des traces répond au 0x10 en 700 ms (lens_135). */
static void t_stop_init_home(void)
{
    char r[BSK_HOST_REPLY_MAX];
    size_t from;
    uint32_t q;
    uint64_t t10;
    printf("q pendant le 0x10 de l'init : E_BUSY, « er busy home », rien d'émis ; ailleurs dans IDENTIFYING, ok\n");
    boot();
    bsk_host_init("2.0.0-dev", "poweron");
    lens_135();
    g_ps.flow = true;
    ps_presence(true);
    while (status().session_state != SESSION_IDENTIFYING && ps_now() < T0 + 5000 * MS) run_for(1 * MS);
    from = g_ps_n;
    q = cmd(CMD_FOCUS_STOP, 0);
    CHECK(status().session_state == SESSION_IDENTIFYING && sent(0, 2, 0x10) < 0 && final_is(q, ACK_COMPLETED, E_OK, NULL) &&
              g_ps_n == from,
          "IDENTIFYING, le 0x01 en vol : q accepté, fini, rien d'émis");
    while (sent(0, 2, 0x10) < 0 && ps_now() < T0 + 5000 * MS) run_for(1 * MS);
    t10 = t_of(sent(0, 2, 0x10));
    run_for(100 * MS);
    from = g_ps_n;
    q = cmd(CMD_FOCUS_STOP, 0);
    CHECK(status().session_state == SESSION_IDENTIFYING && n_acks > 0 && ACKS[n_acks - 1].a.seq == q &&
              ACKS[n_acks - 1].a.result == ACK_REJECTED && ACKS[n_acks - 1].a.reason == E_BUSY && g_ps_n == from,
          "le 0x10 de l'init en vol, 100 ms : q refusé, E_BUSY, rien d'émis");
    HOST("q", "er busy home");
    CHECK(g_ps_n == from, "« er busy home », rien d'émis");
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY && count_sent(t10, ps_now(), 0x1C) == 0,
          "le homing n'est pas interrompu : READY, aucun 0x1C");

    boot();                                         /* le 0x10 sans réponse, sans flux : IDENTIFYING, le 0x05 attendu */
    bsk_host_init("2.0.0-dev", "poweron");
    lens_135();
    g_ps.skip[0x10] = 1;
    ps_presence(true);
    while (sent(0, 2, 0x10) < 0 && ps_now() < T0 + 5000 * MS) run_for(1 * MS);
    run_for(8000 * MS + 10 * MS);
    from = g_ps_n;
    HOST("q", "ok");
    CHECK(status().session_state == SESSION_IDENTIFYING && g_ps_n == from,
          "le 0x10 échu à 8 s, le premier 0x05 attendu : IDENTIFYING, q -> ok, rien d'émis");

    boot();                                         /* D2 retombe pendant le 0x10 de l'init : OFF */
    bsk_host_init("2.0.0-dev", "poweron");
    lens_135();
    g_ps.flow = true;
    ps_presence(true);
    while (sent(0, 2, 0x10) < 0 && ps_now() < T0 + 5000 * MS) run_for(1 * MS);
    run_for(100 * MS);
    ps_presence(false);
    run_for(10 * MS);
    from = g_ps_n;
    HOST("q", "ok");
    CHECK(status().session_state == SESSION_OFF && g_ps_n == from, "D2 retombé pendant le 0x10 de l'init : OFF, q -> ok, rien d'émis");
}

/* L'arrêt non confirmé d'un déplacement suivi (stop_unconfirmed, compté à l'abandon du 0x1C, 4,5 s après le premier envoi :
 * trois fenêtres de 1,5 s) : après q qui arrête un goto, et après STALLED d'un goto ; pas après q sans goto en vol, ni après
 * STALLED du retour à la marque (aucun des deux n'est suivi). Un second q pendant la surveillance de l'arrêt d'un goto
 * reste celui de ce goto. Le moteur instable (jitter) ne s'immobilise jamais et n'accuse pas le 0x1C. */
static void t_stop_unconfirmed(void)
{
    uint64_t tq, ts;
    uint32_t g;
    size_t m0;
    printf("arrêt non confirmé : compté après q ou STALLED d'un déplacement suivi, à 4,5 s ; pas sans lui\n");
    ready_motor();                                  /* q arrête un goto */
    M.jitter = true;
    (void)cmd(CMD_FOCUS_GOTO, 16623);
    run_for(100 * MS);
    tq = ps_now();
    (void)cmd(CMD_FOCUS_STOP, 0);
    run_to(tq + 4500 * MS - 1);
    CHECK(status().stop_unconfirmed == 0, "q, 4,5 s moins 1 µs : rien");
    run_to(tq + 4500 * MS);
    CHECK(status().stop_unconfirmed == 1, "q, le troisième 0x1C sans accusé ni immobilité : compté à 4,5 s (%u)",
          status().stop_unconfirmed);
    run_for(5000 * MS);
    CHECK(status().stop_unconfirmed == 1 && count_sent(tq + 4500 * MS, ps_now(), 0x1C) == 0, "une fois, plus de 0x1C");

    ready_motor();                                  /* STALLED d'un goto */
    M.jitter = true;
    m0 = n_mchg;
    g = cmd(CMD_FOCUS_GOTO, 16623);
    run_for(16000 * MS);
    CHECK(final_is(g, ACK_FAILED, E_STALL, &ts) && mv_entered(MOTION_STALLED, m0) == ts, "STALLED");
    CHECK(status().stop_unconfirmed == 1 && count_sent(ts, ps_now(), 0x1C) == 3, "STALLED, son 0x1C trois fois, abandonné : compté");

    ready_motor();                                  /* q sans goto en vol */
    M.jitter = true;
    (void)cmd(CMD_FOCUS_STOP, 0);
    run_for(6000 * MS);
    CHECK(status().stop_unconfirmed == 0, "q en READY sans goto en vol, abandonné : pas compté (%u)", status().stop_unconfirmed);

    ready_motor();                                  /* q arrête un goto, puis un second q 1 s après */
    M.jitter = true;
    (void)cmd(CMD_FOCUS_GOTO, 16623);
    run_for(100 * MS);
    (void)cmd(CMD_FOCUS_STOP, 0);
    run_for(1000 * MS);
    tq = ps_now();
    (void)cmd(CMD_FOCUS_STOP, 0);
    run_to(tq + 4500 * MS);
    CHECK(status().stop_unconfirmed == 1, "un second q pendant la surveillance : l'arrêt du goto, compté (%u)",
          status().stop_unconfirmed);

    restore_script(KEY135, 20000 | 1u << 16, NULL, 0);   /* STALLED du retour à la marque */
    until_0a();
    M.jitter = true;
    run_for(20000 * MS);
    jdrain();
    CHECK(mv_entered(MOTION_STALLED, 0) != 0 && status().session_state == SESSION_READY && n_acks == 0,
          "le retour, instable : STALLED, READY, aucun accusé");
    CHECK(status().stop_unconfirmed == 0 && count_sent(mv_entered(MOTION_STALLED, 0), ps_now(), 0x1C) == 3,
          "son 0x1C trois fois, abandonné : pas compté (%u)", status().stop_unconfirmed);
    bsk_journal_set(false, false);
}

/* ─────────────────────────── CMD_LENS_CUSTOM ─────────────────────────── */

/* La réponse du 135 au canal 0x40 : 19 octets, l'écho de MainCmd et SubCmd, puis 16 octets de données (samyang.md § 6.1 ;
 * 'P' FA : § 6.3, la configuration à l'octet de données 7). Données de test, toutes différentes, pour voir
 * qu'elles sont rendues telles quelles et dans l'ordre. */
static const uint8_t RPFA[19] = {0x40, 'P', 0xFA, 1, 2, 3, 4, 5, 6, 7, 0x21, 9, 10, 11, 12, 13, 14, 15, 16};
static const uint8_t RP38[19] = {0x40, 'P', 0x38};

/* La trame 0x40 émise depuis l'entrée `from` du journal : la seule, de classe 2, ses 19 octets, `0x40 'P' sub d` puis des
 * zéros (écrits à la main). */
static bool custom_sent(size_t from, uint8_t sub, uint8_t d)
{
    uint8_t want[19] = {0x40, 'P', sub, d};
    long i = sent(from, 2, 0x40);
    return i >= 0 && sent((size_t)i + 1, 2, 0x40) < 0 && sent(from, 1, 0x40) < 0 && g_ps_log[i].frame.len == 19 &&
           !memcmp(g_ps_log[i].frame.msg, want, 19);
}

/* Le 135 des traces en READY, qui répond `r` au 0x40 après 60 ms (la réponse du 135 part au moins 50 ms après, 7_Docs/E-Mount/samyang.md § 6.1). */
static void ready135_custom(const uint8_t *r)
{
    boot();
    lens_135();
    ps_answer(0x40, r, 19, 60 * MS);
    ps_presence(true);
    run_for(4000 * MS);
}

static void t_custom(void)
{
    uint8_t d[BSK_CUSTOM_DATA];
    uint32_t q, g;
    size_t from;
    uint32_t r0;
    bsk_ack_t a;
    printf("CMD_LENS_CUSTOM au 135 : 'P' FA 00 et 'P' 38 30+arg, la réponse rendue, une commande en vol à la fois, échéance, sortie de READY, bench_core\n");
    ready135_custom(RPFA);
    CHECK(status().session_state == SESSION_READY, "READY");
    from = g_ps_n;
    q = cmd(CMD_LENS_CUSTOM, -1);
    CHECK(acks_are(q, 1, ACK_ACCEPTED, E_OK), "lire : acceptée, en vol");
    g = cmd(CMD_FOCUS_GOTO, 20000);
    CHECK(acks_are(g, 1, ACK_REJECTED, E_BUSY), "une commande en vol à la fois : un goto pendant CMD_LENS_CUSTOM, E_BUSY");
    run_for(15 * MS);
    CHECK(custom_sent(from, 0xFA, 0x00), "lire : une trame de classe 2, 40 50 FA 00 et quinze 00");
    run_for(200 * MS);
    bsk_session_custom_data(d);
    CHECK(final_is(q, ACK_COMPLETED, E_OK, NULL) && !memcmp(d, RPFA + 3, sizeof d), "lire : COMPLETED, les 16 octets de "
          "données de la réponse rendus tels quels");
    from = g_ps_n;
    q = cmd(CMD_LENS_CUSTOM, 0x21);
    run_for(200 * MS);
    bsk_session_custom_data(d);
    CHECK(custom_sent(from, 0x38, 0x51), "écrire 21 : 40 50 38 51 et quinze 00");
    CHECK(final_is(q, ACK_FAILED, E_FRAMING, NULL) && !memcmp(d, RPFA + 3, sizeof d),
          "une réponse 0x40 d'une autre sous-commande ('P' FA à 'P' 38) : FAILED, E_FRAMING, ses données pas rendues");
    ps_answer(0x40, RP38, sizeof RP38, 60 * MS);
    from = g_ps_n;
    q = cmd(CMD_LENS_CUSTOM, 0x02);
    run_for(200 * MS);
    bsk_session_custom_data(d);
    CHECK(custom_sent(from, 0x38, 0x32) && final_is(q, ACK_COMPLETED, E_OK, NULL) && !memcmp(d, RP38 + 3, sizeof d),
          "écrire 02 : 40 50 38 32, COMPLETED, l'écho rendu (données à zéro)");
    no_answer(0x40);
    q = cmd(CMD_LENS_CUSTOM, -1);
    run_for(995 * MS);
    CHECK(acks_are(q, 1, ACK_ACCEPTED, E_OK), "sans réponse : en vol à 995 ms");
    run_for(30 * MS);
    CHECK(final_is(q, ACK_FAILED, E_TIMEOUT, NULL), "sans réponse : FAILED, E_TIMEOUT, à 1 s de l'émission");
    CHECK(acks_are(cmd(CMD_LENS_CUSTOM, -1), 1, ACK_ACCEPTED, E_OK), "puis acceptée de nouveau");
    run_for(1100 * MS);
    for (int k = 0; k < 2; k++) {
        static const int32_t BAD[] = {0x03, 0x30};   /* le bas à 3, le haut à 3 */
        r0 = bsk_bench_refused();
        from = g_ps_n;
        q = cmd(CMD_LENS_CUSTOM, BAD[k]);
        run_for(50 * MS);
        CHECK(n_acks && ACKS[n_acks - 1].a.seq == q && ACKS[n_acks - 1].a.result != ACK_COMPLETED &&
                  ACKS[n_acks - 1].a.reason == E_FORBIDDEN && sent(from, 2, 0x40) < 0 && bsk_bench_refused() == r0 + 1,
              "écrire %02X : refusée par bench_core (E_FORBIDDEN, un refus compté), rien d'émis", (unsigned)BAD[k]);
    }
    ps_answer(0x40, RPFA, sizeof RPFA, 60 * MS);
    q = cmd(CMD_LENS_CUSTOM, -1);
    run_for(15 * MS);
    ps_presence(false);
    run_for(10 * MS);
    CHECK(final_is(q, ACK_FAILED, E_ABORTED, NULL) && status().session_state == SESSION_OFF,
          "D2 retombe pendant CMD_LENS_CUSTOM : OFF, FAILED, E_ABORTED");
    run_for(200 * MS);
    CHECK(!bsk_session_ack(&a), "plus aucun accusé ensuite");
}

static void t_custom_others(void)
{
    uint8_t m07[BSK_MSG_MAX], m3f[66] = {0x3F, 0x00};
    size_t n07 = sources135_msg(SRC_R07, m07, sizeof m07);
    uint32_t r0;
    printf("CMD_LENS_CUSTOM refusée, rien d'émis : le 24 mm (LensType2 C93A), un Samyang par son nom seul, un Tamron, hors READY\n");
    m07[10] = 0x3A;                                   /* le LensType2, offsets 9-10 du 0x07 : 0xC93A, le 24 mm */
    m07[11] = 0xC9;                                   /* dans la liste des Samyang */
    boot();
    lens_135();
    ps_answer(0x07, m07, n07, 5 * MS);
    ps_answer(0x40, RPFA, sizeof RPFA, 60 * MS);
    ps_presence(true);
    run_for(4000 * MS);
    r0 = bsk_bench_refused();
    CHECK(status().session_state == SESSION_READY && status().lens_id_product == 0xC93A, "24 mm : READY, LensType2 C93A");
    CHECK(acks_are(cmd(CMD_LENS_CUSTOM, -1), 1, ACK_REJECTED, E_NOCAP) && acks_are(cmd(CMD_LENS_CUSTOM, 0x21), 1, ACK_REJECTED, E_NOCAP),
          "24 mm : lire et écrire refusées, E_NOCAP");
    run_for(200 * MS);
    CHECK(sent(0, 2, 0x40) < 0 && sent(0, 1, 0x40) < 0 && bsk_bench_refused() == r0, "24 mm : aucune trame 0x40, aucun refus");
    boot();
    lens_135();
    memcpy(m3f + 2, NAME135, sizeof NAME135 - 1);
    ps_answer(0x07, TAMRON07, sizeof TAMRON07, 5 * MS);
    ps_answer(0x3F, m3f, sizeof m3f, 6 * MS);
    ps_answer(0x40, RPFA, sizeof RPFA, 60 * MS);
    ps_presence(true);
    run_for(4000 * MS);
    CHECK(status().session_state == SESSION_READY && !strcmp(status().lens_name, NAME135) &&
              acks_are(cmd(CMD_LENS_CUSTOM, -1), 1, ACK_REJECTED, E_NOCAP),
          "nommé « SAMYANG AF 135mm F1.8 », LensType2 C134 : refusée, E_NOCAP (le LensType2 décide)");
    boot();
    lens_135();
    ps_answer(0x07, TAMRON07, sizeof TAMRON07, 5 * MS);
    ps_answer(0x40, RPFA, sizeof RPFA, 60 * MS);
    ps_presence(true);
    run_for(4000 * MS);
    CHECK(status().session_state == SESSION_READY && acks_are(cmd(CMD_LENS_CUSTOM, -1), 1, ACK_REJECTED, E_NOCAP),
          "Tamron : refusée, E_NOCAP");
    run_for(200 * MS);
    CHECK(sent(0, 2, 0x40) < 0, "Tamron, nommé : aucune trame 0x40");
    boot();
    CHECK(acks_are(cmd(CMD_LENS_CUSTOM, -1), 1, ACK_REJECTED, E_BUSY), "OFF : refusée, E_BUSY (comme f<n>)");
    lens_135();
    ps_presence(true);
    run_for(30 * MS);
    CHECK(status().session_state == SESSION_POWERING && acks_are(cmd(CMD_LENS_CUSTOM, -1), 1, ACK_REJECTED, E_BUSY),
          "POWERING : refusée, E_BUSY");
}

/* CMD_LENS_CUSTOM déposée entre le 0x03 et le 0x04 de la boucle : retenue jusqu'au 0x04 (bsk_txn.h). D2 retombe avant lui :
 * la session est oubliée, la boucle arrêtée, et l'écriture retenue ne part pas ; la commande finit en E_ABORTED, sans
 * avoir été émise. */
static void t_custom_held_abort(void)
{
    uint32_t q, r0;
    size_t from;
    int k = 0;
    printf("CMD_LENS_CUSTOM retenue entre le 0x03 et le 0x04, puis D2 retombe : rien d'émis, FAILED, E_ABORTED\n");
    ready135_custom(RP38);
    CHECK(status().session_state == SESSION_READY, "READY");
    while (k++ < 400 && !(g_ps_n && g_ps_log[g_ps_n - 1].kind == PS_SEND && g_ps_log[g_ps_n - 1].frame.msg[0] == 0x03))
        run_for(100);                                /* jusqu'à un 0x03 parti, son 0x04 1,5 ms plus tard */
    CHECK(g_ps_n && g_ps_log[g_ps_n - 1].frame.msg[0] == 0x03, "un 0x03 parti, son 0x04 pas encore");
    from = g_ps_n;
    r0 = bsk_bench_refused();
    q = cmd(CMD_LENS_CUSTOM, 0x21);
    CHECK(acks_are(q, 1, ACK_ACCEPTED, E_OK) && g_ps_n == from, "écrire 21 : acceptée, retenue, rien d'émis");
    ps_presence(false);
    run_for(200 * MS);
    CHECK(status().session_state == SESSION_OFF && final_is(q, ACK_FAILED, E_ABORTED, NULL),
          "D2 retombe avant le 0x04 : OFF, FAILED, E_ABORTED");
    CHECK(sent(from, 2, 0x40) < 0 && sent(from, 1, 0x40) < 0 && sent(from, 1, 0x04) < 0 && bsk_bench_refused() == r0,
          "ni l'écriture retenue ni le 0x04 ne sont partis, aucun refus");
}

/* Le bouton respecte l'exclusion de la commande USB : pendant CMD_LENS_CUSTOM en vol (le 135 muet, 1 s d'échéance), un appui
 * long et un appui court sont ignorés et journalisés comme pendant un goto (t_btn_busy) : ni marque posée, rien d'écrit en
 * NVS, ni goto. Chaque appui est relâché moins d'une seconde après le dépôt de la commande, qui est donc encore en vol. */
static void t_btn_custom(void)
{
    uint32_t w0;
    bool ok;
    printf("bouton : pendant CMD_LENS_CUSTOM en vol, court et long ignorés, journalisés\n");
    ready135_custom(RP38);
    no_answer(0x40);
    bsk_journal_init(ps_now);
    bsk_journal_set(true, false);
    CHECK(status().session_state == SESSION_READY && !mark_now(&ok).position && !ok, "READY, sans marque");
    w0 = g_store_sim.writes;
    jdrain();
    btn(true);
    run_for(300 * MS);
    CHECK(acks_are(cmd(CMD_LENS_CUSTOM, -1), 1, ACK_ACCEPTED, E_OK), "lire : acceptée, en vol, le bouton tenu");
    run_for(900 * MS);
    btn(false);
    run_for(50 * MS);                                /* appui de 1,2 s : long ; relâché 0,95 s après le dépôt */
    run_for(1100 * MS);                              /* la lecture finie à son échéance */
    CHECK(acks_are(cmd(CMD_LENS_CUSTOM, -1), 1, ACK_ACCEPTED, E_OK), "lire de nouveau : acceptée, en vol");
    press_for(300);
    jdrain();
    CHECK(jcount("press=long result=ignore why=busy") == 1 && jcount("press=short result=ignore why=busy") == 1 &&
              jcount("* btn ") == 2,
          "« * btn <t> press=long|short result=ignore why=busy » (%zu : « %s », « %s »)", n_jl, n_jl ? JL[0] : "",
          n_jl > 1 ? JL[1] : "");
    CHECK(!mark_now(&ok).position && !ok && g_store_sim.writes == w0 && board_acks() == 0,
          "ni marque posée, rien d'écrit en NVS, ni goto du bouton");
    bsk_journal_set(false, false);
}

/* Tout refus de bench_core est visible : rendu à l'appelant quand il est émis tout de suite, au journal (`* refused`, une
 * ligne par trame refusée) et compté (bsk_bench_refused), qu'il soit émis tout de suite ou retenu entre le 0x03 et le
 * 0x04 et refusé à son départ derrière le 0x04, où personne n'attend son issue. La forme exacte de la ligne : test_journal. */
static void t_txn_refused(void)
{
    static const uint8_t m0c[] = {0x0C, 0x00};      /* hors liste (bsk_bench_core.h) */
    uint32_t r0;
    printf("TRANSACTION : un refus de bench_core au journal et compté, émis tout de suite ou retenu\n");
    txn_boot();
    bsk_journal_init(ps_now);
    bsk_journal_set(true, false);
    jdrain();
    r0 = bsk_bench_refused();
    CHECK(bsk_txn_send(m0c, sizeof m0c) == E_FORBIDDEN && g_ps_n == 0 && bsk_bench_refused() == r0 + 1,
          "0C 00 seul : E_FORBIDDEN, rien d'émis, compté");
    jdrain();
    CHECK(n_jl == 1 && jcount("* refused ") == 1 && jcount(" 0C 00 ") == 1, "une ligne « * refused <t> … 0C 00 … » (%zu : « %s »)",
          n_jl, n_jl ? JL[0] : "");
    bsk_phy_vd(60);
    bsk_txn_loop(true);
    g_ps_n = 0;
    pump(VD1 + 9000);
    CHECK(g_ps_n == 1 && g_ps_log[0].frame.msg[0] == 0x03, "+9000 µs : le 0x03 est parti");
    r0 = bsk_bench_refused();
    CHECK(bsk_txn_send(m0c, sizeof m0c) == E_OK && bsk_bench_refused() == r0, "0C 00 entre le 0x03 et le 0x04 : retenu, E_OK");
    jdrain();
    CHECK(n_jl == 0, "retenu : pas encore de ligne");
    pump(VD1 + 10100);
    jdrain();
    CHECK(g_ps_n == 2 && g_ps_log[1].frame.msg[0] == 0x04 && bsk_bench_refused() == r0 + 1,
          "derrière le 0x04 : le 0C 00 refusé à son départ, rien d'émis que le 0x04, compté");
    CHECK(n_jl == 1 && jcount("* refused ") == 1 && jcount(" 0C 00 ") == 1,
          "le refus d'une émission retenue au journal aussi (%zu : « %s »)", n_jl, n_jl ? JL[0] : "");
    bsk_txn_loop(false);
    bsk_phy_vd(0);
    bsk_journal_set(false, false);
}

/* L'identité et la déclaration à bench_core se font dans l'init (le 0x07 et le 0x3F qu'elle demande) et à l'oubli de la
 * session, nulle part ailleurs : un 0x07 que l'objectif envoie en READY, sans requête, ne change ni l'une ni l'autre. Le 24 mm
 * qui reçoit le 0x07 du 135 (LensType2 8, sources135.c) n'est pas armé pour l'écriture ; le 135 qui reçoit celui du Tamron
 * (LensType2 C134) n'est pas désarmé. */
static void t_custom_identity(void)
{
    uint8_t m07[BSK_MSG_MAX], r07[BSK_MSG_MAX];
    size_t n07 = sources135_msg(SRC_R07, m07, sizeof m07);
    uint32_t r0, q;
    size_t from;
    printf("un 0x07 non demandé, en READY : ni l'identité ni la déclaration à bench_core ne changent\n");
    memcpy(r07, m07, n07);
    m07[10] = 0x3A;                                   /* le 24 mm, LensType2 0xC93A */
    m07[11] = 0xC9;
    boot();
    lens_135();
    ps_answer(0x07, m07, n07, 5 * MS);
    ps_answer(0x40, RP38, sizeof RP38, 60 * MS);
    ps_presence(true);
    run_for(4000 * MS);
    CHECK(status().session_state == SESSION_READY && status().lens_id_product == 0xC93A, "24 mm : READY, LensType2 C93A");
    ps_frame(2, r07, n07, ps_now() + 1 * MS);         /* le 0x07 du 135, sans requête */
    run_for(50 * MS);
    r0 = bsk_bench_refused();
    from = g_ps_n;
    CHECK(status().lens_id_product == 0xC93A, "24 mm, puis le 0x07 du 135 : LensType2 toujours C93A (0x%X)",
          status().lens_id_product);
    CHECK(acks_are(cmd(CMD_LENS_CUSTOM, 0x21), 1, ACK_REJECTED, E_NOCAP), "24 mm, puis le 0x07 du 135 : écrire refusée, E_NOCAP");
    run_for(200 * MS);
    CHECK(sent(from, 2, 0x40) < 0 && bsk_bench_refused() == r0, "24 mm : aucune trame 0x40, aucun refus");

    boot();
    lens_135();
    ps_answer(0x40, RP38, sizeof RP38, 60 * MS);
    ps_presence(true);
    run_for(4000 * MS);
    CHECK(status().session_state == SESSION_READY && status().lens_id_product == 8, "135 : READY, LensType2 8");
    ps_frame(2, TAMRON07, sizeof TAMRON07, ps_now() + 1 * MS);   /* le 0x07 du Tamron, sans requête */
    run_for(50 * MS);
    r0 = bsk_bench_refused();
    from = g_ps_n;
    CHECK(status().lens_id_product == 8, "135, puis le 0x07 du Tamron : LensType2 toujours 8 (0x%X)", status().lens_id_product);
    q = cmd(CMD_LENS_CUSTOM, 0x21);
    run_for(200 * MS);
    CHECK(custom_sent(from, 0x38, 0x51) && final_is(q, ACK_COMPLETED, E_OK, NULL) && bsk_bench_refused() == r0,
          "135, puis le 0x07 du Tamron : écrire 21 émise (40 50 38 51), COMPLETED, aucun refus");
}

/* La file des accusés (bsk_session.h) : dans l'ordre, et, hors contrat, le plus ancien perdu. Les accusés sont les refus
 * d'un goto en OFF (E_BUSY), un par dépôt. */
static void t_acks(void)
{
    bsk_ack_t a;
    bool order = true;
    uint32_t first = seq_n + 1;
    printf("accusés : dans l'ordre, par lots ; file pleine, le plus ancien perdu\n");
    boot();
    for (int k = 0; k < 7; k++) {
        for (int j = 0; j < 3; j++) {
            bsk_cmd_t c = {.seq = ++seq_n, .op = CMD_FOCUS_GOTO, .arg = 20000};
            bsk_session_command(&c);
        }
        for (int j = 0; j < 3; j++) order = order && bsk_session_ack(&a) && a.seq == first++ && a.result == ACK_REJECTED &&
                                           a.reason == E_BUSY;
    }
    CHECK(order && !bsk_session_ack(&a), "21 refus lus par trois : dans l'ordre, rien de plus");
    for (int j = 0; j < 10; j++) {
        bsk_cmd_t c = {.seq = ++seq_n, .op = CMD_FOCUS_GOTO, .arg = 20000};
        bsk_session_command(&c);
    }
    first = seq_n - 7;
    order = true;
    for (int j = 0; j < 8; j++) order = order && bsk_session_ack(&a) && a.seq == first++;
    CHECK(order && !bsk_session_ack(&a), "dix sans lecture : les huit plus récents restent");
}

/* ─────────────────────────── DEBUG : la mesure du temps réel ─────────────────────────── */

/* L01-01 : un événement de PHY daté avant le précédent, compté par la SESSION avec le plus grand écart (bsk_session_debug).
 * La PHY de la carte peut pousser une trame datée à son traitement avant un front daté plus tôt, à son interruption ; la
 * PHY scriptée trie sa file, ps_push_back y pose l'événement derrière, sans tri. La session en OFF, D2 absent : des fronts
 * VD, que rien ne lit en OFF (la boucle arrêtée). Rendus dans l'ordre 120, 110, 115 puis 100 ms après T0 : 110 avant 120,
 * un, 10 ms ; 115 après 110, rien ; 100 avant 115, deux, le plus grand écart 15 ms. Écarts calculés à la main. */
static void vd_back(uint64_t at)
{
    bsk_phy_event_t e = {.kind = BSK_PHY_VD, .t_us = at};
    ps_push_back(&e);
}

static void t_ev_back(void)
{
    bsk_session_debug_t d;
    printf("DEBUG, L01-01 : un événement de PHY daté avant le précédent, compté, le plus grand écart\n");
    boot();
    vd_back(T0 + 120 * MS);
    vd_back(T0 + 110 * MS);
    ps_run(T0 + 130 * MS);
    bsk_session_step(T0 + 130 * MS);
    bsk_session_debug(&d);
    CHECK(d.ev_back == 1 && d.ev_back_us == 10000, "120 puis 110 ms : un, 10000 µs (%lu, %lu µs)", (unsigned long)d.ev_back,
          (unsigned long)d.ev_back_us);
    vd_back(T0 + 115 * MS);                          /* d'un pas à l'autre : le précédent est celui du pas d'avant */
    ps_run(T0 + 140 * MS);
    bsk_session_step(T0 + 140 * MS);
    bsk_session_debug(&d);
    CHECK(d.ev_back == 1 && d.ev_back_us == 10000, "115 après 110 : rien de plus (%lu, %lu µs)", (unsigned long)d.ev_back,
          (unsigned long)d.ev_back_us);
    vd_back(T0 + 100 * MS);
    ps_run(T0 + 150 * MS);
    bsk_session_step(T0 + 150 * MS);
    bsk_session_debug(&d);
    CHECK(d.ev_back == 2 && d.ev_back_us == 15000, "100 après 115 : deux, 15000 µs (%lu, %lu µs)", (unsigned long)d.ev_back,
          (unsigned long)d.ev_back_us);
    CHECK(status().session_state == SESSION_OFF && g_ps_n == 0, "rien d'autre : OFF, rien sur le fil");
    boot();
    bsk_session_debug(&d);
    CHECK(d.ev_back == 0 && d.ev_back_us == 0, "le démarrage de la carte (bsk_session_init) : à zéro");
}

/* M10 : au plus 64 événements de PHY par pas. La session en POWERING (D2 présent à T0 : le rail logique, le rail moteur
 * attendu à T0 + 50 ms) ; 70 fronts VD rétrodatés (ps_push_back), de T0 + 40 ms en reculant d'1 µs, chacun compté dans
 * ev_back sauf le premier : la boucle arrêtée n'en fait rien. Le pas de T0 + 100 ms en traite 64 (63 comptés) et laisse
 * l'échéance du rail moteur au suivant, derrière les 6 qui restent (69 comptés). */
static void t_step_bound(void)
{
    bsk_session_debug_t d;
    long i;
    printf("M10 : au plus 64 événements de PHY par pas, le reste et les échéances au pas suivant\n");
    boot();
    ps_presence(true);
    bsk_session_step(T0);
    CHECK(status().session_state == SESSION_POWERING && act(0, PS_RAIL) >= 0, "POWERING, le rail logique");
    for (unsigned k = 0; k < 70; k++) vd_back(T0 + 40 * MS - k);
    ps_run(T0 + 100 * MS);
    bsk_session_step(T0 + 100 * MS);
    bsk_session_debug(&d);
    i = act((size_t)act(0, PS_RAIL) + 1, PS_RAIL);
    CHECK(d.ev_back == 63 && i < 0, "un pas : 64 événements (63 comptés, %lu), le rail moteur pas encore", (unsigned long)d.ev_back);
    bsk_session_step(T0 + 100 * MS);
    bsk_session_debug(&d);
    i = act((size_t)act(0, PS_RAIL) + 1, PS_RAIL);
    CHECK(d.ev_back == 69 && i >= 0 && g_ps_log[i].rail == BSK_RAIL_MOTOR && g_ps_log[i].on,
          "le pas suivant : les 6 restants (69 comptés, %lu), puis le rail moteur", (unsigned long)d.ev_back);
}

/* R2 : une trame de la paire ne part que dans sa fenêtre, 2 ms après son échéance au plus, jugée contre l'instant réel du
 * pas (bsk_session_step), pas contre l'échéance que la SESSION pose en la rattrapant (run) ; hors de sa fenêtre, la paire
 * est jetée, sans rattrapage, et comptée (pair_late, pair_late_us : le plus grand retard en la jetant). Le 135 en READY,
 * la boucle à 60 Hz : V, le front du dernier 0x04 parti (à V + 10100 µs) ; W, le premier front après l'instant courant
 * (V plus k fois 16666 µs, la période de bsk_phy_vd(60) dans la PHY scriptée), puis W2, W3… de 16666 en 16666 µs. Les
 * pas de run_to tombent sur chaque échéance : aucun retard. Retards calculés à la main :
 *   - W servi au pas de W + 10600 : le 0x03 2000 µs après son échéance, dans sa fenêtre, le 0x04 500 µs : partis ;
 *   - W2 servi au pas de W2 + 10601 : le 0x03 2001 µs après, hors de sa fenêtre : ni 0x03 ni 0x04, une paire jetée ;
 *   - W3 : le 0x03 à l'heure (run_to), le 0x04 au pas de W3 + 12500, 2400 µs après : le 0x03 parti seul, jetée ;
 *   - W4 et W5 en attente au pas de W5 + 9000 : la paire de W4, 17066 µs après (16666 + 9000 − 8600), jetée ; seul le
 *     0x03 de W5 part, 400 µs après, et son 0x04 à son échéance ;
 *   - W6 servi au pas de W6 + 11100 : le 0x03 2500 µs après, jeté ; le plus grand retard reste 17066 µs ;
 *   - un pas retenu plusieurs périodes : W7, W8, W9 et W10 en attente au pas de W10 + 9000 ; les paires de W7, W8 et W9
 *     jetées (celle de W7 50398 µs après, 3 × 16666 + 9000 − 8600), aucune rafale : seul le 0x03 de W10 part, 400 µs
 *     après, et son 0x04 à son échéance. */
static void t_pair_late(void)
{
    bsk_session_debug_t d;
    uint64_t v = 0, w;
    size_t from;
    long i3, i4;
    printf("R2 : la paire dans sa fenêtre (2 ms) ou jetée, sans rattrapage ; seul le front le plus récent ; pair_late\n");
    boot();
    lens_135();
    ps_presence(true);
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_READY, "READY");
    bsk_session_debug(&d);
    CHECK(d.pair_late == 0 && d.pair_late_us == 0, "un pas à chaque échéance : aucune paire jetée (%lu, %lu µs)",
          (unsigned long)d.pair_late, (unsigned long)d.pair_late_us);
    for (size_t j = 0; j < g_ps_n && j < PS_LOG_CAP; j++)
        if (g_ps_log[j].kind == PS_SEND && g_ps_log[j].frame.msg[0] == 0x04) v = g_ps_log[j].t - 10100;
    for (w = v; w <= ps_now(); w += 16666) {}
    run_to(w - 1);                                   /* la paire du front d'avant, à l'heure */
    bsk_session_debug(&d);
    CHECK(v && d.pair_late == 0 && d.pair_late_us == 0, "jusqu'au front W : aucune paire jetée");

    from = g_ps_n;
    ps_run(w + 10600);
    bsk_session_step(w + 10600);
    note_state(w + 10600);
    i3 = sent(from, 1, 0x03);
    i4 = sent(from, 1, 0x04);
    bsk_session_debug(&d);
    CHECK(i3 >= 0 && i4 > i3 && t_of(i4) == w + 10600 && d.pair_late == 0,
          "W, le 0x03 2000 µs après son échéance : dans sa fenêtre, le 0x03 et le 0x04 partent");

    w += 16666;
    from = g_ps_n;
    ps_run(w + 10601);
    bsk_session_step(w + 10601);
    note_state(w + 10601);
    bsk_session_debug(&d);
    CHECK(sent(from, 1, 0x03) < 0 && sent(from, 1, 0x04) < 0 && d.pair_late == 1 && d.pair_late_us == 2001,
          "W2, le 0x03 2001 µs après : hors de sa fenêtre, ni 0x03 ni 0x04 ; une paire jetée, 2001 µs (%lu, %lu µs)",
          (unsigned long)d.pair_late, (unsigned long)d.pair_late_us);

    w += 16666;
    from = g_ps_n;
    run_to(w + 8600);
    CHECK(sent(from, 1, 0x03) >= 0 && t_of(sent(from, 1, 0x03)) == w + 8600, "W3 : le 0x03 à son échéance");
    from = g_ps_n;
    ps_run(w + 12500);
    bsk_session_step(w + 12500);
    note_state(w + 12500);
    bsk_session_debug(&d);
    CHECK(sent(from, 1, 0x04) < 0 && d.pair_late == 2 && d.pair_late_us == 2400,
          "W3, le 0x04 2400 µs après : jeté, le 0x03 parti seul ; deux paires jetées, 2400 µs (%lu, %lu µs)",
          (unsigned long)d.pair_late, (unsigned long)d.pair_late_us);

    w += 2 * 16666;
    from = g_ps_n;
    ps_run(w + 9000);
    bsk_session_step(w + 9000);
    note_state(w + 9000);
    bsk_session_debug(&d);
    i3 = sent(from, 1, 0x03);
    CHECK(i3 >= 0 && t_of(i3) == w + 9000 && sent((size_t)i3 + 1, 1, 0x03) < 0 && sent(from, 1, 0x04) < 0 &&
              d.pair_late == 3 && d.pair_late_us == 17066,
          "W4 et W5 en attente : la paire de W4 jetée (17066 µs), un seul 0x03, celui de W5 (%lu, %lu µs)",
          (unsigned long)d.pair_late, (unsigned long)d.pair_late_us);
    run_to(w + 10100);
    i4 = sent(from, 1, 0x04);
    CHECK(i4 > i3 && t_of(i4) == w + 10100 && g_ps_log[i4].frame.seq == g_ps_log[i3].frame.seq,
          "puis le 0x04 de W5 à son échéance, sous le numéro de son 0x03");

    w += 16666;
    from = g_ps_n;
    ps_run(w + 11100);
    bsk_session_step(w + 11100);
    note_state(w + 11100);
    bsk_session_debug(&d);
    CHECK(sent(from, 1, 0x03) < 0 && d.pair_late == 4 && d.pair_late_us == 17066,
          "W6, le 0x03 2500 µs après : jetée ; quatre paires, le plus grand retard toujours 17066 µs (%lu, %lu µs)",
          (unsigned long)d.pair_late, (unsigned long)d.pair_late_us);

    w += 4 * 16666;
    from = g_ps_n;
    ps_run(w + 9000);
    bsk_session_step(w + 9000);
    note_state(w + 9000);
    bsk_session_debug(&d);
    i3 = sent(from, 1, 0x03);
    CHECK(i3 >= 0 && t_of(i3) == w + 9000 && sent((size_t)i3 + 1, 1, 0x03) < 0 && sent(from, 1, 0x04) < 0 &&
              d.pair_late == 7 && d.pair_late_us == 50398,
          "W7 à W10 en attente : trois paires jetées (50398 µs au plus), un seul 0x03, celui de W10 (%lu, %lu µs)",
          (unsigned long)d.pair_late, (unsigned long)d.pair_late_us);
    run_to(w + 10100);
    i4 = sent(from, 1, 0x04);
    CHECK(i4 > i3 && t_of(i4) == w + 10100 && sent((size_t)i4 + 1, 1, 0x04) < 0, "puis le 0x04 de W10 seul, à son échéance");
    boot();
    bsk_session_debug(&d);
    CHECK(d.pair_late == 0 && d.pair_late_us == 0, "le démarrage de la carte (bsk_session_init) : à zéro");
}

int main(void)
{
    signal(SIGALRM, too_long);
    alarm(10);
    t_txn_done();
    t_txn_timeout();
    t_txn_phy_error();
    t_txn_no_retry();
    t_txn_forbidden();
    t_txn_loop();
    t_txn_loop04();
    t_txn_phase();
    t_txn_held();
    t_txn_cancel();
    t_txn_window();
    t_txn_goto();
    t_txn_seq();
    t_off();
    t_cold_start();
    t_limits_06();
    t_not_samyang();
    t_home_failed_init();
    t_10_late();
    t_01_once();
    t_01_reset();
    t_01_silent();
    t_01_silent_cut();
    t_nocap();
    t_cut_pending_pair();
    t_required_missing();
    t_optional_missing();
    t_10_missing();
    t_short_replies();
    t_resend_bus();
    t_name_3f();
    t_home_timeout();
    t_home_rx_error();
    t_home_failed_std();
    t_still_fails();
    t_still_stale();
    t_no_loss_in_homing();
    t_d2_homing();
    t_d2_handshake();
    t_d2_ready();
    t_d2_recovering();
    t_hs_stuck();
    t_sony_cs_high();
    t_sony_edges_only();
    t_hs_journal();
    t_loss();
    t_stale_telemetry();
    t_loss_from_entry();
    t_stale_05();
    t_short_05_06();
    t_one_stream();
    t_one_stream_05();
    t_one_stream_ring();
    t_counter();
    t_mv_ack();
    t_mv_fallback();
    t_mv_move_byte();
    t_mv_same();
    t_mv_stall_start();
    t_mv_resent_by_txn();
    t_mv_stall_still();
    t_mv_stall_stop();
    t_mv_loss();
    t_mv_forgotten();
    t_aperture();
    t_aperture_08();
    t_aperture_start();
    t_aperture_absurd();
    t_aperture_sony();
    t_ring_role();
    t_ring_nocap();
    t_ring_aperture();
    t_ring_no_range();
    t_ring_motion();
    t_ring_after_goto();
    t_ring_motion_tail();
    t_ring_journal();
    t_ring_gesture();
    t_ring_sum();
    t_ring_cap();
    t_ring_sony06();
    t_body08_journal();
    t_mark_letters();
    t_mark_key();
    t_mark_v1();
    t_mark_focal();
    t_mark_dir();
    t_mark_async();
    t_mark_gestures();
    t_mark_homing();
    t_btn_busy();
    t_btn_homing();
    t_btn_nomark_limit();
    t_restore_homing();
    t_one_stream_restoring();
    t_restore_wait();
    t_restore_stall();
    t_restore_x();
    t_restore_button();
    t_restore_rekey();
    t_restore_rekey_stop();
    t_restore_equal();
    t_stop_link();
    t_stop_watch();
    t_stop_init_home();
    t_stop_unconfirmed();
    t_clear_fault_now();
    t_acks();
    t_custom();
    t_custom_others();
    t_custom_identity();
    t_custom_held_abort();
    t_txn_refused();
    t_btn_custom();
    t_ev_back();
    t_pair_late();
    t_step_bound();
    printf("session scriptée : %d vérifications, %d échec(s)\n", checks, fails);
    return fails ? 1 : 0;
}
