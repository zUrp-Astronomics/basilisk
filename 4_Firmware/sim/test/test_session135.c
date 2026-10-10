/* SOURCE: la spec de l'atelier (SESSION, TRANSACTION, MOUVEMENT, RESTORING) ; traces/sy135-2026-09-20-astro-normal.txt
 * AUTHOR: engineer
 * DATE: 2026-09-27
 * STATUS: actif — joué par 4_Firmware/run.sh (job `firmware` de la CI), argument : le répertoire des traces (traces/)
 *
 * SESSION et TRANSACTION contre le faux 135 inchangé : le démarrage, les lignes au repos, MOUVEMENT et la boîte de
 * commandes, le verrou de XDETECT, le journal, RESTORING, les renvois, la perte et la reprise, la bague selon le
 * commutateur Custom de l'objectif.
 *
 * La session tourne telle quelle au-dessus de la PHY simulée (sim/phy_sim.c) et du faux 135
 * (sim/lens135.c), inchangés. Pour voir ce qu'elle fait sur le fil sans toucher à la PHY simulée, le
 * programme est lié avec --wrap (sim/test/programmes.sh) : bsk_phy_send, bsk_phy_body_cs, bsk_phy_rail,
 * bsk_phy_vd, bsk_phy_lines et bsk_phy_poll passent par les __wrap_ ci-dessous, qui notent puis appellent la vraie ;
 * après chaque commande, la règle des lignes est vérifiée sur l'état que la PHY simulée en note (phy_sim_wires).
 *
 * Ce qui est comparé aux traces (traces/sy135-2026-09-20-astro-normal.txt, lignes citées dans
 * TRACE_TX) : le message des requêtes d'init, type et corps ; ni la séquence ni la somme. Écarts
 * attendus et déclarés : le 0x3F, absent des traces ; aucune trame 0x40 (décision de l'humain) là où la trace
 * montre 'V' (:258), 'F' 0x32 et 'M' ; l'octet 1 du 0x08, 06 au lieu du 02 de la trace (décision de l'humain :
 * le bit 0x04 d'un boîtier moderne en plus ; :228). */
#define _POSIX_C_SOURCE 200809L   /* alarm, la garde de durée */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bsk_bench_core.h"
#include "bsk_journal.h"
#include "bsk_session.h"
#include "frame.h"
#include "jdecode.h"
#include "lens_sim_135.h"
#include "phy_sim.h"
#include "store_sim.h"

#define MS 1000u
#define DROP (2 * MS)   /* les 2 ms de la retombée, écrites à la main (un mutant de D2_DROP_US rougit) */
#define T0 (1000u * MS)

static int checks, fails;

/* Une machine qui ne rend plus la main est un échec, pas un plantage : la garde de durée le dit comme
 * une vérification (sortie 1), et mutants.sh le compte tué. */
/* La garde de durée, en secondes : 10 ; la passe sous sanitizers de 4_Firmware/run.sh, qui ralentissent le programme
 * (16 s sous ASan, audit R6), la relève (-DGARDE_S). */
#ifndef GARDE_S
#define GARDE_S 10
#endif
#define TEXTE_(x) #x
#define TEXTE(x) TEXTE_(x)

