/* SOURCE: l'interface commune « faux objectif » (sim/lens_sim.h)
 * AUTHOR: engineer
 * DATE: 2026-09-29
 * STATUS: actif — lié avec sim/lens_std.c par test_std (sim/test/programmes.sh)
 * L'adaptateur du faux objectif standard (Tamron F051) à l'interface commune : il appelle sim/lens_std.c et ne fait
 * que convertir le type de sortie et l'état « resté alimenté ».
 *
 * Resté alimenté : le standard porte la mise en page de son premier 0x0A (lstd_start_powered, `grant`), en flux
 * seulement (sans flux, lens_std.c l'ignore). Sont refusés, rien n'est démarré : le mode service (le F051 n'a pas
 * de canal 0x40, 7_Docs/E-Mount/tamron.md § 5.2 : ≥ 0x40 refusé), un flux sans mise en page, une mise en page sans flux. */
#include "lens_sim_std.h"

static void power(void *l, uint64_t t, bool on) { lstd_power(l, t, on); }

static bool start_powered(void *l, uint64_t t, const lens_sim_powered_t *st)
{
    if (st->service || st->flow != (st->grant != NULL)) return false;
    lstd_start_powered(l, t, st->flow, st->grant);
    return true;
}

static void body_cs(void *l, uint64_t t, bool high) { lstd_body_cs(l, t, high); }
static void byte(void *l, uint64_t t, uint8_t b) { lstd_byte(l, t, b); }
static void vd(void *l, uint64_t t) { lstd_vd(l, t); }
static void advance(void *l, uint64_t t) { lstd_advance(l, t); }
static uint64_t next_event(const void *l) { return lstd_next_event(l); }

static bool out(void *l, lens_sim_out_t *o)
{
    lstd_out_t x;
    if (!lstd_out(l, &x)) return false;
    *o = (lens_sim_out_t){x.t, x.kind == LSTD_OUT_BYTE ? LENS_SIM_BYTE : LENS_SIM_LENS_CS, x.v};
    return true;
}

static uint64_t byte_us(const void *l) { return ((const lstd_t *)l)->p.byte_us; }
static uint64_t lens_cs_tail_us(const void *l) { return ((const lstd_t *)l)->p.lens_cs_tail_us; }
static bool blocked(const void *l) { return lstd_blocked(l); }
static bool out_of_model(const void *l) { return lstd_out_of_model(l); }
static uint32_t unmodelled(const void *l, uint8_t type) { return lstd_unmodelled(l, type); }
static int32_t position(const void *l) { return lstd_position(l); }

static const lens_sim_ops_t OPS = {
    "faux Tamron F051", power, start_powered, body_cs, byte, vd, advance, next_event, out,
    byte_us, lens_cs_tail_us, blocked, out_of_model, unmodelled, position,
};

lens_sim_t lens_sim_std(lstd_t *l) { return (lens_sim_t){&OPS, l}; }
