/* SOURCE: PROTOCOL.md (LOG ALL), seul
 * AUTHOR: engineer
 * DATE: 2026-10-02
 * STATUS: actif — test seulement (sim/test/test_journal.c, test_session135.c, test_std.c) ; jamais compilé pour la carte
 * Le décodeur de test des lignes `* rx` et `* tx` du journal, qui reconstruit les trames.
 *
 * Le journal lu ligne à ligne, dans l'ordre où la carte l'a écrit, comme le lirait un humain ou la page : chaque trame
 * entière (une ligne, ou ses morceaux `+<off>/<len>#<id>` réassemblés) est la référence de son sens et de son type ;
 * chaque ligne `~<tt>` en est reconstruite ; `* lost <n>` oublie toutes les références. Le décodeur n'interprète ni le
 * 0x0A ni `LOG ALL` : la carte réécrit elle-même entière la trame qui suit (PROTOCOL.md). */
#ifndef JDECODE_H
#define JDECODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsk_phy.h"

typedef struct {
    bool        rx;      /* `* rx`, sinon `* tx` */
    uint64_t    ms;      /* l'instant de la ligne */
    bool        known;   /* faux : une différence sans référence (après `* lost`) ; seul f.msg[0] est connu */
    bool        diff;    /* la trame vient d'une ligne `~<tt>` */
    bsk_frame_t f;
} jd_frame_t;

typedef enum { JD_NONE, JD_FRAME, JD_BAD } jd_rc_t;

/* La ligne `line` (sans fin de ligne) : JD_FRAME, une trame dans `out` ; JD_NONE, rien (une autre ligne, un morceau qui
 * n'achève pas sa trame) ; JD_BAD, une ligne `* rx|tx` mal formée (`why` dit pourquoi). */
jd_rc_t jd_line(const char *line, jd_frame_t *out, const char **why);

/* Oublie tout : références et morceaux en cours. */
void jd_reset(void);

/* Le journal vidé comme la tâche du journal de la carte (tout ce que l'anneau tient, puis 20 ms d'attente), mesuré et
 * décodé : ce qui sortirait sur l'USB (chaque ligne et sa fin), par seconde depuis t0 et par vidage ; ce que l'anneau
 * tenait au plus à un vidage (chaque ligne et ses 3 octets d'en-tête dans l'anneau). Une ligne
 * d'une génération périmée (d'avant LOG OFF) n'est ni écrite ni décodée, comme sur la carte. */
#define JD_DRAINS 4096u
typedef struct {
    uint64_t t0_us;
    uint64_t bytes, lines;
    uint32_t sec[128];             /* octets de la seconde k depuis t0 */
    uint32_t per[JD_DRAINS];       /* octets de chaque vidage */
    size_t   n_drains;
    size_t   ring_max;
    unsigned lost, rxflood, bad, unknown, frames;
} jd_meter_t;

/* Un vidage à l'instant `now_us` : chaque ligne décodée ; chaque trame donnée à `cb`. Une ligne `* lost` ou `* rxflood`
 * est comptée, une ligne illisible aussi (bad, affichée). */
void jd_drain(jd_meter_t *m, uint64_t now_us, void (*cb)(const jd_frame_t *d, void *ctx), void *ctx);

/* La pire des `secs` premières secondes depuis t0 ; le pire total de `k` vidages consécutifs. */
uint32_t jd_worst_second(const jd_meter_t *m, size_t secs);
uint32_t jd_worst_window(const jd_meter_t *m, size_t k);

#endif
