/* SOURCE: spec de l'atelier § 1 (R1) et § 2 — la PHY de la carte (include/bsk_phy.h), sur ESP-IDF 5.3.2
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — compilé par ESP-IDF seul : aucun test hôte ne le joue (seul phy_common.c l'est, dans les deux PHY) ;
 *         ticket #528 : l'ISR de l'UART en IRAM, la tâche abonnée au chien de garde, le compteur des fronts perdus, la
 *         VD refusée arrête la carte
 *
 * L'UART, les lignes, la VD, les rails et le verrou de XDETECT de la carte ; la réception en flux et la présence
 * viennent de phy_common.c.
 */
#include "bsk_phy.h"

#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_rom_sys.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
/* Exception à -Wconversion (components/phy/CMakeLists.txt), pour cet en-tête seul : les fonctions inline de
 * hal/gpio_ll.h d'ESP-IDF 5.3.2 (gpio_ll.h:187, :564, :565, et les macros REG_SET_FIELD / SET_PERI_REG_BITS de
 * soc/soc.h:87, :140 qu'elles déploient) convertissent sans cast ; on ne touche pas à ESP-IDF. Notre code qui les
 * appelle reste sous l'option. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "hal/gpio_ll.h"
#pragma GCC diagnostic pop
#include "soc/gpio_struct.h"

#include "phy_common.h"
#include "pins.h"

/* UART : 750 000 bauds 8N1, tampons de 4096 et 1200 octets, seuil de FIFO 8 et délai de réception de 2 symboles,
 * la configuration éprouvée sur le 135. CS_GUARD_US : la garde de BODY_CS autour des octets émis. */
#define BAUD         750000
#define CS_GUARD_US  40
#define UART_RX_BUF  4096
#define UART_TX_BUF  1200
#define UART_EV_LEN  32
#define RAW_LEN      64          /* fronts de LENS_CS et de VD, des interruptions à la tâche */
#define EV_LEN       32          /* événements remontés, en attente de bsk_phy_poll */
#define POLL_MS      5           /* D2 et le flux relus au moins toutes les 5 ms */

#define VD_TIMER   LEDC_TIMER_0
#define VD_CHANNEL LEDC_CHANNEL_0
#define VD_MODE    LEDC_LOW_SPEED_MODE
#define VD_RES     LEDC_TIMER_13_BIT
#define VD_LOW_US  60

#define MUST(x) do { if (!(x)) abort(); } while (0)

typedef enum { RAW_LENS_CS, RAW_VD, RAW_BOUNCE } raw_kind_t;   /* RAW_BOUNCE : posé par drop_expire */

typedef struct {
    raw_kind_t kind;
    bool       level;
    int64_t    t_us;
} raw_t;

static QueueHandle_t uart_q, raw_q, ev_q;
static QueueSetHandle_t set;
/* Les fronts qu'une interruption n'a pas pu poser (file des fronts pleine), comptés par les interruptions sous raw_mux
 * (cs_isr, vd_isr et drop_expire peuvent s'interrompre l'une l'autre si leurs niveaux diffèrent) ; la tâche de PHY lit
 * le compteur d'un seul accès (un mot aligné) et rapporte une E_BUS dès qu'il a changé depuis son dernier relevé
 * (raw_seen, à elle seule) : un front perdu entre la lecture et le relevé suivant reste compté, et rapporté au tour
 * suivant (audit M7 : le booléen d'avant pouvait être remis à faux par-dessus un front perdu). */
static volatile uint32_t raw_lost;
static portMUX_TYPE raw_mux = portMUX_INITIALIZER_UNLOCKED;

/* La présence et le verrou, partagés par les interruptions, la tâche de PHY et les commandes, sous pres_mux. */
static pres_t pres;
static portMUX_TYPE pres_mux = portMUX_INITIALIZER_UNLOCKED;
static esp_timer_handle_t drop_timer;    /* l'échéance d'une retombée */

