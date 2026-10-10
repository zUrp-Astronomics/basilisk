/* SOURCE: 7_Docs/E-Mount/tamron.md, le faux Tamron F051 (sim/lens_std.c)
 * AUTHOR: engineer
 * DATE: 2026-09-29
 * STATUS: actif — joué par 4_Firmware/run.sh (job `firmware` de la CI)
 * Le firmware contre le faux objectif standard : la carte testée contre autre chose qu'un 135 (décision de l'humain).
 * Argument : la liste des constats (sim/test/constats_std.txt, sous la racine que run.sh ou mutants.sh donne).
 *
 * Tout est le vrai code : SESSION, TRANSACTION, bench_core, le journal et la couche HOTE, au-dessus de la PHY simulée
 * (sim/phy_sim.c) et du faux F051 (sim/lens_std.c, inchangé), branché par l'interface commune (sim/lens_sim.h,
 * adaptateur sim/lens_sim_std.c). Le programme est lié avec --wrap (sim/test/programmes.sh) : bsk_phy_send et
 * bsk_phy_poll passent par les __wrap_ ci-dessous, qui notent ce que la carte émet et ce qu'elle reçoit.
 *
 * Chaque attendu est écrit à la main, d'après la référence : « § n » = section de 7_Docs/E-Mount/tamron.md,
 * « protocol.md § n » = section de 7_Docs/E-Mount/protocol.md. Ce que le modèle fixe
 * sans que la référence le dise est un paramètre [NÉ] de lens_std.h, nommé là où un attendu en dépend.
 *
 * Les écarts ne se corrigent pas ici : un scénario dont le firmware (ou le modèle) ne tient pas l'attendu reste écrit
 * tel quel, et il est inscrit dans la liste des constats, sim/test/constats_std.txt, avec sa raison et sa citation. Le
 * programme lit la liste, l'affiche, puis :
 *   - un scénario en échec hors de la liste est un échec (sortie 1) ;
 *   - un scénario de la liste qui passe est un échec : le constat est réglé, il doit sortir de la liste ;
 *   - une entrée qui ne nomme aucun scénario, ou sans raison ni citation, est un échec.
 * Dans un scénario de la liste, un attendu manqué s'écrit « écart », pas « ECHEC » : il est connu. */
#define _POSIX_C_SOURCE 200809L   /* alarm, la garde de durée */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bsk_host.h"
#include "bsk_journal.h"
#include "bsk_session.h"
#include "lens_sim_std.h"
#include "phy_common.h"
#include "phy_sim.h"
#include "store_sim.h"
#include "jdecode.h"

#define MS 1000u
#define T0 (1000u * MS)

/* ─────────────────────────── vérifications, scénarios, constats ─────────────────────────── */

static int checks, fails;       /* hors des scénarios de la liste */
static int sc_fails;            /* le scénario en cours */
static bool sc_listed;          /* le scénario en cours est un constat */
static bool quiet;              /* un démarrage rejoué pour atteindre READY : ses attendus sont ceux de demarrage_froid */

static void too_long(int sig)
{
    static const char m[] = "  ECHEC : le programme ne termine pas en 20 s (boucle sans fin)\n";
    (void)sig;
    (void)!write(1, m, sizeof m - 1);
    _exit(1);
}

#define CHECK(c, ...)                                                                   \
    do {                                                                                \
        checks++;                                                                       \
        if (!(c)) {                                                                     \
            sc_fails++;                                                                 \
            if (!quiet) printf("  %s %s:%d : ", sc_listed ? "écart" : "ECHEC", __FILE__, __LINE__); \
            if (!quiet) printf(__VA_ARGS__);                                            \
            if (!quiet) printf("\n");                                                   \
        }                                                                               \
    } while (0)

/* La liste des constats (sim/test/constats_std.txt). Bloc, séparé par une ligne vide :
 *   @ <scénario>
 *   = <raison : ce que le firmware (ou le modèle) fait, contre l'attendu>   (une ou plusieurs lignes)
 *   § <citation>                                                          (une ou plusieurs lignes)
 * Les lignes qui commencent par # hors d'un bloc sont des commentaires. */
#define CONSTAT_CAP 32
static struct {
    char name[64];
    char why[2048];
    char cite[1024];
    bool used;
} K[CONSTAT_CAP];
static size_t n_k;
static int list_faults;

static void cat(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(dst);
    snprintf(dst + n, cap - n, "%s%s", n ? " " : "", src);
}

static void load_constats(const char *path)
{
    char line[1024];
    FILE *fp = fopen(path, "r");
    bool in = false;
    if (!fp) {
        printf("  ECHEC : liste des constats illisible : %s\n", path);
        list_faults++;
        return;
    }
    while (fgets(line, sizeof line, fp)) {
        line[strcspn(line, "\n")] = 0;
        if (!line[0]) { in = false; continue; }
        if (line[0] == '#' && !in) continue;
        if (!strncmp(line, "@ ", 2)) {
            if (n_k == CONSTAT_CAP) { printf("  ECHEC : plus de %d constats\n", CONSTAT_CAP); list_faults++; break; }
            memset(&K[n_k], 0, sizeof K[n_k]);
            if (strlen(line + 2) >= sizeof K[n_k].name) {   /* aucun scénario n'a un nom si long */
                printf("  ECHEC : nom de scénario trop long dans la liste des constats : %s\n", line + 2);
                list_faults++;
            }
            memcpy(K[n_k].name, line + 2, strnlen(line + 2, sizeof K[n_k].name - 1));
            n_k++;
            in = true;
        } else if (in && !strncmp(line, "= ", 2)) {
            cat(K[n_k - 1].why, sizeof K[n_k - 1].why, line + 2);
        } else if (in && !strncmp(line, "§ ", strlen("§ "))) {
            cat(K[n_k - 1].cite, sizeof K[n_k - 1].cite, line + strlen("§ "));
        } else {
            printf("  ECHEC : ligne inattendue dans la liste des constats : %s\n", line);
            list_faults++;
        }
    }
    fclose(fp);
    for (size_t i = 0; i < n_k; i++)
        if (!K[i].why[0] || !K[i].cite[0]) {
            printf("  ECHEC : le constat « %s » n'a pas de raison ou pas de citation\n", K[i].name);
            list_faults++;
        }
}

static long constat(const char *name)
{
    for (size_t i = 0; i < n_k; i++)
        if (!strcmp(K[i].name, name)) return (long)i;
    return -1;
}

/* ─────────────────────────── ce que la carte fait sur le fil ─────────────────────────── */

typedef enum { A_SEND, A_RX, A_LENS_CS, A_ERR } act_kind_t;
typedef struct {
    act_kind_t  kind;
    uint64_t    t;
    bsk_frame_t frame;   /* A_SEND : émise ; A_RX : reçue */
    bool        level;   /* A_LENS_CS */
} act_t;

#define LOG_CAP 60000u
static act_t LOG[LOG_CAP];
static size_t n_log;

static void note(act_t a)
{
    if (n_log < LOG_CAP) LOG[n_log] = a;
    n_log++;
}

bsk_err_t __real_bsk_phy_send(const bsk_frame_t *f);
bsk_err_t __wrap_bsk_phy_send(const bsk_frame_t *f);
bool __real_bsk_phy_poll(bsk_phy_event_t *ev);
bool __wrap_bsk_phy_poll(bsk_phy_event_t *ev);

/* Les fronts de VD remontés par la PHY, pour la phase de la paire sous LOG ALL (log_all_minute). */
static uint64_t VD_EDGES[8192];
static size_t n_vd;

bsk_err_t __wrap_bsk_phy_send(const bsk_frame_t *f)
{
    note((act_t){.kind = A_SEND, .t = phy_sim_now(), .frame = *f});
    return __real_bsk_phy_send(f);
}

bool __wrap_bsk_phy_poll(bsk_phy_event_t *ev)
{
    bool got = __real_bsk_phy_poll(ev);
    if (got && ev->kind == BSK_PHY_FRAME) note((act_t){.kind = A_RX, .t = ev->t_us, .frame = ev->u.frame});
    if (got && ev->kind == BSK_PHY_LENS_CS) note((act_t){.kind = A_LENS_CS, .t = ev->t_us, .level = ev->u.level});
    if (got && ev->kind == BSK_PHY_ERROR) note((act_t){.kind = A_ERR, .t = ev->t_us});
    if (got && ev->kind == BSK_PHY_VD && n_vd < sizeof VD_EDGES / sizeof VD_EDGES[0]) VD_EDGES[n_vd++] = ev->t_us;
    return got;
}

/* ─────────────────────────── une trame perdue ou fausse ─────────────────────────── */

/* Les règles du crochet de la PHY simulée (phy_sim_fault) : les `n` prochaines trames du sens
 * `to_lens` qui portent un message de type `type` (le premier, celui qui suit le 0x04, ou un accusé qui suit le 0x06)
 * ont le sort `fate` ; `hit` en compte, `t_hit` note l'instant de la dernière. Oubliées à chaque banc. */
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

static rule_t *lose(bool to_lens, uint8_t type, unsigned n, phy_sim_fate_t fate)
{
    RULES[n_rules] = (rule_t){.to_lens = to_lens, .type = type, .n = n, .fate = fate};
    phy_sim_fault(fault);
    return &RULES[n_rules++];
}

/* ─────────────────────────── le banc ─────────────────────────── */

static lstd_t L;
static lstd_params_t LP;
static bsk_session_params_t P;
static uint64_t d2_due;
static bool host_mode;          /* la couche HOTE lit les accusés (bsk_host_observe) : le banc ne les lit pas */

typedef struct { uint64_t t; uint8_t state; } chg_t;
static chg_t chg[512], mchg[512];
static size_t n_chg, n_mchg;
static uint8_t last_state, last_mv;

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

static void observe(void)
{
    bsk_ack_t a;
    bsk_status_t st = status();
    uint64_t t = phy_sim_now();
    if (host_mode) bsk_host_observe();               /* app_main : après chaque pas */
    else
        while (bsk_session_ack(&a))
            if (n_acks < sizeof ACKS / sizeof ACKS[0]) ACKS[n_acks++] = (ack_rec_t){a, t};
    if (st.session_state != last_state && n_chg < sizeof chg / sizeof chg[0]) chg[n_chg++] = (chg_t){t, st.session_state};
    last_state = st.session_state;
    if (st.motion_state != last_mv && n_mchg < sizeof mchg / sizeof mchg[0]) mchg[n_mchg++] = (chg_t){t, st.motion_state};
    last_mv = st.motion_state;
}

/* L'alimentation du F051 suivie par le rail logique (rails_power), comme sur la carte à rails commutés ; la PHY
 * simulée ne la commande pas (phy_sim.h). Chaque mise hors et sous tension notée (t_pw_off, t_pw_on, la dernière) ; à la
 * mise hors tension, `unhang` lève le silence du F051 : l'hypothèse du scénario coupure, un objectif muet qu'un cycle
 * d'alimentation remet en marche. */
static bool rails_power, unhang;
static uint64_t t_pw_off, t_pw_on;

static void follow_rail(void)
{
    bool on = phy_sim_wires()->rail[BSK_RAIL_LOGIC];
    if (!rails_power || on == L.powered) return;
    lens_sim_power(lens_sim_std(&L), phy_sim_now(), on);
    if (on) t_pw_on = phy_sim_now();
    else t_pw_off = phy_sim_now();
    if (!on && unhang) L.f.silent = false;
}

static void run_to(uint64_t t_end)
{
    for (;;) {
        uint64_t t = phy_sim_next(), ts = bsk_session_next();
        if (ts < t) t = ts;
        if (d2_due > phy_sim_now() && d2_due < t) t = d2_due;   /* l'anti-rebond de D2 */
        if (t > t_end) t = t_end;
        phy_sim_run(t);
        bsk_session_step(t);
        follow_rail();
        observe();
        if (t >= t_end) break;
    }
}

