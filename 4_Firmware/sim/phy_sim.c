/* SOURCE: spec de l'atelier § 2 et § 11 — la frontière PHY simulée (voir phy_sim.h)
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — compilé par 4_Firmware/run.sh dans les bancs qui jouent le fil
 * Le fil entre la carte et le faux objectif (lens_sim.h), la réception et la présence de la carte (rx_t, pres_t de
 * components/phy/phy_common.c), les lignes et les rails notés (phy_sim_wires), les fautes injectées. */
#include "phy_sim.h"

#include <string.h>

#include "phy_common.h"

#define GUARD_US 40u            /* la garde de BODY_CS de la carte, de chaque côté */
#define BODY_CAP 8192u
#define EV_CAP 1024u
#define INJ_CAP 8192u           /* émissions injectées de l'objectif, octets et fronts de LENS_CS */

typedef struct { uint64_t t; bool is_cs; uint8_t v; } body_out_t;

static struct {
    lens_sim_t lens;
    uint64_t   now;
    body_out_t body[BODY_CAP];
    uint32_t   body_head, body_n;
    uint64_t   body_free;       /* fin de la dernière émission programmée */
    uint64_t   vd_period, vd_next;
    bsk_phy_event_t ev[EV_CAP];
    uint32_t   ev_head, ev_n;
    lens_sim_out_t inj[INJ_CAP]; /* ce que phy_sim_lens_raw fait émettre à l'objectif */
    uint32_t   inj_head, inj_n;
    uint64_t   inj_free;        /* fin de la dernière émission injectée */
    rx_t       rx;              /* la réception, celle de la carte (components/phy/phy.c) */
    bool       send_bus_error;  /* le prochain bsk_phy_send rend E_BUS */
    phy_sim_fault_fn fault;     /* la trame perdue ou fausse, NULL : aucune */
    bool       lens_cs;
    bool       d2_pin;          /* niveau de D2 : true = présent */
    pres_t     pres;            /* la présence et le verrou, comme la carte */
    phy_sim_wires_t w;          /* noté, sans effet sur le fil (phy_sim.h) */
} g;

static void push_event(const bsk_phy_event_t *e)
{
    if (g.ev_n == EV_CAP) {     /* l'appelant ne lit pas ses événements : on perd le plus ancien */
        g.ev_head = (g.ev_head + 1) % EV_CAP;
        g.ev_n--;
    }
    g.ev[(g.ev_head + g.ev_n) % EV_CAP] = *e;
    g.ev_n++;
}

/* Ce que la réception a à rendre, jugé à l'instant `t`, comme drain de components/phy/phy.c. */
static void drain(uint64_t t)
{
    bsk_phy_event_t e = {.kind = BSK_PHY_FRAME};
    while (rx_next(&g.rx, t, &e)) {
        phy_sim_fate_t k = e.kind == BSK_PHY_FRAME && g.fault ? g.fault(false, &e.u.frame) : PHY_SIM_PASS;
        if (k == PHY_SIM_LOSE) continue;
        if (k == PHY_SIM_GARBLE) {              /* ses octets, écartés */
            uint8_t b[BSK_FRAME_MAX];
            size_t n = fr_encode(e.u.frame.cls, e.u.frame.seq, e.u.frame.msg, e.u.frame.len, b, sizeof b);
            e.kind = BSK_PHY_ERROR;
            e.u.err = E_FRAMING;
            phy_raw(&e.u.raw, b, n);
        }
        push_event(&e);
    }
}

/* Une sortie de l'objectif, faux objectif ou émission injectée. Un octet va au flux, jugé à son instant ; LENS_CS n'est
 * que publiée. Le tampon a toujours une place après drain (rx_next, phy_common.h). */
static void from_lens(const lens_sim_out_t *o)
{
    if (o->kind == LENS_SIM_BYTE) {
        g.rx.b[g.rx.n] = o->v;
        rx_got(&g.rx, 1, o->t);
        drain(o->t);
    } else {
        bsk_phy_event_t e = {.kind = BSK_PHY_LENS_CS, .t_us = o->t};
        bool high = o->v != 0;
        if (high == g.lens_cs) return;
        g.lens_cs = high;
        e.u.level = high;
        push_event(&e);
    }
}

