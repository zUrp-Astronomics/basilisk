/* SOURCE: spec § 4.5 (atelier), la table de traduction ; PROTOCOL.md pour la forme ; pour Moonlite, spec § 4.5.4
 * AUTHOR: engineer
 * DATE: 2026-09-27
 * STATUS: actif — joué par 4_Firmware/run.sh (job `firmware`), arguments : la page de banc, puis le CMakeLists.txt du firmware
 *
 * La couche HOTE (la vraie traduction, components/host/host.c, et le vrai journal) contre une session scriptée :
 * chaque ligne suivie jusqu'à ce qu'elle dépose et à ce qu'elle rend, dans chaque état. Les motifs de la page
 * (expectFor) sont lus dans la page de banc (premier argument), pas recopiés : t_page les applique à chaque réponse.
 *
 * Ce fichier implémente bsk_session_status, bsk_session_command et bsk_session_ack, et ce que `DEBUG` lit : le compteur de
 * refus de bench_core (bsk_bench_refused, la valeur que le test pose : REFUSED), les compteurs du temps réel
 * (bsk_session_debug, ceux que le test pose : DBG) et les marges de pile des tâches (bsk_host_stacks, celles que le test
 * pose : STACKS). L'instantané est celui que le
 * test pose (ST) ; une commande déposée est notée (CMDS) et reçoit l'accusé que le test a prévu (RULE, comme la
 * session : ACK_ACCEPTED, et ACK_COMPLETED tout de suite pour ce qui finit dans l'appel) ; les accusés finaux
 * et les changements d'état d'une session qui avance sont posés par le test (push, ST), puis lus par
 * bsk_host_observe. */
#define _POSIX_C_SOURCE 200809L   /* alarm, la garde de durée */

#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bsk_bench_core.h"
#include "bsk_contract.h"
#include "bsk_host.h"
#include "bsk_journal.h"
#include "bsk_led.h"
#include "bsk_session.h"

static int checks, fails;

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

/* ─────────────────────────── la session scriptée ─────────────────────────── */

static bsk_status_t ST;
static bsk_cmd_t CMDS[64];
static size_t n_cmds;
static size_t n_ring_set;   /* les CMD_RING_SET déposées, toutes, au-delà de CMDS aussi */
static bsk_ack_t Q[32];
static size_t q_head, q_n;

/* Ce que reçoit la prochaine commande : accept (et, pour ATTACH, DETACH, STOP, CLEAR_FAULT, ACK_COMPLETED
 * tout de suite, comme la session), ou le refus `why`. Remis à « accepter » après chaque dépôt. */
static bsk_err_t RULE;
/* Accusés que la session ajoute pendant un dépôt, après le premier (l'accusé du goto que q arrête). */
static bsk_ack_t EXTRA[4];
static size_t n_extra;
/* La fin de la prochaine CMD_FOCUS_STOP acceptée, ACK_FAILED avec cette raison (le 0x1C pas émis) ; E_OK :
 * ACK_COMPLETED. Remise à E_OK après chaque dépôt. */
static bsk_err_t STOP_END;

static void push(uint32_t seq, bsk_ack_result_t r, bsk_err_t why)
{
    if (q_n < sizeof Q / sizeof Q[0]) Q[(q_head + q_n++) % (sizeof Q / sizeof Q[0])] = (bsk_ack_t){seq, r, why};
}

void bsk_session_status(bsk_status_t *st) { *st = ST; }

static uint32_t REFUSED;
uint32_t bsk_bench_refused(void) { return REFUSED; }

static bsk_session_debug_t DBG;
void bsk_session_debug(bsk_session_debug_t *d) { *d = DBG; }
static bsk_host_stacks_t STACKS;
void bsk_host_stacks(bsk_host_stacks_t *s) { *s = STACKS; }

void bsk_session_command(const bsk_cmd_t *c)
{
    if (n_cmds < sizeof CMDS / sizeof CMDS[0]) CMDS[n_cmds] = *c;
    n_cmds++;
    n_ring_set += c->op == CMD_RING_SET;
    if (RULE != E_OK) {
        push(c->seq, ACK_REJECTED, RULE);
    } else {
        push(c->seq, ACK_ACCEPTED, E_OK);
        for (size_t i = 0; i < n_extra; i++) push(EXTRA[i].seq, EXTRA[i].result, EXTRA[i].reason);
        if (c->op == CMD_FOCUS_STOP && STOP_END != E_OK) push(c->seq, ACK_FAILED, STOP_END);
        else if (c->op == CMD_ATTACH || c->op == CMD_DETACH || c->op == CMD_FOCUS_STOP || c->op == CMD_CLEAR_FAULT)
            push(c->seq, ACK_COMPLETED, E_OK);
    }
    RULE = E_OK;
    STOP_END = E_OK;
    n_extra = 0;
}

/* Les données de la réponse de l'objectif à CMD_LENS_CUSTOM, posées par le test. */
static uint8_t CDATA[BSK_CUSTOM_DATA];
void bsk_session_custom_data(uint8_t data[BSK_CUSTOM_DATA]) { memcpy(data, CDATA, sizeof CDATA); }

bool bsk_session_ack(bsk_ack_t *a)
{
    if (!q_n) return false;
    *a = Q[q_head];
    q_head = (q_head + 1) % (sizeof Q / sizeof Q[0]);
    q_n--;
    return true;
}

/* ─────────────────────────── le banc ─────────────────────────── */

static char R[BSK_HOST_REPLY_MAX];
static bool replied;

/* La version que la carte donne à bsk_host_init (PROJECT_VER, par esp_app_get_description), lue dans
 * CMakeLists.txt (second argument), sa seule source ; `v` la rend. L'attendu, lui, est écrit à la main : V. */
static char VERSION[40];
#define V "1.0"

static bool load_version(const char *path)
{
    char buf[400];
    const char *key = "set(PROJECT_VER \"";
    FILE *f = fopen(path, "r");
    if (!f) return false;
    while (fgets(buf, sizeof buf, f)) {
        char *p = strstr(buf, key), *e;
        if (p != buf) continue;
        p += strlen(key);
        e = strchr(p, '"');
        if (e && e - p < (long)sizeof VERSION) snprintf(VERSION, sizeof VERSION, "%.*s", (int)(e - p), p);
    }
    fclose(f);
    return VERSION[0] != 0;
}

/* Une ligne des lettres que le driver de série envoie, `f r e a d s c` et `m<n>` (PROTOCOL.md § 1, règle 4). */
static bool serial_letter(const char *line)
{
    return (line[0] && strchr("freadsc", line[0])) || (line[0] == 'm' && line[1] >= '0' && line[1] <= '9');
}

/* Une ligne reçue ; sa réponse dans R. Toute réponse : une seule ligne, que le transport termine par le `\n` que HOTE
 * lui rend ; toute erreur : sans « ok » ; la réponse à une lettre du driver de série, 15 caractères au plus
 * (PROTOCOL.md § 1) : vérifiée ici, à chaque ligne que le test envoie, donc dans chaque état qu'il pose. */
static const char *ask(const char *line)
{
    const char *tail = bsk_host_line(line, R, sizeof R);
    replied = tail != NULL;
    CHECK(tail && !strcmp(tail, "\n"), "« %s » : la réponse suivie de \\n (« %s »)", line, tail ? tail : "(rien)");
    CHECK(!strchr(R, '\n') && !strchr(R, '\r'), "« %s » : une seule ligne", line);
    CHECK(strncmp(R, "er", 2) || !strstr(R, "ok"), "« %s » -> « %s » : une erreur sans « ok »", line, R);
    CHECK(!serial_letter(line) || strlen(R) <= 15, "« %s » -> « %s » : 15 caractères au plus (%zu)", line, R, strlen(R));
    return R;
}

static bool is(const char *line, const char *want)
{
    ask(line);
    return replied && !strcmp(R, want);
}

#define ASK(line, want) CHECK(is(line, want), "« %s » -> « %s », attendu « %s »", line, R, want)

static void state(uint8_t s) { ST.session_state = s; }

/* La carte au démarrage, puis une session en READY : le 135, des bornes scriptées (13808 / 30946), au repos à 20000. */
static void fresh(void)
{
    memset(&ST, 0, sizeof ST);
    n_cmds = 0;
    q_n = 0;
    RULE = E_OK;
    STOP_END = E_OK;
    n_extra = 0;
    bsk_journal_set(false, false);
    bsk_host_init(VERSION, "poweron");
}

static void ready135(void)
{
    fresh();
    state(SESSION_READY);
    ST.motion_state = MOTION_IDLE;
    ST.position_valid = true;
    ST.focus_position = 20000;
    ST.focus_min = 13808;
    ST.focus_max = 30946;
    ST.lens_id_product = 8;
    snprintf(ST.lens_name, sizeof ST.lens_name, "SAMYANG AF 135mm F1.8");
    bsk_host_observe();
}

/* L'ouverture que publie le 135, en codes 256(Av+16) : sa plage, réponse au 0x08 des traces, octets 1-4,
 * BF 11 / 00 19 (sy135-2026-09-20-full.txt:15), f/1,83 et f/22,6, soit 1.8-22 au tiers usuel ;
 * relue au 0x05 du flux, offsets 0-1, B2 11 (dump05:7). */
static void aperture135(void)
{
    ST.aperture_min = 0x11BF;
    ST.aperture_max = 0x1900;
    ST.aperture_current = 0x11B2;
    ST.capabilities = CAP_APERTURE | CAP_APERTURE_READBACK;
}

static const bsk_cmd_t *last_cmd(void) { return n_cmds ? &CMDS[(n_cmds - 1) % (sizeof CMDS / sizeof CMDS[0])] : NULL; }

static bool deposited(size_t n0, bsk_cmd_op_t op, int32_t arg)
{
    const bsk_cmd_t *c = last_cmd();
    return n_cmds == n0 + 1 && c && c->op == op && c->arg == arg;
}

/* La valeur d'une clé de la réponse à `line` (`t`, `DEBUG` ; copiée dans v), NULL si elle manque. */
static const char *key_of(const char *line, const char *key)
{
    static char v[200];
    char pat[40];
    const char *p, *e;
    ask(line);
    snprintf(pat, sizeof pat, " %s=", key);
    p = !strncmp(R, pat + 1, strlen(pat + 1)) ? R - 1 : strstr(R, pat);
    if (!p) return NULL;
    p += strlen(pat);
    e = *p == '"' ? strchr(p + 1, '"') + 1 : p + strcspn(p, " ");
    snprintf(v, sizeof v, "%.*s", (int)(e - p), p);
    return v;
}

static const char *tkey(const char *key) { return key_of("t", key); }

#define TKEY(key, want) CHECK(tkey(key) && !strcmp(tkey(key), want), "t : %s=%s, attendu %s", key, tkey(key) ? tkey(key) : "(absente)", want)
/* Une clé de `DEBUG`. */
#define DKEY(key, want)                                                                                                     \
    CHECK(key_of("DEBUG", key) && !strcmp(key_of("DEBUG", key), want), "DEBUG : %s=%s, attendu %s", key,                   \
          key_of("DEBUG", key) ? key_of("DEBUG", key) : "(absente)", want)

/* ─────────────────────────── READY, lettre par lettre (§ 4.5.2) ─────────────────────────── */

