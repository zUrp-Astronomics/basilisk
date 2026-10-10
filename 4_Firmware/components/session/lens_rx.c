/* SOURCE: composant session — le décodage des messages reçus de l'objectif (voir lens_rx.h)
 * AUTHOR: engineer
 * DATE: 2026-10-04
 * STATUS: actif — C pur, sans ESP-IDF : compilé aussi par les tests hôte
 *
 * Les octets des messages de l'objectif, lus aux longueurs minimales, vers l'état publié (lens_rx_t). La réponse au 0x08
 * est lue telle qu'elle arrive, type compris ; l'init ne la demande qu'une fois par session. Le 0x07, comme le 0x3F, n'est
 * lu que comme la réponse de l'init (lens_rx_id) : l'identité qu'il porte décide de la déclaration à bench_core. */
#include "lens_rx.h"

#include <string.h>

#include "bsk_contract.h"

#define LEN06          40u    /* 0x06, type compris ; les accusés viennent derrière */
#define MIN05          29u    /* 0x05 lu : type et offsets 0 à 27, la focale comprise */
#define MIN06          12u    /* 0x06 lu : type et offsets 0 à 10, les bornes comprises */
#define MIN07          4u     /* réponse au 0x07 lue : type et offsets 0-2, l'identité */
#define MIN08          6u     /* réponse au 0x08 lue : type et offsets 0-3, la plage, et un octet */
#define MIN10          2u     /* réponse au 0x10 lue : type et son octet de résultat */
/* Décision de l'humain : chaque borne publiée est resserrée de 5 pas à la lecture. Au banc, un goto est resté bloqué
 * sur le Sony juste après une arrivée en butée ; avec la marge, on n'atteint jamais la butée. Tout ce qui lit les bornes
 * (`r`, `t`, le refus E_LIMIT, la marque) lit les bornes resserrées. */
#define LIMIT_MARGIN   5

static lens_rx_t pub;
static bool      looping;             /* la boucle de la session est lancée (lens_rx_loop) */

const lens_rx_t *lens_rx(void) { return &pub; }

void lens_rx_forget(void)
{
    memset(&pub, 0, sizeof pub);
    looping = false;
}

void lens_rx_loop(void) { looping = true; }

/* Un code d'ouverture du domaine que la carte convertit (bsk_contract.h). */
static bool ap_known(uint16_t code) { return code >= BSK_AP_CODE_MIN && code <= BSK_AP_CODE_MAX; }

/* L'offset k d'un message est msg[k + 1] : msg[0] est son type. */
uint8_t lens_rx_frame(const bsk_frame_t *f)
{
    uint8_t type = f->msg[0];
    pub.ack1d = pub.ack1c = false;
    /* avant la boucle de la session, ou trop court pour être lu : rien, et pas reçu (lens_rx.h) */
    if ((type == 0x05 || type == 0x06) && (!looping || f->len < (type == 0x05 ? MIN05 : MIN06))) return 0;
    if (type == 0x05) {
        pub.have05 = true;
        pub.focal_nom = (uint16_t)(f->msg[27] | f->msg[28] << 8);
        pub.move = f->len > 61 ? f->msg[61] : 0;
        pub.ap = (uint16_t)(f->msg[1] | f->msg[2] << 8);
        pub.ap_ring = f->msg[20] & 0x01 ? (uint16_t)(f->msg[18] | f->msg[19] << 8) : 0;
        if (f->len > 63) {
            pub.have62 = true;
            pub.o62 = f->msg[63];
        }
        if (f->len > 65) {
            pub.have64 = true;
            pub.o64 = f->msg[65];
        }
        if (f->len > 67) {
            pub.have66 = true;
            pub.o66 = f->msg[67];
        }
    } else if (type == 0x06) {
        pub.have06 = true;
        pub.pos = (uint16_t)(f->msg[3] | f->msg[4] << 8);
        /* les bornes, dans la même trame que la position (tracé sur le Sony FE 24-105 G et le 135 :
         * 7_Docs/E-Mount/protocol.md § 7.6, samyang.md § 4.8) */
        pub.fmin = (int32_t)(f->msg[8] | f->msg[9] << 8) + LIMIT_MARGIN;
        pub.fmax = (int32_t)(f->msg[10] | f->msg[11] << 8) - LIMIT_MARGIN;
        pub.rx06++;
        /* les accusés suivent le 0x06 dans sa trame, deux octets chacun (automate du 135, § 1.7.4) */
        for (size_t i = LEN06; i + 1 < f->len; i += 2) {
            pub.ack1d = pub.ack1d || f->msg[i] == 0x1D;
            pub.ack1c = pub.ack1c || f->msg[i] == 0x1C;   /* 1C 00 du 135 à l'arrêt, 1C 01 du Tamron dans le flux */
        }
    } else if (type == 0x08 && f->len >= 3) {
        pub.id[4] = f->msg[1];
        if (f->len >= MIN08) {
            pub.have08 = true;
            pub.ap_min = (uint16_t)(f->msg[1] | f->msg[2] << 8);
            pub.ap_max = (uint16_t)(f->msg[3] | f->msg[4] << 8);
            if (!ap_known(pub.ap_min) || !ap_known(pub.ap_max)) {   /* hors du domaine : une réponse, sa plage inconnue */
                pub.have08 = false;
                pub.ap_min = pub.ap_max = 0;
            }
        }
    }
    return type;
}

