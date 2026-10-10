/* SOURCE: 7_Docs/E-Mount/samyang.md et protocol.md ; traces du 135 de l'humain (4_Firmware/traces, firmware 1.05) ; analyse statique privée du firmware 1.06
 * AUTHOR: engineer
 * DATE: 2026-09-26
 * STATUS: actif — compilé dans test_lens135, replay et les bancs du firmware sur le faux 135 (lens_sim_135.c)
 *
 * Le faux Samyang AF 135 (interface et paramètres : lens135.h), chaque mécanisme cité à sa source.
 *
 * Citations : « samyang.md § n », « protocol.md § n » = section de 7_Docs/E-Mount/ ; « full:n », « dump05:n »… =
 * ligne de traces/sy135-2026-09-20-*.txt ; « traces/<fichier> ligne n » = capture publiée de 7_Docs/E-Mount/traces/.
 * Ce que seule l'analyse statique privée du firmware 1.06 établit est dit tel, sans renvoi. Quand une trace et
 * l'analyse divergent, la trace l'emporte : le 135 de l'humain est en 1.05 (samyang.md § 0).
 *
 * Le modèle suit l'objectif tel que samyang.md § 2 le décrit : une boucle de fond qui fait des passes, et des
 * interruptions (VD, créneaux, moteur, fourche, UART). Une passe fait, dans cet ordre (analyse statique privée) :
 * réception et répartiteur, poignée de main, émission des 0x05/0x06, traitement iris, traitement de trame, tâche de
 * réponse 0x40, tâche du 0x0A, commutateur Custom, tâche d'émission.
 *
 * SIMPLIFICATIONS DÉCLARÉES (ce que le modèle ne suit pas, et pourquoi c'est sans effet sur ce qu'il
 * affirme, ou ce qu'il faut savoir en s'en servant) :
 *   S1 — une seule latence de boucle (loop_us), entre la fin d'une trame et son traitement ; les
 *        autres tâches de la boucle de fond agissent sans délai. La durée d'une passe n'est pas
 *        lisible dans le code (analyse statique privée ; samyang.md § 2.5 : les délais tracés).
 *   S2 — le moteur de mise au point va à vitesse constante, lente ou rapide (le double), sans rampe ni profil
 *        (samyang.md § 3.1). Une nouvelle cible en cours de mouvement est prise au front VD suivant, demi-tour
 *        compris (l'objectif freine puis repart : analyse statique privée).
 *   S3 — « au repos » veut dire : ni en mouvement, ni armé, ni demandé.
 *   S4 — l'iris est un moteur abstrait : un déplacement part au front VD suivant et dure iris_move_us ;
 *        sa position n'est pas suivie. Une nouvelle consigne d'ouverture (0x03, offsets 3-4) le
 *        déplace si elle diffère de la précédente (l'objectif compare les index de pas, samyang.md § 4.9).
 *        La fourche d'iris est hors de son cran au premier test du homing, et trouvée à la fin de la
 *        recherche (iris_search_us) : aucun échec d'iris hors panne.
 *   S5 — la fourche de mise au point est un seuil mécanique avec hystérésis (fork_low en descendant,
 *        fork_high en montant) ; pas de butée mécanique (samyang.md § 4.3 : aucune zone de butée).
 *   S6 — le 0x1D n'est modélisé qu'en unité 0 (pas), absolu ou relatif sans amplitude, bits 3, 6 et 7
 *        à zéro, à la vitesse lente (samyang.md § 4.2 : sa vitesse vient du 0x03 et du profil du boîtier) ; la
 *        cible est la valeur, sans la compensation de 3 à 5 pas des bits 6-7 à 00 (samyang.md § 4.2). Les unités 2
 *        et 3 et les autres bits sont comptés non modélisés.
 *   S7 — les entrées physiques de l'objectif ne sont modélisées que par ce qu'en disent S12 à S15 : le
 *        sélecteur de limites ne l'est pas (limites à leurs valeurs du démarrage, samyang.md § 4.3), ni les
 *        notifications 'W' du commutateur. Le bouton du fût, enfoncé par le test (l135_inputs_t), n'a qu'un
 *        effet, le bit 3 de l'offset 64 du 0x05 posé tant qu'il l'est (traces/195_samyang.txt lignes 865 et 875,
 *        00 -> 08 à l'appui, 08 -> 00 au relâchement) ; ce qu'il change dans l'objectif (le mode astro d'un homing,
 *        samyang.md § 5.6) n'est pas modélisé : le mode vaut 0 ou 1.
 *        Des profils du boîtier que posent le 0x01 et le 0x08 (samyang.md § 5.3), seul le forçage du
 *        commutateur l'est (S14) : les profils « a6000 » (un 0x01 en FF 7F 00) et « a7r4 » (FF EF FF) sont
 *        tenus à 0, ce que la carte ne change pas (son 0x01 porte FF 01 00).
 *   S8 — l'attente d'environ 10 ms du handler du 0x10 (samyang.md § 3.1) bloque la boucle de fond ; le
 *        homing est lancé au début de l'attente, sans effet observable puisqu'aucune passe ne tourne
 *        pendant. La pause de 300 ms d'un abandon de homing (samyang.md § 3.1) bloque de même la
 *        boucle à partir de la passe suivante.
 *   S9 — les ticks (samyang.md § 8.1 : environ 1,003 ms) ne sont pas modélisés : « plus de N ms » veut dire
 *        plus de N ms.
 *   S10 — en mode service, l'émission attend un peu avant chaque octet (samyang.md § 6.5) :
 *        durée non établie [NÉ], non modélisée.
 *   S11 — seuls les accusés 1D et 1C du 0x06 sont modélisés (emit_06) : la carte n'émet, comme
 *        commandes de mise au point, que 0x1D et 0x1C (liste blanche, spec § 7.1.3), ni 0x1F, ni 0x22,
 *        ni 0x2E, ni 0x3C ; leurs accusés ne peuvent donc pas être en attente. Au plus deux accusés
 *        suivent un 0x06 : la limite de trois de l'objectif n'est pas observable dans ce modèle, et
 *        elle n'est pas codée. Pour mémoire, la règle exacte (samyang.md § 4.4) : au plus trois
 *        accusés après le 0x06 ; un 1C ou un 3C au-delà attend la trame suivante ; un quatrième parmi
 *        1D, 1F, 22 et 2E est perdu.
 *   S12 — l'offset 60 du 0x05, 00 au démarrage et dans le gabarit (dump05:7) : posé à 01 ou FF par un front de la
 *        bague, dans le sens de ce front (compteur + 1 -> 01, - 1 -> FF), si la position courante du commutateur
 *        est configurée AF (S14) ; remis à 0 par chaque 0x04 reçu ; publié tel qu'il est à la construction du 0x05,
 *        au créneau 0 (samyang.md § 5.4, le tableau de l'offset 60 ; § 2.4).
 *        Le compteur de la bague avance à chaque front, dans toutes les positions ; seule l'ouverture native le lit
 *        (S15, samyang.md § 5.5).
 *        Laissés : la mise au point par la bague (samyang.md § 5.4), que pose un 0x04 sans le bit AF du boîtier :
 *        l'objectif y fait la mise au point par la bague, et le modèle n'en fait rien — le 0x04 y remet l'offset 60
 *        à 0 comme avec le bit, et un front n'y déplace pas le moteur ; la carte, qui retire le bit en rôle focus, ne lit
 *        alors pas l'offset 60 ; la remise à 0 du compteur au premier 0x04 avec le bit après un 0x04 sans lui, que
 *        l'entrée dans l'ouverture native refait (S15) ; les écritures du mode astro, qui n'est pas modélisé (S7).
 *   S13 — la configuration du commutateur Custom (l->custom : haut << 4 | bas, chaque position 0 APERTURE, 1 AF,
 *        2 MF ; d'usine 0x10 ; samyang.md § 5.2). 'P' 0x38 la range si v = d - 0x30 a ses deux quartets sous 3, et
 *        la pose aussi en RAM (S14), sinon rien ; sa réponse est l'écho, données à zéro (samyang.md § 6.3). 'P' 0xFA,
 *        hors 0x53, la rend à l'octet de données 7 ; les sept premiers sont d'autres réglages de la même page, à 0
 *        d'usine, que seuls 'P' 0x11 et 'P' 0x31 à 0x37 changent, hors de la liste blanche (samyang.md § 6.3). La
 *        flash et sa copie en RAM, rechargée au démarrage (samyang.md § 5.2), sont toujours égales ici : un seul
 *        champ, hors de la RAM remise à zéro à la mise sous tension (reset_ram). Laissés : les effets en RAM de la
 *        branche « JIG » de 'P' 0xFA, que rien d'émis ne lit dans ce modèle ; 'P' 0xFA avec 0x53 (le drapeau MTF),
 *        hors de la liste blanche (samyang.md § 6.3).
 *   S14 — le commutateur Custom. Sa position est une entrée du test (l135_inputs_t.m2 : posé en M2, la position
 *        basse). La configuration de chaque position, en RAM (pos_up pour M1, pos_dn pour M2), est rechargée de la
 *        flash à la mise sous tension (samyang.md § 5.2).
 *        Le forçage, à chaque passe de la boucle de fond (samyang.md § 5.2, § 5.3) : une fois un 0x08 reçu, en mode
 *        normal (mode = 1, posé par le homing), si le dernier 0x08 ne portait ni le bit 0x02 ni le bit 0x04, ou si le
 *        drapeau du 0x01 est posé, M1 = AF et M2 = MF, en RAM seulement. Le drapeau est posé par un 0x01 qui porte
 *        FF 01 00 aux offsets 6-8, levé par un 0x08 au bit 0x04 de l'offset 0. Le forçage écrase la RAM : la levée
 *        qui suit ne rend pas la configuration de la flash, seule une coupure la recharge.
 *        L'offset 62 du 0x05, bit 1 (samyang.md § 5.2) : en mode normal, posé si la position courante est configurée
 *        MF, effacé en AF ou APERTURE ; effacé hors du mode normal ; inchangé avant le premier 0x08. Au démarrage, 01,
 *        les positions encore à 0 (analyse statique privée) : le gabarit (dump05:7). Laissé : le bouton du fût, qui
 *        poserait le bit 1 au démarrage s'il était tenu (samyang.md § 5.2).
 *   S15 — l'ouverture native (samyang.md § 5.5), à chaque 0x03 reçu. Le mode : en mode normal, la position courante
 *        configurée APERTURE et le bit AF du boîtier posé (l'offset 3 du dernier 0x04, tenu pour posé avant tout 0x04).
 *        L'entrée dans le mode arme la resynchronisation. Tant qu'elle attend, un 0x03 sans le bit 0x10 de l'offset 12
 *        range sa consigne (offsets 3-4) dans un historique de quatre ; si moins de 3 numéros le séparent du dernier
 *        0x03 au bit 0x10, ou si les cases 3 et 4 de l'historique diffèrent, le 0x03 s'arrête là : ni bague, ni iris ;
 *        sinon l'index part de la consigne (la première case de la table qui l'atteint, bornée à 0 et 132). Un 0x03
 *        au bit 0x10 note son numéro et rend les cases 3 et 4 inégales.
 *        Dans le mode : le compteur de la bague remis à 0 au premier passage, puis l'index avance du pas de la bague
 *        (tant que la référence est 0, rien hors des multiples de 3 ; sinon tout l'écart depuis l'appel d'avant),
 *        borné à 0-132 ; offsets 17-18 = la table à l'index, offset 19 bit 0 posé. La table 0 : 23 codes d'ouverture,
 *        chacun six fois (ap_table) ; la table 1 (bit 3 de l'offset 12 du 0x03), que la carte ne demande jamais, met
 *        le modèle hors modèle. La valeur publiée ne va pas à l'iris hors du profil « a6000 », tenu à 0 (S7) ; l'iris
 *        suit la consigne du 0x03 (S4).
 *        Hors du mode : le premier 0x03 garde la publication, le suivant l'efface.
 *        Laissés, faute d'effet sur ce qui est émis : les remises à 0 du compteur à la sortie et de l'entrée par le
 *        homing, que la remise à 0 de l'entrée refait avant toute lecture du compteur ; la consigne d'iris mise à 0
 *        pendant l'attente et posée à la première ouverture par le homing, des détails de l'iris abstrait (S4) ; la
 *        borne du compteur de la bague, qu'aucun banc n'approche. */
