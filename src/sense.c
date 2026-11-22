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

/* Touched on every sample, so they live in directly addressed RAM. */
static __data uint16_t base_q8[N_PIEZO];   /* baseline * 256, tracked only while quiet */
static __data uint16_t noise_q8[N_PIEZO];  /* mean |deviation| * 256, background noise */

/* Touched only on hits. */
typedef struct {
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
static uint8_t  win_t8, t8_div;          /* win_ms / 8 without a 16-bit shift */
static volatile uint8_t thr = PIEZO_THR_DAY;

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
    win_t8 = 0;
    t8_div = 0;
}

void sense_init(void)
{
    uint8_t i;

    adc_init();
    for (i = 0; i < N_PIEZO; i++) {
        base_q8[i]  = (uint16_t)adc_read(i) << 8;
        noise_q8[i] = 0;
    }
    ldr = adc_read(CH_LDR);
    piezo_reset();

    TMOD = (TMOD & 0xF0) | 0x01;         /* Timer0 mode 1 (16-bit) */
    TH0 = T0_RELOAD_H;
    TL0 = T0_RELOAD_L;
    ET0 = 1;
    TR0 = 1;
}

/* A sample above threshold: update the hit features. Called from the ISR only. */
static void piezo_hit(uint8_t i, uint8_t a)
{
    piezo_t __idata *p = &pz[i];

    if (p->active < 0xFFFF) p->active++;
    if (a > p->peak) p->peak = a;
    if (p->hits == 0 || (uint16_t)(win_ms - p->last) >= HIT_DEADTIME_MS) {
        if (p->hits == 0) {
            p->first = win_t8;
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

/* One piezo, one sample. Written out per channel (constant index) so every
 * access is a direct address, and it avoids 16-bit shifts: the 8051 shifts
 * one bit per instruction, but ">> 8" is just the high byte.
 *   adaptive threshold = noise x 4, never below the day / night minimum;
 *   noise follows |deviation| clipped at the threshold, so a footstep barely
 *   moves it but a lasting rise in noise still pulls it up;
 *   the baseline (~256 ms average) only tracks quiet samples. */
#define PIEZO_STEP(I)                                                         \
    do {                                                                      \
        x = adc_read(I);                                                      \
        b = base_q8[I] >> 8;                                                  \
        a = (x > b) ? x - b : b - x;                                          \
        n = noise_q8[I] >> 8;                                                 \
        tt = (n >= (256 >> NOISE_MULT_SHIFT)) ? 255 : (uint8_t)(n << NOISE_MULT_SHIFT); \
        if (tt < thr) tt = thr;                                               \
        if (a < tt) {                                                         \
            noise_q8[I] += a;                                                 \
            noise_q8[I] -= n;                                                 \
            base_q8[I]  += x;                                                 \
            base_q8[I]  -= b;                                                 \
        } else {                                                              \
            noise_q8[I] += tt;                                                \
            noise_q8[I] -= n;                                                 \
            piezo_hit(I, a);                                                  \
        }                                                                     \
    } while (0)

void timer0_isr(void) __interrupt(1)
{
    uint8_t x, b, a, n, tt;

    TH0 = T0_RELOAD_H;
    TL0 = T0_RELOAD_L;
    ms_ticks++;
    if (win_ms < 0xFFFE) win_ms++;
    if (++t8_div >= 8) {
        t8_div = 0;
        if (win_t8 < 254) win_t8++;
    }

    PIEZO_STEP(0);
    PIEZO_STEP(1);
    PIEZO_STEP(2);
#if N_PIEZO != 3
#error "timer0_isr samples exactly three piezos"
#endif

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
        w->noise[i]   = (uint8_t)((noise_q8[i] + 128) >> 8);
    }
    piezo_reset();
    EA = 1;
}
