#include "host_8051.h"
volatile uint8_t P1, SBUF, TMOD, TH0, TL0, PCON;
volatile uint8_t P0_0, P0_1, P0_2, P0_3, P0_4, P0_5 = 1;
volatile uint8_t P3_2, P3_3 = 1, EA, ET0, TR0, PT0, TI;
