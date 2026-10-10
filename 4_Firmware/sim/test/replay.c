/* SOURCE: traces/sy135-2026-09-20-*.txt ; spec de l'atelier § 11 et § 13 — le faux 135 rejoué contre les traces
 * AUTHOR: engineer
 * DATE: 2026-09-26
 * STATUS: actif — joué par 4_Firmware/run.sh (job `firmware` de la CI)
 * Le faux 135 rejoué contre les traces du 135 de l'humain, requête par requête.
 *
 * Ce qu'on compare : chaque PAIRE requête de la carte -> réponse de l'objectif présente dans
 * traces/sy135-2026-09-20-*.txt. La réponse du faux 135 est égale octet pour octet à la réponse
 * tracée (classe, séquence, sous-messages), hors des champs variables déclarés ci-dessous. Attendu :
 * 100 %. Une requête sans réponse tracée est rejouée pour tenir l'état à jour ; on vérifie seulement
 * qu'elle est acceptée sans blocage (l'objectif n'est ni bloqué ni hors modèle en fin de session).
 *
 * Appariement. Une réponse tracée de type T répond à la dernière requête tracée non encore appariée
 * de type T ; un 10 00 répond à la dernière requête qui lance un homing (0x10, ou 0x40 'F' 0x32,
 * 7_Docs/E-Mount/samyang.md § 6.3) ; une réponse 0x40 à la dernière requête 0x40 de même commande et sous-commande ;
 * un 0x06 au 0x04 de même séquence (full:71-73). Les notifications 0x40 'W', 'H', 'L' ne répondent à
 * rien : émissions spontanées, comptées, non comparées ('W' vient du commutateur et du bouton, que
 * le modèle n'a pas ; 'H' et 'L' de la fourche, que le modèle émet mais pas aux instants de la
 * bague de l'utilisateur). La réponse du modèle est la première trame de même type (même commande
 * 0x40, même séquence pour un 0x06) émise après la requête.
 *
 * Champ variable, un seul :
 *   V1 — 0x40 'F' FA, données 0-1 : la position de l'objectif. Après un homing, elle dépend du front
 *        réel de la fourche et du dépassement à l'arrêt (samyang.md § 3.1 : la position est recalculée
 *        depuis le front, puis posée), et entre deux requêtes de la bague de l'utilisateur, que le
 *        modèle n'a pas. Les traces lisent 14735, 14610 et 14673 après trois homings semblables
 *        (reboots:281, reboots:523, reboots:558). Le 0x06, lui, est comparé en entier.
 *
 * Horloge. Chaque ligne tx/rx porte l'horloge de la carte, en ms (7_Docs/E-Mount/traces.md § 0, « Clocks ») ; une
 * autre ligne prend celle de la ligne tx/rx la plus proche, décalée de l'écart de leurs horodatages
 * de journal. Le rejeu est calé sur les réponses : quand la réponse du modèle arrive dt plus tôt ou
 * plus tard que la réponse tracée, tout ce que la carte a émis ensuite est décalé de dt (une carte
 * attend sa réponse avant d'enchaîner). Le 0x06 ne recale pas l'horloge : la boucle suit la VD.
 *
 * HYPOTHÈSES DU REJEU (ce que les traces ne contiennent pas et qu'il faut pourtant rejouer) :
 *   R1 — la VD, à 60 Hz, tout du long : le 0x0D de chaque init demande 60 Hz (0D 00, full:24),
 *        et la boucle tracée tombe toutes les 16-17 ms (full:67-181).
 *        Là où la boucle est tracée (LOG ALL), chaque 0x03 tracé marque un front VD : la carte émet
 *        sa paire dans le rappel de sa VD.
 *   R2 — la boucle 0x03/0x04 de la carte, là où le journal ne la montre pas (LOG ON la tait) : la paire
 *        de full:67-68, telle que la carte d'alors l'émettait à chaque VD (0x03 suivi d'un 0x2F, que la
 *        liste blanche n'a pas : le modèle le compte non modélisé). Quand elle tourne, session par
 *        session, est dans la table ci-dessous, avec sa trace. La carte des traces la lance dès le 0x10
 *        pour une init qui aboutit, après l'échec pour une init qui échoue (ligne « boot step=init
 *        done=0 loop=1 »), et 300 ms après un 0x01 sans réponse (gardée si un 0x05 vient,
 *        « muet_mais_parle », arrêtée sinon, « muet »).
 *   R3 — chaque session part d'un objectif resté alimenté (samyang.md § 2.7 : toutes les sessions
 *        tracées commencent par LENS_CS_never_high), liaison établie, au repos à 14623, en mode
 *        service (la carte envoie 'M' à chaque démarrage, full:52-55 ; samyang.md § 6.5), en flux avec
 *        le masque de la carte, sauf où la table dit hors flux. Les sessions sont rejouées chacune
 *        depuis cet état : ce qu'était l'objectif entre deux sessions n'est pas tracé.
 *   R4 — « 0x10 sans réponse en 8 s » (reboots:128-129, 259-260, 500-501) : pas de 0x03/0x04 pendant
 *        l'init, objectif en flux. On vérifie que le modèle cale : aucun 10 00
 *        entre le 0x10 et le lancement de la boucle.
 *   R5 — le « 0x01 muet » (astro:123-134, reboots:537-538) n'est PAS rejouable : samyang.md (§ 2.7)
 *        y voit deux causes passagères (réponse jetée par le verrou, analyseur désynchronisé)
 *        sans en retenir aucune, et le rejeu n'en choisit pas. Le 0x01 est rejoué
 *        comme toute requête sans réponse tracée ; le modèle, lui, y répond.
 *   R6 — reboots:534-827 part hors flux : la session précédente finit sur « no_telemetry_2s »
 *        (reboots:530) et celle-ci trouve un objectif qui n'émet aucun 0x05 pendant 300 ms de
 *        boucle (« muet », reboots:538), ce que fait un objectif hors flux
 *        (samyang.md § 2.4). Son 'F' 0x32 aboutit sans boucle (reboots:547-549) : hors flux, la VD
 *        suffit (samyang.md § 2.4). */
