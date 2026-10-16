/*
 * ADC0808 driver. Data on P1, control on P0 (see config.h).
 * The ADC needs its own clock (10 kHz - 1.28 MHz): divide the 8051's ALE pin
 * (11.0592 MHz / 6 = 1.84 MHz) by 2 with a 74HC74 -> 921.6 kHz.
 *
 * Only single-bit writes are used on P0: a byte write like P0 = (P0 & ..) reads
 * the pins, and if EOC happened to be low it would latch P0.5 low forever.
 */
#include "config.h"
#include "adc0808.h"

void adc_init(void)
{
    ADC_DATA  = 0xFF;   /* P1 as input */
    ADC_EOC   = 1;      /* input */
    ADC_OE    = 0;
    ADC_START = 0;
}

uint8_t adc_read(uint8_t ch)
{
    uint8_t t, v;

    ADC_A = (ch & 1) ? 1 : 0;
    ADC_B = (ch & 2) ? 1 : 0;
    ADC_C = (ch & 4) ? 1 : 0;

    ADC_START = 1;      /* rising edge latches address (ALE) and resets the SAR */
    ADC_START = 0;      /* falling edge starts the conversion */

    /* EOC drops within ~10 us, then rises when done (~70 us). Timeouts keep
     * the 1 ms tick alive if the ADC is unplugged. */
    for (t = 10; ADC_EOC && t; t--) ;
    for (t = 40; !ADC_EOC && t; t--) ;

    ADC_OE = 1;
    v = ADC_DATA;
    ADC_OE = 0;
    return v;
}
