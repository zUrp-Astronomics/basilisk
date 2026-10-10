/* SOURCE: composant led — le PWM de la LED de statut sur la carte
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — la carte seule (LEDC d'ESP-IDF 5.3.2) ; joué par aucun test hôte : le rendu sur la XIAO n'est pas vérifié
 *
 * Le rendu de l'intensité de bsk_led_eval sur la LED de la XIAO, par le LEDC. */

/* Pièges :
 *   - le timer et le canal LEDC sont distincts de ceux de la VD (LEDC_TIMER_0, LEDC_CHANNEL_0, phy.c), même mode basse
 *     vitesse : le timer de la VD peut être reconfiguré à chaque session (bsk_phy_vd) sans toucher la LED ;
 *   - 14 bits à 2 kHz (32,8 MHz, sous les 80 MHz de l'APB) : un pas de 0,006 % du rapport cyclique, pour un plafond
 *     faible (à 1 %, la respiration de 10 à 50 % va de 16 à 82 pas) ;
 *   - le sens de la broche (pins.h) est porté par le LEDC (output_invert), pas par le calcul : un rapport cyclique nul est
 *     la LED éteinte ;
 *   - sur le S3, les timers basse vitesse partagent une seule source ; LEDC_AUTO_CLK prend l'APB en premier, qui sert la
 *     LED (diviseur 625) comme la VD à 60 Hz (41666) : pas de conflit. Les diviseurs du LEDC sont à l'échelle de 256
 *     (8 bits de fraction) : 625 vaut 2,44, 41666 vaut 162,76. Lu dans les sources d'ESP-IDF 5.3.2, non vérifié sur la
 *     carte ;
 *   - un LEDC qui refuse sa configuration laisse la LED éteinte, sans rien d'autre : la LED ne décide rien. */
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"

#include "bsk_led.h"
#include "bsk_session.h"
#include "pins.h"   /* PIN_LED, LED_ACTIVE_LOW */

#define LED_TIMER   LEDC_TIMER_1
#define LED_CHANNEL LEDC_CHANNEL_1
#define LED_MODE    LEDC_LOW_SPEED_MODE
#define LED_RES     LEDC_TIMER_14_BIT
#define LED_HZ      2000u
#define LED_MAX     ((1u << 14) - 1u)

static bool     ok;
static uint32_t duty = UINT32_MAX;     /* le rapport cyclique posé ; aucun avant le premier */
static uint64_t next;                  /* la prochaine évaluation */

static void put(uint16_t v)
{
    uint32_t want = (uint32_t)v * LED_MAX / BSK_LED_FULL;
    if (!ok || want == duty) return;
    if (ledc_set_duty(LED_MODE, LED_CHANNEL, want) == ESP_OK && ledc_update_duty(LED_MODE, LED_CHANNEL) == ESP_OK) duty = want;
}

void bsk_led_board_init(void)
{
    uint64_t now = (uint64_t)esp_timer_get_time();
    ledc_timer_config_t t = {
        .speed_mode = LED_MODE,
        .duty_resolution = LED_RES,
        .timer_num = LED_TIMER,
        .freq_hz = LED_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_channel_config_t c = {
        .gpio_num = PIN_LED,
        .speed_mode = LED_MODE,
        .channel = LED_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LED_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags = {.output_invert = LED_ACTIVE_LOW},
    };
    ok = ledc_timer_config(&t) == ESP_OK && ledc_channel_config(&c) == ESP_OK;
    bsk_led_init(now);
    next = now;
    bsk_led_board_service(now);
}

void bsk_led_board_service(uint64_t now_us)
{
    bsk_status_t st;
    if (now_us < next) return;
    next += BSK_LED_TICK_US;
    if (next <= now_us) next = now_us + BSK_LED_TICK_US;   /* en retard : pas de rattrapage, la cadence reprend d'ici */
    bsk_session_status(&st);
    put(bsk_led_eval(&st, now_us));
}
