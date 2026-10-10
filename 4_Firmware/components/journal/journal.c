/* SOURCE: PROTOCOL.md § 3 (`LOG`, les lignes `* …`) — le journal (voir include/bsk_journal.h)
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — C pur, sans ESP-IDF : compilé aussi par les tests hôte
 *
 * L'anneau de 8 Ko (entrées [longueur 16 bits][génération][octets]), le plafond de 30 lignes par seconde, les lignes.
 *
 * L'anneau n'a qu'un producteur, la tâche de SESSION, et qu'un consommateur : des indices atomiques, pas de verrou. */
#include "bsk_journal.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "bsk_contract.h"
#include "phy_common.h"

#define RING     8192u
#define PER_S    30u
#define CHUNK    64u
#define LEN04    14u     /* un 0x04 seul ; au-delà, la consigne 0x1D le suit */
#define LEN06    40u     /* un 0x06 seul ; au-delà, des accusés le suivent */
#define LINE_MAX 320u    /* une ligne d'octets à son plus long (PRE_FITS) ; la place d'une différence (diff_line) */
#define MARK_MAX 24u     /* le repère « +<off>/<n>#<id> » à son plus long : off et n (16 bits) sur 5 chiffres, id (32) sur 10 */
#define N_REF    5u      /* les références de la forme différence, une par sens et type (KEYS) */
#define LOST_N   0xFFFFFFu   /* le compte des lignes perdues par le consommateur, sous sa génération */

/* La borne de hex_lines et de frame_all, qui cumulent leurs snprintf dans une ligne de LINE_MAX : un préfixe écrit par
 * snprintf dans un tampon de `size` octets (`size` - 1 caractères au plus), le repère et 64 octets en hexa (3 * CHUNK)
 * y tiennent, terminateur compris. Chaque tampon de préfixe passé à l'une ou à l'autre le vérifie, à sa déclaration :
 * `w` ne dépasse jamais `sizeof s`, et la taille passée au snprintf suivant n'est jamais négative. Le repère : `off` et
 * `n` viennent d'un fr_encode (au plus BSK_FRAME_MAX) ou d'un bsk_phy_raw_t (uint16_t), `id` d'un compteur uint32_t. */
#define PRE_FITS(size) ((size) - 1u + MARK_MAX + 3u * CHUNK < LINE_MAX)

/* Les trames que LOG ALL écrit en différence, celles de la boucle (0x03 et 0x04 émis ; 0x02, 0x05 et 0x06
 * reçus), quelle que soit leur longueur ; toute autre trame est écrite entière. */
static const struct { bool rx; uint8_t type; } KEYS[N_REF] = {{false, 0x03}, {false, 0x04}, {true, 0x02}, {true, 0x05},
                                                             {true, 0x06}};

/* La dernière trame écrite d'un sens et d'un type, et le pas de son numéro de séquence (PROTOCOL.md § 3, LOG ALL). */
typedef struct {
    bool     have;
    uint8_t  cls, seq, step;
    uint16_t len;
    uint8_t  msg[BSK_MSG_MAX];
} ref_t;

static struct {
    uint64_t (*clock)(void);
    bool     on;
    _Atomic bool all;            /* écrit par le producteur, lu aussi par bsk_journal_lost */
    uint8_t  ring[RING];
    _Atomic uint32_t head;       /* écrit par le producteur seul */
    _Atomic uint32_t tail;       /* écrit par le consommateur seul */
    _Atomic uint8_t  gen;
    uint32_t dropped;            /* plafond, anneau plein : le producteur */
    _Atomic uint32_t lost;       /* écritures ratées : le consommateur */
    bool     win;                /* une fenêtre d'une seconde est ouverte */
    uint64_t win_ms;
    uint32_t win_n, win_rx;      /* lignes admises, trames reçues tues */
    uint32_t frames;             /* le numéro #<id> des morceaux */
    ref_t    ref[N_REF];         /* le producteur seul */
    uint32_t seen;               /* bsk_journal_dropped() à la dernière trame écrite sous LOG ALL */
    uint32_t unsaid;             /* lignes perdues anneau plein sous LOG ALL, pas encore dites (`* lost`) */
    _Atomic uint32_t lost_unsaid;   /* écritures ratées sous LOG ALL, pas encore dites : génération << 24 | compte */
} j;