static void t_ready_reads(void)
{
    printf("READY, les lectures : v f r e d a o l i n w j s, et c, sans rien déposer\n");
    ready135();
    ASK("v", V);
    ASK("f", "20000");
    ASK("r", "13808-30946");
    ASK("e", "n");
    ASK("d", "-");
    ASK("a", "er nocap ap");
    ASK("a5.6", "er nocap ap");
    ASK("a+1", "er nocap ap");
    ASK("o", "er nocap ap");
    ASK("l", "er nocap focal");
    ASK("i", "SAMYANG AF 135mm F1.8");
    ASK("n", "er nocap");
    ASK("w", "er nocap");
    ASK("j", "-");
    ASK("s3", "ok");
    ASK("p", "er nocap");
    ASK("pl0", "er nocap");
    ASK("pl1", "er nocap");
    ASK("pm0", "er nocap");
    ASK("pm1", "er nocap");
    ASK("x", "er nocap");
    ASK("h", "er nocap");                           /* le homing au démarrage seulement */
    ASK("c", "ok");                                 /* rien déposé (le compte ci-dessous), puis e et r */
    ASK("e", "n");
    ASK("r", "13808-30946");
    for (const char *const *l = (const char *const[]){"vv", "tx", "ex", "rx", "dx", "cx", "ox", "lx", "ix", "nx", "wx", "hx", "qx",
                                                       "bx", "jz", "j1", "jss", "jgx", NULL};
         *l; l++)
        ASK(*l, "er nocap");                        /* une ligne est reconnue exactement */
    ASK("  v", V);
    CHECK(n_cmds == 0, "aucune commande déposée (%zu)", n_cmds);
    ST.motion_state = MOTION_COMMANDED;
    ASK("e", "y");
    ST.motion_state = MOTION_MOVING;
    ASK("e", "y");
    ST.motion_state = MOTION_SETTLING;
    ASK("e", "y");
    ST.motion_state = MOTION_PLANNING;
    ASK("e", "y");
    ST.motion_state = MOTION_ARRIVED;
    ASK("e", "n");
    ST.motion_state = MOTION_STALLED;
    ASK("e", "n");
    ST.motion_state = MOTION_ABORTED;
    ASK("e", "n");
    ST.mark_valid = true;
    ST.mark_position = 16340;
    ASK("j", "16340");
    ST.position_valid = false;
    ASK("r", "er link limits");   /* aucun 0x06 lu : ni position ni bornes, PROTOCOL.md § 2 */
}

static void t_ready_commands(void)
{
    size_t n0;
    printf("READY, les commandes : ce qui est déposé, ce qui est rendu\n");
    ready135();
    n0 = n_cmds; ASK("f25000", "ok");
    CHECK(deposited(n0, CMD_FOCUS_GOTO, 25000), "f25000 : CMD_FOCUS_GOTO 25000");
    n0 = n_cmds; ASK("f0", "ok");
    CHECK(deposited(n0, CMD_FOCUS_GOTO, 0), "f0 : CMD_FOCUS_GOTO 0");
    n0 = n_cmds; ASK("f65535", "ok");
    CHECK(deposited(n0, CMD_FOCUS_GOTO, 65535), "f65535 : CMD_FOCUS_GOTO 65535");
    n0 = n_cmds; ASK("f+100", "ok");
    CHECK(deposited(n0, CMD_FOCUS_MOVE, 100), "f+100 : CMD_FOCUS_MOVE +100");
    n0 = n_cmds; ASK("f-100", "ok");
    CHECK(deposited(n0, CMD_FOCUS_MOVE, -100), "f-100 : CMD_FOCUS_MOVE -100");
    n0 = n_cmds; ASK("f-20000", "ok");
    CHECK(deposited(n0, CMD_FOCUS_MOVE, -20000), "f-20000 depuis 20000 : 0, déposé, la session juge les bornes");
    n0 = n_cmds; ASK("f+45535", "ok");
    CHECK(deposited(n0, CMD_FOCUS_MOVE, 45535), "f+45535 depuis 20000 : 65535, déposé");
    n0 = n_cmds; ASK("q", "ok");
    CHECK(deposited(n0, CMD_FOCUS_STOP, 0), "q : CMD_FOCUS_STOP");
    n0 = n_cmds; ASK("p1", "ok");
    CHECK(deposited(n0, CMD_ATTACH, 0), "p1 : CMD_ATTACH");
    n0 = n_cmds; ASK("b", "ok");
    CHECK(deposited(n0, CMD_ATTACH, 0), "b : CMD_ATTACH");
    n0 = n_cmds; ASK("p0", "ok");
    CHECK(deposited(n0, CMD_DETACH, 0), "p0 : CMD_DETACH");
    for (size_t i = 1; i < n_cmds; i++) CHECK(CMDS[i].seq > CMDS[i - 1].seq, "seq strictement croissant (§ 4.1)");
    state(SESSION_READY);
    n0 = n_cmds; ASK("js", "ok");
    CHECK(deposited(n0, CMD_SET_MARK, BSK_MARK_HERE), "js : CMD_SET_MARK BSK_MARK_HERE");
    n0 = n_cmds; ASK("jx", "ok");
    CHECK(deposited(n0, CMD_CLEAR_MARK, 0), "jx : CMD_CLEAR_MARK");
    RULE = E_NOCAP; n0 = n_cmds; ASK("js", "er nocap");
    CHECK(deposited(n0, CMD_SET_MARK, BSK_MARK_HERE), "js, sans identité : E_NOCAP -> er nocap (M6)");
    RULE = E_LIMIT; ASK("js", "er range limits");    /* hors des bornes du 0x06 */
    RULE = E_OK;
    n0 = n_cmds; ASK("jg", "er range nomark");
    CHECK(n_cmds == n0, "jg sans marque : rien déposé (§ 4.5.1)");
    ST.mark_valid = true;
    ST.mark_position = 16340;
    n0 = n_cmds; ASK("jg", "ok");
    CHECK(deposited(n0, CMD_FOCUS_GOTO, 16340), "jg : CMD_FOCUS_GOTO vers la marque, tel quel, sans recette d'approche");

    /* m<n>, le Move du driver ASCOM, est f<n> : la même commande, le même ok. */
    n0 = n_cmds; ASK("m25000", "ok");
    CHECK(deposited(n0, CMD_FOCUS_GOTO, 25000), "m25000 : CMD_FOCUS_GOTO 25000");
    n0 = n_cmds; ASK("m0", "ok");
    CHECK(deposited(n0, CMD_FOCUS_GOTO, 0), "m0 : CMD_FOCUS_GOTO 0 (Math.Max(0, Position))");
    n0 = n_cmds; ASK("m65535", "ok");
    CHECK(deposited(n0, CMD_FOCUS_GOTO, 65535), "m65535 : CMD_FOCUS_GOTO 65535");
    n0 = n_cmds; ASK("m007", "ok");
    CHECK(deposited(n0, CMD_FOCUS_GOTO, 7), "m007 : CMD_FOCUS_GOTO 7, comme f007");
    ST.mark_valid = true;
    ST.mark_position = 16340;
    n0 = n_cmds; ASK("m16340", "ok");
    CHECK(deposited(n0, CMD_FOCUS_GOTO, 16340), "m16340 : un goto, plus la marque posée à 16340 (CMD_SET_MARK)");
}

static void t_text(void)
{
    size_t n0;
    printf("ce que la couche HOTE juge elle-même : le texte (er range num, er range 65535), sans rien déposer\n");
    ready135();
    n0 = n_cmds;
    ASK("f12x", "er range num");
    ASK("fx", "er range num");
    ASK("f+", "er range num");
    ASK("f-", "er range num");
    ASK("f+1a", "er range num");
    ASK("f 100", "er range num");
    ASK("f65536", "er range 65535");
    ASK("f99999999999999999999", "er range 65535");
    ASK("f4294967296", "er range 65535");           /* 2^32 : lu sans déborder */
    ASK("f+4294967296", "er range 65535");
    ASK("f+65536", "er range 65535");
    ASK("f-65536", "er range 65535");
    ASK("f+45536", "er range 65535");               /* depuis 20000 : 65536 (PROTOCOL.md § 2) */
    ASK("f-20001", "er range 65535");               /* -1 */
    ASK("f-65535", "er range 65535");
    ASK("m65536", "er range 65535");                 /* m<n> comme f<n> */
    ASK("m99999999999999999999", "er range 65535");
    ASK("m12x", "er range num");
    ASK("m1 ", "er range num");
    CHECK(n_cmds == n0, "rien déposé (%zu)", n_cmds - n0);
    ST.position_valid = false;                       /* sans 0x06, position inconnue (publiée 0) : la session juge */
    ST.focus_position = 0;
    RULE = E_LIMIT;
    ASK("f-100", "er range limits");
    CHECK(deposited(n0, CMD_FOCUS_MOVE, -100), "sans 0x06, f-100 : déposé, E_LIMIT de la session");
}

static void t_refusals(void)
{
    printf("READY, les refus du contrat : E_BUSY, E_LIMIT, E_NOCAP, E_FORBIDDEN, E_TIMEOUT ; q en HOMING\n");
    ready135();
    RULE = E_BUSY; ASK("f20000", "er busy");        /* une commande en vol jusqu'à son accusé final */
    RULE = E_BUSY; ASK("f+10", "er busy");
    RULE = E_BUSY; ASK("p0", "er busy");
    RULE = E_BUSY; ASK("p1", "er busy");
    RULE = E_BUSY; ASK("js", "er busy");
    RULE = E_BUSY; ASK("m20000", "er busy");       /* comme f<n> */
    RULE = E_BUSY; ASK("b", "er busy move");       /* b pendant un mouvement */
    RULE = E_LIMIT; ASK("f+40000", "er range limits");  /* hors des bornes du 0x06 (§ 4.5.1) */
    RULE = E_LIMIT; ASK("f-10000", "er range limits");  /* 10000 : dans 0..65535, hors des bornes */
    RULE = E_LIMIT; ASK("f100", "er range limits");
    RULE = E_LIMIT; ASK("m100", "er range limits");
    RULE = E_NOCAP; ASK("js", "er nocap");
    RULE = E_NOCAP; ASK("m100", "er nocap");
    RULE = E_FORBIDDEN; ASK("f100", "er refused");
    RULE = E_TIMEOUT; ASK("f100", "er link timeout");
    RULE = E_BUS; ASK("f100", "er link bus");
    RULE = E_BUSY; ASK("q", "er busy home");        /* la session ne refuse q que pendant un homing, qu'il n'arrête pas */
    STOP_END = E_BUS; ASK("q", "er link bus");       /* accepté, le 0x1C pas émis */
    ASK("q", "ok");                                  /* puis émis : ok */
}

/* ─────────────────────────── l'ouverture ───────────────────────────
 * Les codes attendus sont calculés à la main, code = round(256 × (2·log2(N) + 16)) : f/5,6 -> 5369
 * (0x14F9), f/2,8 -> 4857 (0x12F9), f/1,8 -> 4530 (0x11B2), f/22 -> 6379 (0x18EB), f/2,2 -> 4678 (0x1246), f/2 -> 4608
 * (0x1200) ; f/0,5, le
 * plancher de la conversion -> 3584 (0x0E00). Et dans l'autre sens, N = 2^((code/256 - 16)/2), arrondi au tiers usuel
 * s'il en est à moins de 0,06 en log : 0x11BF -> 1,83 -> 1.8, 0x1900 -> 22,6 -> 22, 0x1405 -> 4,03 -> 4.0
 * (le Sony FE 24-105 G, capture 17-45-01-034Z.txt:65-66 et :26-30), 0x1A00 -> 32 -> 32, 0x14F0 -> 5,57 -> 5.6, 0x172C ->
 * 12,01 -> 12.0 (hors d'un demi-tiers d'un cran usuel). */

