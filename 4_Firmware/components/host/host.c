/* SOURCE: PROTOCOL.md ; spec de l'atelier § 4.5 — la couche HOTE (bsk_host.h)
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — C pur, sans ESP-IDF : compilé aussi par les tests hôte
 *
 * La couche HOTE : la table de traduction de la spec, § 4.5.1 à § 4.5.6, lettre par lettre ; PROTOCOL.md donne la
 * forme exacte des lignes. */
#include "bsk_host.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsk_bench_core.h"
#include "bsk_contract.h"
#include "bsk_journal.h"
#include "bsk_led.h"
#include "bsk_session.h"
#include "lens_names.h"

static struct {
    const char  *version;      /* la version du firmware de la carte, rendue par `v` */
    const char  *reset;
    uint32_t     seq;          /* le dernier seq alloué (§ 4.1 : strictement croissant) */
    bool         hold;         /* le maintien de p0, tenu ici : l'instantané ne distingue pas « OFF après p0 » */
    uint32_t     pend_seq;     /* la commande qu'on dépose */
    bsk_cmd_op_t pend_op;
    bsk_ack_t    pend_last;    /* le dernier accusé de la commande déposée, rendu pendant son dépôt (q) */
    bool         fly;          /* une commande en vol dont l'accusé final compte : GOTO, MOVE */
    uint32_t     fly_seq;
    char         last_op[48];
    const char  *move_rc;
    const char  *move_end;     /* `g` : le résultat du dernier déplacement suivi (PROTOCOL.md § 2), écrit avec move_rc */
    uint16_t     unconfirmed;  /* le dernier stop_unconfirmed lu ; 0 avec la session, au démarrage de la carte */
    uint32_t     target;       /* :SN, gardée pour :FG# et :GN# ; vit autant que la carte */
    bool         have_target;
    bool         pos_have;     /* la dernière position publiée avec position_valid, et l'identité d'alors */
    int32_t      pos;
    uint32_t     pos_product;
    char         pos_name[65];
    bool         cu_wait;      /* CMD_LENS_CUSTOM acceptée, son accusé final attendu */
    bool         cu_done;      /* ... reçu (cu_ack), la réponse pas encore rendue (bsk_host_later) */
    uint32_t     cu_seq;
    bsk_ack_t    cu_ack;
} h;

const char BSK_HOST_LATER[] = "";   /* comparé par adresse (bsk_host.h), jamais écrit */

static void put(char *out, size_t cap, const char *s) { snprintf(out, cap, "%s", s); }

/* ─────────────────────────── last_op et move_rc (§ 4.5.3) ─────────────────────────── */

static void on_ack(const bsk_ack_t *a, const bsk_status_t *st)
{
    if (a->result == ACK_ACCEPTED) {
        if (a->seq == BSK_SEQ_BOARD) {              /* le goto du bouton du fût, le seul que la carte dépose */
            h.fly = true;
            h.fly_seq = a->seq;
        } else if (a->seq == h.pend_seq && (h.pend_op == CMD_FOCUS_GOTO || h.pend_op == CMD_FOCUS_MOVE)) {
            h.fly = true;
            h.fly_seq = a->seq;
        } else if (a->seq == h.pend_seq && h.pend_op == CMD_LENS_CUSTOM) {
            h.cu_wait = true;
            h.cu_seq = a->seq;
        }
        return;
    }
    if (h.cu_wait && a->seq == h.cu_seq) {          /* la réponse de `CUSTOM` est connue */
        h.cu_wait = false;
        h.cu_done = true;
        h.cu_ack = *a;
        return;
    }
    if (!h.fly || a->seq != h.fly_seq) return;     /* un refus, ou l'accusé d'une autre commande */
    h.fly = false;
    if (a->result == ACK_COMPLETED) {
        h.move_rc = "ok";
    } else {                                        /* la session a quitté READY : move_rc oublié */
        h.move_rc = st->session_state == SESSION_READY ? bsk_err_token(a->reason) : "-";
    }
    h.move_end = h.move_rc;                         /* la même règle : g et move_rc ne divergent jamais sur une fin */
}

/* La dernière position publiée avec position_valid (§ 4.5.4), oubliée en OFF et quand l'identité change : quand un de
 * ses champs, publié, diffère de celui de la position gardée. Un champ que la session n'a pas encore publié n'est pas
 * un changement : une session qui redémarre sur le même objectif (`b`) part sans identité et publie son 0x07 avant
 * son nom (0x3F). */
static void keep_position(const bsk_status_t *st)
{
    bool other = (st->lens_id_product && st->lens_id_product != h.pos_product) ||
                 (st->lens_name[0] && strcmp(st->lens_name, h.pos_name));
    if (st->session_state == SESSION_OFF || other) h.pos_have = false;
    if (!st->position_valid) return;
    h.pos_have = true;
    h.pos = st->focus_position;
    h.pos_product = st->lens_id_product;
    memcpy(h.pos_name, st->lens_name, sizeof h.pos_name);
}

