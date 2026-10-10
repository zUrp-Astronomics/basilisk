/* SOURCE: spec de l'atelier § 1 (R1, R4), § 2 et § 12 — ce que PHY laisse remonter
 * AUTHOR: engineer
 * DATE: 2026-09-27
 * STATUS: actif — joué par 4_Firmware/run.sh (job `firmware` de la CI), chaque vérification tenue par une mutation
 * Le codec, le flux (rx_t, fr_scan) et la présence (pres_t) de components/phy/phy_common.c, communs à la carte et à la
 * PHY simulée ; et la PHY simulée : E_BUS injecté, émissions de l'objectif injectées, trame perdue ou fausse, lignes
 * relâchées, verrou de XDETECT.
 *
 * Seules remontent une trame syntaxiquement valide ou une erreur nommée (bsk_phy.h). E_BUS, l'erreur matérielle que
 * signale le pilote UART de la carte, est injectée dans la PHY simulée. D2 : l'insertion après 300 ms d'anti-rebond, la
 * retombée décidée 2 ms après son premier front, qui coupe rails et lignes si D2 est toujours absent. */
#define _POSIX_C_SOURCE 200809L   /* alarm, la garde de durée */

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bench.h"
#include "frame.h"
#include "lens_sim_135.h"
#include "phy_common.h"
#include "phy_sim.h"

#define MS 1000u
#define DROP (2 * MS)   /* les 2 ms de la retombée, écrites à la main (un mutant de D2_DROP_US rougit) */
#define T0 (1000u * MS)

static l135_t L;
static l135_params_t P;
static int checks, fails;

/* Une réception qui ne rend plus la main (des octets rejugés sans fin) est un échec, pas un plantage : la garde de durée
 * le dit comme une vérification (sortie 1), et mutants.sh le compte tué. */
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

/* full:12, la réponse du 135 au 0x07 : 43 octets, classe 2 */
static const char R07[] = "F0 2B 00 02 00 07 01 03 70 01 00 01 05 00 00 08 00 00 00 00 00 60 92 86 5E 00 00 00 "
                          "00 00 00 00 00 00 00 00 00 00 00 00 8D 02 55";

/* Recalcule la somme d'une trame de `n` octets : seul le défaut voulu la rend fausse. */
static void resum(uint8_t *b, size_t n)
{
    uint16_t s = fr_sum(b, n);
    b[n - 3] = (uint8_t)s;
    b[n - 2] = (uint8_t)(s >> 8);
}

/* La réponse du Sony FE 24-105 G au 0x01, 41 octets : 7_Docs/E-Mount/traces/sony.txt ligne 11.
 * Le Sony rabaisse LENS_CS avant que l'UART ait rendu son dernier octet : 40 sur 41 reçus à la retombée (une
 * capture de la carte face au Sony, non publiée : 7_Docs/E-Mount/provenance.md § 3). */
static const char SONY01[] = "F0 29 00 02 00 01 FF 9F FF 5D EE 60 18 DE FF 0F F8 01 00 00 00 00 00 00 00 00 00 00 00 00 "
                             "00 00 00 00 00 00 00 00 71 07 55";

static size_t r07(uint8_t *b) { return fr_hex(R07, b, 64); }

static fr_scan_t scan(const uint8_t *b, size_t n)
{
    bsk_frame_t f;
    size_t len;
    return fr_scan(b, n, &f, &len);
}

static void t_valid(void)
{
    uint8_t b[64];
    size_t n = r07(b), len = 0;
    bsk_frame_t f;
    printf("trame valide (full:12)\n");
    CHECK(n == 43 && fr_scan(b, n, &f, &len) == FR_OK && len == 43 && f.cls == 2 && f.seq == 0 && f.len == 35 &&
              f.msg[0] == 0x07 && f.msg[34] == 0x00,
          "la réponse 0x07 du 135 se décode : classe 2, séquence 0, 35 octets de message");
    for (uint8_t cls = 1; cls <= 3; cls++) {
        uint8_t m[] = {0x07, 0x00}, t[16];
        size_t k = fr_encode(cls, 0x3D, m, sizeof m, t, sizeof t);
        CHECK(fr_decode(t, k, &f) == E_OK && f.cls == cls, "classe %u admise", cls);
    }
}

static void t_malformed(void)
{
    uint8_t b[64], two[128], big[300];
    size_t n, len;
    bsk_frame_t f;
    printf("trames malformées : FR_BAD, ou FR_SHORT pour un début de trame que les octets reçus n'achèvent pas\n");

    n = r07(b);
    CHECK(scan(b, n - 1) == FR_SHORT && fr_decode(b, n - 1, &f) == E_FRAMING, "tronquée d'un octet : la suite attendue");
    CHECK(scan(b, 12) == FR_SHORT, "tronquée après 12 octets : la suite attendue");
    CHECK(scan(b, 2) == FR_SHORT && scan(b, 1) == FR_SHORT, "F0 et moins de 3 octets : longueur illisible, la suite attendue");

    n = r07(b);
    b[1] = 0x2C;                       /* annonce 44 octets, en porte 43 */
    resum(b, n);
    CHECK(scan(b, n) == FR_SHORT && fr_decode(b, n, &f) == E_FRAMING, "longueur annoncée trop grande : la suite attendue");
    n = r07(b);
    b[1] = 0x2A;                       /* annonce 42 octets, en porte 43 */
    resum(b, n);
    CHECK(fr_decode(b, n, &f) == E_FRAMING && scan(b, n) == FR_BAD, "longueur annoncée trop petite");
    /* au-delà de BSK_FRAME_MAX : 299 octets annoncés, et 300 reçus, pour que seule la borne refuse */
    memset(big, 0, sizeof big);
    (void)r07(big);
    big[2] = 0x01;
    CHECK(scan(big, sizeof big) == FR_BAD && scan(big, 3) == FR_BAD,
          "longueur au-delà de BSK_FRAME_MAX : écartée, même sans la suite (emount.c:355-363)");

    n = r07(b);
    b[n - 3] ^= 0x01;
    CHECK(scan(b, n) == FR_BAD, "somme fausse (octet bas)");
    n = r07(b);
    b[n - 2] ^= 0x01;
    CHECK(scan(b, n) == FR_BAD, "somme fausse (octet haut)");

    n = r07(b);
    b[n - 1] = 0x54;
    CHECK(scan(b, n) == FR_BAD, "55 absent");

    n = r07(b);
    b[0] = 0xF1;
    CHECK(fr_decode(b, n, &f) == E_FRAMING && scan(b, n) == FR_BAD && scan(b, 1) == FR_BAD, "F0 absent");

    {
        unsigned admitted = 0, first = 0;
        for (unsigned cls = 0; cls <= 255; cls++) {
            if (cls >= 1 && cls <= 3) continue;
            n = r07(b);
            b[3] = (uint8_t)cls;
            resum(b, n);
            if (scan(b, n) != FR_BAD && admitted++ == 0) first = cls;
        }
        CHECK(admitted == 0, "classe hors 1 à 3 : %u admise(s), dont %u", admitted, first);
    }

    /* deux trames à la suite : la première passe, puis ce qui suit est examiné à part */
    n = r07(b);
    memcpy(two, b, n);
    memcpy(two + n, b, n);
    len = 0;
    CHECK(fr_scan(two, 2 * n, &f, &len) == FR_OK && len == n && scan(two + n, n) == FR_OK, "deux trames à la suite");
    two[n + 1] = 0x05;                 /* la seconde annonce 5 octets, moins que le plus court (9) */
    two[n + 2] = 0x00;
    CHECK(scan(two + n, n) == FR_BAD && scan(two + n, 3) == FR_BAD, "un en-tête de 5 octets : écarté");
    two[n + 1] = 0x09;                 /* 9 octets, le plus court admis : un début plausible */
    CHECK(scan(two + n, 3) == FR_SHORT, "un en-tête de 9 octets, 3 reçus : la suite attendue");
}

