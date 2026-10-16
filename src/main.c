/*
 * GroundWatch - multi-sensor intrusion detection and localization, 8051 side
 *
 * Flow (slide 6): a piezo crosses its threshold -> zone = first and strongest hit
 *   -> check PIR, magnetometer, sound -> match the fusion table
 *   -> alarm, LCD, UART log
 *
 * Timer0 gives a 1 ms tick and samples the piezos inside its ISR (sense.c).
 * The main loop runs a countdown scheduler for the slower tasks (slide 5):
 *   sound + light   10 ms
 *   magnetometer    100 ms
 *   display + alarm 200 ms
 *   classify + log  2 s
 */
#include "config.h"
#include "sense.h"
#include "fusion.h"
#include "qmc5883l.h"
#include "uart.h"
#include "lcd.h"

/* SDCC needs every ISR prototype visible in the file with main() */
void int0_isr(void) __interrupt(0);

static __code const char * __code const ev_name[6] = {
    "NONE", "NOISE", "ANIMAL", "PERSON", "VEHICLE", "DIGGING"
};
static __code const char zone_char[N_PIEZO] = { 'L', 'C', 'R' };

/* ======================= shared state ======================= */

static volatile __bit pir_flag;          /* set by INT0 */
static __bit night;
static __bit snd_above;
static __bit mag_ok;
static __bit alarm_on;
static __bit display_dirty;

static uint8_t  snd_bursts;
static uint16_t mag_max;
static int16_t  mag_bx, mag_by, mag_bz;  /* baseline field */

static uint8_t  last_ev = EV_NONE;
static uint8_t  last_zone = ZONE_NONE;
static uint8_t  last_peak, last_hits;
static uint16_t alarm_until;

static __idata window_t win;

/* ======================= interrupts ======================= */

void int0_isr(void) __interrupt(0)
{
    pir_flag = 1;                        /* PIR went active (falling edge after the NPN) */
}

/* ======================= helpers ======================= */

