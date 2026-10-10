/* SOURCE: PROTOCOL.md § 3 (lignes `*`, LOG ON / ALL / OFF) — le journal seul, sur une horloge de test
 * AUTHOR: engineer
 * DATE: 2026-09-27
 * STATUS: actif — joué par 4_Firmware/run.sh (job `firmware` de la CI), chaque vérification tenue par une mutation
 *
 * Le journal seul : éteint au démarrage, LOG ON / ALL / OFF, la forme des lignes, les périodiques tus sous LOG ON, les
 * morceaux de 64 octets, le plafond de 30 lignes/s et `* rxflood`, l'anneau de 8 Ko, LOG OFF, `dropped`, la forme
 * différence de LOG ALL et `* lost <n>`. Contre de vraies trames (un goto du faux 135) : sim/test/test_host135.c.
 *
 * Les octets attendus d'une trame sont écrits à la main, d'après la règle du codec (bsk_phy.h : F0, longueur
 * sur deux octets, classe, séquence, message, somme des octets 1 à longueur − 4, 55) : ils ne sont pas
 * recalculés par le code testé. */
#define _POSIX_C_SOURCE 200809L   /* alarm, la garde de durée */

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bsk_contract.h"
#include "bsk_journal.h"
#include "jdecode.h"

#define MS 1000u

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

static uint64_t clk;
static uint64_t clock_us(void) { return clk; }

/* Une erreur sans octet rejeté, comme PHY la rend (E_BUS d'un front perdu, flux vide) : len 0. */
static const bsk_phy_raw_t NONE;

/* Les lignes sorties de l'anneau depuis le dernier appel, une par entrée. */
static char out[400][400];
static uint8_t out_gen[400];
static size_t n_out;

static size_t drain(void)
{
    char b[sizeof out[0]];
    uint8_t g;
    size_t n;
    n_out = 0;
    while ((n = bsk_journal_take(b, sizeof b - 1, &g)) > 0) {
        b[n] = 0;
        if (n_out < sizeof out / sizeof out[0]) {
            memcpy(out[n_out], b, n + 1);
            out_gen[n_out] = g;
        }
        n_out++;
    }
    return n_out;
}

static bsk_frame_t fr(uint8_t cls, uint8_t seq, const uint8_t *msg, uint16_t len)
{
    bsk_frame_t f = {.cls = cls, .seq = seq, .len = len};
    memcpy(f.msg, msg, len);
    return f;
}

static const uint8_t M07[] = {0x07, 0x00};

/* Le journal éteint, fenêtre neuve, anneau vide. */
static void fresh(void)
{
    bsk_journal_set(false, false);
    clk += 10000 * MS;
    drain();
}

static void t_off_by_default(void)
{
    bsk_frame_t f = fr(2, 0, M07, sizeof M07);
    printf("éteint au démarrage : ni trame, ni transition, ni erreur\n");
    CHECK(!bsk_journal_on() && !bsk_journal_all(), "éteint");
    bsk_journal_rx(&f, clk);
    bsk_journal_tx(&f);
    bsk_journal_session(SESSION_READY, E_OK);
    bsk_journal_motion(MOTION_MOVING);
    bsk_journal_error(E_FRAMING, &NONE, clk);
    bsk_journal_lens(62, 0x03);
    bsk_journal_ring(true);
    bsk_journal_gesture(5, 2, 5, 0);
    bsk_journal_body08(0x02, true);
    bsk_journal_btn(BSK_BTN_SHORT, BSK_BTN_OK, NULL);
    bsk_journal_mark(BSK_MARK_CLEAR, "01030800bf087", NULL);
    bsk_journal_restore(&(bsk_mark_t){20000, BSK_APPROACH_UNKNOWN}, 200, 20200);
    bsk_journal_restore_skip("nomark", NULL);
    bsk_journal_restore_end("arrived");
    bsk_journal_xdetect(false, clk);
    bsk_journal_xdetect_bounce(clk);
    bsk_journal_refused(&f);
    CHECK(drain() == 0 && bsk_journal_dropped() == 0, "rien dans l'anneau, rien de perdu (%zu)", n_out);
}

