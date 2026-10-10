/* SOURCE: spec de l'atelier § 3.1, § 3.2, § 4.1-4.3, § 5 — le contrat hôte <-> objectif
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — en-tête partagé, déclarations seulement
 *
 * Deux boîtes aux lettres, pas des appels de fonction : l'hôte dépose une commande (une seule en vol), l'objectif
 * rend un accusé par commande et publie un instantané d'état en continu. Le vocabulaire est celui de la physique de
 * l'objectif : aucun numéro de type de message n'y apparaît, jamais. */
#ifndef BSK_CONTRACT_H
#define BSK_CONTRACT_H

#include <stdbool.h>
#include <stdint.h>

#include "bsk_err.h"

/* ── États des machines, publiés dans l'instantané (§ 3.1, § 3.2) ── */
typedef enum {
    SESSION_OFF,
    SESSION_POWERING,
    SESSION_IDENTIFYING,
    SESSION_HOMING,
    SESSION_RESTORING,
    SESSION_READY,
    SESSION_RECOVERING,  /* une perte mene ici : pas d'etat LOST */
    SESSION_FAULT,
} bsk_session_state_t;

typedef enum {
    MOTION_IDLE,
    MOTION_PLANNING,
    MOTION_COMMANDED,
    MOTION_MOVING,
    MOTION_SETTLING,
    MOTION_ARRIVED,
    MOTION_STALLED,
    MOTION_ABORTED,
} bsk_motion_state_t;

/* ── Capacités (§ 5), champ de bits de l'instantané ──
 * Les bits 2 et 4 sont vides exprès, les autres ne bougent pas. La spec demande deux drapeaux de plus
 * (« l'objectif peut refuser un message », « la fin de homing dit si elle a réussi ») sans les nommer : ils ne sont
 * pas déclarés. */
#define CAP_APERTURE          (1u << 0)  /* l'objectif accepte une consigne de diaphragme */
#define CAP_APERTURE_READBACK (1u << 1)  /* ... et la relit (sinon : commande en ecriture seule) */
#define CAP_LIMITS_REPORTED   (1u << 3)  /* les butees sont annoncees, sinon mesurees au homing */

/* ── 4.1 Commande (hôte -> objectif), une seule en vol ──
 * Les membres gardent leur valeur : un ajout va en fin d'énumération. */
typedef enum {
    CMD_ATTACH,          /* mise sous tension, identification, homing, restauration */
                         /* commande manuelle (p1) : en usage normal, la session demarre seule */
    CMD_DETACH,          /* arret propre, mise hors tension -- commande manuelle (p0) */
    CMD_HOME,            /* jamais deposee : le homing n'a lieu qu'au demarrage ; gardee pour que les membres
                            suivent la spec et gardent leur valeur */
    CMD_FOCUS_GOTO,      /* position absolue, en pas objectif */
    CMD_FOCUS_MOVE,      /* deplacement relatif, en pas objectif */
    CMD_FOCUS_STOP,      /* passe toujours, meme avec une commande en vol -- n'arrete pas un homing, celui du pilote
                            (HOMING) ni le 0x10 de l'init (IDENTIFYING) : E_BUSY (er busy home) ;
                            arrete le retour automatique a la marque */
    CMD_APERTURE_SET,    /* en unites objectif, cf. capacites */
    CMD_CLEAR_FAULT,
    CMD_CLEAR_MARK,      /* servie en READY */
    CMD_SET_MARK,        /* pose la marque : a la position courante (js, le bouton du fut), ou a arg (aucune ligne de
                            HOTE ne la depose) ; servie en READY, dans les bornes du 0x06 (E_LIMIT) */
    CMD_RING_SET,        /* jamais deposee : le role de la bague suit le commutateur ; gardee pour que les membres
                            gardent leur valeur */
    CMD_LENS_CUSTOM,     /* la configuration du commutateur Custom du Samyang AF 135, rangee dans sa flash : exception
                            decidee par l'humain a « jamais d'ecriture dans la flash d'un objectif » ;
                            arg < 0 : la lire ; sinon l'ecrire, arg = haut << 4 | bas (M1, M2), chaque position 0
                            (ouverture), 1 (AF) ou 2 (MF). Servie en READY au 135 seul (E_NOCAP a tout autre objectif) ;
                            en vol de son ACK_ACCEPTED a la reponse de l'objectif ; ACK_COMPLETED : ce qu'il a
                            repondu, bsk_session_custom_data */
} bsk_cmd_op_t;

/* CMD_SET_MARK : comment `arg` distingue la position courante d'une position donnée.
 *   arg == BSK_MARK_HERE : la marque est posée à la position courante de l'objectif (`js`, le bouton du fût) ;
 *   toute autre valeur   : la marque est posée à arg, en pas objectif (aucune ligne de HOTE ne la dépose).
 * Une position d'objectif est un entier de 16 bits sur le bus (le 0x06 du 135 la publie sur deux octets) :
 * INT32_MIN n'est la position d'aucun objectif, et une position donnée garde tout le domaine des positions. */
#define BSK_MARK_HERE INT32_MIN

