/* SOURCE: traces du 135 de l'humain, traces/sy135-2026-09-20-*.txt (voir sources135.h)
 * AUTHOR: engineer
 * DATE: 2026-09-26
 * STATUS: actif — lu par lens135.c ; relu par sources135_verify() dans test_lens135
 *
 * Les gabarits du faux 135 : octets copiés des lignes citées, jamais tapés ni calculés. */
#include "sources135.h"

#include <stdio.h>
#include <string.h>

#include "frame.h"

#define FULL "sy135-2026-09-20-full.txt"
#define DUMP "sy135-2026-09-20-dump05.txt"

const src_trace_t g_src135[SRC_N] = {
    [SRC_R01] = {"rep_01", FULL, {9}, {"F0 29 00 02 00 01 FF 9F 38 5D A2 60 18 5E 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 D7 03 55"}},
    [SRC_R07] = {"rep_07", FULL, {12}, {"F0 2B 00 02 00 07 01 03 70 01 00 01 05 00 00 08 00 00 00 00 00 60 92 86 5E 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 8D 02 55"}},
    [SRC_R08] = {"rep_08", FULL, {15, 16, 17, 18}, {
        "F0 D2 00 02 00 08 BF 11 00 19 04 00 00 80 00 13 01 00 12 E1 01 03 01 35 00 00 18 00 00 00 19 00 00 00 00 00 00 16 00 00 00 00 00 00 00 00 E0 43 00 E0 FF 84 A4 00 00 08 32 25 40 2F 00 00 00 00",
        "00 00 00 43 25 04 32 00 00 00 00 00 00 00 F3 F3 07 FB FB 00 00 00 04 04 20 46 00 80 01 00 00 00 00 00 00 00 00 01 01 01 09 09 00 00 00 00 00 E6 80 DD 52 00 D0 DD DF F0 00 08 A0 DD E0 00 52 00",
        "10 10 00 00 00 00 00 00 00 00 00 84 64 27 00 06 10 FF 33 00 00 00 00 00 03 00 00 00 00 00 00 00 00 00 00 00 00 0C 0C 00 00 00 00 00 00 00 05 00 01 00 05 08 00 00 07 07 FF 00 00 00 00 00 00 00",
        "00 00 00 00 00 00 00 00 00 13 00 03 01 07 01 E7 1A 55"}},
    [SRC_R09] = {"rep_09", FULL, {23}, {"F0 14 00 02 00 09 00 00 00 00 00 00 00 00 00 00 00 1F 00 55"}},
    [SRC_R0B] = {"rep_0b", FULL, {21}, {"F0 0B 00 02 00 0B 60 00 78 00 55"}},
    [SRC_R0D] = {"rep_0d", FULL, {25}, {"F0 0A 00 02 00 0D 01 1A 00 55"}},
    [SRC_R10] = {"rep_10", FULL, {35}, {"F0 0A 00 02 00 10 00 1C 00 55"}},
    [SRC_R40_V] = {"rep_40_v", FULL, {43}, {"F0 1B 00 02 00 40 56 00 01 05 00 00 00 00 00 00 00 00 00 00 00 00 00 00 B9 00 55"}},
    [SRC_T05] = {"flux_05_m1", DUMP, {7}, {"F0 69 00 01 D7 05 B2 11 B2 11 00 00 00 00 07 2A 00 2A 00 54 01 54 01 00 00 00 00 07 80 FF 46 05 46 05 5D 01 00 00 C1 34 6C 62 43 18 E5 E7 A6 B3 CC ED 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 6D 00 00 46 05 21 E4 9C A4 D8 FC 21 E4 9C A4 D8 FC 00 32 16 55"}},
    [SRC_T06] = {"flux_06", DUMP, {11}, {"F0 30 00 01 7A 06 12 00 1F 39 00 10 10 31 36 12 78 00 00 00 00 19 00 00 00 00 1F 39 00 05 46 00 00 00 00 00 00 00 00 00 00 00 00 00 00 E8 02 55"}},
    [SRC_R0A] = {"rep_0a", FULL, {39}, {"F0 19 00 02 00 0A FF 7F 00 00 00 00 00 00 3F 00 00 00 00 00 00 00 E2 01 55"}},
    [SRC_R40_M] = {"rep_40_m", FULL, {55}, {"F0 1B 00 02 00 40 4D 00 26 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 D0 00 55"}},
    [SRC_R40_FA] = {"rep_40_fa", FULL, {187}, {"F0 1B 00 02 00 40 46 FA 1F 39 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F5 01 55"}},
    [SRC_R40_FB] = {"rep_40_fb", FULL, {393}, {"F0 1B 00 02 00 40 46 FB 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 9E 01 55"}},
};

size_t sources135_frame(src_id_t id, uint8_t *out, size_t cap)
{
    size_t n = 0;
    for (int f = 0; f < 4 && g_src135[id].hex[f]; f++) {
        size_t k = fr_hex(g_src135[id].hex[f], out + n, cap - n);
        if (k == 0) return 0;
        n += k;
    }
    return n;
}

size_t sources135_msg(src_id_t id, uint8_t *out, size_t cap)
{
    uint8_t b[BSK_FRAME_MAX];
    bsk_frame_t f;
    size_t n = sources135_frame(id, b, sizeof b);
    if (n == 0 || fr_decode(b, n, &f) != E_OK || f.len > cap) return 0;
    memcpy(out, f.msg, f.len);
    return f.len;
}

static int read_line(const char *path, int n, char *out, size_t cap)
{
    FILE *fp = fopen(path, "r");
    int i = 0;
    if (!fp) return 0;
    while (fgets(out, (int)cap, fp)) {
        if (++i == n) {
            out[strcspn(out, "\r\n")] = 0;
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

int sources135_verify(const char *dir)
{
    int gaps = 0;
    for (int id = 0; id < SRC_N; id++) {
        const src_trace_t *g = &g_src135[id];
        uint8_t b[BSK_FRAME_MAX];
        bsk_frame_t f;
        size_t n;
        for (int k = 0; k < 4 && g->hex[k]; k++) {
            char path[1024], line[4096];
            const char *hit;
            snprintf(path, sizeof path, "%s/%s", dir, g->file);
            if (!read_line(path, g->line[k], line, sizeof line)) {
                printf("  ECART %s : %s:%d illisible\n", g->name, g->file, g->line[k]);
                gaps++;
                continue;
            }
            /* mot pour mot, commençant et finissant sur une frontière d'octet */
            hit = strstr(line, g->hex[k]);
            if (!hit || (hit > line && hit[-1] != ' ') ||
                (hit[strlen(g->hex[k])] != 0 && hit[strlen(g->hex[k])] != ' ')) {
                printf("  ECART %s : fragment absent de %s:%d\n", g->name, g->file, g->line[k]);
                gaps++;
            }
        }
        n = sources135_frame((src_id_t)id, b, sizeof b);
        if (n == 0 || fr_decode(b, n, &f) != E_OK) {
            printf("  ECART %s : la trame reconstituée n'est pas valide\n", g->name);
            gaps++;
        }
    }
    return gaps;
}