/* ─────────────────────────── le flux ───────────────────────────
 * Le flux de la carte et du simulateur (rx_t), nourri ici à la main : des blocs d'octets tels que
 * l'UART les rend, jugés aux instants dits. */

static rx_t R;

#define STALE_US 20000u   /* les 20 ms de silence, écrites ici à la main : RX_STALE_US est jugé */

/* `n` octets reçus en un bloc, à l'instant t ; un bloc qui ne tient pas dans le tampon est un échec, pas un débordement. */
static void got(const uint8_t *b, size_t n, uint64_t t)
{
    CHECK(n <= RX_CAP - R.n, "le bloc de %zu octets tient dans le tampon (%zu octets déjà)", n, R.n);
    if (n > RX_CAP - R.n) return;
    memcpy(R.b + R.n, b, n);
    rx_got(&R, n, t);
}

/* Le prochain événement du flux à l'instant `now` : son genre, ou -1 s'il n'y en a pas. */
static int next_ev(uint64_t now, bsk_phy_event_t *e)
{
    memset(e, 0, sizeof *e);
    return rx_next(&R, now, e) ? (int)e->kind : -1;
}

static bool rejected_is(const bsk_phy_event_t *e, uint64_t t, const uint8_t *b, unsigned n)
{
    unsigned len = n < BSK_PHY_RAW_MAX ? n : BSK_PHY_RAW_MAX;
    bool ok = e->kind == BSK_PHY_ERROR && e->u.err == E_FRAMING && e->t_us == t && e->u.raw.n == n && e->u.raw.len == len &&
              !memcmp(e->u.raw.b, b, len);
    if (!ok)
        printf("  événement : genre %d, t = %llu, err %d, n=%u len=%u\n", (int)e->kind, (unsigned long long)e->t_us,
               (int)e->u.err, e->u.raw.n, e->u.raw.len);
    return ok;
}

static void t_stream(void)
{
    static uint8_t b[RX_CAP];
    uint8_t r07b[64], sony[64];
    size_t n = r07(r07b), ns = fr_hex(SONY01, sony, sizeof sony);
    bsk_phy_event_t e;
    const uint64_t t = 5000;
    printf("le flux : trames extraites sur leur longueur, octets écartés, début de trame périmé\n");

    memset(&R, 0, sizeof R);
    memcpy(b, r07b, n);
    memcpy(b + n, r07b, n);
    got(b, 2 * n, t);
    CHECK(next_ev(t, &e) == BSK_PHY_FRAME && e.t_us == t && e.u.frame.msg[0] == 0x07 && e.u.frame.len == 35 &&
              next_ev(t, &e) == BSK_PHY_FRAME && e.u.frame.msg[0] == 0x07 && next_ev(t, &e) < 0 && R.n == 0,
          "deux trames collées dans un même bloc de l'UART : deux trames, rien d'autre");

    b[0] = 0x00;                        /* un octet parasite, puis un F0 qui n'ouvre pas de trame (longueur 5) */
    memcpy(b + 1, (const uint8_t[]){0xF0, 0x05, 0x00}, 3);
    memcpy(b + 4, r07b, n);
    got(b, 4 + n, t);
    CHECK(next_ev(t, &e) == BSK_PHY_ERROR && rejected_is(&e, t, b, 4), "00 F0 05 00 : quatre octets écartés, rapportés");
    CHECK(next_ev(t, &e) == BSK_PHY_FRAME && e.u.frame.msg[0] == 0x07 && next_ev(t, &e) < 0 && R.n == 0,
          "puis la trame qui les suit");

    CHECK(ns == 41 && scan(sony, ns) == FR_OK, "la réponse du Sony au 0x01 (capture v1) : 41 octets, une trame valide");
    got(sony, 40, t);
    CHECK(next_ev(t, &e) < 0 && R.n == 40, "le Sony, 40 octets sur 41 : rien ne remonte, la suite est attendue");
    CHECK(next_ev(t + STALE_US, &e) < 0 && rx_deadline(&R) == t + STALE_US + 1,
          "20 ms sans suite : toujours attendue (plus de 20 ms, link.c:260)");
    got(sony + 40, 1, t + STALE_US);
    CHECK(next_ev(t + STALE_US, &e) == BSK_PHY_FRAME && e.u.frame.msg[0] == 0x01 && e.u.frame.len == 33 &&
              next_ev(t + STALE_US, &e) < 0 && R.n == 0,
          "le 55 reçu : la réponse au 0x01 remonte entière (33 octets de message)");

    got(sony, 40, t);
    CHECK(next_ev(t + STALE_US + 1, &e) == BSK_PHY_ERROR && rejected_is(&e, t + STALE_US + 1, sony, 40),
          "un début de trame sans suite depuis plus de 20 ms : écarté, ses 40 octets rapportés");
    CHECK(next_ev(t + STALE_US + 1, &e) < 0 && R.n == 0 && rx_deadline(&R) == UINT64_MAX, "puis plus rien");

    memcpy(b, (const uint8_t[]){0xF0, 0xFF, 0x00}, 3);   /* un début plausible, 255 octets annoncés, puis une trame */
    memcpy(b + 3, r07b, n);
    got(b, 3 + n, t);
    CHECK(next_ev(t + STALE_US, &e) < 0 && R.n == 3 + n,
          "F0 FF 00 puis une trame : le début plausible attend sa suite, la trame derrière lui aussi (emount.c:364-367)");
    CHECK(next_ev(t + STALE_US + 1, &e) == BSK_PHY_ERROR && rejected_is(&e, t + STALE_US + 1, b, 3) &&
              next_ev(t + STALE_US + 1, &e) == BSK_PHY_FRAME && e.u.frame.msg[0] == 0x07 && next_ev(t + STALE_US + 1, &e) < 0,
          "… jusqu'à plus de 20 ms : F0 FF 00 écartés, puis la trame");

    got((const uint8_t[]){0x01, 0x02, 0x03}, 3, t);
    CHECK(next_ev(t + 100, &e) < 0 && R.n == 0 && R.junk.n == 3,
          "trois octets sans F0 : écartés, hors du tampon dès leur jugement, rapportés seulement à la trame suivante");
    got(r07b, n, t + 100);
    CHECK(next_ev(t + 100, &e) == BSK_PHY_ERROR && rejected_is(&e, t + 100, (const uint8_t[]){0x01, 0x02, 0x03}, 3) &&
              next_ev(t + 100, &e) == BSK_PHY_FRAME && next_ev(t + 100, &e) < 0,
          "… qui les suit : les trois octets, puis la trame");
    got((const uint8_t[]){0x01, 0x02, 0x03}, 3, t);
    CHECK(next_ev(t + STALE_US + 1, &e) == BSK_PHY_ERROR && rejected_is(&e, t + STALE_US + 1, (const uint8_t[]){0x01, 0x02, 0x03}, 3),
          "… ou quand le flux s'est tu plus de 20 ms");

    for (size_t i = 0; i < sizeof b; i++) b[i] = (uint8_t)(i * 7);
    got(b, 100, t);
    CHECK(next_ev(t, &e) < 0 && R.n == 0, "100 octets sans trame : écartés, hors du tampon, en attente d'être rapportés");
    got(b + 100, sizeof b - 100, t);
    CHECK(next_ev(t, &e) == BSK_PHY_ERROR && rejected_is(&e, t, b, RX_CAP) && R.n == 0,
          "4096 octets écartés : rapportés sans attendre, 4096 octets (les 256 premiers relevés)");
}

