/* SOURCE: spec de l'atelier § 3.1 et § 9 — RESTORING, le retour à la marque
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — C pur, sans ESP-IDF : compilé aussi par les tests hôte
 *
 * Le retour à la marque (restore.h). ARRIVED sur la marque, STALLED, q, ou la clé de la marque ou les bornes du 0x06
 * lues autres mènent à READY. Rien n'est écrit dans le magasin. */
#include "restore.h"

#include <string.h>

#include "bsk_journal.h"
#include "bsk_store.h"
#include "lens_rx.h"
#include "mark.h"
#include "motion.h"

#define X_135          200    /* le dépassement du 135, porté par son firmware (7_Docs/E-Mount/samyang.md § 5.6) */
#define LT2_135        8u     /* le LensType2 du 135, tracé dans son 0x07 */

static struct {
    bool        have;          /* restore_plan : une marque lue */
    bsk_mark_t  m;             /* ... elle */
    int32_t     x, via;        /* ... le dépassement, la cible du premier goto (la marque : le trajet direct) */
    int32_t     rs_mark;       /* RESTORING : la marque visée */
    bool        rs_via;        /* ... le goto en cours est celui du dépassement */
    char        rs_key[BSK_STORE_KEY_MAX];   /* ... la clé de la marque et les bornes du 0x06 du départ (restore_plan) */
    int32_t     rs_fmin, rs_fmax;
    const char *rs_end;        /* ... la cause de la sortie vers READY, pour le journal */
} s;

/* La recette : arriver sur la marque du côté du sens rangé avec elle, en décroissant s'il est inconnu ; si le trajet
 * direct arriverait de l'autre côté, un goto à la marque ± X de ce côté-là, borné aux bornes du 0x06, puis un goto à la
 * marque ; sinon un seul goto. Déjà sur la marque, le trajet direct n'arrive de ce côté que si le dernier sens vu au 0x06
 * (mark_dir) est celui-là : un autre, ou inconnu, et le jeu n'est pas repris ; le dépassement, alors. X : 200
 * pas sur le 135, sinon 1 % de la course du 0x06, au moins 1 — le 24 mm aussi, bien que la spec de l'atelier § 9 le dise
 * à 50 (décision prise ainsi). */
bool restore_plan(void)
{
    const lens_rx_t *l = lens_rx();
    bsk_mark_t m;
    int32_t x, via;
    bool inc, taken;
    s.have = mark_get(&m);
    if (!s.have) return false;
    s.m = m;
    if (!l->have06 || m.position < l->fmin || m.position > l->fmax) return false;   /* MOUVEMENT la refuserait */
    x = l->lens_type2 == LT2_135 ? X_135 : (l->fmax - l->fmin) / 100;   /* lens_type2 : 0 sans 0x07 */
    if (x < 1) x = 1;
    inc = m.approach_dir == BSK_APPROACH_INCREASING;   /* inconnu ou invalide : en décroissant (samyang.md § 5.6) */
    taken = l->pos == m.position && mark_dir() == (inc ? BSK_APPROACH_INCREASING : BSK_APPROACH_DECREASING);
    via = m.position;
    if (inc && l->pos >= m.position && !taken) via = m.position - x < l->fmin ? l->fmin : m.position - x;
    if (!inc && l->pos <= m.position && !taken) via = m.position + x > l->fmax ? l->fmax : m.position + x;
    /* via == la marque : le trajet direct, ou un dépassement borné qui tombe sur elle (une marque à une borne) : un goto */
    s.x = x;
    s.via = via;
    memcpy(s.rs_key, mark_key(), sizeof s.rs_key);
    s.rs_fmin = l->fmin;
    s.rs_fmax = l->fmax;
    return true;
}

void restore_skip(const char *nomark)
{
    if (!s.have) bsk_journal_restore_skip(nomark, NULL);
    else bsk_journal_restore_skip("limit", &s.m);
}

void restore_start(uint64_t now)
{
    bsk_journal_restore(&s.m, s.x, s.via == s.m.position ? -1 : s.via);
    s.rs_mark = s.m.position;
    s.rs_via = s.via != s.m.position;
    motion_restore((uint16_t)s.via, now);
}

/* La clé de la marque ou les bornes du 0x06 lues autres que celles du départ : la marque visée n'est plus celle de la
 * focale courante, ou plus dans ses bornes. Une clé vidée ("" : le 0x05 arrêté) ou des bornes oubliées (have06
 * faux : le 0x06 arrêté) n'en sont pas : cette perte a sa règle, la position et les bornes inconnues pendant le retour
 * (PROTOCOL.md § 2) ; revenues les mêmes, rien n'a changé. */
static bool changed(void)
{
    const lens_rx_t *l = lens_rx();
    const char *k = mark_key();
    return (k[0] && strcmp(k, s.rs_key)) || (l->have06 && (l->fmin != s.rs_fmin || l->fmax != s.rs_fmax));
}

/* La ligne `* restore` de la sortie est écrite par le superviseur, avec la cause posée ici. Un changement (changed)
 * abandonne le retour à tout moment, avant tout autre goto, comme q (cmd.c:stop) : le 0x1C, READY qu'il soit parti ou non ;
 * aucun trajet n'est recalculé pour la focale nouvelle. */
restore_step_t restore_track(uint64_t now)
{
    bsk_motion_state_t m = motion_state();
    if (changed()) {
        (void)motion_stop(now);
        s.rs_end = "changed";
        return RESTORE_END;
    }
    if (m == MOTION_ARRIVED && s.rs_via) {
        s.rs_via = false;
        motion_restore((uint16_t)s.rs_mark, now);
        return RESTORE_NEXT;
    }
    if (m == MOTION_ARRIVED) s.rs_end = "arrived";
    else if (m == MOTION_STALLED) s.rs_end = "stall";
    else return RESTORE_WAIT;
    return RESTORE_END;
}

void restore_stop(void) { s.rs_end = "stop"; }

const char *restore_cause(void) { return s.rs_end; }