static void t_aperture(void)
{
    size_t n0;
    printf("ouverture : a (la plage), a<f>, a+x, a-x (la consigne, un code), o (relue, bornée à la plage)\n");
    ready135();
    aperture135();
    ASK("a", "1.8-22");
    ASK("o", "1.8");
    n0 = n_cmds; ASK("a5.6", "ok");
    CHECK(deposited(n0, CMD_APERTURE_SET, 0x14F9), "a5.6 : CMD_APERTURE_SET 0x14F9 (%ld)", last_cmd() ? (long)last_cmd()->arg : -1L);
    n0 = n_cmds; ASK("a+1", "ok");
    CHECK(deposited(n0, CMD_APERTURE_SET, 0x12F9), "a+1 depuis f/1,8 relue : f/2,8, 0x12F9 (%ld)", last_cmd() ? (long)last_cmd()->arg : -1L);
    ST.aperture_current = 0x14F0;
    ASK("o", "5.6");
    n0 = n_cmds; ASK("a-3.4", "ok");
    CHECK(deposited(n0, CMD_APERTURE_SET, 0x1246), "a-3.4 depuis f/5,6 relue : f/2,2, 0x1246 (%ld)", last_cmd() ? (long)last_cmd()->arg : -1L);
    n0 = n_cmds; ASK("a22", "ok");
    CHECK(deposited(n0, CMD_APERTURE_SET, 0x18EB), "a22 : 0x18EB");
    n0 = n_cmds; ASK("a22.04", "ok");                /* la plage ± 0,05 */
    CHECK(deposited(n0, CMD_APERTURE_SET, 0x18EB), "a22.04 : f/ x10 220, 0x18EB");
    n0 = n_cmds; ASK("a1.76", "ok");
    CHECK(deposited(n0, CMD_APERTURE_SET, 0x11B2), "a1.76 : f/ x10 18, 0x11B2 ; la session la borne à la plage");
    n0 = n_cmds; ASK("a.2e1", "ok");                 /* strtod : 2 ; un nombre qui commence par un point */
    CHECK(deposited(n0, CMD_APERTURE_SET, 0x1200), "a.2e1 : f/2, 0x1200 (%ld)", last_cmd() ? (long)last_cmd()->arg : -1L);
    ST.aperture_current = 0x172C;                    /* f/12,01 : à 0,08 (en log) de f/11 et de f/13, au-delà d'un
                                                        demi-tiers : la valeur exacte */
    ASK("o", "12.0");
    ST.aperture_current = 0x1000;                    /* relue sous la plage, f/1,0 : bornée au minimum */
    ASK("o", "1.8");
    ST.aperture_current = 0x1A00;                    /* au-dessus : au maximum */
    ASK("o", "22.0");
    ST.aperture_current = 0x1405;
    ST.aperture_min = 0x1405;                        /* le Sony FE 24-105 G : f/4, f/22 */
    ASK("a", "4.0-22");
    ASK("o", "4.0");

    ready135();
    aperture135();
    ST.capabilities = CAP_APERTURE;                  /* consigne sans relecture */
    ASK("a", "1.8-22");
    ASK("o", "er nocap ap");
    ST.capabilities = CAP_APERTURE_READBACK;         /* sans CAP_APERTURE : pas de plage */
    ASK("a", "er nocap ap");
    ASK("o", "er nocap ap");
    ST.capabilities = CAP_APERTURE | CAP_APERTURE_READBACK;
    RULE = E_BUSY; ASK("a5.6", "er busy");          /* un goto en vol */
    RULE = E_NOCAP; ASK("a5.6", "er nocap");
    CHECK(n_cmds == 2, "les refus du contrat : déposés (%zu)", n_cmds);

    state(SESSION_FAULT);                            /* FAULT, identité établie : o relit ce que l'objectif a publié */
    ASK("o", "1.8");
    ASK("a", "er fault");
}

/* L'ouverture inconnue (L03-02, L07-03). Le domaine des codes que la carte convertit, 0x0E00 (f/0,5, Av -2) à 0x2000
 * (f/256, Av 16), bornes comprises (bsk_contract.h) : un code hors de lui n'est jamais converti. Une ouverture relue
 * hors du domaine — 0, la session sans 0x05 ou son flux arrêté ; 0x0DFF ; 0x2001 ; 0xFFFF — est inconnue : `o` rend
 * `er link aperture`, jamais une valeur ramenée à la plage ; `a+x` et `a-x`, qui partent d'elle, `er nocap ap`
 * (arbitrage de l'architecte), rien déposé ; `a`, `a<f>` absolu ne la lisent pas. Une plage dont un code est hors du
 * domaine est inconnue : `a`, `a<f>`, `o` -> `er nocap ap`. Aux bornes du domaine, la conversion, à la main :
 * 0x0E00 -> f/0,5, 5 ; 0x2000 -> f/256, 2560. */
static void t_aperture_unknown(void)
{
    size_t n0;
    printf("ouverture inconnue : o -> er link aperture, a±x -> er nocap ap ; une plage hors du domaine, inconnue\n");
    ready135();
    aperture135();
    for (const uint16_t *c = (const uint16_t[]){0, 0x0DFF, 0x2001, 0xFFFF, 1}; *c != 1; c++) {
        ST.aperture_current = *c;
        n0 = n_cmds;
        CHECK(is("o", "er link aperture"), "relue 0x%04X : o -> « %s », attendu er link aperture", *c, R);
        CHECK(is("a+1", "er nocap ap"), "relue 0x%04X : a+1 -> « %s », attendu er nocap ap", *c, R);
        CHECK(is("a-0.3", "er nocap ap"), "relue 0x%04X : a-0.3 -> « %s »", *c, R);
        CHECK(n_cmds == n0, "relue 0x%04X : rien déposé", *c);
        ASK("a", "1.8-22");
        n0 = n_cmds; ASK("a5.6", "ok");
        CHECK(deposited(n0, CMD_APERTURE_SET, 0x14F9), "a5.6 absolu : déposé, 0x14F9");
    }
    ASK("a+abc", "er range num");                    /* le texte d'abord */
    state(SESSION_FAULT);
    ASK("o", "er link aperture");                    /* FAULT, identité établie : la même lecture */
    state(SESSION_READY);

    ST.aperture_current = 0x0E00;                    /* les bornes du domaine : connues */
    ST.aperture_min = 0x0E00;
    ST.aperture_max = 0x2000;
    ASK("a", "0.5-256");
    n0 = n_cmds;
    ASK("a300", "er range ap");                      /* S2 : la plage n'y est pas (« er range 0.5 256 », 16 caractères) */
    CHECK(n_cmds == n0, "a300 hors de la plage : rien déposé");
    ASK("o", "0.5");
    ST.aperture_current = 0x2000;
    ASK("o", "256.0");
    for (const uint16_t *r = (const uint16_t[]){0, 0x11BF, 0x0DFF, 0x1900, 0x11BF, 0x2001, 0x11BF, 0xFFFF, 1}; *r != 1; r += 2) {
        ST.aperture_min = r[0];
        ST.aperture_max = r[1];
        ST.aperture_current = 0x11B2;
        n0 = n_cmds;
        CHECK(is("a", "er nocap ap"), "plage 0x%04X-0x%04X : a -> « %s », attendu er nocap ap", r[0], r[1], R);
        CHECK(is("a5.6", "er nocap ap") && is("o", "er nocap ap") && n_cmds == n0,
              "plage 0x%04X-0x%04X : a5.6, o -> er nocap ap, rien déposé", r[0], r[1]);
    }
}

static void t_aperture_text(void)
{
    size_t n0;
    printf("ouverture : ce que la couche HOTE juge elle-même (er range num, er range ap), sans rien déposer\n");
    ready135();
    aperture135();
    n0 = n_cmds;
    for (const char *const *l = (const char *const[]){"ax", "a 5.6", "a\t5.6", "a5.6x", "a5,6", "a+", "a-", "a.", "anan", "ainf",
                                                       "a+inf", "a-inf", "a+nan", "a1e999", NULL};
         *l; l++)
        ASK(*l, "er range num");
    ASK("a22.06", "er range ap");
    ASK("a1.74", "er range ap");
    ASK("a+20.3", "er range ap");                /* 1,8 + 20,3 = 22,1 */
    ASK("a-0.1", "er range ap");
    ASK("a0", "er range ap");
    ASK("a100", "er range ap");
    CHECK(n_cmds == n0, "rien déposé (%zu)", n_cmds - n0);
}

/* ─────────────────────────── hors READY (§ 4.5.1, tableau d'état) ─────────────────────────── */

/* Réponse attendue par colonne : OFF, états de séquence, FAULT identité établie, FAULT sans identité. NULL : la
 * lettre est jouée ailleurs. */
static const struct {
    const char *line, *off, *seq, *fault_id, *fault_noid;
} COLS[] = {
    {"f", "nc", "er busy boot", "er fault", "er fault"},
    {"f100", "nc", "er busy boot", "er fault", "er fault"},
    {"f+1", "nc", "er busy boot", "er fault", "er fault"},
    {"f-1", "nc", "er busy boot", "er fault", "er fault"},
    {"r", "nc", "er busy boot", "er fault", "er fault"},
    {"d", "nc", "er busy boot", "er fault", "er fault"},
    {"a", "nc", "er busy boot", "er fault", "er fault"},
    {"a5.6", "nc", "er busy boot", "er fault", "er fault"},
    {"c", "nc", "er busy boot", "er fault", "er fault"},   /* comme f<n> */
    {"e", "n", "y", "n", "n"},
    {"h", "er nocap", "er nocap", "er nocap", "er nocap"},
    {"m100", "nc", "er busy boot", "er fault", "er fault"},   /* comme f100 */
    {"m100x", "nc", "er busy boot", "er fault", "er fault"},
    {"js", "er nolens", "er busy boot", "er fault", "er fault"},
    {"jx", "er nolens", "er busy boot", "er fault", "er fault"},
    {"jg", "er nolens", "er busy boot", "er fault", "er fault"},
    {"o", "er nolens", "er busy boot", "er nocap ap", "er nolens"},
    {"i", "er nolens", "er busy boot", "SAMYANG AF 135mm F1.8", "er nolens"},
    {"j", "er nolens", "er busy boot", "-", "er nolens"},
    {"jz", "er nocap", "er nocap", "er nocap", "er nocap"},
    {"m", "er nocap", "er nocap", "er nocap", "er nocap"},       /* des lignes inconnues */
    {"ms", "er nocap", "er nocap", "er nocap", "er nocap"},
    {"mg", "er nocap", "er nocap", "er nocap", "er nocap"},
    {"mx", "er nocap", "er nocap", "er nocap", "er nocap"},
    {"m+1", "er nocap", "er nocap", "er nocap", "er nocap"},
    {"v", V, V, V, V},
    {"DEBUG", "ok reset=poweron dropped=0 move_rc=- refused=0 evq_drop=0 ev_back=0 ev_back_us=0 "
     "tx_wait_us=0 tx_timeout=0 pair_late=0 pair_late_us=0 stack_session=0 stack_phy=0 stack_usb_rx=0 stack_store=0 "
     "stack_journal=0",
     "ok reset=poweron dropped=0 move_rc=- refused=0 evq_drop=0 ev_back=0 ev_back_us=0 "
     "tx_wait_us=0 tx_timeout=0 pair_late=0 pair_late_us=0 stack_session=0 stack_phy=0 stack_usb_rx=0 stack_store=0 "
     "stack_journal=0",
     "ok reset=poweron dropped=0 move_rc=- refused=0 evq_drop=0 ev_back=0 ev_back_us=0 "
     "tx_wait_us=0 tx_timeout=0 pair_late=0 pair_late_us=0 stack_session=0 stack_phy=0 stack_usb_rx=0 stack_store=0 "
     "stack_journal=0",
     "ok reset=poweron dropped=0 move_rc=- refused=0 evq_drop=0 ev_back=0 ev_back_us=0 "
     "tx_wait_us=0 tx_timeout=0 pair_late=0 pair_late_us=0 stack_session=0 stack_phy=0 stack_usb_rx=0 stack_store=0 "
     "stack_journal=0"},
    {"s3", "ok", "ok", "ok", "ok"},
    {"cm", "er nocap", "er nocap", "er nocap", "er nocap"},
    {"l", "er nocap focal", "er nocap focal", "er nocap focal", "er nocap focal"},
    {"n", "er nocap", "er nocap", "er nocap", "er nocap"},
    {"w", "er nocap", "er nocap", "er nocap", "er nocap"},
    {"u", "er nocap", "er nocap", "er nocap", "er nocap"},
    {"ux", "er nocap", "er nocap", "er nocap", "er nocap"},
    {"uf", "er nocap", "er nocap", "er nocap", "er nocap"},
    {"ua", "er nocap", "er nocap", "er nocap", "er nocap"},
    {"p", "er nocap", "er nocap", "er nocap", "er nocap"},
    {"pm1", "er nocap", "er nocap", "er nocap", "er nocap"},
    {"g", "-", "-", "-", "-"},                       /* une lecture pure, dans tout état */
    {"gx", "er nocap", "er nocap", "er nocap", "er nocap"},
};
static const uint8_t SEQ_STATES[] = {SESSION_POWERING, SESSION_IDENTIFYING, SESSION_HOMING, SESSION_RESTORING, SESSION_RECOVERING};

