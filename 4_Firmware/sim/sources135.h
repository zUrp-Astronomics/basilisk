/* SOURCE: traces du 135 de l'humain, traces/sy135-2026-09-20-*.txt
 * AUTHOR: engineer
 * DATE: 2026-09-26
 * STATUS: actif — table fermée, lue par lens135.c ; chaque gabarit est relu sur sa ligne par sources135_verify()
 *
 * Les octets que le faux 135 recopie des traces, avec leur ligne.
 *
 * Chaque octet émis par le faux 135 a une source, et une seule parmi trois :
 *   1. une ligne de trace de traces/sy135-2026-09-20-*.txt, citée : les gabarits de ce fichier ;
 *   2. une valeur que 7_Docs/E-Mount/samyang.md ou protocol.md publie : dans lens135.c, à côté de son
 *      usage, avec sa section ;
 *   3. un champ calculé que samyang.md ou protocol.md décrit : dans lens135.c, la section citée.
 * Aucun octet n'est recopié d'une image du firmware de l'objectif. Une valeur que l'objectif tire de son
 * firmware sans que la doc la publie est neutre (à zéro) et dite telle, à son usage.
 * Un gabarit est une trame complète telle qu'elle est écrite sur sa ligne (F0 … 55), fragments
 * compris pour la réponse au 0x08 (quatre lignes). Le faux 135 n'en garde que les sous-messages ;
 * l'en-tête (classe, séquence) et la somme sont recalculés (source 3, protocol.md § 3). */
#ifndef SOURCES135_H
#define SOURCES135_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    SRC_R01,      /* réponse au 0x01 */
    SRC_R07,      /* réponse au 0x07 */
    SRC_R08,      /* réponse au 0x08, 202 octets, quatre fragments */
    SRC_R09,      /* réponse au 0x09 */
    SRC_R0B,      /* réponse au 0x0B (l'offset 0 est l'écho de la requête, calculé) */
    SRC_R0D,      /* réponse au 0x0D */
    SRC_R10,      /* fin de homing */
    SRC_R40_V,    /* 'V' : version 1.05, l'emporte sur le 01 06 de l'analyse statique de la 1.06 */
    SRC_T05,      /* 0x05, mise en page du masque de la carte, commutateur Custom en M1 */
    SRC_T06,      /* 0x06, mise en page du masque de la carte */
    /* Contrôles seulement : le faux 135 construit ces réponses (sources 2 et 3) ou par écho, et le
     * test des sources vérifie que la construction redonne ces octets tracés. */
    SRC_R0A,      /* écho du 0x0A */
    SRC_R40_M,    /* 'M' 00 */
    SRC_R40_FA,   /* 'F' FA (position 14623) */
    SRC_R40_FB,   /* 'F' FB, fin de déplacement */
    SRC_N
} src_id_t;

typedef struct {
    const char *name;
    const char *file;       /* nom de fichier sous traces/ */
    int         line[4];    /* ligne de chaque fragment */
    const char *hex[4];     /* le fragment, tel qu'écrit sur sa ligne */
} src_trace_t;

extern const src_trace_t g_src135[SRC_N];

/* Octets de la trame du gabarit (fragments mis bout à bout). Rend leur nombre, 0 si illisible. */
size_t sources135_frame(src_id_t id, uint8_t *out, size_t cap);

/* Sous-messages du gabarit (sans en-tête ni fin). Rend leur nombre, 0 si la trame est invalide. */
size_t sources135_msg(src_id_t id, uint8_t *out, size_t cap);

/* Relit chaque ligne citée dans `dir` (traces/) : le fragment doit y être, aligné sur des
 * octets entiers, et la trame reconstituée doit être valide. Rend le nombre d'écarts (imprimés). */
int sources135_verify(const char *dir);

#endif