static void t_lines(void)
{
    bsk_frame_t f = fr(2, 0, M07, sizeof M07);
    printf("LOG ON : la forme de chaque ligne\n");
    fresh();
    bsk_journal_set(true, false);
    CHECK(bsk_journal_on() && !bsk_journal_all(), "LOG ON : allumé, sans les périodiques");
    clk = 123456789;   /* 123456 ms */
    bsk_journal_rx(&f, 7654321);
    bsk_journal_tx(&f);
    bsk_journal_error(E_FRAMING, &NONE, 7655000);
    bsk_journal_error(E_BUS, &NONE, 7655000);
    bsk_journal_session(SESSION_READY, E_LOST);
    bsk_journal_session(SESSION_FAULT, E_LOST);
    bsk_journal_session(SESSION_FAULT, E_HOME_FAILED);
    bsk_journal_motion(MOTION_SETTLING);
    bsk_journal_lens(62, 0x03);
    bsk_journal_lens(66, 0x30);
    bsk_journal_ring(true);
    drain();
    CHECK(n_out == 11, "onze lignes (%zu)", n_out);
    CHECK(!strcmp(out[0], "* rx 7654 F0 0A 00 02 00 07 00 13 00 55"), "rx, l'instant de la trame : « %s »", out[0]);
    CHECK(!strcmp(out[1], "* tx 123456 F0 0A 00 02 00 07 00 13 00 55"), "tx, l'horloge : « %s »", out[1]);
    CHECK(!strcmp(out[2], "* rx 7655 err=framing"), "« %s »", out[2]);
    CHECK(!strcmp(out[3], "* rx 7655 err=bus"), "« %s »", out[3]);
    CHECK(!strcmp(out[4], "* session 123456 ready"), "« %s »", out[4]);
    CHECK(!strcmp(out[5], "* session 123456 fault lost"), "FAULT porte le jeton de last_error : « %s »", out[5]);
    CHECK(!strcmp(out[6], "* session 123456 fault home_failed"), "« %s »", out[6]);
    CHECK(!strcmp(out[7], "* motion 123456 settling"), "« %s »", out[7]);
    CHECK(!strcmp(out[8], "* lens 123456 0x05[62]=03"), "l'offset et sa valeur brute : « %s »", out[8]);
    CHECK(!strcmp(out[9], "* lens 123456 0x05[66]=30"), "« %s »", out[9]);
    CHECK(!strcmp(out[10], "* ring 123456 mode=aperture source=0x05"), "le rôle, décidé par le commutateur : « %s »", out[10]);

    bsk_journal_gesture(5, 2, 5, 0);
    bsk_journal_gesture(-3, 1, 1, 0);                                     /* un 0x05 à FD */
    bsk_journal_gesture(2, 0, 2, 0);
    bsk_journal_gesture(120, 6, 120, 54);                                 /* 54 tiers perdus par le cap */
    drain();
    CHECK(n_out == 4, "quatre lignes (%zu)", n_out);
    CHECK(!strcmp(out[0], "* ring 123456 gesture pulses=5 thirds=2 frames=5 capped=0"),
          "la fin d'un geste : « %s »", out[0]);
    CHECK(!strcmp(out[1], "* ring 123456 gesture pulses=-3 thirds=1 frames=1 capped=0"),
          "vers l'ouvert, signé ; un seul 0x05 non nul : « %s »", out[1]);
    CHECK(!strcmp(out[2], "* ring 123456 gesture pulses=2 thirds=0 frames=2 capped=0"), "aucun tiers appliqué : « %s »",
          out[2]);
    CHECK(!strcmp(out[3], "* ring 123456 gesture pulses=120 thirds=6 frames=120 capped=54"),
          "les tiers perdus par le cap : « %s »", out[3]);

    bsk_journal_body08(0x02, true);
    bsk_journal_body08(0x00, false);
    drain();
    CHECK(n_out == 2, "deux lignes (%zu)", n_out);
    CHECK(!strcmp(out[0], "* std 123456 step=08 body_flags=02 samyang=yes"), "le 0x08 d'un Samyang : « %s »",
          out[0]);
    CHECK(!strcmp(out[1], "* std 123456 step=08 body_flags=00 samyang=no"), "... d'un autre objectif : « %s »", out[1]);

    bsk_journal_btn(BSK_BTN_SHORT, BSK_BTN_OK, NULL);
    bsk_journal_btn(BSK_BTN_LONG, BSK_BTN_ER, "limit");
    bsk_journal_btn(BSK_BTN_HELD, BSK_BTN_IGNORE, NULL);
    bsk_journal_btn(BSK_BTN_LONG, BSK_BTN_IGNORE, "homing");
    bsk_journal_mark(BSK_MARK_SET, "01030800bf087", &(bsk_mark_t){16340, BSK_APPROACH_DECREASING});
    bsk_journal_mark(BSK_MARK_SET, "0107258005018", &(bsk_mark_t){17000, BSK_APPROACH_INCREASING});
    bsk_journal_mark(BSK_MARK_SET, "0107258005018", &(bsk_mark_t){0, BSK_APPROACH_UNKNOWN});
    bsk_journal_mark(BSK_MARK_CLEAR, "01030800bf087", NULL);
    bsk_journal_mark(BSK_MARK_STORE_ER, "01030800bf087", NULL);
    drain();
    CHECK(n_out == 9, "neuf lignes (%zu)", n_out);
    CHECK(!strcmp(out[0], "* btn 123456 press=short result=ok"), "le bouton : « %s »", out[0]);
    CHECK(!strcmp(out[1], "* btn 123456 press=long result=er why=limit"), "« %s »", out[1]);
    CHECK(!strcmp(out[2], "* btn 123456 press=held result=ignore"), "« %s »", out[2]);
    CHECK(!strcmp(out[3], "* btn 123456 press=long result=ignore why=homing"), "« %s »", out[3]);
    CHECK(!strcmp(out[4], "* mark 123456 set key=01030800bf087 pos=16340 dir=dec"), "la marque : « %s »", out[4]);
    CHECK(!strcmp(out[5], "* mark 123456 set key=0107258005018 pos=17000 dir=inc"), "« %s »", out[5]);
    CHECK(!strcmp(out[6], "* mark 123456 set key=0107258005018 pos=0 dir=unknown"), "« %s »", out[6]);
    CHECK(!strcmp(out[7], "* mark 123456 clear key=01030800bf087"), "« %s »", out[7]);
    CHECK(!strcmp(out[8], "* mark 123456 store=er key=01030800bf087"), "« %s »", out[8]);

    bsk_journal_restore(&(bsk_mark_t){20000, BSK_APPROACH_UNKNOWN}, 200, 20200);
    bsk_journal_restore(&(bsk_mark_t){14400, BSK_APPROACH_INCREASING}, 63, -1);
    bsk_journal_restore(&(bsk_mark_t){14400, BSK_APPROACH_DECREASING}, 1, 14401);
    bsk_journal_restore_skip("timeout", NULL);
    bsk_journal_restore_skip("limit", &(bsk_mark_t){13000, BSK_APPROACH_INCREASING});
    bsk_journal_restore_end("stall");
    drain();
    CHECK(n_out == 6, "six lignes (%zu)", n_out);
    CHECK(!strcmp(out[0], "* restore 123456 mark=20000 dir=unknown x=200 path=overshoot via=20200"),
          "l'entrée en RESTORING, avec dépassement : « %s »", out[0]);
    CHECK(!strcmp(out[1], "* restore 123456 mark=14400 dir=inc x=63 path=direct"), "... direct : « %s »", out[1]);
    CHECK(!strcmp(out[2], "* restore 123456 mark=14400 dir=dec x=1 path=overshoot via=14401"), "« %s »", out[2]);
    CHECK(!strcmp(out[3], "* restore 123456 skip=timeout"), "sans RESTORING : « %s »", out[3]);
    CHECK(!strcmp(out[4], "* restore 123456 skip=limit mark=13000"), "... la marque hors des bornes : « %s »", out[4]);
    CHECK(!strcmp(out[5], "* restore 123456 end=stall"), "la sortie : « %s »", out[5]);

    bsk_journal_xdetect(false, 7655999);
    bsk_journal_xdetect(true, 7956000);
    bsk_journal_xdetect_bounce(8001999);
    drain();
    CHECK(n_out == 3, "trois lignes (%zu)", n_out);
    CHECK(!strcmp(out[2], "* xdetect 8001 bounce"), "le rebond ignoré, à son échéance : « %s »", out[2]);
    CHECK(!strcmp(out[0], "* xdetect 7655 absent"), "la retombée, à l'instant de la coupure : « %s »", out[0]);
    CHECK(!strcmp(out[1], "* xdetect 7956 present"), "l'insertion confirmée : « %s »", out[1]);

    clk = 125000999;                                                      /* une autre fenêtre du plafond */
    bsk_journal_resend(0x07, 2, E_TIMEOUT);
    bsk_journal_resend(0x3F, 3, E_FRAMING);
    bsk_journal_resend(0x1D, 2, E_BUS);
    bsk_journal_giveup(0x1C, E_TIMEOUT);
    drain();
    CHECK(n_out == 4, "quatre lignes (%zu)", n_out);
    CHECK(!strcmp(out[0], "* resend 125000 msg=07 n=2 why=timeout"), "un renvoi, son échéance : « %s »", out[0]);
    CHECK(!strcmp(out[1], "* resend 125000 msg=3F n=3 why=framing"), "... une réponse trop courte : « %s »", out[1]);
    CHECK(!strcmp(out[2], "* resend 125000 msg=1D n=2 why=bus"), "« %s »", out[2]);
    CHECK(!strcmp(out[3], "* resend 125000 msg=1C giveup why=timeout"), "l'abandon : « %s »", out[3]);
}

/* `* xdetect` hors du plafond, et pas comptée dans sa fenêtre. */
/* `* refused <t> <hex>` : la trame refusée par bench_core, telle qu'elle serait partie (comme `* tx`), à l'horloge ; la
 * même forme sous LOG ALL (pas la forme différence : ce n'est pas une trame du fil) ; sous le plafond de 30 lignes par
 * seconde. 0C 00 en classe 1, numéro 3D : F0, la longueur 0A 00, 01, 3D, le message, la somme 0A+01+3D+0C = 54 00, 55. */
