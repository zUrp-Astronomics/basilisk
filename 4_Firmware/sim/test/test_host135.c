/* SOURCE: PROTOCOL.md, joué bout à bout contre le faux 135 (sim/lens135.c)
 * AUTHOR: engineer
 * DATE: 2026-09-27
 * STATUS: actif — joué par 4_Firmware/run.sh (job `firmware` de la CI)
 *
 * Des lignes de PROTOCOL.md jouées à travers HOTE, SESSION, MOUVEMENT et le faux 135 : les lettres, le journal, le
 * repli Moonlite, CUSTOM.
 *
 * Tout est le vrai code : la couche HOTE (components/host/host.c), le journal, SESSION, TRANSACTION, bench_core,
 * au-dessus de la PHY simulée et du faux 135 inchangés (sim/phy_sim.c, sim/lens135.c). Le banc fait ce que fait
 * la boucle de app_main : un pas de SESSION, puis bsk_host_observe ; une ligne est jouée entre deux pas, sur la
 * même « tâche », comme bsk_host_usb_service. Le programme est lié avec --wrap=bsk_phy_send (sim/test/programmes.sh) :
 * les trames émises sont notées sans toucher à la PHY simulée. Les bornes attendues sont celles que le faux 135 publie
 * dans son 0x06, offsets 7-10 : 13873 et 30738. */
#define _POSIX_C_SOURCE 200809L   /* alarm, la garde de durée */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bsk_host.h"
#include "bsk_journal.h"
#include "bsk_session.h"
#include "lens_sim_135.h"
#include "phy_common.h"
#include "phy_sim.h"
#include "store_sim.h"

#define MS 1000u
#define DROP (2 * MS)   /* les 2 ms de la retombée, écrites à la main (un mutant de D2_DROP_US rougit) */
#define T0 (1000u * MS)
#define MODEL_MIN "13873"
#define MODEL_MAX "30738"
#define LIM_MIN "13878"                              /* 5 pas en dedans de la borne publiée */
#define LIM_MAX "30733"

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

static l135_t L;
static l135_params_t LP;
static bsk_session_params_t P;
static uint64_t d2_due;

static void run_to(uint64_t t_end)
{
    for (;;) {
        uint64_t t = phy_sim_next(), ts = bsk_session_next();
        if (ts < t) t = ts;
        if (d2_due > phy_sim_now() && d2_due < t) t = d2_due;   /* l'anti-rebond de D2, que phy_sim_next ne compte pas */
        if (t > t_end) t = t_end;
        phy_sim_run(t);
        bsk_session_step(t);
        bsk_host_observe();                          /* app_main : après chaque pas */
        if (t >= t_end) break;
    }
}

static void run_for(uint64_t us) { run_to(phy_sim_now() + us); }

/* Le dernier 0x03 émis, type et offsets 0-6 (la consigne d'ouverture : offsets 3-6). */
static uint8_t last03[8];

/* Les trames émises hors de la boucle 0x03/0x04. */
static unsigned n_once;

/* La dernière trame 0x40 émise, ses 19 octets. */
static uint8_t last40[19];

bsk_err_t __real_bsk_phy_send(const bsk_frame_t *f);
bsk_err_t __wrap_bsk_phy_send(const bsk_frame_t *f);

bsk_err_t __wrap_bsk_phy_send(const bsk_frame_t *f)
{
    if (f->msg[0] == 0x03 && f->len >= sizeof last03) memcpy(last03, f->msg, sizeof last03);
    if (f->msg[0] != 0x03 && f->msg[0] != 0x04) n_once++;
    if (f->msg[0] == 0x40 && f->len >= sizeof last40) memcpy(last40, f->msg, sizeof last40);
    return __real_bsk_phy_send(f);
}

/* Le dernier 0x03 porte `code` aux offsets 3-4 et 5-6, petit-boutiste. */
static bool last03_is(uint16_t code)
{
    return last03[0] == 0x03 && last03[4] == (uint8_t)code && last03[5] == code >> 8 && last03[6] == (uint8_t)code &&
           last03[7] == code >> 8;
}