static void drain_lens(void)
{
    lens_sim_out_t o;
    while (lens_sim_out(g.lens, &o)) from_lens(&o);
}

/* L'insertion confirmée est tenue dans g.pres (pres_next la rend), jamais dans la file. */
static void d2_report(void) { (void)pres_sample(&g.pres, g.d2_pin, g.now); }

/* L'échéance d'une retombée, UINT64_MAX s'il n'y en a aucune : celle de l'esp_timer de la carte. */
static uint64_t d2_due(void) { return g.pres.pend ? g.pres.pend_t + D2_DROP_US : UINT64_MAX; }

static void cut(void);

/* L'échéance, comme le rappel de la carte (drop_expire de components/phy/phy.c) : D2 relu, la coupure s'il est absent ;
 * sinon, le rebond dans la file, pour le journal. */
static void d2_expire(void)
{
    bsk_phy_event_t e = {.kind = BSK_PHY_BOUNCE, .t_us = g.now};
    if (pres_expire(&g.pres, !g.d2_pin, g.now)) cut();
    if (g.d2_pin) push_event(&e);
}

static void schedule(bool is_cs, uint8_t v, uint64_t t)
{
    if (g.body_n == BODY_CAP) return;
    g.body[(g.body_head + g.body_n) % BODY_CAP] = (body_out_t){t, is_cs, v};
    g.body_n++;
}

static void schedule_bytes(const uint8_t *b, size_t n)
{
    uint64_t t = g.body_free > g.now ? g.body_free : g.now;
    schedule(true, 1, t);
    t += GUARD_US;
    for (size_t i = 0; i < n; i++) {
        t += lens_sim_byte_us(g.lens);
        schedule(false, b[i], t);
    }
    t += GUARD_US;
    schedule(true, 0, t);
    g.body_free = t;
}

void phy_sim_init(lens_sim_t lens, uint64_t t0)
{
    memset(&g, 0, sizeof g);
    g.lens = lens;
    g.now = t0;
    g.body_free = t0;
}

uint64_t phy_sim_now(void) { return g.now; }

uint64_t phy_sim_next(void)
{
    uint64_t t = lens_sim_next_event(g.lens);
    if (g.body_n && g.body[g.body_head].t < t) t = g.body[g.body_head].t;
    if (g.inj_n && g.inj[g.inj_head].t < t) t = g.inj[g.inj_head].t;
    if (g.vd_period && g.vd_next < t) t = g.vd_next;
    if (rx_deadline(&g.rx) < t) t = rx_deadline(&g.rx);
    if (d2_due() < t) t = d2_due();                 /* l'échéance d'une retombée */
    if (g.pres.drop || g.pres.ins) t = g.now;       /* une présence tenue est à rendre maintenant */
    return t < g.now ? g.now : t;
}

void phy_sim_run(uint64_t t)
{
    for (;;) {
        uint64_t tb = g.body_n ? g.body[g.body_head].t : UINT64_MAX;
        uint64_t tv = g.vd_period ? g.vd_next : UINT64_MAX;
        uint64_t tl = lens_sim_next_event(g.lens);
        uint64_t td = pres_deadline(&g.pres);
        uint64_t ti = g.inj_n ? g.inj[g.inj_head].t : UINT64_MAX;
        uint64_t tr = rx_deadline(&g.rx);
        uint64_t tx = d2_due();
        uint64_t tn = tb < tv ? tb : tv;
        if (tl < tn) tn = tl;
        if (td < tn) tn = td;
        if (ti < tn) tn = ti;
        if (tr < tn) tn = tr;
        if (tx < tn) tn = tx;
        if (tn > t) break;
        if (tn > g.now) g.now = tn;
        if (tb == tn) {
            body_out_t o = g.body[g.body_head];
            g.body_head = (g.body_head + 1) % BODY_CAP;
            g.body_n--;
            if (o.is_cs) lens_sim_body_cs(g.lens, g.now, o.v != 0);
            else lens_sim_byte(g.lens, g.now, o.v);
        } else if (tv == tn) {
            g.vd_next += g.vd_period;
            phy_sim_vd_edge();
        } else if (tx == tn) {
            d2_expire();
        } else if (td == tn) {
            d2_report();
        } else if (ti == tn) {
            lens_sim_out_t o = g.inj[g.inj_head];
            g.inj_head = (g.inj_head + 1) % INJ_CAP;
            g.inj_n--;
            from_lens(&o);
        } else if (tr == tn) {
            drain(g.now);                           /* un début de trame périmé, des octets écartés à rapporter */
        } else {
            lens_sim_advance(g.lens, g.now);
        }
        drain_lens();
    }
    if (t > g.now) g.now = t;
    lens_sim_advance(g.lens, g.now);
    drain_lens();
}