static void too_long(int sig)
{
    static const char m[] = "  ECHEC : le programme ne termine pas en " TEXTE(GARDE_S) " s (boucle sans fin)\n";
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

/* ─────────────────────────── ce que la carte fait sur le fil ─────────────────────────── */

typedef enum { A_SEND, A_BODY_CS, A_RAIL, A_VD, A_RX, A_LINES } act_kind_t;
typedef struct {
    act_kind_t  kind;
    uint64_t    t;
    bsk_frame_t frame;   /* A_SEND : émise ; A_RX : reçue */
    bool        on;      /* A_BODY_CS, A_RAIL, A_LINES (true : pilotées) */
    bsk_rail_t  rail;
    uint16_t    hz;
    bool        refused; /* A_RAIL, A_VD, A_LINES : refusée par le verrou de XDETECT */
} act_t;

#define LOG_CAP 200000u
static act_t LOG[LOG_CAP];
static size_t n_log;

static void note(act_t a)
{
    if (a.kind != A_RX) a.t = phy_sim_now();   /* reçue : l'instant de l'événement de PHY */
    if (n_log < LOG_CAP) LOG[n_log] = a;
    n_log++;
}

/* La règle des lignes : une ligne émise vers l'objectif (TXD, BODY_CS, VD) n'est pilotée, à aucun
 * niveau, que les deux rails établis — rail logique allumé depuis 100 ms, rail moteur depuis 50 ms (S_RAIL_LOGIC,
 * S_RAIL_MOTOR) ; hors de là, elle est au repos (haute impédance, tirée bas) : au démarrage, en OFF, pendant la
 * coupure de RECOVERING, chaque fois que les rails sont coupés. Et une trame n'est émise que TXD routée. Vérifiée
 * après chaque commande de la session à la PHY, dans chaque test ; une faute est un échec. */
#define LOGIC_UP_US (100u * MS)
#define MOTOR_UP_US (50u * MS)

static unsigned wire_faults;

static bool rest_all(void)
{
    const phy_sim_wires_t *w = phy_sim_wires();
    return w->txd == PHY_SIM_REST && w->body_cs == PHY_SIM_REST && w->vd == PHY_SIM_REST;
}

static bool rails_up(void)
{
    const phy_sim_wires_t *w = phy_sim_wires();
    uint64_t now = phy_sim_now();
    return w->rail[BSK_RAIL_LOGIC] && w->rail[BSK_RAIL_MOTOR] && now >= w->rail_t[BSK_RAIL_LOGIC] + LOGIC_UP_US &&
           now >= w->rail_t[BSK_RAIL_MOTOR] + MOTOR_UP_US;
}

static void wires_check(const char *after)
{
    if (rest_all() || rails_up()) return;
    wire_faults++;
    CHECK(0, "ligne pilotée sans les rails établis, à %llu µs, après %s", (unsigned long long)phy_sim_now(), after);
}

bsk_err_t __real_bsk_phy_send(const bsk_frame_t *f);
bsk_err_t __wrap_bsk_phy_send(const bsk_frame_t *f);
void __real_bsk_phy_body_cs(bool high);
void __wrap_bsk_phy_body_cs(bool high);
bool __real_bsk_phy_rail(bsk_rail_t rail, bool on);
bool __wrap_bsk_phy_rail(bsk_rail_t rail, bool on);
bool __real_bsk_phy_vd(uint16_t hz);
bool __wrap_bsk_phy_vd(uint16_t hz);
bool __real_bsk_phy_lines(bool drive);
bool __wrap_bsk_phy_lines(bool drive);
bool __real_bsk_phy_poll(bsk_phy_event_t *ev);
bool __wrap_bsk_phy_poll(bsk_phy_event_t *ev);

bsk_err_t __wrap_bsk_phy_send(const bsk_frame_t *f)
{
    note((act_t){.kind = A_SEND, .frame = *f});
    if (phy_sim_wires()->txd == PHY_SIM_REST) {
        wire_faults++;
        CHECK(0, "trame 0x%02X émise TXD non routée, à %llu µs", f->msg[0], (unsigned long long)phy_sim_now());
    }
    return __real_bsk_phy_send(f);
}
void __wrap_bsk_phy_body_cs(bool high)
{
    note((act_t){.kind = A_BODY_CS, .on = high});
    __real_bsk_phy_body_cs(high);
    wires_check("BODY_CS");
}
/* Chaque commande est notée avec ce que la PHY en a fait (refused : le verrou de XDETECT l'a refusée). */
bool __wrap_bsk_phy_rail(bsk_rail_t rail, bool on)
{
    bool ok = __real_bsk_phy_rail(rail, on);
    note((act_t){.kind = A_RAIL, .rail = rail, .on = on, .refused = !ok});
    wires_check(on ? "un rail allumé" : "un rail coupé");
    return ok;
}
bool __wrap_bsk_phy_vd(uint16_t hz)
{
    bool ok = __real_bsk_phy_vd(hz);
    note((act_t){.kind = A_VD, .hz = hz, .refused = !ok});
    wires_check("la VD");
    return ok;
}
bool __wrap_bsk_phy_lines(bool drive)
{
    bool ok = __real_bsk_phy_lines(drive);
    note((act_t){.kind = A_LINES, .on = drive, .refused = !ok});
    wires_check(drive ? "les lignes pilotées" : "les lignes relâchées");
    return ok;
}

/* Les fronts de VD remontés par la PHY : aucun pendant la coupure de RECOVERING. */
static uint64_t VD_EDGES[65536];
static size_t n_vd;

bool __wrap_bsk_phy_poll(bsk_phy_event_t *ev)
{
    bool got = __real_bsk_phy_poll(ev);
    if (got && ev->kind == BSK_PHY_FRAME) note((act_t){.kind = A_RX, .t = ev->t_us, .frame = ev->u.frame});
    if (got && ev->kind == BSK_PHY_VD && n_vd < sizeof VD_EDGES / sizeof VD_EDGES[0]) VD_EDGES[n_vd++] = ev->t_us;
    return got;
}

/* ─────────────────────────── une trame perdue ou fausse ─────────────────────────── */

/* Les règles du crochet de la PHY simulée (phy_sim_fault) : les `n` prochaines trames du sens `to_lens` qui portent un
 * message de type `type` ont le sort `fate` ; `hit` en compte, `t_hit` note l'instant de la dernière. Une trame porte un
 * type par son premier message, par celui qui suit le 0x04 (le 0x1D accroché) ou par un accusé qui suit le 0x06
 * (offset 40 et suivants). Oubliées à chaque banc. */
typedef struct { bool to_lens; uint8_t type; unsigned n; phy_sim_fate_t fate; unsigned hit; uint64_t t_hit; } rule_t;
static rule_t RULES[8];
static size_t n_rules;

static bool carries(const bsk_frame_t *f, uint8_t type)
{
    if (f->msg[0] == type) return true;
    if (f->msg[0] == 0x04 && f->len > 14) return f->msg[14] == type;
    if (f->msg[0] == 0x06)
        for (size_t k = 40; k + 1 < f->len; k += 2)
            if (f->msg[k] == type) return true;
    return false;
}

static phy_sim_fate_t fault(bool to_lens, const bsk_frame_t *f)
{
    for (size_t i = 0; i < n_rules; i++) {
        rule_t *r = &RULES[i];
        if (r->to_lens == to_lens && r->n && carries(f, r->type)) {
            r->n--;
            r->hit++;
            r->t_hit = phy_sim_now();
            return r->fate;
        }
    }
    return PHY_SIM_PASS;
}

/* Une règle de plus ; la rend. */
static rule_t *lose(bool to_lens, uint8_t type, unsigned n, phy_sim_fate_t fate)
{
    RULES[n_rules] = (rule_t){.to_lens = to_lens, .type = type, .n = n, .fate = fate};
    phy_sim_fault(fault);
    return &RULES[n_rules++];
}

/* ─────────────────────────── le banc ─────────────────────────── */

static l135_t L;
static l135_params_t LP;
static bsk_session_params_t P;
static uint32_t refused0;

typedef struct { uint64_t t; uint8_t state; } chg_t;
static chg_t chg[512];
static size_t n_chg;
static uint8_t last_state;
static chg_t mchg[512];     /* les changements de motion_state */
static size_t n_mchg;
static uint8_t last_mv;

/* Les accusés, horodatés à leur lecture (après chaque pas, et après chaque dépôt). */
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

/* phy_sim_next ne compte pas l'anti-rebond de D2 : le banc le connaît, il l'a posé (d2()). */
static uint64_t d2_due;

static void d2(bool present)
{
    phy_sim_d2(present);
    d2_due = phy_sim_now() + D2_DEBOUNCE_US;
}

/* Après un pas ou un dépôt : les accusés lus, les changements d'état notés. */
static void observe(void)
{
    bsk_ack_t a;
    bsk_status_t st = status();
    uint64_t t = phy_sim_now();
    while (bsk_session_ack(&a))
        if (n_acks < sizeof ACKS / sizeof ACKS[0]) ACKS[n_acks++] = (ack_rec_t){a, t};
    if (st.session_state != last_state && n_chg < sizeof chg / sizeof chg[0]) chg[n_chg++] = (chg_t){t, st.session_state};
    last_state = st.session_state;
    if (st.motion_state != last_mv && n_mchg < sizeof mchg / sizeof mchg[0]) mchg[n_mchg++] = (chg_t){t, st.motion_state};
    last_mv = st.motion_state;
}

static void run_to(uint64_t t_end)
{
    uint64_t prev = UINT64_MAX;
    unsigned same = 0;
    for (;;) {
        uint64_t t = phy_sim_next(), ts = bsk_session_next();
        if (ts < t) t = ts;
        if (d2_due > phy_sim_now() && d2_due < t) t = d2_due;
        if (t > t_end) t = t_end;
        phy_sim_run(t);
        bsk_session_step(t);
        observe();
        if (t >= t_end) break;
        same = t == prev ? same + 1 : 0;
        prev = t;
        if (same > 100000) {   /* le temps n'avance plus : une échéance jamais traitée */
            CHECK(0, "le temps n'avance plus à %llu µs", (unsigned long long)t);
            break;
        }
    }
}

static void run_for(uint64_t us) { run_to(phy_sim_now() + us); }

static uint64_t entered(uint8_t state, uint64_t after)
{
    for (size_t i = 0; i < n_chg; i++)
        if (chg[i].state == state && chg[i].t >= after) return chg[i].t;
    return 0;
}

/* La même chose pour MOUVEMENT. */
static uint64_t mv_entered(uint8_t state, uint64_t after)
{
    for (size_t i = 0; i < n_mchg; i++)
        if (mchg[i].state == state && mchg[i].t >= after) return mchg[i].t;
    return 0;
}

/* Le 135 hors tension, D2 absent, horloge à 0 ; la session en OFF. */
static void bench(void)
{
    l135_params_default(&LP);
    l135_init(&L, &LP);
    phy_sim_init(lens_sim_135(&L), 0);
    bsk_session_params_default(&P);
    store_sim_reset();                              /* aucune marque rangée */
    bsk_session_init(&P);
    n_rules = 0;                                    /* aucune trame perdue */
    n_log = 0;
    n_chg = 0;
    n_mchg = 0;
    n_acks = 0;
    n_vd = 0;
    d2_due = 0;
    last_state = SESSION_OFF;
    last_mv = MOTION_IDLE;
    refused0 = bsk_bench_refused();
    run_to(T0);
}

/* L'objectif sous tension et sa monture fermée, à l'instant courant. */
static void mount(void)
{
    l135_power(&L, phy_sim_now(), true);
    d2(true);
}

/* Dépose une commande à l'instant courant ; rend son seq. */
static uint32_t cmd(bsk_cmd_op_t op, int32_t arg)
{
    bsk_cmd_t c = {.seq = ++seq_n, .op = op, .arg = arg};
    bsk_session_command(&c);
    observe();
    return c.seq;
}

/* Le k-ième accusé de `seq` (0 : le premier), NULL s'il n'existe pas. */
static const ack_rec_t *ack_of(uint32_t seq, unsigned k)
{
    for (size_t i = 0; i < n_acks; i++)
        if (ACKS[i].a.seq == seq && k-- == 0) return &ACKS[i];
    return NULL;
}

static unsigned acks_of(uint32_t seq)
{
    unsigned n = 0;
    for (size_t i = 0; i < n_acks; i++) n += ACKS[i].a.seq == seq;
    return n;
}

/* `seq` a reçu exactement ACK_REJECTED avec `why`. */
static bool rejected(uint32_t seq, bsk_err_t why)
{
    const ack_rec_t *a = ack_of(seq, 0);
    return acks_of(seq) == 1 && a->a.result == ACK_REJECTED && a->a.reason == why;
}

/* `seq` a reçu ACK_ACCEPTED puis `r` avec `why`, rien d'autre ; `t` : l'instant du final (NULL : sans objet). */
static bool accepted_then(uint32_t seq, bsk_ack_result_t r, bsk_err_t why, uint64_t *t)
{
    const ack_rec_t *a = ack_of(seq, 0), *b = ack_of(seq, 1);
    if (acks_of(seq) != 2 || a->a.result != ACK_ACCEPTED || b->a.result != r || b->a.reason != why) return false;
    if (t) *t = b->t;
    return true;
}

/* Les états de MOUVEMENT publiés à partir du changement `from`, exactement `n`, dans l'ordre. */
static bool motion_seq(size_t from, const uint8_t *st, size_t n)
{
    if (n_mchg - from != n) return false;
    for (size_t i = 0; i < n; i++)
        if (mchg[from + i].state != st[i]) return false;
    return true;
}

static long find(size_t from, act_kind_t kind, uint8_t cls, uint8_t type)
{
    for (size_t i = from; i < n_log && i < LOG_CAP; i++)
        if (LOG[i].kind == kind && (kind != A_SEND || LOG[i].frame.cls == cls) &&
            ((kind != A_SEND && kind != A_RX) || LOG[i].frame.msg[0] == type))
            return (long)i;
    return -1;
}

static size_t count_sent(uint64_t from, uint64_t to, uint8_t type)
{
    size_t n = 0;
    for (size_t i = 0; i < n_log && i < LOG_CAP; i++)
        if (LOG[i].kind == A_SEND && LOG[i].t >= from && LOG[i].t < to && LOG[i].frame.msg[0] == type) n++;
    return n;
}

static size_t count_sent_from(size_t from, uint8_t type)
{
    size_t n = 0;
    for (size_t i = from; i < n_log && i < LOG_CAP; i++) n += LOG[i].kind == A_SEND && LOG[i].frame.msg[0] == type;
    return n;
}

/* Les trames 0x04 + 0x1D émises depuis l'entrée `from` du journal ; la dernière dans `m1d`. */
static size_t count_1d(size_t from, uint8_t *m1d)
{
    size_t n = 0;
    for (size_t i = from; i < n_log && i < LOG_CAP; i++)
        if (LOG[i].kind == A_SEND && LOG[i].frame.msg[0] == 0x04 && LOG[i].frame.len == 19 && LOG[i].frame.msg[14] == 0x1D) {
            if (m1d) memcpy(m1d, LOG[i].frame.msg + 14, 5);
            n++;
        }
    return n;
}

static size_t count_af(size_t from)
{
    size_t n = 0;
    for (size_t i = from; i < n_log && i < LOG_CAP; i++)
        n += LOG[i].kind == A_SEND && LOG[i].frame.msg[0] == 0x03 && (LOG[i].frame.msg[13] & 0x10);
    return n;
}

/* Rien fait sur le fil depuis l'entrée `from` du journal : ni trame, ni BODY_CS, ni rail, ni VD. */
static bool quiet_from(size_t from)
{
    for (size_t i = from; i < n_log && i < LOG_CAP; i++)
        if (LOG[i].kind != A_RX) return false;
    return true;
}

/* Les 0x03 émis depuis l'entrée `from` portent tous la consigne d'ouverture `code` aux offsets 3-6,
 * petit-boutiste, deux fois ; au moins un 0x03. */
static bool ap03_all(size_t from, uint16_t code)
{
    size_t n = 0;
    for (size_t i = from; i < n_log && i < LOG_CAP; i++) {
        const bsk_frame_t *f = &LOG[i].frame;
        if (LOG[i].kind != A_SEND || f->msg[0] != 0x03) continue;
        if (f->msg[4] != (uint8_t)code || f->msg[5] != code >> 8 || f->msg[6] != (uint8_t)code || f->msg[7] != code >> 8)
            return false;
        n++;
    }
    return n > 0;
}

static bool any_40(void)
{
    for (size_t i = 0; i < n_log && i < LOG_CAP; i++)
        if (LOG[i].kind == A_SEND && LOG[i].frame.msg[0] == 0x40) return true;
    return false;
}

static bool lens_clean(void)
{
    for (int t = 0; t < 256; t++)
        if (l135_unmodelled(&L, (uint8_t)t)) return false;
    return !l135_out_of_model(&L) && !l135_blocked(&L);
}

/* ─────────────────────────── les traces ─────────────────────────── */

/* astro-normal.txt : les requêtes d'init de la trace, dans l'ordre */
static const struct { int line; uint8_t type; } TRACE_TX[] = {
    {222, 0x01}, {225, 0x07}, {228, 0x08}, {234, 0x0B}, {236, 0x09}, {238, 0x0D}, {241, 0x10}, {254, 0x0A},
};
#define N_TRACE_TX (sizeof TRACE_TX / sizeof TRACE_TX[0])

/* Le message de la trame émise sur la ligne `line` (« … * tx <ms> F0 … 55 »). */
static bool trace_msg(const char *dir, int line, bsk_frame_t *f)
{
    char path[512], buf[2048];
    uint8_t b[BSK_FRAME_MAX];
    FILE *fp;
    int n = 0;
    const char *p;
    size_t k;
    snprintf(path, sizeof path, "%s/sy135-2026-09-20-astro-normal.txt", dir);
    fp = fopen(path, "r");
    if (!fp) return false;
    while (fgets(buf, sizeof buf, fp) && ++n < line) {}
    fclose(fp);
    if (n != line) return false;
    buf[strcspn(buf, "\r\n")] = 0;
    p = strstr(buf, "* tx ");
    if (!p) return false;
    p = strchr(p + 5, ' ');
    if (!p) return false;
    k = fr_hex(p + 1, b, sizeof b);
    return k && fr_decode(b, k, f) == E_OK;
}

/* ─────────────────────────── les tests ─────────────────────────── */

/* Les bornes que publie le faux 135 : les offsets 7-10 de son 0x06, `31 36 12 78` (gabarit de dump05:11,
 * sim/sources135.c), 13873 et 30738 : 150 pas en dedans de la limite logicielle basse du 135 et 250 en dedans de la
 * haute (13723, 30988 : 7_Docs/E-Mount/samyang.md § 4.3). */
#define MODEL_MIN 13873
#define MODEL_MAX 30738
/* Décision de l'humain : les bornes appliquées, 5 pas en dedans de celles publiées. */
#define LIM_MIN (MODEL_MIN + 5)
#define LIM_MAX (MODEL_MAX - 5)

static void t_cold_start(const char *traces)
{
    size_t k = 0;
    long i, i06, i0a, i1c;
    bsk_status_t s;
    printf("démarrage à froid du faux 135 : READY, requêtes d'init comparées aux traces, aucune trame 0x40\n");
    bench();
    mount();
    run_for(4000 * MS);
    s = status();
    CHECK(s.session_state == SESSION_READY && s.last_error == E_OK, "READY (état %u)", s.session_state);
    for (size_t j = 0; j < n_log && j < LOG_CAP; j++) {
        bsk_frame_t tr;
        const bsk_frame_t *f = &LOG[j].frame;
        if (LOG[j].kind != A_SEND || f->cls != 2) continue;
        if (f->msg[0] == 0x3F) {
            CHECK(k == 2, "le 0x3F entre le 0x07 et le 0x08 (écart déclaré : absent des traces)");
            continue;
        }
        CHECK(k < N_TRACE_TX && f->msg[0] == TRACE_TX[k].type, "requête %zu : 0x%02X", k, f->msg[0]);
        if (k < N_TRACE_TX) {
            CHECK(trace_msg(traces, TRACE_TX[k].line, &tr), "astro-normal:%d lisible", TRACE_TX[k].line);
            if (f->msg[0] == 0x08)   /* décision de l'humain : écart déclaré, l'octet 1 du 0x08, 02 de la trace -> 06 */
                CHECK(tr.len == f->len && f->len > 2 && tr.msg[0] == 0x08 && tr.msg[1] == 0x02 && f->msg[1] == 0x06 &&
                          !memcmp(tr.msg + 2, f->msg + 2, f->len - 2u),
                      "0x08 : drapeaux 06 (astro-normal:%d, la v1 : 02), le reste identique (%02X)", TRACE_TX[k].line,
                      f->len > 1 ? f->msg[1] : 0xFFu);
            else
                CHECK(tr.len == f->len && !memcmp(tr.msg, f->msg, f->len), "0x%02X identique à astro-normal:%d, type et corps",
                      f->msg[0], TRACE_TX[k].line);
        }
        k++;
    }
    CHECK(k == N_TRACE_TX, "les huit requêtes de la trace, dans son ordre, et rien d'autre (%zu)", k);
    CHECK(!any_40(), "aucune trame 0x40");
    CHECK(lens_clean(), "aucun « non modélisé » : le faux 135 a tout servi");
    i06 = -1;
    for (i = find(0, A_RX, 0, 0x06); i >= 0; i = find((size_t)i + 1, A_RX, 0, 0x06)) i06 = i;
    CHECK(i06 >= 0 && s.position_valid &&
              s.focus_position == (int32_t)(LOG[i06].frame.msg[3] | LOG[i06].frame.msg[4] << 8),
          "position publiée : celle du dernier 0x06 reçu, offsets 2-3 (%ld)", (long)s.focus_position);
    CHECK(s.lens_id_product == 8 && !strcmp(s.lens_name, "SAMYANG AF 135mm F1.8"), "identité et nom 0x3F publiés");
    i0a = find(0, A_SEND, 2, 0x0A);
    i1c = find(0, A_SEND, 1, 0x1C);
    CHECK(i0a >= 0 && i1c < 0 && entered(SESSION_HOMING, 0) == 0,
          "init faite : READY sans homing du pilote (ni 0x1C, ni HOMING ; lensctl.c:232)");
    CHECK(s.focus_min == LIM_MIN && s.focus_max == LIM_MAX, "bornes : celles du 0x06 du faux, 5 pas en dedans, %d / %d",
          (int)s.focus_min, (int)s.focus_max);
    CHECK(ap03_all(0, 0x11BF), "chaque 0x03 porte BF 11 BF 11 aux offsets 3-6 : f/1,8 de chaque session (std.c:123, B2 11), "
          "ramenée au minimum de la plage du 0x08, BF 11");
    CHECK(bsk_bench_refused() == refused0, "aucune trame refusée par bench_core");
}

static void t_powered(void)
{
    long ihs, i01;
    printf("objectif resté alimenté : poignée de main sans réponse, 0x01 et la suite, bornes à l'init, READY\n");
    bench();
    l135_start_powered(&L, phy_sim_now(), true, false);
    d2(true);
    run_for(4000 * MS);
    ihs = find(0, A_BODY_CS, 0, 0);
    while (ihs >= 0 && !LOG[ihs].on) ihs = find((size_t)ihs + 1, A_BODY_CS, 0, 0);
    i01 = find(0, A_SEND, 2, 0x01);
    CHECK(ihs >= 0 && i01 >= 0 && LOG[i01].t == LOG[ihs].t + 500 * MS, "0x01 à l'échéance de la poignée de main");
    CHECK(find(0, A_RX, 0, 0x01) >= 0, "le 0x01 répond");
    CHECK(status().session_state == SESSION_READY, "READY");
    CHECK(status().focus_min == LIM_MIN && status().focus_max == LIM_MAX,
          "bornes du 0x06, les mêmes qu'à froid : %d / %d", (int)status().focus_min, (int)status().focus_max);
    CHECK(find(0, A_SEND, 1, 0x1C) < 0 && find(0, A_SEND, 2, 0x10) >= 0 &&
              find((size_t)find(0, A_SEND, 2, 0x10) + 1, A_SEND, 2, 0x10) < 0 && entered(SESSION_HOMING, 0) == 0,
          "à chaud : pas de second homing (ni 0x1C, ni 0x10 08, ni HOMING)");
    CHECK(!any_40() && lens_clean() && bsk_bench_refused() == refused0, "aucune trame 0x40, rien de refusé, rien hors modèle");
}

static void t_silent(void)
{
    uint64_t t = 0, tr;
    int n = 0;
    printf("objectif muet (silent), D2 présent : échecs comptés (E5), FAULT et E_LOST au 4e\n");
    bench();
    L.f.silent = true;
    mount();
    run_for(30000 * MS);
    for (tr = entered(SESSION_RECOVERING, t); tr; tr = entered(SESSION_RECOVERING, t)) {
        uint64_t tp = entered(SESSION_POWERING, tr);
        long ib;
        n++;
        CHECK(tp == tr + 3000 * MS, "échec %d : RECOVERING publié 3 s (sans coupure)", n);
        ib = find(0, A_BODY_CS, 0, 0);
        while (ib >= 0 && !(LOG[ib].t > tp && LOG[ib].on)) ib = find((size_t)ib + 1, A_BODY_CS, 0, 0);
        CHECK(ib >= 0 && LOG[ib].t == tp + 115 * MS, "échec %d : puis POWERING, et la poignée de main ré-émise", n);
        t = tp;
    }
    CHECK(n == 3, "trois reprises (%d)", n);
    CHECK(status().session_state == SESSION_FAULT && status().last_error == E_LOST, "au 4e échec : FAULT, E_LOST");
    CHECK(count_sent(0, phy_sim_now(), 0x01) == 24 && count_sent(0, phy_sim_now(), 0x0A) == 4,
          "quatre essais : six 0x01 et le 0x0A du reset soft chacun");
    CHECK(bsk_bench_refused() == refused0, "aucune trame refusée par bench_core");
}

static void t_loss(void)
{
    uint64_t ts, tr, tlast = 0;
    long i;
    printf("silence en READY : perte à 2 s (E6), RECOVERING, nouvel essai\n");
    bench();
    mount();
    run_for(4000 * MS);
    CHECK(status().session_state == SESSION_READY, "READY");
    ts = phy_sim_now();
    L.f.silent = true;
    run_for(8000 * MS);
    for (i = find(0, A_RX, 0, 0x05); i >= 0; i = find((size_t)i + 1, A_RX, 0, 0x05)) if (LOG[i].t > tlast) tlast = LOG[i].t;
    for (i = find(0, A_RX, 0, 0x06); i >= 0; i = find((size_t)i + 1, A_RX, 0, 0x06)) if (LOG[i].t > tlast) tlast = LOG[i].t;
    tr = entered(SESSION_RECOVERING, ts);
    CHECK(tr == tlast + 2000 * MS + 1,
          "RECOVERING 2 s après la dernière télémétrie (%lld µs)", (long long)(tr - tlast));
    CHECK(entered(SESSION_POWERING, tr) == tr + 3000 * MS, "puis POWERING, 3 s après (sans coupure)");
    CHECK(count_sent(tr + 3000 * MS, phy_sim_now(), 0x01) >= 1, "nouvel essai : 0x01");
    CHECK(bsk_bench_refused() == refused0, "aucune trame refusée par bench_core");
}

static void t_d2_during_10(void)
{
    long i10;
    uint64_t t10, toff;
    printf("D2 retombe pendant le 0x10 de l'init : OFF 2 ms après\n");
    bench();
    mount();
    for (i10 = -1; i10 < 0 && phy_sim_now() < T0 + 3000 * MS; i10 = find(0, A_SEND, 2, 0x10)) run_for(1 * MS);
    CHECK(i10 >= 0, "0x10 émis");
    t10 = phy_sim_now();
    d2(false);
    run_for(3000 * MS);
    toff = entered(SESSION_OFF, t10);
    CHECK(toff == t10 + DROP, "OFF à la coupure, 2 ms après la retombée de D2");
    CHECK(count_sent(toff, phy_sim_now(), 0x0A) == 0 && count_sent(toff, phy_sim_now(), 0x03) == 0,
          "plus aucune trame : ni 0x0A, ni boucle");
    CHECK(status().session_state == SESSION_OFF && !status().position_valid && status().lens_id_product == 0,
          "OFF, session oubliée");
    {
        bool rail_m = false, rail_l = false, vd0 = false;
        for (size_t j = 0; j < n_log && j < LOG_CAP; j++) {
            if (LOG[j].t != toff) continue;
            if (LOG[j].kind == A_RAIL && !LOG[j].on && LOG[j].rail == BSK_RAIL_MOTOR) rail_m = true;
            if (LOG[j].kind == A_RAIL && !LOG[j].on && LOG[j].rail == BSK_RAIL_LOGIC) rail_l = true;
            if (LOG[j].kind == A_VD && LOG[j].hz == 0) vd0 = true;
        }
        CHECK(rail_m && rail_l && vd0, "rails coupés, VD arrêtée");
    }
    CHECK(bsk_bench_refused() == refused0, "aucune trame refusée par bench_core");
}

/* Un faux 135 démarré à froid, READY. */
static void ready(void)
{
    bench();
    mount();
    run_for(4000 * MS);
    CHECK(status().session_state == SESSION_READY, "READY");
}

/* ─────────────────────────── les lignes au repos ─────────────────────────── */

/* Le premier `kind` de l'entrée `from` du journal à l'instant t au plus tôt, avec `on` s'il compte ; -1 sinon. */
static long act_from(size_t from, act_kind_t kind, uint64_t t, int on)
{
    for (size_t i = from; i < n_log && i < LOG_CAP; i++)
        if (LOG[i].kind == kind && LOG[i].t >= t && (on < 0 || LOG[i].on == (on != 0))) return (long)i;
    return -1;
}

static size_t vd_edges(uint64_t from, uint64_t to)   /* fronts de VD dans ]from, to] */
{
    size_t n = 0;
    for (size_t i = 0; i < n_vd; i++) n += VD_EDGES[i] > from && VD_EDGES[i] <= to;
    return n;
}

static bool rails_off(void)
{
    const phy_sim_wires_t *w = phy_sim_wires();
    return !w->rail[BSK_RAIL_LOGIC] && !w->rail[BSK_RAIL_MOTOR];
}

static void t_lines_boot(void)
{
    const phy_sim_wires_t *w = phy_sim_wires();
    uint64_t tp;
    long il, iv, is;
    printf("démarrage : lignes au repos jusqu'aux rails établis, puis la VD, les lignes, BODY_CS basse ; TXD routée avant le 0x01\n");
    bench();
    CHECK(status().session_state == SESSION_OFF && rest_all() && rails_off(),
          "OFF au démarrage de la carte : TXD, BODY_CS et VD au repos, rails coupés");
    mount();
    run_for(4000 * MS);
    CHECK(status().session_state == SESSION_READY, "READY");
    tp = entered(SESSION_POWERING, 0);
    il = act_from(0, A_LINES, 0, -1);
    CHECK(il >= 0 && LOG[il].on && LOG[il].t == tp + LOGIC_UP_US,
          "lignes pilotées 100 ms après POWERING : rail logique, 50 ms, rail moteur, 50 ms");
    iv = act_from(0, A_VD, 0, -1);
    CHECK(iv >= 0 && iv < il && LOG[iv].hz == 60 && LOG[iv].t == LOG[il].t && il + 1 < (long)n_log &&
              LOG[il + 1].kind == A_BODY_CS && !LOG[il + 1].on,
          "au même instant, l'ordre de S_RAIL_MOTOR : la VD, les lignes, puis BODY_CS basse");
    is = act_from(0, A_SEND, 0, -1);
    CHECK(is > il && LOG[is].frame.msg[0] == 0x01, "TXD routée avant la première trame, le 0x01");
    CHECK(vd_edges(0, tp + LOGIC_UP_US) == 0, "aucun front de VD avant les rails établis");
    CHECK(w->txd == PHY_SIM_HIGH && w->body_cs == PHY_SIM_LOW && w->vd == PHY_SIM_HIGH,
          "READY : TXD haute (UART au repos), BODY_CS basse, VD en marche");
}

static void t_lines_off(void)
{
    uint64_t toff;
    printf("OFF : lignes au repos, D2 qui retombe puis p0 (CMD_DETACH)\n");
    ready();
    d2(false);
    run_for(400 * MS);
    toff = entered(SESSION_OFF, 0);
    CHECK(status().session_state == SESSION_OFF && rest_all() && rails_off(),
          "D2 retombé : OFF, TXD, BODY_CS et VD au repos, rails coupés");
    CHECK(act_from(0, A_LINES, toff, 0) >= 0 && LOG[act_from(0, A_LINES, toff, 0)].t == toff, "lignes relâchées à l'entrée en OFF");
    run_for(5000 * MS);
    CHECK(rest_all() && rails_off() && vd_edges(toff, phy_sim_now()) == 0, "5 s plus tard : toujours au repos, aucun front de VD");
    ready();
    CHECK(!rest_all(), "de nouveau READY : lignes pilotées");
    toff = phy_sim_now();
    CHECK(accepted_then(cmd(CMD_DETACH, 0), ACK_COMPLETED, E_OK, NULL) && status().session_state == SESSION_OFF,
          "p0 (CMD_DETACH) : OFF");
    CHECK(rest_all() && rails_off(), "p0 : TXD, BODY_CS et VD au repos, rails coupés");
    run_for(5000 * MS);
    CHECK(rest_all() && rails_off() && vd_edges(toff, phy_sim_now()) == 0, "5 s plus tard, D2 présent : toujours au repos");
}

/* Décision de l'humain : la coupure désactivée par défaut. La reprise attend 3 s puis repart de
 * POWERING : ni rail coupé, ni ligne au repos, ni VD arrêtée, à aucun instant. */
static void t_lines_recovering(void)
{
    uint64_t tr, tp;
    size_t from;
    printf("RECOVERING, coupure désactivée (défaut) : lignes pilotées, VD en marche, rails jamais coupés, POWERING 3 s après\n");
    bench();
    L.f.silent = true;
    mount();
    while (status().session_state != SESSION_RECOVERING && phy_sim_now() < T0 + 5000 * MS) run_for(1 * MS);
    tr = entered(SESSION_RECOVERING, 0);
    from = n_log;
    run_to(tr + P.retry_wait_ms * MS + 500 * MS);
    tp = entered(SESSION_POWERING, tr);
    CHECK(tr != 0 && tp == tr + P.retry_wait_ms * MS, "POWERING 3 s après RECOVERING");
    CHECK(act_from(from, A_RAIL, 0, 0) < 0 && act_from(from, A_LINES, 0, 0) < 0, "aucun rail coupé, aucune ligne mise au repos");
    for (long i = act_from(from, A_VD, 0, -1); i >= 0; i = act_from((size_t)i + 1, A_VD, 0, -1))
        CHECK(LOG[i].hz == 60, "la VD jamais arrêtée");
    CHECK(vd_edges(tr, tp + LOGIC_UP_US) > 0 && !rest_all() && rails_up() && phy_sim_wires()->vd == PHY_SIM_HIGH,
          "la VD tourne pendant la reprise, lignes pilotées, rails établis");
}

/* La coupure activée (cut_rails) : la VD est arrêtée pendant la coupure, et
 * relancée au POWERING suivant, après les rails. La coupure commence 3 s après l'entrée en RECOVERING (retry_wait_ms) et
 * dure 500 ms (cut_ms). */
static void t_lines_recovering_cut(void)
{
    uint64_t tr, tcut, tp;
    long iv, il;
    printf("RECOVERING, coupure activée : lignes et VD au repos pendant la coupure, pilotées et VD relancée au POWERING suivant\n");
    bench();
    P.cut_rails = true;
    bsk_session_init(&P);
    L.f.silent = true;
    mount();
    while (status().session_state != SESSION_RECOVERING && phy_sim_now() < T0 + 5000 * MS) run_for(1 * MS);
    tr = entered(SESSION_RECOVERING, 0);
    CHECK(tr != 0 && !rest_all(), "objectif muet : RECOVERING, lignes encore pilotées pendant l'attente");
    tcut = tr + P.retry_wait_ms * MS;
    run_to(tcut);
    CHECK(rest_all() && rails_off() && phy_sim_wires()->vd == PHY_SIM_REST,
          "début de la coupure : rails coupés, TXD, BODY_CS au repos, VD arrêtée (au repos)");
    run_to(tcut + P.cut_ms * MS / 2);
    CHECK(rest_all() && rails_off(), "au milieu de la coupure : toujours au repos");
    run_to(tcut + P.cut_ms * MS + 200 * MS);
    tp = entered(SESSION_POWERING, tr);
    CHECK(tp == tcut + P.cut_ms * MS, "POWERING à la fin de la coupure");
    iv = act_from(0, A_VD, tp, -1);
    il = act_from(0, A_LINES, tp, 1);
    CHECK(iv >= 0 && LOG[iv].hz == 60 && LOG[iv].t == tp + LOGIC_UP_US && il > iv && LOG[il].t == LOG[iv].t,
          "VD relancée et lignes pilotées 100 ms après POWERING, rails établis");
    CHECK(vd_edges(tcut, tp + LOGIC_UP_US) == 0 && vd_edges(tp + LOGIC_UP_US, phy_sim_now()) > 0,
          "aucun front de VD de la coupure aux rails établis, puis la VD tourne");
    CHECK(!rest_all() && phy_sim_wires()->vd == PHY_SIM_HIGH, "lignes pilotées, VD en marche");
}

/* ─────────────────────────── MOUVEMENT et la boîte de commandes ─────────────────────────── */

static const uint8_t MV_FULL[] = {MOTION_COMMANDED, MOTION_MOVING, MOTION_SETTLING, MOTION_ARRIVED};

static void t_goto(void)
{
    static const uint8_t m1d_ok[] = {0x1D, 0x20, 0x4E, 0x00, 0x00};
    uint8_t m1d[5] = {0};
    size_t from, m0;
    uint32_t q;
    printf("goto dans la plage : 0x1D sur le 0x04, bit AF 30 paires, COMMANDED, MOVING, SETTLING, ARRIVED\n");
    ready();
    from = n_log;
    m0 = n_mchg;
    q = cmd(CMD_FOCUS_GOTO, 20000);
    CHECK(ack_of(q, 0) && ack_of(q, 0)->a.result == ACK_ACCEPTED && status().motion_state == MOTION_COMMANDED,
          "ACK_ACCEPTED, COMMANDED au dépôt");
    run_for(3000 * MS);
    CHECK(count_1d(from, m1d) == 1 && !memcmp(m1d, m1d_ok, 5), "un 0x1D, 1D 20 4E 00 00, accroché au 0x04");
    CHECK(count_af(from) == 30, "bit AF sur 30 paires (%zu)", count_af(from));
    CHECK(motion_seq(m0, MV_FULL, 4), "COMMANDED, MOVING, SETTLING, ARRIVED (%zu changements)", n_mchg - m0);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL), "ACK_COMPLETED");
    CHECK(status().focus_position == 20000 && status().focus_position == l135_position(&L) && status().position_valid,
          "position lue = cible (%d)", (int)status().focus_position);
    CHECK(status().session_state == SESSION_READY && !any_40() && lens_clean() && bsk_bench_refused() == refused0,
          "READY ; ni 0x40, ni refus, ni hors modèle");
}

/* Décision de l'humain : la paire à la phase du NEX-7 (7_Docs/E-Mount/protocol.md § 6.2 : 8,59 et 10,12 ms sur une
 * capture publiée), le 0x03 à t + 8,6 ms et le 0x04 à t + 10,1 ms après chaque front VD à t, à la microseconde au banc
 * (le banc avance aux instants que la session demande, bsk_session_next), une paire par front, un seul numéro, rien entre
 * eux. Pendant un goto aussi : le 0x1D derrière le 0x04, sous le numéro du 0x03. */
#define PAIR03 8600u
#define PAIR04 10100u

static void t_pair_phase(void)
{
    uint64_t t0;
    size_t pairs = 0, bad = 0, n1d = 0;
    printf("la paire 0x03/0x04 : 0x03 à t + 8,6 ms, 0x04 à t + 10,1 ms après le front VD à t, rien entre eux\n");
    ready();
    t0 = phy_sim_now();
    (void)cmd(CMD_FOCUS_GOTO, 20000);
    run_for(1000 * MS);
    for (size_t v = 0; v + 1 < n_vd; v++) {
        long i03, i04;
        if (VD_EDGES[v + 1] <= t0) continue;     /* la trame du dépôt comprise */
        i03 = -1;
        for (size_t i = 0; i < n_log && i < LOG_CAP; i++)
            if (LOG[i].kind == A_SEND && LOG[i].t >= VD_EDGES[v] && LOG[i].t < VD_EDGES[v + 1] && LOG[i].frame.msg[0] == 0x03) {
                i03 = (long)i;
                break;
            }
        i04 = i03 >= 0 && (size_t)i03 + 1 < n_log ? i03 + 1 : -1;
        pairs++;
        if (i03 < 0 || i04 < 0 || LOG[i04].kind != A_SEND || LOG[i04].frame.msg[0] != 0x04 ||
            LOG[i03].t != VD_EDGES[v] + PAIR03 || LOG[i04].t != VD_EDGES[v] + PAIR04 ||
            LOG[i03].frame.seq != LOG[i04].frame.seq || count_sent(VD_EDGES[v], VD_EDGES[v + 1], 0x03) != 1 ||
            count_sent(VD_EDGES[v], VD_EDGES[v + 1], 0x04) != 1) {
            if (!bad++)
                printf("    front %llu µs : 0x03 à %lld, 0x04 à %lld\n", (unsigned long long)VD_EDGES[v],
                       i03 >= 0 ? (long long)(LOG[i03].t - VD_EDGES[v]) : -1LL,
                       i04 >= 0 ? (long long)(LOG[i04].t - VD_EDGES[v]) : -1LL);
            continue;
        }
        for (long i = i03 + 1; i < i04; i++) bad += LOG[i].kind == A_SEND;
        n1d += LOG[i04].frame.len == 19 && LOG[i04].frame.msg[14] == 0x1D;
    }
    CHECK(pairs >= 59 && bad == 0, "%zu fronts, chacun sa paire à +8600 et +10100 µs, un numéro, rien entre ; %zu écart(s)",
          pairs, bad);
    CHECK(n1d == 1, "le 0x1D du goto derrière un 0x04 de la boucle, à sa phase (%zu)", n1d);
    CHECK(status().focus_position == 20000 && lens_clean(), "le goto arrive (%d)", (int)status().focus_position);
}

