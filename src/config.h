/*
 * GroundWatch - multi-sensor intrusion detection on the 8051
 * Target: AT89S52 / P89V51 (8052-compatible), 11.0592 MHz crystal, 5 V
 *
 * Pins and thresholds live here only. Thresholds are starting values:
 * record windows with tools/groundwatch_pc.py and replace them with real numbers.
 */
#ifndef CONFIG_H
#define CONFIG_H

#ifdef __SDCC
#include <8052.h>
#endif
#include <stdint.h>

/* ---------------- Pin map (slide 4) ----------------
 * P1.0-P1.7  <- ADC0808 D0..D7 (D0 = LSB, ADC pin 17 is "2^-8" = LSB)
 * P0.0-P0.2  -> ADC0808 ADD A, B, C
 * P0.3       -> ADC0808 ALE + START (tie both pins together)
 * P0.4       -> ADC0808 OE
 * P0.5       <- ADC0808 EOC
 *               Port 0 is open-drain: 10k pull-up to 5 V on P0.0..P0.5
 * P3.2 INT0  <- PIR (HC-SR501) through an NPN inverter: motion = low
 * P3.4 / P3.5 <-> I2C SCL / SDA to QMC5883L (4.7k pull-ups, see README)
 * P3.0 / P3.1 <-> UART RXD / TXD to USB-UART module
 * P2.0 RS, P2.1 EN, P2.4-P2.7 D4-D7 -> 16x2 LCD (RW to GND)
 * P2.2       -> buzzer through BC547 (high = on)
 * P2.3       -> alarm LED, active low (5 V -> 330R -> LED -> pin)
 */
#define ADC_DATA        P1
#define ADC_A           P0_0
#define ADC_B           P0_1
#define ADC_C           P0_2
#define ADC_START       P0_3
#define ADC_OE          P0_4
#define ADC_EOC         P0_5

#define PIR_PIN         P3_2
#define PIR_ACTIVE()    (PIR_PIN == 0)      /* inverted by the NPN stage */

#define I2C_SCL         P3_4
#define I2C_SDA         P3_5

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

/* ---------------- ADC0808 channels ---------------- */
#define N_PIEZO         3       /* IN0 = zone L, IN1 = zone C, IN2 = zone R */
#define CH_SOUND        3
#define CH_LDR          4

/* ---------------- Timing (ms) ---------------- */
#define WINDOW_MS       2000    /* classify period */
#define HIT_DEADTIME_MS 150     /* one footstep rings for a while; count it once */
#define BURST_GAP_MS    100     /* same idea for sound bursts */
#define ALARM_MS        3000

/* ---------------- Piezo thresholds (ADC counts above baseline, 8-bit) ---------------- */
#define PIEZO_THR_DAY   25      /* a "hit" starts above this */
#define PIEZO_THR_NIGHT 15      /* night mode: more sensitive */
#define MED_PEAK        40      /* person-sized peaks */
#define STRONG_PEAK     100     /* shovel / heavy impact */

/* ---------------- Other sensors ---------------- */
#define SOUND_THR       40      /* mic deviation that counts as a burst */
#define DIG_BURSTS      3       /* bursts per window that mean digging */
#define MAG_THR         300     /* |dX|+|dY|+|dZ| in raw counts (8 G range) */
#define LDR_DARK        60      /* below this = night (LDR to 5 V, 10k to GND) */
#define LDR_LIGHT       80      /* above this = day (hysteresis) */

/* ---------------- Events ---------------- */
#define EV_NONE         0       /* no piezo hit this window */
#define EV_NOISE        1       /* wind / rain: hit but no PIR, no metal -> ignore */
#define EV_ANIMAL       2       /* log only */
#define EV_PERSON       3       /* alarm */
#define EV_VEHICLE      4       /* alarm */
#define EV_DIGGING      5       /* alarm */

#define ZONE_NONE       0xFF

#endif
