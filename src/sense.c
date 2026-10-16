/*
 * 1 ms tick + piezo sampling (slide 5: "Sample piezos, 1 ms, in ISR").
 *
 * Raw samples are never stored (128 bytes of RAM). Each piezo keeps running
 * values for the current window: peak, hit count, active time, first-hit time,
 * and the shortest / longest gap between hits (used to tell a regular
 * walking rhythm from random taps).
 */
#include "config.h"
#include "adc0808.h"
#include "sense.h"

/* 11.0592 MHz / 12 = 921.6 kHz -> 922 counts per ms -> 65536 - 922 = 0xFC66 */
#define T0_RELOAD_H 0xFC
#define T0_RELOAD_L 0x66

typedef struct {
    uint16_t base_q7;   /* baseline * 128, tracked only while quiet */
    uint8_t  peak;
    uint8_t  hits;
    uint16_t active;    /* ms above threshold */
    uint16_t last;      /* ms of the latest hit */
    uint8_t  first;     /* first hit, 8 ms units */
    uint8_t  gap_min, gap_max;   /* 8 ms units */
} piezo_t;

static __idata piezo_t pz[N_PIEZO];
static volatile uint16_t ms_ticks;
static uint16_t win_ms;
static volatile uint8_t thr = PIEZO_THR_DAY;

static uint16_t snd_base_q7;
static volatile uint8_t snd_peak;
static volatile uint8_t ldr;
static uint8_t ldr_div;

static void piezo_reset(void)
{
    uint8_t i;
    for (i = 0; i < N_PIEZO; i++) {
        pz[i].peak    = 0;
        pz[i].hits    = 0;
        pz[i].active  = 0;
        pz[i].first   = T8_NONE;
        pz[i].gap_min = T8_NONE;
        pz[i].gap_max = 0;
    }
    win_ms = 0;
}

void sense_init(void)
{
    uint8_t i;

    adc_init();
    for (i = 0; i < N_PIEZO; i++)
        pz[i].base_q7 = (uint16_t)adc_read(i) << 7;
    snd_base_q7 = (uint16_t)adc_read(CH_SOUND) << 7;
    ldr = adc_read(CH_LDR);
    piezo_reset();

    TMOD = (TMOD & 0xF0) | 0x01;         /* Timer0 mode 1 (16-bit) */
    TH0 = T0_RELOAD_H;
    TL0 = T0_RELOAD_L;
    ET0 = 1;
    TR0 = 1;
}

void timer0_isr(void) __interrupt(1)
{
    uint8_t  i, x, b, a, t8;
    piezo_t __idata *p;

    TH0 = T0_RELOAD_H;
    TL0 = T0_RELOAD_L;
    ms_ticks++;
    if (win_ms < 0xFFFE) win_ms++;
    t8 = (win_ms < 254 * 8) ? win_ms >> 3 : 254;

    for (i = 0; i < N_PIEZO; i++) {
        p = &pz[i];
        x = adc_read(i);
        b = p->base_q7 >> 7;
        a = (x > b) ? x - b : b - x;

        if (a < thr) {
            p->base_q7 = p->base_q7 - (p->base_q7 >> 7) + x;   /* ~128 ms average */
            continue;
        }
        if (p->active < 0xFFFF) p->active++;
        if (a > p->peak) p->peak = a;
        if (p->hits == 0 || (uint16_t)(win_ms - p->last) >= HIT_DEADTIME_MS) {
            if (p->hits == 0) {
                p->first = t8;
            } else {
                uint16_t g = (win_ms - p->last) >> 3;
                uint8_t  g8 = (g < 254) ? g : 254;
                if (g8 < p->gap_min) p->gap_min = g8;
                if (g8 > p->gap_max) p->gap_max = g8;
            }
            if (p->hits < 255) p->hits++;
            p->last = win_ms;
        }
    }

    x = adc_read(CH_SOUND);
    b = snd_base_q7 >> 7;
    a = (x > b) ? x - b : b - x;
    if (a < SOUND_THR) snd_base_q7 = snd_base_q7 - (snd_base_q7 >> 7) + x;
    if (a > snd_peak) snd_peak = a;

    if (++ldr_div >= 10) {
        ldr_div = 0;
        ldr = adc_read(CH_LDR);
    }
}

uint16_t millis(void)
{
    uint16_t t;
    EA = 0;                              /* 16-bit read must be atomic on an 8-bit CPU */
    t = ms_ticks;
    EA = 1;
    return t;
}

void delay_ms(uint16_t ms)
{
    uint16_t start = millis();
    while ((uint16_t)(millis() - start) < ms) ;
}

void sense_set_threshold(uint8_t t) { thr = t; }
uint8_t sense_ldr(void)             { return ldr; }

uint8_t sense_sound_peak(void)
{
    uint8_t v;
    EA = 0;
    v = snd_peak;
    snd_peak = 0;
    EA = 1;
    return v;
}

void sense_take_window(window_t *w)
{
    uint8_t i;
    EA = 0;
    for (i = 0; i < N_PIEZO; i++) {
        w->peak[i]    = pz[i].peak;
        w->hits[i]    = pz[i].hits;
        w->active[i]  = pz[i].active;
        w->first[i]   = pz[i].first;
        w->gap_min[i] = pz[i].gap_min;
        w->gap_max[i] = pz[i].gap_max;
    }
    piezo_reset();
    EA = 1;
}
