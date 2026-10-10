/* SOURCE: 7_Docs/E-Mount/samyang.md et protocol.md, traces du 135 de l'humain — le faux 135 contre ce qu'ils décrivent
 * AUTHOR: engineer
 * DATE: 2026-09-26
 * STATUS: actif — joué par 4_Firmware/run.sh (job `firmware` de la CI)
 *
 * Le faux 135 contre samyang.md : comportements, pannes reçues et émises, son adaptateur à sim/lens_sim.h.
 *
 * Chaque test défend un comportement que samyang.md ou protocol.md décrit (« samyang.md § n », « protocol.md § n » :
 * sections de 7_Docs/E-Mount/), que l'analyse statique privée du firmware 1.06 établit (dit tel), ou d'une trace, et ne conclut que sur ce
 * qui sort de la frontière PHY (bsk_phy.h) : trames, erreurs, LENS_CS. Les seuls regards dans le modèle sont
 * ses signaux déclarés (bloqué, hors modèle, non modélisé, réponses jetées), visibles pour un test. */
#include <stdio.h>
#include <string.h>

#include "bench.h"
#include "frame.h"
#include "lens_sim_135.h"
#include "phy_sim.h"
#include "sources135.h"

#define MS 1000u
#define T0 (1000u * MS)

static l135_t L;
static l135_params_t P;
static int checks, fails;

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

/* ─────────────────────────── mise en place ─────────────────────────── */

static void fresh(void)
{
    l135_init(&L, &P);
    bench_init(lens_sim_135(&L), T0);
    phy_sim_mounted();                       /* sans lui, le verrou de XDETECT refuse la VD */
    (void)bsk_phy_lines(true);               /* relâchées, BODY_CS et l'émission n'arrivent pas à l'objectif */
}

/* Mise sous tension et poignée de main par la carte (protocol.md § 2). */
static void power_and_handshake(void)
{
    fresh();
    l135_power(&L, phy_sim_now(), true);
    bench_run_for(30 * MS);
    bsk_phy_body_cs(true);
    bench_run_for(3 * MS);
    bsk_phy_body_cs(false);
    bench_run_for(3 * MS);
}

/* Objectif resté alimenté (samyang.md § 2.7) : liaison établie, en flux ou non, VD à 60 Hz, boucle ou non. */
static void powered(bool flow, bool service, bool vd, bool loop)
{
    fresh();
    l135_start_powered(&L, phy_sim_now(), flow, service);
    bsk_phy_vd(vd ? 60 : 0);
    bench_loop(loop);
    bench_run_for(100 * MS);
}

static long request(const uint8_t *m, size_t n, uint64_t wait_us)
{
    size_t from = g_bench.n;
    bench_send(2, m, n);
    bench_run_for(wait_us);
    return bench_find(from, m[0], m[0] == 0x40 ? m[1] : 0, m[0] == 0x40 ? m[2] : 0);
}

static bool same_msg(long i, const uint8_t *m, size_t n)
{
    return i >= 0 && BENCH_EV(i).frame.len == n && memcmp(BENCH_EV(i).frame.msg, m, n) == 0;
}

static bool same_as_source(long i, src_id_t id)
{
    uint8_t m[BSK_MSG_MAX];
    size_t n = sources135_msg(id, m, sizeof m);
    return n && same_msg(i, m, n);
}

static int32_t pos06(long i) { return BENCH_EV(i).frame.msg[3] | BENCH_EV(i).frame.msg[4] << 8; }

static const uint8_t Q01[33] = {0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01}; /* full:8 */
static const uint8_t Q07[] = {0x07, 0x00};                                             /* full:11 */
static const uint8_t Q08[] = {0x08, 0x02, 0, 0, 0, 0, 0, 0, 0};                         /* full:14 */
static const uint8_t Q0B[] = {0x0B, 0x60, 0x00};                                       /* full:20 */
static const uint8_t Q09[] = {0x09, 0x00, 0x00, 0x00, 0x00};                           /* full:22 */
static const uint8_t Q0D[] = {0x0D, 0x00};                                             /* full:24 */
static const uint8_t Q10[] = {0x10, 0x1F};                                             /* full:27 */
static const uint8_t Q0A[17] = {0x0A, 0xFF, 0x7F, 0, 0, 0, 0, 0, 0, 0x3F};              /* full:38 */
static const uint8_t Q0A_OFF[17] = {0x0A};
static const uint8_t Q3F[] = {0x3F, 0x00};
static const uint8_t QV[19] = {0x40, 'V', 0x00};                                       /* full:42 */
static const uint8_t QM[19] = {0x40, 'M', 0x00};                                       /* full:52 */
static const uint8_t QM31[19] = {0x40, 'M', 0x31};
static const uint8_t QFA[19] = {0x40, 'F', 0xFA};                                      /* full:167 */
static const uint8_t QF32[19] = {0x40, 'F', 0x32};                                     /* reboots:141 */

/* ─────────────────────────── protocol.md § 2 mise sous tension ─────────────────────────── */

static void t_power_on(void)
{
    long i;
    size_t from;
    printf("protocol.md § 2 poignée de main de mise sous tension\n");
    fresh();
    l135_power(&L, phy_sim_now(), true);
    bench_run_for(30 * MS);
    i = request(Q07, sizeof Q07, 50 * MS);
    CHECK(i < 0, "avant la poignée de main, l'UART est fermée : aucune réponse");
    from = g_bench.n;
    bsk_phy_body_cs(true);
    bench_run_for(3 * MS);
    CHECK(bench_count_kind(from, BSK_PHY_LENS_CS) == 1 && BENCH_EV(g_bench.n - 1).level,
          "BODY_CS haute plus d'une milliseconde : LENS_CS levée");
    bsk_phy_body_cs(false);
    bench_run_for(3 * MS);
    CHECK(!BENCH_EV(g_bench.n - 1).level, "BODY_CS basse plus d'une milliseconde : LENS_CS rabaissée");
    i = request(Q07, sizeof Q07, 50 * MS);
    CHECK(same_as_source(i, SRC_R07), "liaison établie : le 0x07 est servi (full:12)");
}

/* protocol.md § 2 étape 3 : l'UART n'ouvre que la ligne d'entrée haute plus d'une milliseconde après la levée de
 * LENS_CS. Une trame reçue dans cette milliseconde est perdue. */
static void t_handshake_uart(void)
{
    uint8_t q07[16];
    size_t n07 = fr_encode(2, 0x3D, Q07, sizeof Q07, q07, sizeof q07);
    size_t from;
    long up = -1;
    printf("protocol.md § 2 l'UART n'ouvre qu'une milliseconde après LENS_CS\n");
    fresh();
    l135_power(&L, phy_sim_now(), true);
    bench_run_for(30 * MS);
    from = g_bench.n;
    bsk_phy_body_cs(true);
    bench_run_for(1300);
    for (size_t k = from; k < g_bench.n; k++)
        if (BENCH_EV(k).kind == BSK_PHY_LENS_CS && BENCH_EV(k).level) up = (long)k;
    CHECK(up >= 0 && phy_sim_now() - BENCH_EV(up).t < 1000, "LENS_CS levée depuis moins d'une milliseconde");
    from = g_bench.n;
    phy_sim_raw(q07, n07);                         /* finie moins d'une milliseconde après LENS_CS */
    bench_run_for(100 * MS);
    CHECK(bench_count(from, 0x07) == 0, "0x07 reçu avant l'ouverture de l'UART : perdu, sans réponse");
    CHECK(bench_count_kind(from, BSK_PHY_LENS_CS) >= 1 && !BENCH_EV(g_bench.n - 1).level,
          "la poignée de main s'achève : LENS_CS rabaissée");
    from = g_bench.n;
    phy_sim_raw(q07, n07);
    bench_run_for(30 * MS);
    CHECK(bench_count(from, 0x07) == 1, "UART ouverte : le 0x07 suivant est servi");
}

/* ─────────────────────────── samyang.md § 2.7 objectif resté alimenté ─────────────────────────── */

static void t_stayed_powered(void)
{
    long i;
    size_t from;
    printf("samyang.md § 2.7 objectif resté alimenté\n");
    powered(true, true, true, false);
    from = g_bench.n;
    bsk_phy_body_cs(true);
    bench_run_for(20 * MS);
    bsk_phy_body_cs(false);
    bench_run_for(5 * MS);
    CHECK(bench_count_kind(from, BSK_PHY_LENS_CS) == 0, "la poignée de main ne reçoit rien : LENS_CS ne se lève pas");
    i = request(Q01, sizeof Q01, 30 * MS);
    CHECK(same_as_source(i, SRC_R01), "les messages d'init sont servis en plein flux (full:9)");
    from = g_bench.n;
    bench_loop(true);
    bench_run_for(100 * MS);
    CHECK(bench_count(from, 0x05) >= 5 && bench_count(from, 0x06) >= 5, "il est peut-être encore en flux : 0x05/0x06");
}

/* ─────────────────────────── samyang.md § 1.3 réponses d'init ─────────────────────────── */