static void t_goto_twice(void)
{
    static const uint8_t mv_settle[] = {MOTION_COMMANDED, MOTION_SETTLING, MOTION_ARRIVED};
    size_t m0, from;
    uint32_t q1, q2;
    printf("goto vers la borne basse deux fois : ARRIVED, puis COMMANDED -> SETTLING sur l'accusé 1D 00\n");
    ready();
    m0 = n_mchg;
    q1 = cmd(CMD_FOCUS_GOTO, LIM_MIN);
    run_for(3000 * MS);
    CHECK(accepted_then(q1, ACK_COMPLETED, E_OK, NULL) && motion_seq(m0, MV_FULL, 4), "le premier : ARRIVED");
    CHECK(status().focus_position == l135_position(&L) && status().focus_position == LIM_MIN,
          "à la borne basse (%d)", (int)status().focus_position);
    m0 = n_mchg;
    from = n_log;
    q2 = cmd(CMD_FOCUS_GOTO, LIM_MIN);
    run_for(3000 * MS);
    CHECK(count_1d(from, NULL) == 1, "le second 0x1D part");
    CHECK(motion_seq(m0, mv_settle, 3), "le second : COMMANDED, SETTLING, ARRIVED, sans MOVING ni STALLED (%zu)",
          n_mchg - m0);
    CHECK(accepted_then(q2, ACK_COMPLETED, E_OK, NULL), "ACK_COMPLETED");
}

/* Une cible hors des bornes appliquées est refusée avant émission, E_LIMIT ; les bornes appliquées, 5 pas en dedans
 * de celles que publie le 0x06, sont acceptées, celles publiées refusées. */
static void t_goto_limit(void)
{
    int32_t p;
    size_t from;
    uint32_t q;
    printf("cible hors des bornes du 0x06 resserrées de 5 pas : E_LIMIT, aucun 0x1D ; les deux bornes resserrées acceptées\n");
    ready();
    p = status().focus_position;
    from = n_log;
    CHECK(rejected(cmd(CMD_FOCUS_GOTO, MODEL_MIN), E_LIMIT), "goto à la borne basse publiée : E_LIMIT");
    CHECK(rejected(cmd(CMD_FOCUS_GOTO, MODEL_MAX), E_LIMIT), "goto à la borne haute publiée : E_LIMIT");
    CHECK(rejected(cmd(CMD_FOCUS_GOTO, LIM_MIN - 1), E_LIMIT), "goto borne basse + 4 : E_LIMIT");
    CHECK(rejected(cmd(CMD_FOCUS_GOTO, LIM_MAX + 1), E_LIMIT), "goto borne haute - 4 : E_LIMIT");
    CHECK(rejected(cmd(CMD_FOCUS_GOTO, 0), E_LIMIT) && rejected(cmd(CMD_FOCUS_GOTO, 65535), E_LIMIT), "0 et 65535 : E_LIMIT");
    CHECK(rejected(cmd(CMD_FOCUS_GOTO, -1), E_LIMIT), "goto -1 : E_LIMIT");
    CHECK(rejected(cmd(CMD_FOCUS_GOTO, 65536), E_LIMIT), "goto 65536 : E_LIMIT");
    CHECK(rejected(cmd(CMD_FOCUS_MOVE, LIM_MIN - 1 - p), E_LIMIT), "relatif sous la borne basse : E_LIMIT");
    CHECK(rejected(cmd(CMD_FOCUS_MOVE, LIM_MAX + 1 - p), E_LIMIT), "relatif au-delà de la borne haute : E_LIMIT");
    CHECK(rejected(cmd(CMD_FOCUS_MOVE, INT32_MAX), E_LIMIT), "relatif INT32_MAX : E_LIMIT, sans débordement");
    run_for(500 * MS);
    CHECK(count_1d(from, NULL) == 0 && status().motion_state == MOTION_IDLE && status().focus_position == p,
          "aucun 0x1D, mouvement au repos, position inchangée");
    q = cmd(CMD_FOCUS_GOTO, LIM_MAX);
    run_for(4000 * MS);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL) && status().focus_position == LIM_MAX,
          "la borne haute publiée - 5 acceptée : ARRIVED à %d (%d)", LIM_MAX, (int)status().focus_position);
    q = cmd(CMD_FOCUS_MOVE, LIM_MIN - LIM_MAX);
    run_for(4000 * MS);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL) && status().focus_position == LIM_MIN,
          "relatif jusqu'à la borne basse publiée + 5 accepté : ARRIVED à %d (%d)", LIM_MIN, (int)status().focus_position);
}

static void t_move(void)
{
    int32_t p;
    uint32_t q;
    printf("relatif : la position lue ± n, puis comme un goto\n");
    ready();
    p = status().focus_position;
    q = cmd(CMD_FOCUS_MOVE, 500);
    run_for(2000 * MS);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL) && status().focus_position == p + 500, "+500 : %d", (int)status().focus_position);
    q = cmd(CMD_FOCUS_MOVE, -700);
    run_for(2000 * MS);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL) && status().focus_position == p - 200, "-700 : %d", (int)status().focus_position);
}

static void t_stop_goto(void)
{
    size_t from;
    uint32_t g, q;
    uint64_t ts;
    printf("q pendant un goto : 0x1C, le goto ABORTED (E_ABORTED), le stop ACK_COMPLETED, plus de 0x1D\n");
    ready();
    g = cmd(CMD_FOCUS_GOTO, 30000);
    run_for(150 * MS);
    CHECK(status().motion_state == MOTION_MOVING, "MOVING");
    from = n_log;
    ts = phy_sim_now();
    q = cmd(CMD_FOCUS_STOP, 0);
    CHECK(n_acks >= 3 && ACKS[n_acks - 3].a.seq == q && ACKS[n_acks - 3].a.result == ACK_ACCEPTED && ACKS[n_acks - 2].a.seq == g &&
              ACKS[n_acks - 2].a.result == ACK_FAILED && ACKS[n_acks - 2].a.reason == E_ABORTED &&
              ACKS[n_acks - 1].a.seq == q && ACKS[n_acks - 1].a.result == ACK_COMPLETED,
          "stop ACCEPTED, goto ACK_FAILED E_ABORTED, stop ACK_COMPLETED");
    CHECK(from < n_log && LOG[from].kind == A_SEND && LOG[from].frame.msg[0] == 0x1C && LOG[from].frame.len == 1 && LOG[from].t == ts,
          "0x1C émis seul, tout de suite");
    CHECK(status().motion_state == MOTION_ABORTED, "ABORTED");
    run_for(1000 * MS);
    CHECK(count_1d(from, NULL) == 0 && count_af(from) == 0, "plus de 0x1D, plus de bit AF");
    CHECK(status().session_state == SESSION_READY && status().focus_position < 30000, "READY, arrêté avant la cible (%d)",
          (int)status().focus_position);
    CHECK(bsk_bench_refused() == refused0 && lens_clean(), "ni refus, ni hors modèle");
}

static void t_stop_ready(void)
{
    size_t from;
    uint32_t q;
    printf("q en READY sans mouvement : 0x1C quand même, ACK_COMPLETED\n");
    ready();
    from = n_log;
    q = cmd(CMD_FOCUS_STOP, 0);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL), "ACK_ACCEPTED, ACK_COMPLETED");
    CHECK(from < n_log && LOG[from].kind == A_SEND && LOG[from].frame.msg[0] == 0x1C && n_log == from + 1, "0x1C émis, rien d'autre");
}

static void t_stop_powering(void)
{
    size_t from;
    uint32_t q;
    printf("q en POWERING : rien d'émis, ACK_COMPLETED\n");
    bench();
    mount();
    run_for(320 * MS);
    CHECK(status().session_state == SESSION_POWERING, "POWERING");
    from = n_log;
    q = cmd(CMD_FOCUS_STOP, 0);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL) && n_log == from, "ACK_COMPLETED, rien d'émis");
    run_for(4000 * MS);
    CHECK(status().session_state == SESSION_READY && count_sent_from(from, 0x1C) == 0,
          "le démarrage continue, READY (aucun 0x1C : ni l'arrêt, ni un homing du pilote)");
}

static void t_busy(void)
{
    uint32_t g;
    printf("pendant un goto : goto, CMD_ATTACH, CMD_DETACH, CMD_CLEAR_FAULT, CMD_APERTURE_SET : E_BUSY (M2)\n");
    ready();
    g = cmd(CMD_FOCUS_GOTO, 25000);
    run_for(50 * MS);
    CHECK(rejected(cmd(CMD_FOCUS_GOTO, 20000), E_BUSY), "goto : E_BUSY (M2)");
    CHECK(rejected(cmd(CMD_FOCUS_MOVE, 10), E_BUSY), "relatif : E_BUSY");
    CHECK(rejected(cmd(CMD_ATTACH, 0), E_BUSY), "CMD_ATTACH : E_BUSY");
    CHECK(rejected(cmd(CMD_DETACH, 0), E_BUSY), "CMD_DETACH : E_BUSY");
    CHECK(rejected(cmd(CMD_CLEAR_FAULT, 0), E_BUSY), "CMD_CLEAR_FAULT : E_BUSY");
    CHECK(rejected(cmd(CMD_SET_MARK, 0), E_BUSY), "CMD_SET_MARK : E_BUSY");
    CHECK(rejected(cmd(CMD_APERTURE_SET, 0x14F9), E_BUSY), "CMD_APERTURE_SET : E_BUSY (une commande en vol à la fois)");
    run_for(3000 * MS);
    CHECK(accepted_then(g, ACK_COMPLETED, E_OK, NULL) && status().focus_position == 25000, "le goto finit, intact");
    CHECK(accepted_then(cmd(CMD_SET_MARK, BSK_MARK_HERE), ACK_COMPLETED, E_OK, NULL) && status().mark_valid &&
              status().mark_position == 25000,
          "CMD_SET_MARK : servie, finie tout de suite, la marque en 25000");
    CHECK(accepted_then(cmd(CMD_CLEAR_MARK, 0), ACK_COMPLETED, E_OK, NULL) && !status().mark_valid,
          "CMD_CLEAR_MARK : servie, plus de marque");
    CHECK(accepted_then(cmd(CMD_APERTURE_SET, 0x14F9), ACK_COMPLETED, E_OK, NULL),
          "CMD_APERTURE_SET : servie, finie tout de suite (le faux 135 publie sa plage au 0x08)");
    CHECK(accepted_then(cmd(CMD_CLEAR_FAULT, 0), ACK_COMPLETED, E_OK, NULL) && status().session_state == SESSION_READY,
          "CMD_CLEAR_FAULT hors FAULT : ACK_COMPLETED, sans effet");
}

/* Le bouton du fût du faux 135 (l135_inputs_t, l'offset 64 du 0x05, bit 3), décidé au relâchement : un appui de 2 s
 * pose la marque à la position courante, avec le sens du goto qui y a mené (croissant, depuis la position du démarrage),
 * rangée sous la clé du 135 (01030800bf087, calculée à la main) ; un appui de 300 ms y va, par la
 * boîte de l'hôte sous BSK_SEQ_BOARD ; un appui de 4,5 s ne fait rien. */
static void t_button(void)
{
    uint32_t v = 0, w0;
    bsk_status_t s;
    printf("bouton du fût : long -> la marque ici, sens compris ; court -> y aller ; 4 s ou plus -> rien\n");
    ready();
    CHECK(status().focus_position < 25000, "READY en dessous de 25000 (%ld) : le goto est croissant", (long)status().focus_position);
    (void)cmd(CMD_FOCUS_GOTO, 25000);
    run_for(3000 * MS);
    L.in.button = true;
    run_for(2000 * MS);
    L.in.button = false;
    run_for(100 * MS);
    s = status();
    CHECK(s.mark_valid && s.mark_position == 25000 && store_sim_peek("01030800bf087", &v) && v == (25000u | 1u << 16),
          "appui de 2 s en 25000 : la marque, sens croissant, 0x161A8 (0x%X)", v);
    CHECK(acks_of(BSK_SEQ_BOARD) == 0 && s.focus_position == 25000, "pas de goto");
    (void)cmd(CMD_FOCUS_GOTO, 20000);
    run_for(3000 * MS);
    CHECK(status().focus_position == 20000, "en 20000");
    L.in.button = true;
    run_for(300 * MS);
    L.in.button = false;
    run_for(3000 * MS);
    s = status();
    CHECK(accepted_then(BSK_SEQ_BOARD, ACK_COMPLETED, E_OK, NULL) && s.motion_state == MOTION_ARRIVED &&
              s.focus_position == 25000 && s.mark_position == 25000,
          "appui de 300 ms : le goto du bouton, arrivé en 25000, tel quel ; ses accusés sous BSK_SEQ_BOARD");
    (void)cmd(CMD_FOCUS_GOTO, 20000);
    run_for(3000 * MS);
    w0 = g_store_sim.writes;
    L.in.button = true;
    run_for(4500 * MS);
    L.in.button = false;
    run_for(3000 * MS);
    s = status();
    CHECK(s.focus_position == 20000 && s.mark_position == 25000 && g_store_sim.writes == w0 && acks_of(BSK_SEQ_BOARD) == 2,
          "appui de 4,5 s : rien, ni goto ni marque");
}

static void t_detach_attach(void)
{
    size_t from;
    uint32_t q;
    printf("CMD_DETACH : OFF tenu malgré D2 ; CMD_ATTACH : ACK_COMPLETED tout de suite, POWERING, READY\n");
    ready();
    q = cmd(CMD_DETACH, 0);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL) && status().session_state == SESSION_OFF, "OFF, ACK_COMPLETED");
    from = n_log;
    run_for(5000 * MS);
    CHECK(status().session_state == SESSION_OFF && quiet_from(from), "5 s, D2 présent : toujours OFF, rien sur le fil");
    q = cmd(CMD_ATTACH, 0);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL), "CMD_ATTACH : ACK_COMPLETED tout de suite");
    run_for(1 * MS);
    CHECK(entered(SESSION_POWERING, phy_sim_now() - 1 * MS) != 0, "POWERING");
    run_for(4000 * MS);
    CHECK(status().session_state == SESSION_READY, "READY");
    q = cmd(CMD_FOCUS_GOTO, 20000);
    CHECK(ack_of(q, 0) && ack_of(q, 0)->a.result == ACK_ACCEPTED && status().motion_state == MOTION_COMMANDED, "servi");
    run_for(3000 * MS);
    q = cmd(CMD_DETACH, 0);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL) && status().session_state == SESSION_OFF, "de nouveau CMD_DETACH : OFF");
    d2(false);
    run_for(400 * MS);
    q = cmd(CMD_ATTACH, 0);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL) && status().session_state == SESSION_OFF, "CMD_ATTACH, D2 absent : OFF");
    d2(true);
    run_for(301 * MS);
    CHECK(status().session_state == SESSION_POWERING, "D2 revient : POWERING, le maintien de CMD_DETACH est levé");
}

static void t_attach_restart(void)
{
    uint64_t t;
    uint32_t q;
    printf("CMD_ATTACH pendant un démarrage : ACK_COMPLETED, le démarrage repart en POWERING\n");
    bench();
    mount();
    while (status().session_state != SESSION_IDENTIFYING && phy_sim_now() < T0 + 3000 * MS) run_for(1 * MS);
    t = phy_sim_now();
    CHECK(rejected(cmd(CMD_DETACH, 0), E_BUSY) && status().session_state == SESSION_IDENTIFYING,
          "CMD_DETACH pendant un démarrage : E_BUSY, sans effet (control.c:154)");
    q = cmd(CMD_ATTACH, 0);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL) && status().session_state == SESSION_POWERING,
          "IDENTIFYING -> POWERING, ACK_COMPLETED");
    run_for(5000 * MS);
    CHECK(entered(SESSION_READY, t) != 0 && status().session_state == SESSION_READY, "puis READY");
}

static void t_attach_no_d2(void)
{
    size_t from;
    uint32_t q;
    printf("CMD_ATTACH, D2 absent : ACK_COMPLETED, OFF, aucune trame ; D2 présent ensuite : POWERING\n");
    bench();
    from = n_log;
    q = cmd(CMD_ATTACH, 0);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL) && status().session_state == SESSION_OFF, "ACK_COMPLETED, OFF");
    run_for(3000 * MS);
    CHECK(status().session_state == SESSION_OFF && quiet_from(from), "3 s : OFF, rien sur le fil");
    CHECK(rejected(cmd(CMD_FOCUS_GOTO, 20000), E_BUSY), "goto en OFF : E_BUSY");
    q = cmd(CMD_FOCUS_STOP, 0);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL) && quiet_from(from), "q en OFF : ACK_COMPLETED, rien d'émis");
    mount();
    run_for(301 * MS);
    CHECK(status().session_state == SESSION_POWERING, "D2 présent : POWERING");
}

/* Du FAULT (objectif muet, 4 échecs), avec le faux 135 encore muet : l'essai repart avec un compteur à
 * zéro, soit quatre essais avant le FAULT suivant. */
static size_t tries_to_fault(void)
{
    size_t from = n_log;
    run_for(30000 * MS);
    CHECK(status().session_state == SESSION_FAULT && status().last_error == E_LOST, "de nouveau FAULT, E_LOST");
    return count_sent_from(from, 0x01);
}

static void t_fault(void)
{
    uint32_t q;
    size_t n, from;
    uint64_t tc;
    printf("depuis FAULT : CMD_CLEAR_FAULT (OFF puis POWERING) et CMD_ATTACH (POWERING), compteur à zéro, READY\n");
    bench();
    L.f.silent = true;
    mount();
    run_for(30000 * MS);
    CHECK(status().session_state == SESSION_FAULT && status().last_error == E_LOST, "muet : FAULT, E_LOST");
    CHECK(rejected(cmd(CMD_FOCUS_GOTO, 20000), E_LOST), "goto en FAULT : refusé avec la raison du FAULT");
    from = n_log;
    q = cmd(CMD_FOCUS_STOP, 0);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL) && n_log == from, "q en FAULT : ACK_COMPLETED, rien d'émis");

    tc = phy_sim_now();
    q = cmd(CMD_CLEAR_FAULT, 0);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL) && status().session_state == SESSION_OFF &&
              status().last_error == E_OK,
          "CMD_CLEAR_FAULT : OFF, erreur oubliée, ACK_COMPLETED à l'arrivée en OFF");
    run_for(1 * MS);
    CHECK(entered(SESSION_POWERING, tc) == tc, "puis POWERING au même instant (D2 présent : OFF non tenu)");
    n = tries_to_fault();
    CHECK(n == 24, "compteur à zéro : quatre essais de six 0x01 avant le FAULT suivant (%zu)", n);

    q = cmd(CMD_ATTACH, 0);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL) && status().session_state == SESSION_POWERING &&
              status().last_error == E_OK,
          "CMD_ATTACH en FAULT : POWERING tout de suite, ACK_COMPLETED");
    n = tries_to_fault();
    CHECK(n == 24, "compteur à zéro : quatre essais de six 0x01 (%zu)", n);

    L.f.silent = false;
    q = cmd(CMD_ATTACH, 0);
    run_for(5000 * MS);
    CHECK(status().session_state == SESSION_READY, "le faux 135 répond : CMD_ATTACH -> READY");

    L.f.silent = true;
    run_for(40000 * MS);
    CHECK(status().session_state == SESSION_FAULT, "muet de nouveau : FAULT");
    L.f.silent = false;
    q = cmd(CMD_CLEAR_FAULT, 0);
    CHECK(status().session_state == SESSION_OFF, "CMD_CLEAR_FAULT : OFF");
    run_for(5000 * MS);
    CHECK(status().session_state == SESSION_READY, "puis POWERING, READY");

    L.f.silent = true;
    run_for(40000 * MS);
    CHECK(status().session_state == SESSION_FAULT, "muet de nouveau : FAULT");
    q = cmd(CMD_DETACH, 0);
    from = n_log;
    run_for(3000 * MS);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL) && status().session_state == SESSION_OFF && quiet_from(from),
          "CMD_DETACH en FAULT : OFF tenu, rien sur le fil");
    CHECK(bsk_bench_refused() == refused0, "aucune trame refusée par bench_core");
}

static void t_d2_goto(void)
{
    size_t from;
    uint32_t g;
    uint64_t td, tf;
    printf("D2 retombe pendant un goto : ACK_FAILED (E_ABORTED), OFF, plus de 0x1D\n");
    ready();
    g = cmd(CMD_FOCUS_GOTO, 30000);
    run_for(100 * MS);
    td = phy_sim_now();
    d2(false);
    from = n_log;
    run_for(1000 * MS);
    CHECK(accepted_then(g, ACK_FAILED, E_ABORTED, &tf) && tf == td + DROP,
          "ACK_FAILED, E_ABORTED, à la coupure, 2 ms après la retombée");
    CHECK(status().session_state == SESSION_OFF && status().motion_state == MOTION_IDLE, "OFF, mouvement oublié");
    CHECK(count_1d(from, NULL) == 0, "plus de 0x1D");
}

/* ─────────────────────────── le verrou de XDETECT ─────────────────────────── */

/* Le dernier A_RAIL allumé depuis `from` : refusé ? -1 s'il n'y en a aucun. */
static int rail_on_refused(size_t from)
{
    long i, last = -1;
    for (i = act_from(from, A_RAIL, 0, 1); i >= 0; i = act_from((size_t)i + 1, A_RAIL, 0, 1)) last = i;
    return last < 0 ? -1 : LOG[last].refused;
}

