/* SOURCE: composant session — la consigne de mise au point
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — en-tête privé de la SESSION, inclus par motion.c et session.c
 *
 * Ce que MOUVEMENT met dans la boucle de TRANSACTION : le 0x1D accroché au 0x04, le bit AF du 0x03, le 0x1C seul
 * (bsk_txn.h : un champ du 0x03, un message accroché au 0x04, le compte rendu de chaque paire, un message seul). */
#ifndef BSK_DRIVE_H
#define BSK_DRIVE_H

#include <stdbool.h>
#include <stdint.h>

#include "bsk_err.h"

/* Un 0x1D `1D lo hi 00 00` (unité 0, mode 0, extra 0) accroché à la prochaine trame 0x04 de la boucle qui part ; une
 * consigne pas encore partie est remplacée. Le bit AF du 0x03 (offset 12, bit 4) est tenu 30 paires à partir de là,
 * décomptées après chaque paire émise. Une paire qui ne part pas reporte le 0x1D, tel quel, à la suivante (bsk_txn.h). */
void drive_goto(uint16_t target);

/* Le 0x1D du dernier drive_goto a pu partir deux fois : un 0x04 qui le portait a été tenté et a raté, et la paire suivante
 * le reporte (bsk_txn_attach_failed). Un report sans émission tentée (0x04 jeté hors de sa fenêtre, 0x03 raté) n'en est
 * pas un. */
bool drive_goto_failed(void);

/* La consigne pas encore partie est retirée, le bit AF relâché, et un 0x1C part seul, en classe 1, sans réponse
 * attendue. Rend le résultat de bsk_txn_send : E_OK dit le 0x1C parti, ou, entre le 0x03 et le 0x04 de la boucle,
 * retenu jusqu'au 0x04 (bsk_txn.h). */
bsk_err_t drive_stop(void);

/* À l'arrêt de la boucle : la consigne pas encore partie et le bit AF sont oubliés, rien n'est émis ; rien d'une session
 * ne repart dans la suivante. */
void drive_forget(void);

#endif