#include "lens135.h"

#include <stdlib.h>
#include <string.h>

#include "sources135.h"

#define MS 1000u

/* Valeurs que samyang.md publie (positions en pas, samyang.md § 3.2, § 4.3, § 4.8) */
#define POS_LIMIT_HIGH 30988    /* limite logicielle haute, au démarrage */
#define POS_LIMIT_LOW  13723    /* limite logicielle basse, au démarrage */
#define POS_THRESH_HI  29988    /* seuil du bit 3 de l'offset 0 du 0x06 */
#define POS_THRESH_LO  14623    /* seuil du bit 4 ; aussi la pose de fin de homing en mode service */
#define POS_REST       16384    /* la pose de fin de homing hors mode service */
#define HOME_CLIMB     1600     /* la montée du homing parti du côté bas de la fourche (samyang.md § 3.1) */
#define SPEED_SLOW     1u       /* vitesse du moteur, en multiple de steps_per_s : celle de 'F' FB (samyang.md § 3.1) */
#define SPEED_FAST     2u       /* le double : les déplacements de référence du homing */
#define SLOT_US(hz)    (1000000u / ((hz) * 8u))   /* un créneau : le huitième de la trame (samyang.md § 2.4) */
#define CFG_APERTURE   0u       /* configuration d'une position du commutateur (samyang.md § 5.2, S14) */
#define CFG_AF         1u
#define CFG_MF         2u
#define AP_LAST        132      /* le dernier index de la table 0 : 133 pas (samyang.md § 5.5, S15) */
#define AP_F2          (256 * (2 + 16))   /* f/2 : 256 × (Av + 16), Av = 2 (protocol.md § 8.1) */

/* ── Longueur consommée par type (protocol.md § 4.1 ; 0x4B et 0x4C : samyang.md § 2.6). 0 = pas de
 *    taille : le répartiteur boucle sur place (samyang.md § 2.6). La table couvre 0x01-0x4C. ── */
static const uint16_t RX_LEN[0x4D] = {
    [0x01] = 33, [0x03] = 21, [0x04] = 14, [0x07] = 2, [0x08] = 9, [0x09] = 5, [0x0A] = 17,
    [0x0B] = 3, [0x0C] = 2, [0x0D] = 2, [0x10] = 2, [0x14] = 16, [0x15] = 1032, [0x16] = 2,
    [0x19] = 2, [0x1B] = 6, [0x1C] = 1, [0x1D] = 5, [0x1E] = 5, [0x1F] = 14, [0x22] = 3,
    [0x26] = 2, [0x28] = 2, [0x2E] = 3, [0x2F] = 3, [0x34] = 24, [0x35] = 2, [0x3A] = 24,
    [0x3B] = 8, [0x3C] = 8, [0x3D] = 24, [0x3E] = 9, [0x3F] = 2, [0x40] = 19, [0x4B] = 3,
    [0x4C] = 8,
};

/* Le masque de la carte (0x0A de full:38) : c'est la seule mise en page dont les
 * traces donnent le contenu (0x05 de dump05:7, 0x06 de dump05:11). */
static const uint8_t TRACED_MASK[16] = {0xFF, 0x7F, 0, 0, 0, 0, 0, 0, 0x3F, 0, 0, 0, 0, 0, 0, 0};

/* ─────────────────────────── paramètres ─────────────────────────── */

void l135_params_default(l135_params_t *p)
{
    memset(p, 0, sizeof *p);
    /* Non lisible dans le code (S1). La réponse d'init la plus rapide arrive
     * 5 ms après sa requête (samyang.md § 2.5 : full:8-9, full:20-21, full:22-23, full:24-25), transmission et
     * latence de la carte comprises : c'est la valeur mesurée, donc une borne haute. Aucune trace ne
     * la borne par en bas : un 0x06 traité une trame VD plus tard porte la même séquence (la VD
     * l'incrémente en flux, protocol.md § 3.4), full:71-73 ne les distingue pas. */
    p->loop_us = 5000;
    p->boot_us = 20 * MS;          /* environ 20 ms entre la mise sous tension et la boucle (samyang.md § 2.2) */
    p->byte_us = 13;               /* 10 bits à 750 000 bauds (protocol.md § 1.2, § 2) = 13,3 µs */
    p->lens_cs_tail_us = 200;      /* « attend plus de 200 µs » (samyang.md § 2.5) */
    p->hs_us = 1 * MS;             /* « plus d'une milliseconde » (protocol.md § 2) */
    p->busy_0x10_us = 10 * MS;     /* « attend environ 10 ms en boucle » (samyang.md § 3.1) */
    p->abort_pause_us = 300 * MS;  /* samyang.md § 3.1 */
    p->reply40_us = 50 * MS;       /* « 50 ms au moins après la commande » (samyang.md § 6.1) */
    p->wait_0a_us = 200 * MS;      /* « au plus 200 ms » (samyang.md § 2.3) */
    p->stop_1c_us = 1200 * MS;     /* « au-delà de 1,2 s » (samyang.md § 4.5) */
    p->step_us = 1500 * MS;        /* 1,5 s par étape (samyang.md § 3.1) */
    p->iris_search_max_us = 3000 * MS; /* « 3 s pour la recherche » (samyang.md § 3.1) */
    p->homing_pause_us = 5 * MS;   /* la pause en tête de trois étapes du homing (samyang.md § 3.1) */
    /* 'F' FB de 14555 à 17651 : 3096 pas, réponse 303 ms après la
     * commande (astro:19, astro:25, astro:32). La rampe et l'attente de la VD sont négligées (S2). */
    p->steps_per_s = 3096u * 1000u / 303u;
    /* Iris : aucune trace ne donne la durée d'un mouvement d'iris. Valeurs choisies pour que le
     * homing complet du modèle tombe dans l'intervalle tracé, 527 à 757 ms (samyang.md § 3.1 : full:27-35,
     * reboots:332-333, reboots:854-855, astro:241-251). Paramètres sans autre source. */
    p->iris_move_us = 50 * MS;
    p->iris_search_us = 50 * MS;
    /* La fourche de mise au point : notifications 'H' (en descendant) à 14834 et 14837
     * (astro:323, astro:403), 'L' (en montant) à 14901 (astro:28). */
    p->fork_low = 14835;
    p->fork_high = 14901;
    /* La position que le homing donne au front descendant de la fourche : l'objectif la calcule d'une correction
     * lue dans sa mémoire non volatile, inconnue (samyang.md § 3.1). Fixée là où les 'H' tracés le montrent. */
    p->home_edge = 14835;
    p->initial_position = 14623;   /* repos après homing en mode service, full:34, dump05:11 */
    /* La ligne d'entrée de l'étape 3 de la poignée de main : contact non établi (protocol.md § 2, [NÉ]). Lue haute,
     * faute de mieux : c'est la seule valeur qui laisse la poignée de main aboutir. */
    p->hs_in_high = true;
}

