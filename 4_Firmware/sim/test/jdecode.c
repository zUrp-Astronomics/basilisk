/* SOURCE: PROTOCOL.md (LOG ALL) ; le décodeur de test des lignes `* rx` et `* tx` (voir jdecode.h)
 * AUTHOR: engineer
 * DATE: 2026-10-02
 * STATUS: actif — compilé dans les tests du journal (sim/test/programmes.sh), jamais pour la carte
 * Le décodeur de test des lignes `* rx` et `* tx` : la trame reconstruite d'après PROTOCOL.md (LOG ALL). Du
 * firmware, il n'utilise que fr_decode (components/phy/phy_common.c), pour vérifier une trame entière comme la page
 * vérifie sa somme. */
#include "jdecode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsk_journal.h"
#include "phy_common.h"

typedef struct {
    bool     have;
    uint8_t  cls, seq, step;
    uint16_t len;
    uint8_t  msg[BSK_MSG_MAX];
} jref_t;

static jref_t R[2][256];               /* [rx][type] */

#define N_ASM 4
static struct {
    bool     used, rx;
    unsigned id, len, got;
    uint64_t ms;
    uint8_t  b[BSK_FRAME_MAX];
    bool     have[BSK_FRAME_MAX];
} A[N_ASM];

void jd_reset(void)
{
    memset(R, 0, sizeof R);
    memset(A, 0, sizeof A);
}

static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Deux chiffres hexa majuscules en `p` ; -1 sinon. */
static int byte_at(const char *p)
{
    int h = hexv(p[0]), l = h < 0 ? -1 : hexv(p[1]);
    return l < 0 ? -1 : h << 4 | l;
}

/* « XX XX … » jusqu'à la fin : les octets dans b (cap au plus) ; -1 si mal formé. */
static int hex_list(const char *p, uint8_t *b, size_t cap)
{
    size_t n = 0;
    while (*p) {
        int v;
        if (*p != ' ' || n == cap || (v = byte_at(p + 1)) < 0) return -1;
        b[n++] = (uint8_t)v;
        p += 3;
    }
    return (int)n;
}

static jd_rc_t whole(bool rx, uint64_t ms, const uint8_t *b, size_t n, jd_frame_t *out, const char **why)
{
    jref_t *r;
    if (fr_decode(b, n, &out->f) != E_OK) {
        *why = "trame entière invalide (longueur, classe, somme ou 55)";
        return JD_BAD;
    }
    out->rx = rx;
    out->ms = ms;
    out->known = true;
    out->diff = false;
    r = &R[rx][out->f.msg[0]];
    r->have = true;
    r->cls = out->f.cls;
    r->seq = out->f.seq;
    r->step = 0;
    r->len = out->f.len;
    memcpy(r->msg, out->f.msg, out->f.len);
    return JD_FRAME;
}

/* « ~<tt>[ s<hh>][ <o>=<hex>]… » après l'instant. */
static jd_rc_t diff(bool rx, uint64_t ms, const char *p, jd_frame_t *out, const char **why)
{
    int t = byte_at(p + 1);
    jref_t *r;
    bool seq_given = false;
    uint8_t seq = 0;
    if (t < 0) {
        *why = "type illisible après ~";
        return JD_BAD;
    }
    p += 3;
    memset(out, 0, sizeof *out);
    out->rx = rx;
    out->ms = ms;
    out->diff = true;
    out->f.msg[0] = (uint8_t)t;
    r = &R[rx][t];
    if (!strncmp(p, " s", 2)) {
        int v = byte_at(p + 2);
        if (v < 0) {
            *why = "numéro de séquence illisible";
            return JD_BAD;
        }
        seq_given = true;
        seq = (uint8_t)v;
        p += 4;
    }
    if (!r->have) {
        out->known = false;
        return JD_FRAME;
    }
    out->known = true;
    out->f.cls = r->cls;
    out->f.len = r->len;
    memcpy(out->f.msg, r->msg, r->len);
    if (seq_given) r->step = (uint8_t)(seq - r->seq);
    else seq = (uint8_t)(r->seq + r->step);
    out->f.seq = seq;
    while (*p) {
        char *e;
        unsigned long off;
        if (*p != ' ' || p[1] < '0' || p[1] > '9') {
            *why = "suite illisible";
            return JD_BAD;
        }
        off = strtoul(p + 1, &e, 10);
        if (*e != '=' || byte_at(e + 1) < 0) {
            *why = "<off>=<hex> attendu";
            return JD_BAD;
        }
        p = e + 1;
        for (unsigned long i = off + 1; byte_at(p) >= 0; i++, p += 2) {
            if (i >= r->len) {
                *why = "octet hors de la trame de référence";
                return JD_BAD;
            }
            out->f.msg[i] = (uint8_t)byte_at(p);
        }
    }
    r->seq = seq;
    memcpy(r->msg, out->f.msg, r->len);
    return JD_FRAME;
}

