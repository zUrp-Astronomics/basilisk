/* SOURCE: 7_Docs/E-Mount/tamron.md et protocol.md ; analyse statique privée du firmware du F051
 * AUTHOR: engineer
 * DATE: 2026-09-29
 * STATUS: actif — joué par 4_Firmware/run.sh (job `firmware` de la CI) et par les mutants de sim/test/mutations_std.txt
 * Le faux objectif standard (Tamron F051, sim/lens_std.c) contre sa référence, piloté directement.
 *
 * Chaque test pilote sim/lens_std.c sans PHY simulée : il pose BODY_CS, les octets et les fronts VD
 * lui-même, et ne conclut que sur ce qui sort du modèle (octets et LENS_CS, horodatés) et sur ses signaux
 * déclarés (non modélisé, bloqué, hors modèle, trames remplacées ou tues, position pour les pannes).
 * L'attendu de chaque test est écrit à la main et cite 7_Docs/E-Mount/tamron.md (« § n ») ou protocol.md
 * (« protocol.md § n ») ; ce que seule l'analyse statique privée du firmware établit est dit tel. Les réponses
 * constantes sont celles que tamron.md § 1.2 publie, tout octet non publié à zéro. Chaque paramètre [NÉ] de
 * lens_std.h est joué sur au moins deux valeurs. */
#include <stdio.h>
#include <string.h>

#include "lens_std.h"

#define MS 1000u
#define T0 (1000u * MS)

static lstd_t L;
static lstd_params_t P;
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

/* ─────────────────────────── le fil, vu de la carte ─────────────────────────── */

/* Une émission de l'objectif : de LENS_CS haute à LENS_CS basse. */
typedef struct {
    uint64_t t_up, t_first, t_last, t_down;   /* t_first / t_last : fin du premier / dernier octet */
    uint16_t n;
    uint8_t  b[600];
} wire_t;

static wire_t W[1024];
static size_t nW;
static bool open_w;
static uint64_t now, vd_period, vd_next;

static void drain(void)
{
    lstd_out_t o;
    while (lstd_out(&L, &o)) {
        if (o.kind == LSTD_OUT_LENS_CS) {
            if (o.v) {
                if (nW < sizeof W / sizeof W[0]) {
                    memset(&W[nW], 0, sizeof W[nW]);
                    W[nW].t_up = o.t;
                    nW++;
                }
                open_w = true;
            } else {
                if (nW) W[nW - 1].t_down = o.t;
                open_w = false;
            }
        } else if (open_w && nW) {
            wire_t *w = &W[nW - 1];
            if (w->n == 0) w->t_first = o.t;
            w->t_last = o.t;
            if (w->n < sizeof w->b) w->b[w->n++] = o.v;
        } else {
            CHECK(0, "octet émis LENS_CS basse (t=%llu)", (unsigned long long)o.t);
        }
    }
}

/* Fait passer le temps jusqu'à t, avec les fronts VD de la carte. */
static void until(uint64_t t)
{
    while (vd_period && vd_next <= t) {
        lstd_vd(&L, vd_next);
        now = vd_next;
        vd_next += vd_period;
    }
    lstd_advance(&L, t);
    now = t;
    drain();
}

static void run_for(uint64_t us) { until(now + us); }

static void vd(uint64_t period)
{
    vd_period = period;
    vd_next = now + (period ? period : 0);
}

static void body_cs(bool high)
{
    lstd_body_cs(&L, now, high);
    drain();
}

/* Une trame de la carte : F0 | longueur | classe | séquence | message | somme | 55, BODY_CS haute
 * autour des octets, un octet toutes les 14 µs. `raw` : la trame telle quelle (corrompue). */
static void send_raw(const uint8_t *f, size_t n)
{
    until(now + 1);
    body_cs(true);
    for (size_t i = 0; i < n; i++) {
        until(now + 14);
        lstd_byte(&L, now, f[i]);
    }
    until(now + 20);
    body_cs(false);
}

static size_t encode(uint8_t cls, uint8_t seq, const uint8_t *m, size_t n, uint8_t *f)
{
    uint16_t len = (uint16_t)(n + 8), sum = 0;
    f[0] = 0xF0; f[1] = (uint8_t)len; f[2] = (uint8_t)(len >> 8); f[3] = cls; f[4] = seq;
    memcpy(f + 5, m, n);
    for (size_t i = 1; i + 3 < len; i++) sum = (uint16_t)(sum + f[i]);
    f[len - 3] = (uint8_t)sum; f[len - 2] = (uint8_t)(sum >> 8); f[len - 1] = 0x55;
    return len;
}

static void send(uint8_t cls, uint8_t seq, const uint8_t *m, size_t n)
{
    uint8_t f[1200];
    send_raw(f, encode(cls, seq, m, n, f));
}

static void send2(const uint8_t *m, size_t n) { send(2, 0, m, n); }

/* La trame reçue en W[i] est-elle bien formée, de cette classe, et porte-t-elle exactement ce message ? */
static bool is_frame(long i, uint8_t cls, const uint8_t *m, size_t n)
{
    uint16_t sum = 0;
    const wire_t *w;
    if (i < 0) return false;
    w = &W[i];
    if (w->n != n + 8 || w->b[0] != 0xF0 || (w->b[1] | w->b[2] << 8) != w->n || w->b[3] != cls ||
        w->b[w->n - 1] != 0x55)
        return false;
    for (size_t k = 1; k + 3 < w->n; k++) sum = (uint16_t)(sum + w->b[k]);
    if ((w->b[w->n - 3] | w->b[w->n - 2] << 8) != sum) return false;
    return memcmp(w->b + 5, m, n) == 0;
}

static bool same_wire(long i, const uint8_t *f, size_t n)
{
    return i >= 0 && W[i].n == n && memcmp(W[i].b, f, n) == 0;
}

/* Première émission depuis `from` dont le message commence par `type` (0 : n'importe laquelle avec octets). */
static long find(size_t from, uint8_t type)
{
    for (size_t i = from; i < nW; i++)
        if (W[i].n > 5 && (type == 0 || W[i].b[5] == type)) return (long)i;
    return -1;
}

static size_t count(size_t from, uint8_t type)
{
    size_t c = 0;
    for (size_t i = from; i < nW; i++)
        if (W[i].n > 5 && W[i].b[5] == type) c++;
    return c;
}

static size_t count_bytes(size_t from)
{
    size_t c = 0;
    for (size_t i = from; i < nW; i++) c += W[i].n;
    return c;
}

/* ─────────────────────────── mise en place ─────────────────────────── */

static void params(void)
{
    lstd_params_default(&P);
}

static void fresh(void)
{
    lstd_init(&L, &P);
    nW = 0;
    open_w = false;
    now = 0;
    vd_period = 0;
    until(T0);
}

/* Mise sous tension, poignée de main (BODY_CS haute puis basse) et mise en service. */
static void power_up(void)
{
    fresh();
    lstd_power(&L, now, true);
    run_for(P.boot_us + 1 * MS);
    body_cs(true);
    run_for(P.hs_us + 1 * MS);
    body_cs(false);
    run_for(P.service_us + 5 * MS);
}

/* Réponses d'init, écrites à la main d'après tamron.md § 1.2 ; tout octet non publié vaut zéro */
/* 0x01 : la liste des types de § 1.2, en carte par la règle de protocol.md § 7.1 */
static const uint8_t E01[33] = {0x01, 0xFF, 0x9F, 0x78, 0x5D, 0x82, 0x60, 0x18, 0x5E};
/* 0x07 : 01, 03 70 (protocol.md § 7.7), 01 03 = 3.01, A0, 34 C1 = 0xC134, 60 92 86 5E (protocol.md § 7.7) */
static const uint8_t E07[35] = {0x07, 0x01, 0x03, 0x70, 0x00, 0x00, 0x01, 0x03, 0x00, 0xA0, 0x34, 0xC1,
                                0x00, 0x00, 0x00, 0x00, 0x60, 0x92, 0x86, 0x5E};
static const uint8_t E09[12] = {0x09};
static const uint8_t E0D[2] = {0x0D, 0x00};

static const uint8_t Q01[33] = {0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01};
static const uint8_t Q07[2] = {0x07, 0x00};
static const uint8_t Q08[9] = {0x08, 0x02};
static const uint8_t Q09[5] = {0x09};
static const uint8_t Q0B[3] = {0x0B, 0x42, 0x00};
static const uint8_t Q0D[2] = {0x0D, 0x00};
static const uint8_t Q3F[2] = {0x3F, 0x00};
static const uint8_t Q0A[17] = {0x0A, 0xFF, 0x7F, 0, 0, 0, 0, 0, 0, 0x3F};   /* le masque de la carte (INIT0A) */
static const uint8_t Q04[14] = {0x04};
static const uint8_t Q03[21] = {0x03};
static const uint8_t Q1C[1] = {0x1C};

