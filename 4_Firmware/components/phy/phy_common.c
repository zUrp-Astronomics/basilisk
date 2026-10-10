/* SOURCE: spec de l'atelier § 1 (R1) et § 2 — le code commun aux deux PHY
 * AUTHOR: engineer
 * DATE: 2026-10-03
 * STATUS: actif — C pur, sans ESP-IDF : compilé pour la carte et par les tests hôte
 *
 * Le codec de trame, la réception en flux et la présence de D2 (voir phy_common.h). */
#include "phy_common.h"

#include <string.h>

uint16_t fr_sum(const uint8_t *frame, size_t len)
{
    uint16_t s = 0;
    for (size_t i = 1; i + 3 < len; i++) s = (uint16_t)(s + frame[i]);
    return s;
}

size_t fr_encode(uint8_t cls, uint8_t seq, const uint8_t *msg, size_t n, uint8_t *out, size_t cap)
{
    size_t len = n + BSK_FRAME_OVERHEAD;
    uint16_t s;
    if (len > cap) return 0;
    out[0] = 0xF0;
    out[1] = (uint8_t)len;
    out[2] = (uint8_t)(len >> 8);
    out[3] = cls;
    out[4] = seq;
    memcpy(out + 5, msg, n);
    s = fr_sum(out, len);
    out[len - 3] = (uint8_t)s;
    out[len - 2] = (uint8_t)(s >> 8);
    out[len - 1] = 0x55;
    return len;
}

bsk_err_t fr_decode(const uint8_t *b, size_t n, bsk_frame_t *f)
{
    size_t len;
    uint16_t s;
    if (n < BSK_FRAME_OVERHEAD + 1 || n > BSK_FRAME_MAX || b[0] != 0xF0) return E_FRAMING;
    len = (size_t)b[1] | (size_t)b[2] << 8;
    if (len != n || b[3] < 1 || b[3] > 3 || b[n - 1] != 0x55) return E_FRAMING;
    s = fr_sum(b, n);
    if (b[n - 3] != (uint8_t)s || b[n - 2] != (uint8_t)(s >> 8)) return E_FRAMING;
    f->cls = b[3];
    f->seq = b[4];
    f->len = (uint16_t)(n - BSK_FRAME_OVERHEAD);
    memcpy(f->msg, b + 5, f->len);
    return E_OK;
}

fr_scan_t fr_scan(const uint8_t *b, size_t n, bsk_frame_t *f, size_t *len)
{
    size_t l;
    if (b[0] != 0xF0) return FR_BAD;
    if (n < 3) return FR_SHORT;                         /* le champ de longueur n'est pas encore lisible */
    l = (size_t)b[1] | (size_t)b[2] << 8;
    if (l < BSK_FRAME_OVERHEAD + 1 || l > BSK_FRAME_MAX) return FR_BAD;
    if (l > n) return FR_SHORT;
    *len = l;
    return fr_decode(b, l, f) == E_OK ? FR_OK : FR_BAD;
}

/* `k` octets de `b` ajoutés au relevé `r` : comptés tous, recopiés tant qu'il a de la place. */
static void raw_add(bsk_phy_raw_t *r, const uint8_t *b, size_t k)
{
    size_t c = BSK_PHY_RAW_MAX - r->len;
    if (k < c) c = k;
    memcpy(r->b + r->len, b, c);
    r->len = (uint16_t)(r->len + c);
    r->n = (uint16_t)(r->n + k);
}

void phy_raw(bsk_phy_raw_t *r, const uint8_t *b, size_t n)
{
    r->n = r->len = 0;
    raw_add(r, b, n);
}

void rx_got(rx_t *r, size_t k, uint64_t t)
{
    r->n += k;
    r->last = t;
}

static void consume(rx_t *r, size_t k)
{
    memmove(r->b, r->b + k, r->n - k);
    r->n -= k;
}

bool rx_next(rx_t *r, uint64_t now, bsk_phy_event_t *e)
{
    bool stale = (r->n || r->junk.n) && now - r->last > RX_STALE_US;
    size_t pos = 0, len = 0;
    bool frame = false;
    while (pos < r->n && r->junk.n + pos < RX_CAP) {
        fr_scan_t st = fr_scan(r->b + pos, r->n - pos, &e->u.frame, &len);
        if (st == FR_OK) {
            frame = true;
            break;
        }
        if (st == FR_SHORT && !stale) break;           /* rien n'est jeté : la suite est attendue */
        pos++;                                          /* écarté, ou périmé */
    }
    raw_add(&r->junk, r->b, pos);                       /* hors du tampon : jamais rejugés */
    consume(r, pos);
    e->t_us = now;
    if (r->junk.n && (frame || stale || r->junk.n == RX_CAP)) {
        e->kind = BSK_PHY_ERROR;                        /* les octets écartés, avant la trame qui les suit */
        e->u.err = E_FRAMING;
        e->u.raw = r->junk;
        r->junk.n = r->junk.len = 0;
        return true;
    }
    if (!frame) return false;
    e->kind = BSK_PHY_FRAME;
    consume(r, len);
    return true;
}

uint64_t rx_deadline(const rx_t *r) { return r->n || r->junk.n ? r->last + RX_STALE_US + 1 : UINT64_MAX; }

void rx_flush(rx_t *r, bsk_phy_raw_t *raw)
{
    *raw = r->junk;
    raw_add(raw, r->b, r->n);
    r->junk.n = r->junk.len = 0;
    r->n = 0;
}

bool pres_sample(pres_t *p, bool present, uint64_t t)
{
    if (present != p->raw) {
        p->raw = present;
        p->since = t;
    }
    if (p->present || !p->raw || t - p->since < D2_DEBOUNCE_US) return false;
    p->present = true;
    p->ins = true;
    p->ins_t = t;
    return true;
}

uint64_t pres_deadline(const pres_t *p) { return p->raw && !p->present ? p->since + D2_DEBOUNCE_US : UINT64_MAX; }

bool pres_next(pres_t *p, uint64_t before, bsk_phy_event_t *e)
{
    bool drop = p->drop;                             /* tenue avec une insertion, la retombée la précède */
    if (drop ? p->drop_t > before : !p->ins || p->ins_t > before) return false;
    e->kind = BSK_PHY_PRESENCE;
    e->t_us = drop ? p->drop_t : p->ins_t;
    e->u.present = !drop;
    if (drop) p->drop = false;
    else p->ins = false;
    return true;
}
