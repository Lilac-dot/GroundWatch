#ifndef SENSE_H
#define SENSE_H
#include <stdint.h>
#include "fusion.h"

/* Timer0 at 4 kHz: four slots (piezo L, C, R, LDR), each channel at ~1 kHz,
 * with STA/LTA event detection in the ISR. One "ms" is one round of four slots
 * (0.998 ms). See sense.c. */
void     sense_init(void);
uint16_t millis(void);                  /* wraps every ~65 s; use differences */
void     delay_ms(uint16_t ms);

void     sense_set_threshold(uint8_t thr);       /* minimum STA for an event (day / night) */
uint8_t  sense_raw_mode(void);                   /* 1 = raw streaming, no text log */
void     sense_take_window(window_t *w);         /* copy piezo features, start a new window */
uint8_t  sense_ldr(void);                        /* latest LDR reading */

/* SDCC needs ISR prototypes visible in the file that holds main() */
void timer0_isr(void) __interrupt(1) __using(1);

#endif