/* Les accusés dans l'ordre, puis l'état de la session : READY ou FAULT, le résultat du dernier démarrage. Seul le
 * démarrage écrit last_op : l'état publié suffit, sans retenir le précédent. `want` : le seq dont on rend le premier
 * accusé dans `first` (0 : aucun). */
static void observe_(uint32_t want, bsk_ack_t *first)
{
    bsk_status_t st;
    bsk_ack_t a;
    bool got = false;
    bsk_session_status(&st);
    keep_position(&st);
    while (bsk_session_ack(&a)) {
        if (first && !got && a.seq == want) {
            *first = a;
            got = true;
        }
        if (first && a.seq == want) h.pend_last = a;
        on_ack(&a, &st);
    }
    if (st.stop_unconfirmed != h.unconfirmed) h.move_end = "unconfirmed";   /* l'arrêt du dernier déplacement suivi */
    h.unconfirmed = st.stop_unconfirmed;
    if (st.session_state == SESSION_READY) put(h.last_op, sizeof h.last_op, "boot:ok");
    else if (st.session_state == SESSION_FAULT)
        snprintf(h.last_op, sizeof h.last_op, "boot:er:\"%s\"", bsk_err_token(st.last_error));
}

void bsk_host_observe(void) { observe_(0, NULL); }

/* ─────────────────────────── dépôt et refus ─────────────────────────── */

/* Une lecture ne dépose rien : elle lit l'instantané. Une commande dépose un bsk_cmd_t et rend son premier accusé :
 * ACK_ACCEPTED -> `ok`, ACK_REJECTED -> `er …` d'après la raison (refusal) ; `q` lit aussi son accusé final, rendu
 * pendant le dépôt : ACK_FAILED -> `er …`. Hors READY, une lettre qui a besoin de l'objectif répond d'après le tableau
 * d'état du § 4.5.1, lu dans l'instantané, sans rien déposer (not_ready) : la session refuserait de toute façon ce que
 * ce tableau refuse. */

/* Dépose une commande ; rend son premier accusé. La session en dépose un pour chaque commande que HOTE dépose, dans
 * l'appel, et la file est vide avant lui (bsk_host_observe après chaque pas) : il est toujours lu. */
static bsk_ack_t deposit(bsk_cmd_op_t op, int32_t arg)
{
    bsk_cmd_t c = {.seq = ++h.seq, .op = op, .arg = arg};
    bsk_ack_t first = {0};
    h.pend_seq = c.seq;
    h.pend_op = op;
    bsk_session_command(&c);
    observe_(c.seq, &first);
    return first;
}

/* La réponse à un ACK_REJECTED en READY (§ 4.5.1, tableau des raisons). Aucune ne contient `ok` (PROTOCOL.md § 1). */
static void refusal(char *out, size_t cap, bsk_err_t why)
{
    switch (why) {
    case E_BUSY: put(out, cap, "er busy"); break;
    case E_LIMIT: put(out, cap, "er range limits"); break;
    case E_NOCAP: put(out, cap, "er nocap"); break;               /* sans jeton : les marques n'en ont pas (PROTOCOL.md § 1) */
    case E_FORBIDDEN: put(out, cap, "er refused"); break;
    default: snprintf(out, cap, "er link %s", bsk_err_token(why)); break;
    }
}

/* Dépose, puis `ok` ou le refus. */
static bsk_ack_t command(bsk_cmd_op_t op, int32_t arg, char *out, size_t cap)
{
    bsk_ack_t a = deposit(op, arg);
    if (a.result == ACK_ACCEPTED) put(out, cap, "ok");
    else refusal(out, cap, a.reason);
    return a;
}

/* ─────────────────────────── l'état (§ 4.5.1) ─────────────────────────── */

static bool is_ready(const bsk_status_t *st) { return st->session_state == SESSION_READY; }

static bool in_sequence(const bsk_status_t *st)
{
    return st->session_state != SESSION_OFF && st->session_state != SESSION_READY && st->session_state != SESSION_FAULT;
}

/* Le tableau d'état hors READY, pour une lettre qui a besoin de l'objectif ; `core` : une lettre du cœur
 * Pinefeat (colonne « f r a d c s »). */
static const char *not_ready(const bsk_status_t *st, bool core)
{
    if (st->session_state == SESSION_OFF) return core ? "nc" : "er nolens";
    if (st->session_state == SESSION_FAULT) return "er fault";
    return "er busy boot";
}

/* L'identité est établie : un nom, ou un code modèle (§ 4.5.1, sous le tableau). */
static bool identified(const bsk_status_t *st) { return st->lens_name[0] || st->lens_id_product; }