static char R[BSK_HOST_REPLY_MAX];

static const char *ask(const char *line)
{
    if (!bsk_host_line(line, R, sizeof R)) R[0] = 0;
    return R;
}

#define ASK(line, want) CHECK(!strcmp(ask(line), want), "« %s » -> « %s », attendu « %s »", line, R, want)

/* La réponse à `line` (`t`, `DEBUG`) contient ` key=value` (ou commence par `key=value `). */
static bool has(const char *line, const char *kv)
{
    char pat[120];
    ask(line);
    snprintf(pat, sizeof pat, " %s ", kv);
    if (!strncmp(R, pat + 1, strlen(pat + 1))) return true;
    if (strstr(R, pat)) return true;
    snprintf(pat, sizeof pat, " %s", kv);
    return strlen(R) >= strlen(pat) && !strcmp(R + strlen(R) - strlen(pat), pat);
}

#define T_HAS(kv) CHECK(has("t", kv), "t porte %s : « %s »", kv, R)
#define D_HAS(kv) CHECK(has("DEBUG", kv), "DEBUG porte %s : « %s »", kv, R)   /* move_rc et dropped : dans DEBUG, pas dans t */

/* La carte à zéro, le 135 hors tension, D2 absent ; puis monté, READY. */
static void bench(void)
{
    l135_params_default(&LP);
    l135_init(&L, &LP);
    phy_sim_init(lens_sim_135(&L), 0);
    bsk_session_params_default(&P);
    store_sim_reset();                               /* aucune marque rangée */
    bsk_session_init(&P);
    bsk_journal_init(phy_sim_now);
    bsk_journal_set(false, false);
    bsk_host_init("1.0", "poweron");
    d2_due = 0;
    run_to(T0);
}

static void d2(bool present)
{
    phy_sim_d2(present);
    d2_due = phy_sim_now() + D2_DEBOUNCE_US;
}

static void mounted(void)
{
    bench();
    l135_power(&L, phy_sim_now(), true);
    d2(true);
    run_for(4000 * MS);
}

/* ─────────────────────────── le journal, vidé comme la tâche de la carte ─────────────────────────── */

static char JL[2000][400];
static size_t n_jl;

static size_t jdrain(void)
{
    char b[sizeof JL[0]];
    uint8_t g;
    size_t n;
    n_jl = 0;
    while ((n = bsk_journal_take(b, sizeof b - 1, &g)) > 0) {
        b[n] = 0;
        if (g != bsk_journal_gen()) continue;        /* périmée par LOG OFF : la tâche ne l'écrit pas (host_usb.c) */
        if (n_jl < sizeof JL / sizeof JL[0]) memcpy(JL[n_jl], b, n + 1);
        n_jl++;
    }
    return n_jl;
}

static long jfind(const char *pre, const char *in)
{
    for (size_t i = 0; i < n_jl && i < sizeof JL / sizeof JL[0]; i++)
        if (!strncmp(JL[i], pre, strlen(pre)) && (!in || strstr(JL[i], in))) return (long)i;
    return -1;
}

/* ─────────────────────────── les tests ─────────────────────────── */

static void t_boot(void)
{
    printf("démarrage : v, t (present, boot_state, last_op), r (les bornes du faux 135), i\n");
    bench();
    ASK("v", "1.0");
    T_HAS("present=0");
    T_HAS("boot_state=off");
    T_HAS("last_op=-:ok");
    ASK("f", "nc");
    ASK("c", "nc");                                  /* comme f<n> */
    ASK("b", "er nolens");                           /* D2 absent, pas de p0 */
    l135_power(&L, phy_sim_now(), true);
    d2(true);
    run_for(400 * MS);
    T_HAS("present=1");
    T_HAS("busy=boot");
    ASK("f", "er busy boot");
    ASK("c", "er busy boot");                        /* comme f<n> */
    ASK("e", "y");
    run_for(3600 * MS);
    ASK("v", "1.0");
    T_HAS("present=1");
    T_HAS("boot_state=ready");
    T_HAS("last_op=boot:ok");
    T_HAS("busy=-");
    T_HAS("ext=1");                                  /* les bornes et le nom ne sont pas dans t : r, i */
    ASK("r", LIM_MIN "-" LIM_MAX);
    ASK("i", "SAMYANG AF 135mm F1.8");
    ASK("e", "n");
}

