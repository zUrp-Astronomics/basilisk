/* SOURCE: PROTOCOL.md § 1 ; spec de l'atelier § 4.5 — l'USB de la carte pour la couche HOTE
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — ESP-IDF seulement : aucun test hôte ne le compile ; ticket #528 : les créations des tâches vérifiées,
 *         les marges de pile pour DEBUG (bsk_host_stacks), le chien de garde
 *
 * L'USB série-JTAG de la carte : cadre les lignes, les passe à la couche HOTE, écrit les réponses et le journal. */
#include "bsk_host.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "bsk_journal.h"

/* Trois tâches se partagent l'USB, et une seule touche à la session :
 *   - la tâche USB (`usb_rx`) lit les octets, cadre les lignes et écrit les réponses, rien d'autre : elle dépose chaque
 *     ligne dans une boîte aux lettres (req, req_ready), attend la réponse (rep, rep_ready) et l'écrit. Une seule ligne
 *     en vol : une réponse par ligne. Sa pile, 4096 octets, ne formate aucune réponse ;
 *   - la tâche de SESSION (app_main) prend la ligne entre deux pas de SESSION (bsk_host_usb_service) : bsk_host_line y
 *     dépose la commande et lit l'accusé et l'instantané, comme bsk_session.h l'exige. Elle n'écrit jamais sur l'USB :
 *     elle ne bloque pas dessus ;
 *   - la tâche du journal (`journal`) vide l'anneau (bsk_journal_take) vers l'USB. Elle attend 20 ms quand il est
 *     vide, sans notification : le journal, C pur, ne connaît pas FreeRTOS.
 * Deux sémaphores binaires passent la main d'une tâche à l'autre et ordonnent aussi la mémoire : ce que la tâche USB
 * écrit dans req avant de donner req_ready est vu par la tâche de SESSION qui le prend, et réciproquement pour rep.
 * La latence ajoutée est d'un tour de boucle de SESSION, 1 ms.
 *
 * Le chien de garde (ticket #528, sdkconfig.defaults) : ni `usb_rx` ni `journal` n'y sont abonnées.
 *   - `usb_rx` attend sans limite la réponse de la SESSION (deliver, portMAX_DELAY), aussi longtemps qu'une réponse
 *     différée attend l'objectif ; la SESSION est abonnée, et une SESSION figée redémarre la carte. Ses autres attentes
 *     sont bornées : la lecture (50 ms), l'écriture (200 ms). L'abonner obligerait à la nourrir pendant qu'elle attend
 *     une autre tâche : le chien de garde n'y verrait rien de plus ;
 *   - `journal` n'attend que des délais bornés (20 ms, l'écriture 500 ms). Figée, elle ne coûte que des lignes du
 *     journal, et pas en silence : l'anneau plein, chaque ligne perdue est comptée (`dropped` de DEBUG). */

#define MUST(x) do { if (!(x)) abort(); } while (0)

#define USB_LINE_MAX 256u
#define JOURNAL_LINE 600u

static SemaphoreHandle_t out_mutex;            /* une ligne entière par prise */
static SemaphoreHandle_t req_ready, rep_ready; /* la boîte aux lettres : tâche USB -> tâche de SESSION -> tâche USB */
static char req[USB_LINE_MAX];
static char rep[BSK_HOST_REPLY_MAX];
static const char *rep_tail;                   /* ce que bsk_host_line rend : écrit après rep ; NULL, rien d'écrit */
static bool later;                             /* la réponse de la ligne prise attend l'objectif */
static bool cut;                               /* la dernière ligne de texte n'est pas partie en entier, sa fin de ligne
                                                  non plus : la suivante l'écrit d'abord (write_line), sous out_mutex */
static TaskHandle_t rx_handle, journal_handle;

static const char *reset_str(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "poweron"; case ESP_RST_SW: return "sw"; case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int_wdt"; case ESP_RST_TASK_WDT: return "task_wdt"; case ESP_RST_WDT: return "wdt";
    case ESP_RST_BROWNOUT: return "brownout"; case ESP_RST_USB: return "usb"; case ESP_RST_JTAG: return "jtag";
    default: return "unknown";
    }
}

static uint64_t clock_us(void) { return (uint64_t)esp_timer_get_time(); }

/* Une ligne entière, `b` puis `tail`, sous le mutex de sortie, que prend aussi la réponse à LOG OFF. `journal` : la
 * ligne n'est écrite que si sa génération est encore la courante, vérifiée sous ce mutex : une ligne produite avant
 * LOG OFF ne sort pas après son `ok`. Périmée, rien n'est écrit et ce n'est pas une perte.
 * Une ligne de texte a une fin de ligne (`tail` non vide : une réponse à une lettre ou en majuscules, une ligne du
 * journal) ; une réponse Moonlite n'en a pas (`tail` vide, PROTOCOL.md § 4). Une ligne de texte partie en partie (des
 * octets écrits, pas tous, ou pas sa fin de ligne) est une perte (faux, `dropped`) : la ligne de texte suivante écrit
 * d'abord la fin de ligne qui lui manque, pour ne pas s'y coller (PROTOCOL.md § 1) ; si cette fin de ligne ne part pas,
 * rien de la suivante ne part, perdue aussi. Une réponse Moonlite n'écrit jamais de fin de ligne, ni avant ni après :
 * intercalée, elle se colle à la ligne coupée, et la fin de ligne attend la ligne de texte suivante. */
