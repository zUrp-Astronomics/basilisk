/* SOURCE: include/bsk_phy.h ; 7_Docs/E-Mount/protocol.md § 2 (la poignée de main)
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — lié à sim/test/test_session_script.c
 * Le répondeur minimal derrière bsk_phy.h (voir phy_script.h) : bsk_phy_rail, bsk_phy_lines et bsk_phy_vd rendent true. */
#include "phy_script.h"

#include <string.h>

#define EV_CAP 4096u
#define HS_US 1100u      /* « plus d'une milliseconde » */
#define FLOW_05_US 2000u /* 0x05 au créneau 0, environ 2 ms après la VD, comme le faux 135 */
#define FLOW_06_US 3000u

ps_lens_t g_ps;
ps_act_t  g_ps_log[PS_LOG_CAP];
size_t    g_ps_n;

static struct {
    uint64_t        now;
    uint64_t        vd_period, vd_next;
    bsk_phy_event_t ev[EV_CAP];   /* triés par instant, dans l'ordre d'arrivée à instant égal */
    size_t          n;
    bool            lens_cs;      /* poignée de main : la LENS_CS que l'objectif tient haute */
    bool            body_up;      /* poignée de main : BODY_CS vue haute */
    uint64_t        cs_low_at;    /* instant où cette LENS_CS retombe ; UINT64_MAX tant qu'elle est haute */
    bool            level;        /* LENS_CS telle que la PHY la lit (bsk_phy_lens_cs) : le dernier niveau remis */
} p;

static void push(const bsk_phy_event_t *e)
{
    size_t i = p.n;
    if (p.n == EV_CAP) return;   /* le test lit ses événements : jamais plein */
    while (i > 0 && p.ev[i - 1].t_us > e->t_us) {
        p.ev[i] = p.ev[i - 1];
        i--;
    }
    p.ev[i] = *e;
    p.n++;
}

static void note(const ps_act_t *a)
{
    if (g_ps_n < PS_LOG_CAP) g_ps_log[g_ps_n] = *a;
    g_ps_n++;
}

/* Posé derrière la file, sans tri : rendu après tout ce qui y est, même daté plus tard (phy_script.h). */
void ps_push_back(const bsk_phy_event_t *e)
{
    if (p.n < EV_CAP) p.ev[p.n++] = *e;
}

void ps_frame(uint8_t cls, const uint8_t *msg, size_t len, uint64_t at)
{
    bsk_phy_event_t e = {.kind = BSK_PHY_FRAME, .t_us = at};
    e.u.frame.cls = cls;
    e.u.frame.len = (uint16_t)len;
    memcpy(e.u.frame.msg, msg, len);
    push(&e);
}

static void lens_cs_at(bool high, uint64_t at)
{
    bsk_phy_event_t e = {.kind = BSK_PHY_LENS_CS, .t_us = at};
    e.u.level = high;
    push(&e);
}

void ps_lens_cs(bool high, uint64_t at) { lens_cs_at(high, at); }

void ps_lens_cs_start(bool high, bool seen)
{
    p.lens_cs = high;
    p.level = high && seen;
    p.cs_low_at = high ? UINT64_MAX : 0;
}

void ps_init(uint64_t t0)
{
    memset(&p, 0, sizeof p);
    memset(&g_ps, 0, sizeof g_ps);
    g_ps_n = 0;
    p.now = t0;
}

void ps_answer(uint8_t type, const uint8_t *msg, size_t len, uint64_t delay_us)
{
    memcpy(g_ps.reply[type].msg, msg, len);
    g_ps.reply[type].len = (uint16_t)len;
    g_ps.reply[type].delay_us = delay_us;
}

uint64_t ps_now(void) { return p.now; }

uint64_t ps_next(void)
{
    uint64_t t = p.n ? p.ev[0].t_us : UINT64_MAX;
    if (p.vd_period && p.vd_next < t) t = p.vd_next;
    return t < p.now ? p.now : t;
}

void ps_run(uint64_t t)
{
    while (p.vd_period && p.vd_next <= t) {
        bsk_phy_event_t e = {.kind = BSK_PHY_VD, .t_us = p.vd_next};
        push(&e);
        p.vd_next += p.vd_period;
    }
    if (t > p.now) p.now = t;
}