static void t_goto(void)
{
    printf("f<n> -> ok, e -> y puis n, f -> la cible, DEBUG -> move_rc=ok, g -> ok ; hors des bornes : er range limits\n");
    mounted();
    D_HAS("move_rc=-");
    ASK("g", "-");
    ASK("f20000", "ok");
    ASK("e", "y");
    ASK("f25000", "er busy");                        /* un goto en vol, M2 */
    ASK("b", "er busy move");                        /* b pendant un déplacement */
    ASK("p0", "er busy");
    run_for(3000 * MS);
    ASK("e", "n");
    ASK("f", "20000");
    D_HAS("move_rc=ok");
    ASK("g", "ok");
    T_HAS("last_op=boot:ok");

    ASK("f" MODEL_MIN, "er range limits");           /* la borne basse publiée, refusée : 5 pas en dedans */
    ASK("f" MODEL_MAX, "er range limits");
    ASK("f" LIM_MIN, "ok");                          /* la borne basse du 0x06 + 5 */
    run_for(3000 * MS);
    ASK("f", LIM_MIN);
    D_HAS("move_rc=ok");

    ASK("f13877", "er range limits");                /* sous la borne basse : E_LIMIT */
    ASK("f-1", "er range limits");
    ASK("f+60000", "er range 65535");                /* le résultat sort de 0..65535 */
    ASK("f-13879", "er range 65535");                /* -1 */
    ASK("f+50000", "er range limits");               /* 63878 : dans 0..65535, hors des bornes */
    ASK("f0", "er range limits");
    ASK("f30734", "er range limits");                /* au-delà de la borne haute */
    ASK("f65536", "er range 65535");                 /* le texte, jugé par la couche HOTE */
    ASK("e", "n");
    ASK("f", LIM_MIN);                               /* rien n'a bougé */
    ASK("f+500", "ok");
    run_for(2000 * MS);
    ASK("f", "14378");
    ASK("f" LIM_MAX, "ok");
    run_for(3000 * MS);
    ASK("f", LIM_MAX);
    D_HAS("move_rc=ok");
}

static void t_stop(void)
{
    printf("q pendant un goto -> ok, puis move_rc=aborted, g -> aborted ; le 135 bloqué : g -> stall\n");
    mounted();
    ASK("f30000", "ok");
    run_for(150 * MS);
    ASK("e", "y");
    ASK("q", "ok");
    ASK("e", "n");
    D_HAS("move_rc=aborted");
    ASK("g", "aborted");
    run_for(1000 * MS);
    D_HAS("move_rc=aborted");
    ASK("g", "aborted");
    ASK("q", "ok");                                  /* q sans mouvement : ok */
    ASK("g", "aborted");                             /* rien à finir : inchangé */

    mounted();
    ASK("f30000", "ok");
    run_for(3000 * MS);
    ASK("g", "ok");
    L.f.frozen_position = true;                      /* le faux 135 bloqué : STALLED à 3 s (t_goto_blocked de test_session135) */
    ASK("f20000", "ok");
    run_for(2000 * MS);
    ASK("g", "ok");                                  /* en vol : le résultat du précédent */
    run_for(3000 * MS);
    D_HAS("move_rc=stall");
    ASK("g", "stall");
    L.f.frozen_position = false;
}