static void run_for(uint64_t us) { run_to(phy_sim_now() + us); }

static void d2(bool present)
{
    phy_sim_d2(present);
    d2_due = phy_sim_now() + D2_DEBOUNCE_US;
}

/* Le F051 hors tension, paramètres par défaut (lstd_params_default) ; D2 absent, horloge à 0 ; la session en OFF. */
static void bench(void)
{
    lstd_params_default(&LP);
    lstd_init(&L, &LP);
    phy_sim_init(lens_sim_std(&L), 0);
    bsk_session_params_default(&P);
    store_sim_reset();                               /* aucune marque rangée */
    bsk_session_init(&P);
    bsk_journal_init(phy_sim_now);
    bsk_journal_set(false, false);
    bsk_host_init("1.0", "poweron");
    host_mode = false;
    n_rules = 0;                                     /* aucune trame perdue */
    rails_power = unhang = false;                    /* l'alimentation commandée par le scénario */
    t_pw_off = t_pw_on = 0;
    n_log = n_chg = n_mchg = n_acks = 0;
    n_vd = 0;
    d2_due = 0;
    last_state = SESSION_OFF;
    last_mv = MOTION_IDLE;
    run_to(T0);
}

/* Les paramètres du modèle après bench(), avant la mise sous tension : lstd_init de nouveau. */
static void params(void) { lstd_init(&L, &LP); }

/* L'objectif mis sous tension (§ 2.1 : phase init, état libre) et sa monture fermée, à l'instant courant. */
static void mount_cold(void)
{
    lens_sim_power(lens_sim_std(&L), phy_sim_now(), true);
    d2(true);
}

static uint32_t cmd(bsk_cmd_op_t op, int32_t arg)
{
    bsk_cmd_t c = {.seq = ++seq_n, .op = op, .arg = arg};
    bsk_session_command(&c);
    observe();
    return c.seq;
}

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

/* `seq` a reçu ACK_ACCEPTED puis `r` avec `why`, rien d'autre. */
static bool accepted_then(uint32_t seq, bsk_ack_result_t r, bsk_err_t why)
{
    const ack_rec_t *a = ack_of(seq, 0), *b = ack_of(seq, 1);
    return acks_of(seq) == 2 && a->a.result == ACK_ACCEPTED && b->a.result == r && b->a.reason == why;
}

static bool ever(uint8_t state)
{
    for (size_t i = 0; i < n_chg; i++)
        if (chg[i].state == state) return true;
    return false;
}

static bool motion_seen(size_t from, uint8_t st)
{
    for (size_t i = from; i < n_mchg; i++)
        if (mchg[i].state == st) return true;
    return false;
}

/* ── le journal du fil ── */

static const act_t *at(size_t i) { return i < n_log && i < LOG_CAP ? &LOG[i] : NULL; }

/* Première entrée à partir de `from` : trame émise (A_SEND) ou reçue (A_RX) dont le premier message est `type`. */
static long find(size_t from, act_kind_t kind, uint8_t type)
{
    for (size_t i = from; at(i); i++)
        if (LOG[i].kind == kind && LOG[i].frame.msg[0] == type) return (long)i;
    return -1;
}

/* Une trame reçue après l'entrée `from` dont le message est exactement `m`. */
static long find_rx_exact(size_t from, const uint8_t *m, size_t n)
{
    for (size_t i = from; at(i); i++)
        if (LOG[i].kind == A_RX && LOG[i].frame.len == n && !memcmp(LOG[i].frame.msg, m, n)) return (long)i;
    return -1;
}

static size_t count_rx(size_t from, uint8_t type)
{
    size_t n = 0;
    for (size_t i = from; at(i); i++) n += LOG[i].kind == A_RX && LOG[i].frame.msg[0] == type;
    return n;
}

/* Un 0x06 de classe 1 reçu après `from` que suit, dans sa trame, le sous-message `a b` (§ 2.5, § 4.4 : les accusés
 * sont écrits au pointeur d'écriture du 0x06, qui part de la fin des blocs accordés : 40 octets, type compris, pour la
 * mise en page que donne le 0x0A de la carte, blocs 1 à 6 de tailles 2, 11, 9, 4, 6, 7, protocol.md § 7.6). */
#define LEN06 40u
static long find_ack(size_t from, uint8_t a, uint8_t b)
{
    for (size_t i = from; at(i); i++) {
        const bsk_frame_t *f = &LOG[i].frame;
        if (LOG[i].kind != A_RX || f->cls != 1 || f->msg[0] != 0x06) continue;
        for (size_t k = LEN06; k + 1 < f->len; k += 2)
            if (f->msg[k] == a && f->msg[k + 1] == b) return (long)i;
    }
    return -1;
}

/* Les 0x03 émis depuis l'entrée `from` portent tous la consigne d'ouverture `code` aux offsets 3-6, petit-boutiste,
 * deux fois ; au moins un 0x03. */
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

static bool lens_clean(void) { return !lstd_out_of_model(&L) && !lstd_blocked(&L); }

/* ─────────────────────────── ce que le F051 répond (§ 1.2) ─────────────────────────── */

/* 0x01 : 33 octets, la carte des types de § 1.2 (règle de protocol.md § 7.1) dans ces neuf premiers, puis des zéros */
static const uint8_t R01_HEAD[] = {0x01, 0xFF, 0x9F, 0x78, 0x5D, 0x82, 0x60, 0x18, 0x5E};
/* 0x07 : 35 octets, « 07 01 03 70 00 00 01 03 00 A0 34 C1 … » (§ 1.2 ; offsets 1-2, protocol.md § 7.7 ; les octets
 * non publiés à zéro) */
static const uint8_t R07_HEAD[] = {0x07, 0x01, 0x03, 0x70, 0x00, 0x00, 0x01, 0x03, 0x00, 0xA0, 0x34, 0xC1};
#define F051_NAME "E 24mm F2.8 F051"   /* 0x3F, 66 octets, le nom aux offsets 1-64 (§ 1.1, § 1.2) */
#define F051_TYPE2 0xC134u             /* R07_HEAD, offsets 9-10 (octets 10-11) : ce que la carte lit */
static const uint8_t R0B[] = {0x0B, 0x60, 0x00};   /* 0B <offset 0 de la requête> 00 (§ 1.2) */
static const uint8_t R0D[] = {0x0D, 0x00};         /* § 1.2 */
static const uint8_t R10_OK[] = {0x10, 0x00};      /* § 3 */
static const uint8_t R10_FAIL[] = {0x10, 0x01};    /* un indicateur d'erreur de la mise au point ou de l'iris (§ 3) */
/* 0x0A : 0A + (requête ET le masque de capacités FF FF 03 00 00 00 00 00 FF 00…, § 1.2) ; la requête est celle de
 * la carte, 0A FF 7F 00 00 00 00 00 00 3F 00… */
static const uint8_t R0A[] = {0x0A, 0xFF, 0x7F, 0, 0, 0, 0, 0, 0, 0x3F, 0, 0, 0, 0, 0, 0, 0};
/* Le refus (§ 2.3) : 02 <classe reçue> <type du premier sous-message, FF s'il est le fautif> 04 00 00 00 00, en classe 3 */
static const uint8_t REFUS_P2[] = {0x02, 0x02, 0xFF, 0x04, 0, 0, 0, 0};   /* un message d'init seul, hors phase init */

/* ─────────────────────────── les scénarios ─────────────────────────── */

/* Le démarrage à froid jusqu'à READY, joué 10 s (les échéances de la carte au plus : init_home_ms 8 s). `tail` : la
 * retombée de LENS_CS après le dernier octet (lens_cs_tail_us [NÉ]). */
static void cold(uint64_t tail)
{
    long i01, i10, r10, i0a, r0a, i;
    bsk_status_t st;
    bench();
    LP.lens_cs_tail_us = tail;
    params();
    mount_cold();
    run_for(10000 * MS);
    st = status();

    /* la poignée de main du F051 (§ 2.8 ; lens_std.c, « Couche physique ») : LENS_CS levée quand
     * BODY_CS est haute, rabaissée quand elle retombe ; le 0x01 après la retombée (il prouve la liaison) */
    i01 = find(0, A_SEND, 0x01);
    CHECK(i01 > 0 && LOG[0].kind == A_LENS_CS && LOG[0].level && LOG[1].kind == A_LENS_CS && !LOG[1].level &&
              LOG[1].t <= LOG[i01].t,
          "poignée de main : LENS_CS haute puis basse avant le 0x01");

    /* l'init, chacune de ses requêtes servie en phase init, état libre (§ 2.1, § 2.2), par sa réponse (§ 1.2) */
    i = find(i01 > 0 ? (size_t)i01 : 0, A_RX, 0x01);
    CHECK(i > 0 && LOG[i].frame.len == 33 && !memcmp(LOG[i].frame.msg, R01_HEAD, sizeof R01_HEAD) && LOG[i].frame.cls == 2,
          "0x01 : 33 octets, 01 FF 9F 78 5D 82 60 18 5E…, classe 2");
    i = find(0, A_RX, 0x07);
    CHECK(i > 0 && LOG[i].frame.len == 35 && !memcmp(LOG[i].frame.msg, R07_HEAD, sizeof R07_HEAD), "0x07 : 35 octets, 07 01 03 70…");
    i = find(0, A_RX, 0x3F);
    CHECK(i > 0 && LOG[i].frame.len == 66 && !memcmp(LOG[i].frame.msg + 2, F051_NAME, strlen(F051_NAME)), "0x3F : 66 octets, le nom");
    i = find(0, A_RX, 0x08);
    CHECK(i > 0 && LOG[i].frame.len == 202, "0x08 : 202 octets");
    i = find(0, A_SEND, 0x08);
    CHECK(i > 0 && LOG[i].frame.len >= 2 && LOG[i].frame.msg[1] == 0x00,
          "0x08 émis : drapeaux 00, le F051 n'est pas un Samyang (06 pour un Samyang seul)");
    CHECK(find_rx_exact(0, R0B, sizeof R0B) > 0, "0x0B : 0B 60 00");
    i = find(0, A_RX, 0x09);
    CHECK(i > 0 && LOG[i].frame.len == 12, "0x09 : 12 octets");
    CHECK(find_rx_exact(0, R0D, sizeof R0D) > 0, "0x0D : 0D 00");

    /* le homing : réponse différée 10 00 (§ 2.6, § 3) ; le 0x0A seulement après elle (§ 2.6 : une réponse en attente
     * fait refuser le 0x0A) ; sa réponse, le masque accordé (§ 1.2) */
    i10 = find(0, A_SEND, 0x10);
    r10 = find_rx_exact(0, R10_OK, sizeof R10_OK);
    i0a = find(0, A_SEND, 0x0A);
    r0a = find_rx_exact(0, R0A, sizeof R0A);
    CHECK(i10 > 0 && r10 > i10, "0x10 de l'init, puis 10 00");
    CHECK(i0a > r10 && r10 > 0, "le 0x0A après le 10 00, jamais avant");
    CHECK(r0a > i0a && i0a > 0 && LOG[r0a].frame.cls == 2, "0x0A : 0A FF 7F 00 00 00 00 00 00 3F 00…, classe 2");

    /* le flux : 0x05 et 0x06 en classe 1 (§ 2.5) */
    i = find(r0a > 0 ? (size_t)r0a : 0, A_RX, 0x05);
    CHECK(i > 0 && LOG[i].frame.cls == 1, "le flux après le 0x0A : un 0x05 de classe 1");
    i = find(r0a > 0 ? (size_t)r0a : 0, A_RX, 0x06);
    CHECK(i > 0 && LOG[i].frame.cls == 1 && LOG[i].frame.len >= LEN06, "le flux après le 0x0A : un 0x06 de classe 1, 40 octets au moins");

    /* aucun refus : chaque message est envoyé dans la phase qui l'accepte (§ 2.2) */
    CHECK(count_rx(0, 0x02) == 0, "aucun 0x02 reçu (%zu)", count_rx(0, 0x02));
    CHECK(find(0, A_SEND, 0x40) < 0, "aucune trame 0x40 : le F051 n'est pas un Samyang reconnu");
    CHECK(ap03_all(0, 0x1312), "chaque 0x03 porte 12 13 12 13 aux offsets 3-6 : f/1,8 de chaque session (std.c:123, B2 11), "
          "ramenée au minimum de la plage du F051, sa réponse au 0x08, octets 1-2, 12 13 (R08)");

    /* la carte sert l'objectif : READY, la position publiée par le flux (0x06 bloc 2, octets 0-1 : la position
     * après le homing, home_pos [NÉ] 16384), son nom et son LensType2 */
    CHECK(st.session_state == SESSION_READY && st.position_valid, "READY, position valide (état %s)",
          bsk_state_name(st.session_state));
    CHECK(st.focus_position == 16384 && lstd_position(&L) == 16384, "position 16384 (%ld)", (long)st.focus_position);
    CHECK(!strcmp(st.lens_name, F051_NAME), "nom « %s »", st.lens_name);
    CHECK(st.lens_id_product == F051_TYPE2, "LensType2 0x%04lX", (unsigned long)st.lens_id_product);
    CHECK(st.last_error == E_OK && !ever(SESSION_RECOVERING) && !ever(SESSION_FAULT), "ni reprise ni FAULT");
    CHECK(L.phase == LSTD_FLOW, "le F051 en flux (%d)", L.phase);
    CHECK(lens_clean(), "le F051 reste dans son modèle (%s)", L.oom_why ? L.oom_why : "bloqué");
}