/* État de la tâche de PHY, qu'elle seule touche. */
static rx_t    rx;                       /* la réception en flux (phy_common.h) */
static bool    lens_cs;
static uint32_t raw_seen;                /* raw_lost au dernier relevé */

/* Les compteurs de DEBUG (bsk_phy_stats), à zéro au démarrage. evq_drop : écrit par la tâche de PHY seule (push), lu par
 * la tâche de SESSION, un mot aligné, lu et écrit d'un seul accès. tx_wait_us et tx_timeout : la tâche de SESSION seule,
 * qui émet (bsk_phy_send) et les lit. */
static volatile uint32_t evq_drop;
static uint32_t tx_wait_us, tx_timeout;

/* ─────────────────────────── interruptions ─────────────────────────── */
/* Tout ce qui touche au verrou de XDETECT le fait sous pres_mux, la section critique que prennent aussi les
 * interruptions :
 *   - bsk_phy_rail lit le verrou et allume le rail sous elle : aucune retombée entre les deux ;
 *   - bsk_phy_lines(true) et bsk_phy_vd(hz) passent par le pilote (uart_set_pin, ledc_*), qu'on n'appelle pas sous
 *     section critique : le verrou est lu avant (refus, rien n'est touché), puis relu après, sous elle (lock_held) ;
 *     s'il est tombé pendant l'appel, cut() est rejouée et la commande rend false. La fenêtre où une ligne peut être
 *     pilotée après la retombée est la fin de l'appel au pilote, quelques microsecondes ;
 *   - la tâche de PHY lit D2 au moins toutes les POLL_MS pour l'insertion (pres_sample), bsk_phy_poll rend la présence
 *     tenue (pres_next). La retombée, elle, n'est pas lue par la tâche : elle passe par detect_isr. */

static void IRAM_ATTR raw_lost_one(void)
{
    portENTER_CRITICAL_ISR(&raw_mux);
    raw_lost = raw_lost + 1;
    portEXIT_CRITICAL_ISR(&raw_mux);
}

static void IRAM_ATTR raw_from_isr(raw_kind_t kind, bool level)
{
    raw_t r = {kind, level, esp_timer_get_time()};
    BaseType_t hp = pdFALSE;
    if (xQueueSendFromISR(raw_q, &r, &hp) != pdTRUE) raw_lost_one();
    if (hp) portYIELD_FROM_ISR();
}

static void IRAM_ATTR cs_isr(void *arg)
{
    (void)arg;
    raw_from_isr(RAW_LENS_CS, gpio_ll_get_level(&GPIO, PIN_LENS_CS) != 0);
}

static void IRAM_ATTR vd_isr(void *arg)
{
    (void)arg;
    raw_from_isr(RAW_VD, false);
}

/* La coupure du verrou, sans tâche ni file, en IRAM, des registres seulement (gpio_ll, en ligne). Appelée sous
 * pres_mux. Les rails ; l'interruption de la VD avant sa sortie (la retombée vers le repos n'est pas un front de VD) ;
 * la sortie de TXD, BODY_CS et VD coupée, détachée de l'UART et du LEDC. Le tirage bas y est déjà, posé par
 * bsk_phy_init et rest(), et aucune commande de ce fichier ne le retire.
 * Elle n'arrête pas le LEDC (ledc_stop n'est pas en IRAM) : il tourne, détaché de la broche et sans interruption,
 * jusqu'à ce que la SESSION arrête la VD en passant à OFF, au pas suivant (une milliseconde environ, plus une écriture
 * en flash en cours). De la retombée à la coupure : 2 ms, plus la prise de l'interruption de l'esp_timer et le parcours
 * de sa liste (quelques dizaines de microsecondes estimées), plus la plus longue section critique en cours sur le
 * cœur 0 — non mesuré. */
static void IRAM_ATTR cut(void)
{
    gpio_ll_set_level(&GPIO, PIN_EN_LOGIC, 0);
    gpio_ll_set_level(&GPIO, PIN_EN_MOTOR, 0);
    gpio_ll_intr_disable(&GPIO, PIN_BODY_VD);
    gpio_ll_output_disable(&GPIO, PIN_LENS_TXD);
    gpio_ll_output_disable(&GPIO, PIN_BODY_CS);
    gpio_ll_output_disable(&GPIO, PIN_BODY_VD);
}