static void column(uint8_t s, bool id, const char *name, size_t k)
{
    for (size_t i = 0; i < sizeof COLS / sizeof COLS[0]; i++) {
        const char *want = k == 0 ? COLS[i].off : k == 1 ? COLS[i].seq : k == 2 ? COLS[i].fault_id : COLS[i].fault_noid;
        size_t n0 = n_cmds;
        fresh();
        state(s);
        ST.last_error = s == SESSION_FAULT ? E_LOST : E_OK;
        if (id) {
            ST.lens_id_product = 8;
            snprintf(ST.lens_name, sizeof ST.lens_name, "SAMYANG AF 135mm F1.8");
        }
        ST.mark_valid = false;
        n0 = n_cmds;
        CHECK(is(COLS[i].line, want), "%s : « %s » -> « %s », attendu « %s »", name, COLS[i].line, R, want);
        CHECK(n_cmds == n0, "%s : « %s » ne dépose rien", name, COLS[i].line);
    }
}

static void t_states(void)
{
    printf("hors READY : chaque lettre dans chaque colonne du tableau d'état (OFF, séquence, FAULT), rien déposé\n");
    column(SESSION_OFF, false, "off", 0);
    for (size_t i = 0; i < sizeof SEQ_STATES; i++) column(SEQ_STATES[i], true, bsk_state_name(SEQ_STATES[i]), 1);
    column(SESSION_FAULT, true, "fault (identité établie)", 2);
    column(SESSION_FAULT, false, "fault (sans identité)", 3);
    fresh();
    state(SESSION_FAULT);
    ST.lens_id_product = 0xC93A;   /* un code sans nom : identité établie par le code seul */
    ASK("o", "er nocap ap");
    ASK("i", "Samyang AF 24mm F1.8");
}

/* q, b, p0, p1 hors READY : ils déposent (§ 4.5.1). */
static void t_state_commands(void)
{
    size_t n0;
    printf("hors READY : q, b, p0, p1 ; le maintien de p0, tenu par la couche HOTE\n");
    fresh();                                        /* OFF, D2 absent, pas de maintien */
    n0 = n_cmds; ASK("b", "er nolens");
    CHECK(n_cmds == n0, "b en OFF sans p0 : er nolens, rien déposé");
    n0 = n_cmds; ASK("q", "ok");
    CHECK(deposited(n0, CMD_FOCUS_STOP, 0), "q en OFF : CMD_FOCUS_STOP, ok (q passe toujours)");
    n0 = n_cmds; ASK("p1", "ok");
    CHECK(deposited(n0, CMD_ATTACH, 0), "p1 en OFF : CMD_ATTACH, ok (p1 autorise l'alimentation, comme la v1)");
    n0 = n_cmds; ASK("p0", "ok");
    CHECK(deposited(n0, CMD_DETACH, 0), "p0 en OFF : CMD_DETACH, ok");
    n0 = n_cmds; ASK("b", "ok");
    CHECK(deposited(n0, CMD_ATTACH, 0), "b en OFF après p0 : CMD_ATTACH, ok (b lève le maintien de p0, comme la v1)");
    n0 = n_cmds; ASK("b", "er nolens");
    CHECK(n_cmds == n0, "b a levé le maintien : de nouveau er nolens");
    ASK("p0", "ok");
    ASK("p1", "ok");
    n0 = n_cmds; ASK("b", "er nolens");
    CHECK(n_cmds == n0, "p1 a levé le maintien");
    ASK("p0", "ok");
    RULE = E_BUSY;
    ASK("p1", "er busy");
    n0 = n_cmds; ASK("b", "ok");
    CHECK(deposited(n0, CMD_ATTACH, 0), "p1 refusé ne lève pas le maintien");

    for (size_t i = 0; i < sizeof SEQ_STATES; i++) {
        fresh();
        state(SEQ_STATES[i]);
        n0 = n_cmds; ASK("b", "ok");
        CHECK(deposited(n0, CMD_ATTACH, 0), "%s : b -> CMD_ATTACH (b redémarre depuis tout état)", bsk_state_name(SEQ_STATES[i]));
        RULE = E_BUSY;
        ASK("p0", "er busy boot");
        ASK("p1", "ok");
        if (SEQ_STATES[i] != SESSION_HOMING) ASK("q", "ok");
    }
    fresh();
    state(SESSION_HOMING);
    RULE = E_BUSY;
    ASK("q", "er busy home");                       /* q n'arrête pas un homing */
    fresh();
    state(SESSION_IDENTIFYING);
    RULE = E_BUSY;
    ASK("q", "er busy home");                       /* ni le 0x10 de l'init : la session le refuse de même */

    fresh();
    state(SESSION_FAULT);
    ST.last_error = E_LOST;
    n0 = n_cmds; ASK("b", "ok");
    CHECK(deposited(n0, CMD_CLEAR_FAULT, 0), "b en FAULT : CMD_CLEAR_FAULT");
    n0 = n_cmds; ASK("p0", "ok");
    CHECK(deposited(n0, CMD_DETACH, 0), "p0 en FAULT : CMD_DETACH (p0 accepté en FAULT)");
    state(SESSION_FAULT);
    n0 = n_cmds; ASK("p1", "ok");
    CHECK(deposited(n0, CMD_ATTACH, 0), "p1 en FAULT : CMD_ATTACH");
    state(SESSION_FAULT);
    n0 = n_cmds; ASK("q", "ok");
    CHECK(deposited(n0, CMD_FOCUS_STOP, 0), "q en FAULT : ok");
}

/* ─────────────────────────── last_op et move_rc (§ 4.5.3) ─────────────────────────── */

static uint32_t seq_of_last(void) { return last_cmd() ? last_cmd()->seq : 0; }

static void t_last_op(void)
{
    printf("last_op : arrivées de la session (boot:…), le plus récent l'emporte ; aucun home:…\n");
    fresh();
    TKEY("last_op", "-:ok");
    DKEY("move_rc", "-");
    state(SESSION_POWERING); bsk_host_observe();
    state(SESSION_HOMING); bsk_host_observe();
    TKEY("last_op", "-:ok");
    state(SESSION_READY); bsk_host_observe();
    TKEY("last_op", "boot:ok");                     /* l'arrivée du démarrage automatique */
    bsk_host_observe();
    TKEY("last_op", "boot:ok");                     /* une lecture sans changement d'état n'est pas une arrivée */

    ASK("b", "ok");                                 /* CMD_ATTACH, puis la session redémarre */
    state(SESSION_POWERING); bsk_host_observe();
    state(SESSION_RECOVERING); bsk_host_observe();
    state(SESSION_FAULT); ST.last_error = E_LOST; bsk_host_observe();
    TKEY("last_op", "boot:er:\"lost\"");            /* FAULT après la reprise : une arrivée */

    ASK("p1", "ok");
    state(SESSION_POWERING); ST.last_error = E_OK; bsk_host_observe();
    TKEY("last_op", "boot:er:\"lost\"");            /* POWERING n'est pas une arrivée */
    state(SESSION_READY); bsk_host_observe();
    TKEY("last_op", "boot:ok");                     /* toute arrivée, p1 compris */
    state(SESSION_HOMING); bsk_host_observe();
    TKEY("last_op", "boot:ok");                     /* un homing en cours ne change rien */

    fresh();                                        /* le 0x10 de l'init dit l'échec (10 01) */
    state(SESSION_IDENTIFYING); bsk_host_observe();
    state(SESSION_FAULT); ST.last_error = E_HOME_FAILED; bsk_host_observe();
    TKEY("last_op", "boot:er:\"home_failed\"");
}

