/* SOURCE: composant main — app_main
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — ticket #528 : la tâche de SESSION abonnée au chien de garde
 *
 * Prépare la PHY, l'USB, le magasin de la marque et la SESSION, puis avance la SESSION en boucle sur cette tâche. */
#include <stdint.h>

#include "esp_err.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsk_host.h"
#include "bsk_led.h"
#include "bsk_phy.h"
#include "bsk_session.h"
#include "bsk_store.h"

/* Ordre imposé :
 *   - bsk_phy_init d'abord : tant que D2 ne dit pas « présent », la carte n'émet rien (TXD, BODY_CS et VD au repos,
 *     rails coupés) ;
 *   - le magasin prêt avant la SESSION, la LED après elle (son fixe « ON » dit la SESSION prête) ;
 *   - dans la boucle, après chaque pas : bsk_host_observe (accusés et instantané : last_op, move_rc), puis
 *     bsk_host_usb_service (la ligne en attente, déposée sur la tâche de bsk_session_step comme bsk_session.h l'exige),
 *     puis la LED, qui lit l'instantané, rendu sur cette tâche seulement.
 * Rien ici n'écrit sur la console : elle est muette (sdkconfig.defaults). bench_core n'a rien à initialiser : son seul
 * état, le compteur de refus, part de zéro.
 *
 * La boucle rend la main un tick (1 ms) entre deux pas : le 0x03 et le 0x04 sont servis par le pas qui suit leurs
 * échéances, VD + 8,6 ms et VD + 10,1 ms ; un pas qui commence plus de 2 ms après l'échéance jette la paire (bsk_txn.h),
 * ce que DEBUG compte (pair_late, pair_late_us). La fenêtre se juge au début du pas, pas à l'émission : une trame part
 * plus tard du temps que le pas a passé avant elle, sans borne, dont l'attente de fin d'émission qui retient un pas,
 * relevée par DEBUG (tx_wait_us, tx_timeout).
 *
 * Le chien de garde (ticket #528, sdkconfig.defaults) : cette tâche s'abonne après les initialisations (celle de la NVS
 * peut effacer la partition) et le nourrit à chaque tour. Un tour attend l'émission de chaque trame : seule l'attente de
 * sa fin est bornée, à 100 ms (uart_wait_tx_done, components/phy/phy.c) ; l'écriture de ses octets vers l'UART
 * (uart_write_bytes) n'a pas d'échéance. Il attend aussi une écriture en flash du magasin, qui coupe le cache. Une
 * SESSION qui reste bloquée là, ou ailleurs, plus que les 10 s du chien de garde redémarre la carte, et DEBUG le dit
 * (reset=task_wdt). */
void app_main(void)
{
    bsk_session_params_t p;
    bsk_phy_init();
    bsk_host_usb_init();
    bsk_store_init();
    bsk_session_params_default(&p);
    bsk_session_init(&p);
    bsk_led_board_init();
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    for (;;) {
        esp_task_wdt_reset();
        bsk_session_step((uint64_t)esp_timer_get_time());
        bsk_host_observe();
        bsk_host_usb_service();
        bsk_led_board_service((uint64_t)esp_timer_get_time());
        vTaskDelay(1);
    }
}
