/* SOURCE: PROTOCOL.md ; spec de l'atelier § 4.5 — la couche HOTE
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — en-tête partagé ; host.c (C pur) traduit, host_usb.c porte l'USB de la carte ;
 *         ticket #528 : les marges de pile des tâches, pour DEBUG (bsk_host_stacks)
 *
 * La couche HOTE parle PROTOCOL.md sur l'USB et le contrat (bsk_contract.h) avec SESSION : une ligne reçue devient
 * une commande déposée (bsk_session_command) et son premier accusé, ou une lecture de l'instantané, puis une ligne
 * rendue. Elle ne voit jamais un numéro de type, hors du journal (bsk_journal.h).
 *
 * Les tâches : bsk_session_command doit être appelée sur la tâche de bsk_session_step (bsk_session.h).
 * bsk_host_line, bsk_host_later et bsk_host_observe le sont donc aussi : sur la carte, la boucle de app_main les
 * appelle après chaque pas de SESSION (bsk_host_usb_service). La tâche USB ne fait que cadrer les lignes et écrire
 * les réponses ; elle passe chaque ligne à la tâche de SESSION par une boîte aux lettres et n'appelle jamais la
 * session. */
#ifndef BSK_HOST_H
#define BSK_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Une réponse tient toujours dans ce tampon (bien moins : les plus longues, `DEBUG` et `t`, font quelques centaines
 * de caractères ; `DEBUG`, tous ses nombres à 10 chiffres, en fait moins de 400). */
#define BSK_HOST_REPLY_MAX 1536u

/* `version` : la version du firmware de la carte, PROJECT_VER de 4_Firmware/CMakeLists.txt, sa seule source, rendue
 * telle quelle par `v` ; `reset` : la cause du dernier redémarrage de la carte (`DEBUG` → reset=). Les deux chaînes
 * vivent aussi longtemps que la carte. */
void bsk_host_init(const char *version, const char *reset);

/* Lit les accusés et l'instantané pour tenir last_op et move_rc : à appeler après chaque
 * bsk_session_step, sur la même tâche. Les arrivées de la session et les accusés finaux se succèdent dans
 * l'ordre où cet appel les voit. Entre deux pas rien ne change : bsk_host_line n'a pas à le rappeler, et une
 * commande qu'elle dépose est suivie de sa propre lecture des accusés. */
void bsk_host_observe(void);

/* Une ligne reçue, telle que le transport l'a cadrée : sans sa fin (`\n`, ou le `#` d'une ligne Moonlite `:…`).
 * La réponse est dans `out` ; la valeur rendue est ce que le transport écrit juste après elle, tel quel (HOTE décide
 * du terminateur, le transport n'ajoute rien) :
 *   - "\n" après une réponse du protocole Basilisk/Pinefeat (PROTOCOL.md § 1, une ligne par commande) ;
 *   - "" après une réponse Moonlite, octets exacts sans fin de ligne (`4000#`, `10` : PROTOCOL.md § 4) ;
 *   - NULL : rien n'est écrit, ni `out` ni fin (une commande Moonlite sans réponse, `:SN…#`, `:FG#`, `:FQ#`…).
 *   - BSK_HOST_LATER : la réponse attend l'objectif (`CUSTOM`, PROTOCOL.md § 3.1), ci-dessous.
 * `cap` ≥ BSK_HOST_REPLY_MAX. */
const char *bsk_host_line(const char *line, char *out, size_t cap);

/* Une réponse différée. Quand bsk_host_line rend BSK_HOST_LATER (comparé par adresse), rien n'est écrit, et le
 * transport ne lui passe aucune autre ligne (une réponse par ligne, PROTOCOL.md § 1) : il appelle bsk_host_later après chaque
 * bsk_host_observe, sur la même tâche, tant qu'elle rend BSK_HOST_LATER. Elle rend ensuite la réponse dans `out` et le
 * terminateur, comme bsk_host_line. L'attente est bornée : la session finit toujours la commande (la réponse de l'objectif,
 * son échéance, ou la sortie de READY ; bsk_session.h). */
extern const char BSK_HOST_LATER[];
const char *bsk_host_later(char *out, size_t cap);

/* Les marges de pile des tâches de la carte, pour `DEBUG` (PROTOCOL.md § 3) : la plus petite place restée libre sur la
 * pile de chaque tâche depuis le démarrage, en octets (uxTaskGetStackHighWaterMark). Appelée par bsk_host_line, sur la
 * tâche de SESSION. Fournie par la carte (host_usb.c) ; dans la suite, chaque programme qui compile host.c en fournit
 * une (sim/test/test_host.c pose les siennes, les autres prennent sim/stacks_sim.c, à zéro). */
typedef struct {
    uint32_t session, phy, usb_rx, store, journal;
} bsk_host_stacks_t;
void bsk_host_stacks(bsk_host_stacks_t *s);

/* La carte seulement (host_usb.c), appelées par app_main : l'USB série-JTAG, sa tâche de
 * lecture et celle qui vide le journal ; puis, à chaque tour de la boucle de SESSION, la ligne en attente
 * s'il y en a une. Les simulations appellent bsk_host_line directement. */
void bsk_host_usb_init(void);
void bsk_host_usb_service(void);

#endif
