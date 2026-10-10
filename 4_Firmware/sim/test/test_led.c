/* SOURCE: les motifs de la LED de statut (valeurs en tête de components/led/led.c) — le calcul, sans PWM
 * AUTHOR: engineer
 * DATE: 2026-10-01
 * STATUS: actif — joué par 4_Firmware/run.sh (job `firmware` de la CI), chaque règle tenue par une mutation
 *
 * On rejoue des suites d'instantanés (bsk_status_t) à la cadence de la carte, BSK_LED_TICK_US, et on lit l'intensité rendue
 * à des instants donnés.
 *
 * Chaque attendu est calculé à la main depuis les valeurs de components/led/led.c, que voici (le
 * plafond à 100 % : un niveau de 100 % vaut 10000) :
 *   - le fixe « ON » : 10000 de 0 à 990 ms ;
 *   - la respiration de READY, depuis l'entrée : 1000 -> 5000 en 3000 ms, puis 5000 -> 1000 en 3000 ms, en boucle ; à
 *     e ms de l'entrée, 1000 + 4000 × e / 3000, puis 5000 − 4000 × (e − 3000) / 3000, divisions tronquées vers zéro ;
 *   - le signe de vie d'OFF, depuis l'entrée : 0 pendant 8000 ms, 0 -> 5000 en 1000, 5000 -> 0 en 1000, en boucle ;
 *   - IN : 10000 pendant 60, 0 pendant 140 ; OUT : 10000 pendant 350, 0 pendant 350 ; depuis le premier pas, tenus 300 ms
 *     après le dernier ;
 *   - l'init : 3000 pendant 100, 0 pendant 900 ; RESTORING : 10000 pendant 40, 0 pendant 60 ; RECOVERING : 10000, 0,
 *     10000, 0, 100 ms chacun, puis 0 jusqu'à 2000 ;
 *   - ponctuels : objectif vu 10000 pendant 400 ; init OK 0 -> 10000 en 500 ; arrivé 10000 pendant 1000 ; ouverture
 *     10000 pendant 80 ; marque enregistrée trois fois (10000 pendant 100, 0 pendant 150) ; marque effacée 10000 -> 0
 *     en 500 ;
 *   - FAULT : n éclats de 300, 300 de pause chacun, puis 2000 ; n = 1 (E_LOST), 3 (E_HOME_FAILED). */
#include <stdio.h>
#include <string.h>

#include "bsk_led.h"

#define MS   1000u
#define TICK 10u                    /* ms : BSK_LED_TICK_US, écrit à la main */
#define SPAN 30000u                 /* ms rejouées au plus par scénario */

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

static bsk_status_t S;              /* l'instantané que la session publie, modifié par le scénario */
static uint64_t now_ms;             /* le dernier instant évalué */
static uint16_t LOG[SPAN / TICK + 1];   /* l'intensité rendue à chaque instant évalué */

static void eval_at(uint64_t t) { LOG[t / TICK] = bsk_led_eval(&S, t * MS); }

/* Un scénario : la carte démarre à 0 avec l'instantané `st` ; première évaluation à 0. */
static void start(uint8_t st)
{
    memset(&S, 0, sizeof S);
    memset(LOG, 0, sizeof LOG);
    S.session_state = st;
    bsk_led_init(0);
    now_ms = 0;
    eval_at(0);
}

/* Les évaluations jusqu'à `t` ms compris, l'instantané tel qu'il est. */
static void to(uint64_t t)
{
    while (now_ms < t) {
        now_ms += TICK;
        eval_at(now_ms);
    }
}

/* L'intensité rendue à `t` (déjà évalué). */
static uint16_t at(uint64_t t) { return LOG[t / TICK]; }

#define AT(t, want) CHECK(at(t) == (want), "à %u ms : %u, attendu %u", (unsigned)(t), (unsigned)at(t), (unsigned)(want))

/* La position mesurée (un 0x06 lu). */
static void measure(int32_t pos)
{
    S.capabilities |= CAP_LIMITS_REPORTED;
    S.focus_position = pos;
}

/* Un pas de `d` toutes les 20 ms, de `from` à `to_` ms compris : la position change avant l'évaluation de l'instant. */
static void steps(uint64_t from, uint64_t to_, int32_t d)
{
    for (uint64_t t = from; t <= to_; t += 20) {
        to(t - TICK);
        S.focus_position += d;
        to(t);
    }
}

/* ─────────────────────────── le démarrage, OFF ─────────────────────────── */

