/*
 * GroundWatch wiring self-test (no multimeter needed)
 *
 * Flash build/selftest.hex instead of groundwatch.hex. Every 3 s the 8051
 * checks its own ADC wiring and prints a report on the serial line (read it
 * through the ESP32 bridge) plus a one-line verdict on the LCD:
 *
 *   1. Port 0 pull-ups: each control pin is pulled low, released, and read
 *      back. Port 0 is open-drain: without its 10k pull-up it can't go high.
 *   2. ADC conversions: START is pulsed on every channel and EOC is watched.
 *      EOC falling = START reached the ADC. EOC rising again = the ADC has a
 *      clock and finished.
 *   3. Data bus: IN3, IN5-IN7 are tied to GND and must read ~0; IN0-IN2 sit
 *      at VREF (~128); IN4 is the LDR. 255 everywhere = no data on P1.
 *
 * Flash groundwatch.hex again when done.
 */
#include "config.h"
#include "uart.h"
#include "lcd.h"

/* lcd.c needs delay_ms (normally in sense.c): Timer0 mode 1, 1 ms per loop */
void delay_ms(uint16_t ms)
{
    TMOD = (TMOD & 0xF0) | 0x01;
    while (ms--) {
        TH0 = (uint8_t)((65536u - 922u) >> 8);
        TL0 = (uint8_t)((65536u - 922u) & 0xFF);
        TF0 = 0;
        TR0 = 1;
        while (!TF0) ;
        TR0 = 0;
    }
}

static void tiny_wait(void)                 /* ~20 us */
{
    volatile uint8_t t;
    for (t = 0; t < 8; t++) ;
}

static void p0_set(uint8_t i, uint8_t v)
{
    switch (i) {
    case 0:  ADC_A     = v ? 1 : 0; break;
    case 1:  ADC_B     = v ? 1 : 0; break;
    case 2:  ADC_C     = v ? 1 : 0; break;
    case 3:  ADC_START = v ? 1 : 0; break;
    default: ADC_OE    = v ? 1 : 0; break;
    }
}

static uint8_t p0_get(uint8_t i)
{
    switch (i) {
    case 0:  return ADC_A;
    case 1:  return ADC_B;
    case 2:  return ADC_C;
    case 3:  return ADC_START;
    default: return ADC_OE;
    }
}

static void idle_pins(void)
{
    ADC_A = 0; ADC_B = 0; ADC_C = 0;
    ADC_START = 0; ADC_OE = 0;
    ADC_EOC = 1;                             /* input */
    ADC_DATA = 0xFF;                         /* P1 as input */
}

#define F_FELL  1                            /* EOC went low after START */
#define F_DONE  2                            /* EOC came back high: conversion done */

static uint8_t adc_try(uint8_t ch, uint8_t *val)
{
    uint16_t n;
    uint8_t f = 0;

    ADC_A = (ch & 1) ? 1 : 0;
    ADC_B = (ch & 2) ? 1 : 0;
    ADC_C = (ch & 4) ? 1 : 0;
    ADC_START = 1;
    ADC_START = 0;

    for (n = 0; n < 2000; n++) if (!ADC_EOC) { f |= F_FELL; break; }
    if (f & F_FELL)
        for (n = 0; n < 20000; n++) if (ADC_EOC) { f |= F_DONE; break; }

    ADC_OE = 1;
    tiny_wait();
    *val = ADC_DATA;
    ADC_OE = 0;
    return f;
}

static void say(const char *s) { uart_puts(s); }

static char lcd_buf[17];

static void lcd_show(uint8_t row, const char *a, uint8_t num, uint8_t show_num)
{
    uint8_t i = 0;
    while (*a && i < 16) lcd_buf[i++] = *a++;
    if (show_num && i < 14) {
        if (num >= 100) lcd_buf[i++] = '0' + num / 100;
        if (num >= 10)  lcd_buf[i++] = '0' + (num / 10) % 10;
        lcd_buf[i++] = '0' + num % 10;
    }
    lcd_buf[i] = 0;
    lcd_print_line(row, lcd_buf);
}

