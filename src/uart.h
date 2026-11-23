#ifndef UART_H
#define UART_H
#include "mcu.h"

void uart_init(uint8_t fast);            /* 8N1 @ 11.0592 MHz on Timer1: 9600, or 57600 if fast */
void uart_putc(char c);
void uart_puts(const char *s);
void uart_putu(uint16_t v);              /* decimal */

#endif
