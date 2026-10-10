/* SOURCE: les traces de traces/, lues en hexadécimal
 * AUTHOR: engineer
 * DATE: 2026-09-26
 * STATUS: actif — outil du banc hôte (faux 135, PHY simulée, rejeu)
 * La lecture des traces en hexadécimal ; le codec (fr_sum, fr_encode, fr_decode) est dans
 * components/phy/phy_common.c, commun à la PHY de la carte et au simulateur. */
#ifndef FRAME_H
#define FRAME_H

#include <stddef.h>
#include <stdint.h>

#include "phy_common.h"

/* Octets hexadécimaux séparés par des blancs (« F0 0A 00 ») ; rend leur nombre, 0 si illisible. */
size_t fr_hex(const char *s, uint8_t *out, size_t cap);

#endif
