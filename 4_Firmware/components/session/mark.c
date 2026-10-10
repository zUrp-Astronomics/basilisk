/* SOURCE: spec de l'atelier § 9 — la marque, et le bouton du fût (voir mark.h)
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — C pur, sans ESP-IDF : compilé aussi par les tests hôte (le magasin y est sim/store_sim.c)
 *
 * Le magasin est asynchrone (bsk_store.h) : la marque de la clé courante est tenue ici, relue quand la clé change
 * (objectif, ou focale nominale : un zoom), posée ou effacée tout de suite, rangée ensuite. Tant que la lecture n'est pas
 * rendue, il n'y a pas de marque. Une écriture qui échoue est journalisée et la clé relue : ce qui est publié est ce qui
 * est rangé. */
#include "mark.h"

#include <stdio.h>
#include <string.h>

#include "bsk_journal.h"
#include "bsk_store.h"
#include "lens_rx.h"

#define BTN_BIT     0x08u        /* offset 64, bit 3 */
#define BTN_LONG_US 1000000u
#define BTN_HOLD_US 4000000u     /* tenu plus longtemps, rien : un bouton tenu pendant un homing n'écrase pas la marque */
#define DIR_SHIFT   16u          /* le sens, au-dessus des 16 bits de la position */

static struct {
    char       key[BSK_STORE_KEY_MAX];   /* la clé courante ; "" : aucune (pas d'identité, pas de 0x05) */
    bool       loading;                  /* la lecture de la clé courante n'est pas encore rendue */
    bool       ask;                      /* ... ni déposée : la file était pleine */
    uint32_t   want;                     /* le numéro de son dépôt */
    bool       valid;
    bsk_mark_t m;

    bool       have_pos;                 /* le sens : la position du dernier 0x06, et son dernier changement */
    uint16_t   pos;
    uint8_t    dir;

    bool       pressed;                  /* le bouton : enfoncé depuis t_press, l'état de SESSION d'alors */
    uint64_t   t_press;
    uint8_t    press_state;
} k;

/* Chaque dépôt accepté rend un résultat, dans l'ordre (bsk_store.h) : le n-ième relevé est celui du n-ième dépôt. Ces
 * compteurs suivent la file, pas la session : mark_forget ne les touche pas. */
static uint32_t sent, got;

/* Les gestes de l'utilisateur, pose et effacement servis ; comme sent et got, ils ne suivent pas la session. */
static uint16_t sets, clears;

static bool deposit(bsk_store_op_t op, uint32_t v)
{
    if (!bsk_store_request(op, k.key, v)) return false;
    sent++;
    return true;
}

/* Bits 0-15 la position, 16-23 le sens. Une valeur sans sens (0 à 0xFFFF, la forme des marques déjà rangées sur les
 * cartes) se relit avec un sens inconnu. */
static uint32_t encode(const bsk_mark_t *m) { return (uint32_t)m->position | (uint32_t)m->approach_dir << DIR_SHIFT; }

/* Un sens invalide est lu inconnu, la marque gardée à sa position : inconnu et invalide mènent au même défaut
 * (bsk_mark.h). Une valeur dont les bits 24-31 ne sont pas nuls est ignorée. */
static bool decode(uint32_t v, bsk_mark_t *m)
{
    uint8_t dir = (uint8_t)(v >> DIR_SHIFT);
    if (v >> 24) return false;
    m->position = (int32_t)(v & 0xFFFFu);
    m->approach_dir = dir <= BSK_APPROACH_DECREASING ? dir : BSK_APPROACH_UNKNOWN;
    return true;
}

/* La clé : les 5 octets d'identité (lens_rx.h) en hexa, puis la focale nominale arrondie au mm sur 3 chiffres hexa,
 * 13 caractères. C'est la forme des clés déjà rangées sur les cartes : la changer rend leurs marques introuvables.
 * "" sans identité ou sans 0x05. */
static void key_of(char *out, size_t cap)
{
    const lens_rx_t *l = lens_rx();
    out[0] = '\0';
    if (!l->have_id || !l->have05) return;
    snprintf(out, cap, "%02x%02x%02x%02x%02x%03x", l->id[0], l->id[1], l->id[2], l->id[3], l->id[4],
             (unsigned)(((l->focal_nom + 5) / 10) & 0xFFF));
}

static void ask(void)
{
    if (!k.ask || !deposit(BSK_STORE_GET, 0)) return;
    k.ask = false;
    k.want = sent;
}