/* Les 0x02, trames entières écrites à la main (§ 2.3 : classe 3, séquence 0). */
static const uint8_t ERR1[16] = {0xF0, 0x10, 0x00, 0x03, 0x00, 0x02, 0xFF, 0xFF, 0x01, 0, 0, 0, 0, 0x14, 0x02, 0x55};
static const uint8_t ERR2[16] = {0xF0, 0x10, 0x00, 0x03, 0x00, 0x02, 0xFF, 0xFF, 0x02, 0, 0, 0, 0, 0x15, 0x02, 0x55};
static const uint8_t ERR3[16] = {0xF0, 0x10, 0x00, 0x03, 0x00, 0x02, 0xFF, 0xFF, 0x03, 0, 0, 0, 0, 0x16, 0x02, 0x55};
/* code 4, trame de classe 2 dont le premier sous-message est le fautif : 02 02 FF 04 */
static const uint8_t ERR4[16] = {0xF0, 0x10, 0x00, 0x03, 0x00, 0x02, 0x02, 0xFF, 0x04, 0, 0, 0, 0, 0x1A, 0x01, 0x55};
/* 0x08 : offsets 0-3, f/2,9 (0x1312) à f/22,6 (0x1900) ; 27-30 le numéro de série ; 85 bit 7 (§ 1.2) */
static const uint8_t E08[202] = {0x08, 0x12, 0x13, 0x00, 0x19};

/* Attend le calme après un front VD : le flux (VD + q05_us .. VD + q06_us + une trame) est passé, une
 * requête envoyée maintenant a sa réponse avant le front suivant. */
static void quiet(void)
{
    if (vd_period) until(vd_next + 7 * MS);
}

/* Le masque de 0x0A de la carte : 0x05 de 97 octets, 0x06 de 40 (protocol.md § 7.10). */
static void to_flow(void)
{
    vd(16667);
    quiet();
    send2(Q0A, 17);
    run_for(40 * MS);
    quiet();
}

/* ─────────────────────────── § 2.1, § 2.8 mise sous tension ─────────────────────────── */

static void t_power_on(void)
{
    uint64_t t_on, t_low;
    size_t from;
    long i;
    printf("§ 2.1, § 2.8 mise sous tension : poignée de main, puis phase init et état libre\n");
    for (int v = 0; v < 2; v++) {             /* boot_us et hs_us [NÉ] : deux valeurs */
        params();
        P.boot_us = v ? 80 * MS : 20 * MS;
        P.hs_us = v ? 7 * MS : 2 * MS;
        fresh();
        lstd_power(&L, now, true);
        t_on = now;
        body_cs(true);
        run_for(P.boot_us + P.hs_us + 5 * MS);
        CHECK(nW == 1 && W[0].n == 0 && W[0].t_up == t_on + P.boot_us && W[0].t_down == 0,
              "BODY_CS haute : LENS_CS levée boot_us après la mise sous tension, et tenue (§ 2.8)");
        body_cs(false);
        t_low = now;
        run_for(1 * MS);
        CHECK(W[0].t_down == t_low, "BODY_CS basse après la seconde étape : LENS_CS rabaissée aussitôt (§ 2.8)");
        run_for(P.service_us);
        from = nW;
        send2(Q01, sizeof Q01);
        run_for(10 * MS);
        i = find(from, 0x01);
        CHECK(is_frame(i, 2, E01, sizeof E01) && W[i].b[4] == 0,
              "le 0x01 est accepté en premier message : phase init, état libre (§ 2.1)");
    }
    /* BODY_CS basse : il attend (S2) ; BODY_CS basse avant la seconde étape : LENS_CS tombe à la seconde étape */
    params();
    fresh();
    lstd_power(&L, now, true);
    run_for(P.boot_us + 30 * MS);
    CHECK(nW == 0, "BODY_CS basse : LENS_CS ne se lève pas (§ 2.8)");
    body_cs(true);
    t_on = now;
    run_for(100);
    body_cs(false);
    run_for(P.hs_us + 1 * MS);
    CHECK(nW == 1 && W[0].t_up == t_on && W[0].t_down == t_on + P.hs_us,
          "BODY_CS rabaissée tôt : LENS_CS retombe à la seconde étape, hs_us après sa levée");
    /* rien n'est reçu avant l'ouverture de la liaison */
    params();
    fresh();
    lstd_power(&L, now, true);
    run_for(5 * MS);
    send2(Q07, sizeof Q07);
    run_for(P.boot_us + 5 * MS);
    body_cs(true);
    run_for(P.hs_us + 1 * MS);
    body_cs(false);
    run_for(20 * MS);
    CHECK(count(0, 0x07) == 0 && count_bytes(0) == 0, "une trame reçue avant la liaison n'est pas rangée : aucune réponse");
}

/* ─────────────────────────── § 1.2, § 2.2 réponses d'init ─────────────────────────── */

static void t_init_replies(void)
{
    uint8_t e08[202], e3f[66] = {0x3F, 0x00, 'E', ' ', '2', '4', 'm', 'm', ' ', 'F', '2', '.', '8', ' ', 'F', '0', '5', '1'};
    uint8_t q08b[9] = {0x08, 0x82};
    uint8_t e0b[3] = {0x0B, 0x42, 0x00};
    size_t from;
    long i;
    printf("§ 1.2 réponses d'init : champs publiés et tailles du F051\n");
    for (int v = 0; v < 2; v++) {             /* serial [NÉ] : deux valeurs */
        params();
        P.serial = v ? 12345678u : 0;
        power_up();
        from = nW;
        send2(Q01, sizeof Q01); run_for(10 * MS);
        send2(Q07, sizeof Q07); run_for(10 * MS);
        send2(Q08, sizeof Q08); run_for(10 * MS);
        send2(Q09, sizeof Q09); run_for(10 * MS);
        send2(Q0D, sizeof Q0D); run_for(10 * MS);
        send2(Q3F, sizeof Q3F); run_for(10 * MS);
        CHECK(is_frame(find(from, 0x01), 2, E01, 33), "0x01 : 33 octets, la carte des types, classe 2 (§ 1.2)");
        CHECK(is_frame(find(from, 0x07), 2, E07, 35), "0x07 : 35 octets, version 3.01, LensType2 0xC134 (§ 1.2)");
        memcpy(e08, E08, sizeof e08);
        e08[28] = (uint8_t)P.serial; e08[29] = (uint8_t)(P.serial >> 8);
        e08[30] = (uint8_t)(P.serial >> 16); e08[31] = (uint8_t)(P.serial >> 24);
        CHECK(is_frame(find(from, 0x08), 2, e08, 202),
              "0x08 : 202 octets, f/2,9 à f/22,6, offsets 27-30 = le numéro de série, offset 85 bit 7 = 0 (§ 1.2)");
        CHECK(is_frame(find(from, 0x09), 2, E09, 12), "0x09 : 09 et onze zéros (§ 1.2 : rien de publié)");
        CHECK(is_frame(find(from, 0x0D), 2, E0D, 2), "0x0D : 0D 00 (§ 1.2)");
        CHECK(is_frame(find(from, 0x3F), 2, e3f, 66),
              "0x3F : 66 octets, « E 24mm F2.8 F051 » à l'offset 1, l'offset 0 laissé par le 0D 00 (§ 1.2)");
        for (size_t k = from; k < nW; k++) CHECK(W[k].b[4] == 0, "classe 2 : séquence 0 (§ 2.2)");
    }
    from = nW;
    send2(q08b, sizeof q08b); run_for(10 * MS);
    memcpy(e08, E08, sizeof e08);
    e08[28] = 0x4E; e08[29] = 0x61; e08[30] = 0xBC; e08[31] = 0x00;   /* 12345678 */
    e08[86] = 0x80;
    CHECK(is_frame(find(from, 0x08), 2, e08, 202), "0x08 dont l'offset 0 a le bit 7 : offset 85 à 0x80 (§ 1.2, § 2.7)");
    from = nW;
    send2(Q0B, sizeof Q0B); run_for(10 * MS);
    send2(Q3F, sizeof Q3F); run_for(10 * MS);
    e3f[1] = 0x42;
    CHECK(is_frame(find(from, 0x0B), 2, e0b, 3), "0x0B : 0B <offset 0> 00 (§ 1.2)");
    CHECK(is_frame(find(from, 0x3F), 2, e3f, 66), "0x3F après 0B 42 00 : l'offset 0 est le 42 resté dans le tampon (§ 1.2)");
    i = find(from, 0x3F);
    CHECK(i >= 0 && W[i].b[3] == 2, "0x3F : classe 2");
}

/* Une trame, deux réponses : le second message écrase le premier dans le tampon et dans l'unique
 * trame en file (§ 2.8) ; un seul part. */