/* Décision de l'humain : le homing n'a lieu qu'au démarrage ; `h` est une ligne inconnue, et `c` répond `ok` sans rien
 * émettre ; le driver Pinefeat voit `e` = `n` et relit `r`. */
static void t_home(void)
{
    unsigned n0;
    printf("h -> er nocap, c -> ok sans rien émettre, rien ne bouge, toujours ready ; cm -> er nocap\n");
    mounted();
    ASK("f20000", "ok");
    run_for(2000 * MS);
    ASK("h", "er nocap");
    ASK("e", "n");
    n0 = n_once;
    ASK("c", "ok");
    run_for(1000 * MS);                              /* le driver Pinefeat attend au moins 1 s avant de lire e */
    CHECK(n_once == n0, "c : %u trame(s) émise(s) hors de la boucle 0x03/0x04, attendu aucune", n_once - n0);
    ASK("e", "n");
    ASK("r", LIM_MIN "-" LIM_MAX);
    ASK("cm", "er nocap");
    run_for(2000 * MS);
    ASK("e", "n");
    ASK("f", "20000");
    T_HAS("boot_state=ready");
    T_HAS("last_op=boot:ok");
}

/* L'ouverture contre le faux 135. Sa plage, réponse au 0x08 des traces, BF 11 / 00 19 (SRC_R08,
 * full:15) : f/1,83 et f/22,6, soit 1.8-22 ; son 0x05 est figé à B2 11 (SRC_T05, dump05:7), f/1,8 : `o` relit
 * l'objectif, pas la consigne. Après a5.6, le 0x03 porte le code de f/5,6, round(256 × (2·log2(5,6) + 16)) = 5369 =
 * 0x14F9, calculé à la main. */
static void t_aperture(void)
{
    printf("ouverture : a -> 1.8-22, 0x03 à BF 11 BF 11 ; a5.6 -> ok, F9 14 F9 14 ; o -> 1.8 (relue) ; refus ; b -> f/1,8\n");
    mounted();
    CHECK(last03_is(0x11BF), "READY : le 0x03 porte BF 11 BF 11, f/1,8 (std.c:123, B2 11) bornée à la plage, BF 11");
    ASK("a", "1.8-22");
    ASK("o", "1.8");
    ASK("a5.6", "ok");
    run_for(100 * MS);
    CHECK(last03_is(0x14F9), "après a5.6 : le 0x03 porte F9 14 F9 14");
    ASK("o", "1.8");                                 /* le 0x05 du faux, figé : l'ouverture relue */
    ASK("a1.7", "er range ap");
    ASK("a22.1", "er range ap");
    ASK("anan", "er range num");
    ASK("a", "1.8-22");
    ASK("f20000", "ok");
    ASK("a4", "er busy");                            /* un goto en vol */
    run_for(3000 * MS);
    CHECK(last03_is(0x14F9), "la consigne refusée n'a rien changé");
    ASK("b", "ok");
    run_for(4000 * MS);
    T_HAS("boot_state=ready");
    CHECK(last03_is(0x11BF), "nouvelle session : la consigne revient à f/1,8, bornée à la plage, BF 11");
}

