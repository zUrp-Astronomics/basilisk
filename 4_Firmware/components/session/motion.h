/* SOURCE: spec de l'atelier § 3.2 — MOUVEMENT
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — en-tête privé de la SESSION, inclus par session.c, motion.c, cmd.c et restore.c
 *
 * MOUVEMENT : ses états (bsk_motion_state_t), le démarrage constaté, l'immobilité, STALLED, ABORTED, et la commande de
 * mouvement en vol, tenue jusqu'à son accusé final. Il lit lens_rx.h, ne décode aucune trame, et émet par drive.h. */

/* Ordre d'appels : la boîte de commandes (cmd.c) garde l'admission (une commande en vol, q passe toujours) et n'appelle
 * motion_command et motion_stop qu'en READY (motion_stop aussi en RESTORING) ; le superviseur (session.c) signale la
 * sortie de READY par motion_leave. */
#ifndef BSK_MOTION_H
#define BSK_MOTION_H

#include <stdbool.h>
#include <stdint.h>

#include "bsk_session.h"

/* Fourni par session.c : la file des accusés (bsk_session_ack), où MOUVEMENT rend ceux de ses commandes. */
void session_ack(uint32_t seq, bsk_ack_result_t r, bsk_err_t why);

/* Tout à zéro, rien en vol : au bsk_session_init, avant le premier motion_forget. */
void motion_init(void);

/* Une nouvelle session : IDLE (journalisé s'il change), plus d'échéance, la consigne et le bit AF oubliés (drive_forget).
 * La commande en vol a été close avant, la boucle arrêtée. */
void motion_forget(void);

/* L'état publié ; une commande de mouvement en vol, jusqu'à son accusé final. */
bsk_motion_state_t motion_state(void);
bool motion_busy(void);

/* Les arrêts non confirmés d'un déplacement suivi, modulo 2^16 (bsk_status_t, stop_unconfirmed) : l'abandon du 0x1C qui
 * a fini une commande de mouvement en vol, par q (motion_stop, et un q suivant pendant sa surveillance) ou par STALLED
 * (motion_expire). Ni un q sans commande en vol, ni STALLED du retour à la marque. Jamais remis à zéro avec la session. */
uint16_t motion_unconfirmed(void);

/* READY, rien en vol : CMD_FOCUS_GOTO ou CMD_FOCUS_MOVE, refusée hors des bornes (E_LIMIT), sinon acceptée et déposée. */
void motion_command(const bsk_cmd_t *c, uint64_t now);

/* RESTORING : un goto que la carte fait elle-même, la cible calculée par restore.c, déposé comme celui d'une commande
 * (COMMANDED … ARRIVED, STALLED), sans commande en vol ni accusé. restore.c lit sa fin dans motion_state. La cible est
 * dans les bornes du 0x06 : il l'a vérifié. */
void motion_restore(uint16_t target, uint64_t now);

/* READY, RESTORING : CMD_FOCUS_STOP, le 0x1C, à now ; un mouvement en cours est ABORTED, que le 0x1C soit parti ou non.
 * Rend le résultat de drive_stop. E_OK (parti, ou retenu jusqu'au 0x04 : bsk_txn.h), l'arrêt est surveillé (motion.c) :
 * sans accusé 1C ni immobilité en 1,5 s, le même 0x1C, trois envois au plus ; un goto déposé ou la session oubliée
 * l'éteignent. */
bsk_err_t motion_stop(uint64_t now);

/* Sortie de READY : la commande en vol, s'il y en a une, rend ACK_FAILED avec why (E_ABORTED ou E_LOST). */
void motion_leave(bsk_err_t why);

/* Une trame reçue, décodée par lens_rx : son type ; now, l'instant de la SESSION. */
void motion_frame(uint8_t type, uint64_t now);

/* L'échéance du mouvement (UINT64_MAX hors mouvement), ou de la surveillance de l'arrêt ; échue à now : vrai, et le
 * mouvement est STALLED, le 0x1C émis et surveillé comme celui de motion_stop, ou, en COMMANDED avant son troisième envoi,
 * le même 0x1D renvoyé avec une nouvelle échéance ; ou, l'arrêt surveillé encore non confirmé, le même 0x1C, ou
 * l'abandon. L'arrêt confirmé (accusé 1C ou immobilité) se juge à la réception d'un 0x05 ou d'un 0x06, dans motion_frame,
 * qui éteint la surveillance : rien ici. */
uint64_t motion_due(void);
bool motion_expire(uint64_t now);

#endif