/* `i` : le nom, sinon la table de repli, sinon `#<code>`, sinon `-`. */
static void identity(const bsk_status_t *st, char *out, size_t cap)
{
    const char *n = st->lens_id_product ? lens_name_lookup((uint16_t)st->lens_id_product) : NULL;
    if (st->lens_name[0]) put(out, cap, st->lens_name);
    else if (n) put(out, cap, n);
    else if (st->lens_id_product) snprintf(out, cap, "#%lu", (unsigned long)st->lens_id_product);
    else put(out, cap, "-");
}

static void mark(const bsk_status_t *st, char *out, size_t cap)
{
    if (st->mark_valid) snprintf(out, cap, "%ld", (long)st->mark_position);
    else put(out, cap, "-");
}

static bool moving(const bsk_status_t *st)
{
    return st->motion_state == MOTION_PLANNING || st->motion_state == MOTION_COMMANDED || st->motion_state == MOTION_MOVING ||
           st->motion_state == MOTION_SETTLING;
}

/* ─────────────────── l'ouverture : la conversion f/ <-> code 256(Av+16) est ici, la session tient les codes ─────────────────── */

/* Le code de f/ x10. Appelée sur une cible jugée dans la plage ± 0,05 (aperture_line), dont le minimum est au moins f/0,5
 * (le domaine, bsk_contract.h) : fnum_x10 n'est jamais nul. */
static uint16_t ap_code_from_fnum_x10(uint16_t fnum_x10)
{
    double n = fnum_x10 / 10.0;
    return (uint16_t)lround(256.0 * (2.0 * log2(n) + 16.0));
}

/* f/ x10 depuis le code 256(Av+16), arrondi au tiers de diaphragme usuel : Av = 9 donne 22.6 en exact, que
 * tout le monde appelle f/22 ; au-delà d'un demi-tiers d'un cran usuel, la valeur exacte. Un code du domaine seulement
 * (ap_known) : f/0,5 à f/256, 5 à 2560. */
static const uint16_t thirds[] = {10, 11, 12, 14, 16, 18, 20, 22, 25, 28, 32, 35, 40, 45, 50, 56, 63, 71,
                                  80, 90, 100, 110, 130, 140, 160, 180, 200, 220, 250, 290, 320, 360, 400, 450};
static uint16_t fnum_x10_from_ap_code(uint16_t code)
{
    double av = code / 256.0 - 16.0;
    double f = 10.0 * pow(2.0, av / 2.0);
    uint16_t best = (uint16_t)lround(f);
    double bd = 1e9;
    for (size_t i = 0; i < sizeof thirds / sizeof thirds[0]; i++) {
        double d = fabs(log(thirds[i] / f));
        if (d < bd) { bd = d; best = thirds[i]; }
    }
    return bd < 0.06 ? best : (uint16_t)lround(f);
}

/* Un code du domaine que la carte convertit (bsk_contract.h) ; hors de lui, il n'est jamais converti. */
static bool ap_known(uint16_t code) { return code >= BSK_AP_CODE_MIN && code <= BSK_AP_CODE_MAX; }

/* La plage en f/ x10. Faux sans plage : pas de 0x08, ou un code hors du domaine. */
static bool aperture_get(const bsk_status_t *st, uint16_t *mn, uint16_t *mx)
{
    if (!(st->capabilities & CAP_APERTURE) || !ap_known(st->aperture_min) || !ap_known(st->aperture_max)) return false;
    *mn = fnum_x10_from_ap_code(st->aperture_min);
    *mx = fnum_x10_from_ap_code(st->aperture_max);
    return true;
}

/* L'ouverture courante relue, en f/ x10, bornée à la plage : le Samyang répète la consigne, le Sony borne ; on borne
 * nous-mêmes. Faux si elle est inconnue, hors du domaine (0 : la session sans 0x05, ou son flux arrêté) : jamais une
 * valeur ramenée à la plage. */
static bool aperture_cur(const bsk_status_t *st, uint16_t mn, uint16_t mx, uint16_t *cur)
{
    if (!ap_known(st->aperture_current)) return false;
    *cur = fnum_x10_from_ap_code(st->aperture_current);
    if (*cur < mn) *cur = mn;
    if (*cur > mx) *cur = mx;
    return true;
}

/* `a`, `a<f>`, `a+x`, `a-x` en READY. Le texte (`er range num`, la plage ± 0,05) est jugé ici ; la session borne la
 * consigne à la plage en codes. Chaque réponse tient en 15 caractères (PROTOCOL.md § 1, règle 4) : les erreurs ne
 * portent pas la plage, que `a` rend (`<min>-<max>`, 9 caractères au plus : f/0,5 à f/256). */