static void t_refused(void)
{
    static const uint8_t M0C[] = {0x0C, 0x00};
    bsk_frame_t f = fr(1, 0x3D, M0C, sizeof M0C);
    uint32_t d0;
    printf("* refused : la trame refusée, à l'horloge, sous LOG ON et LOG ALL, sous le plafond\n");
    fresh();
    bsk_journal_set(true, false);
    clk = 123456789;
    bsk_journal_refused(&f);
    drain();
    CHECK(n_out == 1 && !strcmp(out[0], "* refused 123456 F0 0A 00 01 3D 0C 00 54 00 55"), "LOG ON : « %s »", n_out ? out[0] : "");
    bsk_journal_set(true, true);
    bsk_journal_refused(&f);
    bsk_journal_refused(&f);
    drain();
    CHECK(n_out == 2 && !strcmp(out[0], "* refused 123456 F0 0A 00 01 3D 0C 00 54 00 55") && !strcmp(out[1], out[0]),
          "LOG ALL : la même ligne, entière, deux fois (%zu : « %s »)", n_out, n_out ? out[0] : "");
    fresh();
    bsk_journal_set(true, false);
    d0 = bsk_journal_dropped();
    for (int i = 0; i < 31; i++) bsk_journal_refused(&f);
    drain();
    CHECK(n_out == 30 && bsk_journal_dropped() == d0 + 1, "31 refus dans la seconde : 30 lignes, une perdue et comptée (%zu)",
          n_out);
    bsk_journal_set(false, false);
}

static void t_xdetect_cap(void)
{
    uint32_t d0;
    printf("* xdetect : hors du plafond de 30 lignes par seconde, pas comptée dans sa fenêtre\n");
    fresh();
    bsk_journal_set(true, false);
    d0 = bsk_journal_dropped();
    bsk_journal_xdetect(true, clk);
    for (int i = 0; i < 31; i++) bsk_journal_session(SESSION_READY, E_OK);
    bsk_journal_xdetect(false, clk);
    drain();
    CHECK(n_out == 32 && !strncmp(out[0], "* xdetect ", 10) && !strncmp(out[31], "* xdetect ", 10),
          "une présence, 30 lignes du plafond, une absence au-delà : 32 lignes (%zu)", n_out);
    CHECK(bsk_journal_dropped() == d0 + 1, "seule la 31e ligne du plafond est perdue, comptée");
    bsk_journal_xdetect_bounce(clk);                                     /* la fenêtre est pleine */
    drain();
    CHECK(n_out == 0 && bsk_journal_dropped() == d0 + 2, "* xdetect bounce, sous le plafond : perdue, comptée");
}

/* `* ring <t> gesture` hors du plafond, et pas comptée dans sa fenêtre. */
static void t_gesture_cap(void)
{
    uint32_t d0;
    printf("* ring … gesture : hors du plafond de 30 lignes par seconde, pas comptée dans sa fenêtre\n");
    fresh();
    bsk_journal_set(true, false);
    d0 = bsk_journal_dropped();
    bsk_journal_session(SESSION_READY, E_OK);                            /* la fenêtre ouverte */
    bsk_journal_gesture(4, 2, 4, 0);
    for (int i = 0; i < 30; i++) bsk_journal_session(SESSION_READY, E_OK);
    bsk_journal_gesture(-1, 0, 1, 0);
    drain();
    CHECK(n_out == 32 && strstr(out[1], " gesture pulses=4 thirds=2 frames=4") &&
              strstr(out[31], " gesture pulses=-1 thirds=0 frames=1"),
          "une ligne, un geste, 30 lignes de plus, un geste au-delà du plafond : 32 lignes (%zu)", n_out);
    CHECK(bsk_journal_dropped() == d0 + 1, "seule la 31e ligne du plafond est perdue, comptée");
}

/* `* std <t> step=08` hors du plafond, et pas comptée dans sa fenêtre. */
static void t_body08_cap(void)
{
    uint32_t d0;
    printf("* std … step=08 : hors du plafond de 30 lignes par seconde, pas comptée dans sa fenêtre\n");
    fresh();
    bsk_journal_set(true, false);
    d0 = bsk_journal_dropped();
    bsk_journal_session(SESSION_IDENTIFYING, E_OK);                       /* la fenêtre ouverte */
    bsk_journal_body08(0x02, true);
    for (int i = 0; i < 30; i++) bsk_journal_session(SESSION_IDENTIFYING, E_OK);
    bsk_journal_body08(0x00, false);
    drain();
    CHECK(n_out == 32 && strstr(out[1], " step=08 body_flags=02 samyang=yes") && strstr(out[31], " step=08 body_flags=00 samyang=no"),
          "une ligne, un 0x08, 30 lignes de plus, un 0x08 au-delà du plafond : 32 lignes (%zu)", n_out);
    CHECK(bsk_journal_dropped() == d0 + 1, "seule la 31e ligne du plafond est perdue, comptée");
}

static void t_periodic(void)
{
    static const uint8_t m05[29] = {0x05};
    static const uint8_t m06[40] = {0x06}, m06ack[42] = {0x06, [40] = 0x1D, [41] = 0x00};
    static const uint8_t m02[8] = {0x02};
    static const uint8_t m03[21] = {0x03}, m04[14] = {0x04}, m041d[19] = {0x04, [14] = 0x1D}, m1c[1] = {0x1C};
    bsk_frame_t rx05 = fr(1, 0, m05, sizeof m05), rx06 = fr(1, 0, m06, sizeof m06), rx06a = fr(1, 0, m06ack, sizeof m06ack);
    bsk_frame_t rx02 = fr(1, 0, m02, sizeof m02), tx03 = fr(1, 0, m03, sizeof m03), tx04 = fr(1, 0, m04, sizeof m04);
    bsk_frame_t tx041d = fr(1, 0, m041d, sizeof m041d), tx1c = fr(1, 0, m1c, sizeof m1c);
    printf("LOG ON tait les périodiques ; un 0x06 accusé, un 0x04 avec 0x1D et le 0x1C sortent ; LOG ALL : tout\n");
    fresh();
    bsk_journal_set(true, false);
    bsk_journal_rx(&rx05, clk);
    bsk_journal_rx(&rx06, clk);
    bsk_journal_rx(&rx02, clk);
    bsk_journal_tx(&tx03);
    bsk_journal_tx(&tx04);
    CHECK(drain() == 0 && bsk_journal_dropped() == 0, "0x05, 0x06, 0x02 reçus, 0x03, 0x04 émis : tus, sans perte (%zu)", n_out);
    bsk_journal_rx(&rx06a, clk);
    bsk_journal_tx(&tx041d);
    bsk_journal_tx(&tx1c);
    drain();
    CHECK(n_out == 3 && !strncmp(out[0], "* rx ", 5) && !strncmp(out[1], "* tx ", 5) && !strncmp(out[2], "* tx ", 5),
          "0x06 suivi d'un accusé, 0x04 + 0x1D, 0x1C : sortis (%zu)", n_out);
    bsk_journal_set(true, true);
    CHECK(bsk_journal_all(), "LOG ALL");
    bsk_journal_rx(&rx05, clk);
    bsk_journal_rx(&rx06, clk);
    bsk_journal_rx(&rx02, clk);
    bsk_journal_tx(&tx03);
    bsk_journal_tx(&tx04);
    CHECK(drain() == 5, "LOG ALL : les cinq périodiques sortent (%zu)", n_out);
}

