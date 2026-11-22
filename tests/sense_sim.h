/* Drive the real sense.c on the host, one ADC sample at a time. */
#ifndef SENSE_SIM_H
#define SENSE_SIM_H
#include <stdint.h>
#include "fusion.h"

void sim_init(uint8_t l, uint8_t c, uint8_t r, uint8_t ldr);   /* resting levels */
void sim_ms(uint8_t l, uint8_t c, uint8_t r, uint8_t ldr);     /* one 1 ms round (4 slots) */
void sim_window(window_t *w);                                  /* end the 2 s window */
#endif
