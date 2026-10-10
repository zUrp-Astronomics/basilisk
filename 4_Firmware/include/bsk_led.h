/* SOURCE: PROTOCOL.md § 2 (`k`) — la LED de statut
 * AUTHOR: engineer
 * DATE: 2026-10-03
 * STATUS: actif — déclarations seulement ; le calcul : components/led/led.c (C pur) ; le PWM : components/led/led_board.c
 *
 * La LED est un observateur pur : son intensité est une fonction de l'instantané courant, de l'instantané précédent, de
 * son horloge et du plafond, évaluée à cadence fixe (BSK_LED_TICK_US). Elle n'écrit rien dans la SESSION et personne ne
 * lit sa mémoire (l'instantané précédent, les débuts de motif) : ce n'est pas un état du système. */

/* Le plafond : un pourcentage du rapport cyclique plein, de 0 à 100, 100 au démarrage, réglé par `k` ; il n'est pas
 * rangé : INDI le renvoie à la connexion. Tout motif est mis à l'échelle : un niveau de 100 % est le plafond courant. Les
 * niveaux sont des rapports cycliques, sans correction gamma. */
#ifndef BSK_LED_H
#define BSK_LED_H

#include <stdbool.h>
#include <stdint.h>

#include "bsk_contract.h"

#define BSK_LED_FULL    10000u    /* le rapport cyclique plein, en dix-millièmes : la sortie de bsk_led_eval */
#define BSK_LED_TICK_US 10000u    /* la cadence d'évaluation : 10 ms */

/* Le démarrage de la carte (ou d'un test) : le plafond à 100 %, aucun instantané vu, le fixe « ON » à partir de `now_us`. */
void bsk_led_init(uint64_t now_us);

/* L'intensité à `now_us`, de 0 à BSK_LED_FULL, d'après l'instantané `st` et le précédent passé ici. À appeler toutes les
 * BSK_LED_TICK_US, instants croissants : deux changements entre deux appels n'en font qu'un. */
uint16_t bsk_led_eval(const bsk_status_t *st, uint64_t now_us);

/* Le plafond, en pour cent ; au-delà de 100, rien n'est changé et false est rendu. */
bool    bsk_led_ceiling_set(unsigned pct);
uint8_t bsk_led_ceiling(void);

/* La carte seulement (components/led/led_board.c), appelées par app_main sur la tâche de la SESSION : le canal LEDC de la
 * LED (components/phy/pins.h) et le fixe « ON » ; puis, à chaque tour de la boucle, une évaluation si BSK_LED_TICK_US est
 * passé depuis la précédente. */
void bsk_led_board_init(void);
void bsk_led_board_service(uint64_t now_us);

#endif