/* L'échéance d'une retombée, rappelée en interruption par drop_timer : D2 relu, absent (tirage haut), la coupure ;
 * présent, le rebond vers la tâche par la file des fronts (pleine, une E_BUS comme pour un front perdu). Le réveil
 * passe par esp_timer_isr_dispatch_need_yield, comme l'exige un rappel ESP_TIMER_ISR, pas par portYIELD_FROM_ISR. */
static void IRAM_ATTR drop_expire(void *arg)
{
    uint64_t t = (uint64_t)esp_timer_get_time();
    bool absent = gpio_ll_get_level(&GPIO, PIN_LENS_DETECT) != 0;
    (void)arg;
    portENTER_CRITICAL_ISR(&pres_mux);
    if (pres_expire(&pres, absent, t)) cut();
    portEXIT_CRITICAL_ISR(&pres_mux);
    if (!absent) {
        raw_t r = {RAW_BOUNCE, false, (int64_t)t};
        BaseType_t hp = pdFALSE;
        if (xQueueSendFromISR(raw_q, &r, &hp) != pdTRUE) raw_lost_one();
        if (hp) esp_timer_isr_dispatch_need_yield();
    }
}

/* La retombée de XDETECT (front montant : le tirage haut, objectif absent) : objectif présent, le premier front arme
 * l'échéance ; absent, la coupure (pres_edge). Si l'esp_timer refuse d'être armé, la coupure part tout de suite (le
 * côté sûr). */
static void IRAM_ATTR detect_isr(void *arg)
{
    uint64_t t = (uint64_t)esp_timer_get_time();
    (void)arg;
    portENTER_CRITICAL_ISR(&pres_mux);
    switch (pres_edge(&pres, t)) {
    case PRES_CUT:
        cut();
        break;
    case PRES_ARM:
        if (esp_timer_start_once(drop_timer, D2_DROP_US) != ESP_OK && pres_expire(&pres, true, t)) cut();
        break;
    case PRES_WAIT:
        break;
    }
    portEXIT_CRITICAL_ISR(&pres_mux);
}

/* Le verrou levé ? Sans rien toucher. */
static bool lock_open(void)
{
    bool open;
    portENTER_CRITICAL(&pres_mux);
    open = pres.present;
    portEXIT_CRITICAL(&pres_mux);
    return open;
}

/* Après une commande passée par le pilote : le verrou encore levé ? Sinon, la coupure rejouée. */
static bool lock_held(void)
{
    bool open;
    portENTER_CRITICAL(&pres_mux);
    open = pres.present;
    if (!open) cut();
    portEXIT_CRITICAL(&pres_mux);
    return open;
}

/* ─────────────────────────── la tâche de PHY ─────────────────────────── */

/* La file pleine (personne ne lit assez vite) : on perd le plus ancien, compté (evq_drop de DEBUG). Le seul
 * producteur est cette tâche : la place libérée est la sienne. */
static void push(const bsk_phy_event_t *e)
{
    if (xQueueSend(ev_q, e, 0) != pdTRUE) {
        bsk_phy_event_t old;
        if (xQueueReceive(ev_q, &old, 0) == pdTRUE) evq_drop = evq_drop + 1;
        (void)xQueueSend(ev_q, e, 0);
    }
}

/* Ce que le flux a à rendre, jugé maintenant. */
static void drain(void)
{
    bsk_phy_event_t e = {.kind = BSK_PHY_FRAME};
    while (rx_next(&rx, (uint64_t)esp_timer_get_time(), &e)) push(&e);
}