void phy_sim_raw(const uint8_t *b, size_t n) { schedule_bytes(b, n); }

void phy_sim_raw_cs_low(const uint8_t *b, size_t n)
{
    uint64_t t = g.body_free > g.now ? g.body_free : g.now;
    for (size_t i = 0; i < n; i++) {
        t += lens_sim_byte_us(g.lens);
        schedule(false, b[i], t);
    }
    g.body_free = t;
}

void phy_sim_vd_edge(void)
{
    bsk_phy_event_t e = {.kind = BSK_PHY_VD, .t_us = g.now};
    lens_sim_vd(g.lens, g.now);
    push_event(&e);
    drain_lens();
}

/* Comme la carte (bus_error de components/phy/phy.c) : la réception vidée, l'erreur porte ce qu'elle n'avait pas encore
 * rapporté (rx_flush). */
void phy_sim_bus_error(void)
{
    bsk_phy_event_t e = {.kind = BSK_PHY_ERROR, .t_us = g.now};
    e.u.err = E_BUS;
    rx_flush(&g.rx, &e.u.raw);
    push_event(&e);
}

static void inject(uint64_t t, lens_sim_out_kind_t kind, uint8_t v)
{
    if (g.inj_n == INJ_CAP) return;
    g.inj[(g.inj_head + g.inj_n) % INJ_CAP] = (lens_sim_out_t){t, kind, v};
    g.inj_n++;
}

/* LENS_CS levée, les `n - late` premiers octets, LENS_CS retombée (après lens_cs_tail_us si late = 0, avec le dernier
 * de ces octets sinon), puis les `late` derniers. */
static void lens_raw(const uint8_t *b, size_t n, size_t late)
{
    uint64_t t = g.inj_free > g.now ? g.inj_free : g.now;
    inject(t, LENS_SIM_LENS_CS, 1);
    for (size_t i = 0; i < n - late; i++) {
        t += lens_sim_byte_us(g.lens);
        inject(t, LENS_SIM_BYTE, b[i]);
    }
    if (!late) t += lens_sim_lens_cs_tail_us(g.lens);
    inject(t, LENS_SIM_LENS_CS, 0);
    for (size_t i = n - late; i < n; i++) {
        t += lens_sim_byte_us(g.lens);
        inject(t, LENS_SIM_BYTE, b[i]);
    }
    g.inj_free = t;
}

void phy_sim_lens_raw(const uint8_t *b, size_t n) { lens_raw(b, n, 0); }
void phy_sim_lens_raw_late(const uint8_t *b, size_t n, size_t late) { lens_raw(b, n, late); }

void phy_sim_send_bus_error(void) { g.send_bus_error = true; }

void phy_sim_fault(phy_sim_fault_fn fn) { g.fault = fn; }

static void body_cs_to_lens(bool high);

/* La coupure du verrou, celle de l'interruption de la carte (cut de components/phy/phy.c) : les rails, puis TXD, BODY_CS et
 * la VD au repos. BODY_CS relâchée retombe pour le faux objectif (le tirage bas), comme bsk_phy_lines(false). */
static void cut(void)
{
    g.w.rail[BSK_RAIL_LOGIC] = g.w.rail[BSK_RAIL_MOTOR] = false;
    g.vd_period = 0;
    body_cs_to_lens(false);
    g.w.txd = g.w.body_cs = g.w.vd = PHY_SIM_REST;
}