/* Le domaine des codes d'ouverture 256·(Av+16) que la carte convertit en f/ : de f/0,5 (Av -2, 0x0E00 ; la limite d'un
 * objectif dans l'air, et le plancher que la conversion f/ -> code avait jusqu'ici) à f/256 (Av 16, 0x2000 ; deux diaphragmes au-delà
 * du f/64 d'un objectif photographique, et f/ x10 = 2560 tient sur 16 bits), bornes comprises. Hors de lui, un code n'est
 * pas une ouverture et n'est jamais converti : une plage du 0x08 qui en contient un n'est pas retenue (ni aperture_min,
 * aperture_max, ni CAP_APERTURE), une ouverture relue hors de lui (aperture_current, 0 sans 0x05) est inconnue. */
#define BSK_AP_CODE_MIN 0x0E00u
#define BSK_AP_CODE_MAX 0x2000u

/* focus_min, focus_max de l'instantané : les bornes que l'objectif publie dans son 0x06, offsets 7-8 et 9-10
 * (petit-boutiste), relues à chaque 0x06 valide, jamais rangées. Elles sont valides comme la position
 * (position_valid) : même trame, même validité. Chacune est resserrée de 5 pas à la lecture (borne basse + 5, borne
 * haute − 5), pour ne jamais atteindre la butée ; une cible hors de [focus_min, focus_max] est refusée avant
 * émission, E_LIMIT. Aucune marge sur l'ouverture. */

/* Le seq du goto que dépose le bouton du fût (appui court), par la même boîte que l'hôte : ses accusés suivent, et
 * l'hôte y lit la fin du déplacement (move_rc) comme pour les siens. L'hôte alloue ses seq à partir de 1 : 0 n'en
 * désigne aucun. */
#define BSK_SEQ_BOARD 0u

typedef struct {
    uint32_t     seq;    /* strictement croissant, alloue par l'hote */
    bsk_cmd_op_t op;
    int32_t      arg;
} bsk_cmd_t;

/* ── 4.2 Accusé (objectif -> hôte), un par commande ──
 * ACCEPTED puis plus tard COMPLETED ou FAILED. REJECTED est immédiat et final. */
typedef enum { ACK_ACCEPTED, ACK_REJECTED, ACK_COMPLETED, ACK_FAILED } bsk_ack_result_t;

typedef struct {
    uint32_t         seq;     /* echo du seq commande */
    bsk_ack_result_t result;
    bsk_err_t        reason;  /* enum unique, § 6 */
} bsk_ack_t;

/* Le rôle de la bague ; inconnu tant que l'objectif ne publie pas son commutateur (offset 62 du 0x05). */
typedef enum { RING_UNKNOWN, RING_FOCUS, RING_APERTURE } bsk_ring_t;

/* Un fait que l'objectif publie, ou non : BSK_UNKNOWN (non publié), BSK_NO, BSK_YES. */
typedef enum { BSK_UNKNOWN, BSK_NO, BSK_YES } bsk_tri_t;

/* ── 4.3 Instantané d'état (objectif -> hôte), publié en continu ── */
typedef struct {
    uint8_t  session_state;  /* bsk_session_state_t, § 3.1 */
    uint8_t  motion_state;   /* bsk_motion_state_t, § 3.2 */

    bool     position_valid; /* vrai uniquement en READY */
    int32_t  focus_position; /* pas objectif, lu de l'objectif */
    int32_t  focus_min, focus_max;   /* les bornes du 0x06, valides comme la position (ci-dessus) */

    uint16_t aperture_current;       /* unites objectif -- 16 bits : le code Sony ; hors de BSK_AP_CODE_MIN..MAX, inconnue */
    uint16_t aperture_min, aperture_max;   /* dans BSK_AP_CODE_MIN..MAX avec CAP_APERTURE ; 0 sans plage */

    uint32_t lens_id_product;
    char     lens_name[65];          /* le nom que l'objectif donne lui-meme ; vide s'il n'en donne pas */
    uint32_t capabilities;           /* champ de bits CAP_*, § 5 */

    bool     mark_valid;             /* une marque existe pour cet objectif a cette focale */
    int32_t  mark_position;          /* sa position, en pas objectif ; sans objet si !mark_valid */

    bsk_err_t last_error;

    uint8_t  ring;                   /* bsk_ring_t, le role que le commutateur donne */
    uint8_t  mf;                     /* bsk_tri_t, commutateur en position MF (offset 62, bits 0-1 : 03),
                                        AF (01) ; inconnu sans offset 62 ou pour une autre valeur */
    uint8_t  oss;                    /* bsk_tri_t, stabilisateur en marche (offset 64, bit 4) */

    /* Les gestes de marque de l'utilisateur, modulo 2^16, que la LED joue : comptés par chaque CMD_SET_MARK et
     * CMD_CLEAR_MARK servie (le bouton du fût compris), jamais sur un zoom ni une relecture, que mark_valid et
     * mark_position ne distinguent pas ; jamais oubliés avec la session. Ni `t` ni la couche HOTE ne les lisent. */
    uint16_t mark_sets;              /* js, le bouton */
    uint16_t mark_clears;            /* jx */

    /* Les arrets non confirmes d'un deplacement suivi (une commande de mouvement en vol, l'hote ou le bouton du fut),
     * modulo 2^16 : le 0x1C qui a fini ce deplacement (q, STALLED) ni accuse ni suivi d'immobilite apres son troisieme
     * envoi. Jamais oublies avec la session. La couche HOTE en lit le changement (`g`, unconfirmed). */
    uint16_t stop_unconfirmed;
} bsk_status_t;

#endif