/* Le bruit va au néant (audit M1, décision de l'humain du 2026-10-07) : un octet écarté sort du tampon dès son jugement,
 * jamais rejugé ; le tampon ne garde qu'un début de trame qui attend sa suite. 4100 octets de bruit reçus un à un et
 * jugés à chaque octet, comme la PHY simulée : le seul F0 (i = 144, 144 × 7 = 1008 = 0x3F0) attend deux octets, puis sa
 * longueur F7 FE (65271) l'écarte ; le tampon n'a jamais plus de deux octets ; une E_FRAMING au 4096e octet, de 4096
 * octets ; les 4 derniers rapportés après 20 ms de silence. */
static void t_stream_void(void)
{
    static uint8_t b[4100];
    bsk_phy_event_t e;
    size_t most = 0, errs = 0, at = 0;
    const uint64_t t = 5000;
    bool first = false;
    printf("le flux : le bruit au néant, jugé une fois, rapporté par 4096\n");
    memset(&R, 0, sizeof R);
    for (size_t i = 0; i < sizeof b; i++) b[i] = (uint8_t)(i * 7);
    for (size_t i = 0; i < sizeof b; i++) {
        got(b + i, 1, t);
        while (next_ev(t, &e) >= 0) {
            errs++;
            at = i;
            first = rejected_is(&e, t, b, 4096);
        }
        if (R.n > most) most = R.n;
    }
    CHECK(most == 2, "le tampon garde deux octets au plus, le F0 et sa longueur incomplète (%zu)", most);
    CHECK(errs == 1 && at == 4095 && first, "une E_FRAMING, au 4096e octet, de 4096 octets (%zu, au %zu)", errs, at + 1);
    CHECK(R.n == 0 && R.junk.n == 4 && next_ev(t + STALE_US + 1, &e) == BSK_PHY_ERROR &&
              rejected_is(&e, t + STALE_US + 1, b + 4096, 4),
          "les 4 derniers, écartés, rapportés quand le flux s'est tu plus de 20 ms");

    /* le même bruit lu par blocs, comme l'UART de la carte : 4000 octets, puis 100 ; une E_FRAMING de 4096, pas plus, les
     * 4 suivants en attente */
    got(b, 4000, t);
    CHECK(next_ev(t, &e) < 0 && R.junk.n == 4000, "4000 octets : écartés, en attente");
    got(b + 4000, 100, t);
    CHECK(next_ev(t, &e) == BSK_PHY_ERROR && rejected_is(&e, t, b, 4096) && next_ev(t, &e) < 0 && R.n == 0 && R.junk.n == 4,
          "100 de plus : une E_FRAMING de 4096 octets exactement, les 4 suivants en attente");
    memset(&R, 0, sizeof R);

    /* l'E_BUS vide la réception (rx_flush) : les octets écartés qui attendaient leur E_FRAMING, puis le début de trame en
     * attente (F0 2B 00 02 00, 43 octets annoncés), dans l'ordre reçu ; rien n'est rapporté une seconde fois */
    {
        static const uint8_t in[] = {0x01, 0x02, 0x03, 0xF0, 0x2B, 0x00, 0x02, 0x00};
        bsk_phy_raw_t raw;
        got(in, sizeof in, t);
        CHECK(next_ev(t, &e) < 0 && R.n == 5 && R.junk.n == 3, "01 02 03 écartés, F0 2B 00 02 00 en attente");
        rx_flush(&R, &raw);
        CHECK(raw.n == 8 && raw.len == 8 && !memcmp(raw.b, in, sizeof in) && R.n == 0 && R.junk.n == 0 &&
                  rx_deadline(&R) == UINT64_MAX,
              "rx_flush : les 8 octets, les écartés puis le début de trame ; la réception vide");
        CHECK(next_ev(t + STALE_US + 1, &e) < 0, "plus de 20 ms après : aucune E_FRAMING, rien n'est rapporté deux fois");
    }
}

/* ─────────────────────────── E_BUS injecté ─────────────────────────── */

static const uint8_t Q07[] = {0x07, 0x00};  /* full:11 */

/* Mise sous tension et poignée de main par la carte (protocol.md § 2), comme test_lens135. */
static void power_and_handshake(void)
{
    l135_init(&L, &P);
    bench_init(lens_sim_135(&L), T0);
    phy_sim_mounted();
    (void)bsk_phy_lines(true);         /* relâchées, BODY_CS et l'émission n'arrivent pas à l'objectif */
    l135_power(&L, phy_sim_now(), true);
    bench_run_for(30 * MS);
    bsk_phy_body_cs(true);
    bench_run_for(3 * MS);
    bsk_phy_body_cs(false);
    bench_run_for(3 * MS);
}

/* Demande le 0x07 et avance jusqu'à ce que l'objectif lève LENS_CS pour répondre (au plus 50 ms). */
static bool until_reply_starts(void)
{
    size_t from = g_bench.n;
    bench_send(2, Q07, sizeof Q07);
    for (int i = 0; i < 500; i++) {
        bench_run_for(100);
        for (size_t k = from; k < g_bench.n; k++)
            if (BENCH_EV(k).kind == BSK_PHY_LENS_CS && BENCH_EV(k).level) return true;
    }
    return false;
}

/* Le relevé de la dernière erreur (bsk_phy_raw_t) : ses champs, et ses `len` octets, depuis `want` (NULL :
 * non jugés). */
static bool raw_is(unsigned n, unsigned len, const uint8_t *want)
{
    const bsk_phy_raw_t *r = &g_bench.raw;
    bool ok = r->n == n && r->len == len && (!want || !memcmp(r->b, want, len));
    if (!ok) printf("  relevé : n=%u len=%u\n", r->n, r->len);
    return ok;
}

