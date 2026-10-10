/* SOURCE: spec de l'atelier § 3.3 (la boucle cadencée) ; traces full:67-68
 * AUTHOR: engineer
 * DATE: 2026-09-26
 * STATUS: actif — lié à test_lens135, replay et test_phy (sim/test/programmes.sh), la PHY simulée sans SESSION
 * Le banc commun du rejeu et des tests de pannes (voir bench.h). */
#include "bench.h"

#include <string.h>

#include "phy_sim.h"

bench_t g_bench;

/* full:67 (0x03, sans le « 2F 5F 5F » qui le suit) et full:68 (0x04) : la paire tracée */
static const uint8_t LOOP_03[] = {0x03, 0xC2, 0x2E, 0x00, 0xB2, 0x11, 0xB2, 0x11, 0x1C, 0x00, 0x00,
                                  0x06, 0x00, 0x00, 0x02, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00};
static const uint8_t LOOP_04[] = {0x04, 0x00, 0x00, 0x19, 0x83, 0x00, 0x00, 0x3D, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};

void bench_loop_msgs(const uint8_t *m03, size_t n03, const uint8_t *m04, size_t n04)
{
    memcpy(g_bench.m03, m03, n03);
    g_bench.n03 = n03;
    memcpy(g_bench.m04, m04, n04);
    g_bench.n04 = n04;
}

void bench_init(lens_sim_t lens, uint64_t t0)
{
    memset(&g_bench, 0, sizeof g_bench);
    lens_sim_advance(lens, t0);      /* hors tension : l'horloge avance, rien d'autre */
    phy_sim_init(lens, t0);
    bench_loop_msgs(LOOP_03, sizeof LOOP_03, LOOP_04, sizeof LOOP_04);
}

void bench_loop(bool on) { g_bench.loop = on; }

void bench_send(uint8_t cls, const uint8_t *msg, size_t n)
{
    bsk_frame_t f = {.cls = cls, .seq = g_bench.seq++, .len = (uint16_t)n};
    memcpy(f.msg, msg, n);
    BENCH_SENT(g_bench.n_sent) = (bench_sent_t){phy_sim_now(), f};
    g_bench.n_sent++;
    bsk_phy_send(&f);
}

static void note(const bsk_phy_event_t *e)
{
    bench_ev_t *b = &BENCH_EV(g_bench.n);
    g_bench.n++;
    memset(b, 0, sizeof *b);
    b->kind = e->kind;
    b->t = e->t_us;
    if (e->kind == BSK_PHY_FRAME) b->frame = e->u.frame;
    else if (e->kind == BSK_PHY_ERROR) {
        b->err = e->u.err;
        g_bench.raw = e->u.raw;
    }
    else if (e->kind == BSK_PHY_LENS_CS) b->level = e->u.level;
    else if (e->kind == BSK_PHY_PRESENCE) b->level = e->u.present;
}

void bench_run(uint64_t t)
{
    for (;;) {
        bsk_phy_event_t e;
        uint64_t tn = phy_sim_next();
        if (tn > t) tn = t;
        phy_sim_run(tn);
        while (bsk_phy_poll(&e)) {
            if (e.kind == BSK_PHY_VD) {
                if (g_bench.loop) {    /* la paire porte une seule séquence (full:67-68) */
                    uint8_t s = g_bench.seq++;
                    g_bench.seq = s;
                    if (g_bench.n03) bench_send(1, g_bench.m03, g_bench.n03);
                    g_bench.seq = s;
                    if (g_bench.n04) bench_send(1, g_bench.m04, g_bench.n04);
                    g_bench.seq = (uint8_t)(s + 1);
                }
            } else {
                note(&e);
            }
        }
        if (tn >= t) break;
    }
}

void bench_run_for(uint64_t us) { bench_run(phy_sim_now() + us); }

static size_t oldest(size_t from)
{
    return g_bench.n > BENCH_LOG_CAP && from < g_bench.n - BENCH_LOG_CAP ? g_bench.n - BENCH_LOG_CAP : from;
}

long bench_find(size_t from, uint8_t type, uint8_t main, uint8_t sub)
{
    for (size_t i = oldest(from); i < g_bench.n; i++) {
        const bench_ev_t *b = &BENCH_EV(i);
        if (b->kind != BSK_PHY_FRAME || b->frame.msg[0] != type) continue;
        if (main && (b->frame.len < 3 || b->frame.msg[1] != main || b->frame.msg[2] != sub)) continue;
        return (long)i;
    }
    return -1;
}

size_t bench_count(size_t from, uint8_t type)
{
    size_t n = 0;
    for (size_t i = oldest(from); i < g_bench.n; i++)
        if (BENCH_EV(i).kind == BSK_PHY_FRAME && BENCH_EV(i).frame.msg[0] == type) n++;
    return n;
}

size_t bench_count_kind(size_t from, bsk_phy_event_kind_t kind)
{
    size_t n = 0;
    for (size_t i = oldest(from); i < g_bench.n; i++)
        if (BENCH_EV(i).kind == kind) n++;
    return n;
}
