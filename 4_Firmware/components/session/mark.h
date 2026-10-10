/* SOURCE: spec de l'atelier § 9 — la marque, et le bouton du fût
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — en-tête privé de la SESSION, inclus par session.c, mark.c, cmd.c et restore.c
 *
 * La marque de l'objectif et de la focale courants : sa clé, sa valeur relue du magasin (bsk_store.h), posée ou effacée
 * sur un geste de l'utilisateur seulement ; le sens du dernier changement de position vu au 0x06 ; le bouton du fût,
 * décidé au relâchement. Ce que le bouton déclenche est décidé par cmd.c, qui garde l'admission. */
#ifndef BSK_SESSION_MARK_H
#define BSK_SESSION_MARK_H

#include <stdbool.h>
#include <stdint.h>

#include "bsk_contract.h"
#include "bsk_journal.h"
#include "bsk_mark.h"

/* Au bsk_session_init : les résultats du magasin encore en attente sont jetés, puis tout part de zéro. */
void mark_init(void);

/* Une nouvelle session : ni clé, ni marque, ni sens, ni appui en cours. Ce qui est rangé ne change pas. */
void mark_forget(void);

/* Une trame reçue, décodée par lens_rx : son type. Le sens au 0x06 ; la clé aux 0x05, 0x07, 0x08, relue si elle change. */
void mark_frame(uint8_t type);

/* Les résultats du magasin, relevés à chaque pas ; une lecture que la file a refusée est redéposée. */
void mark_poll(void);

/* CMD_SET_MARK (arg : BSK_MARK_HERE ou la position) et CMD_CLEAR_MARK. E_NOCAP sans clé (pas d'identité), E_LIMIT sans
 * 0x06 ou hors des bornes, E_BUSY si la file du magasin est pleine : rien n'est changé. */
bsk_err_t mark_set(int32_t arg);
bsk_err_t mark_clear(void);

/* La marque de la clé courante, relue ou posée : vrai et elle (le sens compris, que l'instantané ne publie pas, pour
 * RESTORING), faux sans elle. */
bool mark_get(bsk_mark_t *m);

/* La clé courante, "" sans elle (pas d'identité, ou le 0x05 arrêté) : RESTORING la compare à celle de son départ. */
const char *mark_key(void);

/* Le sens du dernier changement de position vu au 0x06 dans la session (bsk_approach_t), inconnu sans lui : RESTORING le
 * compare au sens de l'arrivée quand la position est déjà sur la marque. */
uint8_t mark_dir(void);

/* Les marques posées et effacées par l'utilisateur depuis bsk_session_init, modulo 2^16 : chaque mark_set et mark_clear
 * qui rend E_OK, et rien d'autre (ni zoom, ni relecture, ni oubli de session). */
uint16_t mark_sets(void);
uint16_t mark_clears(void);

/* La lecture de la clé courante est déposée (ou à redéposer) et pas encore rendue. Faux sans clé : « pas encore relue »
 * n'est pas « absente », que mark_get dit une fois la lecture rendue. */
bool mark_loading(void);

/* Un 0x05 reçu à t : vrai au relâchement du bouton (offset 64, bit 3), avec sa durée (*press) et l'état de SESSION à
 * l'appui (*at) ; `state` : l'état de SESSION maintenant. Un appui commencé hors de READY est oublié par le superviseur,
 * qui le lit dans *at. */
bool mark_button(uint64_t t, uint8_t state, bsk_btn_t *press, uint8_t *at);

#endif