static void demarrage_froid(void) { cold(0); }
static void demarrage_froid_cs300(void) { cold(300); }

/* La retombée de LENS_CS jouée à 0 et à 300 µs après le dernier octet (lens_cs_tail_us [NÉ], lens_std.h) : même
 * résultat — les mêmes messages émis, dans le même ordre ; les mêmes messages reçus, dans le même ordre ; les mêmes
 * états publiés. Les instants, eux, diffèrent : chaque réponse part 300 µs plus tard. */
#define CMP_CAP 4096u
typedef struct { size_t n; bsk_frame_t f[CMP_CAP]; } seq_t;
static seq_t SENT0, RECV0, SENT, RECV;
static chg_t CHG0[512];
static size_t n_chg0;

static void frames(act_kind_t kind, seq_t *q)
{
    q->n = 0;
    for (size_t i = 0; at(i); i++)
        if (LOG[i].kind == kind && q->n < CMP_CAP) q->f[q->n++] = LOG[i].frame;
}

/* Le premier indice où les deux suites diffèrent (message et longueur, ni classe ni séquence), n si aucun. */
static size_t differ(const seq_t *a, const seq_t *b)
{
    size_t i;
    for (i = 0; i < a->n && i < b->n; i++)
        if (a->f[i].len != b->f[i].len || memcmp(a->f[i].msg, b->f[i].msg, a->f[i].len)) return i;
    return a->n == b->n ? a->n : i;
}

static void retombee_lens_cs(void)
{
    size_t j, ds, dr;
    int keep = sc_fails;
    quiet = true;
    cold(0);
    frames(A_SEND, &SENT0);
    frames(A_RX, &RECV0);
    memcpy(CHG0, chg, sizeof chg);
    n_chg0 = n_chg;
    cold(300);
    quiet = false;
    sc_fails = keep;                                 /* les attendus du démarrage sont ceux de demarrage_froid */
    frames(A_SEND, &SENT);
    frames(A_RX, &RECV);
    ds = differ(&SENT0, &SENT);
    dr = differ(&RECV0, &RECV);
    CHECK(ds == SENT0.n && ds == SENT.n, "trames émises : %zu à 0 µs, %zu à 300 µs, la %zue diffère (0x%02X / 0x%02X)", SENT0.n,
          SENT.n, ds + 1, ds < SENT0.n ? SENT0.f[ds].msg[0] : 0, ds < SENT.n ? SENT.f[ds].msg[0] : 0);
    CHECK(dr == RECV0.n && dr == RECV.n, "trames reçues : %zu à 0 µs, %zu à 300 µs, la %zue diffère (0x%02X / 0x%02X)", RECV0.n,
          RECV.n, dr + 1, dr < RECV0.n ? RECV0.f[dr].msg[0] : 0, dr < RECV.n ? RECV.f[dr].msg[0] : 0);
    for (j = 0; j < n_chg && j < n_chg0 && chg[j].state == CHG0[j].state; j++) {}
    CHECK(n_chg == n_chg0 && j == n_chg, "les états publiés : %zu à 0 µs, %zu à 300 µs", n_chg0, n_chg);
}

/* Un démarrage à froid, puis READY attendu (les attendus du démarrage sont ceux de demarrage_froid). */
static bool ready_cold(void)
{
    int keep = sc_fails;
    quiet = true;
    cold(0);
    quiet = false;
    sc_fails = keep;
    CHECK(status().session_state == SESSION_READY, "précondition : READY après le démarrage à froid (état %s)",
          bsk_state_name(status().session_state));
    return status().session_state == SESSION_READY;
}

/* L'objectif resté alimenté (§ 8) : en flux, avec la mise en page du 0x0A de la session d'avant (la
 * réponse R0A, octets 1 à 16), les modules en service et référencés, la position 16384 (initial_position [NÉ]). */
static void reste_alimente(void)
{
    lens_sim_powered_t st = {.flow = true, .grant = R0A + 1};
    long i01, r;
    bsk_status_t s;
    bench();
    CHECK(lens_sim_start_powered(lens_sim_std(&L), phy_sim_now(), &st), "l'adaptateur accepte l'état resté alimenté");
    d2(true);
    run_for(15000 * MS);
    s = status();
    /* le 0x01 exige la phase init : 0x02 de code 4, en classe 3 — le premier 0x02 reçu après lui, avant la requête
     * suivante ; le flux continue (§ 8 ; § 2.3) */
    i01 = find(0, A_SEND, 0x01);
    r = i01 >= 0 ? find((size_t)i01, A_RX, 0x02) : -1;
    CHECK(r > i01 && LOG[r].frame.cls == 3 && LOG[r].frame.len == sizeof REFUS_P2 &&
              !memcmp(LOG[r].frame.msg, REFUS_P2, sizeof REFUS_P2) && find((size_t)i01 + 1, A_SEND, 0x07) > r,
          "le 0x01 refusé : 02 02 FF 04 00 00 00 00, classe 3, avant la requête suivante");
    CHECK(i01 >= 0 && find((size_t)i01, A_RX, 0x05) > 0 && find((size_t)i01, A_RX, 0x06) > 0, "le flux après le 0x01");
    /* la carte sert l'objectif : READY, la position que publie le flux, sans FAULT, en 15 s */
    CHECK(s.session_state == SESSION_READY && s.position_valid, "READY, position valide (état %s)", bsk_state_name(s.session_state));
    CHECK(s.focus_position == 16384, "position 16384 (%ld)", (long)s.focus_position);
    CHECK(!ever(SESSION_FAULT), "jamais FAULT");
    CHECK(lens_clean(), "le F051 reste dans son modèle (%s)", L.oom_why ? L.oom_why : "bloqué");
}

/* L'objectif resté alimenté (§ 8), le chemin de retour (décision de l'humain) : ses 0x01 refusés
 * en flux (0x02, que TRANSACTION ne reconnaît pas comme la réponse : trois échéances de 300 ms), le reset soft — un
 * 0x0A, accepté à l'état libre dans toute phase, qui fait passer le flux en phase init et coupe le flux (§ 2.4) ;
 * sa réponse, 0A + le masque accordé (R0A, § 1.2) —, puis l'init entière en phase init : le 0x01 servi (33 octets), le
 * 10 00, le 0x0A de fin d'init qui ramène le flux, READY. Les codes des 0x02 reçus ne sont pas jugés ici
 * (reste_alimente le fait) : ils dépendent de la réception d'un 0x01 juste après la poignée de main, que le constat
 * reste_alimente décrit. */
static void reste_alimente_reset(void)
{
    lens_sim_powered_t st = {.flow = true, .grant = R0A + 1};
    long i01, i0a, r0a, i01r, r01, j;
    size_t n01 = 0;
    bench();
    CHECK(lens_sim_start_powered(lens_sim_std(&L), phy_sim_now(), &st), "l'adaptateur accepte l'état resté alimenté");
    d2(true);
    run_for(15000 * MS);
    i01 = find(0, A_SEND, 0x01);
    i0a = find(0, A_SEND, 0x0A);
    for (j = i01; j >= 0 && j < i0a; j = find((size_t)j + 1, A_SEND, 0x01)) n01++;
    CHECK(i01 >= 0 && i0a > i01 && n01 == 3 && LOG[i0a].t == LOG[i01].t + 900 * MS,
          "trois 0x01 sans réponse, puis le 0x0A du reset soft à l'échéance du troisième (%zu)", n01);
    CHECK(i0a > 0 && LOG[i0a].frame.len == sizeof R0A && LOG[i0a].frame.msg[0] == 0x0A && LOG[i0a].frame.msg[1] == 0xFF &&
              LOG[i0a].frame.msg[2] == 0x7F && LOG[i0a].frame.msg[9] == 0x3F,
          "le 0x0A du reset soft : celui de l'init, 0A FF 7F 00 00 00 00 00 00 3F 00…");
    r0a = i0a > 0 ? find_rx_exact((size_t)i0a, R0A, sizeof R0A) : -1;
    i01r = i0a > 0 ? find((size_t)i0a, A_SEND, 0x01) : -1;
    CHECK(r0a > i0a && i01r > r0a, "sa réponse, 0A + le masque accordé, puis le 0x01");
    r01 = i01r > 0 ? find((size_t)i01r, A_RX, 0x01) : -1;
    CHECK(r01 > i01r && LOG[r01].frame.len == 33 && !memcmp(LOG[r01].frame.msg, R01_HEAD, sizeof R01_HEAD),
          "le 0x01 servi en phase init : 33 octets, 01 FF 9F 78…");
    CHECK(r01 > 0 && find_rx_exact((size_t)r01, R10_OK, sizeof R10_OK) > 0 &&
              find((size_t)find_rx_exact((size_t)r01, R10_OK, sizeof R10_OK), A_SEND, 0x0A) > 0,
          "l'init entière : 10 00, puis le 0x0A de fin d'init");
    CHECK(status().session_state == SESSION_READY && status().position_valid && status().focus_position == 16384,
          "READY, la position du flux (état %s, %ld)", bsk_state_name(status().session_state), (long)status().focus_position);
    CHECK(!ever(SESSION_RECOVERING) && !ever(SESSION_FAULT) && L.phase == LSTD_FLOW, "ni échec compté ni FAULT ; le F051 en flux");
    CHECK(lens_clean(), "le F051 reste dans son modèle (%s)", L.oom_why ? L.oom_why : "bloqué");
}