static void t_on_life(void)
{
    printf("le fixe « ON » au démarrage, puis le signe de vie en OFF : un souffle de 2 s toutes les 10 s\n");
    start(SESSION_OFF);
    to(20000);
    AT(0, 10000);
    AT(990, 10000);
    AT(1000, 0);
    AT(7990, 0);
    AT(8000, 0);
    AT(8500, 2500);
    AT(9000, 5000);
    AT(9500, 2500);
    AT(10000, 0);
    AT(18500, 2500);
    AT(19000, 5000);

    printf("le signe de vie depuis l'entrée en OFF, après une session ; l'objectif retiré n'a pas de motif\n");
    start(SESSION_READY);
    to(1990);
    S.session_state = SESSION_OFF;                /* la retombée de D2 */
    to(12000);
    AT(2000, 0);
    AT(2400, 0);
    AT(10500, 2500);
    AT(11000, 5000);
}

/* ─────────────────────────── READY au repos ─────────────────────────── */

static void t_breath(void)
{
    printf("READY au repos : la respiration 10-50 %%, période 6 s ; un goto commandé sans pas lu n'y change rien\n");
    start(SESSION_READY);
    measure(20000);
    to(1500);
    S.motion_state = MOTION_COMMANDED;            /* le moteur bouge, la LED clignote ; ici il ne bouge pas */
    to(8000);
    AT(1500, 3000);
    AT(2000, 3666);
    AT(3000, 5000);
    AT(4500, 3000);
    AT(6000, 1000);
    AT(7500, 3000);
}

/* ─────────────────────────── la mise au point : IN, OUT ─────────────────────────── */

static void t_focus(void)
{
    printf("IN puis OUT, à la bague (aucun goto) : IN croissant 60/140, OUT décroissant 350/350, tenus 300 ms\n");
    start(SESSION_READY);
    measure(20000);
    steps(2000, 2400, 250);
    to(2900);
    AT(1990, 3653);
    AT(2000, 10000);
    AT(2050, 10000);
    AT(2060, 0);
    AT(2190, 0);
    AT(2200, 10000);
    AT(2260, 0);
    AT(2400, 10000);
    AT(2650, 10000);
    AT(2660, 0);
    AT(2690, 0);
    AT(2700, 4600);
    steps(3000, 3400, -250);
    to(4000);
    AT(3000, 10000);
    AT(3340, 10000);
    AT(3350, 0);
    AT(3690, 0);
    AT(3700, 4067);

    printf("un changement de sens pendant l'activité repart du début du motif\n");
    steps(5000, 5100, 250);
    steps(5120, 5200, -250);
    to(5600);
    AT(5100, 0);
    AT(5120, 10000);
    AT(5460, 10000);
    AT(5470, 0);
    AT(5490, 0);
    AT(5500, 1667);
}

static void t_focus_restoring(void)
{
    printf("RESTORING : le scintillement 40/60 quand rien ne bouge, IN quand le moteur bouge ; ni ouverture ni "
           "marque hors de READY\n");
    start(SESSION_HOMING);
    measure(14623);
    S.aperture_current = 0x1100;
    to(1100);
    AT(1000, 3000);
    AT(1090, 3000);
    AT(1100, 0);
    to(1990);
    S.session_state = SESSION_RESTORING;
    to(2190);
    S.aperture_current = 0x1200;
    to(2290);
    S.mark_sets = 1;
    steps(2500, 2700, 250);
    to(3100);
    AT(2000, 10000);
    AT(2030, 10000);
    AT(2040, 0);
    AT(2090, 0);
    AT(2100, 10000);
    AT(2250, 0);                                  /* l'ouverture changée en RESTORING : rien */
    AT(2350, 0);                                  /* le compteur de poses changé en RESTORING : rien */
    AT(2500, 10000);
    AT(2560, 0);
    AT(2600, 0);
    AT(2700, 10000);
    AT(2990, 0);
    AT(3000, 10000);
    AT(3050, 0);

    printf("RESTORING : OUT quand le moteur décroît\n");
    steps(3500, 3700, -250);
    to(4100);
    AT(3500, 10000);
    AT(3650, 10000);                              /* OUT ; IN serait à 0 (150 ≥ 60), le scintillement aussi (1650 % 100 ≥ 40) */
    AT(3840, 10000);
    AT(3850, 0);
    AT(3900, 0);                                  /* OUT ; IN et le scintillement seraient à 10000 */
    AT(4000, 10000);                              /* le dernier pas à 3700, tenu jusqu'à 3990 : le scintillement, 2000 % 100 */
    AT(4050, 0);
}