static void aperture_line(const char *arg, const bsk_status_t *st, char *out, size_t cap)
{
    uint16_t cur = 0, mn, mx;
    char *end;
    double v, target;
    bool rel = *arg == '+' || *arg == '-';
    if (!aperture_get(st, &mn, &mx)) {
        put(out, cap, "er nocap ap");
        return;
    }
    if (!*arg) {
        snprintf(out, cap, "%u.%u-%u", (unsigned)(mn / 10), (unsigned)(mn % 10), (unsigned)(mx / 10));
        return;
    }
    if (!isdigit((unsigned char)arg[0]) && arg[0] != '+' && arg[0] != '-' && arg[0] != '.') {
        put(out, cap, "er range num");
        return;
    }
    v = strtod(arg, &end);
    if (end == arg || *end != 0 || !isfinite(v)) {
        put(out, cap, "er range num");                /* NaN, inf refusés (PROTOCOL.md § 2) */
        return;
    }
    if (rel && !aperture_cur(st, mn, mx, &cur)) {
        put(out, cap, "er nocap ap");                 /* relatif à une ouverture inconnue (PROTOCOL.md § 2) */
        return;
    }
    target = rel ? cur / 10.0 + v : v;
    if (!isfinite(target) || target < mn / 10.0 - 0.05 || target > mx / 10.0 + 0.05) {
        put(out, cap, "er range ap");                 /* la plage se lit par `a` seul (PROTOCOL.md § 2) */
        return;
    }
    command(CMD_APERTURE_SET, ap_code_from_fnum_x10((uint16_t)(target * 10 + 0.5)), out, cap);
}

/* `o` : l'ouverture courante relue ; CAP_APERTURE_READBACK. Inconnue, `er link aperture` (PROTOCOL.md § 2). */
static void aperture_now(const bsk_status_t *st, char *out, size_t cap)
{
    uint16_t cur, mn, mx;
    if (!(st->capabilities & CAP_APERTURE_READBACK) || !aperture_get(st, &mn, &mx)) put(out, cap, "er nocap ap");
    else if (!aperture_cur(st, mn, mx, &cur)) put(out, cap, "er link aperture");
    else snprintf(out, cap, "%u.%u", (unsigned)(cur / 10), (unsigned)(cur % 10));
}

/* ─────────────────────────── les lettres (§ 4.5.2) ─────────────────────────── */

/* Là où la spec laisse un choix :
 *   - une lettre sans opération dans le contrat (`l`, `n`, `w`, `p`, `pl*`, `pm*`) ne dépend pas de l'état ; `a…` et
 *     `o`, que le tableau d'état nomme, en dépendent ;
 *   - `s<n>` -> `ok` dans tout état, bien que la colonne « f r a d c s » le range avec les lettres du cœur ;
 *   - en FAULT, les lettres de marque qui déposent (`js`, `jx`, `jg`) -> `er fault` ;
 *   - l'identité est « établie » (`o`, `i`, `j` en FAULT) quand l'instantané porte un nom ou un code modèle ;
 *   - un nombre est lisible s'il n'a que des chiffres (`f12x`, `m12x` -> `er range num`) ; lisible et au-delà de
 *     65535, `er range 65535`, pour `f<n>`, `f±n` et `m<n>` ;
 *   - `m<n>`, c'est `m` suivi d'un chiffre ; tout le reste après `m` est une ligne inconnue (`m+5`, `ms`) ;
 *   - une ligne est reconnue exactement (`hx` -> `er nocap`) : une ligne que PROTOCOL.md ne connaît pas répond
 *     `er nocap`. */

/* Que des chiffres, au moins un. L'accumulation cesse dès que la valeur dépasse 65535 : `v` est alors le premier préfixe
 * au-dessus de 65535 (`70000` donne 70000, `999999` donne 99999), et les appelants refusent toute valeur au-delà de
 * 65535. */
static bool number(const char *s, uint32_t *v)
{
    if (!*s) return false;
    for (*v = 0; *s; s++) {
        if (*s < '0' || *s > '9') return false;
        if (*v <= 0xFFFF) *v = *v * 10 + (uint32_t)(*s - '0');
    }
    return true;
}

/* `f<n>`, `f+n`, `f-n` en READY : le texte d'abord (§ 4.5.1), puis le contrat. Un relatif dont le résultat sort de
 * 0..65535 répond `er range 65535` sans rien déposer (PROTOCOL.md § 2) ; dans 0..65535, la session vérifie les bornes
 * (`er range limits`). Sans 0x06, la position est inconnue : la session refuse (E_LIMIT). */
static void focus_cmd(const char *a, const bsk_status_t *st, char *out, size_t cap)
{
    bool rel = *a == '+' || *a == '-';
    uint32_t v;
    int32_t d;
    if (!number(rel ? a + 1 : a, &v)) {
        put(out, cap, "er range num");
        return;
    }
    if (v > 0xFFFF) {
        put(out, cap, "er range 65535");
        return;
    }
    if (!rel) {
        command(CMD_FOCUS_GOTO, (int32_t)v, out, cap);
        return;
    }
    d = *a == '-' ? -(int32_t)v : (int32_t)v;
    if (st->position_valid && (st->focus_position + d < 0 || st->focus_position + d > 0xFFFF)) put(out, cap, "er range 65535");
    else command(CMD_FOCUS_MOVE, d, out, cap);
}

