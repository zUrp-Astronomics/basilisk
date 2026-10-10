/* SOURCE: spec de l'atelier § 7 et § 7.1.3 — bench_core, le filtre de fil (voir include/bsk_bench_core.h)
 * AUTHOR: engineer
 * DATE: 2026-10-03
 * STATUS: actif — C pur, sans ESP-IDF : compilé aussi par les tests hôte
 *
 * Seul appelant de bsk_phy_send : une trame passe si ses sous-messages se pavent EXACTEMENT par les tailles de la
 * liste. Un type hors liste, un message tronqué ou trop long la font refuser : un objectif qui pave la trame autrement
 * lirait un autre type que celui qu'on a jugé, et un type sans descripteur bloque le répartiteur Samyang jusqu'à la
 * coupure (spec de l'atelier § 7). Liste blanche : 0x14, 0x15, 0x16, 'X' 0x34 n'y sont pas, donc refusés. Une liste
 * noire, écrite sur le 135, laissait passer 'X' 0x34 00, qui remet à zéro en flash son drapeau de mise à jour ('X' 0x34
 * écrit la flash de chacun des douze Samyang lus), et des 'Z' qui écrivent celle du 35-150 et du 16.
 * Chaque sous-commande du canal 0x40, admise ou refusée, est décrite ci-dessous en termes de protocole. La preuve qu'une
 * sous-commande admise n'écrit rien, ou ce qu'écrit l'exception, est l'analyse statique privée des firmwares, publiée
 * dans 7_Docs/E-Mount/samyang.md : la description des sous-commandes (§ 6.3), la carte des écritures, modèle par modèle
 * (§ 7.2), la limite de la preuve (§ 7.3) et cette défense (§ 7.4). */
#include "bsk_bench_core.h"

#include <stddef.h>

static uint32_t refused;

/* Canal 0x40 : [0x40][Main][Sub][16 octets] (samyang.md § 6.1). Les sous-commandes admises, pour un Samyang reconnu :
 *   - 'V' 00 : rend la version du firmware de l'objectif (sur le 14-24, pose aussi le mode service) ;
 *   - 'F' FA : rend la position du focus ; 'F' FB : une consigne de focus, bornée par l'objectif, réponse quand le moteur
 *     est immobile ; 'F' 0x32 : un homing du focus seul, fini par le 10 00 standard ;
 *   - 'M' 00 et 0x31 : posent le mode service et arment les notifications 'W' ; 0x31 arrête aussi les 'H'/'L'.
 * Aucune n'écrit la mémoire non volatile, quelles que soient ses données : sur les six modèles récents, par le recensement
 * des écrivains de leur mémoire ; sur les six anciens, par la lecture de chaque traitement (samyang.md § 6.3, § 7.2,
 * limite § 7.3). Refusées : toute autre sous-commande de 'F', 'I', 'J', 'K', 'P' (certaines écrivent la flash, § 7.2 ;
 * 'P' a son exception, svc135_allowed), 'Z' et tout autre MainCmd. 'X' 0x34 surtout : 00 remet à zéro en flash le
 * drapeau de mise à jour du 135, 01 l'y pose et redémarre l'objectif dans son bootloader (§ 6.3). */
static bool svc_allowed(char main_cmd, uint8_t sub)
{
    switch (main_cmd) {
    case 'V': return sub == 0x00;                              /* version */
    case 'F': return sub == 0xFA || sub == 0xFB || sub == 0x32; /* lecture, consigne, homing */
    case 'M': return sub == 0x00 || sub == 0x31;               /* notifications 'W' */
    default: return false;
    }
}

