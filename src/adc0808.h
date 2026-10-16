#ifndef ADC0808_H
#define ADC0808_H
#include <stdint.h>

void    adc_init(void);
uint8_t adc_read(uint8_t ch);   /* 0..7, returns 0..255. ~70 us with a 921 kHz ADC clock */

#endif