static void t_bus_error(void)
{
    size_t from;
    uint64_t t;
    bsk_frame_t f = {.cls = 2, .seq = 0x3D, .len = sizeof Q07};
    printf("E_BUS injecté dans la PHY simulée\n");

    power_and_handshake();
    from = g_bench.n;
    CHECK(until_reply_starts(), "témoin : le 135 répond au 0x07");
    bench_run_for(100);                /* quelques octets de la réponse déjà reçus */
    t = phy_sim_now();
    phy_sim_bus_error();
    {
        size_t k = g_bench.n;
        uint8_t r[64];
        size_t nr = r07(r);
        uint64_t tc = 0;
        unsigned got;
        for (size_t i = from; i < k; i++)
            if (BENCH_EV(i).kind == BSK_PHY_LENS_CS && BENCH_EV(i).level) tc = BENCH_EV(i).t;
        got = tc ? (unsigned)((t - tc) / 13) : 0;  /* le faux 135 : un octet tous les 13 µs après la levée de LENS_CS */
        bench_run_for(1);
        CHECK(g_bench.n == k + 1 && BENCH_EV(k).kind == BSK_PHY_ERROR && BENCH_EV(k).err == E_BUS && BENCH_EV(k).t == t &&
                  got >= 1 && got < nr && raw_is(got, got, r),
              "l'erreur remontée est E_BUS, à l'instant de l'injection, et porte les %u octets de la réponse reçus avant elle, "
              "ceux du flux qu'elle vide", got);
    }
    bench_run_for(50 * MS);
    CHECK(bench_count_kind(from, BSK_PHY_ERROR) == 2 && bench_count(from, 0x07) == 0 &&
              BENCH_EV(g_bench.n - 1).kind == BSK_PHY_ERROR && BENCH_EV(g_bench.n - 1).err == E_FRAMING,
          "E_BUS pendant la réponse : le tampon vidé, la suite de la réponse écartée (E_FRAMING), aucune trame");
    from = g_bench.n;
    bench_send(2, Q07, sizeof Q07);
    bench_run_for(50 * MS);
    CHECK(bench_count(from, 0x07) == 1 && bench_count_kind(from, BSK_PHY_ERROR) == 0,
          "la réponse suivante, après la levée de LENS_CS, remonte entière");
    from = g_bench.n;
    phy_sim_bus_error();
    bench_run_for(1);
    CHECK(g_bench.n == from + 1 && BENCH_EV(from).kind == BSK_PHY_ERROR && BENCH_EV(from).err == E_BUS && raw_is(0, 0, NULL),
          "le flux vide : l'E_BUS ne porte aucun octet");

    /* des octets écartés qui attendent leur E_FRAMING (01 02 03, le flux pas encore tu 20 ms), puis un début de trame qui
     * attend sa suite (F0 2B 00 02 00 : 43 octets annoncés) : l'E_BUS porte les 8, dans l'ordre reçu ; aucune E_FRAMING
     * ensuite, aucun octet rapporté deux fois */
    {
        static const uint8_t in[] = {0x01, 0x02, 0x03, 0xF0, 0x2B, 0x00, 0x02, 0x00};
        size_t from0 = g_bench.n;
        phy_sim_lens_raw(in, sizeof in);
        bench_run_for(1 * MS);
        from = g_bench.n;
        CHECK(bench_count_kind(from0, BSK_PHY_ERROR) == 0, "les 8 octets reçus : rien de rapporté encore");
        phy_sim_bus_error();
        bench_run_for(50 * MS);
        CHECK(bench_count_kind(from, BSK_PHY_ERROR) == 1 && BENCH_EV(from).kind == BSK_PHY_ERROR && BENCH_EV(from).err == E_BUS &&
                  raw_is(8, 8, in),
              "E_BUS : les 3 octets écartés puis le début de trame, 8 octets, et aucune E_FRAMING ensuite");
    }

    power_and_handshake();
    from = g_bench.n;
    phy_sim_send_bus_error();
    f.msg[0] = Q07[0];
    f.msg[1] = Q07[1];
    CHECK(bsk_phy_send(&f) == E_BUS, "émission : E_BUS injecté, rendu à l'appelant");
    bench_run_for(50 * MS);
    CHECK(g_bench.n == from, "émission en E_BUS : rien n'est parti, l'objectif ne répond pas");
    CHECK(bsk_phy_send(&f) == E_OK, "l'émission suivante passe");
    bench_run_for(50 * MS);
    CHECK(bench_count(from, 0x07) == 1, "l'émission suivante reçoit sa réponse");
}

/* ─────────────────────────── les lignes relâchées ─────────────────────────── */

/* Sur la carte, lignes relâchées, la sortie de BODY_CS est coupée et TXD n'est plus routée à l'UART (bsk_phy.h,
 * bsk_phy_lines) : ni BODY_CS ni une émission n'arrivent à l'objectif. Le 135 ne lève LENS_CS qu'à BODY_CS haute plus
 * d'une milliseconde (protocol.md § 2) et répond au 0x07 (full:12). */
static void t_lines_rest(void)
{
    bsk_frame_t f = {.cls = 2, .seq = 0x3D, .len = sizeof Q07};
    size_t from;
    printf("lignes relâchées : BODY_CS et l'émission n'arrivent pas au faux objectif\n");
    f.msg[0] = Q07[0];
    f.msg[1] = Q07[1];
    l135_init(&L, &P);
    bench_init(lens_sim_135(&L), T0);
    phy_sim_mounted();
    l135_power(&L, phy_sim_now(), true);
    bench_run_for(30 * MS);
    from = g_bench.n;
    bsk_phy_body_cs(true);
    bench_run_for(3 * MS);
    CHECK(bench_count_kind(from, BSK_PHY_LENS_CS) == 0 && phy_sim_wires()->body_cs == PHY_SIM_REST,
          "lignes relâchées, BODY_CS commandée haute 3 ms : rien n'est piloté, LENS_CS ne se lève pas");
    bsk_phy_body_cs(false);
    CHECK(bsk_phy_lines(true), "les lignes pilotées");
    from = g_bench.n;
    bsk_phy_body_cs(true);
    bench_run_for(3 * MS);
    CHECK(bench_count_kind(from, BSK_PHY_LENS_CS) == 1 && BENCH_EV(g_bench.n - 1).level,
          "témoin, lignes pilotées : BODY_CS haute 3 ms, LENS_CS levée");
    bsk_phy_body_cs(false);
    bench_run_for(3 * MS);
    CHECK(bsk_phy_lines(false), "la poignée de main faite, les lignes relâchées");
    from = g_bench.n;
    CHECK(bsk_phy_send(&f) == E_OK, "le 0x07 émis lignes relâchées : E_OK, la carte ne sait pas que rien ne part");
    bench_run_for(50 * MS);
    CHECK(g_bench.n == from, "rien n'arrive au 135 : ni LENS_CS levée, ni réponse");
    CHECK(bsk_phy_lines(true) && bsk_phy_send(&f) == E_OK, "les lignes pilotées de nouveau, le même 0x07");
    bench_run_for(50 * MS);
    CHECK(bench_count(from, 0x07) == 1, "le 0x07 est servi (full:12)");
}

/* ─────────────────────────── une trame perdue ou fausse ─────────────────────────── */

static phy_sim_fate_t fate_07;          /* ce qui arrive aux trames 0x07 dans le sens `fate_to_lens` */
static bool fate_to_lens;
static unsigned fate_asked;             /* trames 0x07 présentées au crochet dans ce sens */

static phy_sim_fate_t fault_07(bool to_lens, const bsk_frame_t *f)
{
    if (to_lens != fate_to_lens || f->msg[0] != 0x07) return PHY_SIM_PASS;
    fate_asked++;
    return fate_07;
}

/* Le 0x07 demandé, 50 ms joués : l'instant de la réponse rendue (0 sans elle) et la réponse, dans `r`. */
static uint64_t ask_07(bsk_frame_t *r, size_t *from)
{
    *from = g_bench.n;
    bench_send(2, Q07, sizeof Q07);
    bench_run_for(50 * MS);
    for (size_t k = *from; k < g_bench.n; k++)
        if (BENCH_EV(k).kind == BSK_PHY_FRAME && BENCH_EV(k).frame.msg[0] == 0x07) {
            *r = BENCH_EV(k).frame;
            return BENCH_EV(k).t;
        }
    return 0;
}