#include <stdio.h>
#include <string.h>

#include "bench.h"
#include "frame.h"
#include "lens_sim_135.h"
#include "phy_sim.h"

#define MS 1000u
#define MAX_LINES 1200
#define WAIT_MAX_US (10000u * MS)   /* attente la plus longue d'une réponse (la carte des traces : 8 s au 0x10) */

/* ─────────────────────────── la table des sessions ─────────────────────────── */

typedef struct { int from, to; } span_t;  /* lignes ; from = 0 : dès le début ; to = 0 : jusqu'à la fin */

typedef struct {
    const char *file;
    int         first, last;
    bool        flow;         /* R3, R6 */
    span_t      loop[2];      /* R2 : la boucle tourne après la ligne from, jusqu'à la ligne to */
    span_t      traced;       /* R1 : fenêtre où la boucle est tracée (LOG ALL) */
    int         stall_tx;     /* R4 : ligne du 0x10 qui doit caler */
    int         stall_until;  /*      ligne où la boucle démarre */
    int         mute01;       /* R5 : ligne du 0x01 muet */
} session_t;

#define FULL "sy135-2026-09-20-full.txt"
#define ASTRO "sy135-2026-09-20-astro-normal.txt"
#define REBOOTS "sy135-2026-09-20-reboots-0x10-stall.txt"

