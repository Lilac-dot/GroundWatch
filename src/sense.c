/*
 * Sampling and event detection.
 *
 * Sampling: Timer0 in 8-bit auto-reload mode interrupts every 250 us (4 kHz),
 * cycling through four slots: piezo L, piezo C, piezo R, LDR. Each slot reads
 * the conversion started in the previous slot and immediately starts the next
 * channel, so the ~70 us conversion runs while the CPU does the maths instead
 * of the CPU waiting on EOC. Every channel is sampled at ~1 kHz; inside one
 * round the channels are 250 us apart (L, C, R), a fixed skew the PC can
 * correct for.
 *
 * Detection (per piezo, per sample), after Koc & Yegin's slow/quick adaptive
 * thresholds and the STA/LTA trigger used for seismic arrival picking:
 *   d   = |x - baseline|               baseline: ~256 ms average of x
 *   STA = average of d over ~16 ms     the short-term envelope
 *   LTA = average of STA over ~2 s     the background level (capped input during events)
 *   event ON  when STA > max(minimum, 3 x LTA)
 *   event OFF when STA < max(minimum / 2, 1.5 x LTA)   (hysteresis)
 * The ratios follow the 2.7 on / 1.8 off used by embedded STA/LTA pickers;
 * they are shifts and adds because the ISR can't call the multiply routine.
 * An event onset is a "hit" unless it comes within HIT_DEADTIME_MS of the
 * previous hit (the same step still ringing). Its time (1 ms resolution) gives
 * the first-hit order for localization, and onset-to-onset gaps give the
 * walking rhythm. The gap from the last step of the previous window is kept
 * (if under STEP_CARRY_MS), so a walk split by a window boundary still shows
 * its rhythm.
 *
 * Raw mode (P3.3 grounded at reset): the UART runs at 57600 baud and the ISR
 * writes every sample straight to SBUF as L, C, R, 0xFF (sync), one byte per
 * slot. A byte takes 174 us to send, less than one 250 us slot, so no buffer
 * is needed. tools/groundwatch_raw.py records the stream.
 *
 * Timing rules for the ISR: no function calls (it runs in register bank 1),
 * fixed array indexes (direct addressing), and no 16-bit shifts other than by
 * 8 (the high byte) or 4 (nibble swap) in the per-sample path.
 */
#include "config.h"
#include "sense.h"
#include "adc0808.h"

/* 11.0592 MHz / 12 = 921.6 kHz timer clock; 230 counts = 249.6 us (4007 Hz). */
#define SLOT_COUNTS 230

/* ---- per-sample state: directly addressed RAM ---- */
static __data uint16_t base_q8[N_PIEZO];   /* baseline x 256 */
static __data uint16_t sta_q4[N_PIEZO];    /* STA x 16 */
static __data uint16_t on_q4[N_PIEZO];     /* event-on threshold, STA x 16 */
static __data uint16_t off_q4[N_PIEZO];    /* event-off threshold, STA x 16 */
static __data uint8_t  slot;               /* 0..3: which channel's result is due */
static __bit trig0, trig1, trig2;          /* piezo is inside an event */
static __bit raw;                          /* raw streaming mode */

/* ---- slower state ---- */
static __idata uint16_t lta_q8[N_PIEZO];   /* LTA x 256, updated every 8 ms */

typedef struct {
    uint8_t  peak;      /* largest |deviation| inside events */
    uint8_t  hits;      /* event onsets */
    uint16_t active;    /* ms inside events */
    uint16_t last;      /* ms_ticks at the latest counted onset (kept across windows) */
    uint16_t first;     /* ms of the first onset, FIRST_NONE = none */
    uint8_t  gap_min, gap_max;   /* onset-to-onset gaps, 8 ms units */
} piezo_t;
static __idata piezo_t pz[N_PIEZO];

static volatile uint16_t ms_ticks;
static volatile uint16_t win_ms;
static volatile uint8_t  trig_min = TRIG_MIN_DAY;
static volatile uint8_t  ldr;