static void t_fault(void)
{
    bsk_frame_t r, r2;
    size_t from;
    uint64_t t;
    uint8_t b[BSK_FRAME_MAX];
    size_t n;
    printf("une trame perdue sur le fil, ou reçue fausse (phy_sim_fault)\n");

    power_and_handshake();
    t = ask_07(&r, &from);
    CHECK(t > 0 && bench_count_kind(from, BSK_PHY_ERROR) == 0, "témoin, sans crochet : la réponse au 0x07 remonte");

    power_and_handshake();                  /* vers l'objectif : perdue, la carte n'en sait rien */
    phy_sim_fault(fault_07);
    fate_to_lens = true;
    fate_07 = PHY_SIM_LOSE;
    fate_asked = 0;
    {
        bsk_frame_t f = {.cls = 2, .seq = 0, .len = sizeof Q07};
        memcpy(f.msg, Q07, sizeof Q07);
        from = g_bench.n;
        CHECK(bsk_phy_send(&f) == E_OK, "vers l'objectif, perdue : bsk_phy_send rend E_OK");
        bench_run_for(50 * MS);
        CHECK(fate_asked == 1 && g_bench.n == from, "… rien n'arrive à l'objectif : ni LENS_CS, ni réponse, ni erreur");
    }
    fate_07 = PHY_SIM_GARBLE;
    bench_run_for(50 * MS);
    from = g_bench.n;
    bench_send(2, Q07, sizeof Q07);
    bench_run_for(50 * MS);
    CHECK(fate_asked == 2 && bench_count(from, 0x07) == 0 && bench_count_kind(from, BSK_PHY_ERROR) == 0,
          "vers l'objectif, fausse : prise comme perdue");
    fate_07 = PHY_SIM_PASS;
    CHECK(ask_07(&r2, &from) > 0 && fate_asked == 3, "vers l'objectif : la suivante passe, et reçoit sa réponse");

    power_and_handshake();                  /* depuis l'objectif : perdue, rien ne remonte */
    phy_sim_fault(fault_07);
    fate_to_lens = false;
    fate_07 = PHY_SIM_LOSE;
    fate_asked = 0;
    CHECK(ask_07(&r2, &from) == 0 && fate_asked == 1 && bench_count_kind(from, BSK_PHY_ERROR) == 0 &&
              bench_count_kind(from, BSK_PHY_LENS_CS) == 2,
          "depuis l'objectif, perdue : ni trame ni erreur, l'objectif a bien émis (LENS_CS levée puis rabaissée)");

    power_and_handshake();                  /* depuis l'objectif : fausse, un E_FRAMING à sa place, ses octets */
    phy_sim_fault(fault_07);
    fate_07 = PHY_SIM_GARBLE;
    fate_asked = 0;
    CHECK(ask_07(&r2, &from) == 0 && fate_asked == 1, "depuis l'objectif, fausse : la trame ne remonte pas");
    n = fr_encode(r.cls, r.seq, r.msg, r.len, b, sizeof b);
    {
        size_t k;
        for (k = from; k < g_bench.n && BENCH_EV(k).kind != BSK_PHY_ERROR; k++) {}
        CHECK(bench_count_kind(from, BSK_PHY_ERROR) == 1 && k < g_bench.n && BENCH_EV(k).err == E_FRAMING &&
                  BENCH_EV(k).t == t && raw_is((unsigned)n, (unsigned)n, b),
              "… un E_FRAMING à sa place, à l'instant où elle aurait remonté (%llu µs), qui porte ses %zu octets",
              (unsigned long long)t, n);
    }
    phy_sim_fault(NULL);
    CHECK(ask_07(&r2, &from) > 0, "sans crochet : la réponse suivante remonte");
}

/* ─────────────────────────── émissions injectées ─────────────────────────── */

/* Un événement attendu : `v` est le type de la trame (FRAME), l'erreur (ERROR) ou le niveau (LENS_CS). */
typedef struct { bsk_phy_event_kind_t kind; uint64_t t; unsigned v; } want_t;

/* Les événements notés depuis `from` sont exactement `w`, dans l'ordre, aux instants dits. */
static bool events_are(size_t from, const want_t *w, size_t n, const char *what)
{
    bool ok = g_bench.n - from == n;
    for (size_t i = 0; ok && i < n; i++) {
        const bench_ev_t *e = &BENCH_EV(from + i);
        unsigned v = e->kind == BSK_PHY_FRAME ? e->frame.msg[0] : e->kind == BSK_PHY_ERROR ? (unsigned)e->err : e->level;
        ok = e->kind == w[i].kind && e->t == w[i].t && v == w[i].v;
    }
    if (!ok) {
        printf("  %s : %zu événement(s) notés, %zu attendus\n", what, g_bench.n - from, n);
        for (size_t i = from; i < g_bench.n; i++)
            printf("    genre %d, t = %llu, err %d, niveau %d\n", (int)BENCH_EV(i).kind, (unsigned long long)BENCH_EV(i).t,
                   (int)BENCH_EV(i).err, (int)BENCH_EV(i).level);
    }
    return ok;
}

/* Une émission de l'objectif, injectée à l'instant courant (le faux 135 hors tension) ; rend cet instant. */
static uint64_t emit(const uint8_t *b, size_t n)
{
    uint64_t t = phy_sim_now();
    phy_sim_lens_raw(b, n);
    bench_run_for(100 * MS);
    return t;
}

static void t_lens_emission(void)
{
    static uint8_t b[4100];
    size_t from, n;
    uint64_t t;
    printf("émissions de l'objectif injectées : octets écartés, trame trop longue, 4096 écartés, le 55 du Sony après LENS_CS\n");
    l135_init(&L, &P);
    bench_init(lens_sim_135(&L), T0);

    /* la réponse au 0x07 (full:12), puis 5 octets sans F0 : 48 octets, un tous les 13 µs, LENS_CS retombée
     * 200 µs après le dernier (byte_us et lens_cs_tail_us du faux 135) ; les 5 octets rapportés quand le flux s'est tu
     * plus de 20 ms (STALE_US) */
    n = r07(b);
    memcpy(b + n, (const uint8_t[]){0x01, 0x02, 0x03, 0x04, 0x05}, 5);
    from = g_bench.n;
    t = emit(b, n + 5);
    {
        const want_t w[] = {{BSK_PHY_LENS_CS, t, 1},
                            {BSK_PHY_FRAME, t + 43 * 13, 0x07},
                            {BSK_PHY_LENS_CS, t + 48 * 13 + 200, 0},
                            {BSK_PHY_ERROR, t + 48 * 13 + STALE_US + 1, E_FRAMING}};
        CHECK(events_are(from, w, 4, "trame puis octets invalides"),
              "trame valide puis 5 octets invalides : LENS_CS levée, la trame à son dernier octet, LENS_CS retombée, "
              "E_FRAMING 20 ms après le dernier octet");
    }
    CHECK(raw_is(5, 5, (const uint8_t[]){0x01, 0x02, 0x03, 0x04, 0x05}), "E_FRAMING : les 5 octets écartés");

    /* F0, longueur 300 (2C 01) : plus que BSK_FRAME_MAX, en 300 octets */
    for (size_t i = 0; i < 300; i++) b[i] = (uint8_t)i;
    memcpy(b, (const uint8_t[]){0xF0, 0x2C, 0x01, 0x02, 0x00}, 5);
    from = g_bench.n;
    t = emit(b, 300);
    {
        const want_t w[] = {{BSK_PHY_LENS_CS, t, 1}, {BSK_PHY_LENS_CS, t + 300 * 13 + 200, 0},
                            {BSK_PHY_ERROR, t + 300 * 13 + STALE_US + 1, E_FRAMING}};
        CHECK(events_are(from, w, 3, "trame trop longue"), "trame de 300 octets : écartée, E_FRAMING 20 ms après, rien d'autre");
    }
    CHECK(raw_is(300, 256, b), "trame trop longue : 300 octets écartés, les 256 premiers relevés (44 omis)");

    /* 4100 octets sans trame : écartés, rapportés au 4096e (4096 octets écartés) ; les 4 derniers 20 ms après */
    for (size_t i = 0; i < sizeof b; i++) b[i] = (uint8_t)(i * 7);
    from = g_bench.n;
    t = emit(b, sizeof b);
    {
        const want_t w[] = {{BSK_PHY_LENS_CS, t, 1}, {BSK_PHY_ERROR, t + 4096 * 13, E_FRAMING},
                            {BSK_PHY_LENS_CS, t + 4100 * 13 + 200, 0}, {BSK_PHY_ERROR, t + 4100 * 13 + STALE_US + 1, E_FRAMING}};
        CHECK(events_are(from, w, 4, "4096 écartés"),
              "4100 octets : E_FRAMING au 4096e (4096 octets écartés), puis les 4 derniers 20 ms après");
    }
    CHECK(raw_is(4, 4, b + 4096), "les 4 derniers octets");

    n = r07(b);
    from = g_bench.n;
    t = emit(b, n);
    {
        const want_t w[] = {{BSK_PHY_LENS_CS, t, 1}, {BSK_PHY_FRAME, t + 43 * 13, 0x07}, {BSK_PHY_LENS_CS, t + 43 * 13 + 200, 0}};
        CHECK(events_are(from, w, 3, "après les 4096 écartés"), "l'émission suivante remonte entière");
    }

    /* Le Sony : LENS_CS retombée avec son 40e octet, le 55 final après elle */
    n = fr_hex(SONY01, b, 64);
    from = g_bench.n;
    t = phy_sim_now();
    phy_sim_lens_raw_late(b, n, 1);
    bench_run_for(100 * MS);
    {
        const want_t w[] = {{BSK_PHY_LENS_CS, t, 1}, {BSK_PHY_LENS_CS, t + 40 * 13, 0}, {BSK_PHY_FRAME, t + 41 * 13, 0x01}};
        CHECK(events_are(from, w, 3, "le Sony"),
              "la réponse du Sony au 0x01, son 55 reçu après la retombée de LENS_CS : la trame remonte entière, à son 55");
        CHECK(g_bench.n - from == 3 && BENCH_EV(from + 2).frame.len == 33 && BENCH_EV(from + 2).frame.msg[32] == 0x00,
              "ses 33 octets de message");
    }
}