static void t_init_replies(void)
{
    static const uint8_t NAME[66] = {0x3F, 0x00, 'S', 'A', 'M', 'Y', 'A', 'N', 'G', ' ', 'A', 'F', ' ', '1', '3', '5', 'm', 'm', ' ', 'F', '1', '.', '8'};
    uint8_t q0b[3] = {0x0B, 0x42, 0x00};
    uint8_t r0b[3] = {0x0B, 0x42, 0x00};
    long i;
    printf("samyang.md § 1.3 réponses immédiates de l'init\n");
    power_and_handshake();
    i = request(Q01, sizeof Q01, 30 * MS);
    CHECK(same_as_source(i, SRC_R01) && BENCH_EV(i).frame.cls == 2 && BENCH_EV(i).frame.seq == 0, "0x01 : full:9, classe 2, séquence 0");
    CHECK(same_as_source(request(Q07, sizeof Q07, 30 * MS), SRC_R07), "0x07 : full:12");
    CHECK(same_as_source(request(Q08, sizeof Q08, 30 * MS), SRC_R08), "0x08 : full:15-18");
    CHECK(same_as_source(request(Q0B, sizeof Q0B, 30 * MS), SRC_R0B), "0x0B : full:21");
    CHECK(same_msg(request(q0b, sizeof q0b, 30 * MS), r0b, sizeof r0b), "0x0B : l'offset 0 de la réponse est celui de la requête (samyang.md § 1.3)");
    CHECK(same_as_source(request(Q09, sizeof Q09, 30 * MS), SRC_R09), "0x09 : full:23");
    CHECK(same_as_source(request(Q0D, sizeof Q0D, 30 * MS), SRC_R0D), "0x0D : full:25");
    CHECK(same_msg(request(Q3F, sizeof Q3F, 30 * MS), NAME, sizeof NAME), "0x3F : « SAMYANG AF 135mm F1.8 », 66 octets (samyang.md § 1.1, § 1.3)");
}

/* ─────────────────────────── samyang.md § 2.5 une seule réponse de classe 2/3 à la fois ─────────────────────────── */

static void t_one_reply(void)
{
    uint8_t two[] = {0x07, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00};
    size_t from;
    printf("samyang.md § 2.5 une seule réponse de classe 2/3 à la fois\n");
    power_and_handshake();
    from = g_bench.n;
    bench_send(2, two, sizeof two);
    bench_run_for(100 * MS);
    CHECK(bench_count(from, 0x07) == 1 && bench_count(from, 0x09) == 0 && L.dropped == 1,
          "deux messages d'init dans une trame : seule la réponse du premier part");
    /* l'émission attend BODY_CS basse ; tant qu'elle ne l'est pas, rien ne part (samyang.md § 2.5) */
    from = g_bench.n;
    bench_send(2, Q07, sizeof Q07);
    bsk_phy_body_cs(true);
    bench_run_for(200 * MS);
    CHECK(bench_count(from, 0x07) == 0 && bench_count_kind(from, BSK_PHY_LENS_CS) == 0,
          "BODY_CS tenue haute : la réponse attend, LENS_CS ne se lève pas");
    bsk_phy_body_cs(false);
    bench_run_for(100 * MS);
    CHECK(bench_count(from, 0x07) == 1, "BODY_CS rabaissée : la réponse en attente part");
}

/* ─────────────────────────── samyang.md § 2.3, § 2.4 le flux ─────────────────────────── */

static void t_flow(void)
{
    size_t from;
    long i, j;
    printf("samyang.md § 2.3 hors flux, en flux, et le 0x0A\n");
    power_and_handshake();
    bsk_phy_vd(60);
    bench_loop(true);
    bench_run_for(200 * MS);          /* hors flux avant tout 0x0A : prouvé par t_flow_guard */
    from = g_bench.n;
    i = request(Q0A, sizeof Q0A, 100 * MS);
    j = bench_find(from, 0x06, 0, 0);
    CHECK(j >= 0 && BENCH_EV(j).frame.len == 40 && pos06(j) == 14623 && BENCH_EV(j).frame.msg[1] == 0x12 &&
              BENCH_EV(j).frame.msg[2] == 0x00,
          "premier 0x06 de la session, au repos à 14623 : 06 12 00 1F 39, sans sens faute de 0x06 précédent (dump05:10-11, samyang.md § 4.8)");
    CHECK(same_as_source(i, SRC_R0A), "0x0A : la réponse est l'écho de la requête (full:39)");
    from = g_bench.n;
    bench_run_for(500 * MS);
    CHECK(bench_count(from, 0x05) >= 28 && bench_count(from, 0x05) <= 31, "en flux : un 0x05 par 0x03 (%zu en 500 ms)", bench_count(from, 0x05));
    CHECK(bench_count(from, 0x06) >= 28 && bench_count(from, 0x06) <= 31, "en flux : un 0x06 par 0x04 (%zu en 500 ms)", bench_count(from, 0x06));
    i = bench_find(from, 0x05, 0, 0);
    CHECK(i >= 0 && BENCH_EV(i).frame.cls == 1 && BENCH_EV(i).frame.len == 97, "0x05 : classe 1, 97 octets pour ce masque (dump05:1-7)");
    j = bench_find(from, 0x06, 0, 0);
    CHECK(j >= 0 && BENCH_EV(j).frame.len == 40 && pos06(j) == 14623 && BENCH_EV(j).frame.msg[1] == 0x12,
          "0x06 au repos à 14623 : 06 12 00 1F 39 (dump05:10-11)");
    {
        size_t bad = 0, n06 = 0;
        for (size_t k = from; k < g_bench.n; k++) {
            const bench_ev_t *b = &BENCH_EV(k);
            const bsk_frame_t *s03 = NULL;
            if (b->kind != BSK_PHY_FRAME || b->frame.msg[0] != 0x06) continue;
            for (size_t s = 0; s < g_bench.n_sent && BENCH_SENT(s).t < b->t; s++)
                if (BENCH_SENT(s).frame.msg[0] == 0x03) s03 = &BENCH_SENT(s).frame;
            n06++;
            if (!s03 || s03->seq != b->frame.seq) bad++;
        }
        CHECK(n06 > 0 && bad == 0, "chaque 0x06 porte la séquence du dernier 0x03 émis avant lui (full:71-73) : %zu écart(s)", bad);
    }
    bench_loop(false);
    bench_run_for(50 * MS);                     /* ce qui est déjà reçu part encore */
    from = g_bench.n;
    bench_run_for(200 * MS);
    CHECK(bench_count(from, 0x05) == 0, "le flux répond : sans 0x03, aucun 0x05");
    bench_loop(true);
    i = request(Q0A_OFF, sizeof Q0A_OFF, 100 * MS);
    CHECK(same_msg(i, Q0A_OFF, sizeof Q0A_OFF), "0x0A aux masques nuls : écho");
    {
        /* la mise en page nulle n'est pas celle des traces : sans le signal « non modélisé », un flux
         * resté allumé ne se verrait pas */
        uint32_t u05 = l135_unmodelled(&L, 0x05), u06 = l135_unmodelled(&L, 0x06);
        from = g_bench.n;
        bench_run_for(200 * MS);
        CHECK(bench_count(from, 0x05) == 0 && bench_count(from, 0x06) == 0 &&
                  l135_unmodelled(&L, 0x05) == u05 && l135_unmodelled(&L, 0x06) == u06,
              "masques nuls : hors flux, aucun 0x05/0x06 n'est même construit (samyang.md § 2.3)");
    }
}

/* S12 : l'offset 60 du 0x05. Le flux du 135 resté alimenté, la VD lancée à T0 (powered) : ses
 * fronts à T0 + 16666 k ; le créneau 0, où le 0x05 est construit, 2083 µs après (le huitième de la trame à 60 Hz, samyang.md § 2.4). La carte
 * émet elle-même ses 0x03 (le 0x05 suivant, samyang.md § 2.4) et ses 0x04, à VD + 5 ms, où le 0x05 du créneau 0 (105
 * octets) est reçu ; le faux les traite 5 ms après leur dernier octet (loop_us), avant le créneau 0 suivant. */
#define VDK(k) (T0 + 16666u * (k))

/* L'offset 60 du premier 0x05 reçu depuis `from` ; 0xEE s'il n'y en a pas. */
static uint8_t o60_from(size_t from)
{
    long i = bench_find(from, 0x05, 0, 0);
    return i >= 0 && BENCH_EV(i).frame.len > 61 ? BENCH_EV(i).frame.msg[61] : 0xEE;
}

static void t_ring_o60(void)
{
    size_t from;
    printf("S12 : un front de la bague pose l'offset 60 (01, FF) en position AF, le 0x04 reçu le remet à 0, le 0x05 du créneau 0 le publie\n");
    powered(true, false, true, false);
    bench_run(VDK(7) + 5 * MS);
    l135_ring_edge(&L, phy_sim_now(), true);
    bench_send(1, g_bench.m03, g_bench.n03);
    from = g_bench.n;
    bench_run(VDK(8) + 5 * MS);
    CHECK(o60_from(from) == 0x01, "d'usine, M1 configuré AF par la flash (10, samyang.md § 5.2), sans 0x01 ni 0x08 : un front montant, 01 "
          "(samyang.md § 5.4) (%02X)", o60_from(from));
    bench_send(1, g_bench.m03, g_bench.n03);
    from = g_bench.n;
    bench_run(VDK(9) + 5 * MS);
    CHECK(o60_from(from) == 0x01, "sans 0x04 reçu, toujours 01 : un drapeau, pas une impulsion (%02X)", o60_from(from));
    bench_send(1, g_bench.m04, g_bench.n04);
    bench_send(1, g_bench.m03, g_bench.n03);
    from = g_bench.n;
    bench_run(VDK(10) + 5 * MS);
    CHECK(o60_from(from) == 0x00, "le 0x04 reçu le remet à 0 (samyang.md § 5.4) (%02X)", o60_from(from));
    l135_ring_edge(&L, phy_sim_now(), false);
    bench_send(1, g_bench.m03, g_bench.n03);
    from = g_bench.n;
    bench_run(VDK(11) + 5 * MS);
    CHECK(o60_from(from) == 0xFF, "un front descendant : FF (samyang.md § 5.4) (%02X)", o60_from(from));
    bench_run(VDK(15) + 5 * MS);
    bench_send(1, g_bench.m04, g_bench.n04);
    bench_send(1, g_bench.m03, g_bench.n03);
    bench_run(VDK(16) + 2000);
    from = g_bench.n;
    l135_ring_edge(&L, phy_sim_now(), true);
    bench_run(VDK(16) + 5 * MS);
    CHECK(o60_from(from) == 0x01, "un front 83 µs avant le créneau 0 : dans son 0x05 (%02X)", o60_from(from));
    bench_send(1, g_bench.m04, g_bench.n04);
    bench_send(1, g_bench.m03, g_bench.n03);
    bench_run(VDK(17) + 2100);
    l135_ring_edge(&L, phy_sim_now(), false);
    from = g_bench.n;
    bench_run(VDK(17) + 5 * MS);
    bench_send(1, g_bench.m03, g_bench.n03);
    CHECK(o60_from(from) == 0x00, "un front 17 µs après le créneau 0 : pas dans son 0x05, construit avant lui (%02X)",
          o60_from(from));
    from = g_bench.n;
    bench_run(VDK(18) + 5 * MS);
    CHECK(o60_from(from) == 0xFF, "... mais dans le suivant (%02X)", o60_from(from));
    CHECK(!l135_out_of_model(&L) && l135_unmodelled(&L, 0x05) == 0, "rien hors modèle");
}

