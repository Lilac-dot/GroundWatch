#ifndef FUSION_H
#define FUSION_H
#include <stdint.h>
#include "config.h"

#define T8_NONE    0xFF            /* "never" for the 8 ms-unit gap fields */
#define FIRST_NONE 0xFFFF          /* no onset in this window */

/* Everything the classifier sees for one 2 s window.
 * Gaps are in 8 ms units so they fit a byte (2 s = 250): RAM is tight. */
typedef struct {
    uint8_t  peak[N_PIEZO];
    uint8_t  hits[N_PIEZO];
    uint16_t active[N_PIEZO];    /* ms above threshold */
    uint16_t first[N_PIEZO];     /* first onset, ms into the window, FIRST_NONE = none */
    uint8_t  gap_min[N_PIEZO];   /* shortest / longest gap between hits, 8 ms units */
    uint8_t  gap_max[N_PIEZO];
    uint8_t  noise[N_PIEZO];     /* background level (LTA), ADC counts */
    uint8_t  pir;
    uint8_t  night;
} window_t;

uint8_t fusion_zone(const window_t *w);                 /* 0..2 or ZONE_NONE */
uint8_t fusion_classify(const window_t *w, uint8_t z);  /* EV_* */
uint8_t fusion_regular(const window_t *w, uint8_t z);   /* steady step rhythm on zone z */
uint8_t fusion_night(uint8_t night, uint8_t ldr);       /* day/night with hysteresis */

/* Buzzer policy: call once per window with that window's event. Returns 1 while
 * the buzzer should sound (confirmation, hold and cooldown: see config.h). */
void    alarm_reset(void);
uint8_t alarm_update(uint8_t ev);

#endif