/* goto par 0x1D jusqu'à ARRIVED (§ 2.2 : 0x1D, en flux ; § 4.4 : 1D 00 dans le flux, après le 0x06). */
static void goto_1d(void)
{
    uint32_t g;
    size_t from, m0;
    long a;
    if (!ready_cold()) return;
    from = n_log;
    m0 = n_mchg;
    g = cmd(CMD_FOCUS_GOTO, 20000);
    run_for(3000 * MS);
    a = find_ack(from, 0x1D, 0x00);
    CHECK(find(from, A_SEND, 0x04) >= 0, "la consigne part avec la boucle");
    CHECK(a > 0, "1D 00 reçu après le 0x06, dans la même trame de classe 1");
    CHECK(count_rx(from, 0x02) == 0, "aucun 0x02 : le 0x1D est accepté en flux");
    CHECK(accepted_then(g, ACK_COMPLETED, E_OK), "ACK_ACCEPTED puis ACK_COMPLETED");
    CHECK(motion_seen(m0, MOTION_MOVING) && status().motion_state == MOTION_ARRIVED, "MOVING puis ARRIVED (%u)",
          status().motion_state);
    CHECK(status().focus_position == 20000 && lstd_position(&L) == 20000, "position 20000 (%ld, objectif %ld)",
          (long)status().focus_position, (long)lstd_position(&L));
    CHECK(lens_clean(), "le F051 reste dans son modèle (%s)", L.oom_why ? L.oom_why : "bloqué");
}

/* goto au-delà d'une butée (§ 4.3). Les bornes : les limites logicielles que le F051 publie dans son 0x06, offsets
 * 7-10 (§ 2.5, § 4.3 ; soft_low 16384, soft_high 22743 [NÉ]), resserrées de 5 pas par la carte (décision
 * de l'humain) : 16389 / 22738 ; la carte refuse une cible hors d'elles avant émission, E_LIMIT, la borne publiée
 * comprise, et accepte la borne resserrée : le travail y finit avec 1D 00. Butée dure : un objectif dans la zone de
 * butée dure (≥ hard_high 24216 [I]) finit un 0x1D au premier contrôle, sans bouger, quel que soit le sens, avec 1D 00 ;
 * il n'en sort pas (§ 4.3). La zone est atteinte par un homing qui y laisse la mise au point (home_pos
 * [NÉ] 24300), aucune cible ne pouvant y mener (bornes). */
static void goto_butee(void)
{
    uint32_t g;
    size_t from;
    if (!ready_cold()) return;
    CHECK(status().focus_min == 16389 && status().focus_max == 22738, "bornes du 0x06, ± 5 pas : 16389 / 22738 (%ld / %ld)",
          (long)status().focus_min, (long)status().focus_max);
    from = n_log;
    g = cmd(CMD_FOCUS_GOTO, 30000);
    run_for(500 * MS);
    CHECK(acks_of(g) == 1 && ack_of(g, 0)->a.result == ACK_REJECTED && ack_of(g, 0)->a.reason == E_LIMIT,
          "au-delà de la borne haute : ACK_REJECTED, E_LIMIT");
    g = cmd(CMD_FOCUS_GOTO, 22743);
    CHECK(acks_of(g) == 1 && ack_of(g, 0)->a.result == ACK_REJECTED && ack_of(g, 0)->a.reason == E_LIMIT,
          "à la borne haute publiée : ACK_REJECTED, E_LIMIT");
    CHECK(count_rx(from, 0x02) == 0 && lstd_position(&L) == 16384 && status().motion_state == MOTION_IDLE,
          "rien d'émis vers l'objectif, pas bougé");
    for (size_t i = from; at(i); i++)
        CHECK(!(LOG[i].kind == A_SEND && LOG[i].frame.msg[0] == 0x04 && LOG[i].frame.len > 14), "aucun 0x1D émis");
    from = n_log;
    g = cmd(CMD_FOCUS_GOTO, 22738);
    run_for(3000 * MS);
    CHECK(find_ack(from, 0x1D, 0x00) > 0, "à la borne : 1D 00 dans le flux");
    CHECK(accepted_then(g, ACK_COMPLETED, E_OK) && status().motion_state == MOTION_ARRIVED, "à la borne : ARRIVED, ACK_COMPLETED");
    CHECK(status().focus_position == 22738 && lstd_position(&L) == 22738, "à la borne : arrêt à 22738 (%ld)",
          (long)status().focus_position);

    bench();
    LP.home_pos = 24300;
    params();
    mount_cold();
    run_for(10000 * MS);
    CHECK(status().session_state == SESSION_READY && status().focus_position == 24300,
          "butée dure : READY à 24300 (état %s, %ld)", bsk_state_name(status().session_state), (long)status().focus_position);
    if (status().session_state != SESSION_READY) return;
    from = n_log;
    g = cmd(CMD_FOCUS_GOTO, 20000);
    run_for(3000 * MS);
    CHECK(find_ack(from, 0x1D, 0x00) > 0, "butée dure : 1D 00 dans le flux");
    CHECK(lstd_position(&L) == 24300 && status().focus_position == 24300, "butée dure : pas bougé (%ld)", (long)lstd_position(&L));
    CHECK(accepted_then(g, ACK_COMPLETED, E_OK) && status().motion_state == MOTION_ARRIVED, "butée dure : ARRIVED, ACK_COMPLETED");
}

/* Une minute de LOG ALL depuis la mise sous tension du F051 — son init, la mise en page de son 0x0A, trois gotos ; pas
 * de bague, que le faux F051 ne modélise pas —, vidé toutes les 20 ms comme la tâche de la carte (le décodeur de test de
 * test_session135) : chaque trame décodée du journal est la suivante de son sens sur le fil, octet pour octet ; rien de
 * tu ni de perdu ; la pire seconde sous la moitié du plancher du lien USB, 64 000 octets par seconde. */
typedef struct { size_t wi[2]; unsigned same, differ, diff_lines, x1d, ack06, x0a; } wire_cmp_t;

static void minute_frame(const jd_frame_t *d, void *ctx)
{
    wire_cmp_t *c = ctx;
    act_kind_t k = d->rx ? A_RX : A_SEND;
    size_t *i = &c->wi[d->rx];
    const bsk_frame_t *w;
    while (*i < n_log && *i < LOG_CAP && LOG[*i].kind != k) (*i)++;
    if (*i >= n_log || *i >= LOG_CAP) {
        c->differ++;
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
        c->ack06 += d->rx && w->msg[0] == 0x06 && w->len > LEN06;
        c->x0a += w->msg[0] == 0x0A;
    }
    (*i)++;
}

static void log_all_minute(void)
{
    static jd_meter_t m;
    wire_cmp_t c = {{0, 0}, 0, 0, 0, 0, 0, 0};
    uint64_t t0;
    uint32_t d0, worst_s;
    size_t wire = 0, pairs = 0, off_phase = 0;
    bench();
    jd_reset();
    memset(&m, 0, sizeof m);
    d0 = bsk_journal_dropped();
    bsk_journal_set(true, true);
    t0 = m.t0_us = phy_sim_now();
    mount_cold();
    for (uint64_t t = t0 + 20 * MS; t <= t0 + 60000 * MS; t += 20 * MS) {
        uint64_t el = t - t0;
        if (el == 15000 * MS) (void)cmd(CMD_FOCUS_GOTO, 20000);
        if (el == 25000 * MS) (void)cmd(CMD_FOCUS_GOTO, 17000);
        if (el == 40000 * MS) (void)cmd(CMD_FOCUS_GOTO, 21000);
        run_to(t);
        jd_drain(&m, t, minute_frame, &c);
    }
    for (size_t i = 0; i < n_log && i < LOG_CAP; i++) wire += LOG[i].kind == A_SEND || LOG[i].kind == A_RX;
    for (size_t v = 0, i = 0; v + 1 < n_vd; v++) {           /* la paire à VD + 8,6 / + 10,1 ms, LOG ALL tenu */
        size_t n03 = 0, n04 = 0;
        bool at03 = false, at04 = false;
        if (VD_EDGES[v] < t0 + 15000 * MS) continue;           /* la boucle lancée (le premier goto à 15 s) */
        while (i < n_log && i < LOG_CAP && LOG[i].t < VD_EDGES[v]) i++;
        for (size_t k = i; k < n_log && k < LOG_CAP && LOG[k].t < VD_EDGES[v + 1]; k++) {
            if (LOG[k].kind != A_SEND) continue;
            if (LOG[k].frame.msg[0] == 0x03) n03++, at03 = LOG[k].t == VD_EDGES[v] + 8600;
            if (LOG[k].frame.msg[0] == 0x04) n04++, at04 = LOG[k].t == VD_EDGES[v] + 10100;
        }
        pairs++;
        off_phase += n03 != 1 || n04 != 1 || !at03 || !at04;
    }
    worst_s = jd_worst_second(&m, 60);
    printf("    %u trames au journal (%zu sur le fil), %u en différence ; %llu octets/s en moyenne, %lu au pire sur une seconde ; "
           "l'anneau : %zu octets au plus à un vidage (20 ms), %lu au plus en 500 ms\n",
           m.frames, wire, c.diff_lines, (unsigned long long)(m.bytes / 60), (unsigned long)worst_s, m.ring_max,
           (unsigned long)jd_worst_window(&m, 25));
    CHECK(status().session_state == SESSION_READY && status().focus_position == 21000, "READY, le dernier goto arrivé (%ld)",
          (long)status().focus_position);
    CHECK(c.same == wire && c.differ == 0 && m.unknown == 0 && m.bad == 0,
          "chaque trame du fil au journal, dans l'ordre de son sens, reconstruite octet pour octet (%u sur %zu)", c.same, wire);
    CHECK(bsk_journal_dropped() == d0 && m.lost == 0 && m.rxflood == 0, "rien de tu ni de perdu : dropped %lu, lost %u, rxflood %u",
          (unsigned long)(bsk_journal_dropped() - d0), m.lost, m.rxflood);
    CHECK(c.x0a >= 2 && c.x1d >= 3 && c.ack06 >= 3 && c.diff_lines * 10 > wire * 9,
          "le flux couvert : le 0x0A et sa réponse (%u), le 0x04 + 0x1D (%u), le 0x06 accusé (%u), neuf trames sur dix en "
          "différence (%u)", c.x0a, c.x1d, c.ack06, c.diff_lines);
    CHECK(worst_s * 2 < 64000u && m.ring_max < 8192, "la pire seconde (%lu) sous 32 000 octets, l'anneau (%zu) sous 8 Ko",
          (unsigned long)worst_s, m.ring_max);
    CHECK(pairs >= 2600 && off_phase == 0, "la paire à VD + 8,6 / + 10,1 ms sous LOG ALL : %zu fronts, %zu hors phase", pairs,
          off_phase);
    CHECK(lens_clean(), "le F051 reste dans son modèle (%s)", L.oom_why ? L.oom_why : "bloqué");
    bsk_journal_set(false, false);
}

/* q pendant un goto : 0x1C, travail « arrêt » (§ 4.5) ; 1C 01 dans le flux (§ 4.4) ; le 0x1D écrasé
 * par le 0x1C n'a pas d'accusé (§ 4.4, éviction) ; la carte : ABORTED. Le moteur à 5000 pas/s
 * (steps_per_s [NÉ]) : le goto de 16384 à 22000 dure plus d'une seconde. */