static void t_power(void)
{
    printf("p0 -> ok, t -> present=0, b -> ok (maintien levé), puis ready ; D2 absent : b -> er nolens\n");
    mounted();
    ASK("p0", "ok");
    T_HAS("present=0");
    T_HAS("boot_state=off");
    run_for(3000 * MS);
    T_HAS("present=0");                              /* le maintien de p0 tient, D2 présent */
    ASK("f", "nc");
    ASK("b", "ok");                                  /* b lève le maintien de p0 */
    run_for(4000 * MS);
    T_HAS("boot_state=ready");
    T_HAS("last_op=boot:ok");
    ASK("p0", "ok");
    ASK("p1", "ok");                                 /* p1 aussi lève le maintien de p0 */
    run_for(4000 * MS);
    T_HAS("boot_state=ready");
    d2(false);
    phy_sim_run(phy_sim_now() + DROP);         /* la coupure, 2 ms après, dans la PHY seule */
    ask("b");                                        /* avant le pas de SESSION qui voit la retombée */
    {
        const phy_sim_wires_t *w = phy_sim_wires();
        CHECK(!w->rail[BSK_RAIL_LOGIC] && !w->rail[BSK_RAIL_MOTOR] && w->txd == PHY_SIM_REST && w->body_cs == PHY_SIM_REST &&
                  w->vd == PHY_SIM_REST,
              "b juste après la coupure : le verrou refuse, rails coupés, lignes et VD au repos");
    }
    run_for(400 * MS);
    T_HAS("present=0");
    T_HAS("boot_state=off");
    CHECK(!phy_sim_wires()->rail[BSK_RAIL_LOGIC], "400 ms après : toujours coupé");
    ASK("b", "er nolens");                           /* OFF sans p0 : D2 absent */
    ASK("p1", "ok");                                 /* ok, sans rien sur le fil */
    T_HAS("present=0");
}

static void t_fault(void)
{
    printf("FAULT : e -> n, f -> er fault, ms/mx/mg -> er fault, last_op=boot:er:\"lost\" ; b -> ready\n");
    bench();
    L.f.silent = true;
    l135_power(&L, phy_sim_now(), true);
    d2(true);
    run_for(30000 * MS);
    T_HAS("boot_state=fault");
    T_HAS("last_op=boot:er:\"lost\"");
    T_HAS("present=1");
    ASK("e", "n");
    ASK("f", "er fault");
    ASK("f100", "er fault");
    ASK("h", "er nocap");                            /* une ligne inconnue, dans tout état */
    ASK("c", "er fault");                            /* comme f<n> */
    ASK("js", "er fault");
    ASK("jx", "er fault");
    ASK("jg", "er fault");
    ASK("m16000", "er fault");                       /* comme f<n> */
    ASK("i", "er nolens");                           /* muet : aucune identité */
    L.f.silent = false;
    ASK("b", "ok");                                  /* CMD_CLEAR_FAULT, puis le démarrage */
    run_for(5000 * MS);
    T_HAS("boot_state=ready");
    T_HAS("last_op=boot:ok");
}

/* Les marques servies par la session (PROTOCOL.md, ligne `j`) ; le goto du bouton du fût (appui de 300 ms, le faux 135 le
 * publie) suivi par move_rc comme un f<n> : après un goto arrêté par q (aborted), ok à son arrivée. `m<n>` va à n comme
 * `f<n>`, sans toucher la marque ; `m`, `ms`, `mg`, `mx` sont des lignes inconnues. */
static void t_marks(void)
{
    printf("marques en READY : j, js, jg, jx ; m<n> un goto ; le goto du bouton dans move_rc\n");
    mounted();
    ASK("j", "-");
    ASK("jg", "er range nomark");
    ASK("m16340", "ok");
    run_for(3000 * MS);
    ASK("f", "16340");
    ASK("j", "-");                                   /* m<n> ne pose pas la marque */
    ASK("m13000", "er range limits");                /* comme f13000 : hors des bornes de r */
    ASK("js", "ok");
    ASK("j", "16340");
    ASK("f20000", "ok");
    run_for(3000 * MS);
    ASK("jg", "ok");
    run_for(3000 * MS);
    ASK("f", "16340");
    ASK("m20000", "ok");
    run_for(3000 * MS);
    ASK("f", "20000");
    ASK("js", "ok");
    ASK("j", "20000");
    ASK("jx", "ok");
    ASK("j", "-");
    for (const char *const *l = (const char *const[]){"m", "ms", "mg", "mx", "u", "ux", "uf", "ua", NULL}; *l; l++)
        ASK(*l, "er nocap");
    ASK("j", "-");
    ASK("f16340", "ok");
    run_for(3000 * MS);
    ASK("js", "ok");
    ASK("f25000", "ok");
    run_for(100 * MS);
    ASK("q", "ok");
    run_for(500 * MS);
    D_HAS("move_rc=aborted");
    L.in.button = true;
    run_for(300 * MS);
    L.in.button = false;
    run_for(3000 * MS);
    ASK("f", "16340");
    D_HAS("move_rc=ok");
}

