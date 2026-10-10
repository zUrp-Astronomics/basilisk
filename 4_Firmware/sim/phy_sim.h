/* SOURCE: spec de l'atelier § 2 et § 11 — la frontière PHY
 * AUTHOR: engineer
 * DATE: 2026-09-26
 * STATUS: actif — compilé par 4_Firmware/run.sh dans les bancs qui jouent le fil
 * Implémente include/bsk_phy.h au-dessus d'un faux objectif (lens_sim.h : le faux 135 ou le faux standard), sur une
 * horloge virtuelle : le fil entre la carte et le faux objectif, et un récepteur qui rend ce que bsk_phy.h promet.
 *
 * Côté carte :
 *   - une trame émise = BODY_CS levée, les octets, BODY_CS rabaissée, avec 40 µs de garde de chaque côté, comme la
 *     carte ;
 *   - un octet dure byte_us du faux objectif (750 000 bauds, lens_sim_byte_us) ;
 *   - la réception est celle de la carte (rx_t de components/phy/phy_common.c) : chaque octet de l'objectif va au flux à
 *     son instant, une trame remonte dès son dernier octet, les octets écartés remontent en E_FRAMING (avant la trame
 *     qui les suit, ou 20 ms après le dernier octet reçu) ; LENS_CS n'est que publiée.
 *     Seule différence : la carte reçoit les octets par blocs de l'UART, ici un par un. */
#ifndef PHY_SIM_H
#define PHY_SIM_H

#include <stddef.h>
#include <stdint.h>

#include "bsk_phy.h"
#include "lens_sim.h"

void     phy_sim_init(lens_sim_t lens, uint64_t t0);
uint64_t phy_sim_now(void);
uint64_t phy_sim_next(void);                       /* prochain instant où quelque chose se passe */
void     phy_sim_run(uint64_t t);                  /* fait tout avancer jusqu'à t */
void     phy_sim_raw(const uint8_t *b, size_t n);  /* octets tels quels, BODY_CS levée autour (fautes) */
void     phy_sim_raw_cs_low(const uint8_t *b, size_t n); /* octets tels quels, BODY_CS laissée basse (fautes) */
void     phy_sim_vd_edge(void);                    /* un front VD à l'instant courant */

/* Une émission de l'objectif telle quelle, hors du faux 135 : LENS_CS levée à l'instant courant (ou à
 * la fin de la précédente émission injectée), un octet tous les byte_us du faux objectif, LENS_CS retombée
 * lens_cs_tail_us (du faux objectif) après le dernier. Pour une émission que le faux objectif ne fait pas (malformée,
 * trop longue, plus grande que le tampon) : à jouer faux objectif muet, les deux ne s'entrelacent pas. 8192 sorties en attente au plus
 * (octets et deux fronts par émission), le reste est ignoré. */
void     phy_sim_lens_raw(const uint8_t *b, size_t n);

/* La même, émise comme le Sony FE 24-105 G : LENS_CS retombe avec l'octet n − late, et les `late`
 * derniers octets arrivent après, un tous les byte_us. Le Sony rabaisse LENS_CS dès son dernier octet sur le fil, que
 * l'UART de la carte ne rend qu'ensuite (une capture de la carte face au Sony, non publiée, 7_Docs/E-Mount/provenance.md
 * § 3 : 40 octets de 41 reçus à la retombée). 0 < late ≤ n. */
void     phy_sim_lens_raw_late(const uint8_t *b, size_t n, size_t late);

/* E_BUS (spec § 1 R4 et § 12) : les erreurs matérielles que le pilote UART de la carte
 * signale (débordement du FIFO ou du tampon, erreur de trame, de parité, break, voir
 * components/phy/phy.c), injectées ici pour que la couche du dessus les rencontre en simulation.
 *   - phy_sim_bus_error : un E_BUS remonte à l'instant courant, et le tampon de réception est vidé, comme sur la
 *     carte (la suite du flux est reçue comme tout octet) ; il porte les octets du tampon (aucun s'il était
 *     vide) ;
 *   - phy_sim_send_bus_error : le prochain bsk_phy_send n'émet rien et rend E_BUS. */
void     phy_sim_bus_error(void);
void     phy_sim_send_bus_error(void);