static bool write_line(const char *b, size_t n, const char *tail, uint32_t timeout_ms, bool journal, uint8_t gen)
{
    int w, wt = 0;
    size_t nt = strlen(tail);
    if (xSemaphoreTake(out_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) return false;
    if (journal && bsk_journal_gen() != gen) {
        xSemaphoreGive(out_mutex);
        return true;
    }
    if (nt && cut && usb_serial_jtag_write_bytes("\n", 1, pdMS_TO_TICKS(timeout_ms)) != 1) {
        xSemaphoreGive(out_mutex);
        return false;
    }
    w = usb_serial_jtag_write_bytes(b, n, pdMS_TO_TICKS(timeout_ms));
    if (nt && w == (int)n) wt = usb_serial_jtag_write_bytes(tail, nt, pdMS_TO_TICKS(timeout_ms));
    if (nt) cut = w > 0 && !(w == (int)n && wt == (int)nt);
    xSemaphoreGive(out_mutex);
    return w == (int)n && wt == (int)nt;
}

/* Une ligne cadrée : à la tâche de SESSION, puis sa réponse sur l'USB, suivie de ce que HOTE a dit d'écrire après elle
 * (bsk_host.h) : rien n'est ajouté ici. */
static void deliver(const char *line, size_t len)
{
    memcpy(req, line, len + 1);
    xSemaphoreGive(req_ready);
    xSemaphoreTake(rep_ready, portMAX_DELAY);
    if (rep_tail && !write_line(rep, strlen(rep), rep_tail, 200, false, 0)) bsk_journal_lost();
}

/* Le cadrage : `\r` ignoré, blancs de tête sautés, `:` … `#` pour Moonlite, 256 octets au plus ; une ligne trop longue
 * ou avec un NUL est jetée entière. */
static void rx_task(void *arg)
{
    static char line[USB_LINE_MAX];
    size_t len = 0;
    bool discard = false;                      /* ligne trop longue ou NUL : on jette tout jusqu'à la fin de message */
    bool moon = false;                         /* cadrage du message en cours : ':'...'#' ou ...'\n' */
    uint8_t c;
    (void)arg;
    for (;;) {
        int n = usb_serial_jtag_read_bytes(&c, 1, pdMS_TO_TICKS(50));
        if (n <= 0) continue;
        if (c == '\r') continue;
        if (len == 0 && !discard) {
            if (c == ' ' || c == '\t' || c == '\n') continue;
            moon = c == ':';
        }
        if ((moon && c == '#') || (!moon && c == '\n')) {
            if (discard) {
                discard = false;
                len = 0;
                continue;
            }
            line[len] = '\0';
            if (len) deliver(line, len);
            len = 0;
            continue;
        }
        if (moon && c == '\n') {               /* ':' orphelin */
            len = 0;
            discard = false;
            continue;
        }
        if (discard) continue;
        if (c == 0) {
            discard = true;
            len = 0;
            continue;
        }
        if (len < USB_LINE_MAX - 1) line[len++] = (char)c;
        else {
            discard = true;
            len = 0;
        }
    }
}

static void journal_task(void *arg)
{
    static char b[JOURNAL_LINE];
    (void)arg;
    for (;;) {
        uint8_t gen;
        size_t n = bsk_journal_take(b, sizeof b, &gen);
        if (!n) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (!write_line(b, n, "\n", 500, true, gen)) bsk_journal_lost();
    }
}

void bsk_host_usb_init(void)
{
    usb_serial_jtag_driver_config_t cfg = {
        .rx_buffer_size = 1024,
        .tx_buffer_size = 8192,
    };
    out_mutex = xSemaphoreCreateMutex();
    req_ready = xSemaphoreCreateBinary();
    rep_ready = xSemaphoreCreateBinary();
    bsk_journal_init(clock_us);
    bsk_host_init(esp_app_get_description()->version, reset_str());
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));
    /* le cœur 0, comme toutes les tâches (décision de l'humain) : la carte finale est un ESP32-C3, à un seul cœur */
    MUST(xTaskCreatePinnedToCore(rx_task, "usb_rx", 4096, NULL, 6, &rx_handle, 0) == pdPASS);
    MUST(xTaskCreatePinnedToCore(journal_task, "journal", 3072, NULL, 3, &journal_handle, 0) == pdPASS);
}

/* bsk_host.h, sur la tâche de SESSION : la sienne par NULL ; celles de la PHY et du magasin par leur nom, celui de leur
 * création (components/phy/phy.c, components/store/store.c), sans élargir leurs en-têtes, que les PHY et le magasin
 * simulés implémentent aussi. Créées avant la boucle de app_main, qui seule appelle ceci : un nom introuvable est un
 * renommage oublié ici, un bug, qui arrête la carte (MUST). ESP-IDF compte la pile en octets, pas en mots. */
void bsk_host_stacks(bsk_host_stacks_t *s)
{
    TaskHandle_t phy = xTaskGetHandle("phy"), store = xTaskGetHandle("store");
    MUST(phy != NULL && store != NULL);
    s->session = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    s->phy = (uint32_t)uxTaskGetStackHighWaterMark(phy);
    s->usb_rx = (uint32_t)uxTaskGetStackHighWaterMark(rx_handle);
    s->store = (uint32_t)uxTaskGetStackHighWaterMark(store);
    s->journal = (uint32_t)uxTaskGetStackHighWaterMark(journal_handle);
}

/* Une ligne prise dont la réponse attend l'objectif (BSK_HOST_LATER) est redemandée à chaque tour, après
 * bsk_host_observe ; aucune autre n'est prise avant : la tâche USB attend sa réponse. */
void bsk_host_usb_service(void)
{
    if (later) {
        rep_tail = bsk_host_later(rep, sizeof rep);
    } else {
        if (xSemaphoreTake(req_ready, 0) != pdTRUE) return;
        rep_tail = bsk_host_line(req, rep, sizeof rep);
    }
    later = rep_tail == BSK_HOST_LATER;
    if (!later) xSemaphoreGive(rep_ready);
}
