/* SOURCE: les traces de traces/, lues en hexadécimal (voir frame.h)
 * AUTHOR: engineer
 * DATE: 2026-09-26
 * STATUS: actif — lié aux programmes de la suite de 4_Firmware/ (sim/test/programmes.sh)
 * La lecture des octets hexadécimaux des traces ; le codec des trames est dans components/phy/phy_common.c. */
#include "frame.h"

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

size_t fr_hex(const char *s, uint8_t *out, size_t cap)
{
    size_t n = 0;
    for (;;) {
        int h, l;
        while (*s == ' ') s++;
        if (!*s) return n;
        h = hexval(s[0]);
        l = h < 0 ? -1 : hexval(s[1]);
        if (h < 0 || l < 0 || n >= cap || (s[2] != ' ' && s[2] != 0)) return 0;
        out[n++] = (uint8_t)(h << 4 | l);
        s += 2;
    }
}
