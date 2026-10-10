/* SOURCE: spec de l'atelier § 1 (R1) et § 2 — la frontière PHY
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — déclarations seulement ; la carte (components/phy/phy.c) et la PHY simulée l'implémentent
 *
 * La frontière PHY : les trames, les lignes de la monture, les rails et le verrou de XDETECT. */
#ifndef BSK_PHY_H
#define BSK_PHY_H

#include <stdbool.h>
#include <stdint.h>

#include "bsk_err.h"

/* La frontière de confiance (R1) est entre PHY et PROTOCOLE : en amont, tout octet est possible ; au-dessus ne
 * remontent qu'une trame syntaxiquement valide ou une erreur nommée (bsk_err_t). Aucun module au-dessus ne revérifie
 * une trame.
 *
 * Trame E-mount : F0 | longueur u16 petit-boutiste | classe | séquence | sous-messages… | somme u16 | 55
 * La longueur compte toute la trame, la somme couvre les octets 1 à longueur − 4 de la trame (F0 en 0).
 * « Syntaxiquement valide » : F0 en tête, longueur cohérente et au plus BSK_FRAME_MAX, classe 1 à 3, somme juste, 55 à
 * la fin. Le pavage des sous-messages est l'affaire du protocole, pas de PHY.
 *
 * Les lignes de la monture portées ici :
 *   - VD, émise par la carte : la trame vidéo qui cadence l'objectif ;
 *   - BODY_CS, émise par la carte : haute pendant qu'elle parle (le 135 ne range un octet que BODY_CS haute, et
 *     n'émet que BODY_CS basse) ;
 *   - LENS_CS, lue par la carte : haute pendant que l'objectif émet ; elle ne délimite pas la réception ;
 *   - D2 (XDETECT), le contact de présence de la monture, lu par la carte ;
 *   - D0 et D1, les rails d'alimentation de l'objectif, commandés par la carte ;
 *   - TXD, l'émission de l'UART vers l'objectif, routée ou non (bsk_phy_lines).
 * PHY lit D2, rapporte sa présence, commande les rails et ne décide rien : les décisions d'alimentation appartiennent à
 * la SESSION. Une exception, le verrou de XDETECT.
 *
 * Le verrou de XDETECT. D2 est le dernier contact établi à l'insertion et le premier à lâcher au retrait ; après lui,
 * les autres contacts glissent sur les pastilles voisines (LENS_POWER sur LENS_GND, LOGIC_VCC sur une entrée de
 * signal). La PHY porte donc un verrou électrique, comme un fusible, qui ne prend aucune décision de session :
 *   - la retombée de D2 (passage à « absent ») est prise par interruption, avec 2 ms d'anti-rebond en temps réel (ni en
 *     VD ni en tick) : objectif présent, le premier front arme un minuteur d'une impulsion de 2 ms rappelé en
 *     interruption ; un nouveau front avant l'échéance ne le relance pas. À l'échéance, D2 est relu :
 *       - toujours absent : la coupure, dans ce rappel même, sans tâche ni file : les deux rails, TXD, BODY_CS et la VD
 *         au repos (ceux de bsk_phy_lines(false) et bsk_phy_vd(0)). Le verrou est posé ;
 *       - revenu présent (un rebond) : rien n'est coupé ni rapporté en BSK_PHY_PRESENCE, le verrou reste levé ; un
 *         BSK_PHY_BOUNCE remonte, pour le journal seul.
 *     Objectif absent (verrou déjà posé), un front de retombée coupe dans l'interruption même ;
 *   - tant qu'il est posé, la PHY refuse, quel que soit l'appelant, d'allumer un rail, de piloter les lignes et de
 *     démarrer la VD : ces commandes rendent false et ne changent rien. Couper un rail, relâcher les lignes, arrêter la
 *     VD passent toujours ;
 *   - il est posé au démarrage (état « absent ») et levé seulement par une insertion confirmée : D2 présent tenu
 *     300 ms, à l'instant où BSK_PHY_PRESENCE (true) remonte.
 * La SESSION ne voit le verrou qu'à travers ce contrat : la retombée lui remonte en BSK_PHY_PRESENCE (false), à
 * l'instant de la coupure, et un refus ne lui apprend rien que cet événement ne lui dise, rendu au plus tard au
 * bsk_phy_poll suivant. */

/* Trame la plus longue que PHY accepte, en-tête et fin compris. Le 135 rejette toute trame dont l'octet haut de
 * longueur n'est pas nul (7_Docs/E-Mount/samyang.md § 2.6), et toutes les réponses aux types de la liste blanche
 * tiennent dans cette borne : la plus longue est celle du 0x08, 202 octets de message. Une trame reçue plus longue est
 * une E_FRAMING. */
#define BSK_FRAME_MAX 255u
#define BSK_FRAME_OVERHEAD 8u                      /* F0, longueur (2), classe, séquence, somme (2), 55 */
#define BSK_MSG_MAX (BSK_FRAME_MAX - BSK_FRAME_OVERHEAD)