void lens_rx_stale(uint8_t type)
{
    if (type == 0x06) {
        pub.have06 = false;
        pub.pos = 0;
        pub.fmin = pub.fmax = 0;
        return;
    }
    /* lens_rx.h : ce qui se lit sans le prochain 0x05 ; le reste n'est lu que derrière son drapeau ou sur un 0x05 lu */
    pub.have05 = pub.have62 = pub.have64 = false;
    pub.move = 0;
    pub.o62 = 0;
    pub.ap = 0;
}

bool lens_rx_full(const bsk_frame_t *r)
{
    uint8_t t = r->msg[0];
    return r->len >= (t == 0x07 ? MIN07 : t == 0x08 ? MIN08 : t == 0x10 ? MIN10 : 1u);
}

void lens_rx_id(const bsk_frame_t *r)
{
    pub.have_id = true;
    pub.id[0] = r->msg[1];
    pub.id[1] = r->msg[2];
    pub.id[2] = r->len >= 12 ? r->msg[10] : r->msg[3];
    pub.id[3] = r->len >= 12 ? r->msg[11] : 0;
    if (r->len >= 12) {
        pub.have07 = true;
        pub.lens_type2 = (uint16_t)(r->msg[10] | r->msg[11] << 8);
    }
}

/* Le nom 0x3F : offset 2, 64 octets au plus, arrêt au premier zéro, espaces de fin retirés, rejeté en entier sur un
 * octet hors 0x20..0x7E ou un `"` (le nom est publié entre guillemets). */
#define NAME_OFF 2
#define NAME_MAX 64
static bool name_3f(const uint8_t *msg, size_t len, char *out, size_t out_size)
{
    size_t i, k;
    out[0] = '\0';
    for (i = NAME_OFF; i < len && i < NAME_OFF + NAME_MAX && msg[i]; i++)
        if (msg[i] < 0x20 || msg[i] > 0x7E || msg[i] == '"') return false;
    k = i > NAME_OFF ? i - NAME_OFF : 0;
    while (k && msg[NAME_OFF + k - 1] == ' ') k--;
    if (k >= out_size) return false;
    memcpy(out, msg + NAME_OFF, k);
    out[k] = '\0';
    return k > 0;
}

bool lens_rx_name(const bsk_frame_t *r) { return name_3f(r->msg, r->len, pub.name, sizeof pub.name); }

bool lens_rx_home_failed(const bsk_frame_t *r) { return r->msg[1] == 0x01; }   /* E3 */