void ps_presence(bool present)
{
    bsk_phy_event_t e = {.kind = BSK_PHY_PRESENCE, .t_us = p.now};
    e.u.present = present;
    push(&e);
}

void ps_error(bsk_err_t err)
{
    bsk_phy_event_t e = {.kind = BSK_PHY_ERROR, .t_us = p.now};
    e.u.err = err;
    push(&e);
}

/* ─────────────────────────── bsk_phy.h ─────────────────────────── */

bsk_err_t bsk_phy_send(const bsk_frame_t *f)
{
    ps_act_t a = {.kind = PS_SEND, .t = p.now, .frame = *f};
    const ps_reply_t *r = &g_ps.reply[f->msg[0]];
    if (g_ps.fail_sends) {
        g_ps.fail_sends--;
        return E_BUS;
    }
    note(&a);
    if (g_ps.on_send) g_ps.on_send(f, p.now);
    if (g_ps.mute_cs_high && p.now < p.cs_low_at) return E_OK;   /* sa LENS_CS de poignée de main est haute */
    if (f->cls == 2 && r->len && g_ps.skip[f->msg[0]]) {
        g_ps.skip[f->msg[0]]--;
    } else if (f->cls == 2 && r->len) {
        ps_frame(2, r->msg, r->len, p.now + r->delay_us);
        if (f->msg[0] == 0x0A && g_ps.flow_on_0a) g_ps.flow = true;
    }
    if (f->cls == 1 && f->msg[0] == 0x04 && g_ps.flow) {
        if (g_ps.n05) ps_frame(1, g_ps.m05, g_ps.n05, p.now + FLOW_05_US);
        if (g_ps.n06) ps_frame(1, g_ps.m06, g_ps.n06, p.now + FLOW_06_US);
    }
    return E_OK;
}

/* Ce que l'objectif voit de BODY_CS : la poignée de main. */
static void body_cs_lens(bool high)
{
    if (!g_ps.handshake) return;
    if (high) {
        p.body_up = true;
        if (!p.lens_cs) {
            p.lens_cs = true;
            p.cs_low_at = UINT64_MAX;
            lens_cs_at(true, p.now + HS_US);
        }
    } else if (p.lens_cs && p.body_up) {
        p.lens_cs = false;
        g_ps.handshake = false;   /* faite : l'objectif ne la refait plus */
        p.cs_low_at = p.now + HS_US;
        lens_cs_at(false, p.cs_low_at);
    }
}

void bsk_phy_body_cs(bool high)
{
    ps_act_t a = {.kind = PS_BODY_CS, .t = p.now, .on = high};
    note(&a);
    body_cs_lens(high);
}

bool bsk_phy_lines(bool drive)
{
    ps_act_t a = {.kind = PS_LINES, .t = p.now, .on = drive};
    note(&a);
    if (!drive) body_cs_lens(false);   /* relâchée, BODY_CS retombe par le tirage bas */
    return true;
}

bool bsk_phy_lens_cs(void) { return p.level; }

bool bsk_phy_vd(uint16_t hz)
{
    ps_act_t a = {.kind = PS_VD, .t = p.now, .hz = hz};
    note(&a);
    p.vd_period = hz ? 1000000u / hz : 0;
    p.vd_next = p.now + p.vd_period;
    return true;
}

bool bsk_phy_rail(bsk_rail_t rail, bool on)
{
    ps_act_t a = {.kind = PS_RAIL, .t = p.now, .on = on, .rail = rail};
    note(&a);
    return true;
}

bool bsk_phy_poll(bsk_phy_event_t *ev)
{
    if (p.n == 0 || p.ev[0].t_us > p.now) return false;
    *ev = p.ev[0];
    if (ev->kind == BSK_PHY_LENS_CS) p.level = ev->u.level;
    memmove(p.ev, p.ev + 1, (p.n - 1) * sizeof p.ev[0]);
    p.n--;
    return true;
}

/* Les compteurs de la carte (bsk_phy.h) : zéro ici, ni file bornée de la carte ni UART. */
void bsk_phy_stats(bsk_phy_stats_t *st) { memset(st, 0, sizeof *st); }