static void t_xdetect(void)
{
    uint32_t g;
    uint64_t td, tf;
    size_t from;
    printf("XDETECT retombe, READY, goto en vol : coupure dans la PHY 2 ms après, OFF au pas suivant, goto fini\n");
    ready();
    g = cmd(CMD_FOCUS_GOTO, 30000);
    run_for(100 * MS);
    CHECK(status().motion_state == MOTION_MOVING && !rest_all() && rails_up(), "un goto en vol, tout allumé");
    td = phy_sim_now();
    phy_sim_d2(false);
    phy_sim_run(td + DROP - 1);
    CHECK(!rest_all() && rails_up() && status().session_state == SESSION_READY,
          "2 ms moins 1 µs après la retombée : rien de coupé");
    phy_sim_run(td + DROP);
    CHECK(rest_all() && rails_off() && status().session_state == SESSION_READY,
          "à 2 ms, dans la PHY, avant tout pas de SESSION : rails coupés, TXD, BODY_CS et VD au repos");
    td += DROP;
    run_for(100 * MS);
    CHECK(status().session_state == SESSION_OFF && entered(SESSION_OFF, td) == td, "OFF au pas suivant, à l'instant de la coupure");
    CHECK(accepted_then(g, ACK_FAILED, E_ABORTED, &tf) && tf == td && status().motion_state == MOTION_IDLE,
          "la commande en vol finie : ACK_FAILED, E_ABORTED");
    from = n_log;
    run_for(5000 * MS);
    CHECK(quiet_from(from) && rest_all() && rails_off() && status().session_state == SESSION_OFF,
          "5 s après : OFF, rien sur le fil, tout au repos");
}

/* CMD_ATTACH (la lettre `b`) déposée avant que la SESSION n'ait vu la retombée : elle rallume, le verrou refuse. */
static void t_xdetect_attach(void)
{
    uint64_t td;
    size_t from;
    printf("XDETECT retombé : CMD_ATTACH avant et après le pas de SESSION, rien ne se rallume\n");
    ready();
    td = phy_sim_now() + DROP;
    phy_sim_d2(false);
    phy_sim_run(td);                                 /* la coupure, 2 ms après */
    from = n_log;
    CHECK(accepted_then(cmd(CMD_ATTACH, 0), ACK_COMPLETED, E_OK, NULL) && status().session_state == SESSION_POWERING,
          "CMD_ATTACH avant le pas qui voit la retombée : POWERING, D2 encore présent pour la SESSION");
    CHECK(rail_on_refused(from) == 1 && rest_all() && rails_off(), "le rail logique demandé, refusé par le verrou : rien d'allumé");
    run_to(td);
    CHECK(status().session_state == SESSION_OFF, "le pas suivant : OFF");
    from = n_log;
    CHECK(accepted_then(cmd(CMD_ATTACH, 0), ACK_COMPLETED, E_OK, NULL) && status().session_state == SESSION_OFF,
          "CMD_ATTACH, D2 absent pour la SESSION : OFF");
    run_for(5000 * MS);
    CHECK(quiet_from(from) && rail_on_refused(from) == -1 && rest_all() && rails_off() && status().session_state == SESSION_OFF,
          "5 s après : rien demandé, rien sur le fil, tout au repos");
    l135_power(&L, phy_sim_now(), true);
    d2(true);
    run_for(301 * MS);
    CHECK(status().session_state == SESSION_POWERING && rail_on_refused(from) == 0, "nouvelle insertion, 300 ms : POWERING, rail accepté");
}

/* La reprise de RECOVERING échoit à l'instant de la retombée, avant que la SESSION ne la lise : le verrou refuse. */
static void t_xdetect_recovery(void)
{
    uint64_t tr, tp;
    size_t from;
    printf("XDETECT coupé à l'échéance de la reprise : POWERING demandé, rail refusé, OFF\n");
    bench();
    L.f.silent = true;
    mount();
    while (status().session_state != SESSION_RECOVERING && phy_sim_now() < T0 + 5000 * MS) run_for(1 * MS);
    tr = entered(SESSION_RECOVERING, 0);
    tp = tr + P.retry_wait_ms * MS;
    run_to(tp - DROP);
    CHECK(tr != 0 && status().session_state == SESSION_RECOVERING, "objectif muet : RECOVERING");
    phy_sim_d2(false);                               /* la coupure à l'échéance de la reprise */
    phy_sim_run(tp);
    from = n_log;
    run_to(tp);
    CHECK(rail_on_refused(from) == 1 && act_from(from, A_RAIL, tp, 1) >= 0 && LOG[act_from(from, A_RAIL, tp, 1)].t == tp,
          "l'échéance de la reprise, avant la retombée dans l'ordre des instants : le rail logique demandé, refusé");
    CHECK(status().session_state == SESSION_OFF && rest_all() && rails_off(), "puis OFF : rien d'allumé");
    run_for(5000 * MS);
    CHECK(status().session_state == SESSION_OFF && rail_on_refused(from) == 1 && rails_off(), "5 s après : toujours OFF");
}

/* La file des événements de PHY pleine derrière la retombée : l'absence est vue quand même. */
static void t_xdetect_full(void)
{
    uint64_t td;
    printf("XDETECT retombé, file de la PHY débordée derrière lui : la SESSION passe à OFF\n");
    ready();
    td = phy_sim_now() + DROP;
    phy_sim_d2(false);
    phy_sim_run(td);                                 /* la coupure, 2 ms après */
    for (int i = 0; i < 1100; i++) phy_sim_bus_error();   /* EV_CAP = 1024 : le plus ancien est jeté */
    run_to(td);
    CHECK(status().session_state == SESSION_OFF && entered(SESSION_OFF, td) == td, "OFF, à l'instant de la coupure");
}

/* ─────────────────────────── le journal ─────────────────────────── */

static char JL[4000][400];
static size_t n_jl;

/* Les lignes du journal sorties de l'anneau depuis le dernier appel. */
static size_t jdrain(void)
{
    char b[sizeof JL[0]];
    uint8_t g;
    size_t n;
    n_jl = 0;
    while ((n = bsk_journal_take(b, sizeof b - 1, &g)) > 0) {
        b[n] = 0;
        if (n_jl < sizeof JL / sizeof JL[0]) memcpy(JL[n_jl], b, n + 1);
        n_jl++;
    }
    return n_jl;
}

/* L'indice de la première ligne qui commence par `pre` et contient `in` (NULL : rien), à partir de `from` ; -1 sinon. */
static long jfind(size_t from, const char *pre, const char *in)
{
    for (size_t i = from; i < n_jl && i < sizeof JL / sizeof JL[0]; i++)
        if (!strncmp(JL[i], pre, strlen(pre)) && (!in || strstr(JL[i], in))) return (long)i;
    return -1;
}

/* Le type de la trame d'une ligne « * rx|tx <t> F0 L L C S <type> … » (6e octet), -1 sinon ; `len` : sa longueur. */
static int jtype_len(const char *l, unsigned *len)
{
    unsigned v[6];
    const char *p = strchr(l + 5, ' ');
    if (!p || sscanf(p, " %x %x %x %x %x %x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6 || v[0] != 0xF0) return -1;
    if (len) *len = v[1] | v[2] << 8;
    return (int)v[5];
}
static int jtype(const char *l) { return jtype_len(l, NULL); }

static void t_journal(void)
{
    static const char *const boot[] = {"* session", "powering", "identifying", "ready"};
    long i, k;
    uint32_t q;
    int rx06 = 0, rx05 = 0, acked = 0;
    printf("journal : les transitions de SESSION et de MOUVEMENT, les trames émises et reçues, les erreurs de PHY\n");
    bench();
    bsk_journal_init(phy_sim_now);
    bsk_journal_set(true, false);
    mount();
    run_for(4000 * MS);
    jdrain();
    CHECK(status().session_state == SESSION_READY, "READY");
    for (k = 0, i = 1; i < 4; i++) {
        k = jfind((size_t)k, boot[0], boot[i]);
        CHECK(k >= 0, "« * session <t> %s », dans l'ordre", boot[i]);
        if (k < 0) break;
    }
    i = jfind(0, "* tx ", NULL);
    CHECK(i >= 0 && jtype(JL[i]) == 0x01, "la première trame émise : le 0x01 (« %.40s »)", i >= 0 ? JL[i] : "");
    i = jfind(0, "* rx ", NULL);
    CHECK(i >= 0 && jtype(JL[i]) == 0x01, "la première reçue : sa réponse");
    for (size_t j = 0; j < n_jl; j++) {
        unsigned n = 0;
        int t = strncmp(JL[j], "* rx ", 5) ? -1 : jtype_len(JL[j], &n);
        rx05 += t == 0x05;
        rx06 += t == 0x06 && n <= 40 + 8;
        acked += t == 0x06 && n > 40 + 8;
    }
    CHECK(rx05 == 0 && rx06 == 0 && jfind(0, "* tx ", " 04 00 00 19 81 ") < 0 && jfind(0, "* tx ", " 03 C2 2E ") < 0,
          "LOG ON : ni 0x05, ni 0x06 seul, ni paire de la boucle");
    CHECK(acked == 0, "aucun 0x06 suivi d'un accusé : ni goto ni homing du pilote au démarrage (%d)", acked);
    CHECK(jfind(0, "* motion ", NULL) < 0, "aucune ligne de MOUVEMENT au démarrage : il ne change pas (repos)");

    q = cmd(CMD_FOCUS_GOTO, 20000);
    run_for(3000 * MS);
    jdrain();
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL), "le goto finit");
    i = jfind(0, "* tx ", " 1D 20 4E 00 00 ");
    CHECK(i >= 0 && jtype(JL[i]) == 0x04, "* tx : le 0x04 qui porte 1D 20 4E 00 00");
    for (i = jfind(0, "* rx ", NULL); i >= 0 && jtype(JL[i]) != 0x06; i = jfind((size_t)i + 1, "* rx ", NULL)) {}
    CHECK(i >= 0, "* rx : le 0x06 suivi de l'accusé du 0x1D");
    k = jfind(0, "* motion ", " commanded");
    CHECK(k >= 0 && jfind((size_t)k, "* motion ", " moving") > k && jfind((size_t)k, "* motion ", " settling") > k &&
              jfind((size_t)k, "* motion ", " arrived") > k,
          "* motion : commanded, moving, settling, arrived");

    phy_sim_bus_error();
    run_for(1 * MS);
    jdrain();
    CHECK(jfind(0, "* rx ", " err=bus") >= 0, "une erreur de PHY : « * rx <t> err=bus »");

    bsk_journal_set(false, false);
    jdrain();
    q = cmd(CMD_FOCUS_GOTO, 25000);
    run_for(3000 * MS);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL) && jdrain() == 0, "LOG OFF : un goto ne laisse plus rien (%zu)", n_jl);
    L.f.silent = true;
    run_for(3000 * MS);
    bsk_journal_set(true, false);
    run_for(30000 * MS);
    jdrain();
    CHECK(status().session_state == SESSION_FAULT && jfind(0, "* session ", " fault lost") >= 0, "* session <t> fault lost");
    CHECK(jfind(0, "* session ", " recovering") >= 0, "* session <t> recovering");
    bsk_journal_set(false, false);
    L.f.silent = false;
}

/* Une émission malformée injectée dans la PHY simulée (phy_sim_lens_raw), le faux 135 hors tension et la session en
 * OFF ; SESSION donne l'erreur au journal avec ses octets. Les lignes attendues sont écrites à la main : un octet tous
 * les 13 µs (byte_us du faux 135) ; des octets écartés remontent avant la trame qui les suit, ou 20,001 ms après le
 * dernier octet reçu (RX_STALE_US, plus de 20 ms), ou par 4096. */
static const char R07[] = "F0 2B 00 02 00 07 01 03 70 01 00 01 05 00 00 08 00 00 00 00 00 60 92 86 5E 00 00 00 "
                          "00 00 00 00 00 00 00 00 00 00 00 00 8D 02 55";   /* full:12 */

/* La réponse du Sony FE 24-105 G au 0x01 : 7_Docs/E-Mount/traces/sony.txt ligne 11. */
static const char SONY01[] = "F0 29 00 02 00 01 FF 9F FF 5D EE 60 18 DE FF 0F F8 01 00 00 00 00 00 00 00 00 00 00 00 00 "
                             "00 00 00 00 00 00 00 00 71 07 55";

/* Les quatre morceaux de 64 octets d'un relevé de 256 : « <head> +<off>/256#<id> » et b[off..off+64) en hexa, le même
 * #id sur chacun (celui du premier). */
static bool chunks_are(const char *head, const uint8_t *b)
{
    const char *h = n_jl ? strchr(JL[0], '#') : NULL;
    bool ok = n_jl >= 4 && h;
    for (size_t k = 0; ok && k < 4; k++) {
        char want[400];
        int w = snprintf(want, sizeof want, "%s +%zu/256%.*s", head, k * 64, (int)strcspn(h, " "), h);
        for (size_t x = 0; x < 64; x++) w += snprintf(want + w, sizeof want - (size_t)w, " %02X", b[k * 64 + x]);
        ok = !strcmp(JL[k], want);
        if (!ok) printf("  attendu « %.100s »\n  reçu    « %.100s »\n", want, JL[k]);
    }
    return ok;
}

/* Une bague lente, un front montant toutes les 37 ms, 81 fronts (2,96 s), contre le faux 135
 * (lens135.c, S12) : l'offset 60 est un drapeau, posé par le front, effacé par le 0x04 reçu, publié par le 0x05 du créneau 0.
 * La carte compte les fronts tombés dans la fenêtre « 0x04 reçu -> créneau 0 suivant », et eux seuls. L'échéancier, écrit à la
 * main, en phase φ dans la trame VD (16666 µs, la VD à 60 Hz) :
 *   - le créneau 0 à φ = 2083 (le huitième de la trame à 60 Hz : 1 000 000 / 480 µs, samyang.md § 2.4) ;
 *   - le 0x04 reçu à φ = 10526 : émis à 10100, BODY_CS 40 µs avant le premier octet (la garde de la PHY simulée), 22
 *     octets de 13 µs (14 du 0x04 et 8 de trame), traité loop_us = 100 µs après le dernier ; le 0x03 de 8600, ses 29 octets
 *     finis à 9017, est traité avant (9117). loop_us vaut 5 ms par défaut, une borne haute (lens135.c, S1) : le 0x04 n'y serait
 *     traité qu'après le créneau 0 suivant et la fenêtre couvrirait presque toute la trame ; 100 µs la veut étroite ;
 *   - un front est compté si sa phase n'est pas dans [2083, 10526). Le premier à φ = 1246 d'un front VD, puis + 37000 µs, soit
 *     + 3668 en phase : φ_i = (1246 + 3668 i) mod 16666. Comptés, i = 0, 3, 4, 8, 9, 12, 13, 17, 18, 21, 22, 26, 27, 30, 31,
 *     32, 35, 36, 39, 40, 41, 44, 45, 48, 49, 50, 53, 54, 58, 59, 62, 63, 67, 68, 71, 72, 76, 77, 80 : 39 fronts. Aucune phase à
 *     moins de 123 µs d'une borne ; deux fronts comptés à 148 ms au plus : un seul geste (pause de 400 ms). */
#define RING_EDGES   81
#define RING_COUNTED 39

static void t_ring_window(void)
{
    long tv;
    uint64_t t0;
    size_t n0, nz = 0, gestures = 0;
    long pulses = 0, frames = 0;
    printf("bague lente, un front toutes les 37 ms : la carte compte les fronts de la fenêtre 0x04 reçu -> créneau 0, eux seuls\n");
    bench();
    LP.loop_us = 100;
    l135_init(&L, &LP);
    bsk_journal_init(phy_sim_now);
    bsk_journal_set(true, false);
    mount();
    run_for(4000 * MS);
    CHECK(status().session_state == SESSION_READY && status().ring == RING_APERTURE, "READY, la bague en rôle ouverture (01)");
    n0 = n_vd;
    while (n_vd == n0) run_for(1 * MS);
    tv = (long)n_vd - 1;
    t0 = VD_EDGES[tv] + 16666;                      /* un front VD, écrit à la main : 16666 µs après le dernier vu */
    jdrain();
    n_jl = 0;
    n0 = n_log;
    for (unsigned i = 0; i < RING_EDGES; i++) {
        run_to(t0 + 1246 + 37000u * i);
        l135_ring_edge(&L, phy_sim_now(), true);
    }
    run_for(1000 * MS);
    jdrain();
    CHECK(VD_EDGES[tv + 1] == t0, "le front VD de l'échéancier (%llu)", (unsigned long long)VD_EDGES[tv + 1]);
    for (size_t i = n0; i < n_log && i < LOG_CAP; i++)
        nz += LOG[i].kind == A_RX && LOG[i].frame.msg[0] == 0x05 && LOG[i].frame.len > 61 && LOG[i].frame.msg[61] != 0;
    CHECK(nz == RING_COUNTED, "sur le fil : %zu 0x05 portent l'offset 60 non nul (%d attendus)", nz, RING_COUNTED);
    for (long k = jfind(0, "* ring ", " gesture "); k >= 0; k = jfind((size_t)k + 1, "* ring ", " gesture ")) {
        const char *p = strstr(JL[k], "pulses="), *f = strstr(JL[k], "frames=");
        gestures++;
        if (p) pulses += strtol(p + 7, NULL, 10);
        if (f) frames += strtol(f + 7, NULL, 10);
    }
    CHECK(gestures == 1 && pulses == RING_COUNTED && frames == RING_COUNTED,
          "la carte : un geste, pulses=%ld frames=%ld (%d attendus, %zu ligne(s))", pulses, frames, RING_COUNTED, gestures);
    bsk_journal_set(false, false);
    CHECK(lens_clean(), "rien hors modèle");
}

/* ─────────────────────── la bague selon la configuration du commutateur ─────────────────────── */

/* La consigne d'ouverture (offsets 3-4) du dernier 0x03 émis ; 0 sans 0x03. */
static uint16_t last_ap03(void)
{
    for (size_t i = n_log < LOG_CAP ? n_log : LOG_CAP; i-- > 0;)
        if (LOG[i].kind == A_SEND && LOG[i].frame.msg[0] == 0x03) return (uint16_t)(LOG[i].frame.msg[4] | LOG[i].frame.msg[5] << 8);
    return 0;
}

/* L'offset 3 du dernier 0x04 émis (le bit AF du boîtier : 0x02) ; 0xEE sans 0x04. */
static uint8_t last_o3_04(void)
{
    for (size_t i = n_log < LOG_CAP ? n_log : LOG_CAP; i-- > 0;)
        if (LOG[i].kind == A_SEND && LOG[i].frame.msg[0] == 0x04) return LOG[i].frame.msg[4];
    return 0xEE;
}

/* Les 0x05 reçus depuis l'entrée `from` du journal : combien portent l'ouverture native (offset 19, bit 0), combien
 * l'offset 60 non nul ; l'offset 62 du dernier. */
typedef struct { size_t ap, o60; uint8_t o62; } rx05_t;
static rx05_t rx05(size_t from)
{
    rx05_t r = {0, 0, 0xEE};
    for (size_t i = from; i < n_log && i < LOG_CAP; i++) {
        const bsk_frame_t *f = &LOG[i].frame;
        if (LOG[i].kind != A_RX || f->msg[0] != 0x05 || f->len < 64) continue;
        r.ap += f->msg[20] & 0x01;
        r.o60 += f->msg[61] != 0;
        r.o62 = f->msg[63];
    }
    return r;
}

/* n fronts (descendants si n < 0), un toutes les 37 ms, puis 500 ms de calme. */
static void ring_slow(int n)
{
    for (int e = 0; e < (n < 0 ? -n : n); e++) {
        run_for(37 * MS);
        l135_ring_edge(&L, phy_sim_now(), n > 0);
    }
    run_for(500 * MS);
}

/* La configuration de l'humain : 20 en flash (M1 = MF, M2 = APERTURE). La carte envoie 06 au 0x08 (init.c) : l'objectif
 * applique sa flash au lieu du forçage (lens135.c, S14). Les ouvertures attendues sont celles de la table de l'objectif
 * (lens135.c, S15), écrites ici à la main : 4543 (11BF) aux index 0-5, 4608 (1200) aux index 6-11, 4693 (1255) aux index
 * 12-17 ; un index par front, après une zone morte de 3 fronts quand le compteur part de 0. */
static void t_ring_custom(void)
{
    size_t from;
    rx05_t r;
    printf("la bague selon le commutateur Custom de l'objectif (20 : M1 = MF, M2 = APERTURE) : rôle focus en MF, ouverture native "
           "recopiée en APERTURE, a<f> puis la bague\n");
    bench();
    L.custom = 0x20;
    mount();
    run_for(4000 * MS);
    from = n_log;
    ring_slow(12);
    r = rx05(from);
    CHECK(status().session_state == SESSION_READY && status().ring == RING_FOCUS && (last_o3_04() & 0x02) == 0 && r.o62 == 0x03,
          "M1 en MF : l'offset 62 à 03, la carte en rôle focus, le bit AF du boîtier retiré (offset 3 du 0x04 à %02X)", last_o3_04());
    CHECK(r.ap == 0 && r.o60 == 0 && last_ap03() == 0x11BF,
          "douze fronts en MF : ni ouverture publiée (%zu), ni offset 60 (%zu), la consigne de la carte reste 11BF, f/1,8 bornée "
          "à la plage (%04X)", r.ap, r.o60, last_ap03());

    L.in.m2 = true;                                 /* le commutateur en M2 */
    from = n_log;
    run_for(500 * MS);
    r = rx05(from);
    CHECK(status().ring == RING_APERTURE && (last_o3_04() & 0x02) && r.o62 == 0x01,
          "M2 en APERTURE : l'offset 62 à 01, la carte en rôle ouverture, le bit AF du boîtier posé (%02X)", last_o3_04());
    /* la consigne de départ, f/1,8 bornée à la plage, BF 11, est aussi la valeur de l'index 0 : appliquée ou non, la
     * première valeur publiée laisse 11BF ; la règle (la première ouverture publiée n'est pas prise) est tenue par
     * test_session_script.c:t_ring_aperture */
    CHECK(r.ap > 0 && last_ap03() == 0x11BF,
          "l'objectif publie son ouverture (index 0, 11BF, la consigne de départ sous la première case), la carte garde 11BF "
          "(%04X)", last_ap03());
    from = n_log;
    ring_slow(12);
    r = rx05(from);
    CHECK(last_ap03() == 0x1255 && r.o60 == 0,
          "douze fronts vers le fermé : index 12, la carte recopie 1255 (%04X) ; l'offset 60 jamais posé en APERTURE (%zu)",
          last_ap03(), r.o60);
    ring_slow(-6);
    CHECK(last_ap03() == 0x1200, "six fronts vers l'ouvert : index 6, 1200 (%04X)", last_ap03());
    CHECK(accepted_then(cmd(CMD_APERTURE_SET, 0x1400), ACK_COMPLETED, E_OK, NULL), "a<f> : 1400");
    run_for(1000 * MS);
    CHECK(last_ap03() == 0x1400, "la bague immobile : a<f> tient, l'ouverture publiée n'a pas changé (%04X)", last_ap03());
    ring_slow(6);
    CHECK(last_ap03() == 0x1255,
          "six fronts vers le fermé : l'objectif repart de sa valeur, index 12, 1255 — pas d'un tiers au-dessus de 1400 (%04X)",
          last_ap03());
    CHECK(lens_clean(), "rien hors modèle");
}

