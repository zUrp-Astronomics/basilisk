/* SOURCE: composant session — la bague de l'objectif
 * AUTHOR: engineer
 * DATE: 2026-10-03
 * STATUS: actif — en-tête privé de la SESSION, inclus par session.c, ring.c et cmd.c
 *
 * Le rôle de la bague, focus ou ouverture : il suit le commutateur (offset 62 du 0x05) sur tout objectif qui le publie,
 * pas seulement un Samyang (décision de l'humain : un Sony en AF a lui aussi sa bague sur l'ouverture). En
 * rôle ouverture, la bague pilote la consigne d'ouverture. */

/* Piège : l'offset 60 est à la fois l'impulsion de la bague et l'octet de mouvement que lit MOUVEMENT. Sans garde, un
 * goto ferait tourner l'ouverture. Rien n'est donc compté tant qu'une commande de mouvement est en vol, puis tant que
 * l'offset 60 n'est pas revenu à 0 (après `q` ou STALLED, l'objectif peut encore publier son mouvement). « Tant que
 * MOUVEMENT n'est pas IDLE » serait faux : MOUVEMENT ne revient jamais à IDLE dans une session, la bague s'arrêterait
 * au premier goto. */
#ifndef BSK_RING_H
#define BSK_RING_H

#include <stdbool.h>
#include <stdint.h>

#include "bsk_contract.h"

/* Fournie par cmd.c : la consigne d'ouverture, bornée à la plage du 0x08 et posée pour le prochain 0x03 (le chemin de
 * CMD_APERTURE_SET) ; sans plage, rien n'est posé. Fournie par session.c : sa valeur. */
void session_aperture(int32_t code);
uint16_t session_aperture_target(void);

/* Au bsk_session_init : tout à zéro. */
void ring_init(void);

/* Une nouvelle session : rôle inconnu, bit du 0x04 retiré, le journal repart. */
void ring_forget(void);

/* Une trame reçue à t (µs), décodée par lens_rx : son type. `turn` : READY et aucune commande de mouvement en vol ; la
 * bague ne fait l'ouverture qu'avec lui, une fois l'offset 60 revenu à 0 depuis qu'il était faux. */
void ring_frame(uint8_t type, uint64_t t, bool turn);

bsk_ring_t ring_role(void);

#endif