typedef struct {
    uint8_t  cls;                 /* classe, 1 à 3 */
    uint8_t  seq;                 /* séquence */
    uint16_t len;                 /* octets de sous-messages */
    uint8_t  msg[BSK_MSG_MAX];    /* sous-messages, octet de type compris */
} bsk_frame_t;

/* Les octets reçus de l'objectif que PHY a écartés, pour le journal seul : aucune couche ne décide sur eux (R1). Un
 * octet n'est rapporté qu'une fois.
 * Une E_FRAMING porte une suite d'octets écartés du flux d'un seul tenant — un octet qui n'ouvre pas de trame, une
 * trame invalide, un début de trame resté sans suite 20 ms —, rapportée avant la trame qui la suit, quand le flux se tait
 * 20 ms, ou quand elle atteint 4096 octets. Une E_BUS d'une erreur du pilote UART porte ceux du flux qu'elle vide, reçus,
 * pris par aucune trame, pas encore rapportés : la suite d'octets écartés qui attendait son E_FRAMING (elle n'en aura
 * pas), puis le début de trame qui attendait sa suite ; aucun s'il n'y en avait pas, ni pour l'E_BUS d'un front perdu.
 * `b` porte les `len` premiers des `n` octets, au plus BSK_PHY_RAW_MAX : 256, une trame de la plus grande longueur
 * acceptée y tient entière. */
#define BSK_PHY_RAW_MAX 256u

typedef struct {
    uint16_t n;       /* octets écartés ; 0 : l'erreur n'en porte aucun */
    uint16_t len;     /* octets recopiés dans b, les premiers */
    uint8_t  b[BSK_PHY_RAW_MAX];
} bsk_phy_raw_t;

typedef enum {
    BSK_PHY_FRAME,    /* une trame syntaxiquement valide reçue de l'objectif */
    BSK_PHY_ERROR,    /* une erreur nommée : E_FRAMING (octets écartés du flux), E_BUS */
    BSK_PHY_LENS_CS,  /* LENS_CS a changé de niveau (la poignée de main ; il ne délimite pas la réception) */
    BSK_PHY_VD,       /* la carte vient d'émettre un front VD */
    BSK_PHY_PRESENCE, /* D2 a changé d'état : retombée, à l'instant de la coupure, 2 ms après son premier front
                       * (objectif absent, ce front même) ; insertion, tenue 300 ms. Jamais perdu (bsk_phy_poll) */
    BSK_PHY_BOUNCE,   /* un rebond de D2, revenu présent à l'échéance d'une retombée. Pour le journal seul : l'état de
                       * présence n'a pas changé. Il passe par la file : une file débordée le perd (le plus ancien jeté) */
} bsk_phy_event_kind_t;

typedef struct {
    bsk_phy_event_kind_t kind;
    uint64_t             t_us;    /* instant de l'événement */
    union {
        bsk_frame_t frame;        /* BSK_PHY_FRAME */
        struct {                  /* BSK_PHY_ERROR */
            bsk_err_t     err;
            bsk_phy_raw_t raw;    /* ce que l'erreur a rejeté */
        };
        bool        level;        /* BSK_PHY_LENS_CS : niveau atteint (true = haut) */
        bool        present;      /* BSK_PHY_PRESENCE : true = objectif présent (D2 à la masse) */
    } u;
} bsk_phy_event_t;

/* Émet une trame : BODY_CS levée, les octets, BODY_CS rabaissée. E_OK, ou E_BUS.
 * Précondition : f->len ≤ BSK_MSG_MAX. Son seul appelant, bench_core (gardé par sim/test/garde_emission.sh), refuse
 * toute trame plus longue : PHY ne la revérifie pas (aucune garde en aval de la barrière). */
bsk_err_t bsk_phy_send(const bsk_frame_t *f);

/* Pose BODY_CS hors de toute émission (la poignée de main), lignes pilotées (bsk_phy_lines). */
void bsk_phy_body_cs(bool high);

/* Le niveau de LENS_CS, lu à l'instant de l'appel (true = haut). BSK_PHY_LENS_CS ne remonte qu'un changement : une
 * ligne déjà haute à l'init de la PHY n'en produit aucun, et un Sony resté alimenté la tient haute avant la poignée
 * de main (capturé par l'humain). Le niveau, lui, se lit toujours. */
bool bsk_phy_lens_cs(void);

/* Cadence de la VD en hertz ; 0 l'arrête. Chaque front émis remonte en BSK_PHY_VD. Arrêtée, la VD est au repos
 * de bsk_phy_lines : haute impédance, tirée bas ; en marche, elle est pilotée.
 * false : le verrou de XDETECT est posé et hz n'est pas nul, la VD reste au repos ; true sinon. L'arrêter passe
 * toujours. Si le verrou tombe pendant l'appel, la VD est remise au repos avant de rendre false.
 * La SESSION ne demande que VD_HZ ou 0 : une cadence que la carte ne sait pas produire est un bug de l'appelant, et la
 * carte s'arrête (abort, DEBUG dira reset=panic ; ticket #528) au lieu de laisser la VD arrêtée en silence. Les PHY
 * simulées ne jugent pas la cadence. */