/* Ce que montrent la carte et l'objectif dans la position `m2` du commutateur : le commutateur posé, 500 ms, puis six
 * fronts lents vers le fermé. */
typedef struct { bsk_ring_t ring; rx05_t r; } pos_seen_t;
static pos_seen_t pos_seen(bool m2)
{
    pos_seen_t s;
    size_t from;
    L.in.m2 = m2;
    run_for(500 * MS);
    from = n_log;
    ring_slow(6);
    s.ring = status().ring;
    s.r = rx05(from);
    return s;
}

/* Une reprise sans coupure de l'objectif, par la perte de liaison : le 135 se tait en READY, la carte passe en RECOVERING
 * (sans couper ses rails, cut_rails faux d'office), il parle de nouveau, la carte refait son init jusqu'à READY. */
static bool resume_no_cut(void)
{
    uint64_t ts = phy_sim_now(), tr;
    size_t from = n_log;
    L.f.silent = true;
    while (status().session_state != SESSION_RECOVERING && phy_sim_now() < ts + 5000 * MS) run_for(1 * MS);
    tr = entered(SESSION_RECOVERING, ts);
    L.f.silent = false;
    run_for(8000 * MS);
    return tr && entered(SESSION_POWERING, tr) == tr + 3000 * MS && status().session_state == SESSION_READY && L.powered &&
           act_from(from, A_RAIL, ts, 0) < 0 && find(from, A_RX, 0, 0x01) >= 0 && find(from, A_SEND, 2, 0x08) > find(from, A_SEND, 2, 0x01);
}

/* La reprise sans coupure : la carte renvoie son 0x01 (FF 01 00, le vieux boîtier) avant son 0x08 ; le 135,
 * sorti du homing de la première session avec un 0x08 reçu, se force entre les deux (M1 = AF, M2 = MF, en RAM : lens135.c,
 * S14), et le 0x08 au bit 0x04 qui suit ne lui rend pas sa flash. Accepté par l'humain : la carte ne s'en corrige pas. Deux
 * façons de retrouver 20 : CUSTOM WRITE 20 (la RAM réécrite aussitôt, le forçage ne la réécrase plus), ou une coupure de
 * l'objectif (la flash rechargée). Attendus, écrits à la main : en 20, M1 = MF (rôle focus, offset 62 à 03, ni offset 60 ni
 * ouverture publiée), M2 = APERTURE (rôle ouverture, offset 62 à 01, ouverture native publiée, pas d'offset 60) ; forcé,
 * M1 = AF (rôle ouverture, offset 62 à 01, offset 60 posé par la bague, pas d'ouverture native), M2 = MF (comme M1 en 20). */
static void t_ring_custom_resume(void)
{
    pos_seen_t s;
    uint64_t toff, tr, ton;
    uint8_t d[BSK_CUSTOM_DATA];
    uint32_t q;
    printf("reprise sans coupure du 135 configuré 20 : M1 en AF, M2 en MF ; CUSTOM WRITE 20 ou une coupure rendent 20\n");
    bench();
    L.custom = 0x20;
    mount();
    run_for(4000 * MS);
    CHECK(status().session_state == SESSION_READY, "READY");
    s = pos_seen(false);
    CHECK(s.ring == RING_FOCUS && s.r.o62 == 0x03 && s.r.o60 == 0 && s.r.ap == 0,
          "première session, M1 : MF, rôle focus (ring %d, offset 62 %02X, offset 60 %zu, ouverture %zu)", s.ring, s.r.o62, s.r.o60,
          s.r.ap);
    s = pos_seen(true);
    CHECK(s.ring == RING_APERTURE && s.r.o62 == 0x01 && s.r.o60 == 0 && s.r.ap > 0,
          "première session, M2 : ouverture native (ring %d, offset 62 %02X, offset 60 %zu, ouverture %zu)", s.ring, s.r.o62, s.r.o60,
          s.r.ap);

    CHECK(resume_no_cut(), "reprise sans coupure : RECOVERING, POWERING 3 s après, rails tenus, le 0x01 répondu puis le 0x08, READY");
    s = pos_seen(false);
    CHECK(s.ring == RING_APERTURE && s.r.o62 == 0x01 && s.r.o60 > 0 && s.r.ap == 0,
          "après la reprise, M1 forcé en AF : rôle ouverture, offset 60 posé (ring %d, offset 62 %02X, offset 60 %zu, ouverture %zu)",
          s.ring, s.r.o62, s.r.o60, s.r.ap);
    s = pos_seen(true);
    CHECK(s.ring == RING_FOCUS && s.r.o62 == 0x03 && s.r.o60 == 0 && s.r.ap == 0,
          "après la reprise, M2 forcé en MF : rôle focus (ring %d, offset 62 %02X, offset 60 %zu, ouverture %zu)", s.ring, s.r.o62,
          s.r.o60, s.r.ap);

    q = cmd(CMD_LENS_CUSTOM, 0x20);
    run_for(1000 * MS);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL), "CUSTOM WRITE 20 : la réponse de l'objectif");
    s = pos_seen(true);
    CHECK(s.ring == RING_APERTURE && s.r.o62 == 0x01 && s.r.o60 == 0 && s.r.ap > 0,
          "CUSTOM WRITE 20, sans coupure : M2 de nouveau en ouverture native (ring %d, offset 62 %02X, offset 60 %zu, ouverture %zu)",
          s.ring, s.r.o62, s.r.o60, s.r.ap);
    s = pos_seen(false);
    CHECK(s.ring == RING_FOCUS && s.r.o62 == 0x03 && s.r.o60 == 0 && s.r.ap == 0,
          "CUSTOM WRITE 20, sans coupure : M1 de nouveau en MF (ring %d, offset 62 %02X, offset 60 %zu, ouverture %zu)", s.ring, s.r.o62,
          s.r.o60, s.r.ap);
    q = cmd(CMD_LENS_CUSTOM, -1);
    run_for(1000 * MS);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL), "CUSTOM READ");
    bsk_session_custom_data(d);
    CHECK(d[7] == 0x20, "CUSTOM READ : 20 en flash (%02X)", d[7]);

    CHECK(resume_no_cut(), "de nouveau une reprise sans coupure, READY");
    s = pos_seen(false);
    CHECK(s.ring == RING_APERTURE && s.r.o60 > 0 && s.r.ap == 0, "de nouveau forcé : M1 en AF (ring %d, offset 60 %zu)", s.ring, s.r.o60);
    toff = phy_sim_now();                           /* la coupure de l'objectif */
    l135_power(&L, toff, false);
    while (status().session_state != SESSION_RECOVERING && phy_sim_now() < toff + 5000 * MS) run_for(1 * MS);
    tr = entered(SESSION_RECOVERING, toff);
    ton = tr + 1000 * MS;
    run_to(ton);
    l135_power(&L, ton, true);
    run_for(15000 * MS);
    CHECK(tr && status().session_state == SESSION_READY, "coupée, rallumée : READY");
    s = pos_seen(false);
    CHECK(s.ring == RING_FOCUS && s.r.o62 == 0x03 && s.r.o60 == 0 && s.r.ap == 0,
          "après la coupure, M1 en MF : la flash rechargée (ring %d, offset 62 %02X, offset 60 %zu, ouverture %zu)", s.ring, s.r.o62,
          s.r.o60, s.r.ap);
    s = pos_seen(true);
    CHECK(s.ring == RING_APERTURE && s.r.o62 == 0x01 && s.r.o60 == 0 && s.r.ap > 0,
          "après la coupure, M2 en ouverture native (ring %d, offset 62 %02X, offset 60 %zu, ouverture %zu)", s.ring, s.r.o62, s.r.o60,
          s.r.ap);
    CHECK(lens_clean() && bsk_bench_refused() == refused0, "rien hors modèle, aucun refus de bench_core");
}

/* La position AF (10 d'usine, M1 = AF) : la carte compte l'offset 60, deux unités de même sens par tiers sur sa table (de
 * 11B2, f/1,8 : 1200, 1246, 12A5, ...), trois tiers par seconde au plus. Une bague rapide, un front toutes les 5 ms
 * pendant 1 s : une unité par trame (l'offset 60 est un drapeau), environ 60 ; un tiers dû toutes les deux trames, appliqué
 * à 22 trames au moins du précédent (21 trames font 349986 µs, 333334 µs au moins entre deux tiers, et un tiers n'est dû
 * qu'aux trames paires du geste) : aux trames 2, 24 et 46, trois tiers, 12A5. */
static void t_ring_af(void)
{
    size_t from;
    long k;
    printf("la bague en position AF (10 d'usine) : le chemin des impulsions de la carte — somme, plafond, pause\n");
    bench();
    bsk_journal_init(phy_sim_now);
    bsk_journal_set(true, false);
    mount();
    run_for(4000 * MS);
    jdrain();
    from = n_log;
    for (unsigned e = 0; e < 200; e++) {
        run_for(5 * MS);
        l135_ring_edge(&L, phy_sim_now(), true);
    }
    run_for(1000 * MS);
    jdrain();
    k = jfind(0, "* ring ", " gesture ");
    CHECK(status().ring == RING_APERTURE && (last_o3_04() & 0x02) && rx05(from).ap == 0 && rx05(from).o60 > 0,
          "M1 en AF : rôle ouverture, l'offset 60 posé, aucune ouverture native");
    CHECK(last_ap03() == 0x12A5, "une seconde de bague rapide : trois tiers, 12A5 (%04X)", last_ap03());
    CHECK(k >= 0 && strstr(JL[k], " thirds=3 ") && !strstr(JL[k], " capped=0") && jfind((size_t)k + 1, "* ring ", " gesture ") < 0,
          "une ligne de geste, close par la pause : trois tiers, les autres perdus (« %s »)", k >= 0 ? JL[k] : "");
    bsk_journal_set(false, false);
    CHECK(lens_clean(), "rien hors modèle");
}

/* ─────────────────────────── LOG ALL à 60 Hz ─────────────────────────── */

/* Une minute de LOG ALL depuis la mise sous tension : l'init (son 0x0A compris), deux gotos, la bague qui tourne, vidé
 * toutes les 20 ms comme la tâche de la carte. Chaque trame décodée du journal est comparée, octet pour octet, à la
 * suivante de son sens sur le fil (les notes de __wrap_bsk_phy_send et __wrap_bsk_phy_poll) : même instant à la
 * milliseconde, même classe, numéro, longueur, message. */
#define MINUTE_US  (60000u * MS)
#define DRAIN_US   (20u * MS)       /* la tâche du journal de la carte : 20 ms quand l'anneau est vide */
#define WRITE_US   (500u * MS)      /* la tâche du journal : une ligne attend l'USB 500 ms au plus */
/* Le lien USB Serial/JTAG de l'ESP32-S3 et de l'ESP32-C3 : USB 2.0 pleine vitesse (12 Mbit/s), CDC-ACM, des paquets de
 * 64 octets en bloc (manuels de référence technique ESP32-S3 et ESP32-C3, chapitre « USB Serial/JTAG Controller »).
 * Le plancher retenu : un paquet de 64 octets par trame USB de 1 ms, 64 000 octets par seconde — le débit du lien si
 * l'hôte ne le lit qu'une fois par trame USB ; en bloc, la norme en permet jusqu'à 19 par trame (USB 2.0, § 5.8.4). */
#define USB_FLOOR_BPS 64000u

typedef struct {
    size_t   wi[2];                 /* la prochaine note du fil à comparer, par sens (0 : émise, 1 : reçue) */
    unsigned same, differ, diff_lines, x1d, ack06, o60, x0a;
} wire_cmp_t;

static void minute_frame(const jd_frame_t *d, void *ctx)
{
    wire_cmp_t *c = ctx;
    act_kind_t k = d->rx ? A_RX : A_SEND;
    size_t *i = &c->wi[d->rx];
    const bsk_frame_t *w;
    while (*i < n_log && *i < LOG_CAP && LOG[*i].kind != k) (*i)++;
    if (*i >= n_log || *i >= LOG_CAP) {
        if (!c->differ++) printf("    trame %s 0x%02X du journal à %llu ms : aucune sur le fil\n", d->rx ? "reçue" : "émise",
                                 d->f.msg[0], (unsigned long long)d->ms);
        return;
    }
    w = &LOG[*i].frame;
    if (!d->known || d->ms != LOG[*i].t / 1000 || d->f.cls != w->cls || d->f.seq != w->seq || d->f.len != w->len ||
        memcmp(d->f.msg, w->msg, w->len)) {
        if (!c->differ++)
            printf("    trame %s 0x%02X du journal à %llu ms (%s) : pas celle du fil (0x%02X à %llu µs)\n", d->rx ? "reçue" : "émise",
                   d->f.msg[0], (unsigned long long)d->ms, d->diff ? "différence" : "entière", w->msg[0],
                   (unsigned long long)LOG[*i].t);
    } else {
        c->same++;
        c->diff_lines += d->diff;
        c->x1d += !d->rx && w->msg[0] == 0x04 && w->len > 14 && w->msg[14] == 0x1D;
        c->ack06 += d->rx && w->msg[0] == 0x06 && w->len > 40;
        c->o60 += d->diff && d->rx && w->msg[0] == 0x05 && w->len > 61 && w->msg[61] != 0;
        c->x0a += w->msg[0] == 0x0A;
    }
    (*i)++;
}

static void t_log_all_minute(void)
{
    static jd_meter_t m;
    wire_cmp_t c = {{0, 0}, 0, 0, 0, 0, 0, 0, 0};
    uint64_t t0, edge = 0;
    uint32_t d0, worst_s, worst_w;
    size_t wire = 0, pairs = 0, off_phase = 0;
    bool up = true;
    printf("LOG ALL, une minute à 60 Hz : chaque trame au journal et reconstruite, rien de perdu, le débit\n");
    bench();
    bsk_journal_init(phy_sim_now);
    jd_reset();
    memset(&m, 0, sizeof m);
    d0 = bsk_journal_dropped();
    bsk_journal_set(true, true);
    t0 = m.t0_us = phy_sim_now();
    mount();
    for (uint64_t t = t0 + DRAIN_US; t <= t0 + MINUTE_US; t += DRAIN_US) {
        uint64_t el = t - t0;
        if (el == 6000 * MS) (void)cmd(CMD_FOCUS_GOTO, 20000);
        if (el == 12000 * MS) (void)cmd(CMD_FOCUS_GOTO, 15000);
        if (el == 20000 * MS) edge = t;                      /* la bague : un front toutes les 37 ms, 10 s, puis l'autre sens */
        while (edge && edge <= t && edge < t0 + 40000 * MS) {
            run_to(edge);
            if (edge >= t0 + 30000 * MS) up = false;
            l135_ring_edge(&L, phy_sim_now(), up);
            edge += 37 * MS;
        }
        if (el == 45000 * MS) (void)cmd(CMD_FOCUS_GOTO, 25000);
        run_to(t);
        jd_drain(&m, t, minute_frame, &c);
    }
    for (size_t i = 0; i < n_log && i < LOG_CAP; i++) wire += LOG[i].kind == A_SEND || LOG[i].kind == A_RX;
    for (size_t v = 0; v + 1 < n_vd; v++) {                  /* la paire à sa phase, LOG ALL tenu */
        long i03 = -1;
        if (VD_EDGES[v] < t0 + 5000 * MS) continue;           /* la boucle lancée */
        for (size_t i = 0; i < n_log && i < LOG_CAP; i++)
            if (LOG[i].kind == A_SEND && LOG[i].frame.msg[0] == 0x03 && LOG[i].t >= VD_EDGES[v] && LOG[i].t < VD_EDGES[v + 1]) {
                i03 = (long)i;
                break;
            }
        pairs++;
        off_phase += i03 < 0 || LOG[i03].t != VD_EDGES[v] + PAIR03 || count_sent(VD_EDGES[v], VD_EDGES[v + 1], 0x03) != 1 ||
                     count_sent(VD_EDGES[v], VD_EDGES[v + 1], 0x04) != 1 ||
                     find((size_t)i03, A_SEND, 1, 0x04) < 0 || LOG[find((size_t)i03, A_SEND, 1, 0x04)].t != VD_EDGES[v] + PAIR04;
    }
    worst_s = jd_worst_second(&m, 60);
    worst_w = jd_worst_window(&m, WRITE_US / DRAIN_US);
    printf("    %u trames au journal (%zu sur le fil), %u en différence, %llu lignes ; %llu octets/s en moyenne, %lu au pire sur "
           "une seconde ; l'anneau : %zu octets au plus à un vidage (20 ms), %lu au plus en 500 ms\n",
           m.frames, wire, c.diff_lines, (unsigned long long)m.lines, (unsigned long long)(m.bytes / 60), (unsigned long)worst_s,
           m.ring_max, (unsigned long)worst_w);
    CHECK(status().session_state == SESSION_READY && status().focus_position == 25000, "READY, le dernier goto arrivé (%d)",
          (int)status().focus_position);
    CHECK(c.same == wire && c.differ == 0 && m.unknown == 0 && m.bad == 0,
          "chaque trame du fil au journal, dans l'ordre de son sens, reconstruite octet pour octet (%u sur %zu)", c.same, wire);
    CHECK(bsk_journal_dropped() == d0 && m.lost == 0 && m.rxflood == 0, "rien de tu ni de perdu : dropped %lu, lost %u, rxflood %u",
          (unsigned long)(bsk_journal_dropped() - d0), m.lost, m.rxflood);
    CHECK(c.x0a >= 2 && c.x1d >= 3 && c.ack06 >= 3 && c.o60 > 0 && c.diff_lines * 10 > wire * 9,
          "le flux couvert : le 0x0A et sa réponse (%u), le 0x04 + 0x1D (%u), le 0x06 accusé (%u), l'offset 60 du "
          "0x05 non nul en différence (%u), neuf trames sur dix en différence",
          c.x0a, c.x1d, c.ack06, c.o60);
    CHECK(worst_s * 2 < USB_FLOOR_BPS, "la pire seconde (%lu octets) sous la moitié du plancher du lien USB (%u octets/s)",
          (unsigned long)worst_s, USB_FLOOR_BPS);
    CHECK(m.ring_max < 8192 && worst_w < 8192, "l'anneau de 8 Ko tient entre deux vidages (%zu), et 500 ms d'USB bloqué (%lu)",
          m.ring_max, (unsigned long)worst_w);
    CHECK(pairs >= 3000 && off_phase == 0, "la paire à VD + 8,6 / + 10,1 ms sous LOG ALL : %zu fronts, %zu hors phase", pairs,
          off_phase);
    CHECK(lens_clean(), "rien hors modèle");
    bsk_journal_set(false, false);
}

static void t_journal_rejected(void)
{
    static uint8_t b[4100];
    size_t n, sent = 0;
    printf("journal : les octets qu'une erreur de réception a rejetés, de la PHY simulée au journal par SESSION\n");
    bench();
    bsk_journal_init(phy_sim_now);
    bsk_journal_set(true, false);
    jdrain();

    n = fr_hex(R07, b, 64);
    memcpy(b + n, (const uint8_t[]){0x01, 0x02, 0x03, 0x04, 0x05}, 5);
    phy_sim_lens_raw(b, n + 5);                     /* à 1000 ms ; le dernier octet à 1000,624 ms */
    run_for(100 * MS);
    jdrain();
    CHECK(n_jl == 2 && !strcmp(JL[0], "* rx 1000 F0 2B 00 02 00 07 01 03 70 01 00 01 05 00 00 08 00 00 00 00 00 60 92 86 5E 00 00 00 "
                                      "00 00 00 00 00 00 00 00 00 00 00 00 8D 02 55"),
          "LOG ON : la trame valide d'abord (« %.40s »)", n_jl ? JL[0] : "");
    CHECK(n_jl == 2 && !strcmp(JL[1], "* rx 1020 err=framing n=5 01 02 03 04 05"),
          "puis les 5 octets écartés, 20 ms après le dernier, sans cs_us (« %s »)", n_jl > 1 ? JL[1] : "");

    n = fr_hex(SONY01, b, 64);
    phy_sim_lens_raw(b, n - 1);                     /* à 1100 ms, 40 octets, le dernier à 1100,52 ms */
    run_for(100 * MS);
    jdrain();
    CHECK(n_jl == 1 && !strcmp(JL[0], "* rx 1120 err=framing n=40 F0 29 00 02 00 01 FF 9F FF 5D EE 60 18 DE FF 0F F8 01 "
                                      "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 71 07"),
          "le 0x01 du Sony sans son 55 : un début de trame périmé, écarté 20 ms après, ses 40 octets (« %s »)",
          n_jl ? JL[0] : "");

    for (size_t k = 0; k < 300; k++) b[k] = (uint8_t)k;
    memcpy(b, (const uint8_t[]){0xF0, 0x2C, 0x01, 0x02, 0x00}, 5);
    phy_sim_lens_raw(b, 300);                       /* à 1200 ms, le dernier octet à 1203,9 ms */
    run_for(100 * MS);
    jdrain();
    CHECK(n_jl == 4 && chunks_are("* rx 1223 err=framing n=300 omitted=44", b),
          "une trame de 300 octets : les 256 premiers en quatre morceaux, 44 omis, 20 ms après (%zu ligne(s))", n_jl);

    for (size_t k = 0; k < sizeof b; k++) b[k] = (uint8_t)(k * 7);
    phy_sim_lens_raw(b, sizeof b);                  /* à 1300 ms, 4096 octets écartés au 4096e octet : 1353,248 ms */
    run_for(100 * MS);
    jdrain();
    CHECK(n_jl == 5 && chunks_are("* rx 1353 err=framing n=4096 omitted=3840", b),
          "4096 octets écartés : les 256 premiers de ses 4096 octets en quatre morceaux, 3840 omis (%zu ligne(s))", n_jl);
    CHECK(n_jl == 5 && !strcmp(JL[4], "* rx 1373 err=framing n=4 00 07 0E 15"),
          "puis ses 4 derniers octets, 20 ms après (« %s »)", n_jl == 5 ? JL[4] : "");

    for (size_t k = 0; k < n_log && k < LOG_CAP; k++) sent += LOG[k].kind == A_SEND;
    CHECK(status().session_state == SESSION_OFF && sent == 0, "la session reste en OFF, rien n'est émis (%zu)", sent);
    bsk_journal_set(false, false);
}