/* ─────────────────────────── sorties et signaux ─────────────────────────── */

static void out_push(l135_t *l, l135_out_kind_t kind, uint8_t v)
{
    if (l->out_n == L135_OUT_CAP) {       /* le banc ne vide pas les sorties : défaut du banc */
        l->oom = true;
        l->oom_why = "file de sorties pleine (le banc ne la vide pas)";
        return;
    }
    l->out[(l->out_head + l->out_n) % L135_OUT_CAP] = (l135_out_t){l->now, kind, v};
    l->out_n++;
}

static void set_lens_cs(l135_t *l, bool high)
{
    if (l->lens_cs == high) return;
    l->lens_cs = high;
    out_push(l, L135_OUT_LENS_CS, high ? 1 : 0);
}

static void unmodelled(l135_t *l, uint8_t type, const char *why)
{
    l->unmodelled[type]++;
    l->unmodelled_why = why;
}

static void out_of_model(l135_t *l, const char *why)
{
    l->oom = true;
    l->oom_why = why;
}

/* ─────────────────────────── moteur de mise au point ─────────────────────────── */

static uint32_t speed_steps_per_s(const l135_t *l, uint32_t speed)
{
    uint32_t v = l->p.steps_per_s * speed;
    return v ? v : 1;
}

/* Position mécanique à l'instant courant. `target` est, pendant un mouvement, la cible mécanique. */
static int64_t mech(const l135_t *l)
{
    uint64_t dist, span;
    if (!l->moving || l->f.frozen_position) return l->m0;
    span = (uint64_t)(l->dir > 0 ? l->target - l->m0 : l->m0 - l->target);
    dist = (l->now - l->t_m0) * speed_steps_per_s(l, l->speed) / 1000000u;
    if (dist > span) dist = span;
    return l->m0 + l->dir * (int64_t)dist;
}

static int32_t published(const l135_t *l) { return (int32_t)(mech(l) + l->k); }

static void freeze(l135_t *l)
{
    l->m0 = mech(l);
    l->t_m0 = l->now;
}

static void set_counter(l135_t *l, int32_t c)   /* la position publiée devient c */
{
    l->k = c - (int32_t)mech(l);
}

static bool focus_idle(const l135_t *l) { return !l->moving && !l->armed && l->pending == 0; } /* S3 */

/* Demande un déplacement vers `target`, sans le borner. */
static void focus_goto(l135_t *l, int32_t target, uint32_t speed)
{
    if (l->moving || target != published(l)) {
        l->pending = 1;
        l->pend_target = target;
        l->pend_speed = speed;
    }
}

static int32_t clamp_soft(int32_t c)   /* les limites logicielles (samyang.md § 4.3) */
{
    return c > POS_LIMIT_HIGH ? POS_LIMIT_HIGH : c < POS_LIMIT_LOW ? POS_LIMIT_LOW : c;
}

/* Borne la cible aux limites logicielles, en silence (samyang.md § 4.3). Une cible ramenée égale à la position ne
 * fait rien : le travail trouve le moteur au repos (samyang.md § 4.3, ce que dit l'analyse du 135). */
static void focus_goto_clamped(l135_t *l, int32_t target, uint32_t speed)
{
    int32_t c = clamp_soft(target);
    if (!l->moving && c == published(l)) return;
    focus_goto(l, c, speed);
}

static void focus_stop_request(l135_t *l)
{
    l->pending = 2;
}

/* Planificateur, au traitement de trame : arme le déplacement demandé pour le front VD suivant, ou arrête le
 * moteur (S2 : sans rampe). */
static void planner(l135_t *l)
{
    if (l->pending == 1) {
        l->armed = true;
        l->armed_target = l->pend_target;
        l->armed_speed = l->pend_speed;
    } else if (l->pending == 2 && l->moving) {
        freeze(l);
        l->moving = false;
    }
    l->pending = 0;
}

/* Front VD : un déplacement armé démarre ici, et nulle part ailleurs (samyang.md § 2.4). */
static void vd_focus(l135_t *l)
{
    int64_t m, tm;
    if (!l->armed) return;
    l->armed = false;
    freeze(l);
    m = l->m0;
    tm = (int64_t)l->armed_target - l->k;
    if (tm == m) {                 /* rien à faire */
        l->moving = false;
        return;
    }
    l->moving = true;
    l->dir = tm > m ? 1 : -1;
    l->target = (int32_t)tm;
    l->speed = l->armed_speed;
}

/* ─────────────────────────── émission ─────────────────────────── */

static uint8_t tx_class(uint8_t type)    /* samyang.md § 2.5, protocol.md § 3.3 */
{
    switch (type) {
    case 0x05: return 1;
    case 0x06: return 1;
    case 0x1D: return 1;
    case 0x17: return 3;                  /* protocol.md § 7.16 */
    default:   return 2;                  /* réponses : classe 02 de toutes les réponses tracées (full:9-43) */
    }
}

