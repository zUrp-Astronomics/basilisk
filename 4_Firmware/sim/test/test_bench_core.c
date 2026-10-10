/* SOURCE: la liste blanche de la spec de l'atelier § 7.1.3 ; 7_Docs/E-Mount/samyang.md § 6.3 et § 7.4 pour le commutateur Custom
 * AUTHOR: engineer
 * DATE: 2026-09-27
 * STATUS: actif — joué par 4_Firmware/run.sh (job `firmware` de la CI), chaque vérification tenue par une mutation
 * Le vrai components/bench_core/bench_core.c, au-dessus d'une PHY qui note ce qu'on lui fait émettre ; l'exception du
 * commutateur Custom, pour le Samyang AF 135 déclaré seul, est jugée par t_custom135.
 *
 * La liste attendue est écrite ici à la main, d'après la spec (§ 7, § 7.1.3) et les tailles des
 * messages du boîtier : elle ne se lit pas dans le code jugé.
 * Chaque type passe à sa taille, et à aucune autre ; chaque type hors liste est refusé à toute taille ;
 * les 65536 sous-commandes du canal 0x40 sont jugées, Samyang reconnu ou non ; chaque refus rend
 * E_FORBIDDEN, n'émet rien et augmente le compteur. */
#include <stdio.h>
#include <string.h>

#include "bsk_bench_core.h"

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

/* ─────────────────── la PHY sous bench_core : elle note, elle rend ce qu'on lui dit ─────────────────── */

static unsigned sent;
static bsk_frame_t last;
static bsk_err_t phy_rc = E_OK;

bsk_err_t bsk_phy_send(const bsk_frame_t *f)
{
    sent++;
    last = *f;
    return phy_rc;
}

/* ─────────────────────────── la liste, à la main ─────────────────────────── */

static const struct { uint8_t type, size; } LISTED[] = {
    {0x01, 33}, {0x03, 21}, {0x04, 14}, {0x07, 2}, {0x08, 9}, {0x09, 5}, {0x0A, 17},
    {0x0B, 3},  {0x0D, 2},  {0x10, 2},  {0x1C, 1}, {0x1D, 5}, {0x3F, 2},
};
#define N_LISTED (sizeof LISTED / sizeof LISTED[0])
#define SVC_LEN 19

static int listed_size(uint8_t t)
{
    for (size_t i = 0; i < N_LISTED; i++)
        if (LISTED[i].type == t) return LISTED[i].size;
    return 0;
}

static bool svc_listed(uint8_t main, uint8_t sub)
{
    return (main == 'V' && sub == 0x00) || (main == 'F' && (sub == 0xFA || sub == 0xFB || sub == 0x32)) ||
           (main == 'M' && (sub == 0x00 || sub == 0x31));
}

/* Une trame de classe 2 : `n` octets de message, `msg` en tête, des zéros ensuite. */
static bsk_frame_t frame(const uint8_t *msg, size_t k, size_t n)
{
    bsk_frame_t f = {.cls = 2, .seq = 0x3D, .len = (uint16_t)n};
    memset(f.msg, 0, sizeof f.msg);
    memcpy(f.msg, msg, k);
    return f;
}

/* Soumet `f` pour l'objectif déclaré `lens` ; vrai si le verdict est celui attendu, émission et compteur compris. */
static bool judged_lens(const bsk_frame_t *f, bsk_bench_lens_t lens, bool pass)
{
    unsigned s0 = sent;
    uint32_t r0 = bsk_bench_refused();
    bsk_err_t rc = bsk_bench_send(f, lens);
    if (pass)
        return rc == E_OK && sent == s0 + 1 && memcmp(&last, f, sizeof *f) == 0 && bsk_bench_refused() == r0;
    return rc == E_FORBIDDEN && sent == s0 && bsk_bench_refused() == r0 + 1;
}

/* Samyang reconnu (BSK_BENCH_SAMYANG) ou non (BSK_BENCH_OTHER), hors le 135 déclaré. */
static bool judged(const bsk_frame_t *f, bool samyang, bool pass)
{
    return judged_lens(f, samyang ? BSK_BENCH_SAMYANG : BSK_BENCH_OTHER, pass);
}

/* ─────────────────────────── les tests ─────────────────────────── */