static uint16_t absdiff(int16_t a, int16_t b)
{
    return (a > b) ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

static void mag_calibrate(void)
{
    int16_t x, y, z;
    uint8_t i;

    mag_bx = mag_by = mag_bz = 0;
    for (i = 0; i < 8; i++) {            /* average of 8; /8 first so int16 can't overflow */
        if (!mag_read(&x, &y, &z)) { mag_ok = 0; return; }
        mag_bx += x / 8;
        mag_by += y / 8;
        mag_bz += z / 8;
        delay_ms(20);
    }
}

/* "EV,PERSON,C,pk0,pk1,pk2,h0,h1,h2,act0,act1,act2,first0,first1,first2,mag,snd,pir,night,ldr" */
static void log_window(uint8_t ev, uint8_t z)
{
    uint8_t i;

    uart_puts("EV,");
    uart_puts(ev_name[ev]);
    uart_putc(',');
    uart_putc(z == ZONE_NONE ? '-' : zone_char[z]);
    for (i = 0; i < N_PIEZO; i++) { uart_putc(','); uart_putu(win.peak[i]); }
    for (i = 0; i < N_PIEZO; i++) { uart_putc(','); uart_putu(win.hits[i]); }
    for (i = 0; i < N_PIEZO; i++) { uart_putc(','); uart_putu(win.active[i]); }
    for (i = 0; i < N_PIEZO; i++) {      /* first hit in ms, '-' if none */
        uart_putc(',');
        if (win.first[i] == T8_NONE) uart_putc('-');
        else                         uart_putu(win.first[i] * 8u);
    }
    uart_putc(','); uart_putu(win.mag_dev);
    uart_putc(','); uart_putu(win.snd_bursts);
    uart_putc(','); uart_putu(win.pir);
    uart_putc(','); uart_putu(win.night);
    uart_putc(','); uart_putu(sense_ldr());
    uart_puts("\r\n");
}

static void lcd_put3(uint8_t v)          /* right-aligned 3 digits */
{
    lcd_putc(v >= 100 ? '0' + v / 100 : ' ');
    lcd_putc(v >= 10  ? '0' + (v / 10) % 10 : ' ');
    lcd_putc('0' + v % 10);
}

static void lcd_puts_at(uint8_t row, uint8_t col, const char *s)
{
    lcd_goto(row, col);
    while (*s) lcd_putc(*s++);
}

/* ======================= tasks ======================= */

/* 10 ms: count mic bursts, switch day / night from the LDR */
static void task_sound_light(void)
{
    uint8_t s = sense_sound_peak();
    uint8_t l = sense_ldr();

    if (s >= SOUND_THR) {
        if (!snd_above && snd_bursts < 255) snd_bursts++;
        snd_above = 1;
    } else {
        snd_above = 0;
    }

    if (!night && l < LDR_DARK) {
        night = 1;
        sense_set_threshold(PIEZO_THR_NIGHT);
        display_dirty = 1;
    } else if (night && l > LDR_LIGHT) {
        night = 0;
        sense_set_threshold(PIEZO_THR_DAY);
        display_dirty = 1;
    }
}

/* 100 ms: compare the field with the baseline; drift slowly while quiet */
static void task_mag(void)
{
    int16_t  x, y, z;
    uint16_t d;

    if (!mag_ok || !mag_read(&x, &y, &z)) return;

    d = absdiff(x, mag_bx);
    d += absdiff(y, mag_by);
    d += absdiff(z, mag_bz);
    if (d > mag_max) mag_max = d;

    if (d < MAG_THR / 4) {               /* temperature drift, not a car */
        mag_bx += (x - mag_bx) / 16;
        mag_by += (y - mag_by) / 16;
        mag_bz += (z - mag_bz) / 16;
    }
}

/* 2 s: fuse everything, decide the event and zone, alarm + log */
static void task_classify(void)
{
    uint8_t z, ev;

    sense_take_window(&win);
    win.mag_dev    = mag_max;
    win.snd_bursts = snd_bursts;
    win.pir        = (pir_flag || PIR_ACTIVE()) ? 1 : 0;
    win.night      = night;
    mag_max    = 0;
    snd_bursts = 0;
    pir_flag   = 0;

    z  = fusion_zone(&win);
    ev = fusion_classify(&win, z);

    if (ev >= EV_ANIMAL) {               /* animal: log + LCD; the rest also alarm */
        last_ev   = ev;
        last_zone = z;
        last_peak = win.peak[z];
        last_hits = win.hits[z];
        display_dirty = 1;
    }
    if (ev >= EV_PERSON) {
        alarm_on    = 1;
        alarm_until = millis() + ALARM_MS;
    }
    log_window(ev, z);
}

/* 200 ms: buzzer beeps, LED on, LCD redrawn only when something changed */
static void task_display(void)
{
    static uint8_t beat;

    if (alarm_on && (int16_t)(millis() - alarm_until) >= 0) {
        alarm_on = 0;
        display_dirty = 1;
    }
    if (alarm_on) {
        ALARM_LED = LED_ON;
        BUZZER = (++beat & 1);
    } else {
        ALARM_LED = LED_OFF;
        BUZZER = 0;
    }

    if (!display_dirty) return;
    display_dirty = 0;

    /* line 0: "PERSON   Zone C" while alarming, else "Listening   DAY" */
    lcd_print_line(0, alarm_on ? ev_name[last_ev] : "Listening");
    if (alarm_on) {
        lcd_puts_at(0, 9, "Zone ");
        lcd_putc(zone_char[last_zone]);
    } else {
        lcd_puts_at(0, 11, night ? "NIGHT" : "  DAY");
    }

    /* line 1: "pk 87 hits  3" while alarming, else "Last: ANIMAL  C" */
    if (last_ev == EV_NONE) {
        lcd_print_line(1, "No events");
        if (!mag_ok) lcd_puts_at(1, 10, "noMAG");
    } else if (alarm_on) {
        lcd_print_line(1, "pk");
        lcd_goto(1, 2);
        lcd_put3(last_peak);
        lcd_puts_at(1, 6, "hits");
        lcd_put3(last_hits);
    } else {
        lcd_print_line(1, "Last: ");
        lcd_puts_at(1, 6, ev_name[last_ev]);
        lcd_goto(1, 15);
        lcd_putc(zone_char[last_zone]);
    }
}

/* ======================= scheduler ======================= */

#define N_TASKS 4
static void (* __code const task_fn[N_TASKS])(void) = {
    task_sound_light, task_mag, task_display, task_classify
};
static __code const uint16_t task_period[N_TASKS] = { 10, 100, 200, WINDOW_MS };
static __idata uint16_t task_left[N_TASKS];

void main(void)
{
    uint8_t  i;
    uint16_t now, prev, el;

    BUZZER    = 0;                       /* port pins come up high after reset */
    ALARM_LED = LED_OFF;

    uart_init();
    sense_init();
    IT0 = 1;                             /* INT0 on falling edge */
    EX0 = 1;
    EA  = 1;

    lcd_init();
    lcd_print_line(0, "GroundWatch");
    lcd_print_line(1, "Calibrating...");

    mag_ok = mag_init();
    if (mag_ok) {
        delay_ms(50);
        mag_calibrate();
    }
    delay_ms(1500);                      /* let the piezo baselines settle */

    uart_puts("GW,ready,mag=");
    uart_putu(mag_ok);
    uart_puts("\r\n");

    sense_take_window(&win);             /* throw away the settling window */
    pir_flag = 0;
    display_dirty = 1;
    for (i = 0; i < N_TASKS; i++) task_left[i] = task_period[i];

    prev = millis();
    for (;;) {
        now = millis();
        el  = now - prev;
        if (el == 0) {
            PCON |= 0x01;                /* idle until the next tick interrupt */
            continue;
        }
        prev = now;
        for (i = 0; i < N_TASKS; i++) {
            if (task_left[i] <= el) {
                task_left[i] = task_period[i];
                task_fn[i]();
            } else {
                task_left[i] -= el;
            }
        }
    }
}
