/* Lets the firmware's C files compile on the host for testing: the 8051
 * registers become plain variables (defined in tests/host_8051.c) and the
 * SDCC keywords disappear. Used with: cc -include tests/host_8051.h */
#ifndef HOST_8051_H
#define HOST_8051_H
#include <stdint.h>

#define __data
#define __idata
#define __code
#define __bit        uint8_t
#define __interrupt(n)
#define __using(n)

extern volatile uint8_t P1, SBUF, TMOD, TH0, TL0, PCON;
extern volatile uint8_t P0_0, P0_1, P0_2, P0_3, P0_4, P0_5;
extern volatile uint8_t P3_2, P3_3, EA, ET0, TR0, PT0, TI;
#endif