/* Le Sony FE 24-105 G répond au 0x01 et rabaisse LENS_CS dès son dernier octet ; l'UART de la carte rend
 * le 55 final après la retombée (phy_sim_lens_raw_late). Le faux 135 hors tension ne répond à rien : la poignée de
 * main va à son échéance (E9), le 0x01 part, et la réponse du Sony est injectée 6 ms après, comme la capture de SONY01
 * (:16-17, 6955 -> 6961). Le 0x01 doit compter comme répondu : le 0x07 suit, pas un second 0x01. */
static void t_sony_late_55(void)
{
    uint8_t b[64];
    size_t n = fr_hex(SONY01, b, sizeof b);
    long i01, irx, i07;
    printf("le Sony : le 55 de sa réponse au 0x01 après la retombée de LENS_CS, la trame entière, le 0x01 répondu\n");
    bench();
    d2(true);
    for (i01 = -1; i01 < 0 && phy_sim_now() < T0 + 2000 * MS; i01 = find(0, A_SEND, 2, 0x01)) run_for(1 * MS);
    CHECK(i01 >= 0, "témoin : le 0x01 part");
    if (i01 < 0) return;
    run_to(LOG[i01].t + 6 * MS);
    phy_sim_lens_raw_late(b, n, 1);
    run_for(50 * MS);
    irx = find((size_t)i01, A_RX, 0, 0x01);
    CHECK(irx >= 0 && LOG[irx].frame.len == 33 && LOG[irx].frame.msg[32] == 0x00 && LOG[irx].t == LOG[i01].t + 6 * MS + 41 * 13,
          "la réponse remonte entière, à son 55, reçu après la retombée de LENS_CS");
    i07 = find((size_t)i01, A_SEND, 2, 0x07);
    CHECK(irx >= 0 && i07 > irx && LOG[i07].t == LOG[irx].t && count_sent(0, phy_sim_now(), 0x01) == 1,
          "le 0x01 compte comme répondu : le 0x07 part à sa réponse, pas de second 0x01");
}

/* ─────────────────────────── RESTORING ─────────────────────────── */

/* La clé de la marque du faux 135 (calculée à la main) ; la valeur rangée : la position (bits 0-15)
 * et le sens (bits 16-23 : 1 croissant, 2 décroissant, 0 inconnu). Le faux 135 finit son init en 16384 (la pose de fin
 * de homing, POS_REST), ses bornes 13878 / 30733 ; c'est le 135 (LensType2 8) : X = 200. */
#define KEY135 "01030800bf087"
#define INC (1u << 16)
#define DEC (2u << 16)

/* Les cibles des 0x1D émis depuis l'entrée `from` du journal, dans l'ordre (1D lo hi 00 00) ; leur nombre. */
static size_t targets_1d(size_t from, uint16_t *t, size_t cap)
{
    size_t n = 0;
    for (size_t i = from; i < n_log && i < LOG_CAP; i++)
        if (LOG[i].kind == A_SEND && LOG[i].frame.msg[0] == 0x04 && LOG[i].frame.len == 19 && LOG[i].frame.msg[14] == 0x1D) {
            if (n < cap) t[n] = (uint16_t)(LOG[i].frame.msg[15] | LOG[i].frame.msg[16] << 8);
            n++;
        }
    return n;
}

/* Le magasin préchargé (`v` sous KEY135), puis le démarrage à froid du faux 135 ; `ms` de temps virtuel. */
static void restore_boot(uint32_t v, uint64_t ms)
{
    bench();
    store_sim_poke(KEY135, v);
    mount();
    run_for(ms * MS);
}

/* Le premier indice de chg[] après `from` qui porte `state`, n_chg sinon. */
static size_t chg_at(size_t from, uint8_t state)
{
    while (from < n_chg && chg[from].state != state) from++;
    return from;
}

/* Chaque trajet, ses cibles calculées à la main depuis 16384, X = 200, bornes 13878 / 30733. Le faux 135 décide la fin de
 * son démarrage sur un seul 0x06 lu : le dernier sens est inconnu, et sur la marque le jeu se reprend par le dépassement
 * (L05-05 ; le dernier sens connu : t_restore_equal de test_session_script.c). */
static void t_restore_paths(void)
{
    static const struct {
        uint32_t v;
        const char *what;
        uint16_t t[2];
        size_t n;
    } C[] = {
        {20000 | INC, "20000, croissant, depuis 16384 : du bon côté, droit sur la marque", {20000}, 1},
        {20000, "20000, sens inconnu : en décroissant, depuis 16384 le mauvais côté : 20200 puis 20000", {20200, 20000}, 2},
        {14400 | INC, "14400, croissant, depuis 16384 le mauvais côté : 14200 puis 14400", {14200, 14400}, 2},
        {14400 | DEC, "14400, décroissant, depuis 16384 : du bon côté, droit sur la marque", {14400}, 1},
        {13950 | INC, "13950, croissant : 13750 sous la borne basse, borné à 13878, puis 13950", {13878, 13950}, 2},
        {30650, "30650, sens inconnu : 30850 au-delà de la borne haute, borné à 30733, puis 30650", {30733, 30650}, 2},
        {16384 | INC, "16384, croissant, sur la marque, le dernier sens inconnu : 16184 puis 16384", {16184, 16384}, 2},
        {16384, "16384, sens inconnu, sur la marque, le dernier sens inconnu : 16584 puis 16384", {16584, 16384}, 2},
        {20000 | 5u << 16, "20000, sens invalide (5) : comme inconnu, en décroissant : 20200 puis 20000",
         {20200, 20000}, 2},
        {14400 | 3u << 16, "14400, sens invalide (3) : en décroissant, du bon côté, droit sur la marque", {14400}, 1},
    };
    printf("RESTORING, faux 135 : chaque trajet, direct ou avec dépassement de 200 pas, borné ; READY sur la marque\n");
    for (size_t k = 0; k < sizeof C / sizeof C[0]; k++) {
        uint16_t t[4] = {0};
        size_t n, ir, iy;
        bsk_status_t st;
        restore_boot(C[k].v, 12000);
        st = status();
        n = targets_1d(0, t, 4);
        ir = chg_at(0, SESSION_RESTORING);
        iy = chg_at(ir, SESSION_READY);
        CHECK(ir < n_chg && ir > 0 && chg[ir - 1].state == SESSION_IDENTIFYING && iy == ir + 1 && iy + 1 == n_chg,
              "%s : IDENTIFYING, RESTORING, READY", C[k].what);
        CHECK(n == C[k].n && t[0] == C[k].t[0] && (n < 2 || t[1] == C[k].t[1]), "%s : %zu 0x1D, %u %u", C[k].what, n, t[0], t[1]);
        CHECK(st.session_state == SESSION_READY && st.motion_state == MOTION_ARRIVED && st.position_valid &&
                  st.focus_position == (int32_t)(C[k].v & 0xFFFF) && l135_position(&L) == (int32_t)(C[k].v & 0xFFFF),
              "%s : READY, ARRIVED en %ld", C[k].what, (long)st.focus_position);
        CHECK(n_acks == 0 && g_store_sim.writes == 0 && st.mark_valid && st.mark_position == (int32_t)(C[k].v & 0xFFFF),
              "%s : aucun accusé, rien d'écrit dans le magasin (la marque ne s'écrit que sur un geste), la marque intacte", C[k].what);
        CHECK(!any_40() && lens_clean() && bsk_bench_refused() == refused0, "%s : ni 0x40, ni refus, ni hors modèle", C[k].what);
    }
}

/* Sans marque, ou une marque hors des bornes du 0x06 (13000 < 13878, 31000 > 30733, rangées sans sens) : READY sans
 * RESTORING, sans 0x1D. */
static void t_restore_none(void)
{
    printf("RESTORING, faux 135 : sans marque, ou une marque hors des bornes : READY sans mouvement\n");
    bench();
    mount();
    run_for(6000 * MS);
    CHECK(status().session_state == SESSION_READY && entered(SESSION_RESTORING, 0) == 0 && count_1d(0, NULL) == 0 &&
              status().focus_position == 16384,
          "sans marque : READY en 16384, ni RESTORING ni 0x1D");
    restore_boot(13000, 6000);
    CHECK(status().session_state == SESSION_READY && entered(SESSION_RESTORING, 0) == 0 && count_1d(0, NULL) == 0 &&
              status().focus_position == 16384 && status().mark_position == 13000,
          "13000, hors des bornes : READY en 16384, ni RESTORING ni 0x1D ; la marque publiée telle quelle");
    restore_boot(31000, 6000);
    CHECK(status().session_state == SESSION_READY && entered(SESSION_RESTORING, 0) == 0 && count_1d(0, NULL) == 0 &&
              status().focus_position == 16384 && status().mark_position == 31000,
          "31000, au-delà de la borne haute (30733) : READY en 16384, ni RESTORING ni 0x1D");
}

/* q pendant le retour à la marque : le 0x1C tout de suite, READY, position valide ; plus de 0x1D. */
static void t_restore_stop(void)
{
    size_t from;
    uint32_t q;
    uint64_t ts;
    long a;
    printf("RESTORING, faux 135 : q arrête le retour à la marque, 0x1C, READY, position valide\n");
    bench();
    store_sim_poke(KEY135, 30000);
    bsk_journal_init(phy_sim_now);
    mount();
    while (find(0, A_SEND, 2, 0x0A) < 0 && phy_sim_now() < T0 + 3000 * MS) run_for(1 * MS);
    bsk_journal_set(true, false);                   /* après le 0x0A : t_restore_journal */
    while (status().motion_state != MOTION_MOVING && phy_sim_now() < T0 + 6000 * MS) run_for(1 * MS);
    run_for(100 * MS);
    CHECK(status().session_state == SESSION_RESTORING && status().motion_state == MOTION_MOVING, "RESTORING, en mouvement");
    from = n_log;
    ts = phy_sim_now();
    q = cmd(CMD_FOCUS_STOP, 0);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL), "q : ACK_ACCEPTED, ACK_COMPLETED");
    CHECK(from < n_log && LOG[from].kind == A_SEND && LOG[from].frame.msg[0] == 0x1C && LOG[from].frame.len == 1 &&
              LOG[from].t == ts,
          "0x1C émis seul, tout de suite");
    CHECK(status().session_state == SESSION_READY && entered(SESSION_READY, ts) == ts && status().motion_state == MOTION_ABORTED,
          "READY au dépôt de q, le mouvement ABORTED");
    jdrain();
    bsk_journal_set(false, false);
    a = jfind(0, "* session ", " ready");
    CHECK(a >= 0 && jfind(0, "* restore ", " end=stop") == a + 1, "« * session <t> ready » puis « * restore <t> end=stop »");
    run_for(2000 * MS);
    CHECK(count_1d(from, NULL) == 0 && count_af(from) == 0, "plus de 0x1D, plus de bit AF");
    CHECK(status().session_state == SESSION_READY && status().position_valid && status().focus_position > 16384 &&
              status().focus_position < 30000 && status().focus_position == l135_position(&L),
          "READY, arrêté en route, position valide, lue sur l'objectif (%ld)", (long)status().focus_position);
    CHECK(g_store_sim.writes == 0, "rien d'écrit dans le magasin (la marque ne s'écrit que sur un geste)");
}

/* Une perte pendant le retour à la marque : comme en HOMING, un échec compté, la reprise ; le faux 135 muet ensuite, les
 * trois démarrages suivants échouent : FAULT au quatrième échec, la perte comprise. Le journal : LOG
 * ON après le 0x0A de l'init (t_restore_journal). Puis le faux 135 muet dès l'entrée en RESTORING : le détecteur de perte
 * part de la dernière télémétrie, comme en READY ; le goto, sans démarrage en 1 s, est STALLED d'abord : READY, puis la perte. */
static uint64_t last_telemetry(void)
{
    uint64_t tl = 0;
    for (long i = find(0, A_RX, 0, 0x05); i >= 0; i = find((size_t)i + 1, A_RX, 0, 0x05)) if (LOG[i].t > tl) tl = LOG[i].t;
    for (long i = find(0, A_RX, 0, 0x06); i >= 0; i = find((size_t)i + 1, A_RX, 0, 0x06)) if (LOG[i].t > tl) tl = LOG[i].t;
    return tl;
}

static void t_restore_loss(void)
{
    uint64_t tr, tlast, te;
    size_t n_rec = 0, ir;
    long a;
    printf("RESTORING, faux 135 : perte pendant le retour, échec compté, reprise ; muet dès l'entrée\n");
    bench();
    store_sim_poke(KEY135, 30000);
    bsk_journal_init(phy_sim_now);
    mount();
    while (find(0, A_SEND, 2, 0x0A) < 0 && phy_sim_now() < T0 + 3000 * MS) run_for(1 * MS);
    bsk_journal_set(true, false);
    while (status().motion_state != MOTION_MOVING && phy_sim_now() < T0 + 6000 * MS) run_for(1 * MS);
    run_for(100 * MS);
    CHECK(status().session_state == SESSION_RESTORING, "RESTORING, en mouvement");
    L.f.silent = true;
    while (status().session_state == SESSION_RESTORING && phy_sim_now() < T0 + 12000 * MS) run_for(1 * MS);
    jdrain();
    bsk_journal_set(false, false);
    a = jfind(0, "* session ", " recovering");
    CHECK(a >= 0 && jfind(0, "* restore ", " end=recovering") == a + 1, "« * session <t> recovering » puis « * restore <t> "
          "end=recovering »");
    run_for(40000 * MS);
    tlast = last_telemetry();
    ir = chg_at(0, SESSION_RESTORING);
    tr = entered(SESSION_RECOVERING, 0);
    CHECK(ir + 1 < n_chg && chg[ir + 1].state == SESSION_RECOVERING && tr == tlast + 2000 * MS + 1,
          "RESTORING -> RECOVERING, 2 s après la dernière télémétrie, sans READY (%lld µs)", (long long)(tr - tlast));
    CHECK(entered(SESSION_POWERING, tr) == tr + 3000 * MS, "la reprise : POWERING 3 s après");
    for (size_t k = 0; k < n_chg; k++) n_rec += chg[k].state == SESSION_RECOVERING;
    CHECK(n_rec == 3 && status().session_state == SESSION_FAULT && status().last_error == E_LOST,
          "la perte comptée : trois reprises, FAULT au quatrième échec (%zu)", n_rec);
    CHECK(g_store_sim.writes == 0, "rien d'écrit dans le magasin (la marque ne s'écrit que sur un geste)");

    bench();
    store_sim_poke(KEY135, 30000);
    mount();
    while (status().session_state != SESSION_RESTORING && phy_sim_now() < T0 + 6000 * MS) run_for(1 * MS);
    te = entered(SESSION_RESTORING, 0);
    L.f.silent = true;
    run_for(5000 * MS);
    tlast = last_telemetry();
    ir = chg_at(0, SESSION_RESTORING);
    /* le 0x1D renvoyé à 1 s et à 2 s, STALLED à 3 s seulement ; la perte (2 s sans télémétrie) arrive avant */
    CHECK(te != 0 && ir + 1 < n_chg && chg[ir + 1].state == SESSION_RECOVERING && chg[ir + 1].t == tlast + 2000 * MS + 1 &&
              tlast + 2000 * MS + 1 < te + 3000 * MS && mv_entered(MOTION_STALLED, te) == 0,
          "muet dès l'entrée : ni STALLED ni READY, RECOVERING 2 s après la dernière télémétrie (%lld µs après l'entrée)",
          (long long)(tlast + 2000 * MS + 1 - te));
}

/* RESTORING après une reprise : READY sur la marque 20000 (croissante, un goto depuis 16384), puis le faux 135 muet 3 s : la
 * perte, RECOVERING ; il reparle : POWERING 3 s après, l'init (son homing le ramène en 16384), et de nouveau RESTORING, un
 * goto à 20000, READY sur la marque. */
static void t_restore_recovery(void)
{
    uint16_t t[4] = {0};
    size_t from, n, k;
    uint64_t tr;
    printf("RESTORING, faux 135 : de nouveau après une reprise réussie\n");
    restore_boot(20000 | INC, 8000);
    CHECK(status().session_state == SESSION_READY && status().focus_position == 20000, "READY sur la marque, 20000");
    L.f.silent = true;
    while (status().session_state != SESSION_RECOVERING && phy_sim_now() < T0 + 20000 * MS) run_for(1 * MS);
    tr = phy_sim_now();
    from = n_log;
    L.f.silent = false;
    run_for(15000 * MS);
    n = targets_1d(from, t, 4);
    for (k = 0; k < n_chg && !(chg[k].t >= tr && chg[k].state == SESSION_RESTORING); k++) {}
    CHECK(k < n_chg && k > 0 && chg[k - 1].state == SESSION_IDENTIFYING && k + 1 < n_chg && chg[k + 1].state == SESSION_READY,
          "RECOVERING, POWERING, IDENTIFYING, puis RESTORING et READY");
    CHECK(n == 1 && t[0] == 20000 && status().session_state == SESSION_READY && status().focus_position == 20000 &&
              l135_position(&L) == 20000,
          "un 0x1D, 20000 ; READY sur la marque (%zu : %u, position %ld)", n, t[0], (long)status().focus_position);
    {
        bool home = false;
        for (long i = find(from, A_RX, 0, 0x06); i >= 0 && !home; i = find((size_t)i + 1, A_RX, 0, 0x06))
            home = (LOG[i].frame.msg[3] | LOG[i].frame.msg[4] << 8) == 16384;
        CHECK(home, "l'init de la reprise l'a ramené en 16384 : le goto l'a bien déplacé");
    }
    CHECK(g_store_sim.writes == 0, "rien d'écrit dans le magasin (la marque ne s'écrit que sur un geste)");
}

/* Le journal : LOG ON après le 0x0A de l'init (la première seconde du démarrage du 135 est pleine : 29 lignes, t_journal). */
static void restore_logged(uint32_t v)
{
    bench();
    store_sim_poke(KEY135, v);
    bsk_journal_init(phy_sim_now);
    mount();
    while (find(0, A_SEND, 2, 0x0A) < 0 && phy_sim_now() < T0 + 3000 * MS) run_for(1 * MS);
    bsk_journal_set(true, false);
    jdrain();
    run_for(10000 * MS);
    jdrain();
    bsk_journal_set(false, false);
}

/* Une ligne par retombée, à l'instant de la coupure (2 ms après), et une par insertion confirmée ; chacune avant
 * la ligne `* session` de sa transition. */
static void t_xdetect_journal(void)
{
    char want[64];
    long a, off, p, pw;
    uint64_t td, ti;
    printf("journal : * xdetect <t> absent à la retombée, * xdetect <t> present à l'insertion confirmée\n");
    ready();
    bsk_journal_init(phy_sim_now);
    bsk_journal_set(true, false);
    jdrain();
    run_for(1500 * MS);                              /* hors de la fenêtre du plafond où tombe la fin du démarrage */
    jdrain();
    td = phy_sim_now() + 7 * MS + 123;
    phy_sim_run(td);
    phy_sim_d2(false);
    run_for(100 * MS);
    jdrain();
    snprintf(want, sizeof want, "* xdetect %llu absent", (unsigned long long)((td + DROP) / 1000));
    a = jfind(0, want, NULL);
    off = jfind(0, "* session ", " off");
    CHECK(a >= 0 && !strcmp(JL[a], want) && off > a, "« %s », puis « * session <t> off » (%ld, %ld)", want, a, off);
    CHECK(jfind(0, "* xdetect ", NULL) == a && jfind((size_t)a + 1, "* xdetect ", NULL) < 0, "une seule ligne * xdetect");
    l135_power(&L, phy_sim_now(), true);
    ti = phy_sim_now();
    d2(true);
    run_for(400 * MS);
    jdrain();
    snprintf(want, sizeof want, "* xdetect %llu present", (unsigned long long)((ti + D2_DEBOUNCE_US) / 1000));
    p = jfind(0, want, NULL);
    pw = jfind(0, "* session ", " powering");
    CHECK(p >= 0 && !strcmp(JL[p], want) && pw > p, "« %s », 300 ms après, puis « * session <t> powering »", want);
    CHECK(jfind(0, "* xdetect ", NULL) == p && jfind((size_t)p + 1, "* xdetect ", NULL) < 0, "une seule ligne * xdetect");
    bsk_journal_set(false, false);
}

