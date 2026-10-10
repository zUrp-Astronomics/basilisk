/* SOURCE: PROTOCOL.md § 3 (`LOG`, les lignes `* …`) — spec de l'atelier § 4.5.5, le journal
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — déclarations seulement ; implémenté par components/journal/journal.c
 *
 * Le journal observe toutes les couches et ne décide rien. Toujours compilé, éteint au démarrage : `LOG ON`, `LOG ALL`
 * (les trames périodiques en plus), `LOG OFF`. Ses lignes `* rx` et `* tx` portent les trames telles qu'elles passent
 * sur le fil ; la forme de chaque ligne est celle de PROTOCOL.md § 3, `<t>` en millisecondes, sans fin de ligne ici.
 *
 * 30 lignes par seconde au plus (journal.c dit lesquelles y échappent) et un anneau de 8 Ko : une ligne au-delà du
 * plafond ou qui ne tient pas dans l'anneau est perdue et comptée (`dropped` de `DEBUG`). Rien ici ne bloque : l'anneau est
 * vidé par une autre tâche (bsk_journal_take ; sur la carte, host_usb.c).
 *
 * Les tâches : toutes les fonctions sauf bsk_journal_take, bsk_journal_gen et bsk_journal_lost sont
 * appelées par la tâche de SESSION (la couche HOTE y traite aussi les lignes USB, `LOG` compris) ;
 * bsk_journal_take par la tâche qui vide l'anneau, seule : l'anneau a un producteur et un consommateur.
 * bsk_journal_gen et bsk_journal_lost (atomique) par les tâches qui écrivent sur l'USB. */
#ifndef BSK_JOURNAL_H
#define BSK_JOURNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsk_err.h"
#include "bsk_mark.h"
#include "bsk_phy.h"

/* L'horloge des lignes émises sans instant (tx, transitions), en µs ; NULL : 0. Sur la carte,
 * esp_timer_get_time ; en simulation, l'horloge virtuelle. */
void bsk_journal_init(uint64_t (*clock_us)(void));

/* LOG ON (on, !all), LOG ALL (on, all), LOG OFF (!on). Éteindre change la génération : une ligne produite
 * avant ne sort plus (bsk_journal_gen). */
void bsk_journal_set(bool on, bool all);
bool bsk_journal_on(void);
bool bsk_journal_all(void);

/* Les observations. Sans effet journal éteint. Sous `LOG ON`, les trames périodiques sont tues : reçues 0x02, 0x05, et
 * 0x06 sans accusé derrière lui ; émises 0x03, et 0x04 sans consigne 0x1D. Une erreur n'est pas périodique.
 * La page ne décode en trame qu'une ligne `* rx <t> [+<off>/<len>[#<id>] ]<hex>` : dans une erreur, `err=`, juste après
 * l'instant, l'en écarte, et ses octets rejetés ne sont pas pris pour une trame. */
void bsk_journal_rx(const bsk_frame_t *f, uint64_t t_us);
void bsk_journal_tx(const bsk_frame_t *f);
void bsk_journal_error(bsk_err_t e, const bsk_phy_raw_t *raw, uint64_t t_us);   /* raw : ce que PHY a rejeté (len 0 : rien) */
void bsk_journal_session(uint8_t state, bsk_err_t last_error);

/* Une trame que bench_core a refusée (rien n'est émis), au moment du refus : `* refused <t> <hex>`, la trame telle qu'elle
 * serait partie, comme `* tx`. Retenue entre le 0x03 et le 0x04 et refusée derrière le 0x04, personne n'attend l'issue de
 * son émission : cette ligne et le compteur de bench_core (`refused` de `DEBUG`) sont tout ce qui en reste. */
void bsk_journal_refused(const bsk_frame_t *f);