static void piezo_reset(void)
{
    uint8_t i;
    for (i = 0; i < N_PIEZO; i++) {
        /* forget a step too old to carry over, so a 16-bit wrap of ms_ticks
         * (every 65 s) can never make it look recent again */
        if ((uint16_t)(ms_ticks - pz[i].last) > STEP_CARRY_MS) pz[i].last = ms_ticks - 0x8000u;
        pz[i].peak    = 0;
        pz[i].hits    = 0;
        pz[i].active  = 0;
        pz[i].first   = FIRST_NONE;
        pz[i].gap_min = T8_NONE;
        pz[i].gap_max = 0;
    }
    win_ms = 0;
}

/* One piezo's LTA and its two thresholds. Run in the LDR slot, one channel per
 * ms, so each channel updates every 8 ms (LTA time constant 256 x 8 ms ~ 2 s).
 * Inside an event the LTA input is capped at the on-threshold: a footstep
 * barely moves it, but noise that stays high (wind, a generator) still pulls
 * the LTA up until the event ends. Freezing the LTA instead would let a
 * lasting rise in noise hold the trigger on forever. */
#define LTA_UPDATE(I, TRIG)                                                   \
    do {                                                                      \
        uint16_t l4, on, off;                                                 \
        uint16_t sq = sta_q4[I];                                              \
        uint8_t  s;                                                           \
        if ((TRIG) && sq > on_q4[I]) sq = on_q4[I];                           \
        s = (sq >= 0x1000) ? 255 : (uint8_t)(sq >> 4);                        \
        lta_q8[I] += s;                                                       \
        lta_q8[I] -= (uint8_t)(lta_q8[I] >> 8);                               \
        l4  = lta_q8[I] >> 4;                    /* LTA x 16 */               \
        on  = (l4 << 1) + l4;                    /* 3 x LTA */                \
        off = l4 + (l4 >> 1);                    /* 1.5 x LTA */              \
        if (on  < ((uint16_t)trig_min << 4)) on  = (uint16_t)trig_min << 4;   \
        if (off < ((uint16_t)trig_min << 3)) off = (uint16_t)trig_min << 3;   \
        on_q4[I]  = on;                                                       \
        off_q4[I] = off;                                                      \
    } while (0)

/* One piezo sample: baseline, envelope, trigger, and the hit features. */
#define PIEZO_SAMPLE(I, TRIG)                                                 \
    do {                                                                      \
        b = base_q8[I] >> 8;                                                  \
        d = (x > b) ? x - b : b - x;                                          \
        sta_q4[I] += d;                                                       \
        sta_q4[I] -= sta_q4[I] >> 4;                                          \
        base_q8[I] += x;            /* a footstep's ring is ~zero-mean, so */ \
        base_q8[I] -= b;            /* the baseline keeps tracking DC drift */\
        if (!(TRIG)) {                                                        \
            if (sta_q4[I] > on_q4[I]) {                                       \
                piezo_t __idata *p = &pz[I];                                  \
                TRIG = 1;                                                     \
                uint16_t g = (uint16_t)(ms_ticks - p->last);                  \
                if (d > p->peak) p->peak = d;                                 \
                /* closer than the dead time = the same step ringing again */ \
                if (g >= HIT_DEADTIME_MS) {                                   \
                    if (p->hits == 0) p->first = win_ms;                      \
                    /* the gap to the previous step counts even when that */  \
                    /* step fell in the previous window (STEP_CARRY_MS)  */   \
                    if (p->hits || g < STEP_CARRY_MS) {                       \
                        uint8_t g8 = (g < 254 * 8) ? (uint8_t)(g >> 3) : 254; \
                        if (g8 < p->gap_min) p->gap_min = g8;                 \
                        if (g8 > p->gap_max) p->gap_max = g8;                 \
                    }                                                         \
                    if (p->hits < 255) p->hits++;                             \
                    p->last = ms_ticks;                                       \
                }                                                             \
            }                                                                 \
        } else {                                                              \
            piezo_t __idata *p = &pz[I];                                      \
            if (d > p->peak) p->peak = d;                                     \
            if (p->active < 0xFFFF) p->active++;                              \
            if (sta_q4[I] < off_q4[I]) TRIG = 0;                              \
        }                                                                     \
    } while (0)

