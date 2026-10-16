/* Checks the fusion table (slide 6) on the Mac: make test */
#include <stdio.h>
#include <string.h>
#include "fusion.h"

static int fails;

static window_t quiet(void)
{
    window_t w;
    memset(&w, 0, sizeof w);
    for (int i = 0; i < N_PIEZO; i++) { w.first[i] = T8_NONE; w.gap_min[i] = T8_NONE; }
    return w;
}

/* zone i gets n hits of the given peak, gap ms apart, first at t0 ms */
static void hits(window_t *w, int i, int n, int peak, int gap, int t0)
{
    w->peak[i] = peak;
    w->hits[i] = n;
    w->first[i] = t0 / 8;
    if (n > 1) { w->gap_min[i] = gap / 8; w->gap_max[i] = gap / 8; }
}

static void expect(const char *name, const window_t *w, int zone, int ev)
{
    int z = fusion_zone(w), e = fusion_classify(w, z);
    if (z != zone || e != ev) {
        printf("FAIL %-28s zone %d (want %d)  event %d (want %d)\n", name, z, zone, e, ev);
        fails++;
    } else {
        printf("ok   %s\n", name);
    }
}

int main(void)
{
    window_t w;

    w = quiet();
    expect("nothing", &w, ZONE_NONE, EV_NONE);

    w = quiet(); hits(&w, 1, 4, 60, 500, 100); hits(&w, 0, 2, 30, 500, 140); w.pir = 1;
    expect("person, zone C first", &w, 1, EV_PERSON);

    w = quiet(); hits(&w, 2, 3, 60, 0, 50); w.gap_min[2] = 150 / 8; w.gap_max[2] = 900 / 8; w.pir = 1;
    expect("medium but irregular = animal", &w, 2, EV_ANIMAL);

    w = quiet(); hits(&w, 0, 5, 20, 300, 10); w.pir = 1;
    expect("weak = animal", &w, 0, EV_ANIMAL);

    w = quiet(); hits(&w, 0, 1, 30, 0, 700); hits(&w, 2, 1, 30, 0, 200); w.mag_dev = 900;
    expect("vehicle, no PIR, zone R", &w, 2, EV_VEHICLE);

    w = quiet(); hits(&w, 1, 6, 150, 0, 0); w.gap_min[1] = 150 / 8; w.gap_max[1] = 700 / 8;
    w.snd_bursts = 5; w.pir = 1;
    expect("digging", &w, 1, EV_DIGGING);

    w = quiet(); hits(&w, 1, 6, 150, 0, 0); w.snd_bursts = 5;
    expect("strong hits, no PIR = noise", &w, 1, EV_NOISE);

    w = quiet(); hits(&w, 0, 2, 20, 400, 300); hits(&w, 1, 2, 22, 400, 300);
    expect("tie on first hit -> stronger", &w, 1, EV_NOISE);

    printf(fails ? "\n%d test(s) failed\n" : "\nall passed\n", fails);
    return fails != 0;
}
