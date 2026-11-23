/*
 * GroundWatch - multi-sensor intrusion detection on the 8051
 * Target: AT89S52 / P89V51 (8052-compatible), 11.0592 MHz crystal, 5 V
 *
 * Pins and thresholds live here only. Thresholds are starting values:
 * record windows with tools/groundwatch_pc.py and replace them with real numbers.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "mcu.h"            /* chip header + stdint for SDCC, Keil or host */

/* ---------------- Pin map (slide 4) ----------------
 * P1.0-P1.7  <- ADC0809 / ADC0808 D0..D7 (D0 = LSB, ADC pin 17 is "2^-8" = LSB)
 * P0.0-P0.2  -> ADC0808 ADD A, B, C
 * P0.3       -> ADC0808 ALE + START (tie both pins together)
 * P0.4       -> ADC0808 OE
 * P0.5       <- ADC0808 EOC
 *               Port 0 is open-drain: 10k pull-up to 5 V on P0.0..P0.5
 * P3.2       <- PIR (HC-SR501) OUT, direct: motion = high (3.3 V, above
 *               the 1.9 V VIH of the AT89S52). Polled every 10 ms, because
 *               INT0 only fires on a falling edge.
 * P3.3       <- raw-mode jumper: GND at reset streams raw samples at 57600
 * P3.0 / P3.1 <-> UART RXD / TXD to USB-UART module
 * P2.0 RS, P2.1 EN, P2.4-P2.7 D4-D7 -> 16x2 LCD (RW to GND)
 * P2.2       -> buzzer through BC547 low-side switch (high = on):
 *               10k pull-up to 5 V on P2.2, 1k into the base
 * P2.3       -> alarm LED, active low (5 V -> 1k -> LED -> pin, ~3 mA)
 */
#define ADC_DATA        P1
#define ADC_A           P0_0
#define ADC_B           P0_1
#define ADC_C           P0_2
#define ADC_START       P0_3
#define ADC_OE          P0_4
#define ADC_EOC         P0_5

#define RAW_MODE_PIN    P3_3                /* jumper to GND at reset = raw streaming */

#define PIR_PIN         P3_2
#define PIR_ACTIVE()    (PIR_PIN == 1)      /* HC-SR501 output is active high */

#define LCD_RS          P2_0
#define LCD_EN          P2_1
#define LCD_D4          P2_4
#define LCD_D5          P2_5
#define LCD_D6          P2_6
#define LCD_D7          P2_7

#define BUZZER          P2_2
#define ALARM_LED       P2_3
#define LED_ON          0
#define LED_OFF         1

/* ---------------- ADC0808 channels ----------------
 * Each piezo goes through an LM358 non-inverting stage, gain 11, biased at a
 * shared 2.5 V VREF: at rest the ADC reads ~128 and the baseline tracks it.
 * The LM358 only swings up to ~3.5 V on 5 V, so upward deviations clip at
 * ~50 counts; downward ones have the full ~128. Peaks above ~50 (STRONG_PEAK)
 * therefore come from the downward half of the ringing, which piezos always have.
 * Until a channel is built its input is tied to VREF and reads ~0 deviation. */
#define N_PIEZO         3       /* IN0 = zone L, IN1 = zone C, IN2 = zone R */
#define CH_LDR          4       /* IN3, IN5-IN7 unused */

/* ---------------- Timing (ms) ---------------- */
#define WINDOW_MS       2000    /* classify period */
#define HIT_DEADTIME_MS 250     /* onsets closer than this are one step ringing twice;
                                   real steps are >= ~330 ms apart even running */
#define STEP_CARRY_MS   1500    /* a step this soon after the previous window's last
                                   step still gives a gap (slow walking ~1.2 s) */
#define PIR_WARMUP_MS   60000u  /* HC-SR501 output is unreliable for ~1 min after power-on */
#define ALARM_MS        3000    /* LED + LCD show each PERSON/DIGGING window this long */

/* Buzzer policy (fusion.c, alarm_update). Counted in 2 s windows, not ms, so the
 * 16-bit ms counter's 65 s wrap can't upset the 40 s cycle. */
#define ALARM_CONFIRM   2       /* sound only when this many of the last 3 windows
                                   are PERSON/DIGGING: a one-off is only shown */
#define ALARM_SOUND_WIN 5       /* then beep for 5 windows (10 s) */
#define ALARM_QUIET_WIN 15      /* then stay silent 15 windows (30 s) even if
                                   detections go on; they are still shown and logged */

/* ---------------- Event trigger (STA/LTA, see sense.c) ----------------
 * STA = ~16 ms envelope of |deviation from baseline|, LTA = ~2 s background.
 * An event starts when STA > max(TRIG_MIN, 3 x LTA) and ends when
 * STA < max(TRIG_MIN / 2, 1.5 x LTA). Units: 8-bit ADC counts. */
#define TRIG_MIN_DAY    8       /* minimum STA for an event by day */
#define TRIG_MIN_NIGHT  5       /* night mode: more sensitive */

/* ---------------- Classification (peak |deviation| inside events) ---------------- */
#define MED_PEAK        40      /* person-sized peaks, by day */
#define MED_PEAK_NIGHT  25      /* at night: the quieter ground lets the trigger hear
                                   smaller steps (TRIG_MIN_NIGHT), and this lets them
                                   count as a person. Trade-off: animals are more
                                   active at night. Tune from night recordings. */
#define STRONG_PEAK     100     /* shovel / heavy impact */

/* ---------------- Other sensors ---------------- */
#define DIG_HITS        3       /* strong hits per window that mean digging
                                   (unvalidated: needs recordings of real digging) */
#define LDR_DARK        60      /* below this = night (LDR to 5 V, 10k to GND) */
#define LDR_LIGHT       80      /* above this = day (hysteresis) */

/* ---------------- Events ---------------- */
#define EV_NONE         0       /* no piezo hit this window */
#define EV_NOISE        1       /* wind / rain: hit but no PIR -> ignore */
#define EV_ANIMAL       2       /* log only */
#define EV_PERSON       3       /* alarm */
#define EV_DIGGING      4       /* alarm */

#define ZONE_NONE       0xFF

#endif