static void t_types(void)
{
    printf("§ 7.1.3 chaque type : à sa taille, et à aucune autre\n");
    for (int sy = 0; sy <= 1; sy++) {
        for (unsigned t = 0; t <= 255; t++) {
            uint8_t m = (uint8_t)t;
            int size = listed_size(m);
            unsigned bad = 0, first = 0;
            if (t == 0x40) continue;                      /* le canal de service : t_service */
            for (size_t n = 1; n <= BSK_MSG_MAX; n++) {
                bsk_frame_t f = frame(&m, 1, n);
                if (!judged(&f, sy, size && (int)n == size) && bad++ == 0) first = (unsigned)n;
            }
            if (size)
                CHECK(bad == 0, "0x%02X (%d octets), Samyang %s : %u verdict(s) faux, le premier à %u octets", t, size,
                      sy ? "reconnu" : "non reconnu", bad, first);
            else
                CHECK(bad == 0, "0x%02X hors liste, Samyang %s : %u taille(s) passent, la première %u", t,
                      sy ? "reconnu" : "non reconnu", bad, first);
        }
    }
}

static void t_service(void)
{
    unsigned bad_sy = 0, bad_other = 0, passed = 0;
    printf("§ 7.1.3 canal 0x40 : 'V' 00, 'F' FA/FB/32, 'M' 00/31, Samyang reconnu seulement\n");
    for (unsigned mc = 0; mc <= 255; mc++) {
        for (unsigned sc = 0; sc <= 255; sc++) {
            uint8_t m[3] = {0x40, (uint8_t)mc, (uint8_t)sc};
            bsk_frame_t f = frame(m, sizeof m, SVC_LEN);
            bool in = svc_listed((uint8_t)mc, (uint8_t)sc);
            if (!judged(&f, true, in)) {
                if (bad_sy++ == 0) printf("    Samyang reconnu : %02X %02X mal jugée\n", mc, sc);
            } else if (in) {
                passed++;
            }
            if (!judged(&f, false, false) && bad_other++ == 0)
                printf("    Samyang non reconnu : %02X %02X passe\n", mc, sc);
        }
    }
    CHECK(bad_sy == 0 && passed == 6, "Samyang reconnu : les 6 sous-commandes passent, les 65530 autres sont refusées (%u faux)",
          bad_sy);
    CHECK(bad_other == 0, "Samyang non reconnu : les 65536 sous-commandes sont refusées (%u passent)", bad_other);
    {
        uint8_t v[3] = {0x40, 'V', 0x00};
        bsk_frame_t f = frame(v, sizeof v, SVC_LEN - 1);
        CHECK(judged(&f, true, false), "'V' 00 de 18 octets : tronquée, refusée");
    }
}

static void t_frames(void)
{
    /* full:68 (0x04) suivi d'un 0x1D : la consigne de mise au point voyage dans la trame du 0x04 (spec § 3.3) */
    static const uint8_t M0401D[] = {0x04, 0x00, 0x00, 0x19, 0x83, 0x00, 0x00, 0x3D, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
                                     0x1D, 0x00, 0x00, 0x00, 0x00};
    /* full:67 : la trace porte un 0x2F après le 0x03, qui n'est pas dans la liste */
    static const uint8_t M032F[] = {0x03, 0xC2, 0x2E, 0x00, 0xB2, 0x11, 0xB2, 0x11, 0x1C, 0x00, 0x00, 0x06, 0x00, 0x00,
                                    0x02, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x2F, 0x5F, 0x5F};
    static const uint8_t M0414[] = {0x07, 0x00, 0x14, 0x00};
    static const uint8_t M4040[] = {0x40, 'V', 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                    0x40, 'X', 0x34};
    bsk_frame_t f;
    printf("§ 7 trames de plusieurs messages : chaque type est jugé\n");
    f = frame(M0401D, sizeof M0401D, sizeof M0401D);
    CHECK(judged(&f, false, true), "0x04 puis 0x1D dans la même trame : passe");
    f = frame(M032F, sizeof M032F, sizeof M032F);
    CHECK(judged(&f, false, false), "0x03 puis 0x2F (full:67) : refusée");
    f = frame(M0414, sizeof M0414, sizeof M0414);
    CHECK(judged(&f, true, false), "0x07 puis 0x14 : refusée");
    f = frame(M4040, sizeof M4040, 2 * SVC_LEN);
    CHECK(judged(&f, true, false), "'V' 00 puis 'X' 34 : refusée");
    f = frame(M0414, 0, 0);
    CHECK(judged(&f, true, false), "trame sans message : refusée");
    {
        /* une longueur au-delà de msg[] : la mémoire qui suit ne porte que des 0x1C (arrêt, 1 octet,
         * listé), de sorte qu'une barrière qui lirait au-delà de BSK_MSG_MAX laisserait passer */
        struct { bsk_frame_t f; uint8_t after[64]; } big;
        memset(&big, 0x1C, sizeof big);
        big.f.cls = 2;
        big.f.len = BSK_MSG_MAX;
        CHECK(judged(&big.f, false, true), "témoin : BSK_MSG_MAX arrêts 0x1C passent");
        big.f.len = BSK_MSG_MAX + 1;
        CHECK(judged(&big.f, false, false), "longueur BSK_MSG_MAX + 1, au-delà de msg[] : refusée, comptée, rien d'émis");
    }
}