/* Une E_BUS. flush, une erreur du pilote UART (débordement du FIFO ou du tampon, erreur de trame, de parité, break) :
 * le flux est vidé, des octets lui manquent, sa suite est jugée comme tout octet reçu, et l'erreur porte ceux qu'il
 * n'avait pas encore rapportés (rendus par le pilote, pris par aucune trame : rx_flush) — les octets écartés en attente
 * de leur E_FRAMING, qui n'en aura pas, puis ceux qui attendaient leur jugement. Aucun autre n'est disponible : ceux que
 * uart_flush_input jette ne sont pas lus, et le pilote ne rend pas ceux qu'il a perdus. Sinon, un front de LENS_CS ou de
 * VD perdu (file des interruptions pleine) : le flux n'est pas touché, aucun octet. */
static void bus_error(bool flush)
{
    bsk_phy_event_t e = {.kind = BSK_PHY_ERROR, .t_us = (uint64_t)esp_timer_get_time()};
    e.u.err = E_BUS;
    if (flush) rx_flush(&rx, &e.u.raw);
    push(&e);
}

static void on_uart(const uart_event_t *u)
{
    switch (u->type) {
    case UART_DATA: {                          /* lu dans la place libre, jamais nulle après drain, jugé à sa lecture */
        size_t n = u->size;
        while (n) {
            size_t room = RX_CAP - rx.n;
            int k = uart_read_bytes(LENS_UART, rx.b + rx.n, n < room ? n : room, 0);
            if (k <= 0) break;
            rx_got(&rx, (size_t)k, (uint64_t)esp_timer_get_time());
            n -= (size_t)k;
            drain();
        }
        break;
    }
    case UART_FIFO_OVF:
    case UART_BUFFER_FULL:
        uart_flush_input(LENS_UART);
        bus_error(true);
        break;
    case UART_FRAME_ERR:
    case UART_PARITY_ERR:
    case UART_BREAK:
        bus_error(true);
        break;
    default:                                   /* émission (UART_DATA_BREAK), rien d'autre n'est armé */
        break;
    }
}

static void on_raw(const raw_t *r)
{
    bsk_phy_event_t e = {.t_us = (uint64_t)r->t_us};
    if (r->kind == RAW_VD || r->kind == RAW_BOUNCE) {
        e.kind = r->kind == RAW_VD ? BSK_PHY_VD : BSK_PHY_BOUNCE;
        push(&e);
        return;
    }
    if (r->level == lens_cs) return;
    lens_cs = r->level;
    e.kind = BSK_PHY_LENS_CS;
    e.u.level = lens_cs;
    push(&e);
}

/* Abonnée au chien de garde (ticket #528, sdkconfig.defaults), nourri à chaque tour : sa seule attente est la file des
 * événements, bornée à POLL_MS, et rien d'autre n'y attend (le pilote UART est lu sans attente). Figée, elle ne rendrait
 * plus ni trame, ni front, ni insertion : la SESSION verrait un objectif muet, perdu, sans que rien ne redémarre ; le
 * chien de garde redémarre la carte. Un flux de bruit qui l'occupe la ralentit sans la figer : elle le nourrit encore. */
static void phy_task(void *arg)
{
    (void)arg;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    for (;;) {
        QueueSetMemberHandle_t q;
        uint32_t lost;
        esp_task_wdt_reset();
        q = xQueueSelectFromSet(set, pdMS_TO_TICKS(POLL_MS));
        if (q == uart_q) {
            uart_event_t u;
            if (xQueueReceive(uart_q, &u, 0) == pdTRUE) on_uart(&u);
        } else if (q == raw_q) {
            raw_t r;
            if (xQueueReceive(raw_q, &r, 0) == pdTRUE) on_raw(&r);
        }
        lost = raw_lost;
        if (lost != raw_seen) {                /* des fronts perdus depuis le relevé : LENS_CS publiée peut être fausse */
            raw_seen = lost;
            bus_error(false);
        }
        drain();                               /* un début de trame périmé */
        {                                      /* l'insertion ; tenue dans pres, rendue par bsk_phy_poll */
            bool present = gpio_get_level(PIN_LENS_DETECT) == 0;
            uint64_t t = (uint64_t)esp_timer_get_time();
            portENTER_CRITICAL(&pres_mux);
            (void)pres_sample(&pres, present, t);
            portEXIT_CRITICAL(&pres_mux);
        }
    }
}

