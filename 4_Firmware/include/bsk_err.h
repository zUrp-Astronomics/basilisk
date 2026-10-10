/* SOURCE: spec de l'atelier § 6 — l'énumération des erreurs
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — en-tête partagé, déclarations seulement
 *
 * Une seule énumération d'erreurs pour tout le firmware. */
#ifndef BSK_ERR_H
#define BSK_ERR_H

/* Chaque erreur a deux attributions, qui ne disent pas la même chose :
 *   - qui la lève : les étages qui la produisent dans ce firmware, la flèche de l'énumération, plus bas ; un étage
 *     qui rend telle quelle l'erreur d'un autre (TRANSACTION, celle de l'émission de sa requête) est dit entre
 *     parenthèses ;
 *   - qui décide de la suite (la reprise, le refus, FAULT) : le propriétaire de ce tableau, un seul par erreur.
 * Les erreurs du bus, E_TIMEOUT, E_FRAMING et E_BUS, sont levées par PHY et TRANSACTION, qui n'en décident rien
 * (bsk_txn.h) : SESSION en décide, et lève aussi E_FRAMING. Une erreur de réception (E_FRAMING, E_BUS de PHY) ne coûte que
 * la trame qu'elle touche : aucune requête ne s'en termine, elle reste au journal (`* rx`). Une requête ne finit en échec
 * que par son échéance (E_TIMEOUT) ou son émission ratée (E_BUS, E_FORBIDDEN). E_REJECTED et E_IDENTITY ne sont levées
 * nulle part dans ce firmware.
 *
 *   erreur                       propriétaire  action
 *   E_TIMEOUT, E_FRAMING, E_BUS  SESSION       reçue (E_FRAMING, E_BUS de PHY) : au journal, rien d'autre ; la
 *                                              fin d'une requête (E_TIMEOUT, E_BUS de son émission) : décidée
 *                                              par l'étape qui l'a émise (renvoi, échec compté). Aucune
 *                                              n'alimente le détecteur de perte, qui ne lit que le silence
 *                                              des 0x05 et 0x06
 *   E_REJECTED                   MOUVEMENT     remonte à l'hôte, session inchangée
 *   E_STALL, E_LIMIT             MOUVEMENT     remonte à l'hôte, session inchangée
 *   E_HOME_FAILED                SESSION       FAULT, terminal, sans réessai — seulement là où l'objectif dit
 *                                              l'échec (10 01 chez Tamron) ; un homing Samyang au-delà de son
 *                                              échéance passe par la reprise plafonnée
 *   E_IDENTITY                   SESSION       marque invalidée ; FAULT si aucun modèle
 *   E_FORBIDDEN                  BENCH_CORE    journalisé, remonté ; jamais silencieux
 *   E_BUSY                       SESSION       refus immédiat (ACK_REJECTED) d'une commande déposée pendant
 *                                              qu'une autre est en vol, CMD_FOCUS_STOP excepté
 *   E_LOST                       SESSION       FAULT au-delà du plafond de reprises, quand la carte ne peut
 *                                              pas couper ; publié dans last_error, jeton `lost`. Aussi la
 *                                              raison, non publiée, de l'ACK_FAILED d'une commande en vol
 *                                              quand la session quitte READY sur une perte
 *   E_ABORTED                    MOUVEMENT     fin d'un mouvement arrêté par q, pas un échec ; move_rc=aborted.
 *                                              Aussi la raison, non publiée, de l'ACK_FAILED d'une commande
 *                                              en vol quand la session quitte READY sur b ou la retombée de D2
 *   E_NOCAP                      SESSION       refus immédiat (ACK_REJECTED) d'une opération ou d'une capacité
 *                                              non servie ; HOTE répond `er nocap` ; jamais dans last_error
 *
 * Ce qui dépend de la famille d'objectif devient un drapeau de capacité (bsk_contract.h), pas du code
 * conditionnel : E_REJECTED n'existe pas chez Samyang (aucun 0x02, une trame fautive est jetée en silence),
 * et un homing Samyang ne dit pas son échec (10 00 même manqué). */
typedef enum {                 /* -> qui lève l'erreur */
    E_OK = 0,
    E_TIMEOUT,      /* pas de reponse dans l'echeance      -> TRANSACTION, l'echeance de sa requete ; MOUVEMENT, la
                                                              raison journalisee de ses renvois et abandons */
    E_FRAMING,      /* reponse malformee                   -> PHY, les octets ecartes du flux, au journal seul ;
                                                              SESSION, une reponse trop courte a l'init, ou qui n'est
                                                              pas celle de CUSTOM */
    E_REJECTED,     /* l'objectif a dit non                -> levee nulle part */
    E_BUS,          /* defaut electrique detecte           -> PHY, son UART, un front perdu (au journal seul), une
                                                              emission ratee (TRANSACTION, bench_core) */
    E_STALL,        /* mouvement commande sans progression -> MOUVEMENT */
    E_LIMIT,        /* cible hors butees etablies          -> MOUVEMENT ; SESSION, la marque sans 0x06 ou hors de
                                                              ses bornes */
    E_HOME_FAILED,  /* reference mecanique inetablissable  -> SESSION */
    E_IDENTITY,     /* objectif inconnu ou different       -> levee nulle part */
    E_FORBIDDEN,    /* refuse par bench_core               -> BENCH_CORE */
    E_BUSY,         /* une commande est deja en vol        -> SESSION, aussi la file du magasin pleine ;
                                                              TRANSACTION, sa file des retenues pleine */
    E_LOST,         /* objectif perdu, plafond de reprises depasse -> SESSION */
    E_ABORTED,      /* mouvement arrete par q, pas un echec -> MOUVEMENT ; SESSION, la sortie de READY ;
                                                               TRANSACTION, une requete retenue, la boucle arretee */
    E_NOCAP,        /* operation ou capacite non servie    -> SESSION */
} bsk_err_t;

#endif
