/* SOURCE: composant session — la consigne de mise au point (voir drive.h)
 * AUTHOR: engineer
 * DATE: 2026-10-03
 * STATUS: actif — C pur, sans ESP-IDF : compilé aussi par les tests hôte
 *
 * Le 0x1D, le bit AF et le 0x1C de MOUVEMENT, posés dans la boucle de TRANSACTION.
 *
 * Invariant : le bit AF est posé dans le 0x03 de TRANSACTION si et seulement si d.af_left > 0 (af_hold seul écrit
 * l'un et l'autre). */
#include "drive.h"

#include <stdbool.h>
#include <stddef.h>

#include "bsk_txn.h"

#define AF_OFF     12     /* offset 12 du message 0x03, octet de type exclu */
#define AF_BIT     0x10
#define AF_FRAMES  30

_Static_assert(AF_OFF < BSK_TXN_LOOP03_LEN - 1, "le bit AF est dans le 0x03");

static struct {
    uint8_t af_left;                  /* paires restantes avec le bit AF */
} d;

static void af_hold(uint8_t pairs)
{
    d.af_left = pairs;
    bsk_txn_loop03(AF_OFF, AF_BIT, pairs ? AF_BIT : 0);
}

/* Après chaque paire partie (bsk_txn_loop_observer). Une paire qui ne part pas garde le 0x1D accroché pour la suivante
 * (bsk_txn_attach) : une position absolue, qu'on renvoie au lieu d'arrêter (décision de l'humain du 2026-10-07). */
static void paired(void)
{
    if (d.af_left) af_hold((uint8_t)(d.af_left - 1));
}

void drive_goto(uint16_t target)
{
    uint8_t m[5];
    _Static_assert(sizeof m <= BSK_TXN_ATTACH_MAX, "le 0x1D tient derrière le 0x04");
    m[0] = 0x1D;                      /* 1D lo hi extra (unité | mode << 6) */
    m[1] = (uint8_t)target;
    m[2] = (uint8_t)(target >> 8);
    m[3] = 0;
    m[4] = 0;
    bsk_txn_loop_observer(paired);    /* le décompte du bit AF ; sans bit AF, il ne fait rien */
    bsk_txn_attach(m, sizeof m);
    af_hold(AF_FRAMES);
}

bool drive_goto_failed(void) { return bsk_txn_attach_failed(); }

bsk_err_t drive_stop(void)
{
    static const uint8_t stop[] = {0x1C};
    bsk_txn_attach(NULL, 0);          /* une consigne pas encore partie ne part pas après l'arrêt */
    af_hold(0);
    return bsk_txn_send(stop, sizeof stop);
}

void drive_forget(void)
{
    bsk_txn_attach(NULL, 0);
    af_hold(0);
}