static uint64_t now_us(void) { return j.clock ? j.clock() : 0; }

/* ─────────────────────────── l'anneau ─────────────────────────── */

static bool room(size_t n)
{
    uint32_t h = atomic_load_explicit(&j.head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&j.tail, memory_order_acquire);
    uint32_t used = (h - t + RING) % RING;
    return RING - 1 - used >= n + 3;
}

static void put_raw(const char *b, size_t n)
{
    uint32_t h = atomic_load_explicit(&j.head, memory_order_relaxed);
    j.ring[h] = (uint8_t)(n & 0xFF);
    h = (h + 1) % RING;
    j.ring[h] = (uint8_t)(n >> 8);
    h = (h + 1) % RING;
    j.ring[h] = atomic_load_explicit(&j.gen, memory_order_relaxed);
    h = (h + 1) % RING;
    for (size_t i = 0; i < n; i++, h = (h + 1) % RING) j.ring[h] = (uint8_t)b[i];
    atomic_store_explicit(&j.head, h, memory_order_release);
}

/* Plein : la ligne est perdue et comptée, faux. Sous LOG ALL, la perte est dite par `* lost <n>` avant la ligne suivante
 * qui tient (la ligne et son repère ensemble, ou rien) ; sous LOG ON, seulement celle des morceaux d'une ligne écrite en
 * partie (hex_lines). */
static bool put(const char *b, size_t n)
{
    char m[24];
    int k = j.unsaid ? snprintf(m, sizeof m, "* lost %lu", (unsigned long)j.unsaid) : 0;
    if (!room(n + (k ? (size_t)k + 3 : 0))) {
        j.dropped++;
        if (atomic_load_explicit(&j.all, memory_order_relaxed)) j.unsaid++;
        return false;
    }
    if (k) put_raw(m, (size_t)k);
    j.unsaid = 0;
    put_raw(b, n);
    return true;
}

/* Les écritures ratées sous LOG ALL sont dites par `* lost <n>` avant la ligne suivante, sous la génération
 * où elles ont été comptées : une perte d'avant LOG OFF ne sort pas après son `ok` (bsk_journal.h). */
static size_t lost_line(char *buf, size_t cap, uint8_t *gen)
{
    char m[24];
    uint32_t v = atomic_exchange_explicit(&j.lost_unsaid, 0, memory_order_acq_rel);
    int n;
    if (!(v & LOST_N)) return 0;
    n = snprintf(m, sizeof m, "* lost %lu", (unsigned long)(v & LOST_N));
    *gen = (uint8_t)(v >> 24);
    if ((size_t)n > cap) n = (int)cap;
    memcpy(buf, m, (size_t)n);
    return (size_t)n;
}

size_t bsk_journal_take(char *buf, size_t cap, uint8_t *gen)
{
    uint32_t t = atomic_load_explicit(&j.tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&j.head, memory_order_acquire);
    size_t n = lost_line(buf, cap, gen);
    if (n) return n;
    if (h == t) return 0;
    n = j.ring[t];
    t = (t + 1) % RING;
    n |= (size_t)j.ring[t] << 8;
    t = (t + 1) % RING;
    *gen = j.ring[t];
    t = (t + 1) % RING;
    for (size_t i = 0; i < n; i++, t = (t + 1) % RING)
        if (i < cap) buf[i] = (char)j.ring[t];
    atomic_store_explicit(&j.tail, t, memory_order_release);
    return n > cap ? cap : n;
}

uint8_t bsk_journal_gen(void) { return atomic_load_explicit(&j.gen, memory_order_acquire); }

/* La génération est lue avant LOG ALL : LOG OFF éteint LOG ALL avant de changer de génération (bsk_journal_set), donc une
 * perte comptée sous la nouvelle génération ne l'est plus sous LOG ALL, et une perte comptée sous l'ancienne est périmée. */
void bsk_journal_lost(void)
{
    uint8_t g = atomic_load_explicit(&j.gen, memory_order_seq_cst);
    uint32_t v, w;
    atomic_fetch_add_explicit(&j.lost, 1, memory_order_relaxed);
    if (!atomic_load_explicit(&j.all, memory_order_seq_cst)) return;
    v = atomic_load_explicit(&j.lost_unsaid, memory_order_relaxed);
    do {
        uint32_t k = (uint8_t)(v >> 24) == g ? v & LOST_N : 0;
        w = (uint32_t)g << 24 | (k < LOST_N ? k + 1 : k);
    } while (!atomic_compare_exchange_weak_explicit(&j.lost_unsaid, &v, w, memory_order_acq_rel, memory_order_relaxed));
}