/* samyang.md § 2.4 : la garde « 0x05/0x06 seulement en flux », prouvée avec une mise en page que le
 * modèle sait construire : le masque des traces, posé par l135_start_powered, et la paire à chaque VD. */
static void t_flow_guard(void)
{
    size_t from;
    long i;
    printf("samyang.md § 2.4 hors flux, mise en page valide : aucun 0x05/0x06\n");
    powered(false, false, true, true);
    from = g_bench.n;
    bench_run_for(500 * MS);
    CHECK(bench_count(from, 0x05) == 0 && bench_count(from, 0x06) == 0 &&
              l135_unmodelled(&L, 0x05) == 0 && l135_unmodelled(&L, 0x06) == 0,
          "hors flux, masque des traces, 0x03/0x04 à chaque VD : aucun 0x05/0x06, et aucun n'est construit");
    i = request(Q0A, sizeof Q0A, 100 * MS);
    from = g_bench.n;
    bench_run_for(200 * MS);
    CHECK(same_as_source(i, SRC_R0A) && bench_count(from, 0x05) >= 5 && bench_count(from, 0x06) >= 5,
          "témoin : le même masque, en flux, émet ses 0x05/0x06");
}

/* samyang.md § 2.3 étape 2 : un 0x0A aux masques nuls oublie les 0x05/0x06 en attente, un 0x0A
 * au masque non nul les garde. Hors flux au départ, masque des traces : les 0x03/0x04
 * arrivent sans VD, aucun créneau ne les a encore convertis quand le 0x0A aux masques nuls est traité ;
 * les créneaux qui suivent ne peuvent rien émettre, hors flux, avant le 0x0A qui rallume le flux. */
static void t_zero_mask_forgets(void)
{
    size_t from;
    printf("samyang.md § 2.3 masques nuls : les 0x05/0x06 en attente sont oubliés\n");
    for (int zero = 1; zero >= 0; zero--) {
        powered(false, false, false, false);
        bench_send(1, g_bench.m03, g_bench.n03);
        bench_send(1, g_bench.m04, g_bench.n04);
        bench_run_for(30 * MS);
        /* le 0x03 a changé la consigne d'iris (samyang.md § 4.9) : l'étape 1 arrête les moteurs, et l'écho attend
         * leur arrêt, 200 ms au plus (samyang.md § 2.3) */
        if (zero) CHECK(same_msg(request(Q0A_OFF, sizeof Q0A_OFF, 300 * MS), Q0A_OFF, sizeof Q0A_OFF), "0x0A aux masques nuls : écho");
        bsk_phy_vd(60);
        bench_run_for(100 * MS);
        from = g_bench.n;
        CHECK(same_as_source(request(Q0A, sizeof Q0A, 300 * MS), SRC_R0A), "0x0A au masque des traces : écho");
        bench_run_for(200 * MS);
        if (zero)
            CHECK(bench_count(from, 0x05) == 0 && bench_count(from, 0x06) == 0,
                  "0x03/0x04 reçus, puis masques nuls : rallumé sans nouveau 0x03/0x04, le flux n'émet rien");
        else
            CHECK(bench_count(from, 0x05) == 1 && bench_count(from, 0x06) == 1,
                  "témoin, sans les masques nuls : le 0x05 et le 0x06 en attente partent une fois le flux allumé");
    }
}

/* samyang.md § 2.3, 0x0A d'une mise en page que les traces ne montrent pas : écho, puis 0x05/0x06 signalés « non
 * modélisé », sans réponse fabriquée. */
static void t_other_mask(void)
{
    static const uint8_t Q0A_OTHER[17] = {0x0A, 0x01, 0, 0, 0, 0, 0, 0, 0, 0x01};
    uint32_t u05, u06;
    size_t from;
    printf("samyang.md § 2.3 mise en page hors des traces : non modélisée\n");
    powered(true, false, true, true);
    CHECK(same_msg(request(Q0A_OTHER, sizeof Q0A_OTHER, 300 * MS), Q0A_OTHER, sizeof Q0A_OTHER),
          "0x0A d'un autre masque : écho (samyang.md § 2.3)");
    u05 = l135_unmodelled(&L, 0x05);
    u06 = l135_unmodelled(&L, 0x06);
    from = g_bench.n;
    bench_run_for(300 * MS);
    CHECK(bench_count(from, 0x05) == 0 && bench_count(from, 0x06) == 0 &&
              l135_unmodelled(&L, 0x05) > u05 && l135_unmodelled(&L, 0x06) > u06,
          "en flux, autre masque : aucun 0x05/0x06 fabriqué, chacun compté non modélisé");
}

/* ─────────────────────────── samyang.md § 2.4 et § 3.1 la cadence, le homing ─────────────────────────── */

static size_t homing_replies(bool flow, bool vd, bool loop, uint64_t wait)
{
    size_t from;
    powered(flow, true, vd, loop);
    from = g_bench.n;
    bench_send(2, Q10, sizeof Q10);
    bench_run_for(wait);
    return bench_count(from, 0x10);
}

static void t_cadence(void)
{
    size_t from;
    long i;
    printf("samyang.md § 2.4, § 3.1 ce qui cadence le homing\n");
    CHECK(homing_replies(false, true, false, 2000 * MS) == 1, "hors flux, VD présente : le homing avance, 10 00");
    CHECK(homing_replies(true, true, true, 2000 * MS) == 1, "en flux, 0x03 et 0x04 à chaque trame : 10 00");
    i = bench_find(0, 0x10, 0, 0);
    CHECK(i >= 0 && BENCH_EV(i).frame.msg[1] == 0x00 && BENCH_EV(i).frame.len == 2, "la réponse vaut 10 00 (full:35)");
    CHECK(homing_replies(true, true, false, 8000 * MS) == 0, "en flux, sans 0x03/0x04 : le homing cale, pas de 10 00 en 8 s");
    from = g_bench.n;
    bench_loop(true);
    bench_run_for(2000 * MS);
    CHECK(bench_count(from, 0x10) == 1, "la boucle reprend : le homing calé repart et répond");
    CHECK(homing_replies(false, false, false, 8000 * MS) == 0, "hors flux, sans VD ni 0x03/0x04 : rien ne tourne, pas de 10 00");
    /* chaque traitement a sa porte : en flux, le créneau 3 ne fait pas tourner l'iris, ni le 4 la trame */
    {
        uint8_t iris_only[] = {0x10, 0x04};
        uint8_t m03[64], m04[64];
        size_t n03 = g_bench.n03, n04 = g_bench.n04;
        memcpy(m03, g_bench.m03, n03);
        memcpy(m04, g_bench.m04, n04);
        powered(true, true, true, false);
        bench_loop_msgs(m03, 0, m04, n04);             /* le 0x04 seul, à chaque VD */
        bench_loop(true);
        from = g_bench.n;
        bench_send(2, iris_only, sizeof iris_only);
        bench_run_for(4000 * MS);
        CHECK(bench_count(from, 0x10) == 0, "en flux, 0x04 sans 0x03 : le homing iris cale (le créneau 3 ne compte pas en flux)");
        powered(true, true, true, false);
        bench_loop_msgs(m03, n03, m04, 0);             /* le 0x03 seul, à chaque VD */
        bench_loop(true);
        from = g_bench.n;
        bench_send(2, QF32, sizeof QF32);
        bench_run_for(4000 * MS);
        CHECK(bench_count(from, 0x10) == 0, "en flux, 0x03 sans 0x04 : le homing focus ('F' 0x32) cale (le créneau 4 ne compte pas en flux)");
        powered(false, true, true, false);
        from = g_bench.n;
        bench_send(2, iris_only, sizeof iris_only);
        bench_run_for(2000 * MS);
        CHECK(bench_count(from, 0x10) == 1, "hors flux, VD présente : le homing iris seul répond (créneau 3)");
    }
    /* hors flux, sans VD, avec 0x03/0x04 : le traitement tourne, mais aucun mouvement ne démarre */
    powered(false, true, false, false);
    from = g_bench.n;
    bench_send(2, Q10, sizeof Q10);
    for (int k = 0; k < 600; k++) {
        bench_send(1, g_bench.m03, g_bench.n03);
        bench_send(1, g_bench.m04, g_bench.n04);
        bench_run_for(16667);
    }
    CHECK(bench_count(from, 0x10) == 0 && bench_count(from, 0x17) == 1,
          "sans VD, aucun mouvement ne démarre : échéances, reprises, puis 0x17 sans 10 00 (samyang.md § 3.1)");
}