static void q_1c(void)
{
    uint32_t g, q;
    size_t m0, fq;
    int32_t p;
    bench();
    LP.steps_per_s = 5000;
    params();
    mount_cold();
    run_for(10000 * MS);
    CHECK(status().session_state == SESSION_READY, "précondition : READY (état %s)", bsk_state_name(status().session_state));
    if (status().session_state != SESSION_READY) return;
    m0 = n_mchg;
    g = cmd(CMD_FOCUS_GOTO, 22000);
    run_for(300 * MS);
    fq = n_log;
    q = cmd(CMD_FOCUS_STOP, 0);
    run_for(1000 * MS);
    p = lstd_position(&L);
    CHECK(find(fq, A_SEND, 0x1C) >= 0 && LOG[find(fq, A_SEND, 0x1C)].frame.cls == 1, "0x1C émis, classe 1");
    CHECK(find_ack(fq, 0x1C, 0x01) > 0, "1C 01 dans le flux, après le 0x06");
    CHECK(find_ack(fq, 0x1D, 0x00) < 0 && find_ack(fq, 0x1D, 0x01) < 0, "aucun accusé 1D après le 0x1C");
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK), "q : ACK_ACCEPTED puis ACK_COMPLETED");
    CHECK(accepted_then(g, ACK_FAILED, E_ABORTED), "le goto : ACK_FAILED, E_ABORTED");
    CHECK(motion_seen(m0, MOTION_MOVING) && status().motion_state == MOTION_ABORTED, "MOVING puis ABORTED (%u)",
          status().motion_state);
    CHECK(p > 16384 && p < 22000, "arrêté en route (%ld)", (long)p);
    run_for(500 * MS);
    CHECK(lstd_position(&L) == p && status().focus_position == p, "immobile, la position publiée (%ld)", (long)status().focus_position);
}

/* Le homing de l'init répond 10 01 (homing_fails [NÉ] : un indicateur d'erreur des modules, § 3) : FAULT,
 * E_HOME_FAILED, terminal, sans réessai (E3) ; aucun 0x0A après. */
static void homing_10_01(void)
{
    long r;
    bench();
    LP.homing_fails = true;
    params();
    mount_cold();
    run_for(40000 * MS);
    r = find_rx_exact(0, R10_FAIL, sizeof R10_FAIL);
    CHECK(r > 0, "10 01 reçu");
    CHECK(status().session_state == SESSION_FAULT && status().last_error == E_HOME_FAILED, "FAULT, E_HOME_FAILED (%s, %s)",
          bsk_state_name(status().session_state), bsk_err_token(status().last_error));
    CHECK(r > 0 && find((size_t)r, A_SEND, 0x0A) < 0 && find((size_t)r, A_SEND, 0x01) < 0, "ni 0x0A ni nouvel essai après");
    CHECK(!ever(SESSION_RECOVERING) && !ever(SESSION_READY), "ni reprise ni READY");
}

/* La couche HOTE de bout en bout, comme test_host135 : v, t, r, f<n>, e, f, q. Les réponses sont celles de
 * PROTOCOL.md telles que la couche HOTE les écrit (components/host/host.c) ; les valeurs, celles du F051 : son nom
 * (§ 1.1, 0x3F), sa position (16384, home_pos [NÉ]), ses bornes (0x06, offsets 7-10, goto_butee ci-dessus), sa plage
 * d'ouverture, réponse au 0x08, octets 1-4, 12 13 / 00 19 (§ 1.2) : f/2,9 et f/22,6, soit 2.8-22 au
 * tiers usuel ; après a5.6, le 0x03 porte le code de f/5,6, 0x14F9, calculé à la main
 * (round(256 × (2·log2(5,6) + 16)) = 5369). */
static char R[BSK_HOST_REPLY_MAX];

static const char *ask(const char *line)
{
    if (!bsk_host_line(line, R, sizeof R)) R[0] = 0;
    return R;
}

#define ASK(line, want) CHECK(!strcmp(ask(line), want), "« %s » -> « %s », attendu « %s »", line, R, want)
#define T_HAS(kv) CHECK(strstr(ask("t"), kv) != NULL, "t porte %s : « %s »", kv, R)
/* move_rc, un champ de `DEBUG` : " move_rc=<v>", suivi d'une espace ou de la fin de la ligne. */
static bool has_field(const char *line, const char *kv)
{
    const char *p = strstr(line, kv);
    return p && (p[strlen(kv)] == ' ' || p[strlen(kv)] == '\0');
}
#define MOVE_RC(v) CHECK(has_field(ask("DEBUG"), " move_rc=" v), "DEBUG porte move_rc=%s : « %s »", v, R)

static void hote(void)
{
    size_t from;
    bench();
    host_mode = true;
    LP.steps_per_s = 5000;                           /* q pendant le second goto : plus d'une seconde de mouvement */
    params();
    ASK("v", "1.0");
    T_HAS(" present=0 ");
    ASK("f", "nc");
    mount_cold();
    run_for(10000 * MS);
    ASK("v", "1.0");
    T_HAS(" present=1 ");
    T_HAS(" boot_state=ready ");
    T_HAS(" last_op=boot:ok ");
    ASK("i", F051_NAME);
    ASK("r", "16389-22738");                         /* 5 pas en dedans des bornes publiées */
    ASK("a", "2.8-22");
    from = n_log;
    ASK("a5.6", "ok");
    run_for(100 * MS);
    CHECK(ap03_all(from, 0x14F9), "après a5.6 : chaque 0x03 porte F9 14 F9 14");
    ASK("f30000", "er range limits");
    ASK("f", "16384");
    ASK("e", "n");
    ASK("f20000", "ok");
    ASK("e", "y");
    run_for(3000 * MS);
    ASK("e", "n");
    ASK("f", "20000");
    MOVE_RC("ok");
    ASK("f16500", "ok");
    run_for(300 * MS);
    ASK("q", "ok");
    run_for(1000 * MS);
    ASK("e", "n");
    MOVE_RC("aborted");
    CHECK(atoi(ask("f")) > 16500 && atoi(R) < 20000, "f : arrêté en route (« %s »)", R);
}

/* Le retour à la marque au démarrage, contre le F051. La clé de la marque, calculée à la main : les
 * octets 1-2 et 10-11 du 0x07 (01 03, 34 C1 : R07_HEAD), l'octet 1 de la réponse au 0x08 (12), puis la focale nominale
 * du 0x05, offsets 26-27, que le modèle laisse à 0 : (0 + 5) / 10 = 0, « 000 ». La marque 20000, sens inconnu : arrivée en décroissant ; le F051 finit son
 * init en 16384 (home_pos [NÉ]), sous elle : le trajet direct arriverait en croissant. Le F051 n'est pas le 135 : X = 1 % de
 * la course du 0x06, (22738 − 16389) / 100 = 63 ; d'abord 20063, puis 20000. */
#define KEYF051 "010334c112000"

static void restauration(void)
{
    uint16_t t[4] = {0};
    size_t n = 0;
    bench();
    store_sim_poke(KEYF051, 20000);
    mount_cold();
    run_for(15000 * MS);
    for (size_t i = 0; at(i); i++)
        if (LOG[i].kind == A_SEND && LOG[i].frame.msg[0] == 0x04 && LOG[i].frame.len == 19 && LOG[i].frame.msg[14] == 0x1D) {
            if (n < 4) t[n] = (uint16_t)(LOG[i].frame.msg[15] | LOG[i].frame.msg[16] << 8);
            n++;
        }
    CHECK(ever(SESSION_RESTORING) && n_chg >= 2 && chg[n_chg - 2].state == SESSION_RESTORING && chg[n_chg - 1].state == SESSION_READY,
          "RESTORING, puis READY");
    CHECK(n == 2 && t[0] == 20063 && t[1] == 20000, "deux 0x1D, 20063 puis 20000 : X = 63 (%zu : %u, %u)", n, t[0], t[1]);
    CHECK(status().session_state == SESSION_READY && status().focus_position == 20000 && lstd_position(&L) == 20000 &&
              status().mark_valid && status().mark_position == 20000,
          "READY sur la marque, 20000 (%ld)", (long)status().focus_position);
    CHECK(g_store_sim.writes == 0 && n_acks == 0, "rien d'écrit dans le magasin (la marque ne s'écrit que sur un geste), aucun accusé");
    CHECK(count_rx(0, 0x02) == 0 && lens_clean(), "aucun 0x02 ; le F051 reste dans son modèle (%s)", L.oom_why ? L.oom_why : "bloqué");
}

/* L'adaptateur du F051 à l'interface commune (sim/lens_sim_std.c) : il refuse, sans rien démarrer, ce que le F051 ne
 * sait pas représenter — le mode service (aucun canal 0x40, § 5.2 : ≥ 0x40 refusé), un flux sans mise en page, une
 * mise en page sans flux — et passe la mise en page telle quelle. */
static void interface_std(void)
{
    static const uint8_t other[16] = {0xFF, 0x01, 0, 0, 0, 0, 0, 0, 0x01};   /* blocs 0x05 1 à 8 et 9, 0x06 bloc 1 */
    lens_sim_powered_t svc = {.flow = true, .service = true, .grant = R0A + 1};
    lens_sim_powered_t no_grant = {.flow = true};
    lens_sim_powered_t no_flow = {.flow = false, .grant = R0A + 1};
    lens_sim_powered_t idle = {.flow = false};
    lens_sim_powered_t ok = {.flow = true, .grant = other};
    lens_sim_t l;
    bench();
    l = lens_sim_std(&L);
    CHECK(!lens_sim_start_powered(l, phy_sim_now(), &svc) && !L.powered, "mode service : refusé, rien démarré");
    CHECK(!lens_sim_start_powered(l, phy_sim_now(), &no_grant) && !L.powered, "flux sans mise en page : refusé");
    CHECK(!lens_sim_start_powered(l, phy_sim_now(), &no_flow) && !L.powered, "mise en page sans flux : refusée");
    CHECK(lens_sim_start_powered(l, phy_sim_now(), &idle) && L.powered && L.phase == LSTD_INIT, "sans flux : phase init");
    bench();
    l = lens_sim_std(&L);
    CHECK(lens_sim_start_powered(l, phy_sim_now(), &ok) && L.powered && L.phase == LSTD_FLOW, "flux : en flux");
    /* la mise en page passée telle quelle : 0x05 blocs 1 à 9 (8 1 8 3 10 28 2 6 1), 0x06 bloc 1 (2) */
    CHECK(L.end05 == 6 + 67 && L.end06 == 6 + 2, "la mise en page de `grant` (fin 0x05 %u, 0x06 %u)", L.end05, L.end06);
    CHECK(lens_sim_byte_us(l) == LP.byte_us && lens_sim_lens_cs_tail_us(l) == LP.lens_cs_tail_us,
          "byte_us et lens_cs_tail_us : ceux du modèle");
    CHECK(!strcmp(l.ops->name, "faux Tamron F051"), "nom : %s", l.ops->name);

    /* les sorties converties : la poignée de main (LENS_CS haute puis basse, lens_std.c « Couche physique »), puis la
     * réponse au 0x01 (§ 1.2), octet par octet, F0 en tête et 55 en fin */
    {
        static const uint8_t m01[33] = {0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01};
        uint8_t b[64];
        size_t n = fr_encode(2, 0, m01, sizeof m01, b, sizeof b), cs = 0, bytes = 0;
        uint64_t t;
        lens_sim_out_t o;
        uint8_t first = 0, last = 0;
        bool order = true;
        bench();
        l = lens_sim_std(&L);
        t = phy_sim_now();
        lens_sim_power(l, t, true);
        t += LP.boot_us + 1000;
        lens_sim_body_cs(l, t, true);
        t += LP.hs_us + 1000;
        lens_sim_body_cs(l, t, false);
        t += 1000;
        lens_sim_body_cs(l, t, true);
        for (size_t i = 0; i < n; i++) lens_sim_byte(l, t += LP.byte_us, b[i]);
        lens_sim_body_cs(l, t += 40, false);
        lens_sim_advance(l, t + 10000);
        while (lens_sim_out(l, &o)) {
            if (o.kind == LENS_SIM_LENS_CS) order = order && o.v == (cs++ % 2 == 0);
            else {
                if (!bytes++) first = o.v;
                last = o.v;
            }
        }
        CHECK(cs == 4 && order, "LENS_CS : haute, basse, haute, basse (%zu changements)", cs);
        CHECK(bytes == 41 && first == 0xF0 && last == 0x55, "la réponse au 0x01 : 41 octets, F0 … 55 (%zu, %02X … %02X)", bytes,
              first, last);
    }
}