static void t_move_rc(void)
{
    uint32_t g;
    printf("move_rc (DEBUG) et g : l'accusé final de GOTO et de MOVE ; aborted par q ; - quand la session quitte READY ; le goto du bouton\n");
    ready135();
    DKEY("move_rc", "-");
    ASK("g", "-");
    ASK("f25000", "ok");
    g = seq_of_last();
    ST.motion_state = MOTION_MOVING; bsk_host_observe();
    DKEY("move_rc", "-");
    ASK("g", "-");
    push(g, ACK_COMPLETED, E_OK); ST.motion_state = MOTION_ARRIVED; bsk_host_observe();
    DKEY("move_rc", "ok");
    ASK("g", "ok");
    TKEY("last_op", "boot:ok");                     /* un goto n'est pas une opération de last_op */

    ASK("f+100", "ok");
    g = seq_of_last();
    push(g, ACK_FAILED, E_STALL); bsk_host_observe();
    DKEY("move_rc", "stall");
    ASK("g", "stall");

    ASK("f20000", "ok");
    g = seq_of_last();
    EXTRA[0] = (bsk_ack_t){g, ACK_FAILED, E_ABORTED};   /* l'accusé du goto arrêté, dans le dépôt de q */
    n_extra = 1;
    ASK("q", "ok");
    DKEY("move_rc", "aborted");
    ASK("g", "aborted");

    ASK("f20000", "ok");
    g = seq_of_last();
    push(g, ACK_FAILED, E_ABORTED);
    state(SESSION_OFF); bsk_host_observe();         /* D2 retombe pendant le goto : le déplacement en vol est oublié */
    DKEY("move_rc", "-");
    ASK("g", "-");

    ready135();
    ASK("f20000", "ok");
    push(seq_of_last(), ACK_COMPLETED, E_OK); bsk_host_observe();
    DKEY("move_rc", "ok");
    ASK("g", "ok");
    ASK("f21000", "ok");
    push(seq_of_last(), ACK_FAILED, E_LOST);
    state(SESSION_RECOVERING); bsk_host_observe();  /* perte : oublié aussi */
    DKEY("move_rc", "-");
    ASK("g", "-");

    ready135();
    ASK("f25000", "ok");
    ASK("q", "ok");                                 /* la session scriptée n'arrête pas le goto : q finit seul */
    bsk_host_observe();
    DKEY("move_rc", "-");
    ASK("g", "-");                           /* l'accusé final de q ne finit pas le goto */

    ready135();
    ST.mark_valid = true;
    ST.mark_position = 16340;
    ASK("jg", "ok");
    push(seq_of_last(), ACK_COMPLETED, E_OK); bsk_host_observe();
    DKEY("move_rc", "ok");
    ASK("g", "ok");                          /* jg est un goto */
    ASK("m25000", "ok");
    push(seq_of_last(), ACK_FAILED, E_STALL); bsk_host_observe();
    DKEY("move_rc", "stall");
    ASK("g", "stall");                       /* m<n> aussi */

    ready135();
    RULE = E_BUSY;
    ASK("f100", "er busy");
    push(seq_of_last(), ACK_COMPLETED, E_OK); bsk_host_observe();
    DKEY("move_rc", "-");
    ASK("g", "-");                           /* un refus n'est pas un vol */

    ready135();                                     /* le goto du bouton du fût, BSK_SEQ_BOARD */
    push(BSK_SEQ_BOARD, ACK_ACCEPTED, E_OK); bsk_host_observe();
    DKEY("move_rc", "-");
    ASK("g", "-");
    push(BSK_SEQ_BOARD, ACK_FAILED, E_STALL); bsk_host_observe();
    DKEY("move_rc", "stall");
    ASK("g", "stall");
    push(BSK_SEQ_BOARD, ACK_REJECTED, E_LIMIT); bsk_host_observe();
    push(BSK_SEQ_BOARD, ACK_COMPLETED, E_OK); bsk_host_observe();
    DKEY("move_rc", "stall");
    ASK("g", "stall");                       /* refusé : pas un vol, son final n'est pas lu */
    push(BSK_SEQ_BOARD, ACK_ACCEPTED, E_OK); bsk_host_observe();
    push(BSK_SEQ_BOARD, ACK_COMPLETED, E_OK); bsk_host_observe();
    DKEY("move_rc", "ok");
    ASK("g", "ok");

    ready135();                                     /* g : gardé jusqu'à la fin du suivant, sortie de READY comprise */
    ASK("f20000", "ok");
    push(seq_of_last(), ACK_FAILED, E_STALL); bsk_host_observe();
    ASK("f21000", "ok");
    ST.motion_state = MOTION_MOVING; bsk_host_observe();
    ASK("g", "stall");                              /* en vol : le résultat du précédent */
    push(seq_of_last(), ACK_COMPLETED, E_OK); ST.motion_state = MOTION_ARRIVED; bsk_host_observe();
    state(SESSION_RECOVERING); bsk_host_observe();
    ASK("g", "ok");                                 /* fini en READY, puis la perte : gardé */
    state(SESSION_READY); bsk_host_observe();
    ASK("g", "ok");
    CHECK(n_cmds == 2, "g ne dépose rien (%zu)", n_cmds);

    ready135();                                     /* l'arrêt non confirmé : le compteur de la session change */
    ASK("f20000", "ok");
    EXTRA[0] = (bsk_ack_t){seq_of_last(), ACK_FAILED, E_ABORTED};
    n_extra = 1;
    ASK("q", "ok");
    ASK("g", "aborted");
    ST.stop_unconfirmed = 1; bsk_host_observe();
    ASK("g", "unconfirmed");                        /* aborted -> unconfirmed, l'abandon 4,5 s après q */
    DKEY("move_rc", "aborted");                     /* DEBUG ne change pas */
    bsk_host_observe();
    ASK("g", "unconfirmed");
    ASK("f21000", "ok");
    ST.motion_state = MOTION_MOVING; bsk_host_observe();
    ASK("g", "unconfirmed");                        /* en vol : le résultat du précédent */
    push(seq_of_last(), ACK_COMPLETED, E_OK); ST.motion_state = MOTION_ARRIVED; bsk_host_observe();
    ASK("g", "ok");                                 /* le compteur n'a pas changé : la fin suivante l'emporte */
    ASK("f22000", "ok");
    push(seq_of_last(), ACK_FAILED, E_STALL); bsk_host_observe();
    ASK("g", "stall");
    ST.stop_unconfirmed = 2; bsk_host_observe();
    ASK("g", "unconfirmed");                        /* STALLED, puis son 0x1C abandonné : le plus grave l'emporte */
    DKEY("move_rc", "stall");
    state(SESSION_OFF); bsk_host_observe();
    ASK("g", "unconfirmed");                        /* gardé, sortie de READY comprise */
    ST.stop_unconfirmed = 0xFFFF; bsk_host_observe();
    state(SESSION_READY);
    ASK("f20000", "ok");
    push(seq_of_last(), ACK_COMPLETED, E_OK); bsk_host_observe();
    ST.stop_unconfirmed = 0; bsk_host_observe();
    ASK("g", "unconfirmed");                        /* modulo 2^16 : 0xFFFF -> 0 est un changement */
}

/* ─────────────────────────── t (§ 4.5.3) ─────────────────────────── */

static void t_line_t(void)
{
    /* Les clés que `t` ne porte pas : doublons d'une autre lettre (fw : v ; id : i ; mark : la marque ; min, max : r ;
     * module : n), clés sans source dans la carte (caps, limits_focal, limits_src, home, focal_nom, session,
     * boot_step, boot_reason, power, stored, sw, uptime), diagnostic de DEBUG (reset, dropped, move_rc). */
    static const char *const gone[] = {"fw", "reset", "module", "caps", "id", "limits_focal", "limits_src", "mark", "min", "max",
                                       "home", "focal_nom", "session", "boot_step", "boot_reason", "power", "stored", "dropped",
                                       "move_rc", "sw", "uptime"};
    printf("t : l'état de la carte seul, ext=1 en tête, aucun champ retiré ; i : le nom, la table de repli, "
           "#<code>, -\n");
    ready135();
    ASK("t", "ext=1 present=1 boot_state=ready busy=- power_off=0 last_op=boot:ok mf=- oss=- ring=-");
    aperture135();                                   /* tout ce que l'instantané publie : t n'en dit pas plus */
    ST.mark_valid = true;
    ST.mark_position = 16340;
    ST.ring = RING_APERTURE;
    ST.mf = BSK_NO;
    ST.oss = BSK_YES;
    ASK("t", "ext=1 present=1 boot_state=ready busy=- power_off=0 last_op=boot:ok mf=0 oss=1 ring=ap");
    for (size_t i = 0; i < sizeof gone / sizeof gone[0]; i++) CHECK(!tkey(gone[i]), "clé %s retirée (« %s »)", gone[i], R);
    ST.lens_name[0] = 0;
    ASK("i", "#8");                                  /* le LensType2 du 135 n'est pas dans la table ExifTool */
    ST.lens_id_product = 13;
    ASK("i", "Samyang AF 35-150mm F2-2.8");          /* sans nom : la table de repli (lens_names.c, code 13) */
    ST.lens_id_product = 0x7777;
    ASK("i", "#30583");                              /* ni nom ni entrée : #<code> */
    ST.lens_id_product = 0;
    ASK("i", "-");

    fresh();
    ASK("t", "ext=1 present=0 boot_state=off busy=- power_off=0 last_op=-:ok mf=- oss=- ring=-");
    for (size_t i = 0; i < sizeof SEQ_STATES; i++) {
        state(SEQ_STATES[i]);
        TKEY("present", "1");
        TKEY("busy", "boot");
        TKEY("boot_state", bsk_state_name(SEQ_STATES[i]));
    }
    state(SESSION_FAULT);
    ASK("t", "ext=1 present=1 boot_state=fault busy=- power_off=0 last_op=-:ok mf=- oss=- ring=-");

    printf("  power_off : le maintien de p0, levé par p1 et par b\n");
    fresh();
    ASK("p0", "ok");
    TKEY("power_off", "1");
    ASK("p1", "ok");
    TKEY("power_off", "0");
    ASK("p0", "ok");
    RULE = E_BUSY;
    ASK("p1", "er busy");
    TKEY("power_off", "1");                         /* un p1 refusé ne lève rien */
    ASK("b", "ok");
    TKEY("power_off", "0");
}

/* Les compteurs du temps réel de `DEBUG`, tous à zéro (bsk_session_debug), puis les marges de pile, à zéro
 * (bsk_host_stacks). */
#define ZERO " evq_drop=0 ev_back=0 ev_back_us=0 tx_wait_us=0 tx_timeout=0 pair_late=0 pair_late_us=0" STK0
#define STK0 " stack_session=0 stack_phy=0 stack_usb_rx=0 stack_store=0 stack_journal=0"

/* `DEBUG` : le diagnostic, hors de `t`, une ligne `ok clé=valeur…`, dans tout état (t_states), rien déposé. */
static void t_debug(void)
{
    size_t n0;
    printf("DEBUG : reset, dropped, move_rc, refused, les compteurs du temps réel, les marges de pile ; v rend la version "
           "donnée à bsk_host_init\n");
    ready135();
    n0 = n_cmds;
    ASK("DEBUG", "ok reset=poweron dropped=0 move_rc=- refused=0" ZERO);
    bsk_journal_lost();
    bsk_journal_lost();
    ASK("DEBUG", "ok reset=poweron dropped=2 move_rc=- refused=0" ZERO);
    REFUSED = 7;                                     /* le compteur de bench_core, tel qu'il le rend */
    ASK("DEBUG", "ok reset=poweron dropped=2 move_rc=- refused=7" ZERO);
    REFUSED = 0;
    /* chaque compteur de bsk_session_debug à sa clé : sept valeurs distinctes, l'une à 2^32 − 1 */
    DBG = (bsk_session_debug_t){.evq_drop = 11, .ev_back = 12, .ev_back_us = 13, .tx_wait_us = 14, .tx_timeout = 15,
                                .pair_late = 16, .pair_late_us = 4294967295u};
    ASK("DEBUG", "ok reset=poweron dropped=2 move_rc=- refused=0 evq_drop=11 ev_back=12 ev_back_us=13 tx_wait_us=14 "
                 "tx_timeout=15 pair_late=16 pair_late_us=4294967295" STK0);
    DBG = (bsk_session_debug_t){0};
    /* chaque marge de pile à sa clé, après les compteurs (PROTOCOL.md § 3) : cinq valeurs distinctes, l'une à 2^32 − 1 */
    STACKS = (bsk_host_stacks_t){.session = 2100, .phy = 1300, .usb_rx = 2900, .store = 700, .journal = 4294967295u};
    ASK("DEBUG", "ok reset=poweron dropped=2 move_rc=- refused=0" " evq_drop=0 ev_back=0 ev_back_us=0 tx_wait_us=0 "
                 "tx_timeout=0 pair_late=0 pair_late_us=0 stack_session=2100 stack_phy=1300 stack_usb_rx=2900 stack_store=700 "
                 "stack_journal=4294967295");
    STACKS = (bsk_host_stacks_t){0};
    ASK("DEBUG ", "er nocap");                       /* une ligne reconnue exactement */
    ASK("DEBUG ALL", "er nocap");
    ASK("DEBUGX", "er nocap");
    ASK("Debug", "er nocap");
    CHECK(n_cmds == n0, "DEBUG : rien déposé (%zu)", n_cmds - n0);
    bsk_host_init("7.3", "task_wdt");                /* une autre carte : v et reset sont ceux qu'on lui donne */
    ASK("v", "7.3");
    ASK("DEBUG", "ok reset=task_wdt dropped=2 move_rc=- refused=0" ZERO);
}