void timer0_isr(void) __interrupt(1) __using(1)
{
    uint8_t x, b, d;

    /* 1. collect the conversion started one slot ago (finished ~180 us ago) */
    ADC_OE = 1;
    x = ADC_DATA;
    ADC_OE = 0;

    /* 2. start the next channel (bit writes only, see adc0808.c):
     *    slot 0 -> IN1   slot 1 -> IN2   slot 2 -> IN4 (LDR)   slot 3 -> IN0 */
    switch (slot) {
    case 0:  ADC_A = 1; ADC_B = 0; ADC_C = 0; break;
    case 1:  ADC_A = 0; ADC_B = 1; ADC_C = 0; break;
    case 2:  ADC_A = 0; ADC_B = 0; ADC_C = 1; break;
    default: ADC_A = 0; ADC_B = 0; ADC_C = 0; break;
    }
    ADC_START = 1;          /* rising edge latches the address, falling edge starts */
    ADC_START = 0;

    /* 3. raw mode: stream the sample (0xFF is reserved for sync) */
    if (raw) {
        TI = 0;
        SBUF = (slot == 3) ? 0xFF : ((x == 0xFF) ? 0xFE : x);
    }

    /* 4. process the sample that just arrived */
    switch (slot) {
    case 0:
        PIEZO_SAMPLE(0, trig0);
        slot = 1;
        break;
    case 1:
        PIEZO_SAMPLE(1, trig1);
        slot = 2;
        break;
    case 2:
        PIEZO_SAMPLE(2, trig2);
        slot = 3;
        break;
    default:                /* LDR slot, plus the 1 ms housekeeping */
        ldr = x;
        ms_ticks++;
        if (win_ms < 0xFFFE) win_ms++;
        switch ((uint8_t)ms_ticks & 7) {
        case 0: LTA_UPDATE(0, trig0); break;
        case 1: LTA_UPDATE(1, trig1); break;
        case 2: LTA_UPDATE(2, trig2); break;
        default: break;
        }
        slot = 0;
        break;
    }
}

void sense_init(void)
{
    uint8_t i, x;

    raw = (RAW_MODE_PIN == 0);           /* jumper to GND at reset */

    adc_init();
    for (i = 0; i < N_PIEZO; i++) {
        x = adc_read(i);
        base_q8[i] = (uint16_t)x << 8;
        sta_q4[i]  = 0;
        lta_q8[i]  = 0;
        on_q4[i]   = (uint16_t)TRIG_MIN_DAY << 4;
        off_q4[i]  = (uint16_t)TRIG_MIN_DAY << 3;
    }
    trig0 = trig1 = trig2 = 0;
    ldr = adc_read(CH_LDR);
    ms_ticks = 0;
    for (i = 0; i < N_PIEZO; i++) pz[i].last = 0x8000u;   /* no previous step */
    piezo_reset();

    /* prime the pipeline: the first slot collects IN0 */
    ADC_A = 0; ADC_B = 0; ADC_C = 0;
    ADC_START = 1;
    ADC_START = 0;
    slot = 0;

    TMOD = (TMOD & 0xF0) | 0x02;         /* Timer0 mode 2, 8-bit auto-reload */
    TH0 = 256 - SLOT_COUNTS;
    TL0 = 256 - SLOT_COUNTS;
    PT0 = 1;                             /* sampling outranks everything else */
    ET0 = 1;
    TR0 = 1;
}

uint8_t sense_raw_mode(void) { return raw; }

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

void sense_set_threshold(uint8_t t) { trig_min = t; }
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
        w->noise[i]   = (uint8_t)((lta_q8[i] + 128) >> 8);
    }
    piezo_reset();
    EA = 1;
}