/* Message de type `type` tel que le tampon d'émission le porte à cet instant ; rend sa longueur. */
static size_t tx_msg(l135_t *l, uint8_t type, uint8_t *out, size_t cap)
{
    static const uint8_t NAME_135[] = {                /* samyang.md § 1.1, § 1.3 ; traces/firmware2_ring.txt lignes 17-18 */
        0x00, 'S', 'A', 'M', 'Y', 'A', 'N', 'G', ' ', 'A', 'F', ' ', '1', '3', '5', 'm', 'm', ' ', 'F', '1', '.', '8'};
    size_t n;
    int32_t pos;
    switch (type) {
    case 0x01: return sources135_msg(SRC_R01, out, cap);
    case 0x07: return sources135_msg(SRC_R07, out, cap);
    case 0x08: return sources135_msg(SRC_R08, out, cap);
    case 0x09: return sources135_msg(SRC_R09, out, cap);
    case 0x0D: return sources135_msg(SRC_R0D, out, cap);
    case 0x10: return sources135_msg(SRC_R10, out, cap);     /* toujours 10 00 (samyang.md § 1.3, § 3.1) */
    case 0x0A:                            /* écho : octets 1 à 16 de la requête (samyang.md § 2.3) */
        memcpy(out, l->req0a, 17);
        return 17;
    case 0x0B:                            /* 0B <offset 0 de la requête> 00 (samyang.md § 1.3) */
        n = sources135_msg(SRC_R0B, out, cap);
        out[1] = l->buf0b;
        return n;
    case 0x17:                            /* 3 octets (protocol.md § 4.2) ; les deux derniers ne sont pas publiés
                                           * (tirés du firmware, sens inconnu) : à zéro */
        out[0] = 0x17; out[1] = 0x00; out[2] = 0x00;
        return 3;
    case 0x1C: out[0] = 0x1C; out[1] = 0x00; return 2;   /* samyang.md § 4.4 */
    case 0x1D: out[0] = 0x1D; out[1] = 0x00; return 2;   /* samyang.md § 4.4 */
    case 0x3F:                            /* 66 octets, le nom puis des zéros (samyang.md § 1.3) */
        memset(out, 0, 66);
        out[0] = 0x3F;
        memcpy(out + 1, NAME_135, sizeof NAME_135);
        return 66;
    case 0x40: memcpy(out, l->buf40, 19); return 19;
    case 0x05:                            /* dump05:7, et les champs que le modèle fait varier */
        n = sources135_msg(SRC_T05, out, cap);
        if (l->in.button) out[65] |= 0x08;   /* offset 64, bit 3 (S7) ; out[k] porte l'offset k-1 */
        out[61] = l->o60;                    /* l'offset 60, sa valeur du moment (S12) */
        out[18] = (uint8_t)l->ap_pub;        /* offsets 17-18 et 19 bit 0 : l'ouverture native (S15) */
        out[19] = (uint8_t)(l->ap_pub >> 8);
        out[20] = (uint8_t)((out[20] & ~0x01) | (l->ap_valid ? 0x01 : 0));
        out[63] = (uint8_t)((out[63] & ~0x02) | (l->sw_mf ? 0x02 : 0));   /* offset 62, bit 1 (S14) */
        return n;
    case 0x06:
        /* samyang.md § 4.8. Une source par bit (out[k] porte l'offset k-1) :
         *   out[1], offset 0 : bits 0-2 = 2 ; bits 3 à 6, champs calculés : bit 3 position >= 29988, bit 4
         *     position <= 14623, bit 5 position >= limite haute, bit 6 position <= limite basse ; bit 7,
         *     dump05:11 (0) : samyang.md ne le décrit pas.
         *   out[2], offset 1 : bits 1-2, champ calculé, le sens de variation de la position entre deux 0x06
         *     (2 si elle a crû, 4 si elle a décru, 0 si elle est égale ou si aucun 0x06 n'a encore été
         *     construit) ; bits 0 et 3 à 7, dump05:11 (0) : samyang.md ne les décrit pas.
         *   out[3-4], offsets 2-3 : la position, champ calculé.
         *   out[0] et out[5-39] : dump05:11 tels quels ; ils ne suivent pas l'état.
         * Au repos en 14623, le calcul redonne la trace : 06 12 00 1F 39 (dump05:11). */
        n = sources135_msg(SRC_T06, out, cap);
        pos = published(l);
        out[1] = (uint8_t)((out[1] & 0x80) | 0x02);                          /* bit 7 tracé ; bits 0-2 = 2 */
        if (pos >= POS_THRESH_HI)  out[1] |= 0x08;
        if (pos <= POS_THRESH_LO)  out[1] |= 0x10;
        if (pos >= POS_LIMIT_HIGH) out[1] |= 0x20;
        if (pos <= POS_LIMIT_LOW)  out[1] |= 0x40;
        out[2] = (uint8_t)(out[2] & ~0x06);                                  /* bits 0, 3-7 tracés ; 1-2 calculés */
        if (l->published_prev != 0 && pos > l->published_prev) out[2] |= 0x02;
        if (l->published_prev != 0 && pos < l->published_prev) out[2] |= 0x04;
        l->published_prev = pos;
        out[3] = (uint8_t)pos;
        out[4] = (uint8_t)(pos >> 8);
        return n;
    default:
        return 0;
    }
}

/* Une trame : jusqu'à trois messages, classe du premier (samyang.md § 2.5). */
static void send_frame(l135_t *l, uint8_t a, uint8_t b, uint8_t c)
{
    uint8_t types[3] = {a, b, c};
    uint8_t frame[L135_TX_CAP];
    size_t len = 5, n;
    uint8_t cls = tx_class(a);
    uint16_t sum;
    if (l->lock) {                        /* verrou posé : la nouvelle trame est jetée (samyang.md § 2.5) */
        l->dropped++;
        return;
    }
    if (l->f.silent) {
        l->silenced++;
        return;
    }
    if (cls != 1) {                       /* classe 2/3 : séquence 0, verrou posé (samyang.md § 2.5) */
        l->seq = 0;
        l->lock = true;
    }
    for (int i = 0; i < 3 && types[i]; i++) {
        n = tx_msg(l, types[i], frame + len, sizeof frame - len - 3);
        len += n;
    }
    len += 3;
    frame[0] = 0xF0;
    frame[1] = (uint8_t)len;
    frame[2] = (uint8_t)(len >> 8);
    frame[3] = cls;
    frame[4] = l->seq;
    sum = 0;
    for (size_t i = 1; i + 3 < len; i++) sum = (uint16_t)(sum + frame[i]);
    frame[len - 3] = (uint8_t)sum;
    frame[len - 2] = (uint8_t)(sum >> 8);
    frame[len - 1] = 0x55;
    if (cls != 1) {
        if (l->f.truncate_after && l->f.truncate_after < len) len = l->f.truncate_after;
        if (l->f.delay_us) l->tx_hold_until = l->now + l->f.delay_us;
    }
    if (l->txq_n + len > L135_TX_CAP) {
        out_of_model(l, "file d'émission pleine");
        return;
    }
    for (size_t i = 0; i < len; i++) l->txq[(l->txq_head + l->txq_n + i) % L135_TX_CAP] = frame[i];
    l->txq_n = (uint16_t)(l->txq_n + len);
    l->tx_state = 1;
}

/* La tâche d'émission (samyang.md § 2.5) : BODY_CS basse, LENS_CS levée, le tampon, plus de 200 µs, LENS_CS
 * rabaissée, verrou levé. Un pas par passe. */
static void tx_task(l135_t *l)
{
    switch (l->tx_state) {
    case 1:
        if (!l->body_cs && l->now >= l->tx_hold_until) l->tx_state = 2;
        break;
    case 2:
        set_lens_cs(l, true);
        if (!l->tx_isr) {
            l->tx_isr = true;
            l->t_next_byte = l->now + l->p.byte_us;
        }
        l->tx_state = 3;
        break;
    case 3:
        if (l->txq_n == 0 && !l->tx_isr) {
            l->t_tx = l->now;
            l->tx_state = 4;
        }
        break;
    case 4:
        if (l->now - l->t_tx > l->p.lens_cs_tail_us) {
            set_lens_cs(l, false);
            l->tx_state = 5;
        }
        break;
    case 5:
        l->lock = false;
        l->tx_state = 0;
        break;
    default:
        break;
    }
}

/* ─────────────────────────── flux ─────────────────────────── */

/* Longueurs du 0x05 et du 0x06 recalculées à chaque 0x0A, d'après le masque (samyang.md § 2.3) : les tailles de
 * bloc de protocol.md § 7.5 et § 7.6, octet de type compris. */
static void layout(l135_t *l)
{
    static const uint8_t ADD05[3][8] = {
        {8, 1, 8, 3, 10, 0x1C, 2, 6},     /* octet 1 du masque : blocs 1 à 8 */
        {1, 2, 8, 6, 6, 6, 1, 6},         /* octet 2 : blocs 9 à 16 */
        {3, 3, 0, 0, 0, 0, 0, 0},         /* octet 3 : blocs 17 et 18 */
    };
    static const uint8_t ADD06[8] = {2, 0xB, 9, 4, 6, 7, 0x2D, 4};  /* octet 9 : blocs 1 à 8 */
    uint16_t n05 = 1, n06 = 1;
    for (int o = 0; o < 3; o++)
        for (int b = 0; b < 8; b++)
            if (l->mask[o] & (1u << b)) n05 = (uint16_t)(n05 + ADD05[o][b]);
    for (int b = 0; b < 8; b++)
        if (l->mask[8] & (1u << b)) n06 = (uint16_t)(n06 + ADD06[b]);
    l->len05 = n05;
    l->len06 = n06;
    l->layout_traced = memcmp(l->mask, TRACED_MASK, 16) == 0;
}

static void emit_05(l135_t *l)
{
    if (!l->layout_traced) {              /* aucune trace ne donne le contenu d'une autre mise en page */
        unmodelled(l, 0x05, "0x05 d'une mise en page que les traces ne montrent pas");
        return;
    }
    send_frame(l, 0x05, 0, 0);
}

/* Le 0x06 puis ses accusés, 1D d'abord, puis 1C ; ils suivent le 0x06 dans la même trame (samyang.md § 4.4).
 * Deux accusés au plus : S11. */
static void emit_06(l135_t *l)
{
    uint8_t acks[2] = {0, 0};
    int n = 0;
    if (!l->layout_traced) {
        unmodelled(l, 0x06, "0x06 d'une mise en page que les traces ne montrent pas");
        return;
    }
    if (l->ack1d) { acks[n++] = 0x1D; l->ack1d = false; }
    if (l->ack1c) { acks[n++] = 0x1C; l->ack1c = false; }
    send_frame(l, 0x06, acks[0], acks[1]);
}

/* ─────────────────────────── homing (samyang.md § 3.1) ─────────────────────────── */

static void homing_start(l135_t *l)   /* bit 2 : iris puis focus ; bit 3 seul : focus (samyang.md § 3.1) */
{
    if (l->msg10 & 0x04) l->homing = 1;
    else if (l->msg10 & 0x08) l->homing = 0x14;
    /* aucun des deux bits : rien, et aucune réponse ne viendra */
}