/* ─────────────────────────── la bague, et les lignes inconnues `u…`, `cm`, `m…` ─────────────────────────── */

/* Décision de l'humain : le rôle de la bague suit le commutateur, jamais forcé depuis l'hôte : `u`, `ux`, `uf`, `ua` et
 * `cm` sont des lignes inconnues, `m`, `ms`, `mg`, `mx` aussi (la marque est sur `j`) ; le rôle est lisible dans `t`
 * (`ring`), avec `mf` et `oss`. Aucune ligne d'un ou deux caractères, minuscule en tête, ne dépose CMD_RING_SET, quel que
 * soit le rôle publié. */
static void t_retired(void)
{
    static const char *const RETIRED[] = {"u", "ux", "uf", "ua", "uA", "uz", "ufx", "u ", "cm", "m", "ms", "mg", "mx", "mz", "m+5",
                                          "m-5", "m 5"};
    static const uint8_t ROLES[] = {RING_UNKNOWN, RING_FOCUS, RING_APERTURE};
    static const char ALPHA[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+-. ";
    char line[3];
    size_t n0;
    printf("lignes retirées : u, ux, uf, ua, cm, m, ms, mg, mx -> er nocap, rien déposé ; aucune ligne ne dépose CMD_RING_SET ; "
           "ring, mf et oss dans t\n");
    for (size_t k = 0; k < sizeof ROLES; k++) {
        ready135();
        ST.ring = ROLES[k];
        ST.mf = ROLES[k] == RING_FOCUS ? BSK_YES : ROLES[k] == RING_APERTURE ? BSK_NO : BSK_UNKNOWN;
        ST.mark_valid = true;
        ST.mark_position = 16340;
        n0 = n_cmds;
        for (size_t i = 0; i < sizeof RETIRED / sizeof RETIRED[0]; i++) ASK(RETIRED[i], "er nocap");
        CHECK(n_cmds == n0, "rôle %u : rien déposé (%zu)", ROLES[k], n_cmds - n0);
        n_ring_set = 0;
        for (const char *a = "abcdefghijklmnopqrstuvwxyz"; *a; a++) {
            line[0] = *a;
            line[1] = 0;
            ask(line);
            for (const char *b = ALPHA; *b; b++) {
                line[1] = *b;
                line[2] = 0;
                ready135();                          /* p0, k0… : chaque ligne part de READY */
                ST.ring = ROLES[k];
                ask(line);
            }
        }
        CHECK(n_ring_set == 0, "rôle %u : %zu CMD_RING_SET déposée(s) par les lignes d'un ou deux caractères", ROLES[k], n_ring_set);
    }
    bsk_led_init(0);                                 /* `k0` est passé : le plafond de la LED rendu à 100 */

    ready135();
    TKEY("ring", "-");                               /* l'offset 62 pas publié */
    TKEY("mf", "-");
    TKEY("oss", "-");
    ST.ring = RING_APERTURE;
    ST.mf = BSK_NO;
    ST.oss = BSK_NO;
    TKEY("ring", "ap");
    TKEY("mf", "0");
    TKEY("oss", "0");
    ST.ring = RING_FOCUS;
    ST.mf = BSK_YES;
    ST.oss = BSK_YES;
    TKEY("ring", "focus");
    TKEY("mf", "1");
    TKEY("oss", "1");
    ST.oss = BSK_NO;
    TKEY("mf", "1");                                /* chacun sa clé */
    TKEY("oss", "0");
    ST.mf = BSK_NO;                                  /* le rôle publié par la session, pas recalculé d'après mf */
    TKEY("ring", "focus");
    for (size_t i = 0; i < sizeof SEQ_STATES; i++) {
        state(SEQ_STATES[i]);
        TKEY("ring", "focus");                       /* dans tout état */
    }
}

/* ─────────────────────────── le driver ASCOM de Pinefeat (PROTOCOL.md § 2.1) ─────────────────────────── */

/* La séquence du driver ASCOM de Pinefeat (github.com/pinefeat/cef135, PinefeatCEF/FocuserDriver/FocuserHardware.cs, lu, rien
 * repris) contre le 135 en READY : `v` journalisée ; `r` découpée sur `-`, l'entier du dernier morceau ; `f` int.Parse ;
 * `m` + position, `ok` exact sinon exception ; `e` : `y` exact = en mouvement ; `a` la plage ; `a` + ouverture au format
 * « 0.0#### » (22 -> « 22.0 »), `ok` exact ; `c`, `ok` exact. Chaque réponse est écrite à la main, entière. */
static void t_ascom(void)
{
    size_t n0;
    printf("le driver ASCOM de Pinefeat : v, r, f, m<n>, e jusqu'à n, f, a, a<f>, c ; réponses exactes\n");
    ready135();
    aperture135();
    ASK("v", V);
    ASK("r", "13808-30946");
    ASK("f", "20000");
    n0 = n_cmds; ASK("m25000", "ok");
    CHECK(deposited(n0, CMD_FOCUS_GOTO, 25000), "m25000 : CMD_FOCUS_GOTO 25000");
    ST.motion_state = MOTION_MOVING;
    ST.focus_position = 21500;
    ASK("e", "y");
    ST.motion_state = MOTION_SETTLING;
    ST.focus_position = 25000;
    ASK("e", "y");
    push(seq_of_last(), ACK_COMPLETED, E_OK);
    ST.motion_state = MOTION_ARRIVED;
    bsk_host_observe();
    ASK("e", "n");
    ASK("f", "25000");
    ASK("a", "1.8-22");
    n0 = n_cmds; ASK("a2.8", "ok");
    CHECK(deposited(n0, CMD_APERTURE_SET, 0x12F9), "a2.8 : CMD_APERTURE_SET 0x12F9");
    n0 = n_cmds; ASK("a22.0", "ok");
    CHECK(deposited(n0, CMD_APERTURE_SET, 0x18EB), "a22.0 : CMD_APERTURE_SET 0x18EB");
    n0 = n_cmds; ASK("a1.8", "ok");
    CHECK(deposited(n0, CMD_APERTURE_SET, 0x11B2), "a1.8 : CMD_APERTURE_SET 0x11B2");
    n0 = n_cmds; ASK("c", "ok");
    CHECK(n_cmds == n0, "c : rien déposé");
    ASK("r", "13808-30946");
    ASK("f", "25000");
    DKEY("move_rc", "ok");
    RULE = E_LIMIT;
    ASK("m0", "er range limits");                    /* Math.Max(0, Position) hors des bornes : pas ok, l'exception du driver */
}

/* ─────────────────────────── `k`, le plafond de la LED ─────────────────────────── */

/* `k` rend le plafond en pour cent ; `k<n>`, n de 0 à 100, le règle et répond ok ; hors plage ou illisible,
 * `er range 0 100` (15 caractères au plus) ; dans tout état, rien déposé ; 100 au démarrage, rien rangé. */
static void t_led_ceiling(void)
{
    static const uint8_t STATES[] = {SESSION_OFF, SESSION_POWERING, SESSION_HOMING, SESSION_READY, SESSION_FAULT};
    size_t n0;
    printf("k : le plafond de la LED lu et réglé dans tout état, er range 0 100, rien déposé\n");
    fresh();
    bsk_led_init(0);
    n0 = n_cmds;
    for (size_t i = 0; i < sizeof STATES; i++) {
        state(STATES[i]);
        ASK("k", "100");
    }
    state(SESSION_OFF);
    ASK("k5", "ok");
    CHECK(bsk_led_ceiling() == 5, "k5 : le plafond à 5 (%u)", bsk_led_ceiling());
    ASK("k", "5");
    ASK("k0", "ok");
    ASK("k", "0");
    ASK("k100", "ok");
    ASK("k", "100");
    state(SESSION_FAULT);
    ASK("k007", "ok");
    ASK("k", "7");
    for (const char *const *l = (const char *const[]){"k101", "k65536", "k-1", "k+5", "kx", "k5x", "k 5", "k5.0", NULL}; *l; l++) {
        ASK(*l, "er range 0 100");
        CHECK(strlen(R) <= 15, "« %s » : 15 caractères au plus (%zu)", *l, strlen(R));
    }
    ASK("k", "7");
    CHECK(n_cmds == n0, "k : rien déposé (%zu)", n_cmds - n0);
    bsk_led_init(0);
    ASK("k", "100");
}

/* ─────────────────────────── majuscules et Moonlite ─────────────────────────── */

static void t_upper(void)
{
    printf("LOG ON|ALL|OFF ; toute autre ligne en majuscules : er nocap (pas de mode labo)\n");
    fresh();
    ASK("LOG ON", "ok log=1 all=0");
    CHECK(bsk_journal_on() && !bsk_journal_all(), "journal allumé");
    ASK("LOG ALL", "ok log=1 all=1");
    CHECK(bsk_journal_all(), "LOG ALL");
    ASK("LOG OFF", "ok log=0 all=0");
    CHECK(!bsk_journal_on(), "LOG OFF");
    ASK("LOG  on", "ok log=1 all=0");
    ASK("LOG off", "ok log=0 all=0");
    ASK("LOG", "er range LOG ON|ALL|OFF");
    ASK("LOG MAYBE", "er range LOG ON|ALL|OFF");
    ASK("LOG ON ALL", "er range LOG ON|ALL|OFF");
    CHECK(!bsk_journal_on(), "un LOG refusé ne change rien");
    ASK("LOGON", "er nocap");
    ASK("DUMP", "er nocap");
    ASK("DUMP 05", "er nocap");
    ASK("SDRIVE", "er nocap");
    ASK("SNIFF ON", "er nocap");
    ASK("LIMITS", "er nocap");
    ASK("SSVC ON", "er nocap");
}

/* ─────────────────────────── CUSTOM (PROTOCOL.md § 3.1) ─────────────────────────── */

/* Une ligne dont la réponse attend la session : BSK_HOST_LATER, rien d'écrit. */
static bool later(const char *line)
{
    R[0] = 'x';
    R[1] = 0;
    return bsk_host_line(line, R, sizeof R) == BSK_HOST_LATER;
}

/* La réponse différée, après un pas : bsk_host_later rend `\n` et `want`, ou toujours BSK_HOST_LATER (want NULL). */
static bool later_is(const char *want)
{
    const char *tail;
    bsk_host_observe();
    tail = bsk_host_later(R, sizeof R);
    if (!want) return tail == BSK_HOST_LATER;
    return tail && tail != BSK_HOST_LATER && !strcmp(tail, "\n") && !strcmp(R, want);
}

static void t_custom(void)
{
    static const char *const BAD[] = {"CUSTOM", "CUSTOM ", "CUSTOM READ X", "CUSTOM REA", "CUSTOM WRITE", "CUSTOM WRITE 3",
                                      "CUSTOM WRITE 1", "CUSTOM WRITE 03", "CUSTOM WRITE 30", "CUSTOM WRITE 23", "CUSTOM WRITE 123",
                                      "CUSTOM WRITE 1x", "CUSTOM WRITE 12 1", "CUSTOM WRITE -1", "CUSTOM 12", "CUSTOM READ 12"};
    static const struct { const char *line; int32_t arg; } OK[] = {
        {"CUSTOM READ", -1}, {"CUSTOM read", -1}, {"CUSTOM\tRead  ", -1}, {"CUSTOM WRITE 10", 0x10}, {"CUSTOM WRITE 00", 0x00},
        {"CUSTOM WRITE 22", 0x22}, {"CUSTOM write 02", 0x02}, {"CUSTOM WRITE 21", 0x21}};
    size_t n0;
    uint32_t seq;
    printf("CUSTOM READ|WRITE <h><b> : le texte, l'état, les refus, la réponse différée\n");
    ready135();
    n0 = n_cmds;
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) ASK(BAD[i], "er range CUSTOM READ|WRITE <0-2><0-2>");
    ASK("CUSTOMX", "er nocap");
    ASK("CUSTOMREAD", "er nocap");
    CHECK(n_cmds == n0, "texte refusé : rien déposé (%zu)", n_cmds - n0);
    for (size_t i = 0; i < sizeof OK / sizeof OK[0]; i++) {
        ready135();
        n0 = n_cmds;
        CHECK(later(OK[i].line) && deposited(n0, CMD_LENS_CUSTOM, OK[i].arg), "« %s » : CMD_LENS_CUSTOM %ld déposée, réponse "
              "différée", OK[i].line, (long)OK[i].arg);
    }
    ready135();
    CHECK(later("CUSTOM READ"), "READ : différée");
    seq = last_cmd()->seq;
    CHECK(later_is(NULL), "sans accusé final : toujours différée");
    push(BSK_SEQ_BOARD, ACK_COMPLETED, E_OK);         /* l'accusé d'une autre commande (le goto du bouton) */
    push(seq + 1, ACK_FAILED, E_TIMEOUT);
    CHECK(later_is(NULL), "l'accusé final d'une autre commande : toujours différée");
    for (uint8_t i = 0; i < BSK_CUSTOM_DATA; i++) CDATA[i] = (uint8_t)(i == 7 ? 0x21 : 0xA0 + i);
    push(seq, ACK_COMPLETED, E_OK);
    CHECK(later_is("ok A0 A1 A2 A3 A4 A5 A6 21 A8 A9 AA AB AC AD AE AF"), "COMPLETED : ok et les 16 octets, en hexadécimal, "
          "dans l'ordre (« %s »)", R);
    ASK("v", V);
    {
        static const struct { bsk_err_t why; const char *want; } FAILS[] = {
            {E_TIMEOUT, "er link timeout"}, {E_FRAMING, "er link framing"}, {E_ABORTED, "er link aborted"},
            {E_FORBIDDEN, "er refused"}, {E_BUS, "er link bus"}};
        for (size_t i = 0; i < sizeof FAILS / sizeof FAILS[0]; i++) {
            ready135();
            CHECK(later("CUSTOM WRITE 21"), "WRITE : différée");
            push(last_cmd()->seq, ACK_FAILED, FAILS[i].why);
            CHECK(later_is(FAILS[i].want), "FAILED %s : « %s » (« %s »)", bsk_err_token(FAILS[i].why), FAILS[i].want, R);
        }
    }
    {
        static const struct { bsk_err_t why; const char *want; } REFUSALS[] = {
            {E_NOCAP, "er nocap custom"}, {E_BUSY, "er busy"}, {E_FORBIDDEN, "er refused"}};
        for (size_t i = 0; i < sizeof REFUSALS / sizeof REFUSALS[0]; i++) {
            ready135();
            RULE = REFUSALS[i].why;
            ASK("CUSTOM READ", REFUSALS[i].want);
        }
    }
    {
        static const struct { uint8_t st; const char *want; } STATES[] = {
            {SESSION_OFF, "er nolens"}, {SESSION_POWERING, "er busy boot"}, {SESSION_HOMING, "er busy boot"},
            {SESSION_RESTORING, "er busy boot"}, {SESSION_FAULT, "er fault"}};
        for (size_t i = 0; i < sizeof STATES / sizeof STATES[0]; i++) {
            ready135();
            state(STATES[i].st);
            n0 = n_cmds;
            ASK("CUSTOM WRITE 21", STATES[i].want);
            CHECK(n_cmds == n0, "%s : rien déposé", bsk_state_name(STATES[i].st));
        }
    }
}