/* Un rebond d'1 ms, READY, goto en vol : rien de coupé, rien ne change pour la SESSION, une ligne au journal. */
static void t_xdetect_bounce(void)
{
    char want[64];
    long b;
    uint64_t td;
    size_t from;
    uint32_t g;
    printf("XDETECT absent 1 ms pendant un goto : rien de coupé, la SESSION inchangée, * xdetect <t> bounce\n");
    ready();
    bsk_journal_init(phy_sim_now);
    bsk_journal_set(true, false);
    run_for(1500 * MS);                              /* hors de la fenêtre du plafond où tombe la fin du démarrage */
    jdrain();
    g = cmd(CMD_FOCUS_GOTO, 30000);
    run_for(100 * MS);
    CHECK(status().motion_state == MOTION_MOVING, "un goto en vol");
    td = phy_sim_now();
    from = n_chg;
    phy_sim_d2(false);
    run_to(td + 1 * MS);
    phy_sim_d2(true);
    run_to(td + DROP);
    CHECK(!rest_all() && rails_up() && status().session_state == SESSION_READY && status().motion_state == MOTION_MOVING,
          "à l'échéance, D2 revenu : rien de coupé, READY, le goto continue");
    run_for(100 * MS);
    jdrain();
    snprintf(want, sizeof want, "* xdetect %llu bounce", (unsigned long long)((td + DROP) / 1000));
    b = jfind(0, want, NULL);
    CHECK(b >= 0 && !strcmp(JL[b], want), "« %s »", want);
    CHECK(jfind(0, "* xdetect ", NULL) == b && jfind((size_t)b + 1, "* xdetect ", NULL) < 0 && jfind(0, "* session ", NULL) < 0,
          "ni absence, ni présence, ni transition au journal");
    CHECK(n_chg == from, "aucune transition de SESSION");
    run_for(3000 * MS);
    CHECK(accepted_then(g, ACK_COMPLETED, E_OK, NULL), "le goto arrive");
    bsk_journal_set(false, false);
}

static void t_restore_journal(void)
{
    long a, b, c, d;
    printf("RESTORING, faux 135 : * restore à l'entrée et à la sortie, après la transition\n");
    restore_logged(20000);
    a = jfind(0, "* session ", " restoring");
    b = jfind(0, "* restore ", " mark=20000 dir=unknown x=200 path=overshoot via=20200");
    c = jfind(0, "* session ", " ready");
    d = jfind(0, "* restore ", " end=arrived");
    CHECK(a >= 0 && b == a + 1, "« * session <t> restoring » puis « * restore <t> mark=20000 dir=unknown x=200 path=overshoot "
          "via=20200 » (%ld, %ld)", a, b);
    CHECK(c > b && d == c + 1, "« * session <t> ready » puis « * restore <t> end=arrived » (%ld, %ld)", c, d);
    restore_logged(20000 | INC);
    CHECK(jfind(0, "* restore ", " mark=20000 dir=inc x=200 path=direct") >= 0 && jfind(0, "* restore ", " end=arrived") >= 0,
          "« * restore <t> mark=20000 dir=inc x=200 path=direct », « end=arrived »");
    restore_logged(13000);
    a = jfind(0, "* session ", " ready");
    CHECK(a >= 0 && jfind(0, "* restore ", " skip=limit mark=13000") == a + 1 && jfind(0, "* session ", " restoring") < 0,
          "hors des bornes : « * session <t> ready » puis « * restore <t> skip=limit mark=13000 »");
    bench();
    bsk_journal_init(phy_sim_now);
    mount();
    while (find(0, A_SEND, 2, 0x0A) < 0 && phy_sim_now() < T0 + 3000 * MS) run_for(1 * MS);
    bsk_journal_set(true, false);
    jdrain();
    run_for(3000 * MS);
    jdrain();
    bsk_journal_set(false, false);
    a = jfind(0, "* session ", " ready");
    CHECK(a >= 0 && jfind(0, "* restore ", " skip=nomark") == a + 1, "sans marque : « * session <t> ready » puis « * restore <t> "
          "skip=nomark »");
}

/* ─────────────────────────── les renvois de l'init ─────────────────────────── */

/* L'entrée de la k-ième trame (0 : la première) émise ou reçue de type `type`, -1 sinon. */
static long nth(act_kind_t kind, uint8_t cls, uint8_t type, unsigned k)
{
    long i = find(0, kind, cls, type);
    while (i >= 0 && k--) i = find((size_t)i + 1, kind, cls, type);
    return i;
}

static unsigned n_sent(uint8_t cls, uint8_t type)
{
    unsigned n = 0;
    while (nth(A_SEND, cls, type, n) >= 0) n++;
    return n;
}

/* Deux entrées du journal du fil portent le même message (renvoyé tel quel). */
static bool same_msg(long a, long b)
{
    return a >= 0 && b >= 0 && LOG[a].frame.len == LOG[b].frame.len && !memcmp(LOG[a].frame.msg, LOG[b].frame.msg, LOG[a].frame.len);
}

/* Les émissions de la requête d'init `type` : `n` exactement, le même message, la k-ième à t0 + dt[k] (µs). */
static bool sends_at(uint8_t type, unsigned n, const uint64_t *dt)
{
    long a = nth(A_SEND, 2, type, 0);
    bool ok = a >= 0 && n_sent(2, type) == n;
    for (unsigned k = 1; ok && k < n; k++) {
        long b = nth(A_SEND, 2, type, k);
        ok = same_msg(a, b) && LOG[b].t == LOG[a].t + dt[k];
    }
    if (!ok) {
        printf("  0x%02X émis %u fois :", type, n_sent(2, type));
        for (unsigned k = 0; nth(A_SEND, 2, type, k) >= 0; k++)
            printf(" +%llu µs", (unsigned long long)(LOG[nth(A_SEND, 2, type, k)].t - LOG[a].t));
        printf("\n");
    }
    return ok;
}

/* La ligne `line` du journal, exactement. */
static bool jline(const char *line)
{
    for (size_t i = 0; i < n_jl && i < sizeof JL / sizeof JL[0]; i++)
        if (!strcmp(JL[i], line)) return true;
    printf("  absente du journal : « %s »\n", line);
    return false;
}

static void t_resend_init(void)
{
    static const uint8_t once[] = {0x01, 0x3F, 0x0B, 0x09, 0x0D, 0x10, 0x0A};
    rule_t *r07, *r08;
    long b07, b08;
    bsk_status_t s;
    char l[80];
    printf("renvois de l'init : la réponse au 0x07 perdue, celle au 0x08 reçue fausse ; renvoyés à leur échéance, init complète\n");
    bench();
    bsk_journal_init(phy_sim_now);
    bsk_journal_set(true, false);
    r07 = lose(false, 0x07, 1, PHY_SIM_LOSE);
    r08 = lose(false, 0x08, 1, PHY_SIM_GARBLE);
    mount();
    run_for(4000 * MS);
    jdrain();
    s = status();
    CHECK(r07->hit == 1 && sends_at(0x07, 2, (const uint64_t[]){0, 300 * MS}),
          "0x07 : sa réponse perdue, le même 0x07 renvoyé à son échéance (300 ms), une fois");
    b08 = nth(A_SEND, 2, 0x08, 1);
    CHECK(r08->hit == 1 && b08 >= 0 && sends_at(0x08, 2, (const uint64_t[]){0, 400 * MS}) && LOG[b08].t > r08->t_hit,
          "0x08 : sa réponse reçue fausse, l'erreur ne coûte que la trame : le même 0x08 renvoyé à son échéance (400 ms), une "
          "fois");
    CHECK(jfind(0, "* rx ", " err=framing") >= 0, "l'erreur de réception reste au journal : « * rx <t> err=framing »");
    for (size_t k = 0; k < sizeof once; k++) CHECK(n_sent(2, once[k]) == 1, "0x%02X émis une fois", once[k]);
    CHECK(s.session_state == SESSION_READY && s.lens_id_product == 8 && !strcmp(s.lens_name, "SAMYANG AF 135mm F1.8") &&
              (s.capabilities & CAP_APERTURE) && entered(SESSION_RECOVERING, 0) == 0,
          "READY sans reprise : l'identité, le nom et la plage d'ouverture publiés");
    b07 = nth(A_SEND, 2, 0x07, 1);
    snprintf(l, sizeof l, "* resend %llu msg=07 n=2 why=timeout", (unsigned long long)(b07 >= 0 ? LOG[b07].t / 1000 : 0));
    CHECK(jline(l), "journal : le renvoi du 0x07, sa cause l'échéance");
    snprintf(l, sizeof l, "* resend %llu msg=08 n=2 why=timeout", (unsigned long long)(b08 >= 0 ? LOG[b08].t / 1000 : 0));
    CHECK(jline(l), "journal : le renvoi du 0x08, sa cause l'échéance");
    CHECK(jfind(0, "* resend ", "giveup") < 0, "aucun abandon");
}

/* Trois échecs : l'abandon, chaque requête selon sa règle (le 0x07 toléré, le 0x0B exigé). */
static void t_resend_init_3(void)
{
    rule_t *r;
    bsk_status_t s;
    char l[80];
    long c;
    printf("renvois de l'init : trois réponses perdues ou fausses, la conduite d'avant\n");

    bench();                                        /* 0x07 : toléré, l'init continue sans identité */
    bsk_journal_init(phy_sim_now);
    bsk_journal_set(true, false);
    r = lose(false, 0x07, 3, PHY_SIM_LOSE);
    mount();
    run_for(4000 * MS);
    jdrain();
    s = status();
    CHECK(r->hit == 3 && sends_at(0x07, 3, (const uint64_t[]){0, 300 * MS, 600 * MS}),
          "0x07 sans réponse : trois envois, chacun à l'échéance du précédent");
    c = nth(A_SEND, 2, 0x07, 2);
    CHECK(c >= 0 && nth(A_SEND, 2, 0x3F, 0) >= 0 && LOG[nth(A_SEND, 2, 0x3F, 0)].t == LOG[c].t + 300 * MS,
          "… puis le 0x3F, à l'échéance du troisième");
    CHECK(s.session_state == SESSION_READY && s.lens_id_product == 0 && !strcmp(s.lens_name, "SAMYANG AF 135mm F1.8"),
          "READY, sans identité du 0x07 (toléré, std.c:334-335), reconnu par son nom");
    snprintf(l, sizeof l, "* resend %llu msg=07 giveup why=timeout", (unsigned long long)(c >= 0 ? (LOG[c].t + 300 * MS) / 1000 : 0));
    CHECK(jline(l), "journal : l'abandon du 0x07, à l'échéance du troisième envoi");

    bench();                                        /* 0x0B : exigé, la séquence s'arrête (init_end) */
    r = lose(false, 0x0B, 3, PHY_SIM_GARBLE);
    mount();
    run_for(8000 * MS);
    c = nth(A_SEND, 2, 0x0B, 2);
    CHECK(r->hit == 3 && c >= 0 && n_sent(2, 0x0B) == 4 && nth(A_SEND, 2, 0x09, 0) > nth(A_SEND, 2, 0x01, 1) &&
              nth(A_SEND, 2, 0x01, 1) > c,
          "0x0B faux trois fois : trois envois, puis plus aucune requête d'init de cette session");
    {
        uint64_t te = c >= 0 ? LOG[c].t + 300 * MS : 0;   /* l'échéance du troisième envoi (init_ms) */
        CHECK(c >= 0 && LOG[c].t - LOG[nth(A_SEND, 2, 0x0B, 0)].t == 600 * MS && r->t_hit < te &&
                  count_sent(0, te, 0x03) == 0 && count_sent(te, te + 17 * MS, 0x03) >= 1,
              "chaque envoi à l'échéance du précédent (300 ms), la boucle lancée à l'échéance du troisième (init_end), pas à "
              "sa réponse fausse");
        /* le 135 n'ouvre son flux qu'au 0x0A (samyang.md § 2.3) : aucun 0x05, non servi, compté */
        CHECK(entered(SESSION_RECOVERING, 0) == te + 500 * MS + 300 * MS && entered(SESSION_READY, 0) > 0 &&
                  status().session_state == SESSION_READY,
              "… sans flux, non servi : RECOVERING (500 + 300 ms), puis la reprise, init complète, READY");
    }
}

/* Le 0x0A et le 0x10 jamais renvoyés : leur réponse perdue, chacun finit à son échéance. */
static void t_resend_never(void)
{
    rule_t *r;
    long i;
    printf("renvois de l'init : ni le 0x0A ni le 0x10, leur réponse perdue\n");

    bench();
    r = lose(false, 0x0A, 1, PHY_SIM_LOSE);
    mount();
    run_for(15000 * MS);
    CHECK(r->hit == 1 && n_sent(2, 0x0A) == 1, "0x0A : sa réponse perdue, jamais renvoyé");
    i = find(0, A_SEND, 1, 0x1C);
    CHECK(i >= 0 && LOG[i].t == LOG[nth(A_SEND, 2, 0x0A, 0)].t + 400 * MS && nth(A_SEND, 2, 0x10, 1) >= 0 &&
              LOG[nth(A_SEND, 2, 0x10, 1)].frame.msg[1] == 0x08 && status().session_state == SESSION_READY,
          "… init non faite : à son échéance, homing du pilote (0x1C, 0x10 08), READY");

    bench();
    r = lose(false, 0x10, 1, PHY_SIM_LOSE);
    mount();
    run_for(20000 * MS);
    i = nth(A_SEND, 2, 0x10, 0);
    CHECK(r->hit == 1 && i >= 0 && LOG[i].frame.msg[1] == 0x1F && nth(A_SEND, 2, 0x01, 1) >= 0 &&
              count_sent(0, LOG[nth(A_SEND, 2, 0x01, 1)].t, 0x10) == 1 && count_sent(0, LOG[nth(A_SEND, 2, 0x01, 1)].t, 0x0A) == 0,
          "0x10 1F : sa réponse perdue, jamais renvoyé, pas de 0x0A");
    /* à son échéance, 8 s, init_end ; le 135 n'ouvre son flux qu'au 0x0A : non servi, compté, la reprise */
    CHECK(i >= 0 && entered(SESSION_RECOVERING, 0) == LOG[i].t + 8000 * MS + 500 * MS + 300 * MS &&
              status().session_state == SESSION_READY,
          "… à son échéance (8 s), init_end, non servi (500 + 300 ms), RECOVERING, puis la reprise, READY");
}

/* ─────────────────────────── le renvoi du goto ─────────────────────────── */

/* L'entrée de la k-ième trame 0x04 qui porte un 0x1D, depuis l'entrée `from` du journal du fil ; -1 sinon. */
static long nth_1d(size_t from, unsigned k)
{
    for (size_t i = from; i < n_log && i < LOG_CAP; i++)
        if (LOG[i].kind == A_SEND && LOG[i].frame.msg[0] == 0x04 && LOG[i].frame.len == 19 && LOG[i].frame.msg[14] == 0x1D &&
            k-- == 0)
            return (long)i;
    return -1;
}

/* Le 0x04 de la première paire émise à `t` ou après, depuis l'entrée `from` ; -1 sinon. */
static long pair_at(size_t from, uint64_t t)
{
    for (size_t i = from; i < n_log && i < LOG_CAP; i++)
        if (LOG[i].kind == A_SEND && LOG[i].frame.msg[0] == 0x04 && LOG[i].t >= t) return (long)i;
    return -1;
}

static bool same_1d(long a, long b) { return a >= 0 && b >= 0 && !memcmp(LOG[a].frame.msg + 14, LOG[b].frame.msg + 14, 5); }

static void t_resend_goto(void)
{
    rule_t *r;
    size_t from, m0;
    uint64_t t0, tf;
    uint32_t q;
    long a, b, c;
    char l[80];
    printf("goto : le 0x1D perdu, renvoyé à 1 s, arrivée ; trois perdus, STALLED à 3 s\n");
    ready();
    bsk_journal_init(phy_sim_now);
    bsk_journal_set(true, false);
    jdrain();
    r = lose(true, 0x1D, 1, PHY_SIM_LOSE);
    from = n_log;
    m0 = n_mchg;
    t0 = phy_sim_now();
    q = cmd(CMD_FOCUS_GOTO, 20000);
    run_for(4000 * MS);
    jdrain();
    a = nth_1d(from, 0);
    b = nth_1d(from, 1);
    CHECK(r->hit == 1 && a >= 0 && a == pair_at(from, t0) && b >= 0 && b == pair_at(from, t0 + 1000 * MS) && same_1d(a, b) &&
              nth_1d(from, 2) < 0,
          "le 0x1D perdu : le même, sur la première paire après 1 s sans démarrage ni accusé, une fois");
    CHECK(b >= 0 && (LOG[b - 1].frame.msg[0] == 0x03 && (LOG[b - 1].frame.msg[13] & 0x10)) && count_af(from) == 60,
          "le bit AF tenu 30 paires à chaque envoi, comme au premier (%zu)", count_af(from));
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL) && motion_seq(m0, MV_FULL, 4) && status().focus_position == 20000 &&
              l135_position(&L) == 20000,
          "puis l'arrivée : COMMANDED, MOVING, SETTLING, ARRIVED, en 20000");
    snprintf(l, sizeof l, "* resend %llu msg=1D n=2 why=timeout", (unsigned long long)((t0 + 1000 * MS) / 1000));
    CHECK(jline(l), "journal : le renvoi du 0x1D, à 1 s");

    ready();
    bsk_journal_init(phy_sim_now);
    bsk_journal_set(true, false);
    jdrain();
    r = lose(true, 0x1D, 3, PHY_SIM_LOSE);
    from = n_log;
    m0 = n_mchg;
    t0 = phy_sim_now();
    q = cmd(CMD_FOCUS_GOTO, 20000);
    run_for(4000 * MS);
    jdrain();
    c = nth_1d(from, 2);
    CHECK(r->hit == 3 && nth_1d(from, 1) == pair_at(from, t0 + 1000 * MS) && c >= 0 && c == pair_at(from, t0 + 2000 * MS) &&
              nth_1d(from, 3) < 0,
          "trois 0x1D perdus : trois envois, à 0, 1 et 2 s, pas un quatrième");
    CHECK(accepted_then(q, ACK_FAILED, E_STALL, &tf) && tf == t0 + 3000 * MS && mv_entered(MOTION_STALLED, t0) == t0 + 3000 * MS &&
              status().session_state == SESSION_READY && l135_position(&L) == 16384,
          "STALLED, ACK_FAILED E_STALL, à 3 s exactement : la fin de la troisième fenêtre ; READY ; l'objectif n'a pas bougé");
    snprintf(l, sizeof l, "* resend %llu msg=1D giveup why=timeout", (unsigned long long)((t0 + 3000 * MS) / 1000));
    CHECK(jline(l), "journal : l'abandon du 0x1D, à 3 s");
}

/* Un 0x1D reçu n'est jamais renvoyé : le 135 démarre, ou l'accuse (1D 00 pour une cible égale à sa position,
 * samyang.md § 4.3), bien avant 1 s. Un renvoi l'évincerait et recevrait 1D 00 : une fausse arrivée. */
static void t_resend_goto_none(void)
{
    static const uint8_t mv_settle[] = {MOTION_COMMANDED, MOTION_SETTLING, MOTION_ARRIVED};
    size_t from, m0;
    uint32_t q;
    printf("goto : un 0x1D reçu n'est jamais renvoyé, une cible égale à la position comprise\n");
    ready();
    from = n_log;
    m0 = n_mchg;
    q = cmd(CMD_FOCUS_GOTO, 20000);
    run_for(5000 * MS);
    CHECK(nth_1d(from, 0) >= 0 && nth_1d(from, 1) < 0 && accepted_then(q, ACK_COMPLETED, E_OK, NULL) &&
              motion_seq(m0, MV_FULL, 4),
          "un goto : un seul 0x1D, ARRIVED");
    from = n_log;
    m0 = n_mchg;
    q = cmd(CMD_FOCUS_GOTO, 20000);
    run_for(5000 * MS);
    CHECK(nth_1d(from, 0) >= 0 && nth_1d(from, 1) < 0 && accepted_then(q, ACK_COMPLETED, E_OK, NULL) &&
              motion_seq(m0, mv_settle, 3),
          "la cible égale à la position : un seul 0x1D, accusé 1D 00, SETTLING, ARRIVED");
}

/* ─────────────────────────── la surveillance de l'arrêt ─────────────────────────── */

/* L'entrée du k-ième 0x1C émis depuis `from` ; -1 sinon. */
static long nth_1c(size_t from, unsigned k)
{
    for (size_t i = from; i < n_log && i < LOG_CAP; i++)
        if (LOG[i].kind == A_SEND && LOG[i].frame.cls == 1 && LOG[i].frame.msg[0] == 0x1C && k-- == 0) return (long)i;
    return -1;
}

/* Un 0x06 reçu depuis `from` que suit l'accusé `a v` (samyang.md § 4.4). */
static long ack_rx(size_t from, uint8_t a, uint8_t v)
{
    for (size_t i = from; i < n_log && i < LOG_CAP; i++) {
        const bsk_frame_t *f = &LOG[i].frame;
        if (LOG[i].kind != A_RX || f->msg[0] != 0x06) continue;
        for (size_t k = 40; k + 1 < f->len; k += 2)
            if (f->msg[k] == a && f->msg[k + 1] == v) return (long)i;
    }
    return -1;
}

/* READY, un goto de 16384 à 30700 (1,45 s de mouvement), en mouvement depuis 50 ms. */
static void moving_30700(void)
{
    ready();
    (void)cmd(CMD_FOCUS_GOTO, 30700);
    while (status().motion_state != MOTION_MOVING && phy_sim_now() < T0 + 10000 * MS) run_for(1 * MS);
    run_for(50 * MS);
}

/* q pendant un goto : le 0x1C perdu, ni accusé ni immobilité à 1,5 s (le 135 roule jusqu'à 30700, arrivé à 1,45 s du départ,
 * immobile 300 ms plus tard), le même 0x1C, puis l'arrêt accusé, 1C 00 ; reçu : 1C 00, jamais renvoyé ; q à l'arrêt, son
 * accusé perdu : l'immobilité éteint la surveillance, jamais renvoyé ; la session quittée l'oublie. */