/* `j…`, la marque : `j` la lit ; `js`, `jg`, `jx`, reconnues exactement. */
static void mark_line(const char *l, const bsk_status_t *st, char *out, size_t cap)
{
    if (!l[1]) {                                    /* j : une lecture */
        if (is_ready(st) || (st->session_state == SESSION_FAULT && identified(st))) mark(st, out, cap);
        else put(out, cap, st->session_state == SESSION_FAULT ? "er nolens" : not_ready(st, false));
    } else if (strcmp(l, "js") && strcmp(l, "jx") && strcmp(l, "jg")) {
        put(out, cap, "er nocap");                  /* une ligne inconnue, dans tout état */
    } else if (!is_ready(st)) {
        put(out, cap, not_ready(st, false));        /* en FAULT : er fault */
    } else if (l[1] == 's') {
        command(CMD_SET_MARK, BSK_MARK_HERE, out, cap);
    } else if (l[1] == 'x') {
        command(CMD_CLEAR_MARK, 0, out, cap);
    } else if (st->mark_valid) {                    /* jg : tel quel, sans recette d'approche */
        command(CMD_FOCUS_GOTO, st->mark_position, out, cap);
    } else {
        put(out, cap, "er range nomark");
    }
}

/* `b` */
static void reboot(const bsk_status_t *st, char *out, size_t cap)
{
    bsk_ack_t a;
    if (st->session_state == SESSION_OFF && !h.hold) {   /* OFF sans p0 : D2 absent */
        put(out, cap, "er nolens");
        return;
    }
    a = st->session_state == SESSION_FAULT ? deposit(CMD_CLEAR_FAULT, 0) : deposit(CMD_ATTACH, 0);
    if (a.result == ACK_ACCEPTED) {
        h.hold = false;
        put(out, cap, "ok");
    } else if (a.reason == E_BUSY) {
        put(out, cap, "er busy move");              /* E_BUSY : un mouvement en vol, en READY seulement */
    } else {
        refusal(out, cap, a.reason);
    }
}

static void power(const char *l, const bsk_status_t *st, char *out, size_t cap)
{
    bsk_ack_t a;
    if (!strcmp(l, "p1")) {                         /* dans tout état, D2 absent compris */
        a = command(CMD_ATTACH, 0, out, cap);
        if (a.result == ACK_ACCEPTED) h.hold = false;
    } else if (!strcmp(l, "p0")) {
        a = deposit(CMD_DETACH, 0);
        if (a.result == ACK_ACCEPTED) {
            h.hold = true;
            put(out, cap, "ok");
        } else if (in_sequence(st)) {
            put(out, cap, "er busy boot");          /* pas pendant un démarrage */
        } else {
            refusal(out, cap, a.reason);
        }
    } else {                                        /* p, pl0, pl1, pm0, pm1, et le reste : aucune opération */
        put(out, cap, "er nocap");
    }
}

static const char *tri(uint8_t v) { return v == BSK_YES ? "1" : v == BSK_NO ? "0" : "-"; }

/* Le rôle de la bague ; `-` inconnu (l'offset 62 pas publié). */
static const char *ring_role(uint8_t r) { return r == RING_APERTURE ? "ap" : r == RING_FOCUS ? "focus" : "-"; }

/* `t` (PROTOCOL.md § 5) : l'état de la carte seul, rien qu'une autre lettre rend déjà (`v`, `r`, `j`, `i`, `n`), aucun
 * compteur ni diagnostic (`DEBUG`). La page et INDI la lisent : `clé=valeur` séparées d'une espace,
 * `last_op=<op>:<ok|er>` suivi de `:"<raison>"`. `ext=1` en tête : la carte sert les lettres au-delà du sous-ensemble
 * Pinefeat (PROTOCOL.md § 2). */
static void line_t(const bsk_status_t *st, char *out, size_t cap)
{
    snprintf(out, cap, "ext=1 present=%d boot_state=%s busy=%s power_off=%d last_op=%s mf=%s oss=%s ring=%s",
             st->session_state != SESSION_OFF, bsk_state_name(st->session_state), in_sequence(st) ? "boot" : "-", h.hold,
             h.last_op, tri(st->mf), tri(st->oss), ring_role(st->ring));
}

