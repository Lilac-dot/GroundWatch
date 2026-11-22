#include "host_8051.h"
#include "config.h"
#include "sense.h"
#include "sense_sim.h"

static uint8_t level[4];

void sim_init(uint8_t l, uint8_t c, uint8_t r, uint8_t ldr)
{
    /* sense_init reads each channel once with adc_read(); hold P1 at the
     * resting level (all four are close enough for the baselines to settle) */
    level[0] = l; level[1] = c; level[2] = r; level[3] = ldr;
    P1 = c;
    sense_init();
}

void sim_ms(uint8_t l, uint8_t c, uint8_t r, uint8_t ldr)
{
    /* slot order: the ISR collects L, C, R, LDR in turn */
    P1 = l;   timer0_isr();
    P1 = c;   timer0_isr();
    P1 = r;   timer0_isr();
    P1 = ldr; timer0_isr();
}

void sim_window(window_t *w) { sense_take_window(w); }