static void t_homing(void)
{
    uint8_t q10_none[] = {0x10, 0x03};
    size_t from;
    printf("samyang.md § 3.1 le homing\n");
    CHECK(homing_replies(true, true, true, 3000 * MS) == 1, "référence");
    powered(true, true, true, true);
    from = g_bench.n;
    bench_send(2, q10_none, sizeof q10_none);
    bench_run_for(3000 * MS);
    CHECK(bench_count(from, 0x10) == 0, "ni bit 2 ni bit 3 : rien, et aucune réponse ne viendra");
    powered(true, true, true, true);
    from = g_bench.n;
    bench_send(2, Q10, sizeof Q10);
    bench_run_for(300 * MS);
    bench_send(2, Q10, sizeof Q10);
    bench_run_for(3000 * MS);
    CHECK(bench_count(from, 0x10) == 1, "un 0x10 pendant un homing le relance : une seule réponse");
    /* panne « position figée » : chaque étape focus échoue ; au second échec, 10 00 quand même */
    powered(true, true, true, true);
    L.f.frozen_position = true;
    from = g_bench.n;
    bench_send(2, Q10, sizeof Q10);
    bench_run_for(8000 * MS);
    CHECK(bench_count(from, 0x10) == 1 && bench_count(from, 0x17) == 0 && L.dropped >= 1,
          "homing focus manqué deux fois : 10 00, et le 0x17 qui suit est jeté par le verrou");
}

/* ─────────────────────────── samyang.md § 4 la mise au point ─────────────────────────── */

/* Une consigne 0x1D dans la trame du 0x04 (spec § 3.3). */
static void focus_cmd(uint16_t value, uint8_t flags)
{
    uint8_t m[64];
    size_t n = g_bench.n04;
    memcpy(m, g_bench.m04, n);
    m[n++] = 0x1D;
    m[n++] = (uint8_t)value;
    m[n++] = (uint8_t)(value >> 8);
    m[n++] = 0x00;
    m[n++] = flags;
    bench_send(1, m, n);
}

static long first_ack(size_t from, uint8_t type)
{
    for (size_t i = from; i < g_bench.n; i++) {
        const bench_ev_t *b = &BENCH_EV(i);
        if (b->kind != BSK_PHY_FRAME || b->frame.msg[0] != 0x06) continue;
        for (size_t o = 40; o + 1 < b->frame.len; o += 2)
            if (b->frame.msg[o] == type) return (long)i;
    }
    return -1;
}

static long last_06(void)
{
    for (size_t i = g_bench.n; i-- > 0;)
        if (BENCH_EV(i).kind == BSK_PHY_FRAME && BENCH_EV(i).frame.msg[0] == 0x06) return (long)i;
    return -1;
}

static int32_t last_pos06(void)
{
    long i = last_06();
    return i >= 0 ? pos06(i) : -1;
}

static void t_focus(void)
{
    size_t from;
    long i;
    uint8_t stop[] = {0x1C};
    printf("samyang.md § 4 la mise au point\n");
    powered(true, true, true, true);
    from = g_bench.n;
    focus_cmd(20000, 0x00);
    bench_run_for(1500 * MS);
    i = first_ack(from, 0x1D);
    CHECK(i >= 0 && pos06(i) == 20000, "0x1D en pas : la position va à la cible, puis 1D 00 après le 0x06");
    CHECK(i >= 0 && BENCH_EV(i).frame.msg[40] == 0x1D && BENCH_EV(i).frame.msg[41] == 0x00 && BENCH_EV(i).frame.len == 42,
          "l'accusé suit le 0x06 dans la même trame de classe 1 (samyang.md § 4.4)");
    from = g_bench.n;
    focus_cmd(40000, 0x00);
    bench_run_for(3000 * MS);
    i = first_ack(from, 0x1D);
    CHECK(i >= 0 && pos06(i) == 0x790C, "cible au-delà de la limite : ramenée à 30988 en silence, puis 1D 00 (samyang.md § 4.3)");
    from = g_bench.n;
    focus_cmd(40000, 0x00);
    bench_run_for(200 * MS);
    i = first_ack(from, 0x1D);
    CHECK(i >= 0 && last_pos06() == 0x790C, "déjà à la limite, cible au-delà : aucun mouvement, puis 1D 00 (samyang.md § 4.3)");
    from = g_bench.n;
    focus_cmd(15000, 0x01);
    bench_run_for(200 * MS);
    CHECK(first_ack(from, 0x1D) >= 0 && last_pos06() == 0x790C, "unité 1 refusée : 1D 00, sans mouvement");
    from = g_bench.n;
    focus_cmd((uint16_t)-1000, 0x04);
    bench_run_for(1000 * MS);
    CHECK(first_ack(from, 0x1D) >= 0 && last_pos06() == 0x790C - 1000, "bit 2 : déplacement relatif signé");
    /* éviction et arrêt */
    from = g_bench.n;
    focus_cmd(15000, 0x00);
    bench_run_for(300 * MS);
    bench_send(1, stop, sizeof stop);
    bench_run_for(500 * MS);
    i = first_ack(from, 0x1D);
    CHECK(i >= 0 && first_ack(from, 0x1C) >= i, "0x1C pendant un 0x1D : l'ancien reçoit 1D 00, puis 1C 00 à l'arrêt (samyang.md § 4.4)");
    CHECK(last_pos06() > 15000 && last_pos06() < 0x790C - 1000, "0x1C : le moteur s'arrête en route");
    /* le 0x0A annule le 0x1D sans accusé */
    from = g_bench.n;
    focus_cmd(20000, 0x00);
    bench_run_for(100 * MS);
    CHECK(same_as_source(request(Q0A, sizeof Q0A, 300 * MS), SRC_R0A), "0x0A pendant un 0x1D : écho");
    bench_run_for(2000 * MS);
    CHECK(first_ack(from, 0x1D) < 0 && last_pos06() != 20000, "0x0A : moteurs arrêtés, 0x1D annulé sans accusé (samyang.md § 2.3)");
    /* panne « position figée » */
    powered(true, true, true, true);
    L.f.frozen_position = true;
    from = g_bench.n;
    focus_cmd(20000, 0x00);
    bench_run_for(2000 * MS);
    CHECK(first_ack(from, 0x1D) < 0 && last_pos06() == 14623, "position figée : le 0x06 ne bouge plus, aucun accusé");
}

/* Une trame de classe 1 : le 0x04 de la paire, puis `x` (comme focus_cmd). */
static void send_after_04(const uint8_t *x, size_t nx)
{
    uint8_t m[64];
    size_t n = g_bench.n04;
    memcpy(m, g_bench.m04, n);
    if (nx) memcpy(m + n, x, nx);              /* send_after_04(NULL, 0) : memcpy ne reçoit pas NULL, même pour 0 octet */
    bench_send(1, m, n + nx);
}

/* samyang.md § 4.4 : deux fins avant un même 0x06 ; les accusés le suivent dans la même trame, 1D puis 1C
 * (S11 de lens135.c). Sans VD ni boucle, chaque 0x04 fait tourner le
 * traitement de trame (samyang.md § 2.4), et le 0x06 ne part qu'au créneau 1 du front VD que le test pose. */
static void t_acks_order(void)
{
    static const uint8_t go[] = {0x1D, 0x20, 0x4E, 0x00, 0x00};       /* 20000, en pas */
    static const uint8_t stop[] = {0x1C};
    size_t from;
    long i;
    printf("samyang.md § 4.4 l'ordre des accusés dans la trame du 0x06\n");
    powered(true, false, false, false);
    send_after_04(go, sizeof go);
    bench_run_for(10 * MS);
    phy_sim_vd_edge();                          /* le mouvement démarre ; un 0x06 sans accusé part */
    bench_run_for(50 * MS);
    send_after_04(stop, sizeof stop);           /* 0x1D évincé : 1D 00 en attente ; arrêt demandé */
    bench_run_for(10 * MS);
    send_after_04(NULL, 0);                     /* moteur arrêté : 1C 00 en attente */
    bench_run_for(10 * MS);
    from = g_bench.n;
    phy_sim_vd_edge();
    bench_run_for(30 * MS);
    i = bench_find(from, 0x06, 0, 0);
    CHECK(i >= 0 && BENCH_EV(i).frame.len == 44 && BENCH_EV(i).frame.msg[40] == 0x1D && BENCH_EV(i).frame.msg[41] == 0x00 &&
              BENCH_EV(i).frame.msg[42] == 0x1C && BENCH_EV(i).frame.msg[43] == 0x00,
          "1D 00 puis 1C 00, dans la trame du 0x06 qui suit les deux fins (samyang.md § 4.4)");
}

/* Pour chaque 0x06 à partir de `from`, le sens attendu d'après la position du 0x06 précédent. */
static void check_direction(size_t from, size_t *up, size_t *down, size_t *still, size_t *bad)
{
    long prev = -1;
    for (size_t k = g_bench.n > BENCH_LOG_CAP ? g_bench.n - BENCH_LOG_CAP : 0; k < g_bench.n; k++) {
        const bench_ev_t *b = &BENCH_EV(k);
        if (b->kind != BSK_PHY_FRAME || b->frame.msg[0] != 0x06) continue;
        if (prev >= 0 && k >= from) {
            int32_t p0 = pos06(prev), p1 = pos06((long)k);
            uint8_t want = p1 > p0 ? 0x02 : p1 < p0 ? 0x04 : 0x00;
            if (want == 0x02) (*up)++;
            else if (want == 0x04) (*down)++;
            else (*still)++;
            if ((b->frame.msg[2] & 0x06) != want) (*bad)++;
        }
        prev = (long)k;
    }
}

