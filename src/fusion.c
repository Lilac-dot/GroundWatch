/*
 * Sensor fusion. Plain C with no 8051 registers, so tests/ can run it on the Mac.
 * The magnetometer and microphone were dropped, so vehicles are no longer a
 * separate class and digging is judged from the piezos alone.
 *
 *   Event       Piezo                                    PIR
 *   Wind/rain   any                                      no    -> ignore
 *   Person      medium, regular rhythm                   yes   -> alarm
 *   Digging     strong, DIG_HITS+ hits, not regular      yes   -> alarm
 *   Animal      weak or irregular                        yes   -> log only
 *
 * Once tools/train_tree.py has written src/tree.c from labelled recordings,
 * the Makefile builds with -DGW_TREE and the trained tree replaces the three
 * piezo rules. The PIR rule always stays.
 */
#include "fusion.h"
#ifdef GW_TREE
#include "tree.h"
#endif

/* zone = sensor that felt it first; a tie goes to the stronger peak */
uint8_t fusion_zone(const window_t *w)
{
    uint8_t i, best = ZONE_NONE;

    for (i = 0; i < N_PIEZO; i++) {
        if (w->hits[i] == 0) continue;
        if (best == ZONE_NONE ||
            w->first[i] < w->first[best] ||
            (w->first[i] == w->first[best] && w->peak[i] > w->peak[best]))
            best = i;
    }
    return best;
}

/* Footsteps come at a steady pace: the longest gap is at most 2x the shortest.
 * Needs at least one gap: two hits in the window, or one hit whose gap to the
 * previous window's last step was carried over (sense.c). A single gap is
 * steady by definition. */
uint8_t fusion_regular(const window_t *w, uint8_t z)
{
    if (w->hits[z] == 0 || w->gap_min[z] == T8_NONE) return 0;
    return w->gap_max[z] <= 2 * (uint16_t)w->gap_min[z];
}

/* LDR hysteresis: night below LDR_DARK, day again above LDR_LIGHT */
uint8_t fusion_night(uint8_t night, uint8_t ldr)
{
    if (!night && ldr < LDR_DARK)  return 1;
    if (night  && ldr > LDR_LIGHT) return 0;
    return night;
}

uint8_t fusion_classify(const window_t *w, uint8_t z)
{
    uint8_t pk;

    if (z == ZONE_NONE) return EV_NONE;
    pk = w->peak[z];

    if (!w->pir) return EV_NOISE;
#ifdef GW_TREE
    /* a tree trained on this board's recordings (tools/train_tree.py) */
    tree_features((const window_t __idata *)w, z);   /* main's window is in __idata */
    return tree_predict();
#else
    /* person first: firm footsteps also have strong peaks and 3+ hits, and
     * checking digging first called a third of real walkers DIGGING */
    if (pk >= MED_PEAK && fusion_regular(w, z))              return EV_PERSON;
    if (pk >= STRONG_PEAK && w->hits[z] >= DIG_HITS)         return EV_DIGGING;
    return EV_ANIMAL;
#endif
}