static void letter(const char *l, const bsk_status_t *st, char *out, size_t cap)
{
    bool ready = is_ready(st);
    bsk_ack_t a;
    uint32_t v;
    switch (l[0]) {
    case 'v':
        if (!l[1]) { put(out, cap, h.version); return; }  /* PROJECT_VER, dans tout état */
        break;
    case 't':
        if (!l[1]) { line_t(st, out, cap); return; }
        break;
    case 's':                                             /* vitesse ignorée (PROTOCOL.md § 2), dans tout état */
        put(out, cap, "ok");
        return;
    case 'e':
        if (!l[1]) {                                      /* n en FAULT ; y dans les états de séquence */
            put(out, cap, ready ? (moving(st) ? "y" : "n") : in_sequence(st) ? "y" : "n");
            return;
        }
        break;
    case 'f':
        if (!ready) put(out, cap, not_ready(st, true));
        else if (!l[1] && !st->position_valid) put(out, cap, "er link pos");   /* aucun 0x06 valide (PROTOCOL.md § 2) */
        else if (!l[1]) snprintf(out, cap, "%ld", (long)st->focus_position);
        else focus_cmd(l + 1, st, out, cap);
        return;
    case 'r':
        if (l[1]) break;
        if (!ready) put(out, cap, not_ready(st, true));
        else if (!st->position_valid) put(out, cap, "er link limits");   /* aucun 0x06 lu (PROTOCOL.md § 2) */
        else snprintf(out, cap, "%ld-%ld", (long)st->focus_min, (long)st->focus_max);
        return;
    case 'd':
        if (l[1]) break;
        put(out, cap, ready ? "-" : not_ready(st, true));
        return;
    case 'a':                                             /* a, a<f>, a+x, a-x */
        if (!ready) put(out, cap, not_ready(st, true));
        else aperture_line(l + 1, st, out, cap);
        return;
    case 'c':                                             /* `ok` en READY, rien déposé ni émis */
        if (l[1]) break;                                  /* `cm` aussi : une ligne inconnue */
        put(out, cap, ready ? "ok" : not_ready(st, true));   /* le driver Pinefeat relit `r` après (PROTOCOL.md) */
        return;
    case 'o':
        if (l[1]) break;
        if (ready || (st->session_state == SESSION_FAULT && identified(st))) aperture_now(st, out, cap);
        else put(out, cap, st->session_state == SESSION_FAULT ? "er nolens" : not_ready(st, false));
        return;
    case 'l':
        if (l[1]) break;
        put(out, cap, "er nocap focal");
        return;
    case 'i':
        if (l[1]) break;
        if (ready || (st->session_state == SESSION_FAULT && identified(st))) identity(st, out, cap);
        else put(out, cap, st->session_state == SESSION_FAULT ? "er nolens" : not_ready(st, false));
        return;
    case 'n':
    case 'w':
        if (l[1]) break;
        put(out, cap, "er nocap");
        return;
    case 'k':                                             /* le plafond de la LED, dans tout état, rien déposé */
        if (!l[1]) snprintf(out, cap, "%u", (unsigned)bsk_led_ceiling());
        else if (number(l + 1, &v) && bsk_led_ceiling_set(v)) put(out, cap, "ok");
        else put(out, cap, "er range 0 100");    /* 14 caractères ; jamais `ok` dans une erreur (PROTOCOL.md § 1) */
        return;
    case 'g':                                             /* le résultat du dernier déplacement suivi : une lecture, tout état */
        if (l[1]) break;
        put(out, cap, h.move_end);
        return;
    case 'q':                                             /* passe toujours ; pas pendant un homing (HOMING, 0x10 de l'init) */
        if (l[1]) break;
        a = deposit(CMD_FOCUS_STOP, 0);
        if (a.result == ACK_ACCEPTED && h.pend_last.result == ACK_FAILED)
            refusal(out, cap, h.pend_last.reason);  /* le 0x1C pas émis : `er link …` */
        else if (a.result == ACK_ACCEPTED) put(out, cap, "ok");
        else put(out, cap, "er busy home");         /* la session ne refuse q que pendant un homing, E_BUSY */
        return;
    case 'p':
        power(l, st, out, cap);
        return;
    case 'b':
        if (l[1]) break;
        reboot(st, out, cap);
        return;
    case 'm':                                             /* `m<n>`, le `Move` du driver ASCOM, comme `f<n>` */
        if (l[1] < '0' || l[1] > '9') break;
        if (!ready) put(out, cap, not_ready(st, true));
        else focus_cmd(l + 1, st, out, cap);
        return;
    case 'j':                                             /* la marque */
        mark_line(l, st, out, cap);
        return;
    default:
        break;
    }
    put(out, cap, "er nocap");                            /* une ligne que PROTOCOL.md ne connaît pas (§ 4.5.1) */
}

/* ─────────────────────────── majuscules (§ 4.5.5, § 4.5.6) ─────────────────────────── */

static bool word(const char *w, size_t n, const char *ref)
{
    if (strlen(ref) != n) return false;
    for (size_t i = 0; i < n; i++)
        if (toupper((unsigned char)w[i]) != ref[i]) return false;
    return true;
}

/* `DEBUG` : le diagnostic, hors de `t`, sur une ligne : la cause du dernier redémarrage de la carte, les
 * lignes du journal perdues, le résultat du dernier déplacement fini, les trames refusées par bench_core, puis ce que la
 * carte compte du temps réel (bsk_session_debug), dans l'ordre de bsk_session_debug_t, puis les marges de pile des
 * tâches (bsk_host_stacks), dans l'ordre de bsk_host_stacks_t. */