static void t_chunks(void)
{
    uint8_t m[100];
    bsk_frame_t f;
    const char *h0, *h1;
    size_t l0, l1;
    printf("trame de plus de 64 octets : morceaux +<off>/<len>#<id>, le même id\n");
    fresh();
    bsk_journal_set(true, false);
    for (size_t i = 0; i < sizeof m; i++) m[i] = (uint8_t)(0x10 + i);
    m[0] = 0x08;
    f = fr(2, 5, m, sizeof m);   /* 108 octets : 64, puis 44 */
    bsk_journal_rx(&f, 2000 * MS);
    drain();
    CHECK(n_out == 2, "deux morceaux (%zu)", n_out);
    if (n_out != 2) return;
    CHECK(!strncmp(out[0], "* rx 2000 +0/108#", 17) && !strncmp(out[1], "* rx 2000 +64/108#", 18), "« %.24s » « %.24s »", out[0],
          out[1]);
    h0 = strchr(out[0], '#');
    h1 = strchr(out[1], '#');
    l0 = h0 ? strcspn(h0, " ") : 0;
    l1 = h1 ? strcspn(h1, " ") : 0;
    CHECK(h0 && h1 && l0 > 1 && l0 == l1 && !strncmp(h0, h1, l0), "le même #id");
    CHECK(h0 && !strncmp(h0 + l0, " F0 6C 00 02 05 08 11 12", 24), "le premier : en-tête F0 6C 00 02 05 (108 octets), puis le message");
    CHECK(h0 && strlen(out[0]) == (size_t)(h0 - out[0]) + l0 + 64 * 3, "64 octets dans le premier");
    CHECK(h1 && strlen(out[1]) == (size_t)(h1 - out[1]) + l1 + 44 * 3 && !strcmp(out[1] + strlen(out[1]) - 3, " 55"),
          "44 dans le second, qui finit par 55");
}

static void t_rate(void)
{
    bsk_frame_t f = fr(2, 0, M07, sizeof M07);
    uint8_t m[100] = {0x08};
    bsk_frame_t big = fr(2, 0, m, sizeof m);
    uint64_t base = (uint64_t)50000000 * MS;   /* 50 000 000 ms */
    uint32_t d0, d1;
    printf("30 lignes par seconde au plus : le reste compté dans dropped, puis `* rxflood <n>`\n");
    fresh();
    bsk_journal_set(true, false);
    d0 = bsk_journal_dropped();
    for (int i = 0; i < 40; i++) bsk_journal_rx(&f, base + (uint64_t)i * 10 * MS);
    CHECK(drain() == 30 && bsk_journal_dropped() - d0 == 10, "40 trames en 400 ms : 30 lignes, 10 perdues (%zu, %lu)", n_out,
          (unsigned long)(bsk_journal_dropped() - d0));
    bsk_journal_rx(&f, base + 999 * MS);
    CHECK(drain() == 0 && bsk_journal_dropped() - d0 == 11, "à 999 ms, encore dans la seconde : perdue");
    clk = base;
    bsk_journal_tx(&f);
    CHECK(drain() == 0 && bsk_journal_dropped() - d0 == 12, "une ligne émise aussi : le plafond vaut pour toutes");
    bsk_journal_rx(&f, base + 1000 * MS);
    drain();
    CHECK(n_out == 2 && !strcmp(out[0], "* rxflood 11") && !strncmp(out[1], "* rx 50001000 ", 14),
          "la seconde suivante : « * rxflood 11 » (les trames reçues tues), puis la trame (%zu : « %s »)", n_out, out[0]);
    clk = base + 1000 * MS;
    for (int i = 0; i < 28; i++) bsk_journal_session(SESSION_READY, E_OK);
    CHECK(drain() == 28, "rxflood et la trame comptent : 28 lignes de plus (%zu)", n_out);
    bsk_journal_session(SESSION_READY, E_OK);
    CHECK(drain() == 0 && bsk_journal_dropped() - d0 == 13, "la 31e : perdue");
    clk = base + 2000 * MS;
    bsk_journal_rx(&big, clk);
    CHECK(drain() == 2, "fenêtre suivante, sans trame reçue tue : pas de rxflood, les deux morceaux (%zu)", n_out);
    for (int i = 0; i < 27; i++) bsk_journal_motion(MOTION_MOVING);
    CHECK(drain() == 27, "27 lignes de plus : 29");
    d1 = bsk_journal_dropped();
    bsk_journal_rx(&big, clk);
    CHECK(drain() == 0 && bsk_journal_dropped() - d1 == 2, "à 29 lignes, une trame de deux morceaux : perdue entière, deux comptées (%lu)",
          (unsigned long)(bsk_journal_dropped() - d1));
    bsk_journal_tx(&f);
    CHECK(drain() == 1, "une ligne seule tient encore : la 30e");
    bsk_journal_error(E_FRAMING, &NONE, clk);
    CHECK(drain() == 0 && bsk_journal_dropped() - d1 == 3, "au plafond, une erreur de réception : perdue");
    clk = base + 3000 * MS;
    bsk_journal_motion(MOTION_IDLE);
    drain();
    CHECK(n_out == 2 && !strcmp(out[0], "* rxflood 2"), "rxflood compte les réceptions tues, une par trame : la trame, l'erreur (« %s »)",
          out[0]);
}

/* ─────────────────────────── les octets rejetés ─────────────────────────── */

static bsk_phy_raw_t raw(unsigned n, const uint8_t *b, unsigned len)
{
    bsk_phy_raw_t r = {.n = (uint16_t)n, .len = (uint16_t)len};
    memcpy(r.b, b, len);
    return r;
}

/* « <pre> » suivi des octets b[from..from+k) en hexa, « XX » séparés par une espace. */
static bool hex_line(const char *l, const char *pre, const uint8_t *b, size_t from, size_t k)
{
    char want[400];
    int w = snprintf(want, sizeof want, "%s", pre);
    for (size_t i = 0; i < k; i++) w += snprintf(want + w, sizeof want - (size_t)w, " %02X", b[from + i]);
    return !strcmp(l, want);
}

