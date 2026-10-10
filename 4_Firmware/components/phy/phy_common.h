/* SOURCE: spec de l'atelier § 1 (R1) et § 2 — le code commun aux deux PHY
 * AUTHOR: engineer
 * DATE: 2026-10-03
 * STATUS: actif — C pur, sans ESP-IDF : compilé pour la carte et par les tests hôte
 *
 * Le codec de trame (7_Docs/E-Mount/protocol.md § 3), la réception en flux et la présence de D2 avec son verrou,
 * communs à la PHY de la carte (phy.c) et à la PHY simulée : les deux jugent ce qu'elles reçoivent de la même façon, la
 * frontière R1 est la même pour la carte et pour le simulateur. */
#ifndef PHY_COMMON_H
#define PHY_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsk_phy.h"

uint16_t fr_sum(const uint8_t *frame, size_t len);

/* Trame complète dans `out` ; rend sa longueur, 0 si `cap` ne suffit pas. */
size_t fr_encode(uint8_t cls, uint8_t seq, const uint8_t *msg, size_t n, uint8_t *out, size_t cap);

/* Exactement une trame de `n` octets. E_OK et `f` rempli, ou E_FRAMING : pas de F0 en tête, longueur
 * annoncée différente de `n` ou hors de [BSK_FRAME_OVERHEAD + 1, BSK_FRAME_MAX], classe hors 1 à 3,
 * somme fausse, pas de 55 à la fin. */
bsk_err_t fr_decode(const uint8_t *b, size_t n, bsk_frame_t *f);

/* Le jugement d'un octet du flux. `b[0]` est l'octet jugé, `n` ≥ 1 les octets reçus depuis lui.
 *   FR_OK    : une trame valide (fr_decode) de `*len` octets en tête, `f` rempli ;
 *   FR_SHORT : un début de trame plausible que les octets reçus n'achèvent pas — F0 et moins de 3 octets (longueur
 *              illisible), ou une longueur annoncée entre BSK_FRAME_OVERHEAD + 1 et BSK_FRAME_MAX, plus que `n` :
 *              rien n'est jugé, la suite est attendue ;
 *   FR_BAD   : cet octet n'ouvre pas une trame valide (pas de F0, longueur hors borne, trame complète invalide). */
typedef enum { FR_OK, FR_SHORT, FR_BAD } fr_scan_t;

fr_scan_t fr_scan(const uint8_t *b, size_t n, bsk_frame_t *f, size_t *len);

/* Les octets rejetés : les `n` octets de `b`, écartés d'un seul tenant (n ≤ 65535). n = 0 : l'erreur ne porte aucun
 * octet. */
void phy_raw(bsk_phy_raw_t *r, const uint8_t *b, size_t n);

/* ─────────────────────────── la réception en flux ───────────────────────────
 * LENS_CS ne délimite rien : le Sony FE 24-105 G rabaisse LENS_CS dès son dernier octet, que l'UART ne rend qu'ensuite ;
 * juger à la retombée tronquait sa réponse au 0x01 (capturé par l'humain). Donc :
 *   - les octets reçus s'accumulent (rx_got) ;
 *   - une trame est extraite dès qu'elle est complète, selon sa propre longueur (fr_scan) ;
 *   - un octet qui ne peut pas commencer une trame est écarté (pas de F0, ou un F0 qui n'ouvre pas une trame valide) ;
 *   - un début de trame incomplet attend sa suite ; sans octet reçu depuis plus de RX_STALE_US, il est écarté.
 * Un octet écarté sort du tampon dès son jugement (décision de l'humain du 2026-10-07, audit M1 : « on filtre et on envoie
 * dans le void ») : il n'est jamais rejugé, et le coût de la réception d'un flux de bruit est linéaire. Seuls restent dans
 * le tampon un début de trame qui attend sa suite (au plus BSK_FRAME_MAX − 1 octets) et ce qui le suit, pas encore jugé.
 * Le sort d'un octet ne change pas d'un jugement à l'autre : une fois écarté, il le serait encore avec plus d'octets.
 * Le relevé des octets écartés (junk) n'en garde que leur nombre et les BSK_PHY_RAW_MAX premiers, ce que l'erreur porte.
 * Ils remontent en E_FRAMING, une erreur par suite d'un seul tenant : avant la trame qui la suit, quand le flux se tait
 * RX_STALE_US, ou quand ils sont RX_CAP.
 * Les instants sont ceux de l'appelant : `t` de l'octet reçu, `now` du jugement (l'événement rendu le porte). */
