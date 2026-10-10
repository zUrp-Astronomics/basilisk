/* SOURCE: spec de l'atelier § 3.1, § 4.1 et § 4.2 — SESSION
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — en-tête partagé, déclarations seulement ; implémenté par components/session
 *
 * SESSION possède l'alimentation de l'objectif, la validité de la position et la politique de reprise. Elle lit PHY
 * (bsk_phy_poll), commande TRANSACTION (bsk_txn.h) et publie l'instantané (bsk_status_t, bsk_contract.h).
 *
 * Les tâches : SESSION n'attend rien d'elle-même, l'appelant la fait avancer. L'émission d'une trame la retient, sur la
 * carte (l'émission de PHY, bsk_phy.h, components/phy/phy.c) : l'écriture de ses octets vers l'UART, sans échéance, puis
 * l'attente de sa fin, 100 ms au plus, mesurée dans DEBUG (tx_wait_us, tx_timeout). Sur la carte, app_main l'appelle en
 * boucle avec esp_timer_get_time() ; en simulation, le test l'appelle aux instants où PHY ou SESSION ont quelque chose à faire
 * (bsk_session_next). bsk_session_command s'appelle sur la même tâche que bsk_session_step. */
#ifndef BSK_SESSION_H
#define BSK_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#include "bsk_contract.h"

/* Les échéances, en millisecondes. */
typedef struct {
    uint32_t handshake_ms;   /* poignée de main, chaque attente de LENS_CS */
    uint32_t init_ms;        /* 0x01 (chacun de ses trois essais), 0x07, 0x0B, 0x09, 0x0D */
    uint32_t name_ms;        /* 0x3F */
    uint32_t info_ms;        /* 0x08, 0x0A (celui de l'init et celui du reset soft) */
    uint32_t init_home_ms;   /* 0x10 de l'init */
    uint32_t home_ms;        /* 0x10 du homing du pilote */
    uint32_t still_ms;       /* immobilité après le homing du pilote */
    uint32_t first05_ms;     /* premier 0x05 après l'init, sans échec */
    uint32_t probe05_ms;     /* 0x05 de la sonde standard */
    uint32_t retry_wait_ms;  /* attente entre deux essais */
    uint32_t cut_ms;         /* coupure des rails (si cut_rails) */
    uint32_t lost_ms;        /* perte : READY sans 0x05 ni 0x06 ; un seul des deux arrêté, ses données invalides */
    bool     cut_rails;      /* RECOVERING coupe les rails (et met les lignes au repos) cut_ms ; faux par défaut
                                (décision de l'humain, jusqu'à la carte à rails commutés) : la reprise attend
                                retry_wait_ms puis repart de POWERING */
} bsk_session_params_t;

void bsk_session_params_default(bsk_session_params_t *p);

/* Session en OFF, rien d'émis, compteur d'échecs à zéro. PHY doit être prête avant (bsk_phy_init, ou phy_sim_init en
 * simulation). */
void bsk_session_init(const bsk_session_params_t *p);

/* Traite ce que PHY a remonté, au plus 64 événements, puis ce qui est échu, jusqu'à `now_us`. Au-delà de 64, le reste
 * attend le pas suivant, et ce qui est échu après le dernier événement traité avec lui. */
void bsk_session_step(uint64_t now_us);

/* Prochain instant où SESSION ou TRANSACTION a quelque chose à faire sans événement de PHY ;
 * UINT64_MAX s'il n'y en a pas. */
uint64_t bsk_session_next(void);

void bsk_session_status(bsk_status_t *st);

/* La boîte de commandes. La commande est traitée tout de suite, à l'instant du dernier bsk_session_step. Elle rend ses
 * accusés par bsk_session_ack : ACK_REJECTED seul (immédiat et final), ou ACK_ACCEPTED puis ACK_COMPLETED ou ACK_FAILED,
 * tout de suite ou plus tard.
 *   - une seule commande en vol, de son ACK_ACCEPTED à son accusé final : seules CMD_FOCUS_GOTO et CMD_FOCUS_MOVE
 *     (jusqu'à ARRIVED, STALLED, ABORTED) et CMD_LENS_CUSTOM (jusqu'à la réponse de l'objectif) restent en vol ; les
 *     autres finissent dans l'appel. Toute commande déposée pendant un vol reçoit E_BUSY, sauf CMD_FOCUS_STOP ;
 *   - CMD_LENS_CUSTOM émet une requête au Samyang AF 135 en READY (E_NOCAP à tout autre objectif) ; finie par
 *     ACK_COMPLETED à sa réponse, ou ACK_FAILED : son échéance (E_TIMEOUT), une réponse qui n'est pas la sienne
 *     (E_FRAMING), la sortie de READY (E_ABORTED). Un refus de bench_core la refuse (E_FORBIDDEN), rien d'émis ;
 *   - CMD_HOME et CMD_RING_SET ne sont jamais déposées (aucune ligne de HOTE) : sans accusé ;
 *   - CMD_APERTURE_SET (arg : un code d'ouverture, unités objectif) est servie en READY, bornée à la plage que l'objectif
 *     publie (aperture_min, aperture_max), posée pour le prochain 0x03 ; sans plage, E_NOCAP ;
 *   - CMD_ATTACH finit tout de suite : la session publie l'issue du démarrage dans session_state et last_error.
 * La file garde 8 accusés ; l'hôte les lit après chaque dépôt (il attend le premier), si bien qu'il n'y en a jamais plus
 * de quatre : les deux de la commande, l'accusé final de celle en vol, et celui d'un mouvement qu'un CMD_FOCUS_STOP
 * arrête. Au-delà, le plus ancien serait perdu. */
void bsk_session_command(const bsk_cmd_t *c);

/* Ce que la carte compte pour `DEBUG` (PROTOCOL.md § 3) : des mesures ; compter n'y change rien. À zéro au démarrage de
 * la carte (bsk_phy_init, bsk_session_init). La SESSION les remonte de la PHY et de TRANSACTION, dont HOTE ne dépend
 * pas. */
typedef struct {
    uint32_t evq_drop;       /* PHY : les événements jetés, sa file pleine (bsk_phy_stats) */
    uint32_t ev_back;        /* SESSION : les événements de PHY reçus datés avant le précédent */
    uint32_t ev_back_us;     /*   le plus grand écart vu, en µs */
    uint32_t tx_wait_us;     /* PHY : la plus longue attente de fin d'émission, en µs (bsk_phy_stats) */
    uint32_t tx_timeout;     /*   ses échéances de 100 ms atteintes */
    uint32_t pair_late;      /* TRANSACTION : les paires jetées, une de leurs trames plus de 2 ms après son échéance */
    uint32_t pair_late_us;   /*   le plus grand retard constaté en les jetant, en µs (bsk_txn_late) */
} bsk_session_debug_t;

void bsk_session_debug(bsk_session_debug_t *d);

/* Le prochain accusé, dans l'ordre ; false s'il n'y en a pas. */
bool bsk_session_ack(bsk_ack_t *a);

/* Les 16 octets de données de la réponse de l'objectif à la dernière CMD_LENS_CUSTOM finie par ACK_COMPLETED, tels qu'il
 * les a rendus (rien n'y est interprété). */
#define BSK_CUSTOM_DATA 16u
void bsk_session_custom_data(uint8_t data[BSK_CUSTOM_DATA]);

#endif