/* La fin de la poignée de main ; up_us : la levée de LENS_CS après BODY_CS haute (sans objet pour BSK_HS_NEVER_HIGH). */
typedef enum { BSK_HS_OK, BSK_HS_NEVER_HIGH, BSK_HS_STUCK_HIGH } bsk_hs_t;
void bsk_journal_handshake(bsk_hs_t r, uint64_t up_us);
void bsk_journal_motion(uint8_t state);
void bsk_journal_lens(uint8_t off, uint8_t v);
void bsk_journal_ring(bool aperture);
void bsk_journal_gesture(int32_t pulses, uint32_t thirds, uint32_t frames, uint32_t capped);
void bsk_journal_body08(uint8_t flags, bool samyang);        /* le 0x08 de l'init envoyé, une fois par init */

/* Le bouton du fût à son relâchement ; why : NULL, ou le jeton de `why=`. */
typedef enum { BSK_BTN_SHORT, BSK_BTN_LONG, BSK_BTN_HELD } bsk_btn_t;
typedef enum { BSK_BTN_OK, BSK_BTN_ER, BSK_BTN_IGNORE } bsk_btn_result_t;
void bsk_journal_btn(bsk_btn_t press, bsk_btn_result_t r, const char *why);

/* La marque sous la clé `key` ; m : la marque posée (set), NULL sinon (clear, store=er). Seuls les gestes de
 * l'utilisateur et leurs échecs : une relecture (changement d'objectif ou de focale) n'écrit rien. */
typedef enum { BSK_MARK_SET, BSK_MARK_CLEAR, BSK_MARK_STORE_ER } bsk_mark_ev_t;
void bsk_journal_mark(bsk_mark_ev_t ev, const char *key, const bsk_mark_t *m);

/* L'entrée en RESTORING (via < 0 : le trajet direct), la fin d'un démarrage sans RESTORING (m : NULL, ou la
 * marque hors des bornes), la sortie de RESTORING ; why : le jeton de `skip=` ou de `end=`. */
void bsk_journal_restore(const bsk_mark_t *m, int32_t x, int32_t via);
void bsk_journal_restore_skip(const char *why, const bsk_mark_t *m);
void bsk_journal_restore_end(const char *why);

/* Un changement de présence rendu par la PHY, à son instant (t_us : l'interruption, pour une retombée). */
void bsk_journal_xdetect(bool present, uint64_t t_us);

/* Un rebond de XDETECT rendu par la PHY (BSK_PHY_BOUNCE), à son instant. */
void bsk_journal_xdetect_bounce(uint64_t t_us);

/* Le message de type `type` renvoyé, son `n`-ième envoi, à cause de `why` ; puis, s'il n'aboutit pas,
 * l'abandon. */
void bsk_journal_resend(uint8_t type, uint8_t n, bsk_err_t why);
void bsk_journal_giveup(uint8_t type, bsk_err_t why);

/* La tâche qui vide l'anneau : la prochaine ligne dans `buf` (sans fin de ligne), sa longueur, 0 s'il n'y
 * en a pas ; `gen` : la génération où elle a été produite. Elle ne l'écrit que si bsk_journal_gen() vaut
 * encore `gen` au moment d'écrire, sous le verrou de sortie que prend aussi la réponse à `LOG OFF` : rien
 * d'avant `LOG OFF` ne sort après son `ok`. Une écriture ratée est comptée par bsk_journal_lost ; sous LOG ALL, les
 * écritures ratées sont dites par la ligne `* lost <n>`, que bsk_journal_take rend avant la ligne suivante de l'anneau,
 * sous la génération où elles ont été comptées. */
size_t  bsk_journal_take(char *buf, size_t cap, uint8_t *gen);
uint8_t bsk_journal_gen(void);
void    bsk_journal_lost(void);

/* Lignes perdues depuis le démarrage : plafond, anneau plein, écriture ratée. */
uint32_t bsk_journal_dropped(void);

/* Les noms que le journal et la ligne `t` partagent (PROTOCOL.md § 5) : l'état de SESSION en
 * minuscules (`off`, `powering`…), l'état de MOUVEMENT, et le jeton d'une erreur, son nom en minuscules
 * sans `E_` (`timeout`, `home_failed`…). Chacun reçoit une valeur de son énumération. */
const char *bsk_state_name(uint8_t state);
const char *bsk_motion_name(uint8_t state);
const char *bsk_err_token(bsk_err_t e);

#endif