/* La retombée (un front vers « absent ») est l'interruption de la carte (detect_isr) : objectif présent, l'échéance
 * armée au premier front (d2_due) ; absent, coupure et verrou dans cet appel même. */
void phy_sim_d2(bool present)
{
    if (g.d2_pin && !present && pres_edge(&g.pres, g.now) == PRES_CUT) cut();
    g.d2_pin = present;
    d2_report();
}

void phy_sim_mounted(void)
{
    g.d2_pin = true;
    g.pres.present = g.pres.raw = true;
    g.pres.since = g.now;
}

/* ─────────────────────────── bsk_phy.h ─────────────────────────── */

bsk_err_t bsk_phy_send(const bsk_frame_t *f)
{
    uint8_t b[BSK_FRAME_MAX];
    if (g.send_bus_error) {     /* injecté : le fil simulé n'a pas d'autre défaut électrique */
        g.send_bus_error = false;
        return E_BUS;
    }
    if (g.fault && g.fault(true, f) != PHY_SIM_PASS) return E_OK;   /* perdue sur le fil */
    if (g.w.body_cs == PHY_SIM_REST) return E_OK;   /* lignes relâchées, TXD non routée : rien ne part */
    schedule_bytes(b, fr_encode(f->cls, f->seq, f->msg, f->len, b, sizeof b));
    return E_OK;
}

static void body_cs_to_lens(bool high)
{
    uint64_t t = g.body_free > g.now ? g.body_free : g.now;
    schedule(true, high ? 1 : 0, t);
    g.body_free = t;
}

/* Relâchée, la commande ne pilote rien, et le faux objectif la voit toujours basse (le tirage bas). */
void bsk_phy_body_cs(bool high)
{
    if (g.w.body_cs == PHY_SIM_REST) return;
    body_cs_to_lens(high);
    g.w.body_cs = high ? PHY_SIM_HIGH : PHY_SIM_LOW;
}

/* Le niveau que le faux objectif a posé jusqu'à l'instant courant de la simulation. */
bool bsk_phy_lens_cs(void) { return g.lens_cs; }

bool bsk_phy_vd(uint16_t hz)
{
    if (hz && !g.pres.present) return false;       /* le verrou */
    g.vd_period = hz ? 1000000u / hz : 0;
    g.vd_next = g.now + g.vd_period;
    g.w.vd = hz ? PHY_SIM_HIGH : PHY_SIM_REST;
    return true;
}

/* Le proto de l'humain n'a pas d'interrupteur sur D0/D1 : sans effet sur le faux objectif, comme sur lui ; noté. */
bool bsk_phy_rail(bsk_rail_t rail, bool on)
{
    if (on && !g.pres.present) return false;       /* le verrou */
    if (on && !g.w.rail[rail]) g.w.rail_t[rail] = g.now;
    g.w.rail[rail] = on;
    return true;
}

/* Relâchées, BODY_CS retombe par le tirage bas : le faux objectif la voit basse, comme après bsk_phy_body_cs(false). */
bool bsk_phy_lines(bool drive)
{
    if (drive && !g.pres.present) return false;    /* le verrou */
    if (!drive) body_cs_to_lens(false);
    g.w.txd = drive ? PHY_SIM_HIGH : PHY_SIM_REST;
    g.w.body_cs = drive ? PHY_SIM_LOW : PHY_SIM_REST;
    return true;
}

const phy_sim_wires_t *phy_sim_wires(void) { return &g.w; }

/* La présence d'abord, à son rang dans l'ordre des instants (pres_next) ; puis la file. */
bool bsk_phy_poll(bsk_phy_event_t *ev)
{
    if (pres_next(&g.pres, g.ev_n ? g.ev[g.ev_head].t_us : UINT64_MAX, ev)) return true;
    if (g.ev_n == 0) return false;
    *ev = g.ev[g.ev_head];
    g.ev_head = (g.ev_head + 1) % EV_CAP;
    g.ev_n--;
    return true;
}

/* Les compteurs de la carte (bsk_phy.h) : zéro ici, ni file bornée de la carte ni UART. */
void bsk_phy_stats(bsk_phy_stats_t *st) { memset(st, 0, sizeof *st); }