bool bsk_phy_vd(uint16_t hz);

/* Prochain événement remonté par PHY, dans l'ordre des instants ; false s'il n'y en a aucun.
 * D2 : au démarrage, l'état rapporté est « absent », verrou posé ; un objectif présent à la mise sous tension de la
 * carte remonte donc en BSK_PHY_PRESENCE (true) 300 ms après. Non câblé, D2 lit « absent » (tirage haut) : rien ne
 * remonte jamais, et le verrou reste posé.
 * La présence n'est jamais perdue : ses changements ne passent pas par la file des autres événements, qu'une couche
 * qui ne lit pas assez vite voit déborder (le plus ancien jeté). La PHY tient l'écart entre le dernier état rendu et
 * l'état courant, et le rend ici à son rang dans l'ordre des instants : une retombée qui suit un « présent » rendu est
 * toujours rendue (à l'instant de la première) ; une insertion jamais rendue et la retombée qui la suit s'annulent.
 * Les BSK_PHY_PRESENCE rendus alternent, et le dernier donne l'état courant. Un rebond de moins de 2 ms n'est pas un
 * changement d'état : il n'en rend aucun. */
bool bsk_phy_poll(bsk_phy_event_t *ev);

/* Les rails D0 (3,3 V logique) et D1 (5 V moteur), optionnels : sans interrupteur câblé, la commande est sans
 * effet, et ce n'est pas une erreur. Tous deux coupés au démarrage.
 * false : le verrou de XDETECT est posé et `on` est vrai, le rail reste coupé ; true sinon. Couper passe toujours. Le
 * verrou est lu et le rail allumé sous la même section critique que les interruptions de XDETECT (le front,
 * l'échéance) : aucune coupure ne tombe entre les deux. */
typedef enum {
    BSK_RAIL_LOGIC,   /* D0, EN_LOGIC */
    BSK_RAIL_MOTOR,   /* D1, EN_MOTOR */
} bsk_rail_t;

bool bsk_phy_rail(bsk_rail_t rail, bool on);

/* TXD et BODY_CS, les lignes que la carte émet vers l'objectif hors de la VD.
 *   - false, relâchées : haute impédance, tirées bas ; TXD n'est plus routée à l'UART ;
 *   - true, pilotées : TXD routée à l'UART, haute au repos ; BODY_CS pilotée basse.
 * La VD a sa commande, bsk_phy_vd : arrêtée, elle est au même repos. Pourquoi : une ligne tenue haute vers un
 * objectif non alimenté l'alimente par ses diodes de protection, et sur la carte cible les rails sont commutés.
 * Les trois sont au repos au démarrage (bsk_phy_init). SESSION, propriétaire de l'alimentation, les met au repos
 * avant de couper les rails, et ne les pilote qu'une fois les deux rails établis (rail logique, 50 ms, rail moteur,
 * 50 ms). Lignes relâchées, bsk_phy_body_cs et bsk_phy_send n'ont sur la carte aucun effet électrique ; les piloter
 * pose BODY_CS basse, quoi qu'on lui ait demandé entre-temps.
 * false : le verrou de XDETECT est posé et `drive` est vrai, les lignes restent au repos ; true sinon. Les relâcher
 * passe toujours. Si le verrou tombe pendant l'appel (le routage de TXD n'est pas fait sous section critique), les
 * lignes sont remises au repos avant de rendre false. */
bool bsk_phy_lines(bool drive);

/* Ce que la PHY compte pour `DEBUG` (PROTOCOL.md § 3), sans rien changer à ce qu'elle fait. À zéro au démarrage de la
 * carte.
 *   - evq_drop : les événements jetés parce que la file était pleine (le plus ancien, au profit du nouveau) ;
 *   - tx_wait_us, tx_timeout : la plus longue attente de fin d'émission de bsk_phy_send, en µs, sur la pile de
 *     son appelant, et les échéances de 100 ms de cette attente atteintes.
 * La carte seulement : les PHY de test (sim/phy_sim.c, sim/test/phy_script.c) rendent zéro. Une file simulée qui
 * déborde prouverait le simulateur, pas la carte. Lu sur la tâche de SESSION. */
typedef struct {
    uint32_t evq_drop;
    uint32_t tx_wait_us;
    uint32_t tx_timeout;
} bsk_phy_stats_t;

void bsk_phy_stats(bsk_phy_stats_t *st);

/* La carte seulement, appelée une fois par app_main : broches, UART, lignes au repos (TXD, BODY_CS et VD en haute
 * impédance, tirées bas), rails coupés, verrou de XDETECT posé et son interruption armée, puis la réception. Rien
 * n'est émis. La PHY simulée s'initialise par phy_sim_init. */
void bsk_phy_init(void);

#endif
