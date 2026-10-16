/*
 * Sensor fusion (slide 6). Plain C with no 8051 registers, so tests/ can
 * run it on the Mac.
 *
 *   Event       Piezo                  PIR   Magnetometer  Sound
 *   Vehicle     any hit                any   large change  -
 *   Wind/rain   any                    no    no change     any      -> ignore
 *   Digging     strong                 yes   no change     bursts
 *   Person      medium, regular rhythm yes   no change     -
 *   Animal      weak or irregular      yes   no change     -        -> log only
 */
#include "fusion.h"

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
 * Two hits give one gap, which is regular by definition. */
static uint8_t regular(const window_t *w, uint8_t z)
{
    if (w->hits[z] < 2) return 0;
    if (w->hits[z] == 2) return 1;
    return w->gap_max[z] <= 2 * w->gap_min[z];
}

uint8_t fusion_classify(const window_t *w, uint8_t z)
{
    uint8_t pk;

    if (z == ZONE_NONE) return EV_NONE;
    pk = w->peak[z];

    if (w->mag_dev >= MAG_THR) return EV_VEHICLE;
    if (!w->pir)               return EV_NOISE;
    if (pk >= STRONG_PEAK && w->snd_bursts >= DIG_BURSTS) return EV_DIGGING;
    if (pk >= MED_PEAK && regular(w, z))                  return EV_PERSON;
    return EV_ANIMAL;
}
