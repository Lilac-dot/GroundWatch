#ifndef SENSE_H
#define SENSE_H
#include <stdint.h>
#include "fusion.h"

/* Timer0 1 ms tick. The ISR also samples the piezos and the mic every tick and
 * the LDR every 10 ms, so a slow I2C or UART job in main() can't make it miss samples. */
void     sense_init(void);
uint16_t millis(void);                  /* wraps every ~65 s; use differences */
void     delay_ms(uint16_t ms);

void     sense_set_threshold(uint8_t thr);       /* piezo hit threshold (day / night) */
void     sense_take_window(window_t *w);         /* copy piezo features, start a new window */
uint8_t  sense_sound_peak(void);                 /* max mic deviation since last call */
uint8_t  sense_ldr(void);                        /* latest LDR reading */

/* SDCC needs ISR prototypes visible in the file that holds main() */
void timer0_isr(void) __interrupt(1);

#endif