static bool elapsed(const l135_t *l, uint64_t since, uint64_t us) { return l->now - since > us; }

static void iris_retry(l135_t *l)     /* un échec d'iris (samyang.md § 3.1) */
{
    if (!l->retry_iris) {
        l->retry_iris = true;
        l->homing = 0x14;             /* premier échec : on passe au homing focus */
    } else {
        l->busy_until = l->now + l->p.abort_pause_us;
        l->retry_iris = false;
        send_frame(l, 0x17, 0, 0);    /* 0x17 ; homing abandonné sans 10 00 */
        l->homing = 0;
    }
}

static void focus_retry(l135_t *l)    /* un échec de mise au point (samyang.md § 3.1) */
{
    if (l->retry_focus) {
        l->busy_until = l->now + l->p.abort_pause_us;
        l->retry_focus = false;
        send_frame(l, 0x10, 0, 0);    /* second échec : 10 00 quand même */
        send_frame(l, 0x17, 0, 0);    /* puis 0x17, perdu si le 10 00 n'est pas parti (samyang.md § 3.1) */
        l->homing = 0;
    } else {
        l->retry_focus = true;
        l->homing = 1;                /* premier échec : tout reprend, iris compris */
    }
}

static void iris_move(l135_t *l) { l->iris_armed = true; }                 /* S4 */
static bool iris_idle(const l135_t *l) { return !l->iris_armed && !l->iris_moving; }

/* Étapes 1 à 9, au traitement iris. S4 pour la fourche d'iris. */
static void homing_iris(l135_t *l)
{
    switch (l->homing) {
    case 1:
        l->t_home = l->now;
        l->homing = 2;
        break;
    case 2:
        if (elapsed(l, l->t_home, l->p.homing_pause_us)) {
            l->mode = 1;              /* bouton non tenu (S7) : le mode normal */
            iris_move(l);             /* fourche hors de son cran */
            l->t_home = l->now;
            l->homing = 3;
        }
        break;
    case 3:
        if (!iris_idle(l)) {
            if (elapsed(l, l->t_home, l->p.step_us)) iris_retry(l);
        } else if (l->iris_tries < 4) {
            l->t_home = l->now;       /* fourche lue à 0 : recherche */
            l->homing = 4;
            l->iris_tries = 0;
        } else {
            iris_retry(l);
        }
        break;
    case 4:
        l->t_iris_search = l->now;    /* recherche, lancée tout de suite, sans VD (samyang.md § 2.4) */
        l->t_home = l->now;
        l->homing = 5;
        break;
    case 5:
        if (l->now - l->t_iris_search < l->p.iris_search_us) {
            if (elapsed(l, l->t_home, l->p.iris_search_max_us)) iris_retry(l);
        } else {
            l->t_home = l->now;
            l->homing = 6;
        }
        break;
    case 6:
        if (elapsed(l, l->t_home, l->p.homing_pause_us)) {
            iris_move(l);             /* l'ouverture de repos */
            l->t_home = l->now;
            l->homing = 9;
        }
        break;
    case 9:
        if (!iris_idle(l)) {
            if (elapsed(l, l->t_home, l->p.step_us)) iris_retry(l);
        } else {
            l->homing = (l->msg10 & 0x08) ? 0x14 : 0x1E;
        }
        break;
    default:
        break;
    }
}

/* Étapes 0x14 à 0x1E, au traitement de trame (samyang.md § 3.1, la recherche du front). */
static void homing_focus(l135_t *l)
{
    switch (l->homing) {
    case 0x14:
        l->t_home = l->now;
        l->homing = 0x15;
        break;
    case 0x15:
        if (elapsed(l, l->t_home, l->p.homing_pause_us)) {
            if (l->pi) {              /* côté bas : on se dit en limite basse, on monte de 1600 */
                set_counter(l, POS_LIMIT_LOW);
                focus_goto(l, POS_LIMIT_LOW + HOME_CLIMB, SPEED_FAST);
                l->homing = 0x16;
            } else {                  /* côté haut : on se dit en limite haute, on descend */
                set_counter(l, POS_LIMIT_HIGH);
                focus_goto(l, POS_LIMIT_LOW, SPEED_FAST);
                l->homing = 0x1B;
            }
            l->t_home = l->now;
        }
        break;
    case 0x16:
        if (!focus_idle(l)) {
            if (elapsed(l, l->t_home, l->p.step_us)) focus_retry(l);
        } else {
            l->homing = l->pi ? 0x15 : 0x17;
        }
        break;
    case 0x1B:
        if (!l->pi) {
            if (elapsed(l, l->t_home, l->p.step_us)) focus_retry(l);
        } else {
            focus_stop_request(l);
            l->homing = 0x15;
        }
        break;
    case 0x17:
        set_counter(l, POS_LIMIT_HIGH);
        focus_goto(l, POS_THRESH_LO, SPEED_SLOW);
        l->t_home = l->now;
        l->homing = 0x18;
        break;
    case 0x18:
        if (!l->pi) {
            if (elapsed(l, l->t_home, l->p.step_us)) focus_retry(l);
        } else {
            l->edge_home = l->edge;   /* le front, pris par l'interruption de la fourche */
            focus_stop_request(l);
            l->homing = 0x19;
        }
        break;
    case 0x19:
        if (!focus_idle(l)) {
            if (elapsed(l, l->t_home, l->p.step_us)) focus_retry(l);
        } else {                      /* la position est recalculée depuis le front */
            set_counter(l, published(l) - l->edge_home + l->p.home_edge);
            if (l->mode == 1) focus_goto(l, l->service ? POS_THRESH_LO : POS_REST, SPEED_FAST);   /* samyang.md § 3.2 */
            l->homing = 0x1A;
        }
        break;
    case 0x1A:
        if (!focus_idle(l)) {
            if (elapsed(l, l->t_home, l->p.step_us)) focus_retry(l);
        } else {
            l->homing = 0x1E;
        }
        break;
    case 0x1E:                        /* la réponse, toujours 10 00 (samyang.md § 3.1) */
        send_frame(l, 0x10, 0, 0);
        l->homing = 0;
        break;
    default:
        break;
    }
}

/* ─────────────────────────── travaux de mise au point (samyang.md § 4) ─────────────────────────── */

static void job_1d(l135_t *l)         /* le 0x1D (samyang.md § 4.1, § 4.2), réduit par S6 */
{
    uint8_t flags = l->msg1d[4];
    int32_t value = (int32_t)(uint16_t)(l->msg1d[1] | l->msg1d[2] << 8);
    int32_t target;
    if (l->job1d == 2) {              /* à l'arrêt du moteur : 1D 00 */
        if (focus_idle(l)) {
            l->ack1d = true;
            l->job1d = 0;
        }
        return;
    }
    if (l->job1d != 1) return;
    if ((flags & 3) == 1) {           /* unité 1 refusée : 1D 00, sans mouvement (samyang.md § 4.2) */
        l->job1d = 0;
        l->ack1d = true;
        return;
    }
    if ((flags & 3) != 0 || (flags & 0xC8) != 0 || ((flags & 4) && ((flags & 0x30) || l->msg1d[3]))) {
        unmodelled(l, 0x1D, "0x1D hors de l'unité 0 sans amplitude, bits 3, 6 et 7 nuls (S6)");
        l->job1d = 0;
        return;
    }
    if (flags & 4) target = published(l) + (int16_t)value;   /* relatif */
    else target = value;
    focus_goto_clamped(l, target, SPEED_SLOW);
    l->job1d = 2;
}

static void job_1c(l135_t *l)         /* le 0x1C (samyang.md § 4.5) */
{
    if (l->job1c == 1) {
        focus_stop_request(l);
        l->t1c = l->now;
        l->job1c = 2;
    } else if (l->job1c == 2) {
        if (focus_idle(l)) {
            l->ack1c = true;
            l->job1c = 0;
        } else if (elapsed(l, l->t1c, l->p.stop_1c_us)) {
            focus_stop_request(l);
            send_frame(l, 0x17, 0, 0);
            l->ack1c = true;
            l->job1c = 0;
        }
    }
}

/* Traitement de trame (samyang.md § 2.4) : homing focus, 0x1D, 0x1C, planificateur. */
static void frame_processing(l135_t *l)
{
    homing_focus(l);
    job_1d(l);
    job_1c(l);
    planner(l);
}

/* ─────────────────────────── canal 0x40 (samyang.md § 6) ─────────────────────────── */