static void t_journal(void)
{
    uint32_t d0;
    unsigned n = 0;
    printf("journal : LOG ON puis un goto -> * tx et * rx ; LOG OFF -> plus rien ; LOG ALL : chaque trame, rien de tu, dropped\n");
    mounted();
    ASK("LOG ON", "ok log=1 all=0");
    jdrain();
    ASK("f25000", "ok");
    run_for(3000 * MS);
    jdrain();
    CHECK(jfind("* tx ", " 1D ") >= 0, "* tx : le 0x04 qui porte la consigne 0x1D");
    CHECK(jfind("* rx ", NULL) >= 0, "* rx : l'accusé du 0x1D derrière le 0x06");
    CHECK(jfind("* motion ", " arrived") >= 0, "* motion <t> arrived");
    ASK("LOG OFF", "ok log=0 all=0");
    ASK("f20000", "ok");
    run_for(3000 * MS);
    CHECK(jdrain() == 0, "LOG OFF : plus rien (%zu)", n_jl);

    ask("DEBUG");
    CHECK(sscanf(strstr(R, " dropped=") ? strstr(R, " dropped=") : "", " dropped=%u", &n) == 1 && n == 0, "dropped=0 (%u)", n);
    d0 = bsk_journal_dropped();
    ASK("LOG ALL", "ok log=1 all=1");
    run_for(1000 * MS);
    CHECK(jdrain() >= 240 && n_jl < 300, "LOG ALL, une seconde de boucle (240 trames) : chacune, hors du plafond (%zu lignes)", n_jl);
    CHECK(bsk_journal_dropped() == d0, "rien de tu (%lu)", (unsigned long)(bsk_journal_dropped() - d0));
    ask("DEBUG");
    CHECK(sscanf(strstr(R, " dropped=") ? strstr(R, " dropped=") : "", " dropped=%u", &n) == 1 && n == bsk_journal_dropped(),
          "DEBUG -> dropped=%u", n);
    ASK("LOG OFF", "ok log=0 all=0");
}

/* Le faux 135 démarré, la marque 30000 rangée (sens inconnu ; la clé du 135 calculée à la main) :
 * en RESTORING, en mouvement vers elle (de 16384, la pose de fin de homing du faux 135, à 30200 d'abord). */
static void restoring_135(void)
{
    bsk_status_t st = {0};
    bench();
    store_sim_poke("01030800bf087", 30000);
    l135_power(&L, phy_sim_now(), true);
    d2(true);
    for (int k = 0; k < 6000 && st.session_state != SESSION_RESTORING; k++) {
        run_for(1 * MS);
        bsk_session_status(&st);
    }
    run_for(100 * MS);
}

/* En RESTORING, les lettres qui ont besoin de l'objectif répondent `er busy boot`, `r` compris ; `e` = `y` ; `t` dit
 * restoring. `q` arrête le retour : `ok`, READY, la position lue. `b` redémarre la session : le retour n'est pas une
 * commande en vol (le refus de `b` pendant un déplacement ne s'applique pas). Le retour n'est pas un déplacement de
 * l'hôte : `move_rc` n'en dit rien. */
