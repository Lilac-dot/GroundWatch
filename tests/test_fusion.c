/* Checks every decision rule in src/fusion.c, each on both sides of its
 * threshold: make test. Thresholds come from src/config.h, so the tests follow
 * them if they are retuned. (Hand rules: built without GW_TREE.) */
#include <stdio.h>
#include <string.h>
#include "fusion.h"

static int fails, total;

static window_t quiet(void)
{
    window_t w;
    memset(&w, 0, sizeof w);
    for (int i = 0; i < N_PIEZO; i++) { w.first[i] = FIRST_NONE; w.gap_min[i] = T8_NONE; }
    return w;
}

/* zone i: n hits of the given peak, steps `gap` ms apart, first at t0 ms.
 * With n == 1 there is no gap unless one is carried in (carry()). */
static void hits(window_t *w, int i, int n, int peak, int gap, int t0)
{
    w->peak[i] = peak;
    w->hits[i] = n;
    w->first[i] = t0;
    if (n > 1) { w->gap_min[i] = gap / 8; w->gap_max[i] = gap / 8; }
}
static void gaps(window_t *w, int i, int min_ms, int max_ms) { w->gap_min[i] = min_ms / 8; w->gap_max[i] = max_ms / 8; }

static void check(const char *rule, const char *name, int ok);

/* buzzer state per window for an event sequence: '#' sounding, '_' silent */
static void seq(const int *ev, int n, char *out)
{
    alarm_reset();
    for (int i = 0; i < n; i++) out[i] = alarm_update((uint8_t)ev[i]) ? '#' : '_';
    out[n] = 0;
}

static void check(const char *rule, const char *name, int ok)
{
    total++;
    if (!ok) fails++;
    printf("%s %-9s %s\n", ok ? "ok  " : "FAIL", rule, name);
}

static void expect(const char *rule, const char *name, const window_t *w, int zone, int ev)
{
    int z = fusion_zone(w), e = fusion_classify(w, z);
    char buf[160];
    if (z != zone || e != ev) {
        snprintf(buf, sizeof buf, "%s  (got zone %d event %d, want zone %d event %d)", name, z, e, zone, ev);
        check(rule, buf, 0);
    } else {
        check(rule, name, 1);
    }
}