/* ─────────────────────────── bsk_phy.h ─────────────────────────── */

/* BODY_CS levée, garde, les octets, attente de fin d'émission, garde, BODY_CS rabaissée.
 * L'attente de fin d'émission (uart_wait_tx_done, 100 ms au plus) bloque l'appelant, la tâche de SESSION : sa durée la
 * plus longue et ses échéances atteintes sont relevées pour DEBUG (tx_wait_us, tx_timeout), deux lectures de
 * l'horloge, rien d'autre.
 * Pas d'attente que LENS_CS retombe avant d'émettre : quand émettre est la décision de TRANSACTION, propriétaire du bus,
 * qui voit LENS_CS en événement. Pas de mutex d'émission, ni de paire 0x03/0x04 sous un même numéro de séquence : un
 * seul émetteur, et la séquence est un champ de la trame, posé au-dessus. */
bsk_err_t bsk_phy_send(const bsk_frame_t *f)
{
    uint8_t b[BSK_FRAME_MAX];
    size_t n = fr_encode(f->cls, f->seq, f->msg, f->len, b, sizeof b);
    int w;
    esp_err_t e;
    int64_t t0, dt;
    gpio_set_level(PIN_BODY_CS, 1);
    esp_rom_delay_us(CS_GUARD_US);
    w = uart_write_bytes(LENS_UART, b, n);
    t0 = esp_timer_get_time();
    e = uart_wait_tx_done(LENS_UART, pdMS_TO_TICKS(100));
    dt = esp_timer_get_time() - t0;
    if (dt > (int64_t)tx_wait_us) tx_wait_us = (uint32_t)dt;   /* 100 ms et une préemption : loin de 2^32 µs */
    if (e == ESP_ERR_TIMEOUT) tx_timeout++;
    esp_rom_delay_us(CS_GUARD_US);
    gpio_set_level(PIN_BODY_CS, 0);
    return w == (int)n && e == ESP_OK ? E_OK : E_BUS;
}

void bsk_phy_body_cs(bool high) { gpio_set_level(PIN_BODY_CS, high ? 1 : 0); }

/* La broche elle-même, pas l'état de la tâche de PHY, qui publie ses fronts. */
bool bsk_phy_lens_cs(void) { return gpio_get_level(PIN_LENS_CS) != 0; }

/* Le repos d'une ligne émise : haute impédance, tirée bas, au démarrage comme à l'arrêt. Sur la carte cible, les rails
 * de l'objectif sont commutés, et une ligne haute vers un objectif non alimenté l'alimenterait par ses diodes de
 * protection. Sur le proto, aux rails non commutés, c'est sans conséquence : le 135 jette tout octet reçu BODY_CS
 * basse, et une VD basse ne produit aucun front. gpio_set_direction en entrée coupe la
 * sortie et détache de la broche tout signal de périphérique, UART ou LEDC (gpio_output_disable ->
 * gpio_ll_output_disable, ESP-IDF 5.3.2). Le tirage d'abord : la broche ne flotte pas entre les deux. */
static void rest(gpio_num_t pin)
{
    gpio_set_pull_mode(pin, GPIO_PULLDOWN_ONLY);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
}

/* L'interruption d'abord : la retombée vers le repos n'est pas un front de VD. */
static void vd_stop(void)
{
    gpio_intr_disable(PIN_BODY_VD);
    ledc_stop(VD_MODE, VD_CHANNEL, 0);
    rest(PIN_BODY_VD);
}

/* LEDC 13 bits, une impulsion basse de VD_LOW_US en tête de période ; la broche relue pour l'interruption du front
 * descendant, qui date chaque VD. La SESSION ne demande que VD_HZ ou 0, et la configuration du LEDC est constante : un
 * refus du LEDC ne peut être qu'un bug, et il arrête la carte (MUST : DEBUG dira reset=panic) au lieu de laisser la VD
 * arrêtée en silence (ticket #528, audit M2). */