/* samyang.md § 4.8, le 0x06 : offset 1 bits 1-2, sens de variation de la position entre deux 0x06
 * (bit 1 si elle a crû, bit 2 si elle a décru) ; offset 0, bits 0-2 = 2, bit 3
 * position >= 29988 (0x7524), bit 5 position >= limite haute 30988 (0x790C, samyang.md § 4.3). */
static void t_06_fields(void)
{
    size_t from, up = 0, down = 0, still = 0, bad = 0;
    long i;
    printf("samyang.md § 4.8 les champs calculés du 0x06\n");
    powered(true, false, true, true);
    from = g_bench.n;
    focus_cmd(20000, 0x00);
    bench_run_for(1500 * MS);
    focus_cmd(15000, 0x00);
    bench_run_for(1500 * MS);
    check_direction(from, &up, &down, &still, &bad);
    CHECK(up > 0 && down > 0 && still > 0 && bad == 0,
          "sens entre deux 0x06 : 02 en montant (%zu), 04 en descendant (%zu), 00 immobile (%zu) : %zu écart(s)",
          up, down, still, bad);
    focus_cmd(40000, 0x00);
    bench_run_for(3000 * MS);
    i = last_06();
    CHECK(last_pos06() == 0x790C && i >= 0 && BENCH_EV(i).frame.msg[1] == 0x2A,
          "à la limite haute 30988 : offset 0 = 2A (bits 0-2 = 2, bit 3, bit 5)");
    focus_cmd((uint16_t)-1, 0x04);
    bench_run_for(300 * MS);
    i = last_06();
    CHECK(last_pos06() == 0x790B && i >= 0 && BENCH_EV(i).frame.msg[1] == 0x0A,
          "un pas sous la limite haute : offset 0 = 0A, bit 5 éteint");
}

/* samyang.md § 4.5 : le 0x1C n'obtient pas l'arrêt en 1,2 s -> arrêt forcé, 0x17, 1C 00. Le 0x1D qui suit le
 * 0x1C ne touche pas son travail (samyang.md § 4.1 : un drapeau par ordre) et relance le moteur vers 30000, à plus
 * de 1,2 s de là. Le 0x17 : 3 octets, classe 3 (protocol.md § 4.2, § 7.16) ; ses deux derniers ne sont pas publiés
 * (tirés du firmware, sens inconnu), le faux les met à zéro (lens135.c, tx_msg()). */
static void t_stop_deadline(void)
{
    static const uint8_t stop[] = {0x1C};
    static const uint8_t go[] = {0x1D, 0x30, 0x75, 0x00, 0x00};       /* 30000, en pas */
    static const uint8_t fail[] = {0x17, 0x00, 0x00};
    uint64_t t0, dt;
    size_t from;
    long i;
    printf("samyang.md § 4.5 l'échéance de 1,2 s du 0x1C\n");
    powered(true, false, true, true);
    from = g_bench.n;
    t0 = phy_sim_now();
    send_after_04(stop, sizeof stop);
    send_after_04(go, sizeof go);
    bench_run_for(2500 * MS);
    i = bench_find(from, 0x17, 0, 0);
    dt = i >= 0 ? BENCH_EV(i).t - t0 : 0;
    CHECK(same_msg(i, fail, sizeof fail) && BENCH_EV(i).frame.cls == 3 && dt > 1200 * MS && dt < 1250 * MS,
          "arrêt non obtenu en 1,2 s : 17 00 00, classe 3 (%llu ms après le 0x1C)", (unsigned long long)(dt / MS));
    CHECK(bench_count(from, 0x17) == 1 && first_ack(from, 0x1C) >= 0 && first_ack(from, 0x1D) >= 0,
          "puis 1C 00, et 1D 00 à l'arrêt du moteur");
    CHECK(last_pos06() > 14623 && last_pos06() < 30000, "arrêt forcé : le moteur s'arrête avant la cible (%d)", last_pos06());
}

/* samyang.md § 4.9 : chaque 0x03 range la consigne d'ouverture ; l'iris ne bouge que si elle change (S4 de
 * lens135.c). Observable par samyang.md § 2.3 étape 1 : un 0x0A annule le 0x1D en attente si un moteur bouge,
 * et le laisse sinon. L'octet changé, offset 3 du 0x03, est dans la consigne. */
static void t_iris(void)
{
    static const uint8_t go[] = {0x1D, 0x20, 0x4E, 0x00, 0x00};       /* 20000, en pas */
    uint8_t m03[64];
    size_t n03, from;
    printf("samyang.md § 4.9 l'iris suit la consigne du 0x03\n");
    for (int moved = 0; moved < 2; moved++) {
        powered(true, false, true, true);       /* la paire a posé sa consigne ; l'iris est arrivé */
        bench_loop(false);
        bench_run_for(50 * MS);
        n03 = g_bench.n03;
        memcpy(m03, g_bench.m03, n03);
        if (moved) m03[4] ^= 0x10;
        from = g_bench.n;
        bench_send(1, m03, n03);
        bench_send(1, go, sizeof go);            /* sans 0x04 : le 0x1D attend le traitement de trame */
        bench_send(2, Q0A, sizeof Q0A);
        bench_run_for(300 * MS);
        CHECK(same_as_source(bench_find(from, 0x0A, 0, 0), SRC_R0A), "0x0A : écho");
        bench_loop(true);
        bench_run_for(1500 * MS);
        if (moved)
            CHECK(first_ack(from, 0x1D) < 0 && last_pos06() == 14623,
                  "consigne d'ouverture changée : l'iris bouge, le 0x0A annule le 0x1D sans accusé");
        else
            CHECK(first_ack(from, 0x1D) >= 0 && last_pos06() == 20000,
                  "même consigne : l'iris ne bouge pas, le 0x0A laisse le 0x1D aller au bout");
    }
}

/* ─────────────────────────── samyang.md § 6 le canal 0x40 ─────────────────────────── */

static void t_service(void)
{
    uint8_t qfb[19] = {0x40, 'F', 0xFB, 0xF3, 0x44};           /* astro:25 : aller à 17651 */
    uint8_t rm31[19] = {0x40, 'M', 0x31, 0x26};
    uint8_t qf20[19] = {0x40, 'F', 0x20};
    size_t from;
    long i;
    printf("samyang.md § 6 le canal 0x40\n");
    powered(true, false, true, true);
    from = g_bench.n;
    bench_send(2, QV, sizeof QV);
    bench_run_for(40 * MS);
    CHECK(bench_find(from, 0x40, 'V', 0) < 0, "réponse 0x40 : pas avant 50 ms");
    bench_run_for(100 * MS);
    i = bench_find(from, 0x40, 'V', 0);
    CHECK(same_as_source(i, SRC_R40_V), "'V' : 01 05, la version de l'objectif de l'humain (full:43)");
    CHECK(same_as_source(request(QFA, sizeof QFA, 150 * MS), SRC_R40_FA), "'F' FA : la position, 14623 (full:187)");
    /* avant 'M', pas de mode service : pas de notification de fourche */
    from = g_bench.n;
    bench_send(2, qfb, sizeof qfb);
    bench_run_for(600 * MS);
    CHECK(same_as_source(bench_find(from, 0x40, 'F', 0xFB), SRC_R40_FB), "'F' FB : réponse après l'arrêt du moteur (full:393)");
    CHECK(bench_find(from, 0x40, 'L', 0xFB) < 0, "hors mode service : pas de notification de fourche");
    CHECK(same_as_source(request(QM, sizeof QM, 150 * MS), SRC_R40_M), "'M' 00 : 26 (full:55)");
    /* mode service, sans retour : la fourche franchie hors homing se notifie ('L' en montant, astro:28) */
    qfb[3] = 0x1F; qfb[4] = 0x39;                              /* retour à 14623 */
    from = g_bench.n;
    bench_send(2, qfb, sizeof qfb);
    bench_run_for(600 * MS);
    i = bench_find(from, 0x40, 'H', 0xFB);
    CHECK(i >= 0 && (BENCH_EV(i).frame.msg[3] | BENCH_EV(i).frame.msg[4] << 8) == 14835,
          "mode service : en descendant, 'H' à la position du front (astro:323, astro:403)");
    from = g_bench.n;
    bench_send(2, QF32, sizeof QF32);
    bench_run_for(2000 * MS);
    CHECK(bench_count(from, 0x10) == 1 && bench_count(from, 0x40) == 0,
          "mode service : un homing franchit la fourche sans notification (samyang.md § 6.4 : hors homing)");
    CHECK(same_msg(request(QM31, sizeof QM31, 150 * MS), rm31, sizeof rm31), "'M' 31 : 4D 31 26 (samyang.md § 6.3)");
    qfb[3] = 0xF3; qfb[4] = 0x44;
    from = g_bench.n;
    bench_send(2, qfb, sizeof qfb);
    bench_run_for(600 * MS);
    CHECK(bench_find(from, 0x40, 'L', 0xFB) < 0, "après 'M' 31 : plus de notification de fourche (samyang.md § 6.4)");
    from = g_bench.n;
    bench_send(2, QF32, sizeof QF32);
    bench_run_for(2000 * MS);
    CHECK(bench_count(from, 0x10) == 1 && bench_find(from, 0x40, 'F', 0x32) < 0,
          "'F' 0x32 : homing focus seul, fini par un 10 00 standard, sans réponse 0x40 (reboots:547-549)");
    from = g_bench.n;
    bench_send(2, qf20, sizeof qf20);
    bench_run_for(200 * MS);
    CHECK(bench_find(from, 0x40, 'F', 0x20) < 0 && l135_unmodelled(&L, 0x40) == 1,
          "sous-commande hors liste : consommée, non modélisée, sans réponse");
}