/* ─────────────────────────── Moonlite (§ 4.5.4) ─────────────────────────── */

/* Ce que le transport écrit pour une ligne (deliver, dans host_usb.c) : la réponse, puis ce que HOTE dit d'écrire après elle ;
 * "" quand HOTE ne rend rien à écrire (NULL). Une ligne Moonlite arrive comme le transport la cadre : `:` … sans `#`. */
static char W[BSK_HOST_REPLY_MAX + 2];
static bool silent;

static const char *wire(const char *line)
{
    const char *tail = bsk_host_line(line, R, sizeof R);
    silent = tail == NULL;
    snprintf(W, sizeof W, "%s%s", tail ? R : "", tail ? tail : "");
    return W;
}

/* Les octets exacts sur le fil, terminateur compris ; "" : aucune réponse, HOTE ne rend rien à écrire. */
#define WIRE(line, want)                                                                                                    \
    CHECK(!strcmp(wire(line), want) && silent == !*(want), "« %s » -> « %s »%s, attendu « %s »", line, W,                  \
          silent ? " (rien)" : "", want)

/* La session publie un nouvel instantané, que HOTE lit après le pas (app_main : bsk_host_observe). */
static void step(void) { bsk_host_observe(); }

static void t_moonlite(void)
{
    size_t n0;
    printf("Moonlite : chaque commande, octets exacts sans fin de ligne ; :GP# hors READY, comme l'ancien firmware ; :SN, :GN#, :FG#, :FQ#, :GI#\n");
    ready135();
    n0 = n_cmds;
    WIRE(":GV", "10");                              /* 2 chiffres, sans '#' */
    WIRE(":GT", "0000#");
    WIRE(":GC", "00#");
    WIRE(":GD", "02#");
    WIRE(":GH", "00#");
    for (const char *const *m = (const char *const[]){":SD", ":SH", ":SF", ":SC", ":PO", ":C", ":+", ":-", ":SP", ":SP1234", ":XX",
                                                      ":", ":gp", ":GPX", NULL};
         *m; m++)
        WIRE(*m, "");
    CHECK(n_cmds == n0, "les constantes et les commandes muettes : rien déposé (%zu)", n_cmds - n0);

    printf("  :GP# : focus_position en READY, la dernière position valide hors READY, oubliée en OFF, 0000# sans elle\n");
    WIRE(":GP", "4E20#");                           /* 20000 */
    ST.focus_position = 43981;
    WIRE(":GP", "ABCD#");                           /* 4 chiffres hexa, en majuscules */
    ST.focus_position = 10;
    WIRE(":GP", "000A#");
    ST.focus_position = 20000;
    step();
    state(SESSION_POWERING);                        /* `b` : une session qui redémarre ; rien de publié */
    ST.position_valid = false;
    ST.focus_position = 0;
    ST.lens_id_product = 0;
    ST.lens_name[0] = 0;
    step();
    WIRE(":GP", "4E20#");
    state(SESSION_IDENTIFYING);
    ST.lens_id_product = 8;                         /* le 0x07 avant le nom (0x3F) : pas un changement d'identité */
    step();
    WIRE(":GP", "4E20#");
    snprintf(ST.lens_name, sizeof ST.lens_name, "SAMYANG AF 135mm F1.8");
    state(SESSION_RESTORING);
    ST.focus_position = 15000;                      /* publiée, mais pas valide hors READY */
    step();
    WIRE(":GP", "4E20#");
    state(SESSION_FAULT);
    step();
    WIRE(":GP", "4E20#");
    state(SESSION_OFF);                             /* oubliée en OFF */
    ST.lens_id_product = 0;
    ST.lens_name[0] = 0;
    step();
    WIRE(":GP", "0000#");
    state(SESSION_POWERING);
    step();
    WIRE(":GP", "0000#");
    fresh();
    state(SESSION_HOMING);
    step();
    WIRE(":GP", "0000#");                           /* jamais de position : 0000# */
    ready135();
    ST.position_valid = false;                      /* READY sans 0x06 : comme hors READY, la position gardée (t_no_position) */
    ST.focus_position = 0;
    WIRE(":GP", "4E20#");

    printf("  :GP# : la position oubliée quand l'identité change (le code modèle, puis le nom)\n");
    ready135();
    state(SESSION_IDENTIFYING);
    ST.position_valid = false;
    ST.lens_id_product = 0x2061;                    /* un autre code modèle, le nom pas encore lu */
    ST.lens_name[0] = 0;
    step();
    WIRE(":GP", "0000#");
    ready135();
    state(SESSION_IDENTIFYING);
    ST.position_valid = false;
    snprintf(ST.lens_name, sizeof ST.lens_name, "SAMYANG AF 24mm F1.8");   /* un autre nom, le même code modèle */
    step();
    WIRE(":GP", "0000#");
    ready135();
    state(SESSION_IDENTIFYING);
    ST.position_valid = false;
    ST.lens_id_product = 0;                         /* l'identité pas encore publiée : pas un changement */
    ST.lens_name[0] = 0;
    step();
    WIRE(":GP", "4E20#");
    ready135();
    ST.focus_position = 21000;
    step();
    state(SESSION_RECOVERING);                      /* la dernière position publiée valide, pas la première */
    ST.position_valid = false;
    step();
    WIRE(":GP", "5208#");

    printf("  :SN, :GN# : sans :SN, la position (host_moonlite.c:36) ; la cible vit autant que la carte\n");
    ready135();
    n0 = n_cmds;
    WIRE(":GN", "4E20#");
    state(SESSION_HOMING);
    ST.position_valid = false;
    step();
    WIRE(":GN", "4E20#");                           /* hors READY : la position gardée */
    WIRE(":SN3E80", "");
    WIRE(":GN", "3E80#");
    WIRE(":SN 1a2B", "");                           /* un blanc, l'hexadécimal sans casse */
    WIRE(":GN", "1A2B#");
    WIRE(":SNFFFF", "");
    WIRE(":GN", "FFFF#");
    WIRE(":SN0", "");
    WIRE(":GN", "0000#");
    WIRE(":SN4000", "");
    for (const char *const *m = (const char *const[]){":SN", ":SN ", ":SN10000", ":SNxyz", ":SN12 ", ":SN12G", NULL}; *m; m++) {
        WIRE(*m, "");
        WIRE(":GN", "4000#");                       /* illisible ou au-delà de FFFF : ignorée, la cible reste */
    }
    state(SESSION_OFF);                             /* la cible survit à OFF et au changement d'identité */
    ST.lens_id_product = 0x2061;
    step();
    WIRE(":GN", "4000#");
    CHECK(n_cmds == n0, ":SN, :GN# : rien déposé (%zu)", n_cmds - n0);

    printf("  :FG# : CMD_FOCUS_GOTO vers la cible, l'admission de la session ; sans réponse, refusé ou non\n");
    ready135();
    n0 = n_cmds;
    WIRE(":FG", "");
    CHECK(n_cmds == n0, ":FG# sans :SN : rien déposé (host_moonlite.c:51)");
    WIRE(":SN4000", "");
    n0 = n_cmds;
    WIRE(":FG", "");
    CHECK(deposited(n0, CMD_FOCUS_GOTO, 0x4000), ":FG# : CMD_FOCUS_GOTO 16384");
    RULE = E_BUSY;
    n0 = n_cmds;
    WIRE(":FG", "");
    CHECK(deposited(n0, CMD_FOCUS_GOTO, 0x4000), ":FG# refusé (E_BUSY) : déposé, aucune réponse");
    RULE = E_LIMIT;
    WIRE(":FG", "");
    state(SESSION_OFF);
    RULE = E_BUSY;                                  /* la session refuse hors READY */
    n0 = n_cmds;
    WIRE(":FG", "");
    CHECK(deposited(n0, CMD_FOCUS_GOTO, 0x4000), ":FG# en OFF : la session juge, aucune réponse");

    printf("  :FQ# : CMD_FOCUS_STOP, comme q, sans réponse ; refusé pendant un homing, toujours sans réponse\n");
    ready135();
    n0 = n_cmds;
    WIRE(":FQ", "");
    CHECK(deposited(n0, CMD_FOCUS_STOP, 0), ":FQ# : CMD_FOCUS_STOP");
    state(SESSION_HOMING);
    RULE = E_BUSY;
    n0 = n_cmds;
    WIRE(":FQ", "");
    CHECK(deposited(n0, CMD_FOCUS_STOP, 0), ":FQ# pendant un homing : déposé, refusé, aucune réponse");

    printf("  :GI# : comme e, 01# / 00#\n");
    ready135();
    WIRE(":GI", "00#");
    ST.motion_state = MOTION_MOVING;
    WIRE(":GI", "01#");
    ST.motion_state = MOTION_ARRIVED;
    WIRE(":GI", "00#");
    state(SESSION_HOMING);                          /* e : y dans les états de séquence */
    WIRE(":GI", "01#");
    state(SESSION_FAULT);
    WIRE(":GI", "00#");
    state(SESSION_OFF);
    WIRE(":GI", "00#");

    printf("  une ligne Moonlite ne change pas la suivante : la lettre garde son \\n\n");
    ready135();
    WIRE(":GP", "4E20#");
    WIRE("v", V "\n");
    WIRE(":SN4000", "");
    WIRE("f", "20000\n");
    WIRE(":GV", "10");
    WIRE("LOG OFF", "ok log=0 all=0\n");
    WIRE("  :GP", "4E20#");                         /* les blancs de tête sautés, comme une lettre */
}

