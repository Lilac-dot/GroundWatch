#ifndef UART_H
#define UART_H
#include <stdint.h>

void uart_init(void);                    /* 9600 8N1 @ 11.0592 MHz, uses Timer1 */
void uart_putc(char c);
void uart_puts(const char *s);
void uart_putu(uint16_t v);              /* decimal */

#endif