/* Une trame perdue sur le fil, ou reçue fausse, choisie par le test, trame par trame (NULL : aucune). Les
 * faux objectifs n'en sont pas changés : ils émettent et reçoivent ce qu'ils émettaient et recevaient.
 *   - to_lens, une trame que la carte émet : PHY_SIM_LOSE, rien n'arrive à l'objectif, et bsk_phy_send rend E_OK (la
 *     carte ne sait pas qu'elle est perdue) ; PHY_SIM_GARBLE est pris comme PHY_SIM_LOSE (la trame fausse qu'un objectif
 *     recevrait n'est pas modélisée : le Tamron y répond par un 0x02, que la carte n'exploite pas) ;
 *   - !to_lens, une trame entière que la réception rend : PHY_SIM_LOSE, elle ne remonte pas (ses octets ne sont jamais
 *     arrivés) ; PHY_SIM_GARBLE, un E_FRAMING remonte à sa place, au même instant, qui porte ses octets, ceux que la
 *     réception de la carte écarte d'une trame dont un octet est faux (sans attendre les 20 ms de silence qu'elle
 *     attendrait peut-être : l'instant de l'erreur est celui de la trame). */
typedef enum { PHY_SIM_PASS, PHY_SIM_LOSE, PHY_SIM_GARBLE } phy_sim_fate_t;
typedef phy_sim_fate_t (*phy_sim_fault_fn)(bool to_lens, const bsk_frame_t *f);
void     phy_sim_fault(phy_sim_fault_fn fn);

/* D2, le contact de présence : son niveau à partir de l'instant courant, true = présent.
 * Il part « absent », verrou posé. Comme la carte (pres_t, components/phy/phy_common.h) : un passage de
 * « présent » à « absent » est l'interruption de la carte. Objectif présent, le premier arme l'échéance de
 * 2 ms (D2_DROP_US), qu'un autre avant elle ne relance pas ; à elle, un instant de phy_sim_next, D2 est relu : absent,
 * rails coupés, TXD, BODY_CS et VD au repos, verrou posé, retombée tenue pour bsk_phy_poll, à cet instant ; présent,
 * rien de coupé, un BSK_PHY_BOUNCE dans la file. Objectif absent (verrou posé), la coupure est dans cet appel même. Une insertion est rapportée tenue 300 ms, et
 * lève le verrou. Il ne touche pas l'alimentation du faux objectif, que l'appelant commande (lens_sim_power).
 * phy_sim_next ne compte pas l'anti-rebond de l'insertion : le banc qui pose D2 réveille la simulation 300 ms après. */
void     phy_sim_d2(bool present);

/* L'objectif monté avant l'instant courant : D2 présent, insertion déjà confirmée, verrou levé, sans
 * événement. Pour un test qui joue le fil sans SESSION (le faux objectif, le rejeu) : sans lui, le verrou refuse la VD et
 * les lignes, qu'un tel test pilote ensuite (bsk_phy_lines(true)) pour que BODY_CS et l'émission arrivent à l'objectif. */
void     phy_sim_mounted(void);

/* Les lignes émises et les rails, tels que la carte les commande, notés pour les tests. Le faux objectif
 * n'est pas alimenté par les rails (lens_sim_power). Les lignes touchent le fil comme sur la carte : bsk_phy_vd(0)
 * arrête la VD ; relâcher les lignes y pose BODY_CS basse, le tirage bas de la carte ; la retombée de D2 fait les deux ;
 * lignes relâchées, bsk_phy_body_cs ne change rien à ce que voit l'objectif (BODY_CS reste basse) et
 * bsk_phy_send ne lui fait rien parvenir, rend E_OK (TXD n'est plus routée à l'UART).
 *   - PHY_SIM_REST : haute impédance, tirée bas ; PHY_SIM_LOW, PHY_SIM_HIGH : pilotée à ce niveau ;
 *   - TXD : haute, pilotée, dès que les lignes le sont (l'UART la tient haute au repos) ;
 *   - BODY_CS : pilotée basse avec les lignes, puis au niveau de bsk_phy_body_cs ; relâchée, une commande de
 *     BODY_CS ne la pilote pas, comme sur la carte (sortie coupée) ;
 *   - VD : haute pendant qu'elle tourne (ses impulsions basses durent 60 µs par période), au repos arrêtée ;
 *   - un rail : allumé ou non, et l'instant de sa dernière mise sous tension. */
typedef enum { PHY_SIM_REST, PHY_SIM_LOW, PHY_SIM_HIGH } phy_sim_line_t;

typedef struct {
    bool           rail[2];     /* indexé par bsk_rail_t */
    uint64_t       rail_t[2];
    phy_sim_line_t txd, body_cs, vd;
} phy_sim_wires_t;

const phy_sim_wires_t *phy_sim_wires(void);

#endif