static void t_one_pointer(void)
{
    static const uint8_t two[] = {0x07, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00};
    size_t from;
    printf("émission : un seul pointeur de trame en file\n");
    params();
    power_up();
    from = nW;
    send2(two, sizeof two);
    run_for(20 * MS);
    CHECK(count(from, 0x07) == 0 && is_frame(find(from, 0x09), 2, E09, 12) && L.replaced == 1,
          "07 puis 09 dans une trame : seule la réponse du 09 part, celle du 07 est remplacée");
}

/* ─────────────────────────── § 2.4, § 2.5 le 0x0A, les phases, la mise en page ─────────────────────────── */

/* Le 0x05 attendu avec le masque de la carte (blocs 1 à 15, 97 octets) : tout à zéro (RAM remise à zéro,
 * S4) sauf le type et l'offset 22 (bit 6 en mouvement, bit 7 en service, § 2.5). L'octet constant du bloc 15
 * (§ 9) n'est pas publié : zéro. */
static void e05(uint8_t *m, bool moving, bool service)
{
    memset(m, 0, 97);
    m[0] = 0x05;
    m[23] = (uint8_t)((moving ? 0x40 : 0) | (service ? 0x80 : 0));
}

/* Le 0x06 attendu avec le masque de la carte (blocs 1 à 6, 40 octets) : bloc 1 = offsets 0-1, bloc 2 =
 * offsets 2-12 (position, 0, 0x10, limites logicielles) (§ 2.5). */
static void e06(uint8_t *m, uint8_t st0, uint8_t st1, uint16_t pos)
{
    memset(m, 0, 40);
    m[0] = 0x06;
    m[1] = st0;
    m[2] = st1;
    m[3] = (uint8_t)pos; m[4] = (uint8_t)(pos >> 8);
    m[7] = 0x10;
    m[8] = (uint8_t)P.soft_low; m[9] = (uint8_t)(P.soft_low >> 8);
    m[10] = (uint8_t)P.soft_high; m[11] = (uint8_t)(P.soft_high >> 8);
}