/* ─────────────────────────── les renvois de l'init ─────────────────────────── */

/* L'entrée de la k-ième trame émise (A_SEND) ou reçue (A_RX) de type `type`, -1 sinon. */
static long nth(act_kind_t kind, uint8_t type, unsigned k)
{
    long i = find(0, kind, type);
    while (i >= 0 && k--) i = find((size_t)i + 1, kind, type);
    return i;
}

static unsigned n_sent(uint8_t type)
{
    unsigned n = 0;
    while (nth(A_SEND, type, n) >= 0) n++;
    return n;
}

/* Les envois de `type` : `n` exactement, le même message, le k-ième à t0 + dt[k] (µs). */
static bool sends_at(uint8_t type, unsigned n, const uint64_t *dt)
{
    long a = nth(A_SEND, type, 0);
    bool ok = a >= 0 && n_sent(type) == n;
    for (unsigned k = 1; ok && k < n; k++) {
        long b = nth(A_SEND, type, k);
        ok = LOG[b].frame.len == LOG[a].frame.len && !memcmp(LOG[b].frame.msg, LOG[a].frame.msg, LOG[a].frame.len) &&
             LOG[b].t == LOG[a].t + dt[k];
    }
    if (!ok) {
        printf("  0x%02X émis %u fois :", type, n_sent(type));
        for (unsigned k = 0; nth(A_SEND, type, k) >= 0; k++)
            printf(" +%llu µs", (unsigned long long)(LOG[nth(A_SEND, type, k)].t - LOG[a].t));
        printf("\n");
    }
    return ok;
}

/* La réponse au 0x07 perdue, celle au 0x0D reçue fausse : chacune renvoyée à son échéance, telle quelle, une fois ; l'init
 * complète. */
static void renvoi_init(void)
{
    static const uint8_t once[] = {0x01, 0x3F, 0x08, 0x0B, 0x09, 0x10, 0x0A};
    rule_t *r07, *r0d;
    long b;
    bsk_status_t st;
    bench();
    r07 = lose(false, 0x07, 1, PHY_SIM_LOSE);
    r0d = lose(false, 0x0D, 1, PHY_SIM_GARBLE);
    mount_cold();
    run_for(10000 * MS);
    st = status();
    CHECK(r07->hit == 1 && sends_at(0x07, 2, (const uint64_t[]){0, 300 * MS}),
          "0x07 : sa réponse perdue, le même 0x07 à son échéance (300 ms), une fois");
    b = nth(A_SEND, 0x0D, 1);
    CHECK(r0d->hit == 1 && b >= 0 && sends_at(0x0D, 2, (const uint64_t[]){0, 300 * MS}) && LOG[b].t > r0d->t_hit &&
              LOG[b].frame.len == 2 && LOG[b].frame.msg[1] == 0x00,
          "0x0D : sa réponse reçue fausse, l'erreur ne coûte que la trame : le même 0x0D à son échéance (300 ms), une fois");
    for (size_t k = 0; k < sizeof once; k++) CHECK(n_sent(once[k]) == 1, "0x%02X émis une fois", once[k]);
    CHECK(st.session_state == SESSION_READY && !ever(SESSION_RECOVERING) && st.lens_id_product == F051_TYPE2 &&
              !strcmp(st.lens_name, F051_NAME) && (st.capabilities & CAP_APERTURE),
          "READY sans reprise : l'identité, le nom et la plage d'ouverture publiés");
    CHECK(count_rx(0, 0x02) == 0 && lens_clean(), "aucun refus : le 0x07 et le 0x0D reçus deux fois sont acceptés (§ 2.2)");
}

/* Trois réponses au 0x09 perdues : trois envois, puis la boucle à l'échéance du troisième (init_end), sans 0x0D ni 0x0A. */
static void renvoi_init_3(void)
{
    rule_t *r;
    long c;
    bench();
    r = lose(false, 0x09, 3, PHY_SIM_LOSE);
    mount_cold();
    run_for(1500 * MS);
    c = nth(A_SEND, 0x09, 2);
    CHECK(r->hit == 3 && sends_at(0x09, 3, (const uint64_t[]){0, 300 * MS, 600 * MS}),
          "0x09 sans réponse : trois envois, chacun à l'échéance du précédent (300 ms)");
    CHECK(c >= 0 && n_sent(0x0D) == 0 && n_sent(0x10) == 0 && n_sent(0x0A) == 0 && find((size_t)c, A_SEND, 0x03) >= 0 &&
              LOG[find((size_t)c, A_SEND, 0x03)].t >= LOG[c].t + 300 * MS,
          "… puis plus aucune requête d'init ; la boucle à l'échéance du troisième (init_end)");
}

/* Le 0x0A et le 0x10 de l'init, leur réponse perdue : jamais renvoyés. Le 0x0A reçu, le F051 émet son flux
 * à chaque VD sans attendre la boucle de la carte (§ 2.5) ; ce 0x05, reçu avant que la carte ne lance sa boucle,
 * ne sert pas S_FIRST05 : le homing du pilote part au premier 0x05 reçu après le lancement, dans la période de VD (60 Hz)
 * qui suit l'échéance du 0x0A. */
static void renvoi_jamais(void)
{
    rule_t *r;
    long i, i1c, i05;
    uint64_t tl;
    bench();
    r = lose(false, 0x0A, 1, PHY_SIM_LOSE);
    mount_cold();
    run_for(10000 * MS);
    i = nth(A_SEND, 0x0A, 0);
    CHECK(r->hit == 1 && n_sent(0x0A) == 1, "0x0A : sa réponse perdue, jamais renvoyé (une bascule, § 2.4)");
    tl = i >= 0 ? LOG[i].t + 400 * MS : 0;          /* son échéance : init_end, la boucle lancée */
    for (i05 = i >= 0 ? find((size_t)i, A_RX, 0x05) : -1; i05 >= 0 && LOG[i05].t < tl; i05 = find((size_t)i05 + 1, A_RX, 0x05)) {}
    i1c = find(0, A_SEND, 0x1C);
    CHECK(i >= 0 && find((size_t)i, A_RX, 0x05) >= 0 && LOG[find((size_t)i, A_RX, 0x05)].t < tl,
          "… le F051 en flux avant l'échéance du 0x0A (son 0x0A reçu)");
    CHECK(i05 >= 0 && i1c >= 0 && LOG[i1c].t == LOG[i05].t && LOG[i05].t < tl + 16667 &&
              nth(A_SEND, 0x10, 1) >= 0 && LOG[nth(A_SEND, 0x10, 1)].frame.msg[1] == 0x08,
          "… init non faite : à son échéance (400 ms), la boucle ; au premier 0x05 reçu après (%lld µs), le homing du pilote, "
          "0x1C puis 0x10 08", i05 >= 0 ? (long long)(LOG[i05].t - tl) : -1LL);

    bench();
    r = lose(false, 0x10, 1, PHY_SIM_LOSE);
    mount_cold();
    run_for(8500 * MS);
    i = nth(A_SEND, 0x10, 0);
    CHECK(r->hit == 1 && i >= 0 && LOG[i].frame.msg[1] == 0x1F && n_sent(0x10) == 1 && n_sent(0x0A) == 0,
          "0x10 1F : sa réponse perdue, jamais renvoyé ; pas de 0x0A");
    CHECK(i >= 0 && find(0, A_SEND, 0x03) >= 0 && LOG[find(0, A_SEND, 0x03)].t >= LOG[i].t + 8000 * MS,
          "… la boucle à son échéance, 8 s (init_end)");
}

/* ─────────────────────────── le renvoi du goto ─────────────────────────── */

/* L'entrée de la k-ième trame 0x04 qui porte un 0x1D, depuis l'entrée `from` ; -1 sinon. */
static long nth_1d(size_t from, unsigned k)
{
    for (size_t i = from; at(i); i++)
        if (LOG[i].kind == A_SEND && LOG[i].frame.msg[0] == 0x04 && LOG[i].frame.len == 19 && LOG[i].frame.msg[14] == 0x1D &&
            k-- == 0)
            return (long)i;
    return -1;
}

/* Le 0x04 de la première paire émise à `t` ou après, depuis l'entrée `from` ; -1 sinon. */
static long pair_at(size_t from, uint64_t t)
{
    for (size_t i = from; at(i); i++)
        if (LOG[i].kind == A_SEND && LOG[i].frame.msg[0] == 0x04 && LOG[i].t >= t) return (long)i;
    return -1;
}

/* Le 0x1D perdu vers le F051 : le même, à 1 s, puis l'arrivée ; trois perdus : STALLED à 3 s ; un 0x1D reçu, une cible égale
 * à la position comprise (le travail fini au premier contrôle, 1D 00, § 4.1) : jamais renvoyé. */
static void renvoi_goto(void)
{
    rule_t *r;
    size_t from;
    uint64_t t0;
    uint32_t g;
    long a, b;
    if (!ready_cold()) return;
    r = lose(true, 0x1D, 1, PHY_SIM_LOSE);
    from = n_log;
    t0 = phy_sim_now();
    g = cmd(CMD_FOCUS_GOTO, 20000);
    run_for(4000 * MS);
    a = nth_1d(from, 0);
    b = nth_1d(from, 1);
    CHECK(r->hit == 1 && a == pair_at(from, t0) && b >= 0 && b == pair_at(from, t0 + 1000 * MS) &&
              !memcmp(LOG[a].frame.msg + 14, LOG[b].frame.msg + 14, 5) && nth_1d(from, 2) < 0,
          "le 0x1D perdu : le même, sur la première paire après 1 s, une fois");
    CHECK(accepted_then(g, ACK_COMPLETED, E_OK) && status().focus_position == 20000 && lstd_position(&L) == 20000,
          "puis l'arrivée, en 20000");

    from = n_log;
    g = cmd(CMD_FOCUS_GOTO, 20000);
    run_for(4000 * MS);
    CHECK(nth_1d(from, 0) >= 0 && nth_1d(from, 1) < 0 && accepted_then(g, ACK_COMPLETED, E_OK) && find_ack(from, 0x1D, 0x00) > 0,
          "la cible égale à la position : un seul 0x1D, 1D 00, ARRIVED");

    if (!ready_cold()) return;
    r = lose(true, 0x1D, 3, PHY_SIM_LOSE);
    from = n_log;
    t0 = phy_sim_now();
    g = cmd(CMD_FOCUS_GOTO, 20000);
    run_for(4000 * MS);
    CHECK(r->hit == 3 && nth_1d(from, 2) == pair_at(from, t0 + 2000 * MS) && nth_1d(from, 3) < 0,
          "trois 0x1D perdus : trois envois, à 0, 1 et 2 s");
    CHECK(accepted_then(g, ACK_FAILED, E_STALL) && ack_of(g, 1)->t == t0 + 3000 * MS && lstd_position(&L) == 16384,
          "STALLED, E_STALL, à 3 s exactement ; le F051 n'a pas bougé");
    CHECK(count_rx(0, 0x02) == 0 && lens_clean(), "aucun refus, le F051 dans son modèle");
}

/* ─────────────────────────── la surveillance de l'arrêt ─────────────────────────── */

