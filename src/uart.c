/* Polled UART TX to the PC. No RX needed. */
#include "config.h"
#include "uart.h"

void uart_init(void)
{
    TMOD = (TMOD & 0x0F) | 0x20;         /* Timer1 mode 2 (8-bit auto-reload) */
    TH1  = 0xFD;                         /* 9600 baud @ 11.0592 MHz */
    TL1  = 0xFD;
    SCON = 0x50;                         /* mode 1 */
    TR1  = 1;
    TI   = 0;
}

void uart_putc(char c)
{
    SBUF = c;
    while (!TI) ;
    TI = 0;
}

void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

void uart_putu(uint16_t v)
{
    char    buf[5];
    uint8_t n = 0;
    do {
        buf[n++] = '0' + (v % 10);
        v /= 10;
    } while (v);
    while (n) uart_putc(buf[--n]);
}