/* READY sans position valide (L03-03) : la session y entre sans aucun 0x06 (l'échéance de la lecture de la marque), ou y
 * reste quand le flux du 0x06 s'arrête (L03-02) ; elle publie alors position_valid faux et focus_position 0. `f`, lettre
 * figée Pinefeat : `er link pos` (11 caractères ; pas `nc`, `er busy boot`, `er fault`, faux en READY). `:GP#` et `:GN#`
 * sans `:SN`, le repli Moonlite figé sans forme d'erreur : exactement comme hors READY, la dernière position gardée
 * (publiée valide), sinon `0000#` (arbitrage de l'architecte, [D26]). */
static void t_no_position(void)
{
    size_t n0;
    printf("READY sans position valide : f -> er link pos ; :GP# et :GN# sans :SN, la position gardée, sinon 0000#\n");
    ready135();                                     /* 20000 publiée valide, gardée */
    ST.position_valid = false;
    ST.focus_position = 0;
    step();
    n0 = n_cmds;
    ASK("f", "er link pos");
    WIRE(":GP", "4E20#");
    WIRE(":GN", "4E20#");
    fresh();                                        /* aucune position gardée */
    state(SESSION_READY);
    step();
    ASK("f", "er link pos");
    WIRE(":GP", "0000#");
    WIRE(":GN", "0000#");
    CHECK(n_cmds == n0, "rien déposé (%zu)", n_cmds - n0);
    ST.position_valid = true;                       /* le 0x06 revenu */
    ST.focus_position = 16384;
    step();
    ASK("f", "16384");
    WIRE(":GP", "4000#");
}

/* ─────────────────────────── la page (expectFor de la page de banc) ─────────────────────────── */

typedef struct { char key[8]; char re[160]; } pat_t;
static pat_t PAT[32];
static size_t n_pat;
static char PAT_UPPER[160], PAT_MULTI[160];

/* Le motif JavaScript `/…/` de la ligne, en expression POSIX étendue : \d -> [0-9], ou 0-9 dans un ensemble `[…]`
 * (`[\d.]` du motif de `a` et de `o`), \/ -> / ; \b et le reste sont compris par regcomp (glibc). */
static bool js_re(const char *line, char *out, size_t cap)
{
    const char *p = strstr(line, "return /"), *e = p ? strstr(p + 8, "/;") : NULL;
    size_t k = 0;
    bool set = false;                                /* dans un ensemble […] */
    if (!e) return false;
    for (p += 8; p < e && k + 6 < cap; p++) {
        if (p[0] == '\\' && p[1] == 'd') { k += (size_t)snprintf(out + k, cap - k, set ? "0-9" : "[0-9]"); p++; }
        else if (p[0] == '\\' && p[1] == '/') { out[k++] = '/'; p++; }
        else if (p[0] == '\\' && p[1]) { out[k++] = *p++; out[k++] = *p; }
        else {
            if (*p == '[') set = true;
            else if (*p == ']') set = false;
            out[k++] = *p;
        }
    }
    out[k] = 0;
    return p == e;
}

static bool load_page(const char *path)
{
    char buf[4096];
    FILE *f;
    bool in = false, single = false;
    f = fopen(path, "r");
    if (!f) return false;
    while (fgets(buf, sizeof buf, f)) {
        char *s = buf + strspn(buf, " ");
        if (!in) { in = strstr(buf, "function expectFor(cmd){") != NULL; continue; }
        if (!strncmp(buf, "}", 1)) break;
        if (strstr(s, "cmd.length===1")) single = true;
        else if (single && !strncmp(s, "}", 1)) single = false;
        else if (strstr(s, "if(/^[A-Z]/.test(cmd)) return /")) js_re(s, PAT_UPPER, sizeof PAT_UPPER);
        else if (single && !strncmp(s, "if(c==='", 8) && n_pat < sizeof PAT / sizeof PAT[0]) {
            snprintf(PAT[n_pat].key, sizeof PAT[n_pat].key, "%c", s[8]);
            if (js_re(s, PAT[n_pat].re, sizeof PAT[n_pat].re)) n_pat++;
        } else if (!single && !strncmp(s, "return /", 8)) js_re(s, PAT_MULTI, sizeof PAT_MULTI);
    }
    fclose(f);
    return n_pat > 0 && PAT_UPPER[0] && PAT_MULTI[0];
}

/* Le motif que la page applique à la réponse de `cmd` (expectFor) ; NULL : tout lui va. */
static const char *expect_for(const char *cmd)
{
    if (cmd[0] == ':') return NULL;
    if (cmd[0] >= 'A' && cmd[0] <= 'Z') return PAT_UPPER;
    if (strlen(cmd) == 1) {
        for (size_t i = 0; i < n_pat; i++)
            if (PAT[i].key[0] == cmd[0]) return PAT[i].re;
        return NULL;
    }
    return PAT_MULTI;
}

static bool matches(const char *re, const char *s)
{
    regex_t r;
    int ok;
    if (regcomp(&r, re, REG_EXTENDED | REG_NOSUB)) return false;
    ok = regexec(&r, s, 0, NULL, 0) == 0;
    regfree(&r);
    return ok;
}

/* Les lignes que la page envoie (ses `ask(…)`) ; `f20000` pour `'f'+t`, `a5.6` pour `'a'+f`. */
static const char *const PAGE[] = {"v", "DEBUG", "t", "i", "n", "j", "f", "e", "r", "a", "o", "l", "d", "w", "f20000", "f+10", "f-10", "f+100", "f-100",
                                   "c", "h", "q", "b", "p0", "p1", "jg", "js", "a5.6", "LOG ON",
                                   "LOG ALL", "LOG OFF", "DUMP", "DUMP 05", "SDRIVE", "SDRIVE SCAN", "SDRIVE STOP", "SNIFF ON",
                                   "SNIFF OFF"};

static void page_state(const char *name)
{
    for (size_t i = 0; i < sizeof PAGE / sizeof PAGE[0]; i++) {
        const char *re = expect_for(PAGE[i]);
        bsk_status_t keep = ST;
        ask(PAGE[i]);
        CHECK(replied && (!re || matches(re, R)), "%s : « %s » -> « %s », motif de la page %s", name, PAGE[i], R, re ? re : "(aucun)");
        ST = keep;                                   /* une commande de la page ne change pas l'état scripté */
    }
}

static void t_page(const char *page)
{
    printf("la page : chaque réponse aux lignes qu'elle envoie satisfait le motif qu'elle lui applique, dans chaque état\n");
    CHECK(load_page(page), "expectFor lu dans %s (%zu motifs d'une lettre)", page, n_pat);
    CHECK(expect_for("v") && matches(expect_for("v"), V) && matches(expect_for("v"), "1.0") && !matches(expect_for("v"), V "b")
              && !matches(expect_for("v"), "1.0.0b"),
          "v : le motif de la page admet " V " et 1.0, refuse " V "b et 1.0.0b");
    CHECK(expect_for("t") && !matches(expect_for("t"), "fw=2.0.0-dev present=1"), "t : le motif de la page refuse l'ancien t (fw=)");
    CHECK(expect_for("f") && matches(expect_for("f"), "er busy boot") && !matches(expect_for("f"), "ok"),
          "le motif de f admet « er busy boot » et refuse « ok » : il est bien appliqué");
    CHECK(expect_for("a") && matches(expect_for("a"), "1.8-22") && !matches(expect_for("a"), "1.8"),
          "le motif de a, [\\d.]+-[\\d.]+, admet « 1.8-22 » et refuse « 1.8 » : \\d traduit dans un ensemble");
    ready135();
    ASK("v", V);
    page_state("ready");
    ready135();
    ST.position_valid = false;
    ST.motion_state = MOTION_MOVING;
    page_state("ready, sans 0x06, en mouvement");
    ready135();
    RULE = E_BUSY;
    page_state("ready, une commande refusée");
    ready135();
    aperture135();
    page_state("ready, l'ouverture connue");
    fresh();
    page_state("off");
    for (size_t i = 0; i < sizeof SEQ_STATES; i++) {
        fresh();
        state(SEQ_STATES[i]);
        page_state(bsk_state_name(SEQ_STATES[i]));
    }
    fresh();
    state(SESSION_FAULT);
    ST.last_error = E_LOST;
    page_state("fault");
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage : test_host <page de banc> <CMakeLists.txt du firmware>\n");
        return 2;
    }
    signal(SIGALRM, too_long);
    alarm(10);
    CHECK(load_version(argv[2]), "PROJECT_VER lu dans %s", argv[2]);
    printf("v : la version de la carte, PROJECT_VER (« %s »)\n", VERSION);
    t_ready_reads();
    t_ready_commands();
    t_text();
    t_refusals();
    t_aperture();
    t_aperture_text();
    t_aperture_unknown();
    t_states();
    t_state_commands();
    t_last_op();
    t_move_rc();
    t_line_t();
    t_debug();
    t_retired();
    t_ascom();
    t_led_ceiling();
    t_upper();
    t_custom();
    t_moonlite();
    t_no_position();
    t_page(argv[1]);
    printf("couche HOTE, session scriptée : %d vérifications, %d échec(s)\n", checks, fails);
    return fails ? 1 : 0;
}