/* L'exception du commutateur Custom, écrite ici à la main d'après 7_Docs/E-Mount/samyang.md § 6.3 et § 7.4 : 'P' 0x38 avec
 * un octet de données 0x30 + (haut << 4 | bas), chaque position 0, 1 ou 2 — les neuf valeurs ci-dessous —, et 'P' 0xFA
 * avec tout octet sauf 0x53 ; pour le 135 déclaré (BSK_BENCH_SAMYANG135) seul. */
static const uint8_t WRITE_OK[] = {0x30, 0x31, 0x32, 0x40, 0x41, 0x42, 0x50, 0x51, 0x52};

static bool write_ok(uint8_t d)
{
    for (size_t i = 0; i < sizeof WRITE_OK; i++)
        if (WRITE_OK[i] == d) return true;
    return false;
}

static void t_custom135(void)
{
    static const bsk_bench_lens_t OTHERS[] = {BSK_BENCH_OTHER, BSK_BENCH_SAMYANG};
    unsigned bad = 0, passed = 0, bad38 = 0, badfa = 0, bad_other = 0;
    printf("'P' 38 (9 valeurs) et 'P' FA (hors 53) au 135 déclaré seul ; la liste inchangée pour les autres objectifs\n");
    for (unsigned mc = 0; mc <= 255; mc++) {
        for (unsigned sc = 0; sc <= 255; sc++) {
            uint8_t m[3] = {0x40, (uint8_t)mc, (uint8_t)sc};
            bsk_frame_t f = frame(m, sizeof m, SVC_LEN);   /* octet de données 00 */
            bool in = svc_listed((uint8_t)mc, (uint8_t)sc) || (mc == 'P' && sc == 0xFA);
            if (!judged_lens(&f, BSK_BENCH_SAMYANG135, in)) {
                if (bad++ == 0) printf("    135 : %02X %02X 00 mal jugée\n", mc, sc);
            } else if (in) {
                passed++;
            }
        }
    }
    CHECK(bad == 0 && passed == 7, "135, octet de données 00 : la liste (6) et 'P' FA passent, les 65529 autres sont refusées "
          "(%u faux, %u passent)", bad, passed);
    for (unsigned d = 0; d <= 255; d++) {
        uint8_t w[4] = {0x40, 'P', 0x38, (uint8_t)d}, r[4] = {0x40, 'P', 0xFA, (uint8_t)d};
        bsk_frame_t fw = frame(w, sizeof w, SVC_LEN), fr = frame(r, sizeof r, SVC_LEN);
        if (!judged_lens(&fw, BSK_BENCH_SAMYANG135, write_ok((uint8_t)d)) && bad38++ == 0) printf("    135 : 'P' 38 %02X mal jugée\n", d);
        if (!judged_lens(&fr, BSK_BENCH_SAMYANG135, d != 0x53) && badfa++ == 0) printf("    135 : 'P' FA %02X mal jugée\n", d);
        for (size_t k = 0; k < sizeof OTHERS / sizeof OTHERS[0]; k++) {
            if (!judged_lens(&fw, OTHERS[k], false) && bad_other++ == 0) printf("    déclaration %d : 'P' 38 %02X passe\n", OTHERS[k], d);
            if (!judged_lens(&fr, OTHERS[k], false) && bad_other++ == 0) printf("    déclaration %d : 'P' FA %02X passe\n", OTHERS[k], d);
        }
    }
    CHECK(bad38 == 0, "135 : 'P' 38 passe avec 30-32, 40-42, 50-52, et aucune autre donnée (%u faux)", bad38);
    {
        static const uint8_t DATA[] = {0x00, 0x30, 0x41, 0x52, 0x53};   /* refusée et acceptées par 'P' 38, la valeur MTF */
        unsigned badp = 0;
        for (unsigned sc = 0; sc <= 255; sc++)
            for (size_t k = 0; k < sizeof DATA; k++) {
                uint8_t m[4] = {0x40, 'P', (uint8_t)sc, DATA[k]};
                bsk_frame_t f = frame(m, sizeof m, SVC_LEN);
                bool in = (sc == 0x38 && write_ok(DATA[k])) || (sc == 0xFA && DATA[k] != 0x53);
                if (!judged_lens(&f, BSK_BENCH_SAMYANG135, in) && badp++ == 0) printf("    135 : 'P' %02X %02X mal jugée\n", sc, DATA[k]);
            }
        CHECK(badp == 0, "135 : toute autre sous-commande de 'P' refusée, avec une donnée que 'P' 38 accepte aussi (%u faux)", badp);
    }
    CHECK(badfa == 0, "135 : 'P' FA passe avec toute donnée sauf 53 (%u faux)", badfa);
    CHECK(bad_other == 0, "un autre Samyang reconnu (le 24 mm) et un objectif non reconnu : 'P' 38 et 'P' FA refusées à toute "
          "donnée (%u passent)", bad_other);
    {
        uint8_t w[4] = {0x40, 'P', 0x38, 0x41};
        bsk_frame_t f = frame(w, sizeof w, SVC_LEN - 1);
        CHECK(judged_lens(&f, BSK_BENCH_SAMYANG135, false), "'P' 38 41 de 18 octets : tronquée, refusée");
        f = frame(w, sizeof w, SVC_LEN);
        f.msg[SVC_LEN] = 0x14;                         /* suivie d'un 0x14 (16 octets, hors liste) */
        f.len = SVC_LEN + 16;
        CHECK(judged_lens(&f, BSK_BENCH_SAMYANG135, false), "'P' 38 41 suivie d'un 0x14 : refusée");
    }
}