static const session_t SESSIONS[] = {
    /* init complète ; boucle dès le 0x10 (full:27 ; « rx05=29 », full:41) ; LOG ALL de
     * full:65 à full:198, où la boucle est tracée */
    {FULL, 1, 395, true, {{27, 65}, {198, 0}}, {65, 198}, 0, 0, 0},
    /* la carte tourne avant le début du journal (LOG ON) : boucle dès le début */
    {ASTRO, 1, 115, true, {{0, 0}}, {0, 0}, 0, 0, 0},
    /* 0x01 muet (astro:123) ; « muet_mais_parle » : la boucle tourne (astro:134) */
    {ASTRO, 116, 214, true, {{134, 0}}, {0, 0}, 0, 0, 123},
    /* init complète ; boucle dès le 0x10 (astro:241 ; « rx05=29 », astro:257) */
    {ASTRO, 215, 425, true, {{241, 0}}, {0, 0}, 0, 0, 0},
    /* 0x10 sans réponse en 8 s : boucle après l'échec (reboots:132) */
    {REBOOTS, 102, 232, true, {{132, 0}}, {0, 0}, 128, 132, 0},
    {REBOOTS, 233, 307, true, {{263, 0}}, {0, 0}, 259, 263, 0},
    /* init complète ; boucle dès le 0x10 (reboots:332) */
    {REBOOTS, 308, 473, true, {{332, 0}}, {0, 0}, 0, 0, 0},
    /* la réponse au 'F' FA de reboots:529 arrive en reboots:532, après « no_telemetry_2s » */
    {REBOOTS, 474, 533, true, {{504, 0}}, {0, 0}, 500, 504, 0},
    /* 0x01 muet (reboots:537), « muet » : boucle 300 ms puis arrêtée (reboots:538) ; hors flux (R6) */
    {REBOOTS, 534, 827, false, {{537, 538}}, {0, 0}, 0, 0, 537},
    /* init complète ; boucle dès le 0x10 (reboots:854) */
    {REBOOTS, 828, 951, true, {{854, 0}}, {0, 0}, 0, 0, 0},
};
/* reboots:1-101 : aucune trame (la carte est en LOG ON, rien n'est émis ni reçu) ; non rejoué. */

/* ─────────────────────────── lecture des traces ─────────────────────────── */

enum { LN_OTHER, LN_TX, LN_RX };

typedef struct {
    int      kind;
    int64_t  wall_ms, board_ms;
    uint8_t  b[512];
    size_t   n;
} tline_t;

static tline_t T[MAX_LINES + 1];
static int n_lines;
static int trace_errors;

static int load(const char *dir, const char *file)
{
    char path[1024], line[8192];
    FILE *fp;
    int frag_line = 0;
    size_t frag_len = 0;
    snprintf(path, sizeof path, "%s/%s", dir, file);
    fp = fopen(path, "r");
    if (!fp) {
        printf("  trace illisible : %s\n", path);
        return -1;
    }
    memset(T, 0, sizeof T);
    n_lines = 0;
    while (fgets(line, sizeof line, fp) && n_lines < MAX_LINES) {
        tline_t *t = &T[++n_lines];
        int h, m, s, ms;
        long long bms;
        char *p;
        line[strcspn(line, "\r\n")] = 0;
        if (sscanf(line, "%d:%d:%d.%d", &h, &m, &s, &ms) == 4) t->wall_ms = ((h * 60LL + m) * 60 + s) * 1000 + ms;
        if ((p = strstr(line, " * tx ")) && sscanf(p + 6, "%lld", &bms) == 1) {
            p = strchr(p + 6, ' ');
            t->kind = LN_TX;
            t->board_ms = bms;
            t->n = p ? fr_hex(p + 1, t->b, sizeof t->b) : 0;
            if (t->n == 0) { printf("  %s:%d : trame tx illisible\n", file, n_lines); trace_errors++; }
        } else if ((p = strstr(line, " * rx ")) && sscanf(p + 6, "%lld", &bms) == 1 && !strstr(p, "untiled")) {
            int off, len, id;
            uint8_t frag[256];
            size_t k;
            p = strchr(p + 6, ' ');
            if (!p) continue;
            if (sscanf(p + 1, "+%d/%d#%d", &off, &len, &id) == 3) {      /* réponse longue, en fragments */
                p = strchr(p + 1, ' ');
                k = p ? fr_hex(p + 1, frag, sizeof frag) : 0;
                if (off == 0) { frag_line = n_lines; frag_len = 0; }
                if (!frag_line || (size_t)off != frag_len || k == 0) { printf("  %s:%d : fragment hors d'ordre\n", file, n_lines); trace_errors++; continue; }
                memcpy(T[frag_line].b + frag_len, frag, k);
                frag_len += k;
                T[frag_line].n = frag_len;
                if ((int)frag_len == len) { T[frag_line].kind = LN_RX; T[frag_line].board_ms = bms; frag_line = 0; }
            } else {
                t->kind = LN_RX;
                t->board_ms = bms;
                t->n = fr_hex(p + 1, t->b, sizeof t->b);
                if (t->n == 0) { printf("  %s:%d : trame rx illisible\n", file, n_lines); trace_errors++; }
            }
        }
    }
    fclose(fp);
    /* horloge de la carte pour les lignes qui n'en portent pas */
    for (int i = 1; i <= n_lines; i++) {
        int j;
        if (T[i].kind != LN_OTHER) continue;
        for (j = i + 1; j <= n_lines && T[j].kind == LN_OTHER; j++) {}
        if (j <= n_lines) T[i].board_ms = T[j].board_ms - (T[j].wall_ms - T[i].wall_ms);
        else {
            for (j = i - 1; j >= 1 && T[j].kind == LN_OTHER; j--) {}
            T[i].board_ms = j >= 1 ? T[j].board_ms + (T[i].wall_ms - T[j].wall_ms) : 0;
        }
    }
    return 0;
}