static void debug(char *out, size_t cap)
{
    bsk_session_debug_t d;
    bsk_host_stacks_t k;
    bsk_session_debug(&d);
    bsk_host_stacks(&k);
    snprintf(out, cap,
             "ok reset=%s dropped=%lu move_rc=%s refused=%lu evq_drop=%lu ev_back=%lu ev_back_us=%lu tx_wait_us=%lu "
             "tx_timeout=%lu pair_late=%lu pair_late_us=%lu "
             "stack_session=%lu stack_phy=%lu stack_usb_rx=%lu stack_store=%lu stack_journal=%lu",
             h.reset, (unsigned long)bsk_journal_dropped(), h.move_rc, (unsigned long)bsk_bench_refused(),
             (unsigned long)d.evq_drop, (unsigned long)d.ev_back, (unsigned long)d.ev_back_us, (unsigned long)d.tx_wait_us,
             (unsigned long)d.tx_timeout, (unsigned long)d.pair_late, (unsigned long)d.pair_late_us,
             (unsigned long)k.session, (unsigned long)k.phy, (unsigned long)k.usb_rx, (unsigned long)k.store,
             (unsigned long)k.journal);
}

/* `CUSTOM READ`, `CUSTOM WRITE <haut><bas>` (PROTOCOL.md § 3.1) : la configuration du commutateur Custom du
 * Samyang AF 135 (CMD_LENS_CUSTOM), chaque position un chiffre, 0 (ouverture), 1 (AF) ou 2 (MF), haut = M1, bas = M2. Le
 * texte d'abord, puis l'état (le tableau du § 4.5.1, comme `j`), puis la session : son refus, ou vrai, la réponse attendue
 * (bsk_host_later). Mots insensibles à la casse, comme ceux de LOG. */
static bool custom(const char *a, char *out, size_t cap)
{
    const char *t[3];
    size_t n[3];
    int k = 0;
    int32_t arg;
    bsk_status_t st;
    bsk_ack_t ack;
    for (; k < 3; k++) {
        while (*a == ' ' || *a == '\t') a++;
        if (!*a) break;
        t[k] = a;
        for (n[k] = 0; a[n[k]] && a[n[k]] != ' ' && a[n[k]] != '\t'; n[k]++) {}
        a += n[k];
    }
    if (k == 1 && word(t[0], n[0], "READ")) {
        arg = -1;
    } else if (k == 2 && word(t[0], n[0], "WRITE") && n[1] == 2 && t[1][0] >= '0' && t[1][0] <= '2' && t[1][1] >= '0' &&
               t[1][1] <= '2') {
        arg = (t[1][0] - '0') << 4 | (t[1][1] - '0');
    } else {
        put(out, cap, "er range CUSTOM READ|WRITE <0-2><0-2>");
        return false;
    }
    bsk_session_status(&st);
    if (!is_ready(&st)) {
        put(out, cap, not_ready(&st, false));
        return false;
    }
    ack = deposit(CMD_LENS_CUSTOM, arg);
    if (ack.result == ACK_ACCEPTED) return true;
    if (ack.reason == E_NOCAP) put(out, cap, "er nocap custom");   /* un autre objectif que le 135 */
    else refusal(out, cap, ack.reason);
    return false;
}

/* `LOG ON|ALL|OFF`, `DEBUG`, `CUSTOM` ; toute autre ligne en majuscules : `er nocap`. Vrai : la réponse attend
 * l'objectif (BSK_HOST_LATER). */
static bool labo(const char *l, char *out, size_t cap)
{
    const char *a, *e;
    size_t n = 0;
    if (!strcmp(l, "DEBUG")) {
        debug(out, cap);
        return false;
    }
    if (!strncmp(l, "CUSTOM", 6) && (!l[6] || l[6] == ' ' || l[6] == '\t')) return custom(l + 6, out, cap);
    if (strncmp(l, "LOG", 3) || (l[3] && l[3] != ' ' && l[3] != '\t')) {
        put(out, cap, "er nocap");
        return false;
    }
    a = l + 3;                                      /* calculé ici : la ligne a au moins ses 3 lettres, LOG */
    while (*a == ' ' || *a == '\t') a++;
    while (a[n] && a[n] != ' ' && a[n] != '\t') n++;
    for (e = a + n; *e == ' ' || *e == '\t'; e++) {}
    if (*e) n = 0;                                  /* un seul argument */
    if (word(a, n, "ON")) bsk_journal_set(true, false);
    else if (word(a, n, "ALL")) bsk_journal_set(true, true);
    else if (word(a, n, "OFF")) bsk_journal_set(false, false);
    else {
        put(out, cap, "er range LOG ON|ALL|OFF");
        return false;
    }
    snprintf(out, cap, "ok log=%d all=%d", bsk_journal_on(), bsk_journal_all());
    return false;
}

/* ─────────────────────────── Moonlite (§ 4.5.4, PROTOCOL.md § 4) ─────────────────────────── */