#define RX_STALE_US 20000u
#define RX_CAP      4096u

typedef struct {
    uint8_t       b[RX_CAP];
    size_t        n;       /* octets reçus, ni remontés ni écartés : un début de trame qui attend sa suite, et la suite */
    bsk_phy_raw_t junk;    /* les octets écartés, pas encore rapportés : leur nombre (au plus RX_CAP), les premiers */
    uint64_t      last;    /* instant du dernier octet reçu */
} rx_t;                    /* à zéro : vide */

/* `k` octets viennent d'être écrits à r->b + r->n (au plus RX_CAP − r->n, jamais nul après un rx_next qui a rendu
 * false), à l'instant `t`. */
void rx_got(rx_t *r, size_t k, uint64_t t);

/* Le prochain événement du flux, jugé à l'instant `now` : une trame (BSK_PHY_FRAME), ou les octets écartés
 * (BSK_PHY_ERROR, E_FRAMING, bsk_phy_raw_t). false : rien à rendre — vide, ou un début de trame qui attend sa suite,
 * ou des octets écartés qui ne sont pas encore à rapporter. À rappeler tant qu'il rend true. */
bool rx_next(rx_t *r, uint64_t now, bsk_phy_event_t *e);

/* Instant à partir duquel un appel à rx_next écartera ce qui attend, ou rapportera les octets écartés (plus de
 * RX_STALE_US sans octet) ; UINT64_MAX si rien n'attend. */
uint64_t rx_deadline(const rx_t *r);

/* La réception vidée, pour une E_BUS (des octets manquent au flux) : dans `raw`, les octets reçus qu'aucune trame n'a pris
 * et qui n'ont pas été rapportés, dans l'ordre de leur réception — les écartés pas encore rapportés, puis ceux qui
 * attendaient leur jugement. Chacun n'est rapporté qu'une fois : par une E_FRAMING ou par cette E_BUS. */
void rx_flush(rx_t *r, bsk_phy_raw_t *raw);

/* ─────────────────────────── la présence et le verrou ───────────────────────────
 * XDETECT (D2, broche 10 de la monture) est le dernier contact établi à l'insertion et le premier à lâcher au retrait ;
 * après lui, les autres contacts glissent sur les pastilles voisines. Les deux sens n'ont pas la même règle :
 *   - la retombée (le niveau passe à « absent ») : 2 ms en temps réel depuis son premier front. Un rebond de contact
 *     dure moins d'une milliseconde, faire glisser les contacts à la main des dizaines, et couper pour rien coûte 2 à
 *     3 s (l'init et le retour à la marque). Chaque front de retombée est donné à pres_edge par l'interruption qui le
 *     prend. Le premier, objectif présent, arme l'échéance de D2_DROP_US, sans tâche ni file ; ceux qui le suivent avant
 *     elle ne la relancent pas : la décision tombe 2 ms après le premier, sur le niveau lu alors, donné à pres_expire.
 *     Toujours absent : pres_drop pose le verrou, et l'appelant de pres_expire coupe les rails et met les lignes au
 *     repos dans le même appel. Revenu présent : rien n'est coupé, l'état ne change pas, aucune présence n'est rendue ;
 *     l'appelant met un BSK_PHY_BOUNCE dans la file des événements, pour le journal. Objectif absent (verrou déjà posé,
 *     une insertion peut-être en cours d'anti-rebond), le front est pris tout de suite : pres_drop, coupure ;
 *   - l'insertion : 300 ms sans changement ; pres_sample, appelée à chaque lecture du niveau, lève le verrou. Une
 *     retombée de plus de 2 ms, objectif monté, coupe donc tout, et tout redémarre 300 ms après le retour du contact.
 * Le verrou est l'état « absent » de la PHY (present faux) : posé au démarrage (la structure à zéro), par chaque
 * retombée, levé par une insertion confirmée seulement. Pendant les 2 ms d'une retombée, il ne l'est pas encore.
 * Chaque changement d'état est tenu ici (drop, ins), hors de la file des événements, jusqu'à ce que bsk_phy_poll le rende
 * (pres_next) : une file pleine, qui jette son plus ancien, ne le perd pas. Ce qui est tenu est l'écart entre le dernier
 * état rendu et l'état courant : rien, une retombée, une insertion, ou une retombée puis une insertion. Une insertion
 * jamais rendue et la retombée qui la suit s'annulent ; une retombée qui suit un état « présent » rendu est toujours
 * rendue, à l'instant de la première. Les présences rendues alternent donc, et la dernière est l'état courant.
 * L'instant d'une retombée est celui de sa décision, où elle coupe : 2 ms après son premier front (ou ce front même,
 * objectif absent).
 * Sur la carte, pres_edge et pres_expire tournent dans des interruptions en IRAM, qui n'attendent pas la fin d'une
 * écriture en flash : elles sont en ligne, forcées (always_inline), comme pres_drop, pour que leur code soit celui de
 * l'interruption, en IRAM, sans placement par l'éditeur de liens. Toutes ces fonctions se partagent la structure sous
 * la même section critique (pres_mux de phy.c) ; la PHY simulée n'a qu'un fil. */