static void t_focus_init(void)
{
    printf("HOMING (le homing du 0x10 bouge le moteur) : IN puis OUT passent devant le motif de l'init\n");
    start(SESSION_HOMING);
    measure(14623);
    to(1990);
    steps(2000, 2200, 250);
    to(3000);
    AT(1900, 0);                                  /* l'init depuis l'entrée à 0 : 3000 de 0 à 90 ms de chaque seconde */
    AT(1990, 0);
    AT(2000, 10000);                              /* IN ; l'init serait à 3000 */
    AT(2050, 10000);
    AT(2060, 0);                                  /* IN ; l'init serait à 3000 */
    AT(2200, 10000);
    AT(2490, 0);
    AT(2500, 0);                                  /* le dernier pas à 2200, tenu jusqu'à 2490 : l'init, 500 */
    AT(3000, 3000);
    steps(3500, 3700, -250);
    to(4200);
    AT(3500, 10000);                              /* OUT ; l'init serait à 0 */
    AT(3840, 10000);
    AT(3850, 0);
    AT(3990, 0);
    AT(4000, 3000);                               /* l'init de nouveau, 4000 % 1000 */
    AT(4090, 3000);
    AT(4100, 0);
}

/* ─────────────────────────── première mesure, ouverture, marque ─────────────────────────── */

static void t_first_values(void)
{
    printf("le premier 0x06 et la première ouverture lue sans effet ; la mesure perdue puis revenue non plus\n");
    start(SESSION_READY);
    to(1990);
    measure(14623);
    S.aperture_current = 0x1100;
    to(2490);
    S.capabilities = 0;
    S.focus_position = 0;
    to(2590);
    measure(20000);
    to(2990);
    S.aperture_current = 0x1200;
    to(3200);
    AT(2000, 3666);
    AT(2050, 3733);
    AT(2100, 3800);
    AT(2500, 4333);
    AT(2600, 4466);
    printf("l'ouverture changée en READY : un éclat de 80 ms\n");
    AT(3000, 10000);
    AT(3070, 10000);
    AT(3080, 4894);
}

static void t_marks(void)
{
    printf("le compteur de poses : trois éclats ; d'effacements : la descente ; mark_valid et mark_position sans compteur : "
           "rien\n");
    start(SESSION_READY);
    measure(20000);
    to(3990);
    S.mark_valid = true;                          /* une relecture rendue */
    S.mark_position = 16340;
    to(4190);
    S.mark_position = 15000;                      /* un zoom : une autre clé, une autre marque */
    to(4390);
    S.mark_valid = false;                         /* un zoom : sans marque */
    to(4990);
    AT(4000, 3667);
    AT(4200, 3400);
    AT(4400, 3134);
    S.mark_sets = 1;
    S.mark_valid = true;
    S.mark_position = 20000;
    to(6990);
    AT(5000, 10000);
    AT(5090, 10000);
    AT(5100, 0);
    AT(5240, 0);
    AT(5250, 10000);
    AT(5500, 10000);
    AT(5600, 0);
    AT(5740, 0);
    AT(5750, 1334);
    S.mark_clears = 1;
    S.mark_valid = false;
    to(7600);
    AT(7000, 10000);
    AT(7250, 5000);
    AT(7490, 200);
    AT(7500, 3000);
}

/* ─────────────────────────── la fin d'un démarrage ─────────────────────────── */

static void t_boot_end(void)
{
    printf("l'init : un éclat lent et faible, une seule famille de POWERING à HOMING ; l'objectif vu : OFF -> POWERING\n");
    start(SESSION_OFF);
    to(1990);
    S.session_state = SESSION_POWERING;
    to(3040);
    S.session_state = SESSION_IDENTIFYING;
    to(4490);
    S.session_state = SESSION_HOMING;
    to(5000);
    AT(2000, 10000);
    AT(2390, 10000);
    AT(2400, 0);
    AT(3000, 3000);
    AT(3100, 0);
    AT(4000, 3000);
    AT(5000, 3000);

    printf("fin de RESTORING : « arrivé » sur ARRIVED\n");
    start(SESSION_RESTORING);
    measure(14623);
    to(3990);
    S.session_state = SESSION_READY;
    S.motion_state = MOTION_ARRIVED;
    to(5100);
    AT(4000, 10000);
    AT(4990, 10000);
    AT(5000, 2333);

    printf("fin de RESTORING par q (ABORTED) : « init OK » ; HOMING -> READY : « init OK », même sur un ARRIVED\n");
    start(SESSION_RESTORING);
    measure(14623);
    to(1990);
    S.session_state = SESSION_READY;
    S.motion_state = MOTION_ABORTED;
    to(2600);
    AT(2000, 0);
    AT(2250, 5000);
    AT(2490, 9800);
    AT(2500, 1666);
    start(SESSION_HOMING);
    S.motion_state = MOTION_ARRIVED;
    to(1990);
    S.session_state = SESSION_READY;
    to(2600);
    AT(2000, 0);
    AT(2250, 5000);
    AT(2500, 1666);
}

static void t_recovering(void)
{
    printf("RECOVERING : un double éclat toutes les 2 s ; RECOVERING -> POWERING : l'init, pas « objectif vu »\n");
    start(SESSION_READY);
    to(5990);
    S.session_state = SESSION_RECOVERING;
    to(8990);
    S.session_state = SESSION_POWERING;
    to(9200);
    AT(6000, 10000);
    AT(6090, 10000);
    AT(6100, 0);
    AT(6200, 10000);
    AT(6300, 0);
    AT(7990, 0);
    AT(8000, 10000);
    AT(9000, 3000);
    AT(9150, 0);
}