/* ─────────────────────────── D2 et le verrou ─────────────────────────── */

/* Les événements BSK_PHY_PRESENCE notés depuis `from` : leur nombre, et le dernier. */
static size_t presence(size_t from, long *last)
{
    size_t n = 0;
    *last = -1;
    for (size_t k = from; k < g_bench.n; k++)
        if (BENCH_EV(k).kind == BSK_PHY_PRESENCE) { n++; *last = (long)k; }
    return n;
}

/* Les BSK_PHY_BOUNCE notés depuis `from` : leur nombre, et le dernier. */
static size_t bounces(size_t from, long *last)
{
    size_t n = 0;
    *last = -1;
    for (size_t k = from; k < g_bench.n; k++)
        if (BENCH_EV(k).kind == BSK_PHY_BOUNCE) { n++; *last = (long)k; }
    return n;
}

static bool all_rest(void)
{
    const phy_sim_wires_t *w = phy_sim_wires();
    return !w->rail[BSK_RAIL_LOGIC] && !w->rail[BSK_RAIL_MOTOR] && w->txd == PHY_SIM_REST && w->body_cs == PHY_SIM_REST &&
           w->vd == PHY_SIM_REST;
}

/* Tout allumé, comme la SESSION le fait : les deux rails, la VD, les lignes. true : chaque commande acceptée. */
static bool power_up(void)
{
    bool ok = bsk_phy_rail(BSK_RAIL_LOGIC, true);
    ok = bsk_phy_rail(BSK_RAIL_MOTOR, true) && ok;
    ok = bsk_phy_vd(60) && ok;
    return bsk_phy_lines(true) && ok;
}

/* Chaque commande qui allume refusée, et rien de changé. */
static void refused_all(const char *when)
{
    CHECK(!bsk_phy_rail(BSK_RAIL_LOGIC, true) && all_rest(), "%s : bsk_phy_rail(logique, on) refusé, rien d'allumé", when);
    CHECK(!bsk_phy_rail(BSK_RAIL_MOTOR, true) && all_rest(), "%s : bsk_phy_rail(moteur, on) refusé, rien d'allumé", when);
    CHECK(!bsk_phy_lines(true) && all_rest(), "%s : bsk_phy_lines(true) refusé, lignes au repos", when);
    CHECK(!bsk_phy_vd(60) && all_rest(), "%s : bsk_phy_vd(60) refusé, VD au repos", when);
    CHECK(bsk_phy_rail(BSK_RAIL_LOGIC, false) && bsk_phy_rail(BSK_RAIL_MOTOR, false) && bsk_phy_lines(false) && bsk_phy_vd(0),
          "%s : couper, relâcher, arrêter passent", when);
}

/* L'objectif inséré et confirmé, présence lue ; tout allumé. */
static void inserted(void)
{
    l135_init(&L, &P);
    bench_init(lens_sim_135(&L), T0);
    phy_sim_d2(true);
    bench_run_for(300 * MS);
    CHECK(power_up(), "insertion confirmée : rails, VD et lignes acceptés");
}

static void t_presence(void)
{
    size_t from;
    long last;
    uint64_t t;
    printf("D2 : insertion après 300 ms d'anti-rebond (v1, control.c:256) ; avant, le verrou refuse tout\n");
    l135_init(&L, &P);
    bench_init(lens_sim_135(&L), T0);
    bench_run_for(1000 * MS);
    CHECK(presence(0, &last) == 0, "D2 absent depuis le démarrage : rien ne remonte");
    refused_all("absent depuis le démarrage");

    t = phy_sim_now();
    phy_sim_d2(true);
    bench_run(t + 300 * MS - 1);
    CHECK(presence(0, &last) == 0, "présent depuis 300 ms moins 1 µs : rien encore");
    refused_all("présent depuis 300 ms moins 1 µs");
    bench_run_for(1000 * MS);
    CHECK(presence(0, &last) == 1 && BENCH_EV(last).level && BENCH_EV(last).t == t + 300 * MS,
          "présent tenu 300 ms : une présence, à 300 ms");
    CHECK(power_up() && !all_rest(), "insertion confirmée : le verrou est levé, tout s'allume");

    l135_init(&L, &P);
    bench_init(lens_sim_135(&L), T0);
    phy_sim_d2(true);
    bench_run_for(100 * MS);
    phy_sim_d2(false);
    bench_run_for(50 * MS);
    t = phy_sim_now();
    phy_sim_d2(true);
    bench_run(t + 300 * MS - 1);
    CHECK(presence(0, &last) == 0, "branchement qui rebondit : rien, 300 ms moins 1 µs après le dernier rebond");
    refused_all("branchement qui rebondit, 300 ms moins 1 µs après le dernier rebond");
    bench_run_for(1000 * MS);
    CHECK(presence(0, &last) == 1 && BENCH_EV(last).level && BENCH_EV(last).t == t + 300 * MS,
          "branchement qui rebondit : une présence, 300 ms après le dernier rebond, aucune absence");

    from = g_bench.n;
    phy_sim_d2(false);
    bench_run_for(100 * MS);
    phy_sim_d2(true);
    bench_run_for(100 * MS);
    CHECK(presence(from, &last) == 1 && !BENCH_EV(last).level, "une retombée brève après l'insertion : une absence");
    refused_all("100 ms après le retour du contact");
    t = phy_sim_now() - 100 * MS;
    bench_run_for(1000 * MS);
    CHECK(presence(from, &last) == 2 && BENCH_EV(last).level && BENCH_EV(last).t == t + 300 * MS,
          "puis une présence 300 ms après le retour du contact");
}

