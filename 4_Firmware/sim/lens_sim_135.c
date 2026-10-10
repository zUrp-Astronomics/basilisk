/* SOURCE: l'interface commune « faux objectif » (sim/lens_sim.h)
 * AUTHOR: engineer
 * DATE: 2026-09-29
 * STATUS: actif — lié avec sim/lens135.c par les programmes de la suite de 4_Firmware/
 * L'adaptateur du faux 135 à l'interface commune : il appelle sim/lens135.c et ne fait que convertir le type de sortie
 * et l'état « resté alimenté ».
 *
 * Resté alimenté : le 135 porte `service` (l135_start_powered). Une mise en page (`grant`) n'est pas une donnée
 * du faux 135, dont le flux suit le masque des traces : elle est refusée, rien n'est démarré. */
#include "lens_sim_135.h"

static void power(void *l, uint64_t t, bool on) { l135_power(l, t, on); }

static bool start_powered(void *l, uint64_t t, const lens_sim_powered_t *st)
{
    if (st->grant) return false;
    l135_start_powered(l, t, st->flow, st->service);
    return true;
}

static void body_cs(void *l, uint64_t t, bool high) { l135_body_cs(l, t, high); }
static void byte(void *l, uint64_t t, uint8_t b) { l135_byte(l, t, b); }
static void vd(void *l, uint64_t t) { l135_vd(l, t); }
static void advance(void *l, uint64_t t) { l135_advance(l, t); }
static uint64_t next_event(const void *l) { return l135_next_event(l); }

static bool out(void *l, lens_sim_out_t *o)
{
    l135_out_t x;
    if (!l135_out(l, &x)) return false;
    *o = (lens_sim_out_t){x.t, x.kind == L135_OUT_BYTE ? LENS_SIM_BYTE : LENS_SIM_LENS_CS, x.v};
    return true;
}

static uint64_t byte_us(const void *l) { return ((const l135_t *)l)->p.byte_us; }
static uint64_t lens_cs_tail_us(const void *l) { return ((const l135_t *)l)->p.lens_cs_tail_us; }
static bool blocked(const void *l) { return l135_blocked(l); }
static bool out_of_model(const void *l) { return l135_out_of_model(l); }
static uint32_t unmodelled(const void *l, uint8_t type) { return l135_unmodelled(l, type); }
static int32_t position(const void *l) { return l135_position(l); }

static const lens_sim_ops_t OPS = {
    "faux Samyang AF 135", power, start_powered, body_cs, byte, vd, advance, next_event, out,
    byte_us, lens_cs_tail_us, blocked, out_of_model, unmodelled, position,
};

lens_sim_t lens_sim_135(l135_t *l) { return (lens_sim_t){&OPS, l}; }