static void h40(l135_t *l, const uint8_t *m)
{
    uint8_t main = m[1], sub = m[2];
    int32_t target;
    bool served = (main == 'V' && sub == 0x00) ||
                  (main == 'F' && (sub == 0xFA || sub == 0xFB || sub == 0x32)) ||
                  (main == 'M' && (sub == 0x00 || sub == 0x31)) ||
                  (main == 'P' && (sub == 0x38 || (sub == 0xFA && m[3] != 0x53)));   /* S13 */
    if (!served) {
        unmodelled(l, 0x40, "sous-commande 0x40 hors de la liste blanche (spec § 7.1.3, ticket #336)");
        return;
    }
    l->buf40[0] = 0x40;               /* écho, données à zéro (samyang.md § 6.1) */
    l->buf40[1] = main;
    l->buf40[2] = sub;
    memset(l->buf40 + 3, 0, 16);
    switch (main) {
    case 'F':
        if (sub == 0x32) {            /* homing focus seul, sans réponse 0x40 ; créneaux à 60 Hz (samyang.md § 6.3) */
            l->msg10 = (uint8_t)((l->msg10 & 0xF3) | 0x08);
            l->slot_us = SLOT_US(60u);
            homing_start(l);
            return;
        }
        if (sub == 0xFB) {            /* déplacement borné (samyang.md § 6.3) */
            target = m[3] | m[4] << 8;
            if (target < POS_LIMIT_LOW) target = POS_LIMIT_LOW;
            else if (target > POS_LIMIT_HIGH) target = POS_LIMIT_HIGH;
            focus_goto(l, target, SPEED_SLOW);
        } else {                      /* 'F' FA : la position à la réception (samyang.md § 6.3) */
            target = published(l);
            l->buf40[3] = (uint8_t)target;
            l->buf40[4] = (uint8_t)(target >> 8);
        }
        l->wait40_focus = true;       /* réponse après l'arrêt du moteur (samyang.md § 6.1) */
        break;
    case 'M':                         /* mode service et notifications (samyang.md § 6.3, § 6.5) ; 26 (full:52-56) */
        if (sub == 0x31) l->notify_off = true;
        l->buf40[3] = 0x26;
        l->service = true;
        l->notify = true;
        break;
    case 'P':                         /* S13 */
        if (sub == 0x38) {            /* rangée si chaque quartet est sous 3, rien sinon (samyang.md § 6.3) */
            uint8_t v = (uint8_t)(m[3] - 0x30);
            if ((v >> 4) < 3 && (v & 0x0F) < 3) {
                l->custom = v;
                l->pos_up = v >> 4;   /* en RAM aussi (S14) */
                l->pos_dn = v & 0x0F;
            }
        } else {                      /* 'P' FA : l'octet de données 7 (samyang.md § 6.3) */
            l->buf40[10] = l->custom;
        }
        break;
    case 'V':                         /* 01 05 (full:43) ; l'analyse statique de la 1.06 dit 01 06 (samyang.md § 1.1) */
        {
            uint8_t v[19];
            if (sources135_msg(SRC_R40_V, v, sizeof v) == 19) memcpy(l->buf40 + 3, v + 3, 16);
        }
        break;
    default:
        break;
    }
    l->reply40 = 1;
}

static void reply40_task(l135_t *l)   /* la réponse différée (samyang.md § 6.1) */
{
    if (l->reply40 == 1) {
        l->t40 = l->now;
        l->reply40 = 2;
    }
    if (l->reply40 == 2 && elapsed(l, l->t40, l->p.reply40_us)) {
        if (l->wait40_focus) {
            if (focus_idle(l)) {
                l->wait40_focus = false;
                l->reply40 = 3;
            }
        } else {
            l->reply40 = 3;
        }
    } else if (l->reply40 == 3) {
        send_frame(l, 0x40, 0, 0);
        l->reply40 = 0;
    }
}

/* ─────────────────────────── 0x0A (samyang.md § 2.3) ─────────────────────────── */

static void task_0a(l135_t *l)        /* les deux étapes du 0x0A */
{
    if (l->step0a == 1) {
        if (l->slot != 1 && l->slot != 2) {
            l->step0a = 2;
            l->t0a = l->now;
            if (!iris_idle(l) || !focus_idle(l)) {
                l->job1d = 0;         /* 0x1D annulé, sans accusé */
                focus_stop_request(l);
                l->iris_armed = false; /* arrêt de l'iris (S4) */
                l->iris_moving = false;
            }
        }
    } else if (l->step0a == 2 && l->tx_state == 0) {
        if ((!iris_idle(l) || !focus_idle(l)) && !elapsed(l, l->t0a, l->p.wait_0a_us)) return;
        l->slots_on = false;
        l->flow = false;
        l->slot = 0;
        memcpy(l->mask, l->req0a + 1, 16);
        layout(l);
        send_frame(l, 0x0A, 0, 0);
        l->step0a = 0;
        for (int i = 0; i < 16; i++) {
            if (l->mask[i]) {
                l->flow = true;
                return;
            }
        }
        l->got03 = l->got04 = l->send05 = l->send06 = false;   /* les 0x05/0x06 en attente oubliés */
    }
}

/* ─────────────────────────── répartiteur (samyang.md § 2.6) ─────────────────────────── */

/* La configuration de la position courante du commutateur (S14). */
static uint8_t sw_cfg(const l135_t *l) { return l->in.m2 ? l->pos_dn : l->pos_up; }

/* Le forçage et l'offset 62, à chaque passe (S14). */
static void custom_task(l135_t *l)
{
    if (!l->got08) return;
    if (l->mode != 1) {
        l->sw_mf = false;
        return;
    }
    if (!l->body08 || l->old_body) {
        l->pos_up = CFG_AF;
        l->pos_dn = CFG_MF;
    }
    l->sw_mf = sw_cfg(l) == CFG_MF;
}

/* La table 0 de l'ouverture native (S15) : 23 codes, chacun six cases ; la case 132 est le 23e (132 / 6 = 22). Le
 * premier est l'ouverture la plus grande que publie la réponse au 0x08, ses offsets 0-1 (le gabarit, full:15 : BF 11,
 * 4543) ; les 22 autres, les tiers exacts de protocol.md § 8.1 de f/2 (4608) à f/22,6 (6400, les offsets 2-3 du même
 * 0x08), tronqués (samyang.md § 5.5). */
static uint16_t ap_table(int32_t i)
{
    uint8_t r08[202];
    int32_t k = i / 6;
    if (k > 0) return (uint16_t)(AP_F2 + (k - 1) * 256 / 3);
    if (sources135_msg(SRC_R08, r08, sizeof r08) < 3) return 0;
    return (uint16_t)(r08[1] | r08[2] << 8);
}

static int32_t ap_index(uint16_t sp)  /* la première case de la table qui atteint la consigne, bornée */
{
    int32_t i = 0;
    if (sp <= ap_table(0)) return 0;
    if (sp >= ap_table(AP_LAST)) return AP_LAST;
    while (ap_table(i) < sp) i++;
    return i;
}

static int32_t ring_step(l135_t *l)   /* le pas de la bague (samyang.md § 5.5 : le premier après 3 fronts) */
{
    int8_t d = 0;
    if (l->ring_ref != 0 || l->ring_cnt % 3 == 0) {
        d = (int8_t)(l->ring_cnt - l->ring_ref);
        l->ring_ref = l->ring_cnt;
    }
    return d;
}

static void h03(l135_t *l, const uint8_t *m)   /* le 0x03 : l'ouverture native (S15), l'iris (S4) */
{
    uint16_t sp = (uint16_t)(m[4] | m[5] << 8);   /* offsets 3-4 */
    uint8_t o12 = m[13];
    bool was = l->ap_on, on = false;
    l->got03 = true;
    l->do_iris = true;
    if (l->mode == 1) {
        on = sw_cfg(l) == CFG_APERTURE && l->body_af;
        l->ap_on = on;
        if (on && !was) l->ap_sync = true;
        if (l->ap_sync && !(o12 & 0x10)) {
            l->ap_hist[l->ap_hist_i] = sp;
            l->ap_hist_i = (uint8_t)((l->ap_hist_i + 1) & 3);
            if (abs((int)l->seq - (int)l->ap_seq) < 3 || l->ap_hist[2] != l->ap_hist[3]) return;
            l->ap_idx = ap_index(sp);
            l->ap_zeroed = false;
            l->ap_sync = false;
        }
        if (o12 & 0x10) {
            l->ap_seq = l->seq;
            l->ap_hist_i = 0;
            l->ap_hist[2] = 0;
            l->ap_hist[3] = 1;
        }
    }
    if (on) {
        if (o12 & 0x08) {
            out_of_model(l, "ouverture native, table 1 (bit 3 de l'offset 12 du 0x03) : non modélisée (S15)");
            return;
        }
        if (!l->ap_zeroed) {
            l->ring_cnt = 0;
            l->ring_ref = 0;
            l->ap_zeroed = true;
        }
        l->ap_was = true;
        l->ap_idx += ring_step(l);
        if (l->ap_idx < 0) l->ap_idx = 0;
        if (l->ap_idx > AP_LAST) l->ap_idx = AP_LAST;
        l->ap_pub = ap_table(l->ap_idx);
        l->ap_valid = true;
    } else {
        l->ap_zeroed = false;
        if (!l->ap_was) {
            l->ap_pub = 0;
            l->ap_valid = false;
        }
        l->ap_was = false;
    }
    if (sp != l->iris_setpoint) iris_move(l);  /* S4 */
    l->iris_setpoint = sp;
}