/* Le journal : ses lignes depuis le dernier appel, cherchées exactement. */
static bool jline(const char *line)
{
    char b[400];
    uint8_t g;
    size_t n;
    bool found = false;
    while ((n = bsk_journal_take(b, sizeof b - 1, &g)) > 0) {
        b[n] = 0;
        found = found || !strcmp(b, line);
    }
    if (!found) printf("  absente du journal : « %s »\n", line);
    return found;
}

static void jflush(void)
{
    char b[400];
    uint8_t g;
    while (bsk_journal_take(b, sizeof b, &g) > 0) {}
}

/* L'entrée du k-ième 0x1C émis depuis `from` ; -1 sinon. */
static long nth_1c(size_t from, unsigned k)
{
    for (size_t i = from; at(i); i++)
        if (LOG[i].kind == A_SEND && LOG[i].frame.cls == 1 && LOG[i].frame.msg[0] == 0x1C && k-- == 0) return (long)i;
    return -1;
}

/* READY, le moteur lent (steps_per_s [NÉ] à 1000 pas/s) : un goto de 16384 à 22000 dure 5,6 s, plus que les trois
 * fenêtres de 1,5 s de l'arrêt ; le journal allumé. */
static bool ready_slow(void)
{
    bench();
    LP.steps_per_s = 1000;
    params();
    mount_cold();
    run_for(10000 * MS);
    CHECK(status().session_state == SESSION_READY, "précondition : READY (état %s)", bsk_state_name(status().session_state));
    bsk_journal_set(true, false);
    jflush();
    return status().session_state == SESSION_READY;
}

/* q pendant un goto (de 16384 à 22000, en mouvement depuis 300 ms) : le 0x1C perdu une fois, renvoyé à 1,5 s, l'arrêt ;
 * trois fois perdu : trois envois, l'abandon (« arrêt non confirmé ») ; reçu : jamais renvoyé, 1C 01 (§ 4.4). */
static void renvoi_arret(void)
{
    rule_t *r;
    size_t from;
    uint64_t tq;
    uint32_t q;
    long b;
    int32_t p;
    char l[80];
    if (!ready_slow()) return;
    (void)cmd(CMD_FOCUS_GOTO, 22000);
    run_for(300 * MS);
    r = lose(true, 0x1C, 1, PHY_SIM_LOSE);
    from = n_log;
    tq = phy_sim_now();
    q = cmd(CMD_FOCUS_STOP, 0);
    run_for(1400 * MS);
    CHECK(lstd_position(&L) > 16384 + 1500 && nth_1c(from, 1) < 0, "le 0x1C perdu : le F051 roule toujours à 1,4 s, un seul envoi");
    run_for(4600 * MS);
    p = lstd_position(&L);
    b = nth_1c(from, 1);
    CHECK(r->hit == 1 && nth_1c(from, 0) >= 0 && LOG[nth_1c(from, 0)].t == tq && b >= 0 && LOG[b].t == tq + 1500 * MS &&
              nth_1c(from, 2) < 0,
          "le même 0x1C à 1,5 s exactement, une fois");
    CHECK(b >= 0 && find_ack((size_t)b, 0x1C, 0x01) > 0 && p < 22000 && accepted_then(q, ACK_COMPLETED, E_OK),
          "puis l'arrêt : 1C 01, arrêté en route (%ld) ; q : ACK_COMPLETED", (long)p);
    snprintf(l, sizeof l, "* resend %llu msg=1C n=2 why=timeout", (unsigned long long)((tq + 1500 * MS) / 1000));
    CHECK(jline(l), "journal : le renvoi du 0x1C, à 1,5 s");

    if (!ready_slow()) return;
    (void)cmd(CMD_FOCUS_GOTO, 22000);
    run_for(300 * MS);
    r = lose(true, 0x1C, 3, PHY_SIM_LOSE);
    from = n_log;
    tq = phy_sim_now();
    (void)cmd(CMD_FOCUS_STOP, 0);
    run_for(6000 * MS);
    CHECK(r->hit == 3 && nth_1c(from, 1) >= 0 && LOG[nth_1c(from, 1)].t == tq + 1500 * MS && nth_1c(from, 2) >= 0 &&
              LOG[nth_1c(from, 2)].t == tq + 3000 * MS && nth_1c(from, 3) < 0,
          "trois 0x1C perdus : trois envois, à 0, 1,5 et 3 s, pas un quatrième");
    CHECK(lstd_position(&L) == 22000, "jamais reçu : le F051 finit son goto, 22000 (%ld)", (long)lstd_position(&L));
    snprintf(l, sizeof l, "* resend %llu msg=1C giveup why=timeout", (unsigned long long)((tq + 4500 * MS) / 1000));
    CHECK(jline(l), "journal : l'abandon, « arrêt non confirmé », à 4,5 s");

    if (!ready_slow()) return;
    (void)cmd(CMD_FOCUS_GOTO, 22000);
    run_for(300 * MS);
    from = n_log;
    (void)cmd(CMD_FOCUS_STOP, 0);
    run_for(6000 * MS);
    CHECK(nth_1c(from, 0) >= 0 && nth_1c(from, 1) < 0 && find_ack(from, 0x1C, 0x01) > 0 && lstd_position(&L) < 22000,
          "le 0x1C reçu : 1C 01, jamais renvoyé");
}

/* Un goto admis pendant la surveillance de l'arrêt l'éteint : le 0x1C perdu n'est pas renvoyé, il arrêterait ce goto. */
static void renvoi_arret_goto(void)
{
    rule_t *r;
    size_t from;
    uint32_t g;
    if (!ready_slow()) return;
    (void)cmd(CMD_FOCUS_GOTO, 22000);
    run_for(300 * MS);
    r = lose(true, 0x1C, 1, PHY_SIM_LOSE);
    from = n_log;
    (void)cmd(CMD_FOCUS_STOP, 0);
    run_for(500 * MS);
    g = cmd(CMD_FOCUS_GOTO, 22000);
    run_for(8000 * MS);
    CHECK(r->hit == 1 && nth_1c(from, 1) < 0, "aucun 0x1C après le goto admis");
    CHECK(accepted_then(g, ACK_COMPLETED, E_OK) && lstd_position(&L) == 22000, "le goto arrive, 22000 (%ld)", (long)lstd_position(&L));
}

/* q en RESTORING (le retour à la marque 20000, le moteur lent) : le 0x1C perdu, renvoyé à 1,5 s, en READY. */
static void renvoi_arret_restoring(void)
{
    rule_t *r;
    size_t from;
    uint64_t tq;
    uint32_t q;
    bench();
    LP.steps_per_s = 1000;
    params();
    store_sim_poke(KEYF051, 20000);
    mount_cold();
    while (!(status().session_state == SESSION_RESTORING && status().motion_state == MOTION_MOVING) &&
           phy_sim_now() < T0 + 15000 * MS)
        run_for(1 * MS);
    run_for(300 * MS);
    CHECK(status().session_state == SESSION_RESTORING, "précondition : RESTORING, en mouvement");
    r = lose(true, 0x1C, 1, PHY_SIM_LOSE);
    from = n_log;
    tq = phy_sim_now();
    q = cmd(CMD_FOCUS_STOP, 0);
    run_for(5000 * MS);
    CHECK(accepted_then(q, ACK_COMPLETED, E_OK) && status().session_state == SESSION_READY, "q : READY");
    CHECK(r->hit == 1 && nth_1c(from, 1) >= 0 && LOG[nth_1c(from, 1)].t == tq + 1500 * MS && nth_1c(from, 2) < 0 &&
              find_ack((size_t)nth_1c(from, 1), 0x1C, 0x01) > 0 && lstd_position(&L) < 20063,
          "le 0x1C perdu : le même à 1,5 s, une fois, 1C 01, arrêté en route (%ld)", (long)lstd_position(&L));
}


/* ─────────────────────────── perte, reprise, faute ─────────────────────────── */

/* Les changements d'état publiés à partir de l'instant `t` : `n` exactement, dans l'ordre. */
static bool states_from(uint64_t t, const uint8_t *st, size_t n)
{
    size_t k = 0;
    while (k < n_chg && chg[k].t < t) k++;
    if (n_chg - k != n) return false;
    for (size_t i = 0; i < n; i++)
        if (chg[k + i].state != st[i]) return false;
    return true;
}

/* Un F051 muet (silent : la poignée de main reste, aucune trame) : à chaque essai, trois 0x01 à 300 ms (init_ms), le reset
 * soft (le 0x0A de l'init, 400 ms, info_ms) sans réponse, trois 0x01 ; l'échec compté 3 x 300 + 400 + 3 x 300 = 2200 ms après
 * le premier 0x01 (E5) ; RECOVERING, POWERING 3 s après ; au quatrième échec, FAULT, E_LOST : 24 0x01, 4 0x0A, rien
 * d'autre de l'init. */
static void muet_fault(void)
{
    static const uint8_t st[] = {SESSION_POWERING, SESSION_IDENTIFYING, SESSION_RECOVERING, SESSION_POWERING,
                                 SESSION_IDENTIFYING, SESSION_RECOVERING, SESSION_POWERING, SESSION_IDENTIFYING,
                                 SESSION_RECOVERING, SESSION_POWERING, SESSION_IDENTIFYING, SESSION_FAULT};
    long i01;
    size_t nr = 0;
    bench();
    L.f.silent = true;
    mount_cold();
    run_for(30000 * MS);
    i01 = find(0, A_SEND, 0x01);
    CHECK(states_from(0, st, sizeof st), "trois fois POWERING, IDENTIFYING, RECOVERING, puis POWERING, IDENTIFYING, FAULT");
    CHECK(i01 >= 0 && n_chg > 2 && chg[2].state == SESSION_RECOVERING && chg[2].t == LOG[i01].t + 2200 * MS,
          "le premier échec : 3 x 0x01, le 0x0A, 3 x 0x01 : RECOVERING 2200 ms après le premier 0x01");
    for (size_t k = 0; k + 1 < n_chg; k++)
        if (chg[k].state == SESSION_RECOVERING) {
            nr++;
            CHECK(chg[k + 1].state == SESSION_POWERING && chg[k + 1].t == chg[k].t + 3000 * MS, "reprise %zu : POWERING 3 s après", nr);
        }
    CHECK(nr == 3 && status().session_state == SESSION_FAULT && status().last_error == E_LOST, "trois reprises, FAULT, E_LOST");
    CHECK(n_sent(0x01) == 24 && n_sent(0x0A) == 4 && n_sent(0x07) == 0 && find(0, A_SEND, 0x03) < 0,
          "24 0x01, 4 0x0A (le reset soft de chaque essai), ni 0x07 ni boucle (%u, %u)", n_sent(0x01), n_sent(0x0A));
    CHECK(count_rx(0, 0x01) == 0 && L.phase == LSTD_INIT && !lstd_out_of_model(&L), "rien reçu ; le F051 en phase init, dans son modèle");
}

/* Le premier 0x01 perdu sur le fil (le F051 ne le reçoit pas : muet pour la carte), le second répondu : le même 0x01 300 ms
 * après (init_ms), le 0x07 à sa réponse ; l'init entière, sans reset soft (un seul 0x0A, celui de la fin d'init), READY sans
 * reprise. */