static void t_rx_bytes(void)
{
    static const uint8_t five[] = {0x01, 0x02, 0x03, 0x04, 0x05}, bus[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    uint8_t b[256];
    bsk_phy_raw_t r;
    char pre[80];
    const char *h;
    size_t lid;
    uint32_t d0;
    for (size_t i = 0; i < sizeof b; i++) b[i] = (uint8_t)i;
    printf("une erreur de réception porte ses octets : n, omitted, l'hexa, en morceaux au-delà de 64\n");
    fresh();
    bsk_journal_set(true, false);
    d0 = bsk_journal_dropped();

    r = raw(5, five, 5);
    bsk_journal_error(E_FRAMING, &r, 7655000);
    r = raw(5, bus, 5);
    bsk_journal_error(E_BUS, &r, 7655000);
    r = raw(0, b, 0);
    bsk_journal_error(E_BUS, &r, 7655000);
    r = raw(64, b, 64);
    bsk_journal_error(E_FRAMING, &r, 7655000);
    drain();
    CHECK(n_out == 4, "quatre lignes (%zu)", n_out);
    CHECK(!strcmp(out[0], "* rx 7655 err=framing n=5 01 02 03 04 05"),
          "LOG ON : 5 octets écartés, les 5, sans at= : « %s »", out[0]);
    CHECK(!strcmp(out[1], "* rx 7655 err=bus n=5 AA BB CC DD EE"), "rien d'omis : pas d'omitted : « %s »", out[1]);
    CHECK(!strcmp(out[2], "* rx 7655 err=bus"), "aucun octet : la ligne sans octets : « %s »", out[2]);
    CHECK(hex_line(out[3], "* rx 7655 err=framing n=64", b, 0, 64), "64 octets : une seule ligne, sans morceau : « %.60s »",
          out[3]);

    r = raw(300, b, 256);         /* une trame de 300 octets : les 256 premiers relevés */
    bsk_journal_error(E_FRAMING, &r, 7656000);
    drain();
    CHECK(n_out == 4, "256 octets : quatre morceaux (%zu)", n_out);
    h = n_out ? strchr(out[0], '#') : NULL;
    lid = h ? strcspn(h, " ") : 0;
    for (size_t k = 0; k < 4 && k < n_out && h; k++) {
        snprintf(pre, sizeof pre, "* rx 7656 err=framing n=300 omitted=44 +%zu/256%.*s", k * 64, (int)lid, h);
        CHECK(hex_line(out[k], pre, b, k * 64, 64), "morceau %zu : « %s », 64 octets, le même #id : « %.90s »", k, pre, out[k]);
    }

    r = raw(4096, b, 256);        /* 4096 octets écartés, le plus long relevé de la réception */
    bsk_journal_error(E_FRAMING, &r, 7657000);
    drain();
    CHECK(n_out == 4 && !strncmp(out[0], "* rx 7657 err=framing n=4096 omitted=3840 +0/256#", 49) &&
              !strncmp(out[3], "* rx 7657 err=framing n=4096 omitted=3840 +192/256#", 51),
          "4096 octets écartés : 4096 octets, 3840 omis, quatre morceaux (%zu : « %.60s »)", n_out, n_out ? out[0] : "");
    CHECK(bsk_journal_dropped() == d0, "rien de perdu");

    bsk_journal_set(false, false);
    r = raw(5, five, 5);
    bsk_journal_error(E_FRAMING, &r, 7658000);
    CHECK(drain() == 0 && bsk_journal_dropped() == d0, "LOG OFF : rien, rien de compté");

    printf("les octets rejetés sous le plafond de 30 lignes par seconde : admis ensemble, comptés dans dropped et rxflood\n");
    bsk_journal_set(true, false);
    clk = (uint64_t)60000000 * MS;
    for (int i = 0; i < 27; i++) bsk_journal_motion(MOTION_MOVING);
    r = raw(300, b, 256);
    bsk_journal_error(E_FRAMING, &r, clk);
    CHECK(drain() == 27 && bsk_journal_dropped() - d0 == 4, "à 27 lignes, quatre morceaux : perdus ensemble, quatre comptés (%zu, %lu)",
          n_out, (unsigned long)(bsk_journal_dropped() - d0));
    bsk_journal_motion(MOTION_MOVING);
    bsk_journal_motion(MOTION_MOVING);
    bsk_journal_motion(MOTION_MOVING);
    CHECK(drain() == 3, "les trois lignes qui restent passent");
    clk += 1000 * MS;
    bsk_journal_motion(MOTION_IDLE);
    drain();
    CHECK(n_out == 2 && !strcmp(out[0], "* rxflood 1"), "la seconde suivante : « * rxflood 1 », l'erreur tue (« %s »)", out[0]);
    for (int i = 0; i < 24; i++) bsk_journal_motion(MOTION_MOVING);
    bsk_journal_error(E_FRAMING, &r, clk);
    CHECK(drain() == 28, "à 26 lignes, les quatre morceaux tiennent : 30 (%zu)", n_out);
}

static void t_ring(void)
{
    bsk_frame_t f = fr(2, 0, M07, sizeof M07);
    uint32_t d0;
    size_t kept;
    printf("anneau de 8 Ko : plein, la ligne est perdue et comptée ; rien ne bloque\n");
    fresh();
    bsk_journal_set(true, false);
    d0 = bsk_journal_dropped();
    /* 20 s, 30 lignes par seconde, jamais vidé : 600 lignes. « * session <ms> recovering » à 8 chiffres fait
     * 29 octets, 32 avec l'en-tête : 255 entrées tiennent dans 8191 octets et en laissent 31, assez pour la
     * ligne mais pas pour son en-tête. */
    clk = (uint64_t)20000000 * MS;
    for (int s = 0; s < 20; s++) {
        clk += 1000 * MS;
        for (int i = 0; i < 30; i++) bsk_journal_session(SESSION_RECOVERING, E_OK);
    }
    kept = drain();
    CHECK(kept == 255 && strlen(out[0]) == 29, "l'anneau garde ce que tiennent 8 Ko : 8191 octets utiles, 3 d'en-tête par ligne (%zu)",
          kept);
    CHECK(bsk_journal_dropped() - d0 == 600 - kept, "le reste compté (%lu)", (unsigned long)(bsk_journal_dropped() - d0));
    clk += 1000 * MS;
    bsk_journal_tx(&f);
    CHECK(drain() == 1, "vidé, il reprend");
    bsk_journal_lost();
    CHECK(bsk_journal_dropped() - d0 == 600 - kept + 1, "une écriture ratée aussi est comptée");
}

/* L08-01 de l'audit : sous LOG ON, une trame en morceaux que l'anneau ne prend qu'en partie est dite, `* lost <n>` (ses
 * morceaux perdus), avant le morceau ou la ligne suivante qui tient ; perdue entière, rien n'est dit (comme une ligne
 * seule, t_ring) ; chaque morceau perdu compté dans dropped. L'anneau rempli à la main de lignes « * session <ms>
 * recovering » (ms à 8 chiffres : 29 octets, 32 avec l'en-tête), 30 par seconde : il en reste 8191 − 32 k octets. La trame :
 * un 0x08 de 72 octets de message, 80 octets, deux morceaux, de 64 octets (« * rx <ms> +0/80#<id> » et 192 octets, 215
 * environ avec l'en-tête) et de 16 (« * rx <ms> +64/80#<id> » et 48 octets, 72 environ), à quelques chiffres de <id> près :
 *   - k = 248, 255 octets libres : le premier tient, il en reste 40 environ, le second est perdu ; `* lost 1` devant la
 *     ligne suivante ;
 *   - k = 250, 191 octets libres : le premier est perdu, le second tient, `* lost 1` (11 octets) devant lui ;
 *   - k = 255, 31 octets libres : les deux perdus, rien n'est dit ;
 *   - sous LOG ALL, k = 248 : une perte dite une fois, pas deux (par put, et par hex_lines).
 * Le remplissage tient dans neuf fenêtres d'une seconde (lignes 0, 30… 240) ; la trame, dans la dixième, à <base> + 9000
 * ms. */
static void fill(uint64_t base_ms, int k)
{
    clk = base_ms * MS;
    for (int i = 0; i < k; i++) {
        if (i && i % 30 == 0) clk += 1000 * MS;
        bsk_journal_session(SESSION_RECOVERING, E_OK);
    }
    clk += 1000 * MS;
}

static void t_on_partial(void)
{
    uint8_t m[72] = {0x08};
    bsk_frame_t f = fr(2, 0, m, sizeof m);
    uint32_t d0;
    printf("LOG ON, l'anneau plein entre deux morceaux d'une trame : « * lost <n> », ses morceaux perdus\n");

    fresh();
    bsk_journal_set(true, false);
    d0 = bsk_journal_dropped();
    fill(30000000, 248);
    bsk_journal_rx(&f, clk);
    drain();
    CHECK(n_out == 249 && !strncmp(out[248], "* rx 30009000 +0/80#", 20) && bsk_journal_dropped() - d0 == 1,
          "255 octets libres : le premier morceau écrit, le second perdu, compté (%zu, « %.24s », %lu)", n_out,
          n_out == 249 ? out[248] : "", (unsigned long)(bsk_journal_dropped() - d0));
    bsk_journal_session(SESSION_READY, E_OK);
    drain();
    CHECK(n_out == 2 && !strcmp(out[0], "* lost 1") && !strcmp(out[1], "* session 30009000 ready"),
          "« * lost 1 » devant la ligne suivante (%zu : « %s »)", n_out, n_out ? out[0] : "");

    fresh();
    bsk_journal_set(true, false);
    d0 = bsk_journal_dropped();
    fill(31000000, 250);
    bsk_journal_rx(&f, clk);
    drain();
    CHECK(n_out == 252 && !strcmp(out[250], "* lost 1") && !strncmp(out[251], "* rx 31009000 +64/80#", 21) &&
              bsk_journal_dropped() - d0 == 1,
          "191 octets libres : le premier perdu, compté, « * lost 1 » devant le second (%zu, « %s »)", n_out,
          n_out == 252 ? out[250] : "");
    bsk_journal_session(SESSION_READY, E_OK);
    drain();
    CHECK(n_out == 1 && !strcmp(out[0], "* session 31009000 ready"), "dit une fois (« %s »)", n_out ? out[0] : "");

    fresh();
    bsk_journal_set(true, false);
    d0 = bsk_journal_dropped();
    fill(32000000, 255);
    bsk_journal_rx(&f, clk);
    drain();
    CHECK(n_out == 255 && bsk_journal_dropped() - d0 == 2, "31 octets libres : les deux perdus, comptés (%zu, %lu)", n_out,
          (unsigned long)(bsk_journal_dropped() - d0));
    bsk_journal_session(SESSION_READY, E_OK);
    drain();
    CHECK(n_out == 1 && !strcmp(out[0], "* session 32009000 ready"), "perdue entière : rien n'est dit (« %s »)",
          n_out ? out[0] : "");

    /* LOG ALL : put() dit déjà chaque morceau perdu ; une erreur de réception de 80 octets rejetés passe par les morceaux
     * (« * rx <ms> err=framing n=80 +0/80#<id> » et 192 octets, 235 environ avec l'en-tête : il en reste 20 environ). */
    fresh();
    bsk_journal_set(true, true);
    fill(33000000, 248);
    {
        uint8_t b[80] = {0xF0};
        bsk_phy_raw_t r = {.n = 80, .len = 80};
        memcpy(r.b, b, sizeof b);
        bsk_journal_error(E_FRAMING, &r, clk);
    }
    drain();
    CHECK(n_out == 249 && !strncmp(out[248], "* rx 33009000 err=framing n=80 +0/80#", 37),
          "LOG ALL, 255 octets libres : le premier morceau écrit, le second perdu (%zu)", n_out);
    bsk_journal_session(SESSION_READY, E_OK);
    drain();
    CHECK(n_out == 2 && !strcmp(out[0], "* lost 1"), "LOG ALL : « * lost 1 », dit une fois (« %s »)", n_out ? out[0] : "");
    bsk_journal_set(false, false);
}

static void t_gen(void)
{
    uint8_t g0;
    printf("LOG OFF change la génération : la ligne d'avant porte l'ancienne\n");
    fresh();
    bsk_journal_set(true, false);
    g0 = bsk_journal_gen();
    bsk_journal_session(SESSION_READY, E_OK);
    bsk_journal_set(false, false);
    CHECK(!bsk_journal_on(), "LOG OFF : éteint");
    CHECK(bsk_journal_gen() == (uint8_t)(g0 + 1), "génération suivante");
    drain();
    CHECK(n_out == 1 && out_gen[0] == g0 && out_gen[0] != bsk_journal_gen(), "la ligne d'avant LOG OFF porte l'ancienne génération");
    bsk_journal_session(SESSION_READY, E_OK);
    CHECK(drain() == 0, "LOG OFF : plus rien");
    bsk_journal_set(true, false);
    bsk_journal_session(SESSION_READY, E_OK);
    CHECK(drain() == 1 && out_gen[0] == bsk_journal_gen(), "rallumé : la génération courante");
}

/* ─────────────────────────── LOG ALL : la forme différence ─────────────────────────── */

/* Les messages de la boucle, copiés d'une capture LOG ALL du Sony FE 24-105 G, non publiée (7_Docs/E-Mount/provenance.md § 3) ;
 * les lignes entières attendues ont leur somme calculée à la main d'après celle de la capture (seule la séquence
 * change : 4E 03 avec 90, CE 02 avec 10). */
static const uint8_t S03[21] = {0x03, 0xC2, 0x2E, 0x00, 0xB2, 0x11, 0xB2, 0x11, 0x1C, 0x00, 0x00,
                                0x06, 0x00, 0x00, 0x02, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00};
static const uint8_t S04[14] = {0x04, 0x00, 0x00, 0x19, 0x83, 0x00, 0x00, 0x3D, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
static const uint8_t S06[40] = {0x06, 0x82, 0x00, 0x60, 0x41, 0xD9, 0x0F, 0x10, 0xED, 0x3E, 0x53, 0x44, 0x61, 0x00,
                                0x01, 0x00, 0x1B, 0x28, 0x28, 0x16, 0x00, 0x5F, 0x41, 0x00, 0x40, 0x87, 0x00, 0x3D,
                                0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t S0A[17] = {0x0A, 0xFF, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/* Les trames données au journal, dans l'ordre, pour les relire par le décodeur de test. */
static jd_frame_t given[64];
static size_t n_given;

static void tx_at(uint64_t ms, uint8_t cls, uint8_t seq, const uint8_t *msg, uint16_t len)
{
    bsk_frame_t f = fr(cls, seq, msg, len);
    clk = ms * MS;
    bsk_journal_tx(&f);
    if (n_given < 64) given[n_given++] = (jd_frame_t){.rx = false, .ms = ms, .known = true, .f = f};
}

static void rx_at(uint64_t ms, uint8_t cls, uint8_t seq, const uint8_t *msg, uint16_t len)
{
    bsk_frame_t f = fr(cls, seq, msg, len);
    bsk_journal_rx(&f, ms * MS + 999);
    if (n_given < 64) given[n_given++] = (jd_frame_t){.rx = true, .ms = ms, .known = true, .f = f};
}

/* Les lignes sorties relues par le décodeur de test, comparées aux trames données à partir de la `from`-ième : vrai si
 * chaque trame est reconstruite, octet pour octet, avec son sens et son instant. */
static bool decoded_as_given(size_t from)
{
    size_t k = from;
    for (size_t i = 0; i < n_out; i++) {
        jd_frame_t d;
        const char *why;
        jd_rc_t rc = jd_line(out[i], &d, &why);
        if (rc == JD_BAD) {
            printf("    ligne %zu illisible (%s) : %s\n", i, why, out[i]);
            return false;
        }
        if (rc != JD_FRAME) continue;
        if (k >= n_given || !d.known || d.rx != given[k].rx || d.ms != given[k].ms || d.f.cls != given[k].f.cls ||
            d.f.seq != given[k].f.seq || d.f.len != given[k].f.len || memcmp(d.f.msg, given[k].f.msg, d.f.len)) {
            printf("    ligne %zu (%s) : pas la trame %zu donnée\n", i, out[i], k);
            return false;
        }
        k++;
    }
    return k == n_given;
}

static void t_all_form(void)
{
    uint8_t m03[21], m06[40], m041d[19];
    printf("LOG ALL : une trame entière, puis ses différences ; identique, un octet, le pas de séquence, une longueur, un 0x0A, "
           "une perte, une classe\n");
    memcpy(m03, S03, sizeof m03);
    memcpy(m06, S06, sizeof m06);
    memcpy(m041d, S04, sizeof S04);
    memcpy(m041d + 14, (const uint8_t[]){0x1D, 0x20, 0x4E, 0x00, 0x00}, 5);
    fresh();
    jd_reset();
    n_given = 0;
    bsk_journal_set(true, true);
    tx_at(70000, 1, 0x10, S03, sizeof S03);
    tx_at(70001, 1, 0x11, S04, sizeof S04);
    rx_at(70008, 1, 0x10, S06, sizeof S06);
    tx_at(70017, 1, 0x12, S03, sizeof S03);                  /* identique, le numéro + 2 : le pas d'une référence est 0 */
    tx_at(70018, 1, 0x13, S04, sizeof S04);
    m06[3] = 0x10;
    m06[4] = 0x42;
    rx_at(70025, 1, 0x12, m06, sizeof m06);                  /* la position, offsets 2-3 */
    tx_at(70034, 1, 0x14, S03, sizeof S03);                  /* identique, le pas de 2 appris : rien que l'instant */
    m03[12] = 0x10;
    tx_at(70051, 1, 0x16, m03, sizeof m03);                  /* un octet, offset 11 */
    rx_at(70058, 1, 0x14, m06, sizeof m06);
    tx_at(70068, 1, 0x17, m041d, sizeof m041d);              /* 0x04 + 0x1D : une autre longueur */
    tx_at(70085, 1, 0x19, S04, sizeof S04);                  /* le 0x04 seul de nouveau */
    tx_at(70090, 1, 0x1A, S0A, sizeof S0A);
    tx_at(70102, 1, 0x1B, S03, sizeof S03);                  /* après le 0x0A */
    drain();
    CHECK(n_out == 13, "treize lignes (%zu)", n_out);
    CHECK(!strcmp(out[0], "* tx 70000 F0 1D 00 01 10 03 C2 2E 00 B2 11 B2 11 1C 00 00 06 00 00 02 00 02 01 00 00 00 CE 02 55"),
          "la première trame de LOG ALL : entière (« %s »)", out[0]);
    CHECK(!strcmp(out[1], "* tx 70001 F0 16 00 01 11 04 00 00 19 83 00 00 3D 00 00 00 01 00 00 06 01 55"), "« %s »", out[1]);
    CHECK(!strcmp(out[2], "* rx 70008 F0 30 00 01 10 06 82 00 60 41 D9 0F 10 ED 3E 53 44 61 00 01 00 1B 28 28 16 00 5F 41 00 "
                          "40 87 00 3D 02 00 00 00 00 00 00 00 00 00 00 00 AD 06 55"),
          "« %s »", out[2]);
    CHECK(!strcmp(out[3], "* tx 70017 ~03 s12"), "identique, le numéro donné : « %s »", out[3]);
    CHECK(!strcmp(out[4], "* tx 70018 ~04 s13"), "« %s »", out[4]);
    CHECK(!strcmp(out[5], "* rx 70025 ~06 s12 2=1042"), "deux octets qui se suivent, une seule suite : « %s »", out[5]);
    CHECK(!strcmp(out[6], "* tx 70034 ~03"), "identique, le numéro au pas : l'instant seul : « %s »", out[6]);
    CHECK(!strcmp(out[7], "* tx 70051 ~03 11=10"), "un octet : « %s »", out[7]);
    CHECK(!strcmp(out[8], "* rx 70058 ~06"), "contre la précédente de son type, pas contre la référence : « %s »", out[8]);
    CHECK(!strcmp(out[9], "* tx 70068 F0 1B 00 01 17 04 00 00 19 83 00 00 3D 00 00 00 01 00 00 1D 20 4E 00 00 9C 01 55"),
          "une autre longueur : entière (« %s »)", out[9]);
    CHECK(!strcmp(out[10], "* tx 70085 F0 16 00 01 19 04 00 00 19 83 00 00 3D 00 00 00 01 00 00 0E 01 55"),
          "la longueur d'avant : entière (« %s »)", out[10]);
    CHECK(!strcmp(out[11], "* tx 70090 F0 19 00 01 1A 0A FF 7F 00 00 00 00 00 00 3F 00 00 00 00 00 00 00 FB 01 55"),
          "« %s »", out[11]);
    CHECK(!strcmp(out[12], "* tx 70102 F0 1D 00 01 1B 03 C2 2E 00 B2 11 B2 11 1C 00 00 06 00 00 02 00 02 01 00 00 00 D9 02 55"),
          "après un 0x0A : entière (« %s »)", out[12]);
    CHECK(decoded_as_given(0), "le décodeur de test reconstruit chaque trame de l'extrait");

    n_given = 0;
    bsk_journal_lost();                                      /* une écriture ratée par la tâche qui vide l'anneau */
    tx_at(70119, 1, 0x1D, S03, sizeof S03);
    tx_at(70136, 2, 0x1F, S03, sizeof S03);                  /* une autre classe */
    tx_at(70153, 2, 0x21, S03, sizeof S03);
    drain();
    CHECK(n_out == 4 && !strcmp(out[0], "* lost 1"), "la perte dite avant la ligne suivante : « %s »", n_out ? out[0] : "");
    CHECK(n_out == 4 && !strcmp(out[1], "* tx 70119 F0 1D 00 01 1D 03 C2 2E 00 B2 11 B2 11 1C 00 00 06 00 00 02 00 02 01 00 00 00 "
                                        "DB 02 55"),
          "après une perte : entière (« %s »)", n_out > 1 ? out[1] : "");
    CHECK(n_out == 4 && !strcmp(out[2], "* tx 70136 F0 1D 00 02 1F 03 C2 2E 00 B2 11 B2 11 1C 00 00 06 00 00 02 00 02 01 00 00 00 "
                                        "DE 02 55"),
          "une autre classe : entière (« %s »)", n_out > 2 ? out[2] : "");
    CHECK(n_out == 4 && !strcmp(out[3], "* tx 70153 ~03 s21"), "« %s »", n_out > 3 ? out[3] : "");
    CHECK(decoded_as_given(0), "le décodeur de test : la perte oublie les références, la suite se reconstruit");

    n_given = 0;
    bsk_journal_set(true, true);                             /* LOG ALL de nouveau : tout repart entier */
    tx_at(70170, 2, 0x23, S03, sizeof S03);
    drain();
    CHECK(n_out == 1 && !strncmp(out[0], "* tx 70170 F0 1D 00 02 23 03 ", 29), "LOG ALL : entière (« %s »)", n_out ? out[0] : "");
    {                                                        /* une différence qui ne tient pas sur une ligne : entière */
        uint8_t m05[109] = {0x05};
        bsk_frame_t a = fr(1, 0x30, m05, sizeof m05), z;
        bsk_journal_rx(&a, 70180 * MS);
        for (size_t i = 1; i < sizeof m05; i += 2) m05[i] = 0x11;
        z = fr(1, 0x32, m05, sizeof m05);
        bsk_journal_rx(&z, 70181 * MS);
        drain();
        CHECK(n_out == 4 && !strncmp(out[2], "* rx 70181 +0/117#", 18) && !strncmp(out[3], "* rx 70181 +64/117#", 19),
              "54 octets changés un sur deux : la différence dépasserait la ligne, la trame entière en deux morceaux (%zu : "
              "« %.24s »)", n_out, n_out > 2 ? out[2] : "");
    }
    bsk_journal_set(true, false);
    tx_at(70187, 2, 0x25, S03, sizeof S03);
    rx_at(70190, 1, 0x16, m06, sizeof m06);
    CHECK(drain() == 0, "LOG ON : la boucle tue, comme avant (%zu)", n_out);
}

/* Une différence sans référence (sa ligne entière perdue) : le décodeur ne la reconstruit pas, il la dit inconnue. */
static void t_all_unknown(void)
{
    jd_frame_t d;
    const char *why;
    printf("une différence sans référence : inconnue pour le décodeur, jamais fausse\n");
    jd_reset();
    CHECK(jd_line("* tx 70000 ~03 11=10", &d, &why) == JD_FRAME && !d.known && d.f.msg[0] == 0x03 && d.ms == 70000,
          "la trame, son type et son instant, sans contenu");
    CHECK(jd_line("* lost 2", &d, &why) == JD_NONE, "* lost : pas une trame");
}

/* Sous LOG ALL, les trames ne passent pas par le plafond et ne prennent pas de place dans sa fenêtre ; les lignes
 * d'événements gardent le leur. */
static void t_all_cap(void)
{
    uint32_t d0;
    printf("LOG ALL : 300 trames dans une seconde, toutes ; les lignes d'événements gardent leurs 30\n");
    fresh();
    bsk_journal_set(true, true);
    d0 = bsk_journal_dropped();
    clk = (uint64_t)80000000 * MS;
    for (int i = 0; i < 300; i++) {
        bsk_frame_t f = fr(1, (uint8_t)i, S03, sizeof S03);
        bsk_journal_tx(&f);
        if (i == 150)
            for (int k = 0; k < 31; k++) bsk_journal_session(SESSION_READY, E_OK);
    }
    drain();
    CHECK(n_out == 330 && bsk_journal_dropped() == d0 + 1, "300 trames et 30 lignes d'événements, seule la 31e tue (%zu, %lu)",
          n_out, (unsigned long)(bsk_journal_dropped() - d0));
}

/* L'anneau plein sous LOG ALL : la perte dite là où elle a eu lieu, avant la ligne suivante qui tient ; la référence
 * réécrite entière. */
static void t_all_ring(void)
{
    uint32_t d0, lost;
    uint8_t g0;
    printf("LOG ALL, l'anneau plein : « * lost <n> » à la place des lignes perdues, puis entière\n");
    fresh();
    bsk_journal_set(true, true);
    d0 = bsk_journal_dropped();
    clk = (uint64_t)90000000 * MS;
    for (int i = 0; i < 300; i++) {                          /* 0x07 : jamais en différence ; 39 octets la ligne */
        bsk_frame_t f = fr(2, 0, M07, sizeof M07);
        bsk_journal_tx(&f);
    }
    lost = bsk_journal_dropped() - d0;
    {                                                        /* une ligne de place : la trame tient, pas avec son repère */
        char one[400];
        uint8_t g;
        bsk_frame_t f = fr(2, 0, M07, sizeof M07);
        CHECK(bsk_journal_take(one, sizeof one, &g) == 43, "une ligne de 43 octets retirée");
        bsk_journal_tx(&f);
        lost++;
    }
    drain();
    CHECK(lost > 1 && n_out == 300 - lost && bsk_journal_dropped() - d0 == lost,
          "l'anneau plein : %lu perdues, %zu sorties ; la trame qui tenait sans son repère, perdue aussi",
          (unsigned long)lost, n_out);
    {
        bsk_frame_t f = fr(1, 0x10, S03, sizeof S03);
        char want[24];
        bsk_journal_tx(&f);
        drain();
        snprintf(want, sizeof want, "* lost %lu", (unsigned long)lost);
        CHECK(n_out == 2 && !strcmp(out[0], want) && !strncmp(out[1], "* tx 90000000 F0 1D ", 20),
              "vidé : « %s », puis la trame (%zu : « %s »)", want, n_out, n_out ? out[0] : "");
    }

    printf("une écriture ratée dite sous sa génération : périmée après LOG OFF ; rien sous LOG ON\n");
    g0 = bsk_journal_gen();
    bsk_journal_lost();
    bsk_journal_set(false, false);
    drain();
    CHECK(n_out == 1 && !strcmp(out[0], "* lost 1") && out_gen[0] == g0 && g0 != bsk_journal_gen(),
          "la perte d'avant LOG OFF porte l'ancienne génération : la tâche ne l'écrit pas");
    bsk_journal_set(true, false);
    bsk_journal_lost();
    CHECK(drain() == 0, "LOG ON : la perte est comptée, pas dite (%zu)", n_out);
}

static void t_names(void)
{
    static const char *const st[] = {"off", "powering", "identifying", "homing", "restoring", "ready", "recovering", "fault"};
    static const char *const mv[] = {"idle", "planning", "commanded", "moving", "settling", "arrived", "stalled", "aborted"};
    static const struct { bsk_err_t e; const char *tok; } er[] = {
        {E_OK, "ok"},           {E_TIMEOUT, "timeout"},     {E_FRAMING, "framing"},   {E_REJECTED, "rejected"},
        {E_BUS, "bus"},         {E_STALL, "stall"},         {E_LIMIT, "limit"},       {E_HOME_FAILED, "home_failed"},
        {E_IDENTITY, "identity"}, {E_FORBIDDEN, "forbidden"}, {E_BUSY, "busy"},     {E_LOST, "lost"},
        {E_ABORTED, "aborted"}, {E_NOCAP, "nocap"},
    };
    printf("les noms : états en minuscules, jetons = nom de l'erreur sans E_ (spec § 4.5.1, § 4.5.3)\n");
    for (size_t i = 0; i < 8; i++) CHECK(!strcmp(bsk_state_name((uint8_t)i), st[i]), "état %zu : %s", i, bsk_state_name((uint8_t)i));
    for (size_t i = 0; i < 8; i++) CHECK(!strcmp(bsk_motion_name((uint8_t)i), mv[i]), "mouvement %zu : %s", i, bsk_motion_name((uint8_t)i));
    for (size_t i = 0; i < sizeof er / sizeof er[0]; i++) CHECK(!strcmp(bsk_err_token(er[i].e), er[i].tok), "%s", er[i].tok);
}

int main(void)
{
    signal(SIGALRM, too_long);
    alarm(10);
    bsk_journal_init(clock_us);
    t_off_by_default();
    t_lines();
    t_periodic();
    t_chunks();
    t_rate();
    t_xdetect_cap();
    t_gesture_cap();
    t_body08_cap();
    t_rx_bytes();
    t_ring();
    t_gen();
    t_all_form();
    t_all_unknown();
    t_all_cap();
    t_all_ring();
    t_refused();
    t_on_partial();
    t_names();
    printf("journal : %d vérifications, %d échec(s)\n", checks, fails);
    return fails ? 1 : 0;
}