static void t_resend_stop(void)
{
    rule_t *r;
    size_t from;
    uint64_t tq;
    uint32_t q;
    long b;
    char l[80];
    printf("q : le 0x1C perdu, renvoyé à 1,5 s, 1C 00 ; reçu : jamais ; l'accusé perdu, immobile : jamais\n");
    moving_30700();
    bsk_journal_init(phy_sim_now);
    bsk_journal_set(true, false);
    jdrain();
    r = lose(true, 0x1C, 1, PHY_SIM_LOSE);
    from = n_log;
    tq = phy_sim_now();
    q = cmd(CMD_FOCUS_STOP, 0);
    run_for(6000 * MS);
    jdrain();
    b = nth_1c(from, 1);
    CHECK(r->hit == 1 && nth_1c(from, 0) >= 0 && LOG[nth_1c(from, 0)].t == tq && b >= 0 && LOG[b].t == tq + 1500 * MS &&
              nth_1c(from, 2) < 0,
          "le 0x1C perdu : le même à 1,5 s exactement, une fois");
    CHECK(b >= 0 && ack_rx((size_t)b, 0x1C, 0x00) >= 0 && accepted_then(q, ACK_COMPLETED, E_OK, NULL),
          "puis l'arrêt accusé, 1C 00 ; q : ACK_COMPLETED");
    snprintf(l, sizeof l, "* resend %llu msg=1C n=2 why=timeout", (unsigned long long)((tq + 1500 * MS) / 1000));
    CHECK(jline(l), "journal : le renvoi du 0x1C, à 1,5 s");

    moving_30700();
    from = n_log;
    (void)cmd(CMD_FOCUS_STOP, 0);
    run_for(6000 * MS);
    CHECK(nth_1c(from, 0) >= 0 && nth_1c(from, 1) < 0 && ack_rx(from, 0x1C, 0x00) >= 0 && l135_position(&L) < 30700,
          "le 0x1C reçu : 1C 00, arrêté en route, jamais renvoyé");

    ready();
    r = lose(false, 0x1C, 1, PHY_SIM_LOSE);
    from = n_log;
    (void)cmd(CMD_FOCUS_STOP, 0);
    run_for(6000 * MS);
    CHECK(r->hit == 1 && nth_1c(from, 0) >= 0 && nth_1c(from, 1) < 0 && ack_rx(from, 0x1C, 0x00) < 0,
          "à l'arrêt, son accusé 1C 00 perdu : l'immobilité suffit, jamais renvoyé");

    moving_30700();                                 /* la session quittée : la surveillance oubliée avec elle */
    r = lose(true, 0x1C, 1, PHY_SIM_LOSE);
    from = n_log;
    (void)cmd(CMD_FOCUS_STOP, 0);
    run_for(100 * MS);
    (void)cmd(CMD_ATTACH, 0);
    run_for(5000 * MS);
    CHECK(r->hit == 1 && entered(SESSION_POWERING, T0 + 1) > 0 && status().session_state == SESSION_READY &&
              nth_1c(from, 1) < 0,
          "le 0x1C perdu, puis une nouvelle session (CMD_ATTACH) : jamais renvoyé");
}

/* ─────────────────────────── perte, reprise, faute ─────────────────────────── */

/* Le critère de sortie de la phase 2 (spec § 13), en simulation : un objectif en mouvement vers une cible, une marque rangée,
 * perd l'alimentation. Le faux 135 READY en 16384 ; un goto à 20000, la marque posée là (CMD_SET_MARK, ici : 20000, sens
 * croissant, la valeur 0x14E20) ; un goto à 28000, et l'alimentation coupée en plein mouvement (l135_power) : il se tait. La
 * perte 2 s après sa dernière télémétrie (plus 1 µs, on_frame), le goto en vol fini ACK_FAILED E_LOST à cet instant ;
 * l'alimentation revient 1 s après (un choix du test : une coupure brève, l'objectif rallumé pendant l'attente de la reprise) ;
 * POWERING 3 s après la perte, la poignée de main, l'init d'un objectif froid (son homing le ramène en 16384), puis RESTORING :
 * la marque relue sous la clé du 135, croissante, au-dessus de 16384 : du bon côté, un seul goto, 20000 ; READY sur la marque.
 * Rien d'autre n'est écrit dans le magasin que la marque posée. L'essai au banc reste à faire par l'humain. */
static void t_power_loss_restore(void)
{
    uint16_t t[4] = {0};
    uint32_t g, mk, v = 0;
    uint64_t toff, tl, tr, tf = 0, ton;
    size_t from, n, k;
    bool home = false;
    printf("perte d'alimentation en plein goto, une marque rangée : perte, reprise, init, RESTORING, la marque (§ 13)\n");
    ready();
    g = cmd(CMD_FOCUS_GOTO, 20000);
    run_for(3000 * MS);
    CHECK(accepted_then(g, ACK_COMPLETED, E_OK, NULL) && status().focus_position == 20000, "en 20000");
    mk = cmd(CMD_SET_MARK, BSK_MARK_HERE);
    run_for(100 * MS);
    CHECK(accepted_then(mk, ACK_COMPLETED, E_OK, NULL) && store_sim_peek(KEY135, &v) && v == 0x14E20 && g_store_sim.writes == 1,
          "la marque rangée sous " KEY135 " : 20000, croissant, 0x14E20 (0x%X)", v);
    g = cmd(CMD_FOCUS_GOTO, 28000);
    while (status().motion_state != MOTION_MOVING && phy_sim_now() < T0 + 20000 * MS) run_for(1 * MS);
    run_for(100 * MS);
    CHECK(status().motion_state == MOTION_MOVING && l135_position(&L) > 20000 && l135_position(&L) < 28000,
          "en mouvement vers 28000 (%ld)", (long)l135_position(&L));
    toff = phy_sim_now();
    l135_power(&L, toff, false);
    while (status().session_state != SESSION_RECOVERING && phy_sim_now() < toff + 5000 * MS) run_for(1 * MS);
    tl = last_telemetry();
    tr = entered(SESSION_RECOVERING, toff);
    CHECK(tl <= toff && tr == tl + 2000 * MS + 1, "la perte : RECOVERING 2 s après la dernière télémétrie (%lld µs)",
          (long long)(tr - tl));
    CHECK(accepted_then(g, ACK_FAILED, E_LOST, &tf) && tf == tr && status().motion_state == MOTION_IDLE,
          "le goto en vol : ACK_FAILED, E_LOST, à l'entrée en RECOVERING ; le mouvement oublié");
    ton = tr + 1000 * MS;
    run_to(ton);
    from = n_log;
    l135_power(&L, ton, true);
    run_for(15000 * MS);
    CHECK(entered(SESSION_POWERING, tr) == tr + 3000 * MS, "la reprise : POWERING 3 s après la perte");
    for (k = 0; k < n_chg && chg[k].t < tr; k++) {}
    CHECK(n_chg == k + 5 && chg[k].state == SESSION_RECOVERING && chg[k + 1].state == SESSION_POWERING &&
              chg[k + 2].state == SESSION_IDENTIFYING && chg[k + 3].state == SESSION_RESTORING && chg[k + 4].state == SESSION_READY,
          "RECOVERING, POWERING, IDENTIFYING, RESTORING, READY, et rien d'autre (%zu changements)", n_chg - k);
    CHECK(find(from, A_SEND, 2, 0x01) >= 0 && find((size_t)find(from, A_SEND, 2, 0x01), A_RX, 0, 0x01) >= 0 &&
              find(from, A_SEND, 2, 0x0A) >= 0,
          "une nouvelle init : le 0x01 répondu, jusqu'au 0x0A");
    for (long i = find(from, A_RX, 0, 0x06); i >= 0 && !home; i = find((size_t)i + 1, A_RX, 0, 0x06))
        home = (LOG[i].frame.msg[3] | LOG[i].frame.msg[4] << 8) == 16384;
    CHECK(home, "l'init de la reprise l'a ramené en 16384");
    n = targets_1d(from, t, 4);
    CHECK(n == 1 && t[0] == 20000, "RESTORING : un 0x1D, 20000, du bon côté (%zu : %u)", n, t[0]);
    CHECK(status().session_state == SESSION_READY && status().motion_state == MOTION_ARRIVED && status().position_valid &&
              status().focus_position == 20000 && l135_position(&L) == 20000 && status().mark_valid &&
              status().mark_position == 20000 && status().last_error == E_OK,
          "READY sur la marque, ARRIVED en 20000 (%ld)", (long)status().focus_position);
    CHECK(g_store_sim.writes == 1 && n_acks == 6, "rien d'écrit de plus dans le magasin (la marque ne s'écrit que sur un geste), aucun accusé de plus (%zu)", n_acks);
    CHECK(!any_40() && lens_clean() && bsk_bench_refused() == refused0, "ni 0x40, ni refus, ni hors modèle");
}

/* Les commandes de sortie en RECOVERING (spec § 3.1) : CMD_DETACH refusé, E_BUSY, comme pendant un
 * démarrage ; CMD_CLEAR_FAULT sans effet hors FAULT ; CMD_ATTACH, POWERING tout de suite, le compteur à zéro. Le
 * faux 135 muet : chaque essai échoue (six 0x01, le reset soft). Trois échecs comptés, la troisième reprise : CMD_ATTACH y repart
 * avec un compteur à zéro, quatre essais (24 0x01) avant le FAULT, là où le compteur gardé n'en laisserait qu'un (6). */
static void t_recovering_cmds(void)
{
    uint32_t q;
    uint64_t tr, tc;
    size_t nr = 0, from;
    printf("RECOVERING : CMD_DETACH refusé, CMD_CLEAR_FAULT sans effet, CMD_ATTACH POWERING et compteur à zéro\n");
    bench();
    L.f.silent = true;
    mount();
    while (status().session_state != SESSION_RECOVERING && phy_sim_now() < T0 + 10000 * MS) run_for(1 * MS);
    tr = entered(SESSION_RECOVERING, 0);
    run_for(1000 * MS);
    from = n_log;
    q = cmd(CMD_DETACH, 0);
    CHECK(rejected(q, E_BUSY) && status().session_state == SESSION_RECOVERING, "CMD_DETACH en RECOVERING : E_BUSY, toujours RECOVERING");
    q = cmd(CMD_CLEAR_FAULT, 0);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL) && status().session_state == SESSION_RECOVERING,
          "CMD_CLEAR_FAULT en RECOVERING : ACK_COMPLETED, toujours RECOVERING");
    CHECK(quiet_from(from), "ni l'une ni l'autre ne fait rien sur le fil");
    run_for(2500 * MS);
    CHECK(tr != 0 && entered(SESSION_OFF, tr) == 0 && entered(SESSION_POWERING, tr) == tr + 3000 * MS,
          "la reprise suit son cours : POWERING 3 s après RECOVERING, jamais OFF");
    while (phy_sim_now() < T0 + 30000 * MS) {
        nr = 0;
        for (size_t k = 0; k < n_chg; k++) nr += chg[k].state == SESSION_RECOVERING;
        if (nr == 3 && status().session_state == SESSION_RECOVERING) break;
        run_for(1 * MS);
    }
    CHECK(nr == 3 && status().session_state == SESSION_RECOVERING, "trois échecs comptés : la troisième reprise");
    tc = phy_sim_now();
    q = cmd(CMD_ATTACH, 0);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK, NULL) && status().session_state == SESSION_POWERING &&
              entered(SESSION_POWERING, tc) == tc,
          "CMD_ATTACH en RECOVERING : POWERING tout de suite, ACK_COMPLETED");
    CHECK(tries_to_fault() == 24, "compteur à zéro : quatre essais de six 0x01 avant le FAULT");
}

/* Le bouton du fût hors de READY (un appui commencé hors de READY est oublié, toute sortie de READY oublie l'appui avec
 * la session). Le faux 135 publie son bouton dans chaque 0x05 (lens135.c,
 * offset 64 bit 3) ; resté alimenté, en flux (t_powered), ses 0x05 sont lus dès la boucle lancée avec le 0x10 de l'init
 * (IDENTIFYING). (1) Né et relâché en IDENTIFYING, pendant le 0x10 de l'init : une ligne `* btn … result=ignore
 * why=identifying`, ni marque ni goto. (2) Né en READY, puis CMD_ATTACH
 * (READY -> POWERING) ; le bouton tenu, relâché dans le READY de la session suivante, 3 s après l'appui : l'appui de la session
 * quittée est oublié, celui que la session suivante voit naît à son premier 0x05, en IDENTIFYING, et il est ignoré ; un appui
 * né en READY et relâché 3 s après poserait la marque (appui long). */
static void t_button_outside(void)
{
    long i10;
    uint64_t tp;
    bool ok_ready;
    printf("bouton : né et relâché en IDENTIFYING ; né en READY, relâché après une sortie de READY : ignorés\n");
    bench();
    bsk_journal_init(phy_sim_now);
    l135_start_powered(&L, phy_sim_now(), true, false);
    d2(true);
    for (i10 = -1; i10 < 0 && phy_sim_now() < T0 + 3000 * MS; i10 = find(0, A_SEND, 2, 0x10)) run_for(1 * MS);
    run_for(100 * MS);
    bsk_journal_set(true, false);
    jdrain();
    CHECK(i10 >= 0 && status().session_state == SESSION_IDENTIFYING, "le 0x10 de l'init en vol : IDENTIFYING");
    L.in.button = true;
    run_for(300 * MS);
    L.in.button = false;
    run_for(50 * MS);
    CHECK(status().session_state == SESSION_IDENTIFYING, "relâché toujours en IDENTIFYING");
    jdrain();
    CHECK(jfind(0, "* btn ", NULL) >= 0 && jfind(0, "* btn ", " press=short result=ignore why=identifying") == jfind(0, "* btn ", NULL) &&
              jfind((size_t)jfind(0, "* btn ", NULL) + 1, "* btn ", NULL) < 0,
          "une ligne : « * btn <t> press=short result=ignore why=identifying »");
    bsk_journal_set(false, false);
    run_for(4000 * MS);
    CHECK(status().session_state == SESSION_READY && !status().mark_valid && g_store_sim.writes == 0 && acks_of(BSK_SEQ_BOARD) == 0,
          "READY : ni marque posée, ni goto du bouton");

    bsk_journal_set(true, false);
    jdrain();
    tp = phy_sim_now();
    L.in.button = true;
    run_for(200 * MS);
    (void)cmd(CMD_ATTACH, 0);
    while (status().session_state != SESSION_READY && phy_sim_now() < tp + 3000 * MS) run_for(1 * MS);
    ok_ready = status().session_state == SESSION_READY && entered(SESSION_POWERING, tp) != 0;
    run_to(tp + 3000 * MS);
    L.in.button = false;
    run_for(50 * MS);
    jdrain();
    bsk_journal_set(false, false);
    CHECK(ok_ready && status().session_state == SESSION_READY, "une nouvelle session, READY avant le relâchement");
    CHECK(jfind(0, "* btn ", NULL) >= 0 && jfind(0, "* btn ", " result=ignore why=identifying") == jfind(0, "* btn ", NULL) &&
              jfind((size_t)jfind(0, "* btn ", NULL) + 1, "* btn ", NULL) < 0,
          "une ligne : « * btn <t> press=… result=ignore why=identifying »");
    CHECK(!status().mark_valid && g_store_sim.writes == 0 && acks_of(BSK_SEQ_BOARD) == 0, "ni marque posée, ni goto du bouton");
}

/* RESTORING contre le faux 135 : la marque 20000 rangée, sa
 * lecture jamais rendue (le magasin la retient, g_store_sim.hold). Le 135 froid ne publie son flux qu'après son 0x0A (lens135.c :
 * le flux posé par le 0x0A) : la fin du démarrage est son premier 0x05 (S_FIRST05, S_PROBE05) ; READY 500 ms après lui
 * (MARK_WAIT_MS), sans RESTORING ni 0x1D, `* restore <t> skip=timeout` ; la lecture rendue ensuite ne relance rien. */
static void t_restore_expired(void)
{
    uint16_t t[4] = {0};
    long i0a, r0a, f05;
    printf("RESTORING, faux 135 : la lecture de la marque jamais rendue, READY à 500 ms sans retour\n");
    bench();
    store_sim_poke(KEY135, 20000);
    g_store_sim.hold = true;
    bsk_journal_init(phy_sim_now);
    mount();
    while (find(0, A_SEND, 2, 0x0A) < 0 && phy_sim_now() < T0 + 3000 * MS) run_for(1 * MS);
    bsk_journal_set(true, false);
    jdrain();
    run_for(2000 * MS);
    jdrain();
    bsk_journal_set(false, false);
    i0a = find(0, A_SEND, 2, 0x0A);
    r0a = i0a >= 0 ? find((size_t)i0a, A_RX, 0, 0x0A) : -1;
    f05 = r0a > 0 ? find((size_t)r0a, A_RX, 0, 0x05) : -1;
    CHECK(f05 > 0 && find(0, A_RX, 0, 0x05) == f05 && entered(SESSION_READY, 0) == LOG[f05].t + 500 * MS &&
              entered(SESSION_RESTORING, 0) == 0,
          "READY 500 ms après le premier 0x05, qui suit la réponse au 0x0A, sans RESTORING");
    CHECK(jfind(0, "* restore ", " skip=timeout") >= 0 && jfind(0, "* restore ", NULL) == jfind(0, "* restore ", " skip=timeout"),
          "« * restore <t> skip=timeout »");
    store_sim_flush();
    g_store_sim.hold = false;
    run_for(3000 * MS);
    CHECK(targets_1d(0, t, 4) == 0 && status().session_state == SESSION_READY && status().focus_position == 16384 &&
              status().motion_state == MOTION_IDLE && status().mark_valid && status().mark_position == 20000,
          "la lecture rendue ensuite : la marque publiée, aucun 0x1D, toujours en 16384");
    CHECK(g_store_sim.writes == 0, "rien d'écrit dans le magasin (la marque ne s'écrit que sur un geste)");
}

/* RESTORING contre le faux 135 bloqué après son init (frozen_position : le moteur commandé, la position ne change plus) : la marque 20000,
 * croissante, du bon côté depuis 16384 : un goto à 20000 ; ni démarrage ni accusé : le même 0x1D à 1 s et à 2 s, STALLED à
 * 3 s ; READY au même instant, position valide, 16384 ; `* restore <t> end=stall`. */
static void t_restore_blocked(void)
{
    uint16_t t[4] = {0};
    uint64_t tr, ts;
    size_t n;
    printf("RESTORING, faux 135 bloqué : le 0x1D trois fois, STALLED à 3 s, READY, position valide\n");
    bench();
    store_sim_poke(KEY135, 20000 | INC);
    bsk_journal_init(phy_sim_now);
    mount();
    while (find(0, A_SEND, 2, 0x0A) < 0 && phy_sim_now() < T0 + 3000 * MS) run_for(1 * MS);
    L.f.frozen_position = true;                     /* après son homing, en 16384 : bloqué là */
    bsk_journal_set(true, false);
    jdrain();
    run_for(6000 * MS);
    jdrain();
    bsk_journal_set(false, false);
    tr = entered(SESSION_RESTORING, 0);
    ts = mv_entered(MOTION_STALLED, 0);
    n = targets_1d(0, t, 4);
    CHECK(tr != 0 && ts == tr + 3000 * MS && entered(SESSION_READY, 0) == ts,
          "STALLED 3 s après l'entrée en RESTORING, READY au même instant");
    CHECK(n == 3 && t[0] == 20000 && t[1] == 20000 && t[2] == 20000, "le 0x1D trois fois, 20000 (%zu)", n);
    CHECK(status().session_state == SESSION_READY && status().position_valid && status().focus_position == 16384 &&
              l135_position(&L) == 16384,
          "READY, position valide, 16384 (%ld)", (long)status().focus_position);
    CHECK(jfind(0, "* restore ", " end=stall") >= 0, "« * restore <t> end=stall »");
    CHECK(n_acks == 0 && g_store_sim.writes == 0, "aucun accusé, rien d'écrit dans le magasin (la marque ne s'écrit que sur un geste)");
}

/* Un goto de l'hôte vers le faux 135 bloqué (frozen_position) : ni démarrage ni accusé en 1 s, le 0x1D renvoyé à 1 s et à 2 s ;
 * le 135 accuse `1D 00` le 0x1D évincé par le renvoi (samyang.md § 4.4) : ce n'est pas une
 * arrivée. STALLED à 3 s, ACK_FAILED E_STALL ; jamais ACK_COMPLETED pour un objectif qui n'a pas bougé. */
static void t_goto_blocked(void)
{
    uint32_t g;
    uint64_t tg, tf = 0;
    size_t from;
    printf("goto vers le faux 135 bloqué : l'accusé du 0x1D évincé n'est pas une arrivée, STALLED à 3 s\n");
    ready();
    L.f.frozen_position = true;
    from = n_log;
    tg = phy_sim_now();
    g = cmd(CMD_FOCUS_GOTO, 20000);
    run_for(5000 * MS);
    CHECK(count_1d(from, NULL) == 3 && ack_rx(from, 0x1D, 0x00) >= 0, "le 0x1D trois fois ; un 1D 00 reçu (l'évincé)");
    CHECK(accepted_then(g, ACK_FAILED, E_STALL, &tf) && tf == tg + 3000 * MS && status().motion_state == MOTION_STALLED,
          "ACK_FAILED, E_STALL, à 3 s ; STALLED");
    CHECK(mv_entered(MOTION_ARRIVED, tg) == 0 && mv_entered(MOTION_SETTLING, tg) == 0, "jamais SETTLING ni ARRIVED");
    CHECK(status().focus_position == 16384 && l135_position(&L) == 16384 && status().session_state == SESSION_READY,
          "READY, toujours en 16384");
    L.f.frozen_position = false;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage : test_session135 <répertoire des traces de l'objectif>\n");
        return 2;
    }
    const char *traces = argv[1];
    signal(SIGALRM, too_long);
    alarm(GARDE_S);
    t_cold_start(traces);
    t_powered();
    t_silent();
    t_loss();
    t_d2_during_10();
    t_lines_boot();
    t_lines_off();
    t_lines_recovering();
    t_lines_recovering_cut();
    t_goto();
    t_pair_phase();
    t_goto_twice();
    t_goto_limit();
    t_move();
    t_stop_goto();
    t_stop_ready();
    t_stop_powering();
    t_busy();
    t_button();
    t_detach_attach();
    t_attach_restart();
    t_attach_no_d2();
    t_fault();
    t_d2_goto();
    t_xdetect();
    t_xdetect_attach();
    t_xdetect_recovery();
    t_xdetect_full();
    t_journal();
    t_journal_rejected();
    t_ring_window();
    t_ring_custom();
    t_ring_custom_resume();
    t_ring_af();
    t_log_all_minute();
    t_sony_late_55();
    t_restore_paths();
    t_restore_none();
    t_restore_stop();
    t_restore_loss();
    t_restore_recovery();
    t_restore_journal();
    t_xdetect_journal();
    t_xdetect_bounce();
    t_resend_init();
    t_resend_init_3();
    t_resend_never();
    t_resend_goto();
    t_resend_goto_none();
    t_resend_stop();
    t_power_loss_restore();
    t_recovering_cmds();
    t_button_outside();
    t_restore_expired();
    t_restore_blocked();
    t_goto_blocked();
    CHECK(wire_faults == 0, "règle des lignes tenue dans tous les tests : %u faute(s)", wire_faults);
    printf("session contre le faux 135 : %d vérifications, %d échec(s)\n", checks, fails);
    return fails ? 1 : 0;
}
