/* SOURCE: composant session — la bague de l'objectif (voir ring.h)
 * AUTHOR: engineer
 * DATE: 2026-10-03
 * STATUS: actif — C pur, sans ESP-IDF : compilé aussi par les tests hôte
 *
 * Le rôle de la bague et, en rôle ouverture, la consigne d'ouverture qu'elle pilote ; une ligne de journal par geste.
 *
 * Invariant : le bit du 0x04 est posé si et seulement si r.role == RING_APERTURE (role et ring_forget seuls écrivent
 * l'un et l'autre). */
#include "ring.h"

#include <stdlib.h>
#include <string.h>

#include "bsk_journal.h"
#include "bsk_txn.h"
#include "lens_rx.h"

#define BODY_AF_OFF 3u        /* offset 3 du 0x04 */
#define BODY_AF_BIT 0x02u     /* le « mode AF du boîtier » : posé, l'objectif lâche le focus ; indépendant du bit AF du 0x03 */
#define SW_MF       0x02u     /* offset 62, bit 1 : position MF */
#define PULSES      2         /* unités de la somme de l'offset 60 par tiers */
#define PAUSE_US    400000u   /* une pause clôt le geste */
#define PUB_US      1000000u  /* sans changement de l'ouverture publiée depuis 1 s, les impulsions reprennent (aperture) */
#define CAP_PER_S   3u        /* décision de l'humain : 3 tiers par seconde au plus */

/* Les crans d'un tiers (f/1.0 à f/45), en codes 256(Av+16) : lround(256 × (2·log2(f) + 16)). La SESSION ne parle qu'en
 * codes objectif : le cran le plus proche est cherché en codes, pas en f/. */
static const uint16_t THIRDS[] = {0x1000, 0x1046, 0x1087, 0x10F9, 0x115B, 0x11B2, 0x1200, 0x1246, 0x12A5, 0x12F9, 0x135B, 0x139D,
                                  0x1400, 0x1457, 0x14A5, 0x14F9, 0x1550, 0x15A8, 0x1600, 0x1657, 0x16A5, 0x16EB, 0x1767, 0x179D,
                                  0x1800, 0x1857, 0x18A5, 0x18EB, 0x194A, 0x19B7, 0x1A00, 0x1A57, 0x1AA5, 0x1AFC};
#define N_THIRDS (sizeof THIRDS / sizeof THIRDS[0])

_Static_assert(BODY_AF_OFF < BSK_TXN_LOOP04_LEN - 1, "le bit est dans le 0x04");

static struct {
    bsk_ring_t role;
    bool       said[3];       /* le journal a dit l'offset 62, 64, 66 dans cette session */
    uint8_t    last[3];       /* ... cette valeur */

    /* le geste, oublié à chaque bascule de rôle */
    int32_t    accum;         /* la somme de même sens, pas encore un tiers */
    int32_t    gesture;       /* la somme signée de l'offset 60 sur le geste */
    uint32_t   thirds;        /* ... les tiers qui ont changé la consigne pendant lui */
    uint32_t   frames;        /* ... les 0x05 non nuls qu'il a comptés */
    uint32_t   capped;        /* ... les tiers que le cap a perdus */
    uint64_t   third_us;      /* le dernier tiers appliqué ; ni le geste ni la session ne l'oublient (0 au
                                 démarrage : READY, où la bague agit, vient des secondes plus tard) */
    uint64_t   last_us;       /* la dernière impulsion */
    uint16_t   pub_code;      /* la dernière ouverture publiée, 0 : aucune */
    bool       pub;           /* l'objectif publie son ouverture : les impulsions attendent */
    uint64_t   pub_us;        /* ... depuis ce changement */
    bool       quiet;         /* l'offset 60 est revenu à 0 depuis le dernier mouvement de focus (ou avant READY) */
} r;

static void gesture_forget(void)
{
    r.accum = 0;
    r.gesture = 0;
    r.thirds = 0;
    r.frames = 0;
    r.capped = 0;
    r.pub_code = 0;
    r.pub = false;
}

static void role(bsk_ring_t v)
{
    if (v == r.role) return;
    r.role = v;
    gesture_forget();
    bsk_txn_loop04(BODY_AF_OFF, BODY_AF_BIT, v == RING_APERTURE ? BODY_AF_BIT : 0);
    bsk_journal_ring(v == RING_APERTURE);
}

/* Un tiers : le cran le plus proche de la consigne, puis le suivant (dir > 0, on ferme) ou le précédent (on ouvre),
 * bornés à la table ; session_aperture borne à la plage. Vrai si la consigne a changé. */
static bool step(int dir)
{
    int cur = session_aperture_target();
    size_t best = 0;
    for (size_t i = 1; i < N_THIRDS; i++)
        if (abs(THIRDS[i] - cur) < abs(THIRDS[best] - cur)) best = i;
    if (dir > 0 && best + 1 < N_THIRDS) best++;
    if (dir < 0 && best > 0) best--;
    session_aperture(THIRDS[best]);
    return session_aperture_target() != cur;
}