/* Une déclaration hors de l'énumération (bsk_bench_core.h : OTHER 0, SAMYANG 1, SAMYANG135 2) : la barrière ne sait pas
 * quel objectif est déclaré, elle ferme le canal 0x40, liste et exception du 135 comprises ; le reste de la liste ne dépend
 * pas de la déclaration et passe. Valeurs écrites à la main : la première au-delà, le bit haut d'un octet, un octet plein,
 * et -1. */
static void t_out_of_domain(void)
{
    static const unsigned BAD[] = {3, 0x80, 0xFF, (unsigned)-1};
    static const uint8_t DATA[] = {0x00, 0x41};       /* 'P' FA 00 et 'P' 38 41 : l'exception du 135 */
    static const uint8_t M07[] = {0x07, 0x00};
    printf("déclaration hors domaine (3, 0x80, 0xFF, -1) : le canal 0x40 fermé, le reste de la liste passe\n");
    for (size_t b = 0; b < sizeof BAD / sizeof BAD[0]; b++) {
        bsk_bench_lens_t lens = (bsk_bench_lens_t)BAD[b];
        unsigned passed = 0;
        bsk_frame_t f = frame(M07, sizeof M07, sizeof M07);
        for (unsigned mc = 0; mc <= 255; mc++)
            for (unsigned sc = 0; sc <= 255; sc++)
                for (size_t k = 0; k < sizeof DATA; k++) {
                    uint8_t m[4] = {0x40, (uint8_t)mc, (uint8_t)sc, DATA[k]};
                    bsk_frame_t g = frame(m, sizeof m, SVC_LEN);
                    if (!judged_lens(&g, lens, false) && passed++ == 0)
                        printf("    déclaration %u : %02X %02X %02X passe\n", BAD[b], mc, sc, DATA[k]);
                }
        CHECK(passed == 0, "déclaration %u : les 131072 messages 0x40 refusés, 'V' 00 et 'P' 38 41 compris (%u passent)", BAD[b],
              passed);
        CHECK(judged_lens(&f, lens, true), "déclaration %u : 07 00 passe (hors du canal 0x40)", BAD[b]);
    }
}

static void t_phy_result(void)
{
    static const uint8_t M07[] = {0x07, 0x00};
    bsk_frame_t f = frame(M07, sizeof M07, sizeof M07);
    uint32_t r0 = bsk_bench_refused();
    printf("§ 7 ce qui passe rend l'issue de PHY\n");
    phy_rc = E_BUS;
    CHECK(bsk_bench_send(&f, false) == E_BUS && bsk_bench_refused() == r0, "E_BUS de PHY rendu tel quel, pas un refus");
    phy_rc = E_OK;
}

int main(void)
{
    CHECK(bsk_bench_refused() == 0, "aucun refus au démarrage");
    t_types();
    t_service();
    t_frames();
    t_custom135();
    t_out_of_domain();
    t_phy_result();
    printf("test_bench_core : %d vérifications, %d échec(s) ; %u émissions, %lu refus\n", checks, fails, sent,
           (unsigned long)bsk_bench_refused());
    return fails ? 1 : 0;
}