#define D2_DEBOUNCE_US (300u * 1000u)
#define D2_DROP_US     2000u      /* le délai de la retombée, en temps réel */

typedef struct {
    bool     present;   /* l'état de la PHY : true, insertion confirmée, verrou levé ; false, absent, verrou posé */
    bool     raw;       /* le dernier niveau lu par pres_sample depuis la dernière retombée : true = présent */
    uint64_t since;     /* instant où il a changé */
    bool     drop, ins; /* à rendre : une retombée, une insertion confirmée */
    uint64_t drop_t, ins_t;
    bool     pend;      /* une retombée attend son échéance, pend_t + D2_DROP_US */
    uint64_t pend_t;    /* son premier front */
} pres_t;               /* à zéro : absent, verrou posé, rien à rendre, rien en attente */

/* La retombée de XDETECT décidée à l'instant t : le verrou posé, l'anti-rebond de l'insertion repart de zéro. Elle n'est
 * tenue que si l'état était « présent » : une retombée pendant l'anti-rebond d'une insertion ne change pas l'état ; et
 * elle annule une insertion pas encore rendue. */
static inline __attribute__((always_inline)) void pres_drop(pres_t *p, uint64_t t)
{
    if (p->present && p->ins) {
        p->ins = false;
    } else if (p->present) {
        p->drop = true;
        p->drop_t = t;
    }
    p->present = false;
    p->raw = false;
}

/* Ce que l'interruption fait d'un front de retombée : */
typedef enum {
    PRES_CUT,    /* objectif absent : pres_drop fait, l'appelant coupe maintenant */
    PRES_ARM,    /* le premier front, objectif présent : l'appelant arme l'échéance de D2_DROP_US */
    PRES_WAIT,   /* une échéance est déjà armée : rien, elle n'est pas relancée */
} pres_edge_t;

static inline __attribute__((always_inline)) pres_edge_t pres_edge(pres_t *p, uint64_t t)
{
    if (!p->present) {
        pres_drop(p, t);
        return PRES_CUT;
    }
    if (p->pend) return PRES_WAIT;
    p->pend = true;
    p->pend_t = t;
    return PRES_ARM;
}

/* L'échéance d'une retombée, à l'instant t ; `absent` : le niveau de D2 relu maintenant. true : toujours absent,
 * pres_drop fait, l'appelant coupe ; false : revenu présent, un rebond, rien n'a changé. */
static inline __attribute__((always_inline)) bool pres_expire(pres_t *p, bool absent, uint64_t t)
{
    p->pend = false;
    if (absent) pres_drop(p, t);
    return absent;
}

/* Le niveau lu à l'instant t (true = présent). true : l'insertion vient d'être confirmée, le verrou levé. Un niveau
 * « absent » ne fait que remettre l'anti-rebond à zéro : la retombée est l'affaire de pres_edge et pres_expire. */
bool pres_sample(pres_t *p, bool present, uint64_t t);

/* Instant où l'insertion en cours aura tenu l'anti-rebond, UINT64_MAX s'il n'y en a aucune. */
uint64_t pres_deadline(const pres_t *p);

/* Le plus ancien changement d'état tenu (la retombée, s'il y en a une), s'il date au plus de `before` (l'instant du
 * premier événement de la file, UINT64_MAX si elle est vide) : `e` rempli en BSK_PHY_PRESENCE, à son instant, et le
 * changement oublié. false : rien à rendre avant `before`, `e` intact. */
bool pres_next(pres_t *p, uint64_t before, bsk_phy_event_t *e);

#endif