/* S13 : la configuration du commutateur Custom (samyang.md § 5.2, § 6.3) : 'P' 0x38 la range si chaque
 * quartet de d - 0x30 est sous 3, 'P' 0xFA la rend à l'octet de données 7, d'usine 10
 * et les sept octets avant elle à 0 ; une coupure la garde (la flash, rechargée au démarrage).
 * Les réponses attendues sont écrites ici à la main. */
static bool custom_is(uint8_t cfg)
{
    static const uint8_t QPFA[19] = {0x40, 'P', 0xFA};
    uint8_t r[19] = {0x40, 'P', 0xFA};
    r[10] = cfg;
    return same_msg(request(QPFA, sizeof QPFA, 150 * MS), r, sizeof r);
}

static void t_custom(void)
{
    static const uint8_t R38[19] = {0x40, 'P', 0x38};
    static const uint8_t BAD[] = {0x33, 0x60, 0x2F, 0x00, 0xFF};   /* v = 03, 30, FF, D0, CF : un quartet à 3 ou plus */
    uint8_t q38[19] = {0x40, 'P', 0x38, 0x51};                      /* haut 2 (MF), bas 1 (AF) */
    uint8_t q[19] = {0x40, 'P', 0xFA, 0x53};
    size_t from;
    printf("'P' 0x38 range la configuration du commutateur Custom, 'P' 0xFA la rend, une coupure la garde\n");
    powered(true, false, true, true);
    CHECK(custom_is(0x10), "'P' FA : 40 50 FA, sept 00, 10 d'usine, puis 00 (samyang.md § 5.2, § 6.3)");
    CHECK(same_msg(request(q38, sizeof q38, 150 * MS), R38, sizeof R38), "'P' 38 51 : l'écho, données à zéro (samyang.md § 6.3)");
    CHECK(custom_is(0x21), "'P' FA : 21 rangé par 'P' 38 51");
    for (size_t i = 0; i < sizeof BAD; i++) {
        q38[3] = BAD[i];
        CHECK(same_msg(request(q38, sizeof q38, 150 * MS), R38, sizeof R38) && custom_is(0x21),
              "'P' 38 %02X : l'écho, rien rangé (samyang.md § 6.3)", BAD[i]);
    }
    q38[3] = 0x30;
    CHECK(same_msg(request(q38, sizeof q38, 150 * MS), R38, sizeof R38) && custom_is(0x00), "'P' 38 30 : 00 rangé");
    q38[3] = 0x52;
    CHECK(same_msg(request(q38, sizeof q38, 150 * MS), R38, sizeof R38) && custom_is(0x22), "'P' 38 52 : 22 rangé");
    l135_power(&L, phy_sim_now(), false);
    bench_run_for(500 * MS);
    l135_start_powered(&L, phy_sim_now(), true, false);
    bench_run_for(100 * MS);
    CHECK(custom_is(0x22), "après une coupure : 22, gardé (samyang.md § 5.2)");
    from = g_bench.n;
    bench_send(2, q, sizeof q);
    q[2] = 0x11;                                                   /* 'P' 11 : écrit la flash (samyang.md § 7.2) */
    q[3] = 0x00;
    bench_send(2, q, sizeof q);
    bench_run_for(200 * MS);
    CHECK(bench_count(from, 0x40) == 0 && l135_unmodelled(&L, 0x40) == 2,
          "'P' FA 53 et 'P' 11, hors liste : consommées, non modélisées, sans réponse");
    CHECK(custom_is(0x22), "... et rien n'a changé");
    fresh();
    l135_start_powered(&L, phy_sim_now(), true, false);
    bsk_phy_vd(60);
    bench_loop(true);
    bench_run_for(100 * MS);
    CHECK(custom_is(0x10), "un autre 135 (l135_init) : 10 d'usine");
}

/* ─────────────────────── S14 le commutateur Custom, S15 l'ouverture native ─────────────────────── */

static const uint8_t Q08_06[] = {0x08, 0x06, 0, 0, 0, 0, 0, 0, 0};   /* le 0x08 de la carte à un Samyang reconnu (init.c) */
static const uint8_t Q08_00[] = {0x08, 0x00, 0, 0, 0, 0, 0, 0, 0};

/* Le faux 135 resté alimenté (mode normal), `cfg` en flash, le commutateur en M2 si `m2` ; la paire tracée (consigne
 * 11B2, offset 3 du 0x04 à 83 : le bit AF du boîtier posé) part à chaque front VD, la première au numéro `seq`. */
static void powered_cfg(uint8_t cfg, bool m2, uint8_t seq)
{
    fresh();
    L.custom = cfg;
    L.in.m2 = m2;
    g_bench.seq = seq;
    l135_start_powered(&L, phy_sim_now(), true, false);
    bsk_phy_vd(60);
    bench_loop(true);
}

/* La trame VD qui suit l'instant présent (VDK). */
static unsigned vd_next(void) { return (unsigned)((phy_sim_now() - T0) / 16666u) + 1; }

/* Le 0x05 reçu dans [VDK(k), VDK(k) + 8 ms) : celui du créneau 0 de cette trame ; -1 s'il n'y en a pas. */
static long at05(unsigned k)
{
    size_t from;
    bench_run(VDK(k));
    from = g_bench.n;
    bench_run(VDK(k) + 8 * MS);
    return bench_find(from, 0x05, 0, 0);
}

static uint8_t off05(long i, unsigned off)
{
    return i >= 0 && BENCH_EV(i).frame.len > off + 1 ? BENCH_EV(i).frame.msg[off + 1] : 0xEE;
}

/* Les offsets 19, 18 et 17 du 0x05 i, dans cet ordre : 0x011200 = valide, 1200 ; EEEEEE sans 0x05. */
static uint32_t ap05(long i)
{
    return (uint32_t)off05(i, 19) << 16 | (uint32_t)off05(i, 18) << 8 | off05(i, 17);
}

/* n fronts (descendants si n < 0) à VDK(k) + 12 ms, k la trame suivante : après le 0x03 de VDK(k), traité 5 ms après
 * lui (loop_us), et son 0x04, 5 ms plus tard. L'offset 60 qu'ils posent est dans le 0x05 de VDK(k + 1), construit au
 * créneau 0 ; le 0x03 de VDK(k + 1) les compte, et le 0x05 de VDK(k + 2) publie l'ouverture native. Rend k. */
static unsigned turn(int n)
{
    unsigned k = vd_next();
    bench_run(VDK(k) + 12 * MS);
    for (int e = 0; e < (n < 0 ? -n : n); e++) l135_ring_edge(&L, phy_sim_now(), n > 0);
    return k;
}

/* L'ouverture native publiée après n fronts. */
static uint32_t turn_ap(int n) { return ap05(at05(turn(n) + 2)); }

/* L'offset 62 du 0x05 de la trame d'après. */
static uint8_t o62_next(void) { return off05(at05(vd_next() + 1), 62); }

static void t_switch(void)
{
    static const uint8_t q38_30[19] = {0x40, 'P', 0x38, 0x30};
    static const uint8_t q38_02[19] = {0x40, 'P', 0x38, 0x32};
    unsigned k;
    long i;
    printf("S14 : le commutateur Custom — l'offset 62 et l'offset 60 suivent la configuration de la position courante ; le "
           "forçage du vieux boîtier, en RAM\n");
    /* 02 : M1 = APERTURE, M2 = MF */
    powered_cfg(0x02, true, 0);
    bench_run_for(100 * MS);
    CHECK(o62_next() == 0x01, "M2 configuré MF, avant tout 0x08 : 01, l'offset 62 du démarrage, inchangé (S14 de lens135.c)");
    (void)request(Q08_06, sizeof Q08_06, 30 * MS);
    CHECK(o62_next() == 0x03, "... après le 0x08 au bit 0x04, en mode normal : 03 (samyang.md § 5.2)");
    k = turn(1);
    CHECK(off05(at05(k + 1), 60) == 0x00, "M2 en MF : un front ne pose pas l'offset 60 (samyang.md § 5.4)");
    L.in.m2 = false;
    CHECK(o62_next() == 0x01, "le commutateur en M1, configuré APERTURE : 01 (samyang.md § 5.2)");
    k = turn(1);
    CHECK(off05(at05(k + 1), 60) == 0x00, "M1 en APERTURE : un front ne pose pas l'offset 60");
    CHECK(!l135_out_of_model(&L), "rien hors modèle");

    /* le mode normal : avant le homing, l'offset 62 est effacé (samyang.md § 5.2) */
    power_and_handshake();
    (void)request(q38_02, sizeof q38_02, 150 * MS);
    L.in.m2 = true;
    (void)request(Q08_06, sizeof Q08_06, 30 * MS);
    (void)request(Q0A, sizeof Q0A, 100 * MS);
    bsk_phy_vd(60);
    bench_loop(true);
    bench_run_for(100 * MS);
    CHECK(o62_next() == 0x01, "02 rangé par 'P' 38 32, M2 = MF, avant le homing (mode 0) : 01 (samyang.md § 5.2)");
    (void)request(Q10, sizeof Q10, 1500 * MS);
    CHECK(o62_next() == 0x03, "... après le homing (mode normal, samyang.md § 5.2) : 03");

    /* le forçage : 00 en flash, M1 et M2 en APERTURE */
    powered_cfg(0x00, false, 0);
    bench_run_for(100 * MS);
    (void)request(Q01, sizeof Q01, 30 * MS);
    (void)request(Q08_06, sizeof Q08_06, 30 * MS);
    k = turn(1);
    i = at05(k + 1);
    CHECK(off05(i, 60) == 0x00 && off05(i, 62) == 0x01,
          "0x01 en FF 01 00 puis 0x08 au bit 0x04 (l'init de la carte) : pas de forçage, M1 en APERTURE (%02X, %02X)", off05(i, 60),
          off05(i, 62));
    L.in.m2 = true;
    CHECK(o62_next() == 0x01, "... M2 en APERTURE : 01");
    L.in.m2 = false;
    (void)request(Q01, sizeof Q01, 30 * MS);
    (void)request(Q08, sizeof Q08, 30 * MS);
    k = turn(1);
    i = at05(k + 1);
    CHECK(off05(i, 60) == 0x01 && off05(i, 62) == 0x01,
          "0x01 en FF 01 00 puis 0x08 sans le bit 0x04 (full:14) : M1 forcé en AF, le front pose 01 (samyang.md § 5.2, § 5.3) "
          "(%02X, %02X)", off05(i, 60), off05(i, 62));
    L.in.m2 = true;
    CHECK(o62_next() == 0x03, "... M2 forcé en MF : 03");
    (void)request(Q08_06, sizeof Q08_06, 30 * MS);
    CHECK(o62_next() == 0x03, "un 0x08 au bit 0x04 lève le drapeau (samyang.md § 5.3) mais ne rend pas la flash : M2 toujours MF, 03");
    (void)request(q38_30, sizeof q38_30, 150 * MS);
    CHECK(o62_next() == 0x01, "'P' 38 30 pose aussi les positions en RAM (samyang.md § 6.3) : M2 en APERTURE, 01");
    powered_cfg(0x00, true, 0);
    bench_run_for(100 * MS);
    (void)request(Q08_00, sizeof Q08_00, 30 * MS);
    CHECK(o62_next() == 0x03, "un 0x08 sans le bit 0x02 ni 0x04, sans 0x01 (samyang.md § 5.2) : M2 forcé en MF, 03");
    CHECK(!l135_out_of_model(&L), "rien hors modèle");
}