static void t_0a(void)
{
    static const uint8_t R0A[17] = {0x0A, 0xFF, 0x7F, 0, 0, 0, 0, 0, 0, 0x3F};
    static const uint8_t QALL[17] = {0x0A, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                     0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static const uint8_t RALL[17] = {0x0A, 0xFF, 0xFF, 0x03, 0, 0, 0, 0, 0, 0xFF};   /* ET le masque de § 1.2 */
    uint8_t m05[97], m06[40];
    size_t from;
    long i;
    printf("§ 2.4 le 0x0A : phase init -> flux, mise en page figée au premier 0x0A\n");
    params();
    power_up();
    vd(16667);
    run_for(200 * MS);
    CHECK(count(0, 0x05) == 0 && count(0, 0x06) == 0, "phase init : pas de flux avant le 0x0A (§ 2.5)");
    quiet();
    from = nW;
    send2(Q0A, sizeof Q0A);
    run_for(100 * MS);
    CHECK(is_frame(find(from, 0x0A), 2, R0A, 17), "réponse : 0A + requête ET le masque de capacités, classe 2 (§ 1.2)");
    e05(m05, false, true);
    e06(m06, 0x11, 0x00, 16384);
    i = find(from, 0x05);
    CHECK(is_frame(i, 1, m05, 97), "flux : 0x05 de classe 1, blocs 1 à 15 bout à bout, 97 octets (§ 2.4, § 2.5)");
    i = find(from, 0x06);
    CHECK(is_frame(i, 1, m06, 40),
          "0x06 de classe 1, 40 octets : en service non référencé (1), limite logicielle basse (bit 4, 16384 <= 16384)");
    quiet();
    from = nW;
    send2(Q0A, sizeof Q0A);
    run_for(100 * MS);
    CHECK(count(from, 0x0A) == 1 && count(from, 0x05) <= 1 && count(from, 0x06) <= 1,
          "un second 0x0A : flux -> phase init, le flux s'arrête (§ 2.4)");
    from = nW;
    run_for(100 * MS);
    CHECK(count(from, 0x05) == 0 && count(from, 0x06) == 0, "phase init : plus de flux");
    quiet();
    from = nW;
    send2(QALL, sizeof QALL);
    run_for(100 * MS);
    CHECK(is_frame(find(from, 0x0A), 2, RALL, 17), "la réponse annonce le nouveau masque, ET le masque de capacités");
    i = find(from, 0x05);
    CHECK(is_frame(i, 1, m05, 97) && is_frame(find(from, 0x06), 1, m06, 40),
          "mais la mise en page n'est pas retracée : toujours 97 et 40 octets (§ 2.4)");
    /* un premier 0x0A au masque plein : tous les blocs (109 et 89 octets, protocol.md § 7.5, § 7.6) */
    params();
    power_up();
    vd(16667);
    quiet();
    from = nW;
    send2(QALL, sizeof QALL);
    run_for(100 * MS);
    i = find(from, 0x05);
    CHECK(i >= 0 && W[i].n == 109 + 8 && W[i].b[5 + 1 + 8 + 1 + 8 + 3 + 2] == 0x80,
          "masque plein au premier 0x0A : 0x05 de 109 octets, bloc 5 au même offset");
    i = find(from, 0x06);
    CHECK(i >= 0 && W[i].n == 89 + 8, "masque plein : 0x06 de 89 octets");
}

/* § 2.5 : le flux est émis par l'objectif lui-même, cadencé par la routine de trame et ses deux interruptions,
 * sans 0x03 ni 0x04. */
static void t_flow(void)
{
    size_t from;
    long i, j;
    uint64_t t_vd;
    printf("§ 2.5 le flux : émis par l'objectif, cadencé par ses timers\n");
    for (int v = 0; v < 2; v++) {             /* q05_us, q06_us [NÉ] : deux valeurs */
        params();
        P.q05_us = v ? 2 * MS : 1 * MS;
        P.q06_us = v ? 6 * MS : 3 * MS;
        power_up();
        to_flow();
        from = nW;
        run_for(500 * MS);
        CHECK(count(from, 0x05) >= 29 && count(from, 0x05) <= 31 && count(from, 0x06) >= 29 && count(from, 0x06) <= 31,
              "sans 0x03 ni 0x04 : un 0x05 et un 0x06 par front VD (%zu, %zu en 500 ms)", count(from, 0x05), count(from, 0x06));
        until(vd_next - 1);
        t_vd = vd_next;
        from = nW;
        run_for(10 * MS);
        i = find(from, 0x05);
        j = find(from, 0x06);
        CHECK(i >= 0 && W[i].t_up == t_vd + P.q05_us, "0x05 : LENS_CS levée q05_us après le front VD");
        CHECK(j >= 0 && W[j].t_up == t_vd + P.q06_us, "0x06 : LENS_CS levée q06_us après le front VD");
        CHECK(i >= 0 && j >= 0 && W[i].b[4] == W[j].b[4], "0x05 et 0x06 d'une même trame : même séquence");
    }
    /* la séquence (§ 2.5) : 0xffff au démarrage, +1 par routine de trame en flux, reprise d'une trame de classe 1 */
    params();
    power_up();
    to_flow();
    i = find(0, 0x05);
    CHECK(i >= 0 && W[i].b[4] == 0x00, "premier 0x05 : séquence 00 (0xffff, +1)");
    j = find((size_t)i + 1, 0x05);
    CHECK(j >= 0 && W[j].b[4] == 0x01, "0x05 suivant : séquence 01");
    quiet();
    send(1, 0x40, Q04, sizeof Q04);
    from = nW;
    run_for(20 * MS);
    i = find(from, 0x05);
    CHECK(i >= 0 && W[i].b[4] == 0x41, "après une trame de classe 1 de séquence 40 : 41");
    /* l'horloge interne [NÉ] : sans VD, rien ; avec une période, un flux à cette période */
    for (int v = 0; v < 2; v++) {             /* frame_period_us [NÉ] : 0 et 20 ms */
        params();
        P.frame_period_us = v ? 20 * MS : 0;
        power_up();
        send2(Q0A, sizeof Q0A);
        run_for(10 * MS);
        from = nW;
        run_for(200 * MS);
        if (!v) {
            CHECK(count(from, 0x05) == 0, "sans front VD ni horloge interne : pas de routine de trame, pas de flux");
        } else {
            i = find(from, 0x05);
            j = i >= 0 ? find((size_t)i + 1, 0x05) : -1;
            CHECK(count(from, 0x05) >= 9 && count(from, 0x05) <= 11 && j >= 0 && W[j].t_up - W[i].t_up == 20 * MS,
                  "horloge interne de 20 ms : un 0x05 toutes les 20 ms, sans VD");
        }
    }
}

/* ─────────────────────────── § 2.3 le refus 0x02 ─────────────────────────── */

static void t_refusal(void)
{
    static const uint8_t Q1D[5] = {0x1D, 0x20, 0x4E, 0x00, 0x00};
    static const uint8_t q07_1d[] = {0x07, 0x00, 0x1D, 0x20, 0x4E, 0x00, 0x00};
    static const uint8_t q10_1d[] = {0x10, 0x08, 0x1D, 0x20, 0x4E, 0x00, 0x00};
    static const uint8_t err07[16] = {0xF0, 0x10, 0x00, 0x03, 0x00, 0x02, 0x02, 0x07, 0x04, 0, 0, 0, 0, 0x22, 0x00, 0x55};
    static const uint8_t err10[16] = {0xF0, 0x10, 0x00, 0x03, 0x00, 0x02, 0x02, 0x10, 0x04, 0, 0, 0, 0, 0x2B, 0x00, 0x55};
    static const uint8_t errc1[16] = {0xF0, 0x10, 0x00, 0x03, 0x00, 0x02, 0x01, 0xFF, 0x04, 0, 0, 0, 0, 0x19, 0x01, 0x55};
    static const uint8_t q40[19] = {0x40, 'V', 0x00};
    static const uint8_t q00[2] = {0x00, 0x00};
    static const uint8_t q05[2] = {0x05, 0x00};
    static const uint8_t R10[2] = {0x10, 0x00};
    static const uint8_t Q19[2] = {0x19, 0x00};
    size_t from;
    printf("§ 2.3 le refus 0x02, un message hors de sa phase\n");
    params();
    power_up();
    from = nW; send2(Q1D, sizeof Q1D); run_for(10 * MS);
    CHECK(same_wire(find(from, 0x02), ERR4, 16), "0x1D en phase init : 02 02 FF 04, classe 3 (§ 2.3)");
    from = nW; send(1, 0x05, Q1D, sizeof Q1D); run_for(10 * MS);
    CHECK(same_wire(find(from, 0x02), errc1, 16), "trame de classe 1 : 02 01 FF 04, la classe reçue (§ 2.3)");
    from = nW; send2(q40, sizeof q40); run_for(10 * MS);
    CHECK(same_wire(find(from, 0x02), ERR4, 16), "type 0x40 : code 4, au-delà de la borne (§ 2.2)");
    from = nW; send2(q00, sizeof q00); run_for(10 * MS);
    CHECK(same_wire(find(from, 0x02), ERR4, 16), "type 0 : code 4 (§ 2.3)");
    from = nW; send2(q05, sizeof q05); run_for(10 * MS);
    CHECK(same_wire(find(from, 0x02), ERR4, 16), "type 0x05, toujours refusé : code 4 (§ 2.2)");
    from = nW; send2(q07_1d, sizeof q07_1d); run_for(10 * MS);
    CHECK(same_wire(find(from, 0x02), err07, 16) && count(from, 0x07) == 0,
          "07 puis 1D : le 0x02 nomme le premier, 07 (§ 2.3) ; la réponse du 07 est remplacée par le 0x02 (§ 2.8)");
    params();
    P.homing_ref_us = 100 * MS;
    power_up();
    from = nW; send2(q10_1d, sizeof q10_1d); run_for(400 * MS);
    CHECK(same_wire(find(from, 0x02), err10, 16) && is_frame(find(from, 0x10), 2, R10, 2),
          "10 puis 1D en phase init : 0x02 de type 10, et le homing lancé répond 10 00 (§ 2.2 : les sous-messages d'avant sont exécutés)");
    /* le 0x02 remet l'état de réponse à libre : la réponse différée est perdue (§ 2.3) */
    params();
    P.defer_us = 20 * MS;
    power_up();
    from = nW;
    send2(Q19, sizeof Q19);
    run_for(2 * MS);
    send2(Q07, sizeof Q07);
    run_for(200 * MS);
    CHECK(same_wire(find(from, 0x02), ERR4, 16) && count(from, 0x19) == 0,
          "0x19 en attente, puis un message refusé : 0x02, et 19 00 ne vient jamais (§ 2.3)");
    from = nW; send2(Q07, sizeof Q07); run_for(10 * MS);
    CHECK(is_frame(find(from, 0x07), 2, E07, 35), "puis l'état est libre : 0x07 servi");
}

/* ─────────────────────────── § 2.6, § 5.4 les états de réponse ─────────────────────────── */

static void t_substates(void)
{
    static const uint8_t Q19[2] = {0x19, 0x00};
    static const uint8_t R19[2] = {0x19, 0x00};
    static const uint8_t Q1B[6] = {0x1B, 0x00, 0x20, 0x00, 0x20, 0x00};
    uint8_t r1b[11];
    uint64_t t_f;
    size_t from;
    long i;
    printf("§ 2.6, § 5.4 les états de réponse : prête (0x19), en attente (0x1B)\n");
    for (int v = 0; v < 2; v++) {             /* defer_us [NÉ] : deux valeurs */
        params();
        P.defer_us = v ? 5 * MS : 500;
        power_up();
        from = nW;
        send2(Q19, sizeof Q19);
        t_f = now;
        run_for(30 * MS);
        i = find(from, 0x19);
        CHECK(is_frame(i, 2, R19, 2) && W[i].t_up == t_f + P.loop_us + P.defer_us,
              "0x19 : réponse prête, 19 00 à la tâche de fond, defer_us plus tard (§ 2.6, § 5.4)");
        from = nW; send2(Q07, sizeof Q07); run_for(10 * MS);
        CHECK(is_frame(find(from, 0x07), 2, E07, 35), "la réponse différée émise, l'état est libre : l'init est acceptée");
    }
    params();
    P.defer_us = 20 * MS;
    power_up();
    vd(16667);
    quiet();
    from = nW;
    send2(Q19, sizeof Q19);
    run_for(1 * MS);
    send2(Q04, sizeof Q04);
    run_for(40 * MS);
    CHECK(count(from, 0x02) == 0 && is_frame(find(from, 0x19), 2, R19, 2), "réponse prête : le 0x04, sans garde, passe ; 19 00 vient (§ 2.6)");
    quiet();
    from = nW;
    send2(Q19, sizeof Q19);
    run_for(1 * MS);
    send2(Q0A, sizeof Q0A);
    run_for(100 * MS);
    CHECK(same_wire(find(from, 0x02), ERR4, 16) && count(from, 0x0A) == 0 && count(from, 0x05) == 0,
          "réponse prête : le 0x0A est refusé, pas de passage en flux (§ 2.6)");
    for (int v = 0; v < 2; v++) {             /* iris_1b_us et r1b [NÉ] : deux valeurs chacun, toutes deux observées */
        static const uint8_t R1B[2][10] = {
            {1, 2, 3, 4, 5, 6, 7, 8, 9, 10},
            {0xA0, 0x0F, 0x55, 0x00, 0x7E, 0x81, 0x33, 0xC4, 0x02, 0xFD},
        };
        params();
        P.iris_1b_us = v ? 80 * MS : 30 * MS;
        memcpy(P.r1b, R1B[v], 10);
        power_up();
        from = nW;
        send2(Q1B, sizeof Q1B);
        t_f = now;
        run_for(v ? 110 * MS : 60 * MS);
        memset(r1b, 0, sizeof r1b);
        r1b[0] = 0x1B;
        memcpy(r1b + 1, R1B[v], 10);
        r1b[10] = v ? 0xFD & 7 : 10 & 7;      /* offset 9 : bits 0-2 seulement (§ 5.4) : 05, 02 */
        i = find(from, 0x1B);
        CHECK(is_frame(i, 2, r1b, 11) && W[i].t_up == t_f + P.loop_us + P.iris_1b_us + P.defer_us,
              "0x1B : en attente, puis prête quand la condition vient, puis 11 octets (§ 5.4)");
    }
    params();
    P.iris_1b_us = UINT64_MAX;
    power_up();
    from = nW;
    send2(Q1B, sizeof Q1B);
    run_for(1000 * MS);
    CHECK(count(from, 0x1B) == 0, "0x1B dont la condition n'arrive pas : l'attente dure");
    from = nW; send2(Q07, sizeof Q07); run_for(10 * MS);
    CHECK(same_wire(find(from, 0x02), ERR4, 16), "en attente : l'init est refusée (§ 2.6)");
    from = nW; send2(Q07, sizeof Q07); run_for(10 * MS);
    CHECK(is_frame(find(from, 0x07), 2, E07, 35), "le 0x02 a remis l'état à libre : c'est la sortie de l'attente (§ 2.3)");
}

/* ─────────────────────────── § 3 le homing ─────────────────────────── */

static long first06(size_t from) { return find(from, 0x06); }
static uint16_t pos06(long i) { return (uint16_t)(W[i].b[8] | W[i].b[9] << 8); }

static void t_homing(void)
{
    static const uint8_t Q10[2] = {0x10, 0x1F};
    static const uint8_t Q10i[2] = {0x10, 0x04};
    uint8_t r10[2] = {0x10, 0x00};
    uint64_t t_f, t_f2, t_link;
    size_t from;
    long i;
    printf("§ 3 le homing : 10 00 / 10 01 différé, relance, 0x0A accepté pendant\n");
    for (int v = 0; v < 2; v++) {             /* homing_ref_us, homing_fails, home_pos [NÉ] : deux valeurs */
        params();
        P.homing_ref_us = v ? 800 * MS : 300 * MS;
        P.homing_fails = v;
        P.home_pos = v ? 20000 : 17000;
        power_up();
        from = nW;
        send2(Q10, sizeof Q10);
        t_f = now;
        run_for(50 * MS);
        CHECK(count(from, 0x10) == 0 && count(from, 0x02) == 0, "0x10 : aucune réponse immédiate (§ 3)");
        send2(Q07, sizeof Q07);
        run_for(20 * MS);
        CHECK(is_frame(find(from, 0x07), 2, E07, 35), "pendant le homing l'état reste libre : le 0x07 est servi (§ 3)");
        run_for(P.homing_ref_us + 50 * MS);
        r10[1] = v ? 0x01 : 0x00;
        i = find(from, 0x10);
        CHECK(is_frame(i, 2, r10, 2) && W[i].t_up == t_f + P.loop_us + P.homing_ref_us + P.defer_us,
              "fin du homing : réponse 0x10 prête, puis 10 %02X (§ 3)", r10[1]);
        to_flow();
        i = first06(nW > 4 ? nW - 4 : 0);
        CHECK(i >= 0 && pos06(i) == P.home_pos && (W[i].b[6] & 7) == 2,
              "après le homing : position home_pos, module référencé, 0x06 offset 0 bits 0-2 = 2 (§ 2.5)");
    }
    /* homing de l'iris seul (bit 2) : la mise au point ne bouge pas */
    params();
    P.homing_ref_us = 100 * MS;
    P.home_pos = 20000;
    power_up();
    send2(Q10i, sizeof Q10i);
    run_for(300 * MS);
    to_flow();
    i = first06(nW > 4 ? nW - 4 : 0);
    CHECK(i >= 0 && pos06(i) == 16384 && (W[i].b[6] & 7) == 2, "0x10 04 : homing de l'iris seul, position inchangée");
    /* 0x0A pendant le homing : le flux avant la fin, 10 00 en plein flux */
    params();
    P.homing_ref_us = 400 * MS;
    power_up();
    vd(16667);
    quiet();
    from = nW;
    send2(Q10, sizeof Q10);
    run_for(50 * MS);
    quiet();
    send2(Q0A, sizeof Q0A);
    run_for(100 * MS);
    CHECK(count(from, 0x0A) == 1 && count(from, 0x05) >= 3 && count(from, 0x10) == 0,
          "0x0A pendant le homing : accepté, le flux part avant la fin (§ 3)");
    run_for(400 * MS);
    CHECK(count(from, 0x10) == 1, "puis 10 00 arrive en flux");
    /* un 0x10 pendant un homing le relance depuis sa première étape (§ 3) */
    params();
    P.homing_ref_us = 300 * MS;
    power_up();
    from = nW;
    send2(Q10, sizeof Q10);
    run_for(200 * MS);
    send2(Q10, sizeof Q10);
    t_f2 = now;
    run_for(500 * MS);
    i = find(from, 0x10);
    CHECK(count(from, 0x10) == 1 && i >= 0 && W[i].t_up == t_f2 + P.loop_us + P.homing_ref_us + P.defer_us,
          "0x10 relancé : une seule réponse, homing_ref_us après le second");
    /* l'étape 3 bloque la boucle de fond : rien n'est traité pendant */
    for (int v = 0; v < 2; v++) {             /* homing_busy_us [NÉ] : deux valeurs */
        params();
        P.homing_busy_us = v ? 40 * MS : 5 * MS;
        power_up();
        send2(Q10, sizeof Q10);
        t_f = now;
        run_for(1 * MS);
        from = nW;
        send2(Q07, sizeof Q07);
        run_for(100 * MS);
        i = find(from, 0x07);
        CHECK(is_frame(i, 2, E07, 35) && W[i].t_up == t_f + P.loop_us + P.homing_busy_us,
              "un 0x07 reçu pendant l'attente active n'est traité qu'à sa fin (§ 3)");
    }
    /* la première étape attend la mise en service (§ 3) */
    for (int v = 0; v < 2; v++) {             /* service_us [NÉ] : deux valeurs */
        params();
        P.service_us = v ? 200 * MS : 20 * MS;
        P.homing_ref_us = 100 * MS;
        fresh();
        lstd_power(&L, now, true);
        run_for(P.boot_us + 1 * MS);
        body_cs(true);
        run_for(P.hs_us + 1 * MS);
        body_cs(false);
        t_link = now;
        from = nW;
        send2(Q10, sizeof Q10);
        run_for(P.service_us + 300 * MS);
        i = find(from, 0x10);
        CHECK(i >= 0 && W[i].t_up == t_link + P.service_us + P.homing_ref_us + P.defer_us,
              "0x10 avant la mise en service : le homing attend service_us (étape 0)");
    }
}

/* ─────────────────────────── § 4 la mise au point ─────────────────────────── */

static void send1d(uint16_t value, uint8_t flags)
{
    uint8_t m[5] = {0x1D, (uint8_t)value, (uint8_t)(value >> 8), 0x00, flags};
    quiet();
    send2(m, sizeof m);
}

/* Les accusés qui suivent le 0x06 de 40 octets dans la trame W[i] (§ 4.4) : leur nombre d'octets. */
static size_t tail06(long i) { return i >= 0 && W[i].n > 48 ? W[i].n - 48u : 0; }

/* Le premier 0x06 depuis `from` porteur de l'accusé `type`, -1 sinon ; *nack = le nombre de tels 0x06. */
static long ack06(size_t from, uint8_t type, size_t *nack)
{
    long first = -1;
    size_t n = 0;
    for (size_t i = from; i < nW; i++) {
        if (W[i].n <= 5 || W[i].b[5] != 0x06) continue;
        for (size_t k = 0; k + 1 < tail06((long)i); k += 2) {
            if (W[i].b[5 + 40 + k] == type) {
                if (first < 0) first = (long)i;
                n++;
            }
        }
    }
    if (nack) *nack = n;
    return first;
}

static uint8_t ackval(long i, uint8_t type)
{
    for (size_t k = 0; k + 1 < tail06(i); k += 2)
        if (W[i].b[5 + 40 + k] == type) return W[i].b[5 + 40 + k + 1];
    return 0xEE;
}

static void focus_setup(void)
{
    power_up();
    to_flow();
}

/* Objectif resté alimenté, en flux (masque de la carte), position initiale donnée. */
static void powered_flow(int32_t pos)
{
    P.initial_position = pos;
    fresh();
    lstd_start_powered(&L, now, true, Q0A + 1);
    vd(16667);
    run_for(100 * MS);
    quiet();
}

static void t_focus(void)
{
    uint8_t m[42];
    size_t from, n;
    long i, j;
    printf("§ 4 la mise au point par 0x1D : mouvement, bornage, unités, accusés, évictions, butées\n");
    for (int v = 0; v < 2; v++) {             /* steps_per_s [NÉ] : deux valeurs */
        params();
        P.steps_per_s = v ? 5000 : 20000;
        focus_setup();
        from = nW;
        send1d(20000, 0x00);
        run_for(1500 * MS);
        i = ack06(from, 0x1D, &n);
        e06(m, 0x01, 0x00, 20000);
        m[40] = 0x1D; m[41] = 0x00;
        CHECK(n == 1 && is_frame(i, 1, m, 42),
              "1D absolu 20000 : un seul accusé, 1D 00 après le 0x06 dans la même trame, position 20000 (§ 4.4)");
        {
            size_t moving06 = 0, moving05 = 0;
            for (size_t k = from; k < nW; k++) {
                if (W[k].n > 7 && W[k].b[5] == 0x06 && W[k].b[7] == 0x02) moving06++;
                if (W[k].n > 28 && W[k].b[5] == 0x05 && W[k].b[28] == 0xC0) moving05++;
            }
            /* 3616 pas : 181 ms à 20 000 pas/s (10 à 12 trames), 723 ms à 5000 (43 à 45) */
            CHECK(v ? (moving06 >= 42 && moving06 <= 45) : (moving06 >= 10 && moving06 <= 12),
                  "en mouvement : 0x06 offset 1 bit 1 (sens croissant) sur %zu trames (§ 2.5)", moving06);
            CHECK(moving05 == moving06, "0x05 offset 22 : bits 6 (en mouvement) et 7 (en service) sur les mêmes trames (§ 2.5)");
        }
        j = i - 2 >= 0 ? find((size_t)(i - 2), 0x06) : -1;
        CHECK(j >= 0 && j < i && pos06(j) < 20000 && W[j].b[7] == 0x02, "le 0x06 d'avant l'accusé : encore en mouvement");
    }
    /* bornage de la cible dans [lim_low, lim_high], en silence (§ 4.3) */
    for (int v = 0; v < 2; v++) {             /* lim_high [I] : deux valeurs */
        params();
        P.lim_high = v ? 21000 : 22743;
        focus_setup();
        from = nW;
        send1d(30000, 0x00);
        run_for(800 * MS);
        i = ack06(from, 0x1D, &n);
        CHECK(n == 1 && ackval(i, 0x1D) == 0x00 && pos06(i) == P.lim_high,
              "cible 30000 : ramenée à la borne %u, 1D 00 (§ 4.3)", P.lim_high);
        CHECK(i >= 0 && (W[i].b[6] & 0x08) == (v ? 0 : 0x08),
              "0x06 offset 0 bit 3 : limite logicielle haute atteinte seulement si la borne l'atteint");
    }
    for (int v = 0; v < 2; v++) {             /* lim_low [I] : deux valeurs ; limite logicielle basse sous la borne */
        params();
        P.lim_low = v ? 16000 : 15500;
        P.soft_low = 15100;
        focus_setup();
        from = nW;
        send1d(10000, 0x00);
        run_for(800 * MS);
        i = ack06(from, 0x1D, &n);
        CHECK(n == 1 && ackval(i, 0x1D) == 0x00 && pos06(i) == P.lim_low && (W[i].b[6] & 0x10) == 0,
              "cible 10000 : ramenée à la borne basse %u, 1D 00, hors limite logicielle (§ 4.3)", P.lim_low);
    }
    /* unités refusées : 1D 01, sans mouvement (§ 4.2) */
    {
        static const struct { uint16_t value; uint8_t flags; uint8_t ack; const char *why; } U[] = {
            {20000, 0x01, 0x01, "absolu, unité 1 : refusée"},
            {20000, 0x02, 0x01, "absolu, unité 2 : refusée"},
            {100, 0x05, 0x01, "relatif, unité 1 : refusée"},
            {100, 0x07, 0x01, "relatif, unité 3 : refusée"},
            {0, 0x05, 0x00, "relatif nul, unité 1 : pas de décodage, pas d'erreur (§ 4.2)"},
        };
        for (size_t u = 0; u < sizeof U / sizeof U[0]; u++) {
            params();
            focus_setup();
            from = nW;
            send1d(U[u].value, U[u].flags);
            run_for(200 * MS);
            i = ack06(from, 0x1D, &n);
            CHECK(n == 1 && ackval(i, 0x1D) == U[u].ack && pos06(i) == 16384, "%s : 1D %02X, position inchangée", U[u].why, U[u].ack);
        }
    }
    /* relatif en pas, signé (§ 4.2) */
    params();
    focus_setup();
    send1d(1000, 0x04);
    run_for(300 * MS);
    from = nW;
    send1d(0xFE0C, 0x04);                     /* -500 */
    run_for(300 * MS);
    i = ack06(from, 0x1D, &n);
    CHECK(n == 1 && pos06(i) == 16884, "relatif +1000 puis -500 : 16884 (%u)", i >= 0 ? pos06(i) : 0);
    /* évictions (§ 4.4) */
    params();
    focus_setup();
    from = nW;
    send1d(20000, 0x00);
    run_for(30 * MS);
    send1d(18000, 0x00);
    run_for(500 * MS);
    i = ack06(from, 0x1D, &n);
    CHECK(n == 1 && ackval(i, 0x1D) == 0x00 && pos06(i) == 18000, "1D puis 1D : un seul accusé, celui du dernier (§ 4.4)");
    params();
    focus_setup();
    from = nW;
    send1d(20000, 0x00);
    run_for(40 * MS);
    quiet();
    send2(Q1C, sizeof Q1C);
    run_for(300 * MS);
    ack06(from, 0x1D, &n);
    i = ack06(from, 0x1C, NULL);
    CHECK(n == 0 && i >= 0 && ackval(i, 0x1C) == 0x01, "1D puis 1C : le 1D n'a pas d'accusé, le 1C a 1C 01 (§ 4.4, § 4.5)");
    CHECK(i >= 0 && pos06(i) > 16384 && pos06(i) < 20000 && lstd_position(&L) == pos06(i),
          "le 1C arrête le moteur en chemin (§ 4.1, § 4.5)");
    {
        static const uint8_t c1d[] = {0x1C, 0x1D, 0x20, 0x4E, 0x00, 0x00};
        size_t n1c;
        params();
        focus_setup();
        from = nW;
        quiet();
        send2(c1d, sizeof c1d);
        run_for(500 * MS);
        i = ack06(from, 0x1D, &n);
        ack06(from, 0x1C, &n1c);
        CHECK(n == 1 && n1c == 0 && pos06(i) == 20000, "1C puis 1D dans une trame : l'accusé du 1C est effacé par le 1D (§ 4.4)");
    }
    params();
    focus_setup();
    from = nW;
    quiet();
    send2(Q1C, sizeof Q1C);
    run_for(100 * MS);
    i = ack06(from, 0x1C, &n);
    e06(m, 0x11, 0x00, 16384);
    m[40] = 0x1C; m[41] = 0x01;
    CHECK(n == 1 && is_frame(i, 1, m, 42), "0x1C au repos : 1C 01 après le 0x06 (§ 4.4)");
    /* butées dures : en zone, un 0x1D se finit sans bouger, quel que soit le sens (§ 4.3) */
    for (int v = 0; v < 2; v++) {             /* hard_low, hard_high [I] : deux valeurs chacune */
        params();
        P.hard_low = v ? 15500 : 15027;
        P.hard_high = v ? 24000 : 24216;
        powered_flow(15300);                  /* dans la zone basse si hard_low = 15500 */
        from = nW;
        send1d(20000, 0x00);
        run_for(800 * MS);
        i = ack06(from, 0x1D, &n);
        CHECK(n == 1 && ackval(i, 0x1D) == 0x00 && pos06(i) == (v ? 15300 : 20000),
              "position 15300, butée basse %u : %s", P.hard_low, v ? "1D 00 sans bouger" : "hors zone, le travail s'exécute");
        CHECK(i >= 0 && (W[i].b[6] & 0x40) == (v ? 0x40 : 0), "0x06 offset 0 bit 6 : zone de butée dure basse (§ 2.5)");
        powered_flow(24100);                  /* dans la zone haute si hard_high = 24000 */
        from = nW;
        send1d(17000, 0x00);
        run_for(800 * MS);
        i = ack06(from, 0x1D, &n);
        CHECK(n == 1 && ackval(i, 0x1D) == 0x00 && pos06(i) == (v ? 24100 : 17000),
              "position 24100, butée haute %u, cible vers l'intérieur : %s", P.hard_high, v ? "1D 00 sans bouger" : "s'exécute");
        CHECK(i >= 0 && (W[i].b[6] & 0x20) == (v ? 0x20 : 0), "0x06 offset 0 bit 5 : zone de butée dure haute (§ 2.5)");
    }
    /* limite logicielle : déjà en limite, cible au-delà -> fini sans mouvement (§ 4.3) */
    for (int v = 0; v < 2; v++) {             /* soft_margin [NÉ] : deux valeurs */
        params();
        P.lim_high = 30000;
        P.soft_margin = v ? 100 : 0;
        powered_flow(22643);
        from = nW;
        send1d(24000, 0x00);
        run_for(800 * MS);
        i = ack06(from, 0x1D, &n);
        CHECK(n == 1 && (v ? pos06(i) == 22643 : pos06(i) > 22643),
              "position 22643, limite 22743, marge %u : %s", P.soft_margin, v ? "en limite, fini sans bouger" : "le travail avance");
    }
    /* le même côté bas : déjà en limite basse, cible en dessous -> fini sans mouvement (§ 4.3) */
    for (int v = 0; v < 2; v++) {             /* soft_margin [NÉ] : deux valeurs */
        params();
        P.lim_low = 10000;
        P.soft_margin = v ? 100 : 0;
        powered_flow(16484);
        from = nW;
        send1d(15500, 0x00);
        run_for(800 * MS);
        i = ack06(from, 0x1D, &n);
        CHECK(n == 1 && ackval(i, 0x1D) == 0x00 && (v ? pos06(i) == 16484 : pos06(i) < 16484),
              "position 16484, limite basse 16384, marge %u : %s", P.soft_margin, v ? "en limite, fini sans bouger" : "le travail descend");
    }
    /* soft_low / soft_high [NÉ], deux valeurs : publiés au 0x06, offsets 7-10 (§ 2.5) */
    for (int v = 0; v < 2; v++) {
        params();
        P.soft_low = v ? 16000 : 16384;
        P.soft_high = v ? 23000 : 22743;
        powered_flow(18000);
        i = find(nW > 3 ? nW - 3 : 0, 0x06);
        e06(m, 0x02, 0x00, 18000);
        CHECK(is_frame(i, 1, m, 40), "0x06 : limites logicielles %u / %u aux offsets 7-10", P.soft_low, P.soft_high);
    }
    /* initial_position [NÉ], deux valeurs : la position publiée avant tout mouvement */
    for (int v = 0; v < 2; v++) {
        params();
        powered_flow(v ? 21000 : 18000);
        i = find(nW > 3 ? nW - 3 : 0, 0x06);
        CHECK(i >= 0 && pos06(i) == (v ? 21000 : 18000), "position initiale publiée");
    }
    /* § 2.4, § 4.4 : un 0x0A pendant le mouvement n'annule pas le travail ; l'accusé attend le flux */
    params();
    focus_setup();
    from = nW;
    send1d(20000, 0x00);
    run_for(40 * MS);
    quiet();
    send2(Q0A, sizeof Q0A);
    run_for(600 * MS);
    ack06(from, 0x1D, &n);
    CHECK(n == 0, "0x0A pendant le 1D : phase init, plus de 0x06, l'accusé ne part pas");
    quiet();
    send2(Q0A, sizeof Q0A);
    run_for(100 * MS);
    i = ack06(from, 0x1D, &n);
    CHECK(n == 1 && pos06(i) == 20000, "retour en flux : le premier 0x06 porte 1D 00 (§ 4.4 [I])");
    /* ce que le modèle ne suit pas : compté ou hors modèle */
    params();
    focus_setup();
    from = nW;
    send1d(0x0700, 0x03);                     /* absolu, unité 3 (distance Sony, § 4.2) */
    run_for(300 * MS);
    ack06(from, 0x1D, &n);
    CHECK(lstd_unmodelled(&L, 0x1D) == 1 && n == 0 && lstd_position(&L) == 16384, "1D en unité 3 : non modélisé (S6)");
    params();
    P.homing_ref_us = 500 * MS;
    power_up();
    vd(16667);
    send2((const uint8_t[]){0x10, 0x1F}, 2);
    run_for(20 * MS);
    quiet();
    send2(Q0A, sizeof Q0A);
    run_for(50 * MS);
    send1d(20000, 0x00);
    run_for(50 * MS);
    CHECK(lstd_out_of_model(&L), "1D pendant un homing : hors modèle (§ 3, [NÉ])");
}

/* ─────────────────────────── § 8 l'objectif resté alimenté ─────────────────────────── */

static void t_stayed_powered(void)
{
    static const uint8_t R0A[17] = {0x0A, 0xFF, 0x7F, 0, 0, 0, 0, 0, 0, 0x3F};
    static const uint8_t Q0Ab[17] = {0x0A, 0xFF, 0xFF, 0x03, 0, 0, 0, 0, 0, 0xFF};
    static const uint8_t errc1[16] = {0xF0, 0x10, 0x00, 0x03, 0x00, 0x02, 0x01, 0xFF, 0x04, 0, 0, 0, 0, 0x19, 0x01, 0x55};
    uint8_t m05[97], m06[40];
    size_t from;
    printf("§ 8 l'objectif resté alimenté, en flux\n");
    params();
    powered_flow(16384);
    from = nW;
    run_for(100 * MS);
    CHECK(count(from, 0x05) >= 5 && count(from, 0x06) >= 5, "resté en flux : le flux continue");
    quiet();
    from = nW;
    send2(Q01, sizeof Q01);
    run_for(50 * MS);
    CHECK(same_wire(find(from, 0x02), ERR4, 16) && count(from, 0x01) == 0 && count(from, 0x05) >= 2,
          "0x01 : 02 02 FF 04, et le flux continue (§ 8, § 2.3)");
    quiet();
    from = nW;
    send2(Q0A, sizeof Q0A);
    run_for(100 * MS);
    CHECK(is_frame(find(from, 0x0A), 2, R0A, 17) && count(from, 0x05) <= 1, "0x0A de fin d'init : phase init, le flux s'arrête (§ 8, § 2.4)");
    from = nW;
    send(1, 0x07, Q03, sizeof Q03);
    run_for(10 * MS);
    send(1, 0x07, Q04, sizeof Q04);
    run_for(10 * MS);
    CHECK(same_wire(find(from, 0x02), errc1, 16) && count(from, 0x02) == 1,
          "puis le 0x03 reçoit un 0x02, le 0x04 passe (§ 2.2)");
    from = nW;
    send2(Q01, sizeof Q01);
    run_for(10 * MS);
    CHECK(is_frame(find(from, 0x01), 2, E01, 33), "en phase init : l'init est acceptée (§ 8, retour au nominal)");
    quiet();
    from = nW;
    send2(Q0Ab, sizeof Q0Ab);
    run_for(100 * MS);
    e05(m05, false, true);
    e06(m06, 0x12, 0x00, 16384);
    CHECK(count(from, 0x0A) == 1 && is_frame(find(from, 0x05), 1, m05, 97) && is_frame(find(from, 0x06), 1, m06, 40),
          "second 0x0A : flux, mise en page gardée du premier 0x0A (97 et 40 octets), malgré le nouveau masque");
    /* resté alimenté en phase init : l'init passe dès le premier message */
    params();
    fresh();
    lstd_start_powered(&L, now, false, Q0A + 1);
    run_for(10 * MS);
    from = nW;
    send2(Q07, sizeof Q07);
    run_for(10 * MS);
    CHECK(is_frame(find(from, 0x07), 2, E07, 35), "resté alimenté en phase init : 0x07 servi");
}

/* ─────────────────────────── § 2.3 la trame corrompue ─────────────────────────── */

static void t_corrupt(void)
{
    uint8_t f[64];
    size_t n, from;
    static const uint8_t Q19[2] = {0x19, 0x00};
    static const uint8_t short07[10] = {0xF0, 0x0C, 0x00, 0x02, 0x00, 0x07, 0x00, 0x15, 0x00, 0x55};
    static const uint8_t len5[5] = {0xF0, 0x05, 0x00, 0x02, 0x00};
    printf("§ 2.3 la trame corrompue\n");
    params();
    power_up();
    n = encode(2, 0, Q07, sizeof Q07, f);
    f[n - 1] = 0x54;
    from = nW; send_raw(f, n); run_for(10 * MS);
    CHECK(same_wire(find(from, 0x02), ERR1, 16) && count(from, 0x07) == 0, "fin ≠ 55 : 02 FF FF 01 (§ 2.3)");
    n = encode(2, 0, Q07, sizeof Q07, f);
    f[0] = 0xF1;
    from = nW; send_raw(f, n); run_for(10 * MS);
    CHECK(same_wire(find(from, 0x02), ERR2, 16), "début ≠ F0 : 02 FF FF 02 (§ 2.3)");
    n = encode(2, 0, Q07, sizeof Q07, f);
    f[n - 3] ^= 0x01;
    from = nW; send_raw(f, n); run_for(10 * MS);
    CHECK(same_wire(find(from, 0x02), ERR3, 16), "somme fausse : 02 FF FF 03 (§ 2.3)");
    from = nW; send2(Q07, sizeof Q07); run_for(10 * MS);
    CHECK(is_frame(find(from, 0x07), 2, E07, 35), "la trame suivante est traitée normalement, sans resynchronisation");
    /* une trame plus courte que son champ de longueur : sa fin est lue dans l'anneau (§ 2.8) */
    from = nW; send_raw(short07, sizeof short07); run_for(10 * MS);
    CHECK(same_wire(find(from, 0x02), ERR1, 16), "10 octets reçus, longueur 12 : les 2 derniers sont les zéros de l'anneau, code 1");
    /* une réponse différée perdue (§ 2.3) */
    params();
    P.defer_us = 20 * MS;
    power_up();
    from = nW;
    send2(Q19, sizeof Q19);
    run_for(1 * MS);
    n = encode(2, 0, Q07, sizeof Q07, f);
    f[n - 3] ^= 0x01;
    send_raw(f, n);
    run_for(100 * MS);
    CHECK(same_wire(find(from, 0x02), ERR3, 16) && count(from, 0x19) == 0, "0x19 en attente puis une trame corrompue : 19 00 perdu");
    /* une longueur plus courte que l'enveloppe : le répartiteur lirait hors de la trame */
    params();
    power_up();
    send_raw(len5, sizeof len5);
    run_for(10 * MS);
    CHECK(lstd_out_of_model(&L), "longueur 5 : hors modèle");
}

/* ─────────────────────────── couche physique ─────────────────────────── */

static void t_phy(void)
{
    uint64_t t_f, t_l;
    size_t from;
    long i, j;
    printf("couche physique : LENS_CS, BODY_CS, délais d'émission\n");
    for (int v = 0; v < 2; v++) {             /* lens_cs_lead_us, lens_cs_tail_us [NÉ] : deux valeurs */
        params();
        P.lens_cs_lead_us = v ? 20 : 60;
        P.lens_cs_tail_us = v ? 300 : 0;
        power_up();
        from = nW;
        send2(Q07, sizeof Q07);
        run_for(10 * MS);
        i = find(from, 0x07);
        CHECK(i >= 0 && W[i].t_first - W[i].t_up == P.lens_cs_lead_us + P.byte_us,
              "LENS_CS levée, lead_us, puis le premier octet (§ 2.8)");
        CHECK(i >= 0 && W[i].t_last - W[i].t_first == 42 * P.byte_us, "43 octets, un toutes les byte_us");
        CHECK(i >= 0 && W[i].t_down - W[i].t_last == P.lens_cs_tail_us, "LENS_CS rabaissée tail_us après le dernier octet (§ 2.8)");
    }
    for (int v = 0; v < 2; v++) {             /* loop_us, tx_poll_us [NÉ] : deux valeurs */
        params();
        P.loop_us = v ? 3000 : 500;
        P.tx_poll_us = v ? 100 : 0;
        power_up();
        from = nW;
        send2(Q07, sizeof Q07);
        t_f = now;
        run_for(10 * MS);
        i = find(from, 0x07);
        CHECK(i >= 0 && W[i].t_up == t_f + P.loop_us + P.tx_poll_us, "réponse : loop_us après BODY_CS basse, plus tx_poll_us");
    }
    params();
    P.loop_us = 10;
    power_up();
    from = nW;
    send2(Q07, sizeof Q07);
    t_f = now;
    run_for(10 * MS);
    i = find(from, 0x07);
    CHECK(i >= 0 && W[i].t_up == t_f + 51, "BODY_CS basse depuis plus de 50 µs avant d'émettre (§ 2.8)");
    /* BODY_CS tenue haute : la réponse attend */
    params();
    power_up();
    from = nW;
    send2(Q07, sizeof Q07);
    body_cs(true);
    run_for(5 * MS);
    CHECK(count(from, 0x07) == 0 && nW == from, "BODY_CS haute : LENS_CS ne se lève pas");
    body_cs(false);
    t_l = now;
    run_for(5 * MS);
    i = find(from, 0x07);
    CHECK(i >= 0 && W[i].t_up == t_l + 51, "BODY_CS rabaissée : la réponse part 51 µs après");
    /* LENS_CS basse depuis plus de 500 µs entre deux trames (§ 2.8) */
    params();
    P.q06_us = P.q05_us + 100;
    power_up();
    to_flow();
    from = nW;
    run_for(40 * MS);
    i = find(from, 0x05);
    j = i >= 0 ? find((size_t)i, 0x06) : -1;
    CHECK(i >= 0 && j == i + 1 && W[j].t_up == W[i].t_down + 501,
          "0x06 mis en file pendant l'émission du 0x05 : il part 501 µs après la retombée de LENS_CS");
    /* une seule trame en file : BODY_CS haute au travers des deux mises en file, le 0x05 est remplacé par le 0x06 */
    params();
    power_up();
    to_flow();
    until(vd_next);
    body_cs(true);
    from = nW;
    {
        uint32_t before = L.replaced;
        run_for(P.q06_us + 1 * MS);
        body_cs(false);
        run_for(5 * MS);
        CHECK(count(from, 0x05) == 0 && count(from, 0x06) == 1 && L.replaced == before + 1,
              "BODY_CS haute pendant les deux mises en file : le 0x05 est remplacé, seul le 0x06 part (§ 2.8)");
    }
    /* octets reçus BODY_CS basse : rangés quand même, la trame est close au front descendant suivant */
    params();
    power_up();
    {
        uint8_t f[16];
        size_t n = encode(2, 0, Q07, sizeof Q07, f);
        for (size_t k = 0; k < n; k++) {
            run_for(14);
            lstd_byte(&L, now, f[k]);
        }
        run_for(1 * MS);
        from = nW;
        CHECK(count(from, 0x07) == 0, "BODY_CS basse : rien n'est dispatché sans front descendant");
        run_for(20);
        body_cs(true);
        run_for(20);
        body_cs(false);
        run_for(10 * MS);
        CHECK(is_frame(find(from, 0x07), 2, E07, 35), "les octets sont rangés ; le front descendant clôt la trame (§ 2.8)");
    }
}

/* ─────────────────────────── pannes ─────────────────────────── */

static void t_faults(void)
{
    uint64_t t_f;
    size_t from;
    long i;
    printf("pannes émises : muet, retard, trame tronquée, position figée\n");
    params();
    power_up();
    L.f.silent = true;
    from = nW;
    send2(Q07, sizeof Q07);
    run_for(20 * MS);
    CHECK(nW == from && L.silenced == 1, "muet : ni LENS_CS ni octet");
    params();
    power_up();
    L.f.delay_us = 10 * MS;
    from = nW;
    send2(Q07, sizeof Q07);
    t_f = now;
    run_for(30 * MS);
    i = find(from, 0x07);
    CHECK(is_frame(i, 2, E07, 35) && W[i].t_up == t_f + P.loop_us + 10 * MS, "retard : la réponse part 10 ms plus tard");
    to_flow();
    until(vd_next);
    t_f = now;
    from = nW;
    run_for(10 * MS);
    i = find(from, 0x05);
    CHECK(i >= 0 && W[i].t_up == t_f + P.q05_us, "retard : le flux (classe 1) n'est pas retardé");
    params();
    power_up();
    L.f.truncate_after = 10;
    from = nW;
    send2(Q07, sizeof Q07);
    run_for(20 * MS);
    {
        static const uint8_t cut[10] = {0xF0, 0x2B, 0x00, 0x02, 0x00, 0x07, 0x01, 0x03, 0x70, 0x00};
        CHECK(same_wire((long)from, cut, 10), "trame tronquée : les 10 premiers octets de la réponse, puis LENS_CS basse");
    }
    to_flow();
    i = find(nW > 4 ? nW - 4 : 0, 0x05);
    CHECK(i >= 0 && W[i].n == 105, "trame tronquée : le flux (classe 1) reste entier");
    params();
    focus_setup();
    L.f.frozen_position = true;
    from = nW;
    send1d(20000, 0x00);
    run_for(500 * MS);
    {
        size_t n;
        ack06(from, 0x1D, &n);
        i = find(nW > 4 ? nW - 4 : 0, 0x05);
        CHECK(n == 0 && lstd_position(&L) == 16384 && i >= 0 && W[i].b[28] == 0xC0,
              "position figée : le moteur est commandé, publié en mouvement, n'arrive jamais : pas d'accusé");
    }
}

/* ─────────────────────────── ce que le modèle ne suit pas ─────────────────────────── */

static void t_unmodelled(void)
{
    static const uint8_t Q26[2] = {0x26, 0x00};
    static const uint8_t Q0C[2] = {0x0C, 0x00};
    static const uint8_t Q14[16] = {0x14};
    static const uint8_t Q10b[2] = {0x10, 0x01};
    static const uint8_t half07[1] = {0x07};
    size_t from;
    printf("non modélisé, bloqué, hors modèle\n");
    params();
    power_up();
    from = nW;
    send2(Q26, sizeof Q26);
    run_for(20 * MS);
    CHECK(lstd_unmodelled(&L, 0x26) == 1 && nW == from, "0x26 accepté en phase init : non modélisé, sans réponse ni 0x02");
    send2(Q10b, sizeof Q10b);
    run_for(20 * MS);
    CHECK(lstd_unmodelled(&L, 0x10) == 1, "0x10 bit 0 : la méthode du module 3 n'est pas modélisée");
    params();
    power_up();
    send2(Q0C, sizeof Q0C);
    run_for(20 * MS);
    CHECK(lstd_out_of_model(&L), "0x0C : changement de débit, hors modèle");
    params();
    power_up();
    send2(half07, sizeof half07);
    run_for(20 * MS);
    CHECK(lstd_out_of_model(&L), "0x07 d'un seul octet : le pavage déborde, hors modèle");
    params();
    power_up();
    send2(Q14, sizeof Q14);
    run_for(20 * MS);
    from = nW;
    send2(Q07, sizeof Q07);
    run_for(20 * MS);
    CHECK(lstd_blocked(&L) && nW == from, "0x14 : l'objectif quitte l'application et ne répond plus (§ 6.1)");
    lstd_power(&L, now, false);
    run_for(10 * MS);
    lstd_power(&L, now, true);
    run_for(P.boot_us + 1 * MS);
    body_cs(true);
    run_for(P.hs_us + 1 * MS);
    body_cs(false);
    run_for(10 * MS);
    from = nW;
    send2(Q07, sizeof Q07);
    run_for(20 * MS);
    CHECK(!lstd_blocked(&L) && is_frame(find(from, 0x07), 2, E07, 35), "coupure et remise sous tension : de nouveau servi");
}

int main(void)
{
    t_power_on();
    t_init_replies();
    t_one_pointer();
    t_0a();
    t_flow();
    t_refusal();
    t_substates();
    t_homing();
    t_focus();
    t_stayed_powered();
    t_corrupt();
    t_phy();
    t_faults();
    t_unmodelled();
    printf("faux objectif standard : %d vérifications, %d échec(s)\n", checks, fails);
    return fails ? 1 : 0;
}