void main(void)
{
    uint8_t i, v, missing, any_fell, all_done, all_ff, bus_idle;
    uint8_t val[8], flg[8];
    uint16_t round = 0;

    uart_init(0);
    idle_pins();
    lcd_init();
    lcd_print_line(0, "Wiring self-test");
    lcd_print_line(1, "see serial log");

    for (;;) {
        round++;
        say("GW,--- self-test round "); uart_putu(round); say(" ---\r\n");

        /* 1. Port 0 pull-ups (P0.0-P0.4: outputs to the ADC) */
        missing = 0xFF;
        for (i = 0; i < 5; i++) {
            p0_set(i, 0);
            tiny_wait();
            p0_set(i, 1);                    /* release: only a pull-up can lift it */
            tiny_wait();
            v = p0_get(i);
            p0_set(i, 0);
            say("GW,pullup P0."); uart_putc('0' + i);
            say(v ? " OK\r\n" : " MISSING (pin can't go high)\r\n");
            if (!v && missing == 0xFF) missing = i;
        }
        idle_pins();
        say("GW,EOC (P0.5) at rest = "); uart_putc('0' + ADC_EOC);
        say(" (expect 1)\r\n");

        /* 2 + 3. ADC conversions and data bus */
        bus_idle = ADC_DATA;                 /* OE off: bus floats, reads 255 */
        any_fell = 0; all_done = 1; all_ff = 1;
        for (i = 0; i < 8; i++) {
            flg[i] = adc_try(i, &val[i]);
            if (flg[i] & F_FELL) any_fell = 1;
            if (!(flg[i] & F_DONE)) all_done = 0;
            if (val[i] != 0xFF) all_ff = 0;
            say("GW,ADC IN"); uart_putc('0' + i); say(" = "); uart_putu(val[i]);
            say((flg[i] & F_FELL) ? "  start:ok" : "  start:NO");
            say((flg[i] & F_DONE) ? "  done:ok" : "  done:NO");
            say(i == 3 || i >= 5 ? "  (GND, expect ~0)\r\n" :
                i == 4 ? "  (LDR)\r\n" : "  (piezo, expect ~128)\r\n");
        }
        idle_pins();
        say("GW,bus with OE off = "); uart_putu(bus_idle); say(" (expect 255)\r\n");

        /* verdict */
        say("GW,VERDICT: ");
        if (missing != 0xFF) {
            say("add a 10k pull-up from P0."); uart_putc('0' + missing);
            say(" to +5V (check all of P0.0-P0.5)\r\n");
            lcd_show(0, "Pull-up miss P0.", missing, 1);
            lcd_show(1, "10k from pin->5V", 0, 0);
        } else if (!any_fell) {
            say("START never reaches the ADC: P0.3 -> ADC pins 6 AND 22; "
                "also ADC power (pin 11 +5V, 13 GND) and EOC (pin 7 -> P0.5)\r\n");
            lcd_show(0, "ADC not starting", 0, 0);
            lcd_show(1, "P0.3>pin6+22,pwr", 0, 0);
        } else if (!all_done) {
            say("ADC starts but never finishes = no clock: 74LS74 pin 3 <- ALE "
                "(8051 pin 30), 74LS74 pin 5 -> ADC pin 10, 74LS74 pins 1,4,14 +5V\r\n");
            lcd_show(0, "ADC has no clock", 0, 0);
            lcd_show(1, "check 74LS74", 0, 0);
        } else if (all_ff) {
            say("ADC converts but P1 reads 255: OE (P0.4 -> ADC pin 9) or the "
                "8 data wires (ADC 17,14,15,8,18,19,20,21 -> P1.0..P1.7)\r\n");
            lcd_show(0, "ADC data not on", 0, 0);
            lcd_show(1, "P1: OE/data wire", 0, 0);
        } else if (val[3] > 10) {
            say("data arrives but IN3 (grounded) reads "); uart_putu(val[3]);
            say(": data wires swapped, or IN3 (ADC pin 1) not on GND\r\n");
            lcd_show(0, "IN3 should be 0:", 0, 0);
            lcd_show(1, "IN3 = ", val[3], 1);
        } else {
            say("ADC wiring OK. IN0-IN2 should be ~128 (VREF); cover the LDR "
                "and IN4 should drop\r\n");
            lcd_show(0, "ADC OK  LDR ", val[4], 1);
            lcd_show(1, "IN0 ", val[0], 1);
        }

        delay_ms(3000);
    }
}