/* L'EXCEPTION, voulue par l'humain : la configuration du commutateur Custom du Samyang AF 135 (ce que fait chaque
 * position, M1 et M2) vit dans sa flash. Réservée au 135 déclaré (BSK_BENCH_SAMYANG135, LensType2 8) : la même commande
 * écrit la mémoire d'autres Samyang, avec une sémantique que personne n'a relue (samyang.md § 7.2). Établi par l'analyse
 * statique du firmware 1.06 du 135 (samyang.md § 6.3, § 7.4 ; la lecture de 'P' 0x38 relue, § 7.3) ; un 135 en 1.05
 * n'est pas distingué : risque accepté.
 *   - 'P' 0x38, l'écriture : octet de données d = 0x30 + (haut << 4 | bas), chaque position à 0 (APERTURE), 1 (AF) ou
 *     2 (MF) ; le 135 range alors cette configuration en flash (samyang.md § 5.2, § 6.3, § 7.2). Une autre valeur n'écrit
 *     rien chez le 135, mais la carte ne l'émet pas : le même test ici, sur v = d - 0x30 pris sur un octet, et elle est
 *     refusée ;
 *   - 'P' 0xFA, la lecture : rend la configuration, effets en RAM seuls. Avec l'octet de données 0x53, elle pose le
 *     drapeau « MTF », qui change la réponse à 'F' FB : refusée. Tout autre octet prend la branche « JIG », qui remet ce
 *     drapeau à 0 (samyang.md § 6.3) ;
 *   - toute autre sous-commande de 'P' écrit la flash, 0x11 et 0x31 à 0x37 (samyang.md § 7.2) : refusée, comme tout
 *     autre MainCmd que ceux de svc_allowed.
 * Les 15 autres octets de données ne sont pas examinés : le 135 ne lit que le premier pour ces deux commandes
 * (samyang.md § 6.3). */
static bool svc135_allowed(char main_cmd, uint8_t sub, uint8_t data)
{
    uint8_t v = (uint8_t)(data - 0x30);
    if (main_cmd != 'P') return false;
    if (sub == 0xFA) return data != 0x53;
    return sub == 0x38 && (v >> 4) < 3 && (v & 0x0F) < 3;
}

/* Un message 0x40 de 19 octets, pour l'objectif déclaré. Seules les deux déclarations d'un Samyang reconnu ouvrent le canal :
 * une valeur hors de l'énumération ne dit pas quel objectif est monté, la barrière se ferme. */
static bool svc_ok(const uint8_t *m, bsk_bench_lens_t lens)
{
    if (lens != BSK_BENCH_SAMYANG && lens != BSK_BENCH_SAMYANG135) return false;
    if (svc_allowed((char)m[1], m[2])) return true;
    return lens == BSK_BENCH_SAMYANG135 && svc135_allowed((char)m[1], m[2], m[3]);
}

/* Taille (octet de type compris) d'un message de la liste, 0 hors liste : les messages du boîtier, tailles de la table
 * de réception du 135, identique sur cinq Samyang et deux Tamron. Une réserve, signalée à l'humain, non refusée : sur
 * le V-AF 24, le 0x09 de l'init, que tout boîtier envoie, recharge un drapeau d'accessoire lu dans son EEPROM, et sa
 * tâche d'accessoire remet cet octet à 0 s'il valait 1 ; seul un accessoire le met à 1 (samyang.md § 1.3, § 7.2,
 * note 4). Le refuser casserait l'init de tout objectif. */
static uint8_t listed_size(uint8_t t)
{
    switch (t) {
    case 0x01: return 33; case 0x03: return 21; case 0x04: return 14; case 0x07: return 2;
    case 0x08: return 9;  case 0x09: return 5;  case 0x0A: return 17; case 0x0B: return 3;
    case 0x0D: return 2;  case 0x10: return 2;  case 0x1C: return 1;  case 0x1D: return 5;
    case 0x3F: return 2;  case 0x40: return 19;
    default: return 0;
    }
}

static bool allowed(const bsk_frame_t *f, bsk_bench_lens_t lens)
{
    size_t i = 0;
    if (f->len == 0) return false;                  /* aucun message : rien de listé */
    if (f->len > BSK_MSG_MAX) return false;         /* plus long que msg[] : ce n'est pas une trame (bsk_phy.h) */
    while (i < f->len) {
        uint8_t sz = listed_size(f->msg[i]);
        if (sz == 0 || i + sz > f->len) return false;
        if (f->msg[i] == 0x40 && !svc_ok(&f->msg[i], lens)) return false;
        i += sz;
    }
    return true;
}

bsk_err_t bsk_bench_send(const bsk_frame_t *f, bsk_bench_lens_t lens)
{
    if (!allowed(f, lens)) {
        refused++;
        return E_FORBIDDEN;
    }
    return bsk_phy_send(f);
}

uint32_t bsk_bench_refused(void) { return refused; }