static void t_restore(void)
{
    static const char *const busy[] = {"f", "f20000", "f+10", "r", "d", "a", "a5.6", "o", "i", "j", "js", "m16000", "jg", "jx", "p0"};
    int p;
    printf("RESTORING : lettres -> er busy boot, e -> y, t -> restoring ; q -> ok, ready ; b -> ok\n");
    restoring_135();
    T_HAS("boot_state=restoring");
    T_HAS("busy=boot");
    ASK("e", "y");
    for (size_t k = 0; k < sizeof busy / sizeof busy[0]; k++) ASK(busy[k], "er busy boot");
    T_HAS("boot_state=restoring");
    ASK("q", "ok");
    T_HAS("boot_state=ready");
    ASK("e", "n");
    p = atoi(ask("f"));
    CHECK(p > 16384 && p < 30200, "f : arrêté en route (« %s »)", R);
    T_HAS("last_op=boot:ok");
    D_HAS("move_rc=-");
    run_for(500 * MS);                               /* le faux 135 ralentit après le 0x1C */
    p = atoi(ask("f"));
    run_for(2000 * MS);
    CHECK(atoi(ask("f")) == p && p < 30200, "q : le retour ne reprend pas (« %s »)", R);

    restoring_135();
    ASK("b", "ok");
    T_HAS("boot_state=powering");
    run_for(15000 * MS);
    T_HAS("boot_state=ready");
    ASK("f", "30000");
    ASK("j", "30000");
    D_HAS("move_rc=-");
}

/* ─────────────────────────── Moonlite ─────────────────────────── */

/* Ce que le transport écrit, comme host_usb.c : la réponse et ce que HOTE dit d'écrire après elle ; "" : rien. */
static char W[BSK_HOST_REPLY_MAX + 2];

static const char *wire(const char *line)
{
    const char *tail = bsk_host_line(line, R, sizeof R);
    snprintf(W, sizeof W, "%s%s", tail ? R : "", tail ? tail : "");
    return W;
}

#define WIRE(line, want) CHECK(!strcmp(wire(line), want), "« %s » -> « %s », attendu « %s »", line, W, want)

static void t_moonlite(void)
{
    bsk_status_t st;
    long p;
    size_t steps = 0, moved = 0, partial = 0;
    printf("Moonlite contre le faux 135 : :SN, :FG#, :GI#, :FQ#, :GP# ; :GP# gardée pendant le redémarrage de b, 0000# au retrait\n");
    mounted();
    WIRE(":GP", "4000#");                            /* 16384, la pose du faux 135 après son init (POS_REST) */
    WIRE(":GN", "4000#");                            /* sans :SN, la position */
    WIRE(":SN5000", "");
    WIRE(":FG", "");
    run_for(100 * MS);
    WIRE(":GI", "01#");
    run_for(3000 * MS);
    WIRE(":GI", "00#");
    WIRE(":GP", "5000#");                            /* 20480 */
    WIRE("f", "20480\n");
    WIRE(":SN3000", "");                             /* 12288 : sous la borne basse, 13878 */
    WIRE(":FG", "");
    run_for(3000 * MS);
    WIRE(":GP", "5000#");                            /* refusé : rien n'a bougé */
    WIRE(":SN6000", "");                             /* 24576 */
    WIRE(":FG", "");
    run_for(150 * MS);
    WIRE(":FQ", "");
    run_for(2000 * MS);
    WIRE(":GI", "00#");
    p = strtol(wire(":GP"), NULL, 16);
    CHECK(p > 0x5000 && p < 0x6000 && W[4] == '#' && !W[5], ":FQ# : arrêté en route (« %s »)", W);
    CHECK(!strcmp(ask("e"), "n"), "e : n après :FQ#");
    wire(":GP");
    {
        static char kept[sizeof W];
        memcpy(kept, W, sizeof W);
        ASK("b", "ok");
        for (;;) {
            run_for(1 * MS);
            bsk_session_status(&st);
            if (st.session_state == SESSION_READY || ++steps > 20000) break;
            if (st.lens_id_product && !st.lens_name[0]) partial++;
            if (strcmp(wire(":GP"), kept)) moved++;
        }
        CHECK(st.session_state == SESSION_READY && steps > 0 && !moved,
              "b : :GP# rend la dernière position valide pendant tout le redémarrage (« %s » ; %zu pas, %zu autres)", kept, steps, moved);
        CHECK(partial > 0, "le redémarrage passe par le 0x07 publié avant le nom : pas un changement d'identité (%zu pas)", partial);
    }
    WIRE(":GP", "4000#");                            /* READY : la position publiée, de nouveau à la pose */
    d2(false);
    run_for(400 * MS);
    T_HAS("boot_state=off");
    WIRE(":GP", "0000#");                            /* retiré : oubliée */
    WIRE(":GN", "6000#");                            /* la cible survit */
    WIRE(":GI", "00#");
}

