#ifndef FUSION_H
#define FUSION_H
#include <stdint.h>
#include "config.h"

#define T8_NONE 0xFF               /* "never" for the 8 ms-unit fields */

/* Everything the classifier sees for one 2 s window.
 * Times are in 8 ms units so they fit a byte (2 s = 250): RAM is tight. */
typedef struct {
    uint8_t  peak[N_PIEZO];
    uint8_t  hits[N_PIEZO];
    uint16_t active[N_PIEZO];    /* ms above threshold */
    uint8_t  first[N_PIEZO];     /* first hit, 8 ms units, T8_NONE = never */
    uint8_t  gap_min[N_PIEZO];   /* shortest / longest gap between hits, 8 ms units */
    uint8_t  gap_max[N_PIEZO];
    uint16_t mag_dev;            /* largest |dX|+|dY|+|dZ| seen in the window */
    uint8_t  snd_bursts;
    uint8_t  pir;
    uint8_t  night;
} window_t;

uint8_t fusion_zone(const window_t *w);                 /* 0..2 or ZONE_NONE */
uint8_t fusion_classify(const window_t *w, uint8_t z);  /* EV_* */

#endif