uint32_t bsk_journal_dropped(void) { return j.dropped + atomic_load_explicit(&j.lost, memory_order_relaxed); }

/* ─────────────────────────── le plafond ─────────────────────────── */

/* Le plafond vaut pour toutes les lignes, une ligne tue est comptée dans `dropped`, et une trame est admise entière ou
 * pas du tout : jamais un morceau sans les autres (l'anneau plein peut en perdre une partie : hex_lines le dit).
 * Hors du plafond, ni admises par lui ni comptées dans sa fenêtre (l'anneau plein les perd quand même, comptées) :
 *   - `* xdetect absent|present` : le démarrage du 135 écrit déjà 29 lignes dans sa première seconde, et l'insertion
 *     confirmée tombe avec `* session powering` : sous le plafond, elle ferait tomber `* session ready`. Au plus 2 lignes
 *     par 300 ms ; une retombée n'est jamais tue ;
 *   - `* ring … gesture` : sous LOG ALL, les trames remplissent la fenêtre à chaque seconde ; une ligne par geste ;
 *   - `* std … step=08` : une par init, dans la première seconde du démarrage ;
 *   - toute trame sous LOG ALL : à 60 Hz, au moins 300 lignes par seconde, dont le plafond tuerait les trois quarts ; la
 *     forme différence les tient à quelques Ko par seconde.
 * Les autres lignes et les erreurs de réception le gardent. */

/* `lines` lignes à l'instant `t_us`, admises ensemble ou pas du tout ; `rx` : une trame reçue (ou une
 * erreur de réception), que `* rxflood` compte si elle est tue. */
static bool admit(uint32_t lines, bool rx, uint64_t t_us)
{
    uint64_t ms = t_us / 1000;
    if (!j.win || ms >= j.win_ms + 1000) {      /* nouvelle fenêtre, résumé de la précédente */
        uint32_t supp = j.win ? j.win_rx : 0;
        j.win = true;
        j.win_ms = ms;
        j.win_n = j.win_rx = 0;
        if (supp) {
            char b[40];
            int n = snprintf(b, sizeof b, "* rxflood %lu", (unsigned long)supp);
            j.win_n = 1;
            put(b, (size_t)n);
        }
    }
    if (j.win_n + lines > PER_S) {
        j.dropped += lines;
        if (rx) j.win_rx++;
        return false;
    }
    j.win_n += lines;
    return true;
}

/* Chaque format de ce fichier, ses champs à leur plus long, tient dans LINE_MAX : le plus long, `* restore … via=`, fait
 * moins de 130 octets. */