/* La retombée tenue coupe tout 2 ms après son premier front, pas avant, dans la PHY même, et remonte à cet instant, hors de
 * la file. */
static void t_drop(void)
{
    size_t from, sent0;
    long last;
    uint64_t t;
    printf("D2 retombe et reste absent : coupure 2 ms après, pas avant ; l'absence à cet instant\n");
    inserted();
    bench_loop(true);                                /* la paire 0x03/0x04 part à chaque front de VD : elle les compte */
    bench_run_for(100 * MS);
    CHECK(!all_rest() && g_bench.n_sent > 0, "avant : tout allumé, la VD tourne");
    t = phy_sim_now();
    from = g_bench.n;
    phy_sim_d2(false);
    CHECK(!all_rest(), "dans l'appel même : rien de coupé, l'échéance de 2 ms est armée");
    bench_run(t + DROP - 1);
    CHECK(!all_rest() && presence(from, &last) == 0, "2 ms moins 1 µs après la retombée : rien de coupé, aucune absence");
    phy_sim_run(t + DROP);
    CHECK(all_rest(), "à 2 ms, dans la PHY même, avant tout bsk_phy_poll : rails coupés, TXD, BODY_CS et VD au repos");
    bench_run(t + DROP);
    CHECK(presence(from, &last) == 1 && !BENCH_EV(last).level && BENCH_EV(last).t == t + DROP,
          "l'absence remonte, une, à l'instant de la coupure : 2 ms après la retombée");
    CHECK(bounces(from, &last) == 0, "aucun rebond rapporté");
    from = g_bench.n;
    sent0 = g_bench.n_sent;
    bench_run_for(1000 * MS);
    CHECK(presence(from, &last) == 0, "rien d'autre en 1 s");
    CHECK(g_bench.n_sent == sent0, "plus aucun front de VD");
    refused_all("1 s après la retombée");
}

/* 2 ms comptées du premier front de retombée ; D2 revenu à l'échéance, rien ; un front pendant les 2 ms ne
 * relance pas l'échéance. Instants exacts, sur l'horloge simulée. */
static void t_debounce(void)
{
    size_t from;
    long last;
    uint64_t t;
    printf("D2 : 2 ms d'anti-rebond sur la retombée, comptées du premier front\n");
    inserted();
    from = g_bench.n;
    t = phy_sim_now();
    phy_sim_d2(false);
    bench_run(t + 1 * MS);
    phy_sim_d2(true);
    bench_run(t + DROP);
    CHECK(!all_rest() && presence(from, &last) == 0, "absent 1 ms puis revenu : à l'échéance, rien de coupé, aucune présence");
    CHECK(bounces(from, &last) == 1 && BENCH_EV(last).t == t + DROP, "un rebond rapporté, à l'échéance, 2 ms après la retombée");
    bench_run_for(1000 * MS);
    CHECK(!all_rest() && presence(from, &last) == 0 && bounces(from, &last) == 1,
          "1 s après ce rebond : rien de coupé, aucune présence, un seul rebond");
    CHECK(power_up(), "le verrou n'a pas été posé : chaque commande est acceptée");

    inserted();
    from = g_bench.n;
    t = phy_sim_now();
    phy_sim_d2(false);                               /* fronts de retombée à 0, 600, 1100 et 1600 µs */
    for (unsigned k = 1; k <= 3; k++) {
        bench_run(t + k * 500u);
        phy_sim_d2(true);
        bench_run(t + k * 500u + 100u);
        phy_sim_d2(false);
    }
    bench_run(t + DROP - 1);
    CHECK(!all_rest() && presence(from, &last) == 0, "quatre fronts, absent : 2 ms moins 1 µs après le premier, rien");
    bench_run(t + DROP);
    CHECK(all_rest() && presence(from, &last) == 1 && !BENCH_EV(last).level && BENCH_EV(last).t == t + DROP,
          "à 2 ms du premier front, pas du dernier : coupé, l'absence à cet instant");
    CHECK(bounces(from, &last) == 0, "absent à l'échéance : aucun rebond rapporté");
    refused_all("après des fronts multiples, absent à l'échéance");

    inserted();
    from = g_bench.n;
    t = phy_sim_now();
    phy_sim_d2(false);                               /* 0 : absent ; 500 : revenu ; 800 : absent ; 1200 : revenu */
    bench_run(t + 500);
    phy_sim_d2(true);
    bench_run(t + 800);
    phy_sim_d2(false);
    bench_run(t + 1200);
    phy_sim_d2(true);
    bench_run(t + 3 * MS);
    CHECK(!all_rest() && presence(from, &last) == 0, "fronts multiples, revenu à l'échéance : rien de coupé, aucune présence");
    CHECK(bounces(from, &last) == 1 && BENCH_EV(last).t == t + DROP, "un rebond, un seul, à 2 ms du premier front");
    phy_sim_d2(false);                               /* après l'échéance : une nouvelle retombée, une nouvelle échéance */
    bench_run(t + 3 * MS + DROP - 1);
    CHECK(!all_rest() && presence(from, &last) == 0, "une retombée après l'échéance : 2 ms moins 1 µs après elle, rien");
    bench_run(t + 3 * MS + DROP);
    CHECK(all_rest() && presence(from, &last) == 1 && !BENCH_EV(last).level && BENCH_EV(last).t == t + 3 * MS + DROP,
          "à 2 ms d'elle : coupé, l'absence");
    CHECK(bounces(from, &last) == 1, "et pas d'autre rebond");
}

/* Pas de rallumage sans nouvelle insertion confirmée. */
static void t_lock(void)
{
    size_t from;
    long last;
    printf("verrou : aucune commande n'allume tant que D2 est absent, ni avant l'insertion confirmée\n");
    inserted();
    phy_sim_d2(false);
    from = g_bench.n;
    bench_run_for(5000 * MS);
    refused_all("5 s après la retombée");
    phy_sim_d2(true);
    bench_run_for(299 * MS);
    refused_all("D2 revenu depuis 299 ms");
    phy_sim_d2(false);
    bench_run_for(1000 * MS);
    CHECK(presence(from, &last) == 1 && !BENCH_EV(last).level, "un retour de 299 ms : aucune présence, une seule absence");
    refused_all("après un retour de 299 ms");
    phy_sim_d2(true);
    bench_run_for(300 * MS);
    CHECK(presence(from, &last) == 2 && BENCH_EV(last).level, "une insertion tenue 300 ms : la présence");
    CHECK(power_up() && !all_rest(), "puis tout s'allume de nouveau");
}

/* La présence de la carte (components/phy/phy.c), jouée sur pres_t seule : la tâche lit D2 toutes les 5 ms, l'interruption
 * prend la retombée. Un rebond lu par la tâche juste après la retombée ne lève pas le verrou : l'anti-rebond repart. */
