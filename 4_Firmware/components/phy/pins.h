/* SOURCE: composant phy — les broches de la carte, une table par cible ESP-IDF
 * AUTHOR: engineer
 * DATE: 2026-10-03
 * STATUS: actif — inclus par components/phy/phy.c et components/led/led_board.c
 *
 * La table est choisie par la cible du build (CONFIG_IDF_TARGET_*), pas par une option : une carte est un brochage.
 * Seule la XIAO ESP32-S3 en a une ; une cible sans table ne compile pas. Sur la XIAO ESP32-C3, GPIO9 (D9) est la
 * broche de strapping BOOT, qui doit rester haute au repos : à garder en tête quand sa table sera écrite. */
#ifndef PINS_H
#define PINS_H

#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S3
/* Seeed XIAO ESP32S3 */
#define PIN_EN_LOGIC    GPIO_NUM_1   /* D0  : rail 3,3 V de l'objectif (optionnel) */
#define PIN_EN_MOTOR    GPIO_NUM_2   /* D1  : rail moteur 5 V (optionnel) */
#define PIN_LENS_DETECT GPIO_NUM_3   /* D2  : XDETECT, tirage haut, 0 = présent */
#define PIN_BODY_VD     GPIO_NUM_4   /* D3  : VD, sortie LEDC */
#define PIN_LENS_CS     GPIO_NUM_44  /* D7  : LENS_CS, entrée */
#define PIN_LENS_TXD    GPIO_NUM_7   /* D8  : UART1 TX, carte -> objectif */
#define PIN_LENS_RXD    GPIO_NUM_8   /* D9  : UART1 RX, objectif -> carte */
#define PIN_BODY_CS     GPIO_NUM_9   /* D10 : BODY_CS, sortie */
#define LENS_UART       UART_NUM_1
#define PIN_LED         GPIO_NUM_21  /* la LED utilisateur de la XIAO */
#define LED_ACTIVE_LOW  1            /* ... allumée à l'état bas */
#else
#error "firmware2 : aucune table de broches pour cette cible ESP-IDF : ajouter la sienne dans components/phy/pins.h"
#endif

#endif