static void reload(void)
{
    k.valid = false;
    k.loading = k.ask = k.key[0] != '\0';
    ask();
}

static void on_result(const bsk_store_res_t *r)
{
    got++;
    if (r->op == BSK_STORE_GET) {
        if (!k.loading || k.ask || got != k.want) return;   /* une lecture périmée : la clé a changé, ou la marque */
        k.loading = false;
        k.valid = r->ok && decode(r->value, &k.m);
    } else if (!r->ok) {
        bsk_journal_mark(BSK_MARK_STORE_ER, r->key, NULL);
        if (!strcmp(r->key, k.key)) reload();
    }
}

void mark_poll(void)
{
    bsk_store_res_t r;
    ask();
    while (bsk_store_result(&r)) on_result(&r);
}

void mark_init(void)
{
    bsk_store_res_t r;
    while (bsk_store_result(&r)) {}
    sent = got = 0;
    sets = clears = 0;
    memset(&k, 0, sizeof k);
}

void mark_forget(void) { memset(&k, 0, sizeof k); }

void mark_frame(uint8_t type)
{
    const lens_rx_t *l = lens_rx();
    char nk[BSK_STORE_KEY_MAX];
    if (type == 0x06) {                             /* le sens du dernier changement : goto, bague ou homing ; un 0x06
                                                       lu seulement (lens_rx_frame rend 0 pour un autre) */
        if (k.have_pos && l->pos != k.pos) k.dir = l->pos > k.pos ? BSK_APPROACH_INCREASING : BSK_APPROACH_DECREASING;
        k.pos = l->pos;
        k.have_pos = true;
    }
    if (type != 0x05 && type != 0x07 && type != 0x08) return;
    key_of(nk, sizeof nk);
    if (!strcmp(nk, k.key)) return;
    memcpy(k.key, nk, sizeof nk);
    reload();
    mark_poll();
}

/* La position doit être dans les bornes du 0x06, comme la cible d'un goto : une marque hors d'elles ne serait jamais
 * atteinte par `jg`. Une position donnée (arg autre que BSK_MARK_HERE) pose un sens inconnu. */
bsk_err_t mark_set(int32_t arg)
{
    const lens_rx_t *l = lens_rx();
    bsk_mark_t m = {.position = arg, .approach_dir = BSK_APPROACH_UNKNOWN};
    if (!k.key[0]) return E_NOCAP;
    if (!l->have06) return E_LIMIT;                 /* ni position ni bornes */
    if (arg == BSK_MARK_HERE) {
        m.position = l->pos;
        m.approach_dir = k.dir;
    }
    if (m.position < l->fmin || m.position > l->fmax) return E_LIMIT;
    if (!deposit(BSK_STORE_PUT, encode(&m))) return E_BUSY;
    k.m = m;
    k.valid = true;
    k.loading = k.ask = false;
    sets++;
    bsk_journal_mark(BSK_MARK_SET, k.key, &m);
    mark_poll();
    return E_OK;
}

bsk_err_t mark_clear(void)
{
    if (!k.key[0]) return E_NOCAP;
    if (!deposit(BSK_STORE_DEL, 0)) return E_BUSY;
    clears++;
    k.valid = k.loading = k.ask = false;
    bsk_journal_mark(BSK_MARK_CLEAR, k.key, NULL);
    mark_poll();
    return E_OK;
}

uint16_t mark_sets(void) { return sets; }
uint16_t mark_clears(void) { return clears; }

bool mark_get(bsk_mark_t *m)
{
    if (k.valid) *m = k.m;
    return k.valid;
}

bool mark_loading(void) { return k.loading; }

const char *mark_key(void) { return k.key; }

uint8_t mark_dir(void) { return k.dir; }

/* Tout se décide au relâchement : moins d'1 s court, de 1 à 4 s long, 4 s ou plus rien. Un bouton déjà enfoncé au
 * premier 0x05 qui porte l'offset 64 est un appui qui commence là. */
bool mark_button(uint64_t t, uint8_t state, bsk_btn_t *press, uint8_t *at)
{
    const lens_rx_t *l = lens_rx();
    bool down = l->o64 & BTN_BIT;
    uint64_t d = t - k.t_press;
    if (down == k.pressed) return false;
    k.pressed = down;
    if (down) {
        k.t_press = t;
        k.press_state = state;
        return false;
    }
    *press = d < BTN_LONG_US ? BSK_BTN_SHORT : d < BTN_HOLD_US ? BSK_BTN_LONG : BSK_BTN_HELD;
    *at = k.press_state;
    return true;
}