static void muet_puis_repond(void)
{
    rule_t *r;
    long a, b, rb, c07;
    bench();
    r = lose(true, 0x01, 1, PHY_SIM_LOSE);
    mount_cold();
    run_for(10000 * MS);
    a = nth(A_SEND, 0x01, 0);
    b = nth(A_SEND, 0x01, 1);
    rb = b >= 0 ? find((size_t)b, A_RX, 0x01) : -1;
    c07 = find(0, A_SEND, 0x07);
    CHECK(r->hit == 1 && sends_at(0x01, 2, (const uint64_t[]){0, 300 * MS}) && find(0, A_RX, 0x01) == rb,
          "le premier 0x01 perdu : le même 0x01 à son échéance (300 ms), seul répondu");
    CHECK(rb > b && LOG[rb].frame.len == 33 && !memcmp(LOG[rb].frame.msg, R01_HEAD, sizeof R01_HEAD) && c07 > rb &&
              LOG[c07].t == LOG[rb].t && a >= 0,
          "sa réponse, 33 octets, 01 FF 9F 78… ; le 0x07 à sa réponse");
    CHECK(n_sent(0x0A) == 1 && find_rx_exact(0, R10_OK, sizeof R10_OK) > 0 &&
              nth(A_SEND, 0x0A, 0) > find_rx_exact(0, R10_OK, sizeof R10_OK),
          "sans reset soft : un seul 0x0A, celui de la fin d'init, après le 10 00");
    CHECK(status().session_state == SESSION_READY && status().focus_position == 16384 && !ever(SESSION_RECOVERING) &&
              !ever(SESSION_FAULT),
          "READY en 16384, sans reprise (état %s)", bsk_state_name(status().session_state));
    CHECK(count_rx(0, 0x02) == 0 && lens_clean(), "aucun refus ; le F051 reste dans son modèle (%s)", L.oom_why ? L.oom_why : "bloqué");
}

/* Le premier instant, à partir de `t`, où l'état `st` est publié ; 0 sinon. */
static uint64_t entered_at(uint8_t st, uint64_t t)
{
    for (size_t k = 0; k < n_chg; k++)
        if (chg[k].t >= t && chg[k].state == st) return chg[k].t;
    return 0;
}

/* La première entrée à partir de l'instant `t` : trame émise ou reçue de type `type` ; -1 sinon. */
static long nth_from(act_kind_t kind, uint8_t type, uint64_t t)
{
    for (size_t i = 0; at(i); i++)
        if (LOG[i].kind == kind && LOG[i].t >= t && LOG[i].frame.msg[0] == type) return (long)i;
    return -1;
}

/* Les trames reçues de type `type` à partir de l'instant `t`. */
static size_t rx_from(uint8_t type, uint64_t t)
{
    long i = nth_from(A_RX, type, t);
    return i < 0 ? 0 : count_rx((size_t)i, type);
}

/* Une trame reçue à partir de l'instant `t` dont le message est exactement `m`. */
static bool rx_exact_from(const uint8_t *m, size_t n, uint64_t t)
{
    long i = find_rx_exact(0, m, n);
    while (i >= 0 && LOG[i].t < t) i = find_rx_exact((size_t)i + 1, m, n);
    return i >= 0;
}

/* La coupure de RECOVERING activée (cut_rails, décision de l'humain : gardée pour la carte à rails commutés), contre le F051
 * alimenté par le rail logique (rails_power). READY à froid ; le F051 se tait (silent) : la perte 2 s après sa dernière
 * télémétrie (plus 1 µs) ; 3 s après, les rails coupés : le F051 hors tension ; 500 ms après (cut_ms), POWERING, le rail
 * logique, le F051 sous tension. L'hypothèse du scénario : le cycle d'alimentation lève son silence (unhang). Il repart à
 * froid (§ 2.1 : phase init, état libre) : son 0x01 servi (33 octets), aucun refus, l'init entière, le 10 00 (son homing, en 16384,
 * home_pos [NÉ]), READY. Sans la coupure (cut_rails faux), le même F051 resté alimenté refuserait le 0x01 en flux (§ 8,
 * reste_alimente). */
static void coupure(void)
{
    static const uint8_t st[] = {SESSION_RECOVERING, SESSION_POWERING, SESSION_IDENTIFYING, SESSION_READY};
    uint64_t tl = 0, tr, ts;
    long i01, r01;
    bench();
    P.cut_rails = true;
    bsk_session_init(&P);
    rails_power = true;
    d2(true);
    run_for(10000 * MS);
    CHECK(status().session_state == SESSION_READY && L.powered && t_pw_on > 0,
          "précondition : READY, le F051 alimenté par le rail logique (état %s)", bsk_state_name(status().session_state));
    if (status().session_state != SESSION_READY) return;
    ts = phy_sim_now();
    L.f.silent = true;
    unhang = true;
    run_for(2500 * MS);
    for (size_t i = 0; at(i); i++)
        if (LOG[i].kind == A_RX && (LOG[i].frame.msg[0] == 0x05 || LOG[i].frame.msg[0] == 0x06) && LOG[i].t > tl) tl = LOG[i].t;
    tr = entered_at(SESSION_RECOVERING, ts);
    CHECK(status().session_state == SESSION_RECOVERING && tr == tl + 2000 * MS + 1,
          "la perte : RECOVERING 2 s après la dernière télémétrie (%lld µs)", (long long)(tr - tl));
    run_for(15000 * MS);
    CHECK(t_pw_off == tr + 3000 * MS && t_pw_on == tr + 3500 * MS,
          "les rails coupés 3 s après la perte, rétablis 500 ms plus tard : le F051 hors tension 500 ms");
    CHECK(states_from(tr, st, sizeof st), "RECOVERING, POWERING, IDENTIFYING, READY, et rien d'autre");
    CHECK(entered_at(SESSION_POWERING, tr) == tr + 3500 * MS, "POWERING à la fin de la coupure");
    i01 = nth_from(A_SEND, 0x01, t_pw_on);
    r01 = i01 >= 0 ? find((size_t)i01, A_RX, 0x01) : -1;
    CHECK(r01 > i01 && LOG[r01].frame.len == 33 && !memcmp(LOG[r01].frame.msg, R01_HEAD, sizeof R01_HEAD) &&
              nth_from(A_SEND, 0x07, t_pw_on) > r01,
          "à froid : le premier 0x01 après la coupure servi, 33 octets, puis le 0x07");
    CHECK(rx_from(0x02, t_pw_on) == 0 && rx_exact_from(R10_OK, sizeof R10_OK, t_pw_on),
          "aucun refus après la coupure ; l'init entière, son 10 00");
    CHECK(status().session_state == SESSION_READY && status().position_valid && status().focus_position == 16384 &&
              status().last_error == E_OK && !ever(SESSION_FAULT),
          "READY en 16384, sans FAULT (état %s, %ld)", bsk_state_name(status().session_state), (long)status().focus_position);
    CHECK(L.phase == LSTD_FLOW && lens_clean(), "le F051 en flux, dans son modèle (%s)", L.oom_why ? L.oom_why : "bloqué");
}

static const struct {
    const char *name;
    const char *what;
    void (*fn)(void);
} SC[] = {
    {"interface_std", "l'adaptateur du F051 : ce qu'il refuse, ce qu'il passe", interface_std},
    {"demarrage_froid", "démarrage à froid : phase init, l'init, 10 00, le 0x0A, le flux, READY (LENS_CS retombée à 0 µs)",
     demarrage_froid},
    {"demarrage_froid_cs300", "le même, LENS_CS retombée 300 µs après le dernier octet", demarrage_froid_cs300},
    {"retombee_lens_cs", "LENS_CS retombée à 0 et à 300 µs : les mêmes trames, les mêmes états", retombee_lens_cs},
    {"reste_alimente", "objectif resté alimenté, en flux (§ 8) : le 0x01 refusé, le flux, READY", reste_alimente},
    {"reste_alimente_reset", "objectif resté alimenté : 0x01 refusés, reset soft (0x0A), l'init en phase init, READY",
     reste_alimente_reset},
    {"goto", "goto par 0x1D jusqu'à ARRIVED, 1D 00 dans le flux", goto_1d},
    {"goto_butee", "goto au-delà d'une butée : refusé hors des bornes du 0x06, accepté à la borne ; la butée dure (§ 4.3)", goto_butee},
    {"q", "q pendant un goto : 0x1C, 1C 01, ABORTED, aucun accusé 1D", q_1c},
    {"homing_10_01", "le homing de l'init répond 10 01 : FAULT, E_HOME_FAILED (E3)", homing_10_01},
    {"hote", "la couche HOTE de bout en bout : v, t, r, f<n>, e, f, q", hote},
    {"renvoi_init", "la réponse au 0x07 perdue, celle au 0x0D fausse : renvoyés, init complète, READY", renvoi_init},
    {"renvoi_init_3", "trois réponses au 0x09 perdues : trois envois, puis la boucle sans le reste de l'init",
     renvoi_init_3},
    {"renvoi_jamais", "le 0x0A et le 0x10 de l'init, leur réponse perdue : jamais renvoyés", renvoi_jamais},
    {"renvoi_goto", "le 0x1D perdu : renvoyé à 1 s, arrivée ; reçu : jamais ; trois perdus : STALLED à 3 s",
     renvoi_goto},
    {"renvoi_arret", "q : le 0x1C perdu, renvoyé à 1,5 s ; trois perdus, l'abandon ; reçu, jamais renvoyé",
     renvoi_arret},
    {"renvoi_arret_goto", "un goto admis pendant la surveillance de l'arrêt : aucun 0x1C renvoyé", renvoi_arret_goto},
    {"renvoi_arret_restoring", "q en RESTORING, le 0x1C perdu : renvoyé à 1,5 s, en READY", renvoi_arret_restoring},
    {"restauration", "RESTORING : la marque préchargée, 20063 puis 20000 (X = 1 % de la course), READY sur elle",
     restauration},
    {"muet_fault", "un F051 muet : 0x01, reset soft, 0x01, échec compté ; FAULT, E_LOST au quatrième", muet_fault},
    {"muet_puis_repond", "le premier 0x01 perdu, le second répondu : l'init sans reset soft, READY", muet_puis_repond},
    {"log_all_minute", "LOG ALL, une minute à 60 Hz : chaque trame au journal et reconstruite, rien de perdu, le débit",
     log_all_minute},
    {"coupure", "cut_rails : la perte, les rails coupés 500 ms, le F051 repart à froid, READY", coupure},
};
#define N_SC (sizeof SC / sizeof SC[0])

int main(int argc, char **argv)
{
    bool passed[N_SC];
    int wrong = 0;
    if (argc != 2) {
        fprintf(stderr, "usage : test_std <liste des constats (sim/test/constats_std.txt)>\n");
        return 2;
    }
    signal(SIGALRM, too_long);
    alarm(20);
    load_constats(argv[1]);
    for (size_t i = 0; i < N_SC; i++) {
        long k = constat(SC[i].name);
        sc_listed = k >= 0;
        sc_fails = 0;
        printf("%s%s\n", SC[i].what, sc_listed ? " [constat]" : "");
        SC[i].fn();
        passed[i] = sc_fails == 0;
        if (k >= 0) K[k].used = true;
        if (!sc_listed) fails += sc_fails;
    }
    printf("constats (sim/test/constats_std.txt) : %zu\n", n_k);
    for (size_t k = 0; k < n_k; k++) printf("  CONSTAT %s : %s (%s)\n", K[k].name, K[k].why, K[k].cite);
    for (size_t i = 0; i < N_SC; i++) {
        long k = constat(SC[i].name);
        if (k >= 0 && passed[i]) {
            printf("  ECHEC : le constat « %s » est réglé (le scénario passe) : il doit sortir de la liste\n", SC[i].name);
            wrong++;
        }
    }
    for (size_t k = 0; k < n_k; k++)
        if (!K[k].used) {
            printf("  ECHEC : le constat « %s » ne nomme aucun scénario\n", K[k].name);
            wrong++;
        }
    printf("faux F051 : %d vérifications, %d échec(s) hors constats, %zu scénario(s) en constat", checks, fails, n_k);
    printf(", %d erreur(s) de la liste\n", wrong + list_faults);
    return fails || wrong || list_faults ? 1 : 0;
}