int main(void)
{
    window_t w;
    const int S = 500;                       /* a normal step gap, ms */

    /* ---------------- zone ---------------- */
    w = quiet();
    expect("zone", "no hits anywhere -> no zone, NONE", &w, ZONE_NONE, EV_NONE);
    w = quiet(); hits(&w, 0, 3, 60, S, 300); hits(&w, 1, 3, 60, S, 200); w.pir = 1;
    expect("zone", "earliest first hit wins (C at 200 before L at 300)", &w, 1, EV_PERSON);
    w = quiet(); hits(&w, 0, 2, 70, S, 200); hits(&w, 2, 2, 50, S, 200); w.pir = 1;
    expect("zone", "same first hit -> stronger peak wins (L)", &w, 0, EV_PERSON);
    w = quiet(); hits(&w, 1, 2, 60, S, 200); hits(&w, 2, 2, 60, S, 200); w.pir = 1;
    expect("zone", "same time and peak -> lower index (C before R)", &w, 1, EV_PERSON);
    w = quiet(); hits(&w, 2, 2, 60, S, 900); w.first[0] = 10; w.pir = 1;   /* L has a time but no hits */
    expect("zone", "a zone with no hits is ignored", &w, 2, EV_PERSON);

    /* ---------------- PIR gate ---------------- */
    w = quiet(); hits(&w, 1, 4, 60, S, 0);
    expect("pir", "steady person-like steps, no PIR -> NOISE", &w, 1, EV_NOISE);
    w = quiet(); hits(&w, 1, 6, 150, 0, 0); gaps(&w, 1, 150, 700);
    expect("pir", "digging-like strikes, no PIR -> NOISE", &w, 1, EV_NOISE);

    /* ---------------- PERSON: peak >= MED_PEAK and steady ---------------- */
    w = quiet(); hits(&w, 1, 3, MED_PEAK, S, 0); w.pir = 1;
    expect("person", "peak == MED_PEAK, steady -> PERSON", &w, 1, EV_PERSON);
    w = quiet(); hits(&w, 1, 3, MED_PEAK - 1, S, 0); w.pir = 1;
    expect("person", "peak == MED_PEAK - 1, steady -> ANIMAL", &w, 1, EV_ANIMAL);
    w = quiet(); hits(&w, 1, 3, 60, 0, 0); gaps(&w, 1, 400, 800); w.pir = 1;
    expect("person", "longest gap exactly 2x shortest -> steady -> PERSON", &w, 1, EV_PERSON);
    w = quiet(); hits(&w, 1, 3, 60, 0, 0); gaps(&w, 1, 400, 808); w.pir = 1;
    expect("person", "longest gap just over 2x -> unsteady -> ANIMAL", &w, 1, EV_ANIMAL);
    w = quiet(); hits(&w, 1, 2, 60, S, 0); w.pir = 1;
    expect("person", "two steps (one gap) -> PERSON", &w, 1, EV_PERSON);
    w = quiet(); hits(&w, 1, 1, 60, 0, 0); w.pir = 1;
    expect("person", "one step, nothing carried -> ANIMAL", &w, 1, EV_ANIMAL);
    w = quiet(); hits(&w, 1, 1, 60, 0, 0); gaps(&w, 1, S, S); w.pir = 1;
    expect("person", "one step + gap carried from last window -> PERSON", &w, 1, EV_PERSON);
    w = quiet(); hits(&w, 1, 2, 60, 0, 0); gaps(&w, 1, 300, 1400); w.pir = 1;
    expect("person", "two steps, carried gap very different -> ANIMAL", &w, 1, EV_ANIMAL);

    w = quiet(); hits(&w, 1, 3, MED_PEAK_NIGHT, S, 0); w.pir = 1; w.night = 1;
    expect("person", "night: peak == MED_PEAK_NIGHT, steady -> PERSON", &w, 1, EV_PERSON);
    w = quiet(); hits(&w, 1, 3, MED_PEAK_NIGHT - 1, S, 0); w.pir = 1; w.night = 1;
    expect("person", "night: peak == MED_PEAK_NIGHT - 1 -> ANIMAL", &w, 1, EV_ANIMAL);
    w = quiet(); hits(&w, 1, 3, MED_PEAK_NIGHT, S, 0); w.pir = 1;
    expect("person", "same peak by day -> ANIMAL (day needs MED_PEAK)", &w, 1, EV_ANIMAL);

    /* ---------------- DIGGING: peak >= STRONG_PEAK, >= DIG_HITS hits, not steady ---------------- */
    w = quiet(); hits(&w, 1, DIG_HITS, STRONG_PEAK, 0, 0); gaps(&w, 1, 150, 700); w.pir = 1;
    expect("digging", "STRONG_PEAK, DIG_HITS hits, unsteady -> DIGGING", &w, 1, EV_DIGGING);
    w = quiet(); hits(&w, 1, DIG_HITS, STRONG_PEAK - 1, 0, 0); gaps(&w, 1, 150, 700); w.pir = 1;
    expect("digging", "peak just under STRONG_PEAK -> ANIMAL", &w, 1, EV_ANIMAL);
    w = quiet(); hits(&w, 1, DIG_HITS - 1, 150, 0, 0); gaps(&w, 1, 150, 700); w.pir = 1;
    expect("digging", "one hit fewer than DIG_HITS -> ANIMAL", &w, 1, EV_ANIMAL);
    w = quiet(); hits(&w, 1, 4, 150, S, 0); w.pir = 1;
    expect("digging", "strong but steady (firm footsteps) -> PERSON first", &w, 1, EV_PERSON);

    /* ---------------- ANIMAL: everything else with PIR ---------------- */
    w = quiet(); hits(&w, 0, 5, 20, 300, 10); w.pir = 1;
    expect("animal", "weak steady taps -> ANIMAL", &w, 0, EV_ANIMAL);
    w = quiet(); hits(&w, 2, 1, 150, 0, 0); w.pir = 1;
    expect("animal", "one strong isolated hit -> ANIMAL (logged, no alarm)", &w, 2, EV_ANIMAL);

    /* ---------------- rhythm helper ---------------- */
    w = quiet();
    check("rhythm", "no hits -> not steady", fusion_regular(&w, 0) == 0);
    w = quiet(); hits(&w, 0, 3, 60, 0, 0); gaps(&w, 0, 2032, 2032);
    check("rhythm", "largest gap value (254 units) doesn't overflow", fusion_regular(&w, 0) == 1);

    /* ---------------- day / night hysteresis ---------------- */
    check("night", "day, LDR == LDR_DARK -> stays day", fusion_night(0, LDR_DARK) == 0);
    check("night", "day, LDR == LDR_DARK - 1 -> night", fusion_night(0, LDR_DARK - 1) == 1);
    check("night", "night, LDR between the two -> stays night", fusion_night(1, (LDR_DARK + LDR_LIGHT) / 2) == 1);
    check("night", "night, LDR == LDR_LIGHT -> stays night", fusion_night(1, LDR_LIGHT) == 1);
    check("night", "night, LDR == LDR_LIGHT + 1 -> day", fusion_night(1, LDR_LIGHT + 1) == 0);
    check("night", "day, LDR == LDR_LIGHT + 1 -> stays day", fusion_night(0, LDR_LIGHT + 1) == 0);

    /* ---------------- buzzer policy: confirm, hold, rest ---------------- */
    {
        /* feed a sequence of window events, return the buzzer state per window */
        const int P = EV_PERSON, D = EV_DIGGING, A = EV_ANIMAL, N = EV_NOISE, _ = EV_NONE;
        char out[64], buf[160];
        (void)buf;

        #define EXPECT(name, want, ...) do { int ev[] = { __VA_ARGS__ }; seq(ev, sizeof ev / sizeof ev[0], out); \
            snprintf(buf, sizeof buf, "%s  [%s]%s", name, out, strcmp(out, want) ? "  want [" want "]" : ""); \
            check("buzzer", buf, !strcmp(out, want)); } while (0)

        EXPECT("one detection alone: shown, no buzzer",            "_____",     P, _, _, _, _);
        EXPECT("two in a row: buzzer from the second",             "_####",     P, P, _, _, _);
        EXPECT("2 of the last 3 also confirms",                    "__###",     P, _, D, _, _);
        EXPECT("detections 3 windows apart don't confirm",         "_____",     P, _, _, P, _);
        EXPECT("animal and noise never count",                     "_____",     A, A, N, N, A);
        EXPECT("sounds 5 windows, then 15 silent, then again",
               "_#####_______________#####_______________",
               P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P,P);
        EXPECT("after the rest, a fresh intruder needs confirming again",
               "_#####________________#####",
               P,P,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,P,P,_,_,_,_);
    }

    printf(fails ? "\n%d of %d rule checks failed\n" : "\nall %d rule checks passed\n", fails ? fails : total, total);
    return fails != 0;
}