/* La fin du geste, close par la pause ou par un changement de sens : sa ligne, et le suivant repart. */
static void gesture_end(void)
{
    bsk_journal_gesture(r.gesture, r.thirds, r.frames, r.capped);
    r.gesture = 0;
    r.thirds = 0;
    r.frames = 0;
    r.capped = 0;
}

/* La bague en rôle ouverture, à chaque 0x05.
 * L'offset 60 est compté à sa valeur signée : le 135 n'y publie qu'un drapeau ±1, le Tamron F051 un compte de fronts
 * (7_Docs/E-Mount/protocol.md § 7.5). PULSES unités de même sens font un tiers, le reste est gardé ; un changement de
 * sens repart de zéro, la pause clôt le geste et jette le reste.
 * Le cap : deux tiers appliqués par la bague sont séparés d'au moins 1/CAP_PER_S s. Un tiers dû avant est PERDU, pas
 * différé : retiré de la somme, compté dans la ligne du geste. Rien n'est appliqué sur un 0x05 sans impulsion : la bague
 * arrêtée, l'ouverture s'arrête. Le cap court à travers gestes, bascules et sessions : il borne la bague, pas un geste.
 * L'ouverture publiée et CMD_APERTURE_SET n'y passent pas.
 * La suspension : une ouverture publiée qui change fait taire les impulsions PUB_US. Elle défend un objectif qui publie
 * les deux pour le même geste : le Samyang AF 16 F2.8 P. Il pose ±1 à l'offset 60 à chaque front de bague, dans toutes
 * les configurations du commutateur, et, en position APERTURE avec le bit AF du boîtier, publie aux offsets 17-19
 * l'ouverture tirée du même compte de fronts (7_Docs/E-Mount/samyang.md § 5.5). Sans elle, un front ferait un pas par
 * l'ouverture publiée et un autre par l'offset 60. Le 135 n'a jamais les deux à la fois (ouverture publiée en position
 * APERTURE seulement, et l'offset 60 y vaut alors 0, samyang.md § 5.4, § 5.5), ni les autres Samyang lus
 * (§ 5.5), ni le F051 (offsets 17-19 jamais écrits, tamron.md § 2.5). */
static void aperture(const lens_rx_t *l, uint64_t t)
{
    if (l->ap_ring && r.pub_code && l->ap_ring != r.pub_code) {   /* l'ouverture publiée, crue si elle change */
        r.pub = true;
        r.pub_us = t;
        session_aperture(l->ap_ring);
    }
    if (l->ap_ring) r.pub_code = l->ap_ring;
    if (r.pub && t - r.pub_us > PUB_US) r.pub = false;
    if (r.pub) return;
    if (l->move) {
        int32_t d = (int8_t)l->move;
        if (r.accum && (r.accum > 0) != (d > 0)) r.accum = 0;
        if (r.gesture && (r.gesture > 0) != (d > 0)) gesture_end();
        r.accum += d;
        r.gesture += d;
        r.frames++;
        r.last_us = t;
        while (abs(r.accum) >= PULSES) {
            if ((t - r.third_us) * CAP_PER_S >= 1000000u) {
                r.third_us = t;
                r.thirds += step(r.accum);
            } else {
                r.capped++;                                       /* perdu, pas différé */
            }
            r.accum -= r.accum > 0 ? PULSES : -PULSES;
        }
    } else if (r.gesture && t - r.last_us > PAUSE_US) {
        gesture_end();
        r.accum = 0;
    }
}

/* Une ligne par offset qui change, jamais une par trame. */
static void watch(unsigned i, bool have, uint8_t off, uint8_t v)
{
    if (!have || (r.said[i] && r.last[i] == v)) return;
    r.said[i] = true;
    r.last[i] = v;
    bsk_journal_lens(off, v);
}

void ring_init(void) { memset(&r, 0, sizeof r); }

void ring_forget(void)
{
    r.role = RING_UNKNOWN;                         /* le geste avec lui : role() l'oublie à la première bascule */
    bsk_txn_loop04(BODY_AF_OFF, BODY_AF_BIT, 0);
    memset(r.said, 0, sizeof r.said);
}

void ring_frame(uint8_t type, uint64_t t, bool turn)
{
    const lens_rx_t *l = lens_rx();
    /* Jamais le 0x06 : ses offsets 32 à 38 sont la trace de vitesse du focus, pas la bague (l'expression est de
     * weiziqian, https://github.com/weiziqian/E-mount-protocol-RE) ; le Sony les rend non nuls quand on tourne sa bague
     * en AF (son asservissement tremble), et un tiers partait sans geste à son démarrage. */
    if (type != 0x05) return;
    watch(0, l->have62, 62, l->o62);
    watch(1, l->have64, 64, l->o64);
    watch(2, l->have66, 66, l->o66);
    if (l->have62) role(l->o62 & SW_MF ? RING_FOCUS : RING_APERTURE);
    if (!turn) r.quiet = false;                   /* la garde du mouvement de focus (ring.h) */
    else if (!l->move) r.quiet = true;
    if (r.role == RING_APERTURE && r.quiet) aperture(l, t);
}

bsk_ring_t ring_role(void) { return r.role; }
