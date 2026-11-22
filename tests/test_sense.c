/* Runs the real sense.c detector on synthetic signals: make test */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "host_8051.h"
#include "config.h"
#include "sense_sim.h"

static int fails;
#define REST 128

/* one footstep: a decaying 40 Hz ring, amplitude a, starting at t0 ms */
static double step(double t, double t0, double a)
{
    double u = t - t0;
    if (u < 0 || u > 120) return 0;
    return a * exp(-u / 25.0) * sin(2 * M_PI * 40 * u / 1000.0);
}

static uint8_t clamp(double v) { return v < 0 ? 0 : v > 254 ? 254 : (uint8_t)lround(v); }
static double noise(double n) { return n * (2.0 * rand() / RAND_MAX - 1.0); }

/* steps: every `period` ms starting at `start`, nearest piezo `near` gets amplitude a,
 * the others a*0.6 delayed by `delay` ms per position away */
typedef struct { int period, start, near; double a, delay, n; } scene_t;

static void run(const scene_t *s, int ms, window_t *last)
{
    window_t w;
    for (int t = 0; t < ms; t++) {
        double v[3];
        for (int i = 0; i < 3; i++) {
            int away = abs(i - s->near);
            double a = s->a * (away ? 0.6 : 1.0), d = away * s->delay, sum = 0;
            if (s->period)
                for (int k = s->start; k <= t; k += s->period) sum += step(t, k + d, a);
            v[i] = REST + sum + noise(s->n);
        }
        sim_ms(clamp(v[0]), clamp(v[1]), clamp(v[2]), 200);
        if ((t + 1) % WINDOW_MS == 0) sim_window(&w);
    }
    *last = w;
}

static void check(const char *name, int ok, const window_t *w)
{
    printf("%s %-44s hits %3u %3u %3u  first %5u %5u %5u  peak %3u %3u %3u  gaps %3u-%3u ms  lta %u %u %u\n",
           ok ? "ok  " : "FAIL", name, w->hits[0], w->hits[1], w->hits[2],
           w->first[0], w->first[1], w->first[2], w->peak[0], w->peak[1], w->peak[2],
           w->gap_min[1] * 8, w->gap_max[1] * 8, w->noise[0], w->noise[1], w->noise[2]);
    if (!ok) fails++;
}

int main(void)
{
    window_t w;
    srand(1);

    sim_init(REST, REST, REST, 200);
    scene_t quiet = {0, 0, 0, 0, 0, 2};
    /* like main(): let the baselines settle for 1.5 s, then drop that window.
     * (On the host, adc_init's write of 0xFF to P1 makes every baseline start
     * at 255, so this also checks recovery from a badly wrong start.) */
    run(&quiet, 1500, &w);
    sim_window(&w);
    run(&quiet, 6000, &w);
    check("quiet, noise +-2: no hits", w.hits[0] + w.hits[1] + w.hits[2] == 0, &w);

    scene_t walk = {500, 100, 1, 60, 5, 2};
    run(&walk, 2000, &w);
    check("walk past C every 500 ms: 4 hits on C", w.hits[1] == 4, &w);
    check("  zone = C (first onset)", fusion_zone(&w) == 1, &w);
    check("  steady rhythm (gaps 400-600 ms)",
          w.gap_min[1] * 8 >= 400 && w.gap_max[1] * 8 <= 600, &w);

    scene_t left = {600, 50, 0, 60, 5, 2};
    run(&left, 2000, &w);
    check("walk past L: zone = L", fusion_zone(&w) == 0, &w);

    scene_t wind = {0, 0, 0, 0, 0, 20};
    run(&wind, 4000, &w);
    run(&wind, 2000, &w);
    check("wind (noise +-20) after 4 s: settles, <=1 hit", w.hits[1] <= 1, &w);

    scene_t walk_wind = {500, 100, 1, 120, 5, 20};
    run(&walk_wind, 2000, &w);
    check("walk in wind (steps 6x noise): >=3 of 4 on C", w.hits[1] >= 3, &w);

    run(&quiet, 8000, &w);
    check("wind stops: back to quiet, no hits", w.hits[0] + w.hits[1] + w.hits[2] == 0, &w);
    run(&walk, 2000, &w);
    check("  and sensitive again: 4 hits on C", w.hits[1] == 4, &w);

    /* detection rate vs step size in wind (noise +-20): 10 windows x 4 steps */
    printf("\ndetection in wind (noise +-20, LTA ~10): step amplitude -> steps found\n");
    for (int a = 40; a <= 160; a += 20) {
        scene_t sw = {500, 100, 1, a, 5, 20};
        int found = 0;
        run(&wind, 4000, &w);
        for (int k = 0; k < 10; k++) { run(&sw, 2000, &w); found += w.hits[1] > 4 ? 4 : w.hits[1]; }
        printf("  %3d (%.1fx noise)  %2d/40\n", a, a / 20.0, found);
    }

    printf(fails ? "\n%d test(s) failed\n" : "\nall passed\n", fails);
    return fails != 0;
}