/* ─────────────────────────── le rejeu d'une session ─────────────────────────── */

typedef struct {
    int         line;
    bsk_frame_t f;
    size_t      log_from;     /* entrée du journal du banc au moment de l'émission */
    bool        answered;
} req_t;

static req_t R[MAX_LINES];
static int n_req;
static bool consumed[BENCH_LOG_CAP];

static struct {
    int pairs, equal, spontaneous, no_reply, unpaired, stalls, stall_ok, failures;
    int by_kind[256];            /* paires égales, par type ; 0x40 par commande (octet 1) */
} tot;

static l135_t L;

static bool is_homing_start(const bsk_frame_t *f)
{
    return f->msg[0] == 0x10 || (f->msg[0] == 0x40 && f->msg[1] == 'F' && f->msg[2] == 0x32);
}

static int find_request(const bsk_frame_t *x)
{
    for (int i = n_req - 1; i >= 0; i--) {
        const bsk_frame_t *q = &R[i].f;
        if (R[i].answered) continue;
        if (x->msg[0] == 0x10 && is_homing_start(q)) return i;
        if (x->msg[0] == 0x06 && q->msg[0] == 0x04 && q->seq == x->seq) return i;
        if (x->msg[0] == 0x40 && q->msg[0] == 0x40 && q->msg[1] == x->msg[1] && q->msg[2] == x->msg[2]) return i;
        if (x->msg[0] != 0x10 && x->msg[0] != 0x06 && x->msg[0] != 0x40 && q->msg[0] == x->msg[0]) return i;
    }
    return -1;
}

static long find_model_reply(const req_t *r, const bsk_frame_t *x)
{
    for (size_t i = r->log_from; i < g_bench.n; i++) {
        const bench_ev_t *b = &BENCH_EV(i);
        const bsk_frame_t *m = &b->frame;
        if (b->kind != BSK_PHY_FRAME || consumed[i % BENCH_LOG_CAP] || m->msg[0] != x->msg[0]) continue;
        if (x->msg[0] == 0x40 && (m->msg[1] != x->msg[1] || m->msg[2] != x->msg[2])) continue;
        if (x->msg[0] == 0x06 && m->seq != x->seq) continue;
        return (long)i;
    }
    return -1;
}

static bool variable(const bsk_frame_t *x, size_t i)   /* V1 */
{
    return x->msg[0] == 0x40 && x->msg[1] == 'F' && x->msg[2] == 0xFA && (i == 3 || i == 4);
}

static bool same(const bsk_frame_t *m, const bsk_frame_t *x)
{
    if (m->cls != x->cls || m->seq != x->seq || m->len != x->len) return false;
    for (size_t i = 0; i < x->len; i++)
        if (m->msg[i] != x->msg[i] && !variable(x, i)) return false;
    return true;
}

static void dump(const char *what, const bsk_frame_t *f)
{
    printf("      %s : cls %02X seq %02X |", what, f->cls, f->seq);
    for (size_t i = 0; i < f->len && i < 48; i++) printf(" %02X", f->msg[i]);
    printf("%s\n", f->len > 48 ? " …" : "");
}

