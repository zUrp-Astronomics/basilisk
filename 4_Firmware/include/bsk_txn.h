/* SOURCE: spec de l'atelier § 3.3 — TRANSACTION, propriétaire du bus
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — déclarations seulement ; implémenté par components/txn/txn.c
 *
 * TRANSACTION : la boucle 0x03/0x04, les messages seuls et la requête en vol, sur le bus. */
#ifndef BSK_TXN_H
#define BSK_TXN_H

#include <stdbool.h>
#include <stdint.h>

#include "bsk_bench_core.h"
#include "bsk_err.h"
#include "bsk_phy.h"

/* TRANSACTION possède le bus et ne décide d'aucune politique. Elle gère trois choses :
 *   - la boucle 0x03/0x04 : une fois lancée, la paire part une fois par trame VD, sous un même numéro de séquence. Ce
 *     n'est pas une transaction. Elle part à la phase du NEX-7, le 0x03 à VD + 8,6 ms, le 0x04 à VD + 10,1 ms : deux
 *     échéances (bsk_txn_next), servies par bsk_txn_tick. Une trame de la paire n'est émise que si le pas qui la sert a
 *     commencé au plus 2 ms après son échéance (PAIR_WINDOW_US, txn.c ; l'instant du début du pas, bsk_txn_real) ;
 *     sinon elle est jetée avec sa paire, sans rattrapage (bsk_txn_late). Rien ne borne l'instant de l'émission
 *     lui-même : le temps passé dans le pas avant elle (les événements traités, les attentes de fin d'émission des
 *     trames émises avant elle) n'est pas compté. Chaque front de VD repart à zéro. L'appelant DOIT passer par
 *     bsk_txn_tick avant chaque événement de PHY plus tardif : la paire d'un front est servie ou jetée avant que le
 *     front suivant programme la sienne ; des fronts datés avant le début du pas, seul le plus récent peut être servi
 *     (à 60 Hz, la paire d'un front plus ancien a plus de 2 ms de retard). Rien d'autre ne part entre le 0x03 et le
 *     0x04 : une émission demandée dans l'intervalle (bsk_txn_send, bsk_txn_request) est retenue et part juste
 *     derrière le 0x04, ou derrière sa place s'il est jeté. Son contenu vient de l'étage du dessus, que TRANSACTION
 *     n'interprète pas : les champs du 0x03 et du 0x04 qu'il pose, un message accroché à la prochaine trame 0x04 qui
 *     part, et le compte rendu de chaque paire partie ;
 *   - un message de classe 1 confié seul (bsk_txn_send), sans réponse attendue ;
 *   - au plus une requête ponctuelle en vol, de classe 2 : elle se termine par sa réponse (la première
 *     trame reçue dont le premier message est du type demandé) ou son échéance (E_TIMEOUT). Une erreur de réception
 *     (E_FRAMING, E_BUS de PHY) ne coûte que la trame qu'elle touche : la requête attend toujours sa réponse jusqu'à
 *     son échéance. Seule son émission ratée la termine avant (E_BUS, E_FORBIDDEN). Aucun réessai ici : le réessai
 *     est une politique, il vit dans SESSION.
 * Chaque trame passe par bench_core (bsk_bench_send), avec la déclaration de l'objectif que
 * SESSION lui a faite (bsk_txn_samyang). Un refus (E_FORBIDDEN) termine la requête comme un échec.
 *
 * États de la requête : IDLE, WAIT, DONE, FAILED. TX et RX de la spec n'ont pas d'état
 * ici : l'émission de PHY rend la main une fois faite (ou programmée, en simulation), et PHY ne remonte
 * que des trames entières (bsk_phy.h). Il n'y a donc jamais « trame en émission » ni « réception en
 * cours » à ce niveau.
 *
 * Un seul appelant, la SESSION (components/session, MOUVEMENT compris), sur une seule tâche : aucun verrou. La seule
 * file est celle des émissions retenues entre le 0x03 et le 0x04, qui partent derrière le 0x04. */

