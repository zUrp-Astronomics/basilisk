/* SOURCE: composant session — le décodage des messages reçus de l'objectif
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — en-tête privé de la SESSION
 *
 * Ce que l'objectif a publié dans la session en cours. Seule unité qui lit les octets des 0x05, 0x06, 0x07, 0x08, 0x3F
 * et de la réponse au 0x10, longueurs minimales comprises : la SESSION lit cet état et ne décode aucun octet de trame.
 * Un champ publié de plus s'ajoute ici et dans lens_rx.c, sans toucher la SESSION. */
#ifndef BSK_LENS_RX_H
#define BSK_LENS_RX_H

#include <stdbool.h>
#include <stdint.h>

#include "bsk_phy.h"

typedef struct {
    bool     have05, have06, have07;  /* un message de ce type, assez long pour être lu, reçu dans la session ; have05 et
                                         have06 : depuis le lancement de sa boucle (lens_rx_loop), et oubliés quand leur
                                         flux s'arrête (lens_rx_stale) */
    bool     have08;                  /* une réponse au 0x08 assez longue pour la plage d'ouverture, ses deux codes dans
                                         le domaine que la carte convertit (BSK_AP_CODE_MIN..MAX) */
    uint8_t  move;                    /* 0x05, offset 60 ; 0 sans 0x05 */
    bool     have62, have64, have66;  /* un 0x05 assez long pour cet offset, reçu dans la session (have62 et have64
                                         oubliés avec have05, lens_rx_stale) */
    uint8_t  o62, o64, o66;           /* 0x05, offsets 62 (commutateur), 64 et 66 (commandes du fût), bruts,
                                         ceux du dernier 0x05 qui les porte ; 0 avant */
    uint16_t ap_ring;                 /* 0x05, offsets 17-18, l'ouverture que l'objectif publie quand le bit 0
                                         de l'offset 19 le dit (bague configurée en ouverture) ; 0 sinon */
    uint16_t ap;                      /* 0x05, offsets 0-1 : l'ouverture courante, un code 256(Av+16) */
    uint16_t ap_min, ap_max;          /* réponse au 0x08, offsets 0-1 et 2-3 : la plage, en codes ; 0 sans have08 */
    uint16_t pos;                     /* 0x06, offsets 2-3 ; 0 sans 0x06 */
    int32_t  fmin, fmax;              /* 0x06, offsets 7-8 et 9-10 : les bornes, resserrées de LIMIT_MARGIN
                                         pas chacune à la lecture (lens_rx.c) ; 32 bits : une borne publiée
                                         près de 0 ou de 65535 ne déborde pas */
    uint32_t rx06;                    /* 0x06 lus */
    bool     ack1d;                   /* la dernière trame reçue portait un accusé 1D derrière son 0x06 */
    bool     ack1c;                   /* ... un accusé 1C, de toute valeur */
    uint16_t lens_type2;              /* 0x07, offsets 9-10 ; 0 sans 0x07 (have07) */
    bool     have_id;                 /* une réponse au 0x07 de l'init d'au moins 4 octets, dans la session */
    uint8_t  id[5];                   /* l'identité de la clé de la marque : 0x07 offsets 0-1, puis 9-10 (ou
                                         l'offset 2 et 0, s'il fait moins de 12 octets, type compris), puis
                                         l'offset 0 de la réponse au 0x08 (0 sans elle). C'est la forme des
                                         clés déjà rangées sur les cartes : la changer rend leurs marques
                                         introuvables */
    uint16_t focal_nom;               /* 0x05, offsets 26-27, la focale nominale en dixièmes de mm */
    char     name[65];                /* nom 0x3F, vide s'il n'en donne pas */
} lens_rx_t;

const lens_rx_t *lens_rx(void);

/* Tout est oublié : une nouvelle session. */
void lens_rx_forget(void);

/* La boucle de la session est lancée. Un 0x05 ou un 0x06 n'est lu qu'à partir d'elle : celui de la dernière paire d'une
 * session précédente, encore en vol à l'oubli, comme celui d'un objectif déjà en flux (un F051 dont la réponse au 0x0A
 * est perdue), ne change aucun champ publié — sinon il servirait à la session suivante la position, les bornes, le sens
 * de la marque, le rôle de la bague et la clé de la marque. Lancée, elle l'est jusqu'à l'oubli : un second appel ne
 * change rien. */
void lens_rx_loop(void);

/* Une trame reçue de l'objectif : l'état en est mis à jour (un 0x05 ou un 0x06 seulement après lens_rx_loop ; un 0x07
 * jamais, lens_rx_id). Rend le type de son premier message, ou 0 pour un 0x05 ou un 0x06 qui n'est pas lu : avant
 * lens_rx_loop, ou trop court pour l'être (29 octets pour un 0x05, la focale comprise ; 12 pour un 0x06, les bornes
 * comprises). Celui-là n'est pas reçu : il ne réarme pas la perte et ne fait rien rejouer de ce que la SESSION en lit. */
uint8_t lens_rx_frame(const bsk_frame_t *f);

/* Le flux de ce type (0x05 ou 0x06) s'est arrêté : ce que la SESSION en lit avant le prochain qui est lu devient inconnu,
 * comme jamais reçu dans la session. Pour le 0x06, have06 faux, la position et les bornes à 0. Pour le 0x05, have05,
 * have62 et have64 faux (la clé de la marque, le rôle de la bague, oss), l'ouverture, les offsets 60 et 62 à 0 (lus sans
 * drapeau : l'instantané, still.c). Le reste n'est pas touché, et ce n'est pas un oubli : o64 n'est lu que derrière
 * have64 ; l'offset 66 n'est lu que par le journal de la bague, qui ne dit qu'un changement ; l'ouverture de la bague et
 * la focale ne sont lues que sur un 0x05 lu (la focale derrière have05), qui les réécrit. Les remettre à 0 ne changerait
 * rien d'observable. L'ouverture de la bague reste aussi celle que ring.c retient : au retour du 0x05, une ouverture
 * publiée différente est un changement, pris (PROTOCOL.md § 7). */
void lens_rx_stale(uint8_t type);

/* Une réponse porte ce que la séquence en lit : un 0x07 son identité, 4 octets ; un 0x08 sa plage, 6 octets ;
 * un 0x10 son octet de résultat, 2 octets ; toute autre, sa présence seule (le 0x3F aussi : un nom vide n'est pas une
 * troncature). Plus courte, la séquence la traite comme l'absence de réponse de son étape. Octets comptés type compris. */
bool lens_rx_full(const bsk_frame_t *r);

/* La réponse au 0x07 de l'init, complète (lens_rx_full) : l'identité en est lue (have_id, id, et lens_type2 s'il a au
 * moins 12 octets). Un 0x07 reçu hors de cette réponse n'est pas lu : il ne change ni l'identité publiée, ni la clé de
 * la marque, ni la reconnaissance, ni la déclaration à bench_core. */
void lens_rx_id(const bsk_frame_t *r);

/* La réponse au 0x3F : le nom en est lu (vidé s'il est invalide). Vrai si un nom valide a été lu. */
bool lens_rx_name(const bsk_frame_t *r);

/* La réponse complète à un 0x10 (lens_rx_full) dit l'échec du homing (E3). */
bool lens_rx_home_failed(const bsk_frame_t *r);

#endif