/* Octets exacts : le driver lit jusqu'au '#'. */
static void hex4(char *out, size_t cap, int32_t v) { snprintf(out, cap, "%04X#", (unsigned)(v & 0xFFFF)); }

/* La position valide (READY, un 0x06 valide) ; sinon, en READY comme hors READY, la dernière position gardée ; 0 sans
 * elle. */
static int32_t moon_position(const bsk_status_t *st)
{
    if (st->position_valid) return st->focus_position;
    return h.pos_have ? h.pos : 0;
}

/* `cmd` sans ':' ni '#'. Rend "" (une réponse, sans fin de ligne) ou NULL (aucune réponse). */
static const char *moonlite(const char *cmd, char *out, size_t cap)
{
    bsk_status_t st;
    bsk_session_status(&st);                                         /* la position gardée : bsk_host_observe, à chaque pas */
    if (!strcmp(cmd, "GP")) { hex4(out, cap, moon_position(&st)); return ""; }
    if (!strcmp(cmd, "GN")) { hex4(out, cap, h.have_target ? (int32_t)h.target : moon_position(&st)); return ""; }
    if (!strcmp(cmd, "GI")) {                                         /* `e` : y -> 01#, n -> 00# */
        letter("e", &st, out, cap);
        put(out, cap, !strcmp(out, "y") ? "01#" : "00#");
        return "";
    }
    if (!strcmp(cmd, "GV")) { put(out, cap, "10"); return ""; }      /* 2 chiffres, sans '#' : c'est ainsi que le driver le lit */
    if (!strcmp(cmd, "GT")) { put(out, cap, "0000#"); return ""; }   /* température : 0 */
    if (!strcmp(cmd, "GC")) { put(out, cap, "00#"); return ""; }     /* coefficient */
    if (!strcmp(cmd, "GD")) { put(out, cap, "02#"); return ""; }     /* vitesse */
    if (!strcmp(cmd, "GH")) { put(out, cap, "00#"); return ""; }     /* pas entier */
    if (!strncmp(cmd, "SN", 2)) {                                    /* :SN3E80# ou :SN 3E80# */
        const char *p = cmd + 2; char *end; unsigned long v;
        while (*p == ' ') p++;
        v = strtoul(p, &end, 16);
        if (end != p && *end == 0 && v <= 0xFFFF) { h.target = (uint32_t)v; h.have_target = true; }
        return NULL;
    }
    if (!strcmp(cmd, "FG")) {                                        /* comme `f<n>` : l'admission de la session, un refus ne bouge rien */
        if (h.have_target) deposit(CMD_FOCUS_GOTO, (int32_t)h.target);
        return NULL;
    }
    if (!strcmp(cmd, "FQ")) {                                        /* comme `q` ; pendant un homing, rien n'est arrêté */
        deposit(CMD_FOCUS_STOP, 0);
        return NULL;
    }
    return NULL;   /* :SP# (sync), :SD#, :SH#, :SF#, :SC#, :PO#, :C#, :+#, :-# : sans réponse, sans effet */
}

/* ─────────────────────────── bsk_host.h ─────────────────────────── */

void bsk_host_init(const char *version, const char *reset)
{
    memset(&h, 0, sizeof h);
    h.version = version;
    h.reset = reset;
    put(h.last_op, sizeof h.last_op, "-:ok");       /* avant toute opération */
    h.move_rc = "-";
    h.move_end = "-";
}

const char *bsk_host_line(const char *line, char *out, size_t cap)
{
    bsk_status_t st;
    while (*line == ' ' || *line == '\t') line++;
    out[0] = '\0';
    if (*line == ':') return moonlite(line + 1, out, cap);   /* `:` -> Moonlite, sans fin de ligne (PROTOCOL.md § 1) */
    if (*line >= 'A' && *line <= 'Z') {
        if (labo(line, out, cap)) return BSK_HOST_LATER;   /* la réponse attend l'objectif */
    } else {
        bsk_session_status(&st);
        letter(line, &st, out, cap);
    }
    return "\n";                                    /* une ligne par commande (PROTOCOL.md § 1) */
}

/* La réponse de `CUSTOM`, une fois l'accusé final de CMD_LENS_CUSTOM lu (bsk_host_observe) : `ok` suivi des 16 octets
 * de données que l'objectif a rendus, en hexadécimal (PROTOCOL.md § 3.1), ou l'échec, comme un refus. */
const char *bsk_host_later(char *out, size_t cap)
{
    uint8_t d[BSK_CUSTOM_DATA];
    if (!h.cu_done) return BSK_HOST_LATER;
    h.cu_done = false;
    if (h.cu_ack.result != ACK_COMPLETED) {
        refusal(out, cap, h.cu_ack.reason);
        return "\n";
    }
    bsk_session_custom_data(d);
    put(out, cap, "ok");
    for (size_t i = 0; i < sizeof d; i++) snprintf(out + 2 + 3 * i, cap - 2 - 3 * i, " %02X", (unsigned)d[i]);
    return "\n";
}