typedef enum {
    TXN_IDLE,    /* aucune requête en vol */
    TXN_WAIT,    /* requête émise, réponse attendue avant l'échéance */
    TXN_DONE,    /* réponse reçue : bsk_txn_reply() */
    TXN_FAILED,  /* échec : bsk_txn_error() dit pourquoi */
} bsk_txn_state_t;

/* SESSION déclare l'objectif (un Samyang reconnu, le 135, ou un autre) : transmis à bench_core avec chaque trame
 * (bsk_bench_core.h). */
void bsk_txn_samyang(bsk_bench_lens_t lens);

/* Émet `msg` (classe 2) et attend un message du même type pendant `timeout_ms`, à partir de son émission.
 * Elle remplace la requête précédente, quel que soit son état : SESSION n'en émet une qu'après avoir lu
 * la fin de la précédente, ou après l'avoir oubliée (nouvelle session). Rend l'état atteint : WAIT, ou
 * FAILED si l'émission a échoué (E_FORBIDDEN de bench_core, E_BUS de PHY). Le numéro de séquence
 * continue d'une session à l'autre. Entre le 0x03 et le 0x04 de la boucle : WAIT, la
 * requête retenue part derrière le 0x04 (son échéance comptée de là ; une émission ratée la met alors en FAILED) ; aucune
 * trame ni erreur ne la termine avant ; FAILED, E_BUSY, si la file des retenues est pleine. */
bsk_txn_state_t bsk_txn_request(const uint8_t *msg, uint16_t len, uint32_t timeout_ms, uint64_t now_us);

/* Émet `msg` seul, en classe 1, sans réponse attendue ; le numéro de séquence avance s'il est parti. Rend E_OK, ou
 * l'échec de l'émission (E_FORBIDDEN de bench_core, E_BUS de PHY). La requête en vol n'en est pas touchée. Entre le 0x03
 * et le 0x04 de la boucle : retenu, il part derrière le 0x04, et E_OK le dit retenu, pas parti (un échec
 * de son émission n'est alors rendu à personne) ; E_BUSY si la file des retenues est pleine. */
bsk_err_t bsk_txn_send(const uint8_t *msg, uint16_t len);

/* La boucle. Son contenu est tenu ici tel que l'étage du dessus l'a posé, boucle arrêtée comprise : c'est à lui de
 * l'oublier avec la session. Arrêtée, la paire en attente ne part pas, ni son 0x03 ni son 0x04, et le compte rendu n'est
 * pas donné ; rien de ce qui attendait son 0x04 ne part : une émission retenue est perdue, une requête retenue finit en
 * FAILED, E_ABORTED, sans avoir été émise. */
void bsk_txn_loop(bool on);
bool bsk_txn_loop_on(void);

#define BSK_TXN_LOOP03_LEN 21u                                       /* le 0x03, octet de type compris */
#define BSK_TXN_LOOP04_LEN 14u                                       /* le 0x04, octet de type compris */
#define BSK_TXN_ATTACH_MAX (BSK_MSG_MAX - BSK_TXN_LOOP04_LEN)        /* ce qui tient derrière le 0x04 dans sa trame */

/* Pose dans le 0x03 de la boucle les bits `mask` de l'offset `off` du message (octet de type exclu :
 * off < BSK_TXN_LOOP03_LEN - 1) à la valeur de `bits` ; les autres bits ne changent pas. Tenus pour chaque paire
 * suivante, jusqu'à ce que l'étage du dessus les change. */
void bsk_txn_loop03(uint8_t off, uint8_t mask, uint8_t bits);

/* La même chose pour le 0x04 de la boucle (off < BSK_TXN_LOOP04_LEN - 1, octet de type exclu). */
void bsk_txn_loop04(uint8_t off, uint8_t mask, uint8_t bits);