static void t_pres_task(void)
{
    pres_t p = {0};
    bsk_phy_event_t e;
    uint64_t t;
    bool open = false;
    printf("la présence de la carte : lectures de 5 ms, la retombée par interruption, un rebond après elle\n");
    for (t = 0; t < 300 * MS; t += 5 * MS) (void)pres_sample(&p, true, t);
    CHECK(!p.present, "présent lu de 0 à 295 ms : pas encore");
    CHECK(pres_sample(&p, true, 300 * MS) && p.present, "la lecture de 300 ms confirme l'insertion");
    CHECK(pres_next(&p, UINT64_MAX, &e) && e.kind == BSK_PHY_PRESENCE && e.u.present && e.t_us == 300 * MS, "rendue, à 300 ms");
    pres_drop(&p, 1000 * MS + 1);
    CHECK(!p.present, "la retombée pose le verrou");
    for (t = 1005 * MS; t < 1305 * MS; t += 5 * MS) open = pres_sample(&p, true, t) || open || p.present;
    CHECK(!open, "présent relu dès 4 ms après la retombée (un rebond), 295 ms durant : le verrou tient");
    CHECK(pres_sample(&p, true, 1305 * MS) && p.present, "300 ms de lectures « présent » : l'insertion");
    CHECK(pres_next(&p, UINT64_MAX, &e) && !e.u.present && e.t_us == 1000 * MS + 1, "rendues : l'absence, à l'instant de la retombée décidée");
    CHECK(pres_next(&p, UINT64_MAX, &e) && e.u.present && e.t_us == 1305 * MS && !pres_next(&p, UINT64_MAX, &e),
          "puis la présence, et rien d'autre");

    /* Les gestes de detect_isr et drop_expire : un front de retombée objectif présent arme l'échéance,
     * les suivants non ; elle relit D2. */
    CHECK(pres_edge(&p, 2000 * MS) == PRES_ARM && p.present, "présent, un front de retombée : l'échéance armée, rien d'autre");
    CHECK(pres_edge(&p, 2000 * MS + 700) == PRES_WAIT && p.present, "un autre front avant elle : rien, elle n'est pas relancée");
    (void)pres_sample(&p, false, 2000 * MS + 1000);   /* la tâche lit « absent » pendant les 2 ms */
    (void)pres_sample(&p, true, 2000 * MS + 1500);
    CHECK(!pres_expire(&p, false, 2000 * MS + DROP) && p.present && !pres_next(&p, UINT64_MAX, &e),
          "à l'échéance, D2 revenu : un rebond, le verrou levé, rien à rendre");
    CHECK(pres_edge(&p, 2100 * MS) == PRES_ARM, "un front après l'échéance : une nouvelle échéance armée");
    CHECK(pres_expire(&p, true, 2100 * MS + DROP) && !p.present, "à elle, D2 absent : la coupure, le verrou posé");
    CHECK(pres_next(&p, UINT64_MAX, &e) && !e.u.present && e.t_us == 2100 * MS + DROP && !pres_next(&p, UINT64_MAX, &e),
          "l'absence rendue, à l'instant de l'échéance, une seule");
    (void)pres_sample(&p, true, 2150 * MS);          /* une insertion commence */
    CHECK(pres_edge(&p, 2200 * MS) == PRES_CUT && !p.present && !pres_next(&p, UINT64_MAX, &e),
          "objectif absent, un front de retombée : la coupure tout de suite, sans échéance, rien de plus à rendre");
    CHECK(!pres_sample(&p, true, 2450 * MS) && !pres_sample(&p, true, 2749 * MS) && pres_sample(&p, true, 2750 * MS),
          "l'anti-rebond de l'insertion repart de ce front : 300 ms depuis la lecture « présent » qui le suit");
}

/* La présence n'est jamais perdue : ni par une file pleine, ni par une couche qui ne lit pas. */
static void t_presence_kept(void)
{
    bsk_phy_event_t e;
    size_t n_bus = 0, n_pres = 0;
    bool first = false, alt = true, last = true;
    uint64_t t1, t2;
    printf("présence : jamais perdue par la file pleine, rendue à son rang, alternée\n");
    l135_init(&L, &P);
    bench_init(lens_sim_135(&L), T0);
    phy_sim_mounted();
    t1 = phy_sim_now();
    phy_sim_bus_error();                             /* en file, avant la retombée */
    phy_sim_run(t1 + 1 * MS);
    t2 = phy_sim_now() + DROP;
    phy_sim_d2(false);
    phy_sim_run(t2);                                 /* la retombée décidée */
    for (int i = 0; i < 1100; i++) phy_sim_bus_error();   /* EV_CAP = 1024 : la file déborde, le plus ancien est jeté */
    while (bsk_phy_poll(&e)) {
        if (e.kind == BSK_PHY_ERROR) n_bus++;
        if (e.kind == BSK_PHY_PRESENCE) {
            if (!n_pres) first = n_bus == 0 && e.t_us == t2 && !e.u.present;
            n_pres++;
        }
    }
    CHECK(n_pres == 1 && first, "file débordée derrière la retombée : l'absence est rendue, une fois, à son instant");
    CHECK(n_bus == 1024, "la file a jeté les plus anciens E_BUS, dont celui d'avant la retombée (%zu)", n_bus);

    l135_init(&L, &P);
    bench_init(lens_sim_135(&L), T0);
    phy_sim_mounted();
    t1 = phy_sim_now();
    phy_sim_bus_error();
    phy_sim_run(t1 + 1 * MS);
    phy_sim_d2(false);
    phy_sim_run(t1 + 4 * MS);                        /* la retombée décidée à 3 ms */
    phy_sim_bus_error();
    CHECK(bsk_phy_poll(&e) && e.kind == BSK_PHY_ERROR && e.t_us == t1, "dans l'ordre des instants : l'E_BUS d'avant");
    CHECK(bsk_phy_poll(&e) && e.kind == BSK_PHY_PRESENCE && !e.u.present && e.t_us == t1 + 1 * MS + DROP,
          "puis l'absence");
    CHECK(bsk_phy_poll(&e) && e.kind == BSK_PHY_ERROR && e.t_us == t1 + 4 * MS, "puis l'E_BUS d'après");

    /* Rien n'est lu : présent lu, retombée, insertion confirmée, retombée, insertion confirmée, retombée. */
    l135_init(&L, &P);
    bench_init(lens_sim_135(&L), T0);
    phy_sim_mounted();
    t1 = phy_sim_now();
    phy_sim_d2(false);
    for (int k = 0; k < 2; k++) {
        phy_sim_run(phy_sim_now() + DROP);     /* chaque retombée décidée */
        phy_sim_d2(true);
        phy_sim_run(phy_sim_now() + 300 * MS);
        phy_sim_d2(false);
    }
    phy_sim_run(phy_sim_now() + DROP);
    n_pres = 0;
    while (bsk_phy_poll(&e))
        if (e.kind == BSK_PHY_PRESENCE) { n_pres++; last = e.u.present; if (n_pres == 1) first = e.t_us == t1 + DROP; }
    CHECK(n_pres == 1 && !last && first, "trois retombées et deux insertions jamais lues : une absence, la première");

    l135_init(&L, &P);
    bench_init(lens_sim_135(&L), T0);
    phy_sim_mounted();
    phy_sim_d2(false);
    phy_sim_run(phy_sim_now() + DROP);         /* la retombée décidée */
    phy_sim_d2(true);
    phy_sim_run(phy_sim_now() + 300 * MS);
    n_pres = 0;
    last = false;
    while (bsk_phy_poll(&e))
        if (e.kind == BSK_PHY_PRESENCE) { alt = alt && e.u.present == (n_pres % 2 == 1); n_pres++; last = e.u.present; }
    CHECK(n_pres == 2 && alt && last, "une retombée puis une insertion jamais lues : l'absence, puis la présence");
}

int main(void)
{
    signal(SIGALRM, too_long);
    alarm(10);
    l135_params_default(&P);
    t_valid();
    t_malformed();
    t_stream();
    t_stream_void();
    t_bus_error();
    t_lines_rest();
    t_fault();
    t_lens_emission();
    t_presence();
    t_drop();
    t_debounce();
    t_lock();
    t_presence_kept();
    t_pres_task();
    printf("test_phy : %d vérifications, %d échec(s)\n", checks, fails);
    return fails ? 1 : 0;
}