static void handle(l135_t *l, uint8_t type, const uint8_t *m)
{
    switch (type) {
    case 0x01:                        /* le drapeau du 0x01 en FF 01 00 (S14, samyang.md § 5.3) */
        if (m[7] == 0xFF && m[8] == 0x01 && m[9] == 0x00) l->old_body = true;
        send_frame(l, type, 0, 0);
        break;
    case 0x08:                        /* les bits 0x02 et 0x04 du 0x08 (S14, samyang.md § 5.3) */
        if (m[1] & 0x04) l->old_body = false;
        l->body08 = (m[1] & 0x06) != 0;
        l->got08 = true;
        send_frame(l, type, 0, 0);
        break;
    case 0x07: case 0x09:
        send_frame(l, type, 0, 0);    /* réponses immédiates */
        break;
    case 0x03:
        h03(l, m);
        break;
    case 0x04:                        /* samyang.md § 2.4 */
        l->body_af = (m[4] & 0x02) != 0;   /* offset 3, bit 1 (S15) */
        l->o60 = 0;                   /* S12 */
        l->do_frame = true;
        l->got04 = true;
        break;
    case 0x0A:                        /* différé à la tâche ; un second 0x0A relance l'étape 1 (samyang.md § 2.3) */
        memcpy(l->req0a, m, 17);
        l->step0a = 1;
        break;
    case 0x0B:
        l->buf0b = m[1];
        send_frame(l, 0x0B, 0, 0);
        break;
    case 0x0D:                        /* la période des créneaux (samyang.md § 2.4) : 60, 50, 48, 60 Hz */
        {
            static const uint16_t HZ[4] = {60, 50, 48, 60};
            l->slot_us = SLOT_US(HZ[m[1] & 3]);
        }
        send_frame(l, 0x0D, 0, 0);
        break;
    case 0x10:                        /* S8 */
        l->msg10 = m[1];
        l->busy_until = l->now + l->p.busy_0x10_us;
        homing_start(l);
        break;
    case 0x1C:                        /* l'ancien 0x1D accusé (samyang.md § 4.4) */
        if (l->job1d) {
            l->job1d = 0;
            l->ack1d = true;
        }
        l->job1c = 1;
        break;
    case 0x1D:                        /* l'ancien est accusé (samyang.md § 4.4) */
        if (l->job1d) l->ack1d = true;
        l->job1d = 1;
        memcpy(l->msg1d, m, 5);
        break;
    case 0x3F:
        send_frame(l, 0x3F, 0, 0);
        break;
    case 0x40:
        h40(l, m);
        break;
    default:
        unmodelled(l, type, "type connu du 135, hors de la liste blanche (spec § 7.1.3)");
        break;
    }
}

static void dispatch(l135_t *l)      /* le répartiteur (samyang.md § 2.6) */
{
    uint16_t i = 0;
    while (i != l->rx_len) {
        uint8_t type = l->rx_payload[i];
        uint16_t n;
        if (type == 0 || type > 0x4C) {
            out_of_model(l, "type hors de la table de réception : effet non établi (samyang.md § 2.6)");
            return;
        }
        n = RX_LEN[type];
        if (n == 0) {                 /* le compte n'avance plus : boucle sans fin (samyang.md § 2.6) */
            l->blocked = true;
            return;
        }
        if ((uint32_t)i + n > l->rx_len) {
            out_of_model(l, "sous-message plus long que la fin de la trame (samyang.md § 2.6)");
            return;
        }
        if (type == 0x03) l->seq = l->rx_seq;   /* le numéro du 0x03 (protocol.md § 3.4) */
        handle(l, type, &l->rx_payload[i]);
        if (l->oom) return;
        i = (uint16_t)(i + n);
    }
    l->rx_state = 0;
}

/* L'analyseur de trame (protocol.md § 3, samyang.md § 2.6). Une trame abandonnée ne laisse aucune trace. */
static void parse(l135_t *l)
{
    while (l->rx_state != 9 && l->tail != l->head) {
        uint8_t b = l->ring[l->tail];
        l->tail = (uint16_t)((l->tail + 1) % L135_RX_RING);
        switch (l->rx_state) {
        case 0: if (b == 0xF0) l->rx_state = 1; break;
        case 1: l->rx_len_lo = b; l->rx_state = 2; break;
        case 2:
            if (b != 0) { l->rx_state = 0; break; }         /* plus de 255 octets */
            l->rx_len = l->rx_len_lo;
            if (l->rx_len < 9) { l->rx_state = 0; break; }
            l->rx_len = (uint16_t)(l->rx_len - 8);
            l->rx_state = 3;
            break;
        case 3:
            if (b == 0 || b > 3) l->rx_state = 0;           /* classe */
            else { l->rx_cls = b; l->rx_state = 4; }
            break;
        case 4: l->rx_seq = b; l->rx_i = 0; l->rx_state = 5; break;
        case 5:
            l->rx_payload[l->rx_i++] = b;
            if (l->rx_i == l->rx_len) l->rx_state = 6;
            break;
        case 6: l->ck_lo = b; l->rx_state = 7; break;
        case 7: l->ck_hi = b; l->rx_state = 8; break;
        case 8: {
            uint16_t s = (uint16_t)(l->rx_seq + l->rx_cls + l->rx_len_lo);   /* la somme (protocol.md § 3.1) */
            for (uint16_t i = 0; i < l->rx_len; i++) s = (uint16_t)(s + l->rx_payload[i]);
            if (b == 0x55 && (s & 0xFF) == l->ck_lo && (s >> 8) == l->ck_hi) {
                l->rx_state = 9;
                l->t_dispatch = l->now + l->p.loop_us;   /* S1 */
            } else {
                l->rx_state = 0;
            }
            break;
        }
        default: break;
        }
    }
}

/* ─────────────────────────── poignée de main (protocol.md § 2) ─────────────────────────── */

static void handshake(l135_t *l)      /* les étapes 1 à 5 de protocol.md § 2 */
{
    switch (l->hs) {
    case 0: if (l->body_cs) { l->t_hs = l->now; l->hs = 1; } break;
    case 1:
        if (elapsed(l, l->t_hs, l->p.hs_us) && l->body_cs) l->hs = 2;
        break;
    case 2: set_lens_cs(l, true); l->hs = 3; break;
    case 3: l->hs = 4; break;         /* la ligne de sortie de l'étape 2 levée */
    case 4: if (l->p.hs_in_high) { l->t_hs = l->now; l->hs = 5; } break;
    case 5:
        if (elapsed(l, l->t_hs, l->p.hs_us) && l->p.hs_in_high) { l->uart_open = true; l->hs = 6; }
        break;
    case 6: if (!l->body_cs) { l->t_hs = l->now; l->hs = 7; } break;
    case 7: if (elapsed(l, l->t_hs, l->p.hs_us) && !l->body_cs) l->hs = 8; break;
    case 8: set_lens_cs(l, false); l->hs = 0; l->link = 10; break;
    default: break;
    }
}

/* ─────────────────────────── boucle de fond ─────────────────────────── */

static void pass(l135_t *l)
{
    if (!l->powered || l->blocked || l->oom || l->now < l->busy_until) return;
    parse(l);
    if (l->rx_state == 9 && l->now >= l->t_dispatch) {
        dispatch(l);
        if (l->blocked || l->oom || l->now < l->busy_until) return;
    }
    if (l->link == 0) handshake(l);
    if (l->flow && l->tx_state == 0) {                /* seulement en flux (samyang.md § 2.4) */
        if (l->send05) { l->send05 = false; emit_05(l); }
        if (l->send06) { l->send06 = false; emit_06(l); }
    }
    if (l->do_iris) { l->do_iris = false; homing_iris(l); }
    if (l->do_frame) { l->do_frame = false; frame_processing(l); }
    reply40_task(l);
    task_0a(l);
    custom_task(l);                                   /* à chaque passe (S14) */
    tx_task(l);
}

static void settle(l135_t *l)
{
    for (int i = 0; i < 12; i++) pass(l);
}

/* ─────────────────────────── interruptions ─────────────────────────── */

static uint64_t t_focus_event(const l135_t *l)   /* arrivée ou front de fourche */
{
    int64_t edge;
    uint32_t v;
    if (!l->moving || l->f.frozen_position) return UINT64_MAX;
    v = speed_steps_per_s(l, l->speed);
    edge = l->target;
    if (l->dir < 0 && !l->pi && l->target <= l->p.fork_low && l->m0 > l->p.fork_low) edge = l->p.fork_low;
    if (l->dir > 0 && l->pi && l->target >= l->p.fork_high && l->m0 < l->p.fork_high) edge = l->p.fork_high;
    {
        uint64_t d = (uint64_t)(edge > l->m0 ? edge - l->m0 : l->m0 - edge);
        return l->t_m0 + (d * 1000000u + v - 1) / v;
    }
}