/* ─────────────────────────── FAULT ─────────────────────────── */

static void fault_at(bsk_err_t why)
{
    start(SESSION_READY);
    to(1990);
    S.session_state = SESSION_FAULT;
    S.last_error = why;
    to(9000);
}

static void t_fault(void)
{
    printf("FAULT : le code d'après la raison : 1 E_LOST, 3 E_HOME_FAILED\n");
    fault_at(E_LOST);
    AT(2000, 10000);
    AT(2290, 10000);
    AT(2300, 0);
    AT(4590, 0);
    AT(4600, 10000);
    fault_at(E_HOME_FAILED);
    AT(3200, 10000);
    AT(3800, 0);
    AT(5790, 0);
    AT(5800, 10000);

    printf("FAULT masque tout : l'activité, les compteurs, le ponctuel en cours, oublié en sortant\n");
    start(SESSION_READY);
    measure(30000);
    steps(1500, 1990, -10);
    S.session_state = SESSION_FAULT;
    S.last_error = E_LOST;
    steps(2000, 2400, -10);
    S.mark_sets = 3;
    to(2600);
    AT(2100, 10000);                              /* OUT, à 600 ms de son début : éteint */
    AT(2300, 0);
    AT(2500, 0);
    start(SESSION_READY);
    to(1490);
    S.mark_sets = 1;
    to(1590);
    S.session_state = SESSION_FAULT;
    S.last_error = E_LOST;
    to(1690);
    S.session_state = SESSION_OFF;
    to(2000);
    AT(1600, 10000);
    AT(1760, 0);                                  /* la marque, à 260 ms : allumée ; oubliée */
}

/* ─────────────────────────── les ponctuels entre eux ─────────────────────────── */

static void t_punctual(void)
{
    printf("un ponctuel interrompt l'activité, qui reprend à sa phase ; une rafale se fusionne ; le dernier gagne, sans file\n");
    start(SESSION_READY);
    measure(30000);
    S.aperture_current = 0x1100;
    for (uint64_t t = 2000; t <= 3000; t += 20) {
        to(t - TICK);
        S.focus_position -= 10;
        if (t == 2500) S.aperture_current = 0x1200;
        to(t);
    }
    to(3990);
    AT(2490, 0);
    AT(2500, 10000);
    AT(2550, 10000);
    AT(2580, 0);
    AT(2700, 10000);
    S.aperture_current = 0x1300;
    to(4020);
    S.aperture_current = 0x1400;
    to(4050);
    S.aperture_current = 0x1500;
    to(4300);
    AT(4100, 10000);
    AT(4130, 10000);
    AT(4140, 3480);
    to(5990);
    S.mark_sets = 1;
    to(6090);
    S.aperture_current = 0x1600;
    to(6400);
    AT(6150, 10000);
    AT(6180, 1240);
    AT(6260, 1346);
}

/* ─────────────────────────── le plafond ─────────────────────────── */

static void t_ceiling(void)
{
    printf("le plafond mis à l'échelle : à 5 %%, la respiration de 0,5 à 2,5 %% ; à 0, rien ; au-delà de 100, "
           "refusé\n");
    start(SESSION_READY);
    CHECK(bsk_led_ceiling() == 100, "le plafond au démarrage : 100 (%u)", bsk_led_ceiling());
    CHECK(bsk_led_ceiling_set(5) && bsk_led_ceiling() == 5, "5 : pris");
    to(6000);
    AT(500, 500);
    AT(1500, 150);
    AT(3000, 250);
    AT(6000, 50);
    CHECK(!bsk_led_ceiling_set(101) && bsk_led_ceiling() == 5, "101 : refusé, 5 gardé");
    CHECK(bsk_led_ceiling_set(0) && bsk_led_ceiling() == 0, "0 : pris");
    to(7500);
    AT(7500, 0);
    CHECK(bsk_led_ceiling_set(100) && bsk_led_ceiling() == 100, "100 : pris");
    to(9000);
    AT(9000, 5000);
    bsk_led_init(0);
    CHECK(bsk_led_ceiling() == 100, "bsk_led_init : 100 de nouveau, rien n'est rangé");
}

int main(void)
{
    t_on_life();
    t_breath();
    t_focus();
    t_focus_restoring();
    t_focus_init();
    t_first_values();
    t_marks();
    t_boot_end();
    t_recovering();
    t_fault();
    t_punctual();
    t_ceiling();
    printf("LED : %d vérifications, %d échec(s)\n", checks, fails);
    return fails ? 1 : 0;
}