bool bsk_phy_vd(uint16_t hz)
{
    uint32_t period_us, low_ticks;
    ledc_timer_config_t t = {
        .speed_mode = VD_MODE,
        .duty_resolution = VD_RES,
        .timer_num = VD_TIMER,
        .freq_hz = hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_channel_config_t c = {
        .gpio_num = PIN_BODY_VD,
        .speed_mode = VD_MODE,
        .channel = VD_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = VD_TIMER,
    };
    if (hz == 0) {
        vd_stop();
        return true;
    }
    if (!lock_open()) return false;
    MUST(ledc_timer_config(&t) == ESP_OK);
    period_us = 1000000u / hz;
    low_ticks = (VD_LOW_US * 8192u + period_us / 2) / period_us;   /* 29 pas à 60 Hz (VD_HZ), seule cadence */
    c.duty = 8192u - low_ticks;                                     /* durée à l'état haut */
    c.hpoint = (int)low_ticks;                                      /* l'impulsion basse en tête de période */
    MUST(ledc_channel_config(&c) == ESP_OK);
    /* relire notre propre sortie pour dater chaque front, sans toucher au routage de sortie (LEDC) */
    gpio_ll_input_enable(&GPIO, PIN_BODY_VD);
    gpio_set_intr_type(PIN_BODY_VD, GPIO_INTR_NEGEDGE);
    gpio_intr_enable(PIN_BODY_VD);
    return lock_held();
}

void bsk_phy_stats(bsk_phy_stats_t *st)
{
    st->evq_drop = evq_drop;
    st->tx_wait_us = tx_wait_us;
    st->tx_timeout = tx_timeout;
}

/* La présence tenue d'abord, si elle précède le premier événement de la file : lu dans `ev` même, que pres_next
 * écrase quand elle rend. */
bool bsk_phy_poll(bsk_phy_event_t *ev)
{
    bool queued = xQueuePeek(ev_q, ev, 0) == pdTRUE;
    bool got;
    portENTER_CRITICAL(&pres_mux);
    got = pres_next(&pres, queued ? ev->t_us : UINT64_MAX, ev);
    portEXIT_CRITICAL(&pres_mux);
    return got || (queued && xQueueReceive(ev_q, ev, 0) == pdTRUE);
}

bool bsk_phy_rail(bsk_rail_t rail, bool on)
{
    bool ok;
    portENTER_CRITICAL(&pres_mux);
    ok = !on || pres.present;
    if (ok) gpio_ll_set_level(&GPIO, rail == BSK_RAIL_LOGIC ? PIN_EN_LOGIC : PIN_EN_MOTOR, on ? 1 : 0);
    portEXIT_CRITICAL(&pres_mux);
    return ok;
}

/* Pilotées : TXD rattachée à l'UART (uart_set_pin la pose haute avant de la router), BODY_CS basse avant que sa
 * sortie ne soit ouverte. La VD n'est pas touchée : bsk_phy_vd la démarre. */
bool bsk_phy_lines(bool drive)
{
    if (!drive) {
        rest(PIN_LENS_TXD);
        rest(PIN_BODY_CS);
        return true;
    }
    if (!lock_open()) return false;
    ESP_ERROR_CHECK(uart_set_pin(LENS_UART, PIN_LENS_TXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    gpio_set_level(PIN_BODY_CS, 0);
    gpio_set_direction(PIN_BODY_CS, GPIO_MODE_OUTPUT);
    return lock_held();
}

void bsk_phy_init(void)
{
    const gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_EN_LOGIC) | (1ULL << PIN_EN_MOTOR),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
    };
    const gpio_config_t lines = {   /* au repos (rest) : TXD n'est pas routée, VD arrêtée */
        .pin_bit_mask = (1ULL << PIN_LENS_TXD) | (1ULL << PIN_BODY_CS) | (1ULL << PIN_BODY_VD),
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
    };
    const gpio_config_t detect = {   /* la retombée (front montant, tirage haut) par interruption */
        .pin_bit_mask = 1ULL << PIN_LENS_DETECT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    const gpio_config_t cs = {
        .pin_bit_mask = 1ULL << PIN_LENS_CS,
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    const uart_config_t ucfg = {
        .baud_rate = BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    /* rien n'est émis : lignes au repos, rails coupés */
    ESP_ERROR_CHECK(gpio_config(&lines));
    ESP_ERROR_CHECK(gpio_config(&out));
    gpio_set_level(PIN_EN_LOGIC, 0);
    gpio_set_level(PIN_EN_MOTOR, 0);
    ESP_ERROR_CHECK(gpio_config(&detect));
    ESP_ERROR_CHECK(gpio_config(&cs));

    MUST((raw_q = xQueueCreate(RAW_LEN, sizeof(raw_t))) != NULL);
    MUST((ev_q = xQueueCreate(EV_LEN, sizeof(bsk_phy_event_t))) != NULL);
    MUST((set = xQueueCreateSet(RAW_LEN + UART_EV_LEN)) != NULL);
    MUST(xQueueAddToSet(raw_q, set) == pdPASS);

    ESP_ERROR_CHECK(uart_param_config(LENS_UART, &ucfg));
    /* ESP_INTR_FLAG_IRAM, avec CONFIG_UART_ISR_IN_IRAM (sdkconfig.defaults) : l'ISR de l'UART vide le FIFO de
     * réception pendant une écriture de la marque en NVS, cache de la flash coupé (ticket #528, audit M3) */
    ESP_ERROR_CHECK(uart_driver_install(LENS_UART, UART_RX_BUF, UART_TX_BUF, UART_EV_LEN, &uart_q, ESP_INTR_FLAG_IRAM));
    MUST(xQueueAddToSet(uart_q, set) == pdPASS);   /* vide : les broches ne sont pas encore routées */
    /* RXD seule : TXD reste au repos jusqu'à bsk_phy_lines(true) */
    ESP_ERROR_CHECK(uart_set_pin(LENS_UART, UART_PIN_NO_CHANGE, PIN_LENS_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_set_rx_full_threshold(LENS_UART, 8));
    ESP_ERROR_CHECK(uart_set_rx_timeout(LENS_UART, 2));

    lens_cs = gpio_get_level(PIN_LENS_CS) != 0;
    /* Avant l'interruption qui l'arme. ESP_TIMER_ISR exige CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD
     * (sdkconfig.defaults). Pourquoi un esp_timer : son interruption est allouée avec ESP_INTR_FLAG_IRAM et, l'option
     * posée, son traitement des rappels en interruption est en IRAM, comme esp_timer_start_once (lu dans les sources
     * d'ESP-IDF 5.3.2, non vérifié sur la carte). Il compte le temps réel, ni en VD ni en tick FreeRTOS. */
    {
        const esp_timer_create_args_t drop = {
            .callback = drop_expire,
            .dispatch_method = ESP_TIMER_ISR,
            .name = "xdetect",
        };
        ESP_ERROR_CHECK(esp_timer_create(&drop, &drop_timer));
    }
    /* ESP_INTR_FLAG_IRAM : les interruptions tournent en IRAM, cache de la flash coupé compris (une écriture de la
     * marque en NVS) ; un front n'est pas perdu pendant une écriture en flash */
    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_IRAM));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_LENS_CS, cs_isr, NULL));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_BODY_VD, vd_isr, NULL));
    /* le verrou est posé (pres à zéro, « absent ») avant que l'interruption ne soit armée ; une insertion se confirme
     * par la tâche, 300 ms après */
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_LENS_DETECT, detect_isr, NULL));

    /* le cœur 0, comme toutes les tâches (décision de l'humain) : la carte cible est un ESP32-C3, à un seul cœur ; sur
     * la S3, un cœur réservé à la PHY la testerait dans de meilleures conditions que la cible. */
    MUST(xTaskCreatePinnedToCore(phy_task, "phy", 4096, NULL, 12, NULL, 0) == pdPASS);
}
