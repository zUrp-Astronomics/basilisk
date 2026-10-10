/* SOURCE: spec de l'atelier § 4.5.2 — la table de repli des noms de `i` et de `id`
 * AUTHOR: engineer
 * DATE: 2026-10-03
 * STATUS: actif — en-tête privé de la couche HOTE
 *
 * Noms d'objectifs E-mount par code LensType2 (maker note Sony) : le code que l'instantané publie dans
 * lens_id_product (0x07, offsets 9-10). */
#ifndef LENS_NAMES_H
#define LENS_NAMES_H

#include <stddef.h>
#include <stdint.h>

const char *lens_name_lookup(uint16_t code);   /* NULL si inconnu */

#endif
