/* SOURCE: composant store — le magasin de la marque sur la carte (voir include/bsk_store.h)
 * AUTHOR: engineer
 * DATE: 2026-10-03
 * STATUS: actif — la carte seule (NVS, FreeRTOS) ; joué par aucun test hôte (les tests ont sim/store_sim.c, en mémoire) ;
 *         ticket #528 : la tâche abonnée au chien de garde, son attente des dépôts bornée
 *
 * Une tâche du magasin fait les opérations dans l'ordre de leur dépôt. Deux files : les dépôts (la SESSION n'y attend
 * jamais, pleine elle refuse) et les résultats (la tâche du magasin y attend, la SESSION les relève à chaque pas). */

/* Pièges :
 *   - l'espace `mark` et un entier de 32 bits par clé : c'est ainsi que les marques déjà rangées dans une carte se
 *     relisent (PROTOCOL.md § 2, `j`) ; n'en changer ni l'espace ni le type ;
 *   - ce que la tâche ne supprime pas : pendant l'écriture ou l'effacement d'une page, le cache de la flash est coupé, et
 *     le code en flash ne tourne pas, SESSION comprise (ESP-IDF découpe un effacement long,
 *     CONFIG_SPI_FLASH_YIELD_DURING_ERASE). Une écriture n'a lieu que sur un geste de l'utilisateur. Non mesuré sur la
 *     carte ;
 *   - priorité 1, celle de app_main : la boucle de la SESSION rend la main un tick à chaque tour, le magasin tourne
 *     alors ; cœur 0, comme toutes les tâches. */
#include "bsk_store.h"

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_task_wdt.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#define MUST(x) do { if (!(x)) abort(); } while (0)

#define NS      "mark"
#define DEPTH   8u         /* dépôts en attente : une écriture par geste de l'utilisateur, une lecture par clé */
#define STACK   3072u
#define PRIO    1u         /* ESP_TASK_MAIN_PRIO */
#define IDLE_MS 1000u      /* l'attente d'un dépôt, bornée pour nourrir le chien de garde ; bien sous ses 10 s */

typedef struct {
    bsk_store_op_t op;
    char           key[BSK_STORE_KEY_MAX];
    uint32_t       value;
} req_t;

static QueueHandle_t reqs, results;

/* Sans borne sur la valeur : c'est la SESSION qui la lit. */
static bool get(const char *key, uint32_t *v)
{
    nvs_handle_t h;
    esp_err_t e;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    e = nvs_get_u32(h, key, v);
    nvs_close(h);
    return e == ESP_OK;
}

static bool put(const char *key, uint32_t v)
{
    nvs_handle_t h;
    esp_err_t e;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    e = nvs_set_u32(h, key, v);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK;
}

/* Une clé absente est effacée (bsk_store.h). */
static bool del(const char *key)
{
    nvs_handle_t h;
    esp_err_t e;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    e = nvs_erase_key(h, key);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK || e == ESP_ERR_NVS_NOT_FOUND;
}

/* Abonnée au chien de garde (ticket #528, sdkconfig.defaults), nourri à chaque tour. Ses attentes : un dépôt, bornée à
 * IDLE_MS (sans dépôt, le tour ne fait que nourrir) ; l'opération NVS, une écriture et au pis l'effacement d'une page
 * (des dizaines à quelques centaines de millisecondes, non mesuré sur la carte) ; la place dans la file des résultats,
 * que la SESSION vide à chaque pas (mark.c) : elle n'attend donc qu'une SESSION figée, que le chien de garde redémarre
 * de toute façon. Une opération NVS figée (un verrou de la flash jamais rendu) redémarre la carte au lieu d'arrêter
 * la marque en silence. */
static void store_task(void *arg)
{
    (void)arg;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    for (;;) {
        req_t q;
        bsk_store_res_t r;
        esp_task_wdt_reset();
        if (xQueueReceive(reqs, &q, pdMS_TO_TICKS(IDLE_MS)) != pdTRUE) continue;
        memset(&r, 0, sizeof r);
        r.op = q.op;
        memcpy(r.key, q.key, sizeof r.key);
        if (q.op == BSK_STORE_GET) r.ok = get(q.key, &r.value);
        else if (q.op == BSK_STORE_PUT) r.ok = put(q.key, q.value);
        else r.ok = del(q.key);
        (void)xQueueSend(results, &r, portMAX_DELAY);
    }
}

void bsk_store_init(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    MUST((reqs = xQueueCreate(DEPTH, sizeof(req_t))) != NULL);
    MUST((results = xQueueCreate(DEPTH, sizeof(bsk_store_res_t))) != NULL);
    MUST(xTaskCreatePinnedToCore(store_task, "store", STACK, NULL, PRIO, NULL, 0) == pdPASS);
}

bool bsk_store_request(bsk_store_op_t op, const char *key, uint32_t value)
{
    req_t q = {.op = op, .value = value};
    if (strlen(key) >= sizeof q.key) return false;
    memcpy(q.key, key, strlen(key) + 1);
    return xQueueSend(reqs, &q, 0) == pdTRUE;
}

bool bsk_store_result(bsk_store_res_t *r) { return xQueueReceive(results, r, 0) == pdTRUE; }
