/* SOURCE: les deux faux objectifs, le faux 135 (sim/lens135.h) et le faux standard (sim/lens_std.h)
 * AUTHOR: engineer
 * DATE: 2026-09-29
 * STATUS: actif — lu par sim/phy_sim.c et sim/bench.c
 * L'interface commune « faux objectif » : ce que la PHY simulée et le banc voient d'un faux objectif, quel qu'il soit.
 * Chaque faux objectif s'y branche par un adaptateur (sim/lens_sim_135.c, sim/lens_sim_std.c), sans que son code change.
 *
 * Un faux objectif reçoit ce qu'un boîtier enverrait (octets, fronts VD, BODY_CS, alimentation) et rend ce que
 * l'objectif rendrait (octets, LENS_CS), sur une horloge virtuelle en microsecondes. Les deux modèles ont la même
 * forme d'interface (lens_std.h : « même forme que lens135.h ») : les opérations ci-dessous en sont la liste, une
 * à une, et un adaptateur ne fait que les appeler et convertir leur type de sortie.
 *
 * Une seule opération diffère de forme : l'objectif resté alimenté. Le faux 135 porte le mode service de sa
 * session d'avant (l135_start_powered, `service`), le faux standard la mise en page de son premier 0x0A
 * (lstd_start_powered, `grant`). lens_sim_powered_t porte les deux ; chaque
 * adaptateur REFUSE (rend false, sans rien démarrer) un état qu'il ne sait pas représenter, au lieu de le perdre :
 * le 135 refuse une mise en page (sa mise en page est celle des traces, pas une donnée), le standard refuse le mode
 * service (il n'a pas de canal 0x40), un flux sans mise en page et une mise en page sans flux. */
#ifndef LENS_SIM_H
#define LENS_SIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { LENS_SIM_BYTE, LENS_SIM_LENS_CS } lens_sim_out_kind_t;
typedef struct {
    uint64_t            t;
    lens_sim_out_kind_t kind;
    uint8_t             v;        /* octet, ou niveau de LENS_CS (1 = haut) */
} lens_sim_out_t;

/* L'objectif resté alimenté (7_Docs/E-Mount/samyang.md § 2.7 ; § 4.1 de la référence du F051) : ce qu'il garde de
 * la session d'avant. */
typedef struct {
    bool           flow;          /* en flux : le 135 avec le masque des traces, le standard en phase 1 */
    bool           service;       /* faux 135 : le mode service (canal 0x40) ; refusé par le standard */
    const uint8_t *grant;         /* faux standard : les octets 1 à 16 de la réponse au premier 0x0A, en flux
                                   * seulement ; NULL sinon ; refusé (non NULL) par le 135 */
} lens_sim_powered_t;

typedef struct {
    const char *name;
    void     (*power)(void *l, uint64_t t, bool on);
    bool     (*start_powered)(void *l, uint64_t t, const lens_sim_powered_t *st);
    void     (*body_cs)(void *l, uint64_t t, bool high);
    void     (*byte)(void *l, uint64_t t, uint8_t b);
    void     (*vd)(void *l, uint64_t t);
    void     (*advance)(void *l, uint64_t t);
    uint64_t (*next_event)(const void *l);
    bool     (*out)(void *l, lens_sim_out_t *o);
    uint64_t (*byte_us)(const void *l);          /* durée d'un octet sur la ligne, paramètre du modèle */
    uint64_t (*lens_cs_tail_us)(const void *l);  /* LENS_CS tenue haute après le dernier octet, paramètre du modèle */
    bool     (*blocked)(const void *l);
    bool     (*out_of_model)(const void *l);
    uint32_t (*unmodelled)(const void *l, uint8_t type);
    int32_t  (*position)(const void *l);
} lens_sim_ops_t;

/* Un faux objectif : ses opérations et son état. Se copie par valeur. */
typedef struct {
    const lens_sim_ops_t *ops;
    void                 *self;
} lens_sim_t;

static inline void lens_sim_power(lens_sim_t l, uint64_t t, bool on) { l.ops->power(l.self, t, on); }
static inline bool lens_sim_start_powered(lens_sim_t l, uint64_t t, const lens_sim_powered_t *st)
{
    return l.ops->start_powered(l.self, t, st);
}
static inline void lens_sim_body_cs(lens_sim_t l, uint64_t t, bool high) { l.ops->body_cs(l.self, t, high); }
static inline void lens_sim_byte(lens_sim_t l, uint64_t t, uint8_t b) { l.ops->byte(l.self, t, b); }
static inline void lens_sim_vd(lens_sim_t l, uint64_t t) { l.ops->vd(l.self, t); }
static inline void lens_sim_advance(lens_sim_t l, uint64_t t) { l.ops->advance(l.self, t); }
static inline uint64_t lens_sim_next_event(lens_sim_t l) { return l.ops->next_event(l.self); }
static inline bool lens_sim_out(lens_sim_t l, lens_sim_out_t *o) { return l.ops->out(l.self, o); }
static inline uint64_t lens_sim_byte_us(lens_sim_t l) { return l.ops->byte_us(l.self); }
static inline uint64_t lens_sim_lens_cs_tail_us(lens_sim_t l) { return l.ops->lens_cs_tail_us(l.self); }
static inline bool lens_sim_blocked(lens_sim_t l) { return l.ops->blocked(l.self); }
static inline bool lens_sim_out_of_model(lens_sim_t l) { return l.ops->out_of_model(l.self); }
static inline uint32_t lens_sim_unmodelled(lens_sim_t l, uint8_t type) { return l.ops->unmodelled(l.self, type); }
static inline int32_t lens_sim_position(lens_sim_t l) { return l.ops->position(l.self); }

/* Les adaptateurs sont déclarés à côté de chaque modèle, pour qu'un programme n'inclue que celui qu'il lie :
 * lens_sim_135 (sim/lens_sim_135.h), lens_sim_std (sim/lens_sim_std.h). L'objet reste celui de l'appelant, qui
 * l'initialise (l135_init, lstd_init) et peut toujours lire ses champs. */

#endif