static void replay(const char *dir, const session_t *s)
{
    int64_t delta = 0;
    bool loop_on = false, traced = false, blocked_seen = false;
    int s_pairs = tot.pairs, s_equal = tot.equal, s_noreply = 0;
    l135_params_t p;
    uint8_t m03[64], m04[64];
    size_t n03, n04;
    size_t stall_log = 0;
    printf("%s:%d-%d%s\n", s->file, s->first, s->last, s->flow ? "" : " (hors flux au départ, R6)");
    if (load(dir, s->file) != 0) { tot.failures++; return; }
    l135_params_default(&p);
    l135_init(&L, &p);
    bench_init(lens_sim_135(&L), (uint64_t)(T[s->first].board_ms - 200) * MS);
    phy_sim_mounted();                           /* sans lui, le verrou de XDETECT refuse la VD */
    (void)bsk_phy_lines(true);                   /* relâchées, BODY_CS et l'émission n'arrivent pas */
    n03 = fr_hex("03 C2 2E 00 B2 11 B2 11 1C 00 00 06 00 00 02 00 02 01 00 00 00 2F 5F 5F", m03, sizeof m03); /* full:67 */
    n04 = fr_hex("04 00 00 19 83 00 00 3D 00 00 00 01 00 00", m04, sizeof m04);                           /* full:68 */
    bench_loop_msgs(m03, n03, m04, n04);
    l135_start_powered(&L, phy_sim_now(), s->flow, true);
    bsk_phy_vd(60);
    memset(consumed, 0, sizeof consumed);
    n_req = 0;
    if (s->loop[0].from == 0) {                  /* boucle dès le début de la session */
        loop_on = true;
        bench_loop(true);
    }
    for (int ln = s->first; ln <= s->last; ln++) {
        tline_t *t = &T[ln];
        uint64_t when = (uint64_t)((t->board_ms + delta) * (int64_t)MS);
        if (when < phy_sim_now()) when = phy_sim_now();
        bench_run(when);
        for (int k = 0; k < 2; k++)              /* R2 : la boucle s'arrête à la ligne `to` */
            if (loop_on && s->loop[k].to == ln) { loop_on = false; bench_loop(false); }
        if (s->traced.from == ln) { traced = true; bsk_phy_vd(0); }   /* R1 : la VD suit les 0x03 tracés */
        if (s->traced.to == ln) { traced = false; bsk_phy_vd(60); }
        if (s->stall_until && ln == s->stall_until) {   /* R4 */
            bool stalled = true;
            for (size_t i = stall_log; i < g_bench.n; i++)
                if (BENCH_EV(i).kind == BSK_PHY_FRAME && BENCH_EV(i).frame.msg[0] == 0x10) stalled = false;
            tot.stalls++;
            if (stalled) tot.stall_ok++;
            else tot.failures++;
            printf("  %s R4 : 0x10 de la ligne %d, aucune cadence jusqu'à la ligne %d : %s\n", stalled ? "ok  " : "ECHEC",
                   s->stall_tx, s->stall_until, stalled ? "le modèle cale, pas de 10 00" : "le modèle a répondu 10 00");
        }
        if (t->kind == LN_TX) {
            bsk_frame_t f;
            uint8_t re[BSK_FRAME_MAX];
            if (fr_decode(t->b, t->n, &f) != E_OK) { printf("  %s:%d : trame tx invalide\n", s->file, ln); trace_errors++; continue; }
            if (fr_encode(f.cls, f.seq, f.msg, f.len, re, sizeof re) != t->n || memcmp(re, t->b, t->n) != 0) {
                printf("  %s:%d : la trame ne se réencode pas à l'identique\n", s->file, ln);
                trace_errors++;
            }
            if (traced && f.msg[0] == 0x03) phy_sim_vd_edge();
            if (ln == s->stall_tx) stall_log = g_bench.n;
            R[n_req].line = ln;
            R[n_req].f = f;
            R[n_req].log_from = g_bench.n;
            R[n_req].answered = false;
            n_req++;
            bsk_phy_send(&f);
        } else if (t->kind == LN_RX) {
            bsk_frame_t x;
            int q;
            long mi;
            uint64_t deadline;
            if (fr_decode(t->b, t->n, &x) != E_OK) { printf("  %s:%d : trame rx invalide\n", s->file, ln); trace_errors++; continue; }
            if (x.msg[0] == 0x40 && (x.msg[1] == 'W' || x.msg[1] == 'H' || x.msg[1] == 'L')) { tot.spontaneous++; continue; }
            q = find_request(&x);
            if (q < 0) {
                printf("  ECHEC %s:%d : réponse tracée sans requête à laquelle l'apparier\n", s->file, ln);
                tot.unpaired++;
                tot.failures++;
                continue;
            }
            R[q].answered = true;
            deadline = phy_sim_now() + WAIT_MAX_US;
            while ((mi = find_model_reply(&R[q], &x)) < 0 && phy_sim_now() < deadline) bench_run_for(1 * MS);
            tot.pairs++;
            if (mi < 0) {
                printf("  ECHEC %s:%d -> %d : le modèle ne répond pas\n", s->file, R[q].line, ln);
                dump("tracé ", &x);
                tot.failures++;
                continue;
            }
            consumed[mi % BENCH_LOG_CAP] = true;
            if (same(&BENCH_EV(mi).frame, &x)) {
                tot.equal++;
                tot.by_kind[x.msg[0] == 0x40 ? x.msg[1] : x.msg[0]]++;
            }
            else {
                printf("  ECHEC %s:%d -> %d : réponse différente\n", s->file, R[q].line, ln);
                dump("tracé ", &x);
                dump("modèle", &BENCH_EV(mi).frame);
                tot.failures++;
            }
            if (x.cls != 1) delta = (int64_t)(BENCH_EV(mi).t / MS) - t->board_ms;
        }
        for (int k = 0; k < 2; k++)              /* R2 : la boucle démarre après la ligne `from` */
            if (!loop_on && s->loop[k].from == ln) { loop_on = true; bench_loop(true); }
        if (l135_blocked(&L) || l135_out_of_model(&L)) blocked_seen = true;
    }
    for (int i = 0; i < n_req; i++) {
        uint8_t ty = R[i].f.msg[0];
        if (R[i].answered || ty == 0x03 || ty == 0x04 || ty == 0x1C) continue;
        s_noreply++;
        printf("  sans réponse tracée : ligne %d (%02X", R[i].line, ty);
        if (ty == 0x40) printf(" '%c' %02X", R[i].f.msg[1], R[i].f.msg[2]);
        printf(")%s\n", R[i].line == s->mute01 ? " — 0x01 muet, non rejouable (R5)" : R[i].line == s->stall_tx ? " — calé (R4)" : "");
    }
    tot.no_reply += s_noreply;
    if (blocked_seen) {
        printf("  ECHEC : le modèle s'est bloqué ou est sorti du modèle (%s)\n", L.oom_why ? L.oom_why : "type sans descripteur");
        tot.failures++;
    }
    printf("  paires : %d, égales : %d ; non modélisés reçus : 0x2F x%u ; bloqué : %s\n", tot.pairs - s_pairs,
           tot.equal - s_equal, l135_unmodelled(&L, 0x2F), blocked_seen ? "oui" : "non");
}