/* Une ligne dont la réponse attend l'objectif, servie comme host_usb.c : BSK_HOST_LATER, puis bsk_host_later
 * après chaque pas (1 ms, la boucle de app_main), 3 s au plus. */
static const char *custom(const char *line)
{
    const char *tail = bsk_host_line(line, R, sizeof R);
    for (int i = 0; tail == BSK_HOST_LATER && i < 3000; i++) {
        run_for(1 * MS);
        tail = bsk_host_later(R, sizeof R);
    }
    if (tail == BSK_HOST_LATER) snprintf(R, sizeof R, "(pas de réponse en 3 s)");
    return R;
}

#define CUSTOM(line, want) CHECK(!strcmp(custom(line), want), "« %s » -> « %s », attendu « %s »", line, R, want)

/* La dernière trame 0x40 émise : 0x40 'P' sub d, puis quinze 00 (écrits à la main). */
static bool last40_is(uint8_t sub, uint8_t d)
{
    uint8_t want[19] = {0x40, 'P', sub, d};
    return !memcmp(last40, want, sizeof want);
}

#define ZEROS "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"

static void t_custom(void)
{
    printf("CUSTOM contre le faux 135 : lue (10 d'usine), écrite, relue, gardée quand l'objectif est rebranché\n");
    mounted();
    memset(last40, 0, sizeof last40);
    CUSTOM("CUSTOM READ", "ok 00 00 00 00 00 00 00 10 00 00 00 00 00 00 00 00");   /* samyang.md § 6.3, d'usine § 5.2 */
    CHECK(last40_is(0xFA, 0x00), "READ : 40 50 FA 00 émis");
    CUSTOM("CUSTOM WRITE 02", "ok " ZEROS);                                        /* l'écho, samyang.md § 6.3 */
    CHECK(last40_is(0x38, 0x32), "WRITE 02 : 40 50 38 32 émis");
    CUSTOM("CUSTOM READ", "ok 00 00 00 00 00 00 00 02 00 00 00 00 00 00 00 00");
    ASK("f", "16384");                               /* la ligne suivante est servie : rien n'attend plus */
    d2(false);
    run_for(400 * MS);
    l135_power(&L, phy_sim_now(), false);
    run_for(500 * MS);
    l135_power(&L, phy_sim_now(), true);
    d2(true);
    run_for(4000 * MS);
    T_HAS("boot_state=ready");
    CUSTOM("CUSTOM READ", "ok 00 00 00 00 00 00 00 02 00 00 00 00 00 00 00 00");   /* gardée en flash, samyang.md § 5.2 */
    CUSTOM("CUSTOM WRITE 3", "er range CUSTOM READ|WRITE <0-2><0-2>");
    CUSTOM("CUSTOM WRITE 10", "ok " ZEROS);                                        /* le réglage d'usine */
    CUSTOM("CUSTOM READ", "ok 00 00 00 00 00 00 00 10 00 00 00 00 00 00 00 00");
    d2(false);
    run_for(400 * MS);
    CUSTOM("CUSTOM READ", "er nolens");
}

int main(void)
{
    signal(SIGALRM, too_long);
    alarm(10);
    t_boot();
    t_goto();
    t_stop();
    t_home();
    t_aperture();
    t_power();
    t_fault();
    t_marks();
    t_journal();
    t_restore();
    t_moonlite();
    t_custom();
    printf("HOTE contre le faux 135 : %d vérifications, %d échec(s)\n", checks, fails);
    return fails ? 1 : 0;
}