/* L'ouverture native contre la table 0, écrite ici à la main : chaque ouverture six fois, 4543 (11BF) aux index 0-5, 4608
 * (1200) aux index 6-11, 4693 (1255) aux index 12-17, ..., 6314 (18AA) aux index 126-131, 6400 (1900) à l'index 132. */
static void t_native(void)
{
    uint8_t m03[64], m04[64];
    size_t n03, n04, from, none = 0;
    unsigned k;
    uint32_t v;
    long i, first = -1;
    printf("S15 : l'ouverture native — en position APERTURE, le bit AF du boîtier posé, l'objectif compte les fronts et publie "
           "l'ouverture aux offsets 17-19\n");
    /* l'entrée : 01 en flash (M1 = APERTURE, M2 = AF), les 0x03 numérotés depuis 0 */
    powered_cfg(0x01, false, 0);
    from = g_bench.n;
    bench_run_for(200 * MS);
    for (i = bench_find(from, 0x05, 0, 0); i >= 0 && first < 0; i = bench_find((size_t)i + 1, 0x05, 0, 0)) {
        if (ap05(i) == 0) none++;
        else first = i;
    }
    CHECK(none == 3 && ap05(first) == 0x0111BF,
          "les 0x05 des 0x03 numérotés 0, 1 et 2 sans ouverture (moins de 3 numéros depuis 0, samyang.md § 5.5), puis 11BF valide : "
          "la consigne 11B2 sous la première case, index 0 (S15 de lens135.c) (%zu, %06X)", none, (unsigned)ap05(first));
    powered_cfg(0x01, false, 0x10);
    from = g_bench.n;
    bench_run_for(60 * MS);
    CHECK(ap05(bench_find(from, 0x05, 0, 0)) == 0x0111BF,
          "le premier 0x03, numéroté 10 : le bit AF du boîtier tenu pour posé avant tout 0x04 (samyang.md § 5.5), l'ouverture tout de suite");

    /* la sortie : le commutateur en M2 (AF) */
    k = turn(0);
    L.in.m2 = true;
    CHECK(ap05(at05(k + 2)) == 0x0111BF && ap05(at05(k + 3)) == 0,
          "M2 en AF : le premier 0x03 hors du mode garde la publication, le second l'efface (samyang.md § 5.5)");
    n03 = g_bench.n03;
    memcpy(m03, g_bench.m03, n03);
    n04 = g_bench.n04;
    memcpy(m04, g_bench.m04, n04);
    m03[4] = m03[6] = 0x00;                       /* la consigne 1200, f/2 (4608) */
    m03[5] = m03[7] = 0x12;
    bench_loop_msgs(m03, n03, m04, n04);
    k = turn(6);
    CHECK(off05(at05(k + 1), 60) == 0x01, "M2 en AF : le front pose l'offset 60 (samyang.md § 5.4)");
    /* l'entrée de nouveau, resynchronisée sur la consigne ; les fronts tournés hors du mode oubliés */
    k = turn(0);
    L.in.m2 = false;
    v = ap05(at05(k + 2));
    CHECK(v == 0x011200,
          "de retour en M1 : l'index repart de la consigne 1200, index 6, le compteur remis à 0 "
          "(samyang.md § 5.5) : les six fronts de M2 ne comptent pas (%06X)", (unsigned)v);

    /* la bague */
    CHECK(turn_ap(-2) == 0x011200, "deux fronts vers l'ouvert : rien, la zone morte (samyang.md § 5.5)");
    CHECK(turn_ap(-1) == 0x0111BF, "le troisième : trois index d'un coup, index 3, 11BF");
    CHECK(turn_ap(3) == 0x011200, "trois fronts vers le fermé : index 6, 1200 (samyang.md § 5.5)");
    CHECK(turn_ap(6) == 0x011255, "six fronts : un tiers, index 12, 1255");
    CHECK(turn_ap(-15) == 0x0111BF && turn_ap(6) == 0x011200, "quinze fronts vers l'ouvert : index borné à 0 (samyang.md § 5.5), "
          "puis six vers le fermé : index 6, 1200");
    (void)turn_ap(65);
    CHECK(turn_ap(65) == 0x011900 && turn_ap(-1) == 0x0118AA,
          "136 index : borné à 132 (samyang.md § 5.5), 1900 ; un front vers l'ouvert : index 131, 18AA");
    m03[4] = m03[6] = 0x00;                       /* la consigne 1400, comme un a<f> de la carte */
    m03[5] = m03[7] = 0x14;
    bench_loop_msgs(m03, n03, m04, n04);
    bench_run_for(100 * MS);
    CHECK(ap05(at05(vd_next())) == 0x0118AA, "une consigne nouvelle dans le mode : la publication ne bouge pas, pas de "
          "resynchronisation (samyang.md § 5.5)");
    CHECK(turn_ap(-6) == 0x011855, "six fronts vers l'ouvert : l'index repart du sien, 125, 1855 — pas de la consigne");

    /* le bit 0x10 de l'offset 12 (la carte, autour d'un mouvement de focus) retarde la resynchronisation */
    k = turn(0);
    L.in.m2 = true;
    m03[4] = m03[6] = 0x00;
    m03[5] = m03[7] = 0x12;
    m03[13] = 0x10;
    bench_loop_msgs(m03, n03, m04, n04);
    bench_run_for(100 * MS);
    k = turn(0);
    L.in.m2 = false;
    bench_run_for(100 * MS);
    CHECK(ap05(at05(vd_next())) == 0x011855, "de retour en M1, le bit 0x10 posé : pas de resynchronisation, l'index d'avant "
          "publié (samyang.md § 5.5)");
    k = turn(0);
    m03[13] = 0x00;
    bench_loop_msgs(m03, n03, m04, n04);
    CHECK(ap05(at05(k + 4)) == 0x011855 && ap05(at05(k + 5)) == 0x011200,
          "le bit retombé : trois 0x03 attendent (les cases 3 et 4 de l'historique rendues inégales, S15 de lens135.c), le "
          "quatrième resynchronise, 1200");

    /* le bit AF du boîtier retiré */
    m04[4] = 0x81;
    bench_loop_msgs(m03, n03, m04, n04);
    bench_run_for(100 * MS);
    CHECK(ap05(at05(vd_next())) == 0, "un 0x04 sans le bit AF du boîtier (offset 3 à 81) : hors du mode (samyang.md § 5.5)");
    m04[4] = 0x83;
    bench_loop_msgs(m03, n03, m04, n04);
    bench_run_for(100 * MS);
    CHECK(ap05(at05(vd_next())) == 0x011200, "... le bit reposé : le mode, resynchronisé sur 1200");
    CHECK(!l135_out_of_model(&L), "rien hors modèle");
    m03[13] = 0x08;
    bench_loop_msgs(m03, n03, m04, n04);
    bench_run_for(50 * MS);
    CHECK(l135_out_of_model(&L), "le bit 3 de l'offset 12 (la table 1, S15 de lens135.c) : hors modèle");
}

/* ─────────────────────────── pannes reçues ─────────────────────────── */

static size_t raw_then(const uint8_t *b, size_t n, uint64_t wait)
{
    size_t from = g_bench.n;
    phy_sim_raw(b, n);
    bench_run_for(wait);
    return g_bench.n - from;
}

