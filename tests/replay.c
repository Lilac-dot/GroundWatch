/*
 * Replay a recording through the real firmware detector (src/sense.c) and
 * classifier (src/fusion.c) on the host, printing one line per 2 s window in
 * the board's own log format.
 *
 *   make replay
 *   ./build/replay recordings/footstep_20261123_101500.csv [--pir 0|1] [--night]
 *
 * Input: CSV with a header and columns t_ms,L,C,R (8-bit ADC counts, one row
 * per ~1 ms), as written by tools/groundwatch_raw.py, or a .bin file of raw
 * L,C,R byte triplets (used by tools/footprintid_features.py). The PIR isn't
 * recorded, so --pir sets it for the whole file (default 1: someone was there).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_8051.h"
#include "config.h"
#include "sense.h"
#include "fusion.h"
#include "sense_sim.h"

static const char *ev_name[] = { "NONE", "NOISE", "ANIMAL", "PERSON", "DIGGING" };

static void print_window(const window_t *w, int pir, int night)
{
    uint8_t z = fusion_zone(w), ev;
    window_t c = *w;
    c.pir = pir;
    c.night = night;
    ev = fusion_classify(&c, z);
    printf("EV,%s,%c", ev_name[ev], z == ZONE_NONE ? '-' : "LCR"[z]);
    for (int i = 0; i < 3; i++) printf(",%u", w->peak[i]);
    for (int i = 0; i < 3; i++) printf(",%u", w->hits[i]);
    for (int i = 0; i < 3; i++) printf(",%u", w->active[i]);
    for (int i = 0; i < 3; i++) w->first[i] == FIRST_NONE ? printf(",-") : printf(",%u", w->first[i]);
    for (int i = 0; i < 3; i++) w->gap_min[i] == T8_NONE ? printf(",-") : printf(",%u", w->gap_min[i] * 8);
    for (int i = 0; i < 3; i++) w->gap_min[i] == T8_NONE ? printf(",-") : printf(",%u", w->gap_max[i] * 8);
    for (int i = 0; i < 3; i++) printf(",%u", w->noise[i]);
    printf(",%d,%d,0\n", pir, night);
}

int main(int argc, char **argv)
{
    FILE *f;
    char line[128];
    int pir = 1, night = 0, n = 0, settle = 1500;
    unsigned t, l, c, r;
    window_t w;

    if (argc < 2) {
        fprintf(stderr, "usage: %s recording.csv [--pir 0|1] [--night]\n", argv[0]);
        return 2;
    }
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--pir") && i + 1 < argc) pir = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--night")) night = 1;
    }
    size_t len = strlen(argv[1]);
    int bin = len > 4 && !strcmp(argv[1] + len - 4, ".bin");
    if (!(f = fopen(argv[1], bin ? "rb" : "r"))) { perror(argv[1]); return 1; }
    if (!bin && !fgets(line, sizeof line, f)) return 1;   /* header */

    for (;;) {
        if (bin) {
            unsigned char b[3];
            if (fread(b, 1, 3, f) != 3) break;
            l = b[0]; c = b[1]; r = b[2];
        } else {
            if (!fgets(line, sizeof line, f)) break;
            if (sscanf(line, "%u,%u,%u,%u", &t, &l, &c, &r) != 4) continue;
        }
        if (n == 0) {
            sim_init(l, c, r, 200);
            if (night) sense_set_threshold(TRIG_MIN_NIGHT);
        }
        sim_ms(l, c, r, 200);
        n++;
        if (n == settle) sim_window(&w);                  /* like main(): drop the settling window */
        else if (n > settle && (n - settle) % WINDOW_MS == 0) {
            sim_window(&w);
            print_window(&w, pir, night);
        }
    }
    fclose(f);
    fprintf(stderr, "%d samples, %d full windows\n", n, n > settle ? (n - settle) / WINDOW_MS : 0);
    return 0;
}
