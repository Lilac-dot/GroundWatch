/*
 * GroundWatch - multi-sensor intrusion detection and localization, 8051 side
 *
 * Flow: a piezo crosses its threshold -> zone = first and strongest hit
 *   -> check PIR -> match the fusion table -> alarm, LCD, UART log
 *
 * Timer0 gives a 1 ms tick and samples the piezos inside its ISR (sense.c).
 * The main loop runs a countdown scheduler for the slower tasks:
 *   PIR + light     10 ms
 *   display + alarm 200 ms
 *   classify + log  2 s
 */
#include "config.h"
#include "sense.h"
#include "fusion.h"
#include "uart.h"
#include "lcd.h"

static __code const char * __code const ev_name[5] = {
    "NONE", "NOISE", "ANIMAL", "PERSON", "DIGGING"
};
static __code const char zone_char[N_PIEZO] = { 'L', 'C', 'R' };

/* ======================= shared state ======================= */

static __bit pir_seen;                   /* PIR was high at some poll this window */
static __bit pir_now;                    /* PIR level at the last poll, for the LCD */
static __bit night;
static __bit alarm_on;
static __bit display_dirty;

/* main-loop state lives in indirect RAM: the direct RAM below 0x80 is kept
 * for the sampling ISR (and the trained classifier, when there is one) */
static __idata uint8_t  last_ev = EV_NONE;
static __idata uint8_t  last_zone = ZONE_NONE;
static __idata uint8_t  last_peak, last_hits;
static __idata uint16_t alarm_until;

static __idata window_t win;

/* ======================= helpers ======================= */

/* "EV,PERSON,C, peak x3, hits x3, active ms x3, first ms x3,
 *  gap min ms x3, gap max ms x3, noise x3, pir, night, ldr"   (x3 = L, C, R) */
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
        if (win.first[i] == FIRST_NONE) uart_putc('-');
        else                            uart_putu(win.first[i]);
    }
    for (i = 0; i < N_PIEZO; i++) {      /* gaps need 2+ hits, '-' otherwise */
        uart_putc(',');
        if (win.hits[i] < 2) uart_putc('-');
        else                 uart_putu(win.gap_min[i] * 8u);
    }
    for (i = 0; i < N_PIEZO; i++) {
        uart_putc(',');
        if (win.hits[i] < 2) uart_putc('-');
        else                 uart_putu(win.gap_max[i] * 8u);
    }
    for (i = 0; i < N_PIEZO; i++) { uart_putc(','); uart_putu(win.noise[i]); }
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

/* 10 ms: latch the PIR, switch day / night from the LDR.
 * Polling is enough: the HC-SR501 holds OUT high for seconds after motion. */
static void task_pir_light(void)
{
    uint8_t l = sense_ldr();

    if (PIR_ACTIVE()) pir_seen = 1;
    if (PIR_ACTIVE() != pir_now) {       /* show "Motion" on the idle screen */
        pir_now = !pir_now;
        display_dirty = 1;
    }

    if (!night && l < LDR_DARK) {
        night = 1;
        sense_set_threshold(TRIG_MIN_NIGHT);
        display_dirty = 1;
    } else if (night && l > LDR_LIGHT) {
        night = 0;
        sense_set_threshold(TRIG_MIN_DAY);
        display_dirty = 1;
    }
}

/* 2 s: fuse everything, decide the event and zone, alarm + log */
static void task_classify(void)
{
    uint8_t z, ev;

    sense_take_window(&win);
    win.pir   = (pir_seen || PIR_ACTIVE()) ? 1 : 0;
    win.night = night;
    pir_seen  = 0;

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
    if (!sense_raw_mode()) log_window(ev, z);   /* raw mode: the UART carries samples */
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

    /* line 0: "PERSON   Zone C" while alarming, else "Listening   DAY" ("Motion" while the PIR is high) */
    lcd_print_line(0, alarm_on ? ev_name[last_ev] :
                      pir_now ? "Motion" : sense_raw_mode() ? "Raw 57600" : "Listening");
    if (alarm_on) {
        lcd_puts_at(0, 9, "Zone ");
        lcd_putc(zone_char[last_zone]);
    } else {
        lcd_puts_at(0, 11, night ? "NIGHT" : "  DAY");
    }

    /* line 1: "pk 87 hits  3" while alarming, else "Last: ANIMAL  C" */
    if (last_ev == EV_NONE) {
        lcd_print_line(1, "No events");
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

#define N_TASKS 3
static void (* __code const task_fn[N_TASKS])(void) = {
    task_pir_light, task_display, task_classify
};
static __code const uint16_t task_period[N_TASKS] = { 10, 200, WINDOW_MS };
static __idata uint16_t task_left[N_TASKS];

void main(void)
{
    uint8_t  i;
    uint16_t now, prev, el;

    BUZZER    = 0;                       /* port pins come up high after reset */
    ALARM_LED = LED_OFF;

    sense_init();                        /* reads the raw-mode jumper */
    uart_init(sense_raw_mode());
    EA = 1;

    lcd_init();
    lcd_print_line(0, "GroundWatch");
    lcd_print_line(1, "Calibrating...");

    delay_ms(1500);                      /* let the piezo baselines settle */
    if (!sense_raw_mode()) uart_puts("GW,ready\r\n");

    sense_take_window(&win);             /* throw away the settling window */
    pir_seen = 0;
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