static void t_received_faults(void)
{
    uint8_t q07[16], bad[16], q0a[32];
    size_t n07 = fr_encode(2, 0x3D, Q07, sizeof Q07, q07, sizeof q07);
    size_t from;
    printf("samyang.md § 2.6 trames fautives reçues\n");
    power_and_handshake();
    bsk_phy_vd(60);
    CHECK(raw_then(q07, n07, 30 * MS) > 0, "référence : le 0x07 bien formé est servi");
    /* octets reçus BODY_CS basse : jamais stockés (samyang.md § 2.6) */
    from = g_bench.n;
    phy_sim_raw_cs_low(q07, n07);
    bench_run_for(30 * MS);
    CHECK(bench_count(from, 0x07) == 0, "0x07 reçu BODY_CS basse : jamais stocké, sans réponse");
    CHECK(raw_then(q07, n07, 30 * MS) > 0, "la trame suivante, BODY_CS haute, est servie");
    /* trame tronquée : l'analyseur compte la longueur annoncée et avale la suivante (samyang.md § 2.6, protocol.md § 3.2) */
    from = g_bench.n;
    phy_sim_raw(q07, 5);
    phy_sim_raw(q07, n07);
    bench_run_for(30 * MS);
    CHECK(bench_count(from, 0x07) == 0, "trame tronquée : jetée en silence, la suivante est avalée puis rejetée");
    CHECK(raw_then(q07, n07, 30 * MS) > 0, "retour au nominal : la trame d'après est servie");
    /* somme fausse, octet de fin faux : jetées en silence, sans décaler l'analyseur */
    memcpy(bad, q07, n07); bad[n07 - 3] ^= 0x01;
    CHECK(raw_then(bad, n07, 30 * MS) == 0, "somme fausse : jetée en silence");
    memcpy(bad, q07, n07); bad[n07 - 1] = 0x54;
    CHECK(raw_then(bad, n07, 30 * MS) == 0, "octet de fin faux : jetée en silence");
    CHECK(raw_then(q07, n07, 30 * MS) > 0, "la trame suivante est servie");
    /* longueur fausse */
    memcpy(bad, q07, n07); bad[1] = 8;
    CHECK(raw_then(bad, n07, 30 * MS) == 0, "longueur < 9 : trame abandonnée");
    memcpy(bad, q07, n07); bad[2] = 0x01;
    CHECK(raw_then(bad, n07, 30 * MS) == 0, "octet haut de longueur non nul : trame abandonnée (samyang.md § 2.6)");
    CHECK(raw_then(q07, n07, 30 * MS) > 0, "la trame suivante est servie");
    memcpy(bad, q07, n07); bad[1] = 11;
    from = g_bench.n;
    phy_sim_raw(bad, n07);
    phy_sim_raw(q07, n07);
    bench_run_for(30 * MS);
    CHECK(bench_count(from, 0x07) == 0, "longueur trop grande d'un octet : la fin de la trame d'après est prise pour la sienne");
    CHECK(raw_then(q07, n07, 30 * MS) > 0, "puis retour au nominal");
    /* aucun état ne change : un 0x0A à la somme fausse ne met pas en flux */
    fr_encode(2, 0x6B, Q0A, sizeof Q0A, q0a, sizeof q0a);
    q0a[sizeof Q0A + 5] ^= 0x10;
    bench_loop(true);
    from = g_bench.n;
    raw_then(q0a, sizeof Q0A + 8, 300 * MS);
    CHECK(bench_count(from, 0x0A) == 0 && bench_count(from, 0x05) == 0, "0x0A à la somme fausse : pas d'écho, pas de flux");
    /* type connu hors liste : consommé selon son descripteur, signalé, la suite est servie */
    {
        uint8_t m[] = {0x19, 0x00, 0x07, 0x00};
        uint8_t f[16];
        size_t n = fr_encode(2, 0x01, m, sizeof m, f, sizeof f);
        from = g_bench.n;
        raw_then(f, n, 30 * MS);
        CHECK(bench_count(from, 0x19) == 0 && bench_count(from, 0x07) == 1 && l135_unmodelled(&L, 0x19) == 1,
              "0x19 (hors liste) : consommé, compté non modélisé, sans réponse ; le 0x07 qui suit est servi");
    }
    /* type sans taille : blocage jusqu'à la coupure (samyang.md § 2.6) */
    {
        uint8_t m[] = {0x02, 0x00};
        uint8_t f[16];
        size_t n = fr_encode(2, 0x01, m, sizeof m, f, sizeof f);
        raw_then(f, n, 30 * MS);
        CHECK(l135_blocked(&L), "0x02 (sans descripteur) : le répartiteur boucle");
        from = g_bench.n;
        CHECK(raw_then(q07, n07, 2000 * MS) == 0, "bloqué : plus rien ne sort, même pour un 0x07");
        l135_power(&L, phy_sim_now(), false);
        bench_run_for(100 * MS);
        l135_power(&L, phy_sim_now(), true);
        bench_run_for(30 * MS);
        bsk_phy_body_cs(true);
        bench_run_for(3 * MS);
        bsk_phy_body_cs(false);
        bench_run_for(3 * MS);
        CHECK(!l135_blocked(&L) && raw_then(q07, n07, 30 * MS) > 0, "la coupure d'alimentation, seule sortie : servi à nouveau");
    }
    /* hors modèle : un type hors de la table n'a pas d'effet établi */
    {
        uint8_t m[] = {0x00, 0x00};
        uint8_t f[16];
        size_t n = fr_encode(2, 0x01, m, sizeof m, f, sizeof f);
        raw_then(f, n, 30 * MS);
        CHECK(l135_out_of_model(&L), "type 0 : hors modèle, signalé");
    }
}

/* ─────────────────────────── pannes émises (spec § 11) ─────────────────────────── */

static void t_emitted_faults(void)
{
    uint64_t t_ref, t_late;
    size_t from;
    long i;
    printf("spec § 11 pannes émises par le faux 135\n");
    power_and_handshake();
    L.f.silent = true;
    from = g_bench.n;
    bench_send(2, Q07, sizeof Q07);
    bench_run_for(100 * MS);
    CHECK(g_bench.n == from && L.silenced == 1, "silence : ni trame ni LENS_CS");

    power_and_handshake();
    from = g_bench.n;
    bench_send(2, Q07, sizeof Q07);
    t_ref = phy_sim_now();
    bench_run_for(100 * MS);
    i = bench_find(from, 0x07, 0, 0);
    t_ref = i >= 0 ? BENCH_EV(i).t - t_ref : 0;
    L.f.delay_us = 40 * MS;
    from = g_bench.n;
    bench_send(2, Q07, sizeof Q07);
    t_late = phy_sim_now();
    bench_run_for(200 * MS);
    i = bench_find(from, 0x07, 0, 0);
    t_late = i >= 0 ? BENCH_EV(i).t - t_late : 0;
    CHECK(i >= 0 && t_late == t_ref + 40 * MS && same_as_source(i, SRC_R07), "réponse retardée : la même, 40 ms plus tard");

    power_and_handshake();
    L.f.truncate_after = 12;
    from = g_bench.n;
    bench_send(2, Q07, sizeof Q07);
    bench_run_for(100 * MS);
    CHECK(bench_count(from, 0x07) == 0 && bench_count_kind(from, BSK_PHY_ERROR) == 1 &&
              BENCH_EV(g_bench.n - 1).kind == BSK_PHY_ERROR && BENCH_EV(g_bench.n - 1).err == E_FRAMING,
          "réponse tronquée après 12 octets : PHY remonte une E_FRAMING (début de trame périmé), aucune trame");
}

/* L'adaptateur du faux 135 à l'interface commune (lens_sim_135.c) : l'état « resté alimenté » passe
 * `flow` et `service` tels quels ; une mise en page (`grant`, celle du faux standard) n'est pas une donnée du 135,
 * dont le flux suit le masque des traces : refusée, rien n'est démarré. */
static void t_adapter(void)
{
    static const uint8_t grant[16] = {0xFF, 0x7F};
    lens_sim_powered_t with_grant = {.flow = true, .service = true, .grant = grant};
    lens_sim_powered_t svc = {.flow = true, .service = true};
    lens_sim_t l;
    printf("l'adaptateur du faux 135 : une mise en page refusée, le mode service passé\n");
    fresh();
    l = lens_sim_135(&L);
    CHECK(!lens_sim_start_powered(l, phy_sim_now(), &with_grant) && !L.powered, "une mise en page : refusée, rien démarré");
    CHECK(lens_sim_start_powered(l, phy_sim_now(), &svc) && L.powered && L.service && L.flow, "flux et mode service : passés");
    CHECK(lens_sim_byte_us(l) == P.byte_us && lens_sim_lens_cs_tail_us(l) == P.lens_cs_tail_us,
          "byte_us et lens_cs_tail_us : ceux du modèle");
    CHECK(!strcmp(l.ops->name, "faux Samyang AF 135"), "nom : %s", l.ops->name);
}

int main(int argc, char **argv)
{
    const char *traces;
    int gaps;
    if (argc != 2) {
        fprintf(stderr, "usage : test_lens135 <répertoire des traces de l'objectif>\n");
        return 2;
    }
    traces = argv[1];
    l135_params_default(&P);
    printf("provenance : gabarits relus sur leurs lignes (%s)\n", traces);
    gaps = sources135_verify(traces);
    CHECK(gaps == 0, "%d gabarit(s) ne sont pas sur leur ligne", gaps);
    t_power_on();
    t_handshake_uart();
    t_stayed_powered();
    t_init_replies();
    t_one_reply();
    t_flow();
    t_ring_o60();
    t_flow_guard();
    t_zero_mask_forgets();
    t_other_mask();
    t_cadence();
    t_homing();
    t_focus();
    t_acks_order();
    t_06_fields();
    t_stop_deadline();
    t_iris();
    t_service();
    t_custom();
    t_switch();
    t_native();
    t_received_faults();
    t_emitted_faults();
    t_adapter();
    printf("test_lens135 : %d vérifications, %d échec(s)\n", checks, fails);
    return fails ? 1 : 0;
}