static jd_rc_t chunk(bool rx, uint64_t ms, const char *p, jd_frame_t *out, const char **why)
{
    char *e;
    unsigned off = (unsigned)strtoul(p + 1, &e, 10), len, id, k;
    uint8_t b[BSK_FRAME_MAX];
    int n;
    size_t a;
    if (*e != '/') goto bad;
    len = (unsigned)strtoul(e + 1, &e, 10);
    if (*e != '#') goto bad;
    id = (unsigned)strtoul(e + 1, &e, 10);
    if (len > BSK_FRAME_MAX || (n = hex_list(e, b, sizeof b)) <= 0 || off + (unsigned)n > len) goto bad;
    for (a = 0; a < N_ASM && !(A[a].used && A[a].rx == rx && A[a].id == id); a++) {}
    if (a == N_ASM) {
        for (a = 0; a < N_ASM && A[a].used; a++) {}
        if (a == N_ASM) a = 0;
        memset(&A[a], 0, sizeof A[a]);
        A[a].used = true;
        A[a].rx = rx;
        A[a].id = id;
        A[a].len = len;
        A[a].ms = ms;
    }
    for (k = 0; k < (unsigned)n; k++)
        if (!A[a].have[off + k]) {
            A[a].have[off + k] = true;
            A[a].b[off + k] = b[k];
            A[a].got++;
        }
    if (A[a].got < A[a].len) return JD_NONE;
    A[a].used = false;
    return whole(rx, A[a].ms, A[a].b, A[a].len, out, why);
bad:
    *why = "morceau mal formé";
    return JD_BAD;
}

jd_rc_t jd_line(const char *line, jd_frame_t *out, const char **why)
{
    bool rx;
    char *e;
    uint64_t ms;
    *why = "";
    if (!strncmp(line, "* lost ", 7)) {
        jd_reset();
        return JD_NONE;
    }
    if (strncmp(line, "* rx ", 5) && strncmp(line, "* tx ", 5)) return JD_NONE;
    rx = line[2] == 'r';
    ms = strtoull(line + 5, &e, 10);
    if (e == line + 5 || *e != ' ') {
        *why = "instant illisible";
        return JD_BAD;
    }
    if (!strncmp(e + 1, "err=", 4)) return JD_NONE;
    if (e[1] == '~') return diff(rx, ms, e + 1, out, why);
    if (e[1] == '+') return chunk(rx, ms, e + 1, out, why);
    {
        uint8_t b[BSK_FRAME_MAX];
        int n = hex_list(e, b, sizeof b);
        if (n <= 0) {
            *why = "octets illisibles";
            return JD_BAD;
        }
        return whole(rx, ms, b, (size_t)n, out, why);
    }
}

/* ─────────────────────────── le vidage mesuré ─────────────────────────── */

void jd_drain(jd_meter_t *m, uint64_t now_us, void (*cb)(const jd_frame_t *d, void *ctx), void *ctx)
{
    char b[640];
    uint8_t g;
    size_t n, ring = 0;
    uint32_t here = 0;
    while ((n = bsk_journal_take(b, sizeof b - 1, &g)) > 0) {
        jd_frame_t d;
        const char *why;
        jd_rc_t rc;
        b[n] = 0;
        ring += n + 3;
        if (g != bsk_journal_gen()) continue;                  /* périmée par LOG OFF : la tâche ne l'écrit pas */
        here += (uint32_t)n + 1;
        m->lines++;
        m->lost += !strncmp(b, "* lost ", 7);
        m->rxflood += !strncmp(b, "* rxflood ", 10);
        rc = jd_line(b, &d, &why);
        if (rc == JD_BAD) {
            if (!m->bad++) printf("    ligne illisible (%s) : %s\n", why, b);
        } else if (rc == JD_FRAME) {
            m->frames++;
            m->unknown += !d.known;
            if (cb) cb(&d, ctx);
        }
    }
    m->bytes += here;
    if ((now_us - m->t0_us) / 1000000u < sizeof m->sec / sizeof m->sec[0]) m->sec[(now_us - m->t0_us) / 1000000u] += here;
    if (m->n_drains < JD_DRAINS) m->per[m->n_drains] = here;
    m->n_drains++;
    if (ring > m->ring_max) m->ring_max = ring;
}

uint32_t jd_worst_second(const jd_meter_t *m, size_t secs)
{
    uint32_t w = 0;
    for (size_t i = 0; i < secs && i < sizeof m->sec / sizeof m->sec[0]; i++)
        if (m->sec[i] > w) w = m->sec[i];
    return w;
}

uint32_t jd_worst_window(const jd_meter_t *m, size_t k)
{
    uint32_t w = 0;
    size_t n = m->n_drains < JD_DRAINS ? m->n_drains : JD_DRAINS;
    for (size_t i = 0; i + k <= n; i++) {
        uint32_t s = 0;
        for (size_t j = i; j < i + k; j++) s += m->per[j];
        if (s > w) w = s;
    }
    return w;
}