int main(int argc, char **argv)
{
    const char *dir;
    if (argc != 2) {
        fprintf(stderr, "usage : replay <répertoire des traces de l'objectif>\n");
        return 2;
    }
    dir = argv[1];
    for (size_t i = 0; i < sizeof SESSIONS / sizeof SESSIONS[0]; i++) replay(dir, &SESSIONS[i]);
    printf("paires égales par type :");
    for (int k = 0; k < 0x40; k++)
        if (tot.by_kind[k]) printf(" %02X x%d", k, tot.by_kind[k]);
    for (int k = 'A'; k <= 'Z'; k++)
        if (tot.by_kind[k]) printf(" 40 '%c' x%d", k, tot.by_kind[k]);
    printf("\n");
    printf("rejeu : %d paires comparées, %d égales (%s) ; %d émissions spontanées non comparées ; "
           "%d requêtes sans réponse tracée ; %d/%d calages R4 ; %d erreur(s) de trace\n",
           tot.pairs, tot.equal, tot.pairs && tot.pairs == tot.equal ? "100 %" : "PAS 100 %", tot.spontaneous,
           tot.no_reply, tot.stall_ok, tot.stalls, trace_errors);
    return tot.failures || trace_errors || tot.pairs == 0 || tot.pairs != tot.equal ? 1 : 0;
}