/* Accroche `msg` (len <= BSK_TXN_ATTACH_MAX, copié) derrière le prochain 0x04 de la boucle, dans la même trame, sous
 * le numéro du 0x03 (y compris celui d'une paire dont le 0x03 est déjà parti). Il part une fois : le 0x04 qui le porte
 * le retire quand il part. Une paire qui ne part pas (une trame hors de sa fenêtre, une émission ratée, le 0x04 parti
 * peut-être en partie) le garde, tel quel, pour la suivante : il doit donc pouvoir partir deux fois sans effet de plus
 * (le 0x1D, une position absolue). Un message pas encore parti est remplacé ; len 0 le retire (msg peut être NULL). */
void bsk_txn_attach(const uint8_t *msg, uint16_t len);

/* Le message du dernier bsk_txn_attach a-t-il été porté par un 0x04 dont l'émission a été tentée et a raté ? Ce 0x04 a pu
 * partir quand même (E_BUS : l'attente de fin d'émission dépassée), et la paire suivante porte le message de nouveau :
 * l'objectif a pu le recevoir deux fois. Vrai jusqu'au bsk_txn_attach suivant, même une fois le message parti. Faux pour
 * une paire qui ne l'a pas tenté (son 0x04 jeté hors de sa fenêtre, ou son 0x03 raté) : c'est un report sans émission.
 * Un compte rendu seulement : TRANSACTION n'en change rien. */
bool bsk_txn_attach_failed(void);

/* Le compte rendu de chaque paire partie (ses deux trames émises), donné à l'étage du dessus juste après son 0x04 et ce
 * qui l'attendait, avant que le front VD suivant soit traité ; une paire qui ne part pas n'en donne aucun. L'observateur peut poser les
 * champs, accrocher et émettre (bsk_txn_send). Un seul observateur ; NULL, aucun. */
typedef void (*bsk_txn_pair_fn)(void);
void bsk_txn_loop_observer(bsk_txn_pair_fn fn);

/* Un événement de PHY, après bsk_txn_tick à son instant : BSK_PHY_VD programme la paire si la boucle tourne ;
 * BSK_PHY_FRAME termine la requête en vol s'il en est la réponse ; BSK_PHY_ERROR n'y change rien (la requête attend son
 * échéance). */
void bsk_txn_on_event(const bsk_phy_event_t *e);

/* Les trames de la paire échues à `now_us`, puis l'échéance : WAIT et `now_us` atteint l'échéance ->
 * FAILED, E_TIMEOUT. */
void bsk_txn_tick(uint64_t now_us);
uint64_t bsk_txn_deadline(void);            /* UINT64_MAX hors WAIT, ou la requête retenue */
uint64_t bsk_txn_next(void);                /* la plus proche de l'échéance et des trames de la paire */

/* La fenêtre de la paire se juge contre l'instant réel du pas. Une SESSION retenue rattrape les échéances passées à
 * l'instant de l'échéance même (`now_us` de bsk_txn_tick), si bien que le retard ne se lit que contre l'instant réel de
 * son pas, que l'appelant donne par bsk_txn_real au début de chaque pas (sur la carte, l'horloge lue par app_main ; 0,
 * jamais donné : aucune trame en retard). bsk_txn_late, pour DEBUG, un compteur, pas un rapport : les paires jetées
 * parce qu'une de leurs trames était hors de sa fenêtre (`n`) et le plus grand retard constaté en les jetant (`max_us`,
 * en µs), depuis bsk_txn_late_clear (au démarrage de la carte, par bsk_session_init) ; à zéro au démarrage. */
typedef struct {
    uint32_t n;
    uint32_t max_us;
} bsk_txn_late_t;

void bsk_txn_real(uint64_t now_us);
bsk_txn_late_t bsk_txn_late(void);
void bsk_txn_late_clear(void);

bsk_txn_state_t bsk_txn_state(void);
bsk_err_t bsk_txn_error(void);              /* la cause, en FAILED */
const bsk_frame_t *bsk_txn_reply(void);     /* la réponse, en DONE */
void bsk_txn_ack(void);                     /* DONE ou FAILED lus : IDLE */

#endif