static void line(bool rx, uint64_t t_us, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void line(bool rx, uint64_t t_us, const char *fmt, ...)
{
    char b[LINE_MAX];
    va_list ap;
    int n;
    if (!j.on || !admit(1, rx, t_us)) return;
    va_start(ap, fmt);
    n = vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    put(b, (size_t)n);
}

/* ─────────────────────────── les trames ─────────────────────────── */

/* Périodique ne dit pas seulement le type : un 0x04 qui porte une consigne 0x1D et un 0x06 suivi d'un accusé ne sont pas
 * tus sous LOG ON. Un goto part accroché au 0x04 de la boucle et l'objectif l'accuse derrière son 0x06 : sans cela, un
 * goto ne laisserait aucune ligne `* tx` ni `* rx` sous LOG ON. */
static bool periodic_rx(const bsk_frame_t *f)
{
    uint8_t t = f->msg[0];
    return t == 0x02 || t == 0x05 || (t == 0x06 && f->len <= LEN06);
}

static bool periodic_tx(const bsk_frame_t *f)
{
    uint8_t t = f->msg[0];
    return t == 0x03 || (t == 0x04 && f->len <= LEN04);
}

/* `n` octets (n ≥ 1) en hexa après `pre`, en morceaux de 64 « +<off>/<n>#<id> » au-delà, admis ensemble ou pas du tout
 * par le plafond. L'anneau, lui, peut se remplir entre deux morceaux, le dernier, plus court, tenant parfois là où le
 * premier ne tient pas : une ligne écrite en partie. Sous LOG ALL, put() dit déjà chaque perte. Sous LOG ON, la même
 * chose pour cette ligne seule : `* lost <n>`, ses morceaux perdus, avant le morceau suivant qui tient ou, s'il n'y en
 * a plus, avant la ligne suivante qui tient. Perdue entière, rien n'est dit, comme une ligne d'un seul morceau. Chaque
 * morceau perdu est compté dans `dropped` (put).
 * Un morceau tient dans LINE_MAX : PRE_FITS, vérifié à chaque tampon de préfixe. */
static void hex_lines(const char *pre, bool rx, const uint8_t *b, size_t n, uint64_t t_us)
{
    uint32_t id = ++j.frames, before = 0;          /* sous LOG ON : les morceaux perdus avant le premier écrit */
    bool out = false;                              /* un morceau écrit */
    if (!admit((uint32_t)((n + CHUNK - 1) / CHUNK), rx, t_us)) return;
    for (size_t off = 0; off < n; off += CHUNK) {
        char s[LINE_MAX];
        size_t k = off + CHUNK < n ? CHUNK : n - off;
        int w = n > CHUNK ? snprintf(s, sizeof s, "%s +%u/%u#%lu", pre, (unsigned)off, (unsigned)n, (unsigned long)id)
                          : snprintf(s, sizeof s, "%s", pre);
        for (size_t i = 0; i < k; i++) w += snprintf(s + w, sizeof s - (size_t)w, " %02X", b[off + i]);
        if (j.all) {
            put(s, (size_t)w);
            continue;
        }
        j.unsaid += before;                        /* dits devant ce morceau, s'il tient */
        if (put(s, (size_t)w)) {
            out = true;
            before = 0;
        } else {
            j.unsaid -= before;
            if (out) j.unsaid++;
            else before++;
        }
    }
}

/* ─────────────────────────── LOG ALL : la forme différence ─────────────────────────── */

static ref_t *ref_of(bool rx, uint8_t type)
{
    for (size_t i = 0; i < N_REF; i++)
        if (KEYS[i].rx == rx && KEYS[i].type == type) return &j.ref[i];
    return NULL;
}

static void forget(void)
{
    for (size_t i = 0; i < N_REF; i++) j.ref[i].have = false;
}

/* La ligne `<pre> ~<tt>[ s<hh>][ <off>=<hex>]…` de `f` contre sa référence `r` (même classe, même longueur) dans `s` ;
 * faux si elle ne tient pas dans une ligne : la trame est alors écrite entière. */
static bool diff_line(char *s, size_t cap, const char *pre, const ref_t *r, const bsk_frame_t *f)
{
    size_t w = (size_t)snprintf(s, cap, "%s ~%02X", pre, f->msg[0]);
    if (f->seq != (uint8_t)(r->seq + r->step)) w += (size_t)snprintf(s + w, cap - w, " s%02X", f->seq);
    for (size_t i = 1; i < f->len; i++) {
        if (f->msg[i] == r->msg[i]) continue;
        if (w + 8 >= cap) return false;                            /* « <off>= » et un octet tiennent encore */
        if (i == 1 || f->msg[i - 1] == r->msg[i - 1]) w += (size_t)snprintf(s + w, cap - w, " %u=", (unsigned)(i - 1));
        w += (size_t)snprintf(s + w, cap - w, "%02X", f->msg[i]);
    }
    return true;
}

/* Sous LOG ALL : toute trame, hors du plafond et pas comptée dans sa fenêtre ; en différence contre la précédente de
 * son sens et de son type quand il y en a une (même classe, même longueur), entière sinon. Les références sont oubliées
 * à LOG ALL (bsk_journal_set), après toute ligne perdue ou tue (dropped a changé : la différence suivante ne se
 * reconstruirait pas) et après un 0x0A (la mise en page du 0x05 et du 0x06 change). */
static void frame_all(const char *pre, bool rx, const bsk_frame_t *f, const uint8_t *b, size_t n)
{
    char s[LINE_MAX];
    ref_t *r = ref_of(rx, f->msg[0]);
    uint32_t d = bsk_journal_dropped();
    if (d != j.seen) {
        forget();
        j.seen = d;
    }
    j.frames++;
    if (r && r->have && r->cls == f->cls && r->len == f->len && diff_line(s, sizeof s, pre, r, f)) {
        put(s, strlen(s));
        r->step = (uint8_t)(f->seq - r->seq);                      /* inchangé quand le numéro est omis */
    } else {
        for (size_t off = 0; off < n; off += CHUNK) {
            size_t k = off + CHUNK < n ? CHUNK : n - off;
            int w = n > CHUNK ? snprintf(s, sizeof s, "%s +%u/%u#%lu", pre, (unsigned)off, (unsigned)n, (unsigned long)j.frames)
                              : snprintf(s, sizeof s, "%s", pre);
            for (size_t i = 0; i < k; i++) w += snprintf(s + w, sizeof s - (size_t)w, " %02X", b[off + i]);
            put(s, (size_t)w);
        }
        if (r) *r = (ref_t){.have = true, .cls = f->cls, .len = f->len};   /* le pas d'une référence : 0 */
    }
    if (r) {
        r->seq = f->seq;
        memcpy(r->msg, f->msg, f->len);
    }
    if (f->msg[0] == 0x0A) forget();
}

/* La trame est réencodée (fr_encode) depuis ce que PHY rend : PHY ne remonte que des trames valides, et le codec, le même
 * des deux côtés, rend exactement les octets passés sur le fil. */
static void frame(const char *dir, bool rx, const bsk_frame_t *f, uint64_t t_us)
{
    uint8_t b[BSK_FRAME_MAX];
    char pre[40];
    size_t n = fr_encode(f->cls, f->seq, f->msg, f->len, b, sizeof b);
    _Static_assert(PRE_FITS(sizeof pre), "le préfixe d'une trame, le repère et 64 octets tiennent dans LINE_MAX");
    snprintf(pre, sizeof pre, "* %s %llu", dir, (unsigned long long)(t_us / 1000));
    if (j.all) frame_all(pre, rx, f, b, n);
    else hex_lines(pre, rx, b, n, t_us);
}

void bsk_journal_rx(const bsk_frame_t *f, uint64_t t_us)
{
    if (j.on && (j.all || !periodic_rx(f))) frame("rx", true, f, t_us);
}

void bsk_journal_tx(const bsk_frame_t *f)
{
    if (j.on && (j.all || !periodic_tx(f))) frame("tx", false, f, now_us());
}

void bsk_journal_error(bsk_err_t e, const bsk_phy_raw_t *raw, uint64_t t_us)
{
    char pre[96];      /* au plus 60 caractères : « * rx <17 chiffres> err=home_failed n=<5> omitted=<5> » */
    int w;
    _Static_assert(PRE_FITS(sizeof pre), "le préfixe d'une erreur, le repère et 64 octets tiennent dans LINE_MAX");
    if (!raw->len) {
        line(true, t_us, "* rx %llu err=%s", (unsigned long long)(t_us / 1000), bsk_err_token(e));
        return;
    }
    if (!j.on) return;
    w = snprintf(pre, sizeof pre, "* rx %llu err=%s n=%u", (unsigned long long)(t_us / 1000), bsk_err_token(e),
                 (unsigned)raw->n);
    if (raw->n > raw->len)
        (void)snprintf(pre + w, sizeof pre - (size_t)w, " omitted=%u", (unsigned)(raw->n - raw->len));
    hex_lines(pre, true, raw->b, raw->len, t_us);
}

/* Sous le plafond, sous LOG ON comme sous LOG ALL : ce n'est pas une trame passée sur le fil, et une émission refusée à
 * chaque paire en écrirait 60 par seconde. Une ligne perdue reste comptée dans `refused` de `DEBUG` (bench_core). */
void bsk_journal_refused(const bsk_frame_t *f)
{
    uint8_t b[BSK_FRAME_MAX];
    char pre[40];
    uint64_t t = now_us();
    size_t n;
    _Static_assert(PRE_FITS(sizeof pre), "le préfixe d'un refus, le repère et 64 octets tiennent dans LINE_MAX");
    if (!j.on) return;
    n = fr_encode(f->cls, f->seq, f->msg, f->len, b, sizeof b);
    snprintf(pre, sizeof pre, "* refused %llu", (unsigned long long)(t / 1000));
    hex_lines(pre, false, b, n, t);
}

void bsk_journal_session(uint8_t state, bsk_err_t last_error)
{
    uint64_t t = now_us();
    if (state == SESSION_FAULT)
        line(false, t, "* session %llu %s %s", (unsigned long long)(t / 1000), bsk_state_name(state), bsk_err_token(last_error));
    else
        line(false, t, "* session %llu %s", (unsigned long long)(t / 1000), bsk_state_name(state));
}

void bsk_journal_handshake(bsk_hs_t r, uint64_t up_us)
{
    static const char *const n[] = {"ok", "LENS_CS_never_high", "LENS_CS_stuck_high"};
    uint64_t t = now_us();
    if (r == BSK_HS_NEVER_HIGH)
        line(false, t, "* std %llu step=handshake result=%s", (unsigned long long)(t / 1000), n[r]);
    else
        line(false, t, "* std %llu step=handshake result=%s lens_cs_up_us=%llu", (unsigned long long)(t / 1000), n[r],
             (unsigned long long)up_us);
}

void bsk_journal_motion(uint8_t state)
{
    uint64_t t = now_us();
    line(false, t, "* motion %llu %s", (unsigned long long)(t / 1000), bsk_motion_name(state));
}

/* La valeur brute : le sens des offsets 64 et 66 n'est pas établi. */
void bsk_journal_lens(uint8_t off, uint8_t v)
{
    uint64_t t = now_us();
    line(false, t, "* lens %llu 0x05[%u]=%02X", (unsigned long long)(t / 1000), (unsigned)off, (unsigned)v);
}

void bsk_journal_ring(bool aperture)
{
    uint64_t t = now_us();
    line(false, t, "* ring %llu mode=%s source=0x05", (unsigned long long)(t / 1000), aperture ? "aperture" : "focus");
}

void bsk_journal_btn(bsk_btn_t press, bsk_btn_result_t r, const char *why)
{
    static const char *const p[] = {"short", "long", "held"};
    static const char *const n[] = {"ok", "er", "ignore"};
    uint64_t t = now_us();
    line(false, t, "* btn %llu press=%s result=%s%s%s", (unsigned long long)(t / 1000), p[press], n[r], why ? " why=" : "",
         why ? why : "");
}

void bsk_journal_mark(bsk_mark_ev_t ev, const char *key, const bsk_mark_t *m)
{
    static const char *const o[] = {"set", "clear", "store=er"};
    static const char *const d[] = {"unknown", "inc", "dec"};
    uint64_t t = now_us();
    if (m)
        line(false, t, "* mark %llu %s key=%s pos=%ld dir=%s", (unsigned long long)(t / 1000), o[ev], key, (long)m->position,
             d[m->approach_dir]);   /* 0 à 2 : mark.c n'en tient pas d'autre (decode) */
    else
        line(false, t, "* mark %llu %s key=%s", (unsigned long long)(t / 1000), o[ev], key);
}

/* approach_dir de 0 à 2, comme `* mark` : mark.c n'en tient pas d'autre (decode). */
void bsk_journal_restore(const bsk_mark_t *m, int32_t x, int32_t via)
{
    static const char *const DIR[] = {"unknown", "inc", "dec"};
    uint64_t t = now_us();
    if (via < 0)
        line(false, t, "* restore %llu mark=%ld dir=%s x=%ld path=direct", (unsigned long long)(t / 1000), (long)m->position,
             DIR[m->approach_dir], (long)x);
    else
        line(false, t, "* restore %llu mark=%ld dir=%s x=%ld path=overshoot via=%ld", (unsigned long long)(t / 1000),
             (long)m->position, DIR[m->approach_dir], (long)x, (long)via);
}

void bsk_journal_restore_skip(const char *why, const bsk_mark_t *m)
{
    uint64_t t = now_us();
    if (m) line(false, t, "* restore %llu skip=%s mark=%ld", (unsigned long long)(t / 1000), why, (long)m->position);
    else line(false, t, "* restore %llu skip=%s", (unsigned long long)(t / 1000), why);
}

void bsk_journal_restore_end(const char *why)
{
    uint64_t t = now_us();
    line(false, t, "* restore %llu end=%s", (unsigned long long)(t / 1000), why);
}

/* 106 caractères au plus, tous les champs à leur plus long. Hors du plafond (voir l'en-tête). */
void bsk_journal_gesture(int32_t pulses, uint32_t thirds, uint32_t frames, uint32_t capped)
{
    char b[112];
    uint64_t t = now_us();
    if (j.on) {
        int n = snprintf(b, sizeof b, "* ring %llu gesture pulses=%ld thirds=%lu frames=%lu capped=%lu",
                         (unsigned long long)(t / 1000), (long)pulses, (unsigned long)thirds, (unsigned long)frames,
                         (unsigned long)capped);
        put(b, (size_t)n);
    }
}

/* Hors du plafond (voir l'en-tête). */
void bsk_journal_body08(uint8_t flags, bool samyang)
{
    char b[80];
    uint64_t t = now_us();
    if (j.on) {
        int n = snprintf(b, sizeof b, "* std %llu step=08 body_flags=%02X samyang=%s", (unsigned long long)(t / 1000),
                         (unsigned)flags, samyang ? "yes" : "no");
        put(b, (size_t)n);
    }
}

/* L'instant de l'événement de PHY, pas l'horloge, comme `* rx` : pour une retombée, celui de la coupure. Hors du
 * plafond (voir l'en-tête). */
void bsk_journal_xdetect(bool present, uint64_t t_us)
{
    char b[48];
    int n = snprintf(b, sizeof b, "* xdetect %llu %s", (unsigned long long)(t_us / 1000), present ? "present" : "absent");
    if (j.on) put(b, (size_t)n);
}

/* Sous le plafond, contrairement à `* xdetect absent|present` : un contact qui rebondit en écrirait une toutes les 2 ms. */
void bsk_journal_xdetect_bounce(uint64_t t_us)
{
    line(false, t_us, "* xdetect %llu bounce", (unsigned long long)(t_us / 1000));
}

void bsk_journal_resend(uint8_t type, uint8_t n, bsk_err_t why)
{
    uint64_t t = now_us();
    line(false, t, "* resend %llu msg=%02X n=%u why=%s", (unsigned long long)(t / 1000), (unsigned)type, (unsigned)n,
         bsk_err_token(why));
}

void bsk_journal_giveup(uint8_t type, bsk_err_t why)
{
    uint64_t t = now_us();
    line(false, t, "* resend %llu msg=%02X giveup why=%s", (unsigned long long)(t / 1000), (unsigned)type, bsk_err_token(why));
}

/* ─────────────────────────── réglages ─────────────────────────── */

void bsk_journal_init(uint64_t (*clock_us)(void)) { j.clock = clock_us; }

/* LOG ALL éteint avant que la génération change (bsk_journal_lost) ; chaque LOG ALL repart des trames
 * entières. */
void bsk_journal_set(bool on, bool all)
{
    j.all = on && all;
    if (!on) atomic_fetch_add_explicit(&j.gen, 1, memory_order_release);
    j.on = on;
    j.win = false;
    j.unsaid = 0;
    forget();
}

bool bsk_journal_on(void) { return j.on; }
bool bsk_journal_all(void) { return j.all; }

/* ─────────────────────────── les noms ─────────────────────────── */

const char *bsk_state_name(uint8_t state)
{
    static const char *const n[] = {"off", "powering", "identifying", "homing", "restoring", "ready", "recovering", "fault"};
    _Static_assert(sizeof n / sizeof n[0] == SESSION_FAULT + 1, "un nom par état de SESSION");
    return n[state];
}

const char *bsk_motion_name(uint8_t state)
{
    static const char *const n[] = {"idle", "planning", "commanded", "moving", "settling", "arrived", "stalled", "aborted"};
    _Static_assert(sizeof n / sizeof n[0] == MOTION_ABORTED + 1, "un nom par état de MOUVEMENT");
    return n[state];
}

const char *bsk_err_token(bsk_err_t e)
{
    static const char *const n[] = {"ok",       "timeout",     "framing",  "rejected",  "bus",  "stall",   "limit",
                                    "home_failed", "identity", "forbidden", "busy", "lost", "aborted", "nocap"};
    _Static_assert(sizeof n / sizeof n[0] == E_NOCAP + 1, "un jeton par erreur");
    return n[e];
}