/* La fourche : le front, la position au front, et hors homing en mode service une notification 'H' (côté bas)
 * ou 'L' (côté haut), sauf après 'M' 31 (samyang.md § 6.4). */
static void fork_isr(l135_t *l)
{
    l->pi = !l->pi;
    l->edge = published(l);
    if (l->service && l->homing == 0 && !l->notify_off) {
        uint8_t save[3] = {l->buf40[1], l->buf40[3], l->buf40[4]};
        l->buf40[1] = l->pi ? 'H' : 'L';
        l->buf40[3] = (uint8_t)l->edge;
        l->buf40[4] = (uint8_t)(l->edge >> 8);
        send_frame(l, 0x40, 0, 0);
        l->buf40[1] = save[0];
        l->buf40[3] = save[1];
        l->buf40[4] = save[2];
    }
}

static void focus_event(l135_t *l)
{
    int64_t m = mech(l);
    freeze(l);
    if (m == l->target) {
        l->moving = false;
    }
    if ((l->dir < 0 && !l->pi && m <= l->p.fork_low) || (l->dir > 0 && l->pi && m >= l->p.fork_high)) fork_isr(l);
}

static void slot_isr(l135_t *l)      /* les créneaux (samyang.md § 2.4) */
{
    switch (l->slot) {
    case 0: if (l->got03) { l->got03 = false; l->send05 = true; } break;
    case 1: if (l->got04) { l->got04 = false; l->send06 = true; } break;
    case 3: if (!l->flow) l->do_iris = true; break;
    case 4: if (!l->flow) l->do_frame = true; break;
    default: break;
    }
    l->slot++;
    if (l->slot == 8) l->slots_on = false;
    else l->t_slot += l->slot_us;
}

static void tx_byte_isr(l135_t *l)
{
    if (l->txq_n == 0) {
        l->tx_isr = false;
        return;
    }
    out_push(l, L135_OUT_BYTE, l->txq[l->txq_head]);
    l->txq_head = (uint16_t)((l->txq_head + 1) % L135_TX_CAP);
    l->txq_n--;
    if (l->txq_n == 0) l->tx_isr = false;
    else l->t_next_byte += l->p.byte_us;
}

static void fire_due(l135_t *l)
{
    if (!l->powered) return;
    if (l->tx_isr && l->t_next_byte <= l->now) tx_byte_isr(l);
    if (l->slots_on && l->t_slot <= l->now) slot_isr(l);
    if (t_focus_event(l) <= l->now) focus_event(l);
    if (l->iris_moving && l->t_iris_end <= l->now) l->iris_moving = false;
}

/* ─────────────────────────── API ─────────────────────────── */

uint64_t l135_next_event(const l135_t *l)
{
    uint64_t t = UINT64_MAX;
#define CAND(x) do { uint64_t c_ = (x); if (c_ > l->now && c_ < t) t = c_; } while (0)
#define CAND_DUE(x) do { uint64_t c_ = (x); if (c_ < t) t = c_ < l->now ? l->now : c_; } while (0)
    if (!l->powered) return t;
    if (l->tx_isr) CAND_DUE(l->t_next_byte);
    if (l->slots_on) CAND_DUE(l->t_slot);
    CAND_DUE(t_focus_event(l));
    if (l->iris_moving) CAND_DUE(l->t_iris_end);
    if (l->blocked || l->oom) return t;
    CAND(l->busy_until);
    if (l->rx_state == 9) CAND(l->t_dispatch);
    if (l->link == 0 && (l->hs == 1 || l->hs == 5 || l->hs == 7)) CAND(l->t_hs + l->p.hs_us + 1);
    if (l->reply40 == 2) CAND(l->t40 + l->p.reply40_us + 1);
    if (l->step0a == 2) CAND(l->t0a + l->p.wait_0a_us + 1);
    if (l->tx_state == 1) CAND(l->tx_hold_until);
    if (l->tx_state == 4) CAND(l->t_tx + l->p.lens_cs_tail_us + 1);
#undef CAND
#undef CAND_DUE
    return t;
}

void l135_advance(l135_t *l, uint64_t t)
{
    for (;;) {
        uint64_t te = l135_next_event(l);
        if (te > t || te == UINT64_MAX) break;
        l->now = te;
        fire_due(l);
        settle(l);
    }
    if (t > l->now) l->now = t;
}

/* Tout l'état entre `powered` et les sorties vaut 0 au démarrage : toutes les variables de session de
 * l'objectif y sont à zéro (samyang.md § 2.1). Paramètres, pannes, horloge et sorties déjà
 * produites ne sont pas l'objectif : ils restent. */
static void reset_ram(l135_t *l)
{
    memset((char *)l + offsetof(l135_t, powered), 0, offsetof(l135_t, out) - offsetof(l135_t, powered));
    l->slot_us = SLOT_US(60u);                      /* 60 Hz avant tout 0x0D (samyang.md § 2.4) */
    l->m0 = l->p.initial_position;
    l->t_m0 = l->now;
    l->pi = l->p.initial_position <= l->p.fork_low;
    l->pos_up = l->custom >> 4;                     /* la flash rechargée (S14) */
    l->pos_dn = l->custom & 0x0F;
    l->body_af = true;                              /* tenu pour posé avant tout 0x04 (S15) */
}

void l135_init(l135_t *l, const l135_params_t *p)
{
    memset(l, 0, sizeof *l);
    l->p = *p;
    l->custom = 0x10;                  /* d'usine (S13) */
    reset_ram(l);
}

void l135_power(l135_t *l, uint64_t t, bool on)
{
    l135_advance(l, t);
    if (on && !l->powered) {
        reset_ram(l);
        l->powered = true;
        l->busy_until = t + l->p.boot_us;   /* liaison 0 : poignée de main, UART fermée */
    } else if (!on && l->powered) {
        set_lens_cs(l, false);
        l->powered = false;
    }
    settle(l);
}

void l135_start_powered(l135_t *l, uint64_t t, bool flow, bool service)
{
    l135_advance(l, t);
    reset_ram(l);
    l->powered = true;
    l->link = 10;
    l->uart_open = true;
    l->mode = 1;                       /* un homing avec iris a eu lieu, bouton non tenu */
    l->service = l->notify = service;
    memcpy(l->mask, TRACED_MASK, 16);
    layout(l);
    l->flow = flow;
}

void l135_body_cs(l135_t *l, uint64_t t, bool high)
{
    l135_advance(l, t);
    l->body_cs = high;
    settle(l);
}

void l135_byte(l135_t *l, uint64_t t, uint8_t b)
{
    l135_advance(l, t);
    if (l->powered && l->uart_open && l->body_cs) {    /* rangé seulement BODY_CS haute (samyang.md § 2.6) */
        uint16_t next = (uint16_t)((l->head + 1) % L135_RX_RING);
        if (next == l->tail) out_of_model(l, "anneau de réception plein");
        else {
            l->ring[l->head] = b;
            l->head = next;
        }
    }
    settle(l);
}

void l135_vd(l135_t *l, uint64_t t)  /* un front VD (samyang.md § 2.4) */
{
    l135_advance(l, t);
    if (!l->powered) return;
    if (l->flow) l->seq = l->seq == 0xEF ? 0 : (uint8_t)(l->seq + 1);   /* protocol.md § 3.4 */
    if (l->iris_armed) {
        l->iris_armed = false;
        l->iris_moving = true;
        l->t_iris_end = l->now + l->p.iris_move_us;
    }
    vd_focus(l);
    l->slot = 0;
    l->slots_on = true;
    l->t_slot = l->now + l->slot_us;
    settle(l);
}

void l135_ring_edge(l135_t *l, uint64_t t, bool up)   /* un front de la bague (S12) */
{
    l135_advance(l, t);
    if (!l->powered) return;
    l->ring_cnt += up ? 1 : -1;
    if (sw_cfg(l) == CFG_AF) l->o60 = up ? 0x01 : 0xFF;
    settle(l);
}

bool l135_out(l135_t *l, l135_out_t *o)
{
    if (l->out_n == 0) return false;
    *o = l->out[l->out_head];
    l->out_head = (l->out_head + 1) % L135_OUT_CAP;
    l->out_n--;
    return true;
}

bool l135_blocked(const l135_t *l) { return l->blocked; }
bool l135_out_of_model(const l135_t *l) { return l->oom; }
uint32_t l135_unmodelled(const l135_t *l, uint8_t type) { return l->unmodelled[type]; }
int32_t l135_position(const l135_t *l) { return published(l); }
