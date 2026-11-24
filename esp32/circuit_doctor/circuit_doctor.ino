/*
 * GroundWatch circuit doctor (ESP32)
 *
 * Walks through the board one test point at a time with a single probe wire
 * and says what works and what doesn't, in dependency order (power -> VREF ->
 * piezo amp -> ADC supply -> ADC clock -> 8051 -> ADC control -> 8051 serial
 * -> PIR -> LDR). The first FAIL in the list is usually the real problem.
 *
 * WIRING (one resistor):
 *
 *   probe wire ── 10 kΩ ──── GPIO32   (voltage, frequency, activity)
 *                         └── GPIO27   (digital: frequency, activity, UART)
 *
 *   8051 P3.1 ── 10k ──┬── GPIO16    (serial log; 10k+10k from GPIO16 to GND)
 *
 *   ESP32 GND ──────────── breadboard GND        (must be joined)
 *
 *   The 10 kΩ is mandatory: it limits current when the probe touches 5 V.
 *   Readings above ~3.1 V show as "HIGH (5 V level)". Use a multimeter if
 *   you need the exact rail voltage.
 *
 * USE: Serial Monitor at 115200, line ending "Newline".
 *   g  measure this step        s  skip       b  back
 *   5  jump to step 5           l  summary    x  clear results
 * Results are kept in flash, so they survive a reset or unplugging.
 */

#include <Preferences.h>
#include "driver/pulse_cnt.h"

#define PIN_A   32      /* analog (same pin as PIN_D) */
#define PIN_D   32      /* digital: frequency, activity */
#define PIN_RX  16      /* 8051 P3.1 (TXD) through a 10k / 20k divider */
#define SAT_MV  3050    /* above this the pin is clamped: node is at 5 V level */

enum Kind { K_LOW, K_HIGH, K_DC, K_TAP, K_CLK, K_ACT, K_UART, K_PIR, K_LDR };

struct Step {
  const char *name;
  const char *where;
  Kind kind;
  float lo, hi;           /* volts for DC/TAP, Hz for CLK/ACT */
  const char *hintLow;    /* below range / no signal */
  const char *hintHigh;   /* above range / stuck high */
};

const Step S[] = {
 {"Probe + ground", "breadboard GND rail", K_LOW, 0, 0,
  "", "Reads a voltage on GND: ESP32 GND is not joined to breadboard GND, or the probe isn't on GND."},
 {"+5 V rail", "breadboard + rail", K_HIGH, 0, 0,
  "No 5 V on the breadboard: check the board's 5 V header wire and that both halves of the rail are joined.", ""},
 {"VREF 2.5 V", "VREF node (between the two 10k, 10uF + leg)", K_DC, 2.30, 2.70,
  "VREF low: top 10k not reaching +5 V, or 10uF in backwards / shorted.", "VREF high: bottom 10k not reaching GND."},
 {"LM358 #1 supply", "LM358 #1 pin 8", K_HIGH, 0, 0,
  "LM358 #1 has no supply: pin 8 must go to +5 V.", ""},
 {"LM358 #1 ground", "LM358 #1 pin 4", K_LOW, 0, 0,
  "", "LM358 #1 pin 4 is not grounded."},
 {"Piezo L node X", "LM358 #1 pin 3 (IN+)", K_DC, 2.30, 2.70,
  "Node X low: 1M to VREF missing, lower 1N4148 reversed (band must face node X), or piezo red shorted to GND.",
  "Node X high: upper 1N4148 reversed (band must face +5 V) or node X touching 5 V."},
 {"Amp L IN-", "LM358 #1 pin 2 (IN-)", K_DC, 2.30, 2.70,
  "IN- low: 1k from pin 2 must go to VREF (not GND); check the 10k feedback to pin 1.",
  "IN- high: 1k/10k on the wrong pins, or output stuck high (see next step)."},
 {"Amp L output", "LM358 #1 pin 1 (OUT) - tap LEFT piezo when asked", K_TAP, 2.20, 2.80,
  "OUT stuck low: VREF missing at the 1k, or IN+ pulled low.",
  "OUT stuck high (~3.5 V): IN+ high, 10k feedback missing, or a diode reversed."},
 {"ADC IN0", "ADC0809 pin 26 (IN0) - tap LEFT piezo when asked", K_TAP, 2.20, 2.80,
  "Pin 26 doesn't match the amp output: wire from LM358 #1 pin 1 to ADC pin 26 missing or in the wrong row.",
  "Pin 26 doesn't match the amp output: wire from LM358 #1 pin 1 to ADC pin 26 missing or in the wrong row."},
 {"ADC VCC", "ADC0809 pin 11", K_HIGH, 0, 0, "ADC0809 pin 11 has no +5 V.", ""},
 {"ADC VREF+", "ADC0809 pin 12", K_HIGH, 0, 0, "ADC0809 pin 12 (VREF+) must be +5 V.", ""},
 {"ADC GND", "ADC0809 pin 13", K_LOW, 0, 0, "", "ADC0809 pin 13 not grounded."},
 {"ADC VREF-", "ADC0809 pin 16", K_LOW, 0, 0, "", "ADC0809 pin 16 (VREF-) must be grounded."},
 {"8051 ALE", "8051 ALE (DIP pin 30) or 74LS74 pin 3", K_CLK, 1.70e6, 2.10e6,
  "No ALE: the 8051 isn't running. Check crystal, EA (pin 31) at +5 V, RST (pin 9) not stuck high, and that the chip is flashed.",
  "ALE faster than expected: crystal is not 11.0592 MHz (serial baud will be wrong)."},
 {"ADC clock", "ADC0809 pin 10 (CLOCK) = 74LS74 pin 5", K_CLK, 0.60e6, 1.28e6,
  "No ADC clock: 74LS74 power (14/7), pin 3 from ALE, pin 2 joined to pin 6, pins 1 and 4 to +5 V, pin 5 to ADC pin 10.",
  "Clock above 1.28 MHz: ALE is going straight to the ADC (divider bypassed). The ADC0809 is out of spec there."},
 {"ADC START/ALE", "ADC0809 pin 6 (START)", K_ACT, 3000, 5200,
  "START never pulses: firmware not running / stuck at start-up, or P0.3 not wired to pins 6+22. Stuck LOW = P0.3 10k pull-up missing (Port 0 is open-drain).",
  "START pulses too fast: wrong wire (is this on ALE of the 8051?)"},
 {"ADC OE", "ADC0809 pin 9 (OE)", K_ACT, 3000, 5200,
  "OE never pulses: P0.4 not wired to pin 9, or P0.4 10k pull-up missing.", "OE too fast: wrong wire."},
 {"ADC EOC", "ADC0809 pin 7 (EOC)", K_ACT, 3000, 5200,
  "EOC doesn't toggle: the ADC isn't converting. Fix the clock / START steps first; also check EOC to P0.5.", "EOC too fast: wrong wire."},
 {"ADC address A", "ADC0809 pin 25 (ADD A)", K_ACT, 600, 1500,
  "ADD A never changes: P0.0 not wired to pin 25 or its 10k pull-up missing.", "ADD A too fast: wrong wire."},
 {"8051 serial", "nowhere - it listens on GPIO16 (wired to P3.1)", K_UART, 0, 0,
  "8051 TX is silent.", ""},
 {"PIR", "PIR OUT pin (= 8051 P3.2) - wave a hand when asked", K_PIR, 0, 0,
  "PIR never went high: wait 60 s after power-up, set jumper to H, check VCC/GND.", "PIR stuck high: turn the time pot fully anticlockwise, wait for warm-up."},
 {"LDR", "ADC0809 pin 2 (IN4) - cover the LDR when asked", K_LDR, 0, 0,
  "LDR junction doesn't react: check LDR to +5 V and 10k to GND at pin 2.", ""},
};
const int NSTEP = sizeof(S) / sizeof(S[0]);

enum Res : uint8_t { R_NONE = 0, R_PASS, R_FAIL, R_WARN, R_SKIP };
const char *RES_TXT[] = {"  -  ", "PASS ", "FAIL ", "WARN ", "skip "};

Preferences prefs;
int cur = 0;
char msg[200];

/* ---------------- measurement helpers ---------------- */

struct Dc { float mean, mn, mx; };

Dc measureDc(uint32_t ms) {
  Dc d = {0, 99, -1};
  uint32_t t0 = millis(), n = 0; double sum = 0;
  while (millis() - t0 < ms) {
    float v = analogReadMilliVolts(PIN_A) / 1000.0f;
    sum += v; n++;
    if (v < d.mn) d.mn = v;
    if (v > d.mx) d.mx = v;
    delayMicroseconds(200);
  }
  d.mean = sum / n;
  return d;
}

String vtxt(float v) {
  if (v * 1000 > SAT_MV) return "HIGH (5 V level)";
  return String(v, 2) + " V";
}

/* rising edges per second on PIN_D, gated 20 x 10 ms */
float measureHz() {
  pinMode(PIN_D, INPUT);
  pcnt_unit_config_t uc = {};
  uc.low_limit = -10; uc.high_limit = 32767;
  pcnt_unit_handle_t u = NULL;
  if (pcnt_new_unit(&uc, &u) != ESP_OK) return -1;
  pcnt_chan_config_t cc = {};
  cc.edge_gpio_num = PIN_D; cc.level_gpio_num = -1;
  pcnt_channel_handle_t ch = NULL;
  pcnt_new_channel(u, &cc, &ch);
  pcnt_channel_set_edge_action(ch, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
  pcnt_unit_enable(u);
  long total = 0; int64_t us = 0;
  for (int i = 0; i < 20; i++) {
    int c = 0;
    pcnt_unit_clear_count(u);
    int64_t t0 = esp_timer_get_time();
    pcnt_unit_start(u);
    delayMicroseconds(10000);
    pcnt_unit_stop(u);
    us += esp_timer_get_time() - t0;
    pcnt_unit_get_count(u, &c);
    total += c;
  }
  pcnt_unit_disable(u);
  pcnt_del_channel(ch);
  pcnt_del_unit(u);
  return total * 1e6f / us;
}

float highFraction() {
  pinMode(PIN_D, INPUT);
  uint32_t hi = 0;
  for (int i = 0; i < 20000; i++) hi += digitalRead(PIN_D);
  return hi / 20000.0f;
}

String hztxt(float hz) {
  if (hz >= 1e6) return String(hz / 1e6, 3) + " MHz";
  if (hz >= 1e3) return String(hz / 1e3, 2) + " kHz";
  return String(hz, 0) + " Hz";
}

void countdown(const char *what, int sec) {
  Serial.printf("   >>> %s in ", what);
  for (int i = sec; i > 0; i--) { Serial.printf("%d.. ", i); delay(1000); }
  Serial.println("NOW");
}

/* ---------------- 8051 serial check ---------------- */

int splitCsv(char *line, char **f, int maxf) {
  int n = 0;
  f[n++] = line;
  for (char *p = line; *p && n < maxf; p++) if (*p == ',') { *p = 0; f[n++] = p + 1; }
  return n;
}

Res checkUart() {
  Serial2.setRxBufferSize(8192);

  /* ---- normal mode: 9600 baud text ---- */
  Serial2.begin(9600, SERIAL_8N1, PIN_RX, -1);
  Serial.println("   listening at 9600 baud. Keep everything STILL for 5 s...");
  char line[160]; int len = 0; uint32_t bytes = 0, printable = 0;
  int evStill = 0, evTap = 0; int peakStill[3] = {0}, peakTap[3] = {0}, hitsTap[3] = {0};
  int ldr = -1, pir = -1; bool gwReady = false, tapping = false;
  uint32_t t0 = millis();
  while (millis() - t0 < 12000) {
    if (!tapping && millis() - t0 > 5000) {
      tapping = true;
      Serial.println("   >>> NOW tap the LEFT piezo / square firmly, about twice a second, for 7 s");
    }
    while (Serial2.available()) {
      int c = Serial2.read(); bytes++;
      if ((c >= 32 && c < 127) || c == '\r' || c == '\n') printable++;
      if (c == '\n' || len >= (int)sizeof(line) - 1) {
        line[len] = 0; len = 0;
        if (strncmp(line, "GW,", 3) == 0) { gwReady = true; Serial.printf("   8051: %s\n", line); }
        if (strncmp(line, "EV,", 3) == 0) {
          Serial.printf("   8051: %s\n", line);
          char *f[32]; int n = splitCsv(line, f, 32);
          if (n >= 27) {
            for (int i = 0; i < 3; i++) {
              int pk = atoi(f[3 + i]), ht = atoi(f[6 + i]);
              if (tapping) { peakTap[i] = max(peakTap[i], pk); hitsTap[i] += ht; }
              else peakStill[i] = max(peakStill[i], pk);
            }
            pir = atoi(f[24]); ldr = atoi(f[26]);
            if (tapping) evTap++; else evStill++;
          }
        }
      } else if (c != '\r') line[len++] = (char)c;
    }
    delay(2);
  }
  Serial2.end();

  int ev = evStill + evTap;
  if (ev > 0 || gwReady) {
    Serial.println("   8051 is running and its log is readable at 9600 -> crystal and UART are OK.");
    if (ev == 0) { snprintf(msg, sizeof msg, "8051 alive (GW lines) but no EV lines in 12 s."); return R_WARN; }
    Serial.printf("   still: peak L/C/R = %d/%d/%d    tapping: peak L/C/R = %d/%d/%d, hits L = %d   LDR=%d PIR=%d\n",
                  peakStill[0], peakStill[1], peakStill[2], peakTap[0], peakTap[1], peakTap[2], hitsTap[0], ldr, pir);
    if (ldr == 0 || ldr == 255)
      Serial.println("   note: LDR reads 0 or 255 - suspicious (stuck ADC data, or LDR divider open).");
    if (peakStill[0] > 40 || peakStill[1] > 40 || peakStill[2] > 40)
      Serial.println("   note: large peaks with nothing moving -> a noisy / floating channel.");
    if (evTap > 0 && (peakTap[0] >= 8 || hitsTap[0] > 0)) {
      snprintf(msg, sizeof msg, "8051 sees LEFT taps (peak %d, %d hits). ADC path works.", peakTap[0], hitsTap[0]);
      return R_PASS;
    }
    snprintf(msg, sizeof msg,
      "8051 runs but sees no LEFT taps (peak %d). If 'Amp L output' and 'ADC IN0' passed: suspect ADC data lines P1 / OE / address. Put P3.3 to GND, reset, rerun this step for raw values.",
      peakTap[0]);
    return R_FAIL;
  }

  /* ---- raw mode: 57600 binary L,C,R,0xFF ---- */
  Serial2.begin(57600, SERIAL_8N1, PIN_RX, -1);
  static uint8_t buf[8192];
  int nb = 0; t0 = millis();
  Serial.println("   no text at 9600, trying raw mode (57600)... tap the LEFT piezo now");
  while (millis() - t0 < 1500 && nb < (int)sizeof buf) {
    while (Serial2.available() && nb < (int)sizeof buf) buf[nb++] = Serial2.read();
  }
  Serial2.end();
  int off = -1;
  for (int o = 0; o < 4 && nb > 400; o++) {
    int ok = 0, tot = 0;
    for (int i = o; i < nb; i += 4) { tot++; if (buf[i] == 0xFF) ok++; }
    if (ok > tot * 9 / 10) { off = o; break; }
  }
  if (off >= 0) {
    int mn[3] = {255, 255, 255}, mx[3] = {0}, n = 0; long sum[3] = {0};
    uint8_t orBits[3] = {0}, andBits[3] = {0xFF, 0xFF, 0xFF};
    for (int i = off + 1; i + 2 < nb; i += 4) {
      for (int k = 0; k < 3; k++) {
        uint8_t v = buf[i + k];
        mn[k] = min(mn[k], (int)v); mx[k] = max(mx[k], (int)v); sum[k] += v;
        orBits[k] |= v; andBits[k] &= v;
      }
      n++;
    }
    const char nm[3] = {'L', 'C', 'R'};
    bool bad = false;
    Serial.printf("   raw mode OK, %d samples per channel (expect ~128 at rest):\n", n);
    for (int k = 0; k < 3; k++) {
      int mean = sum[k] / max(n, 1);
      uint8_t toggling = orBits[k] & ~andBits[k];
      Serial.printf("   %c: mean %3d  min %3d  max %3d  bits that changed: ", nm[k], mean, mn[k], mx[k]);
      for (int b = 7; b >= 0; b--) Serial.print((toggling >> b) & 1 ? '1' : '.');
      Serial.println();
      if (mean >= 250) { Serial.printf("      %c reads 255: ADC isn't driving the bus (OE, EOC, clock).\n", nm[k]); bad = true; }
      else if (mean <= 5) { Serial.printf("      %c reads 0: input at 0 V or data lines shorted to GND.\n", nm[k]); bad = true; }
      else if (mean < 100 || mean > 156) { Serial.printf("      %c is far from 128: input not at 2.5 V, or a stuck data bit.\n", nm[k]); bad = true; }
    }
    if (bad) { snprintf(msg, sizeof msg, "Raw ADC values wrong (see above)."); return R_FAIL; }
    if (mx[0] - mn[0] < 15) { snprintf(msg, sizeof msg, "Raw values OK at rest but LEFT taps barely change IN0 (%d..%d).", mn[0], mx[0]); return R_WARN; }
    snprintf(msg, sizeof msg, "Raw mode: ADC values sane, LEFT swings %d..%d. ADC path works.", mn[0], mx[0]);
    return R_PASS;
  }

  pinMode(PIN_RX, INPUT);
  uint32_t hi = 0; for (int i = 0; i < 20000; i++) hi += digitalRead(PIN_RX);
  float hf = hi / 20000.0f;
  if (bytes == 0 && nb == 0) {
    snprintf(msg, sizeof msg, "TX silent (GPIO16 %s). Firmware not running / not flashed, or the P3.1 -> GPIO16 wire / divider is wrong.",
             hf > 0.9 ? "idle high" : hf < 0.1 ? "stuck LOW" : "toggling?");
    return R_FAIL;
  }
  snprintf(msg, sizeof msg,
    "Bytes arrive but are garbage at 9600 and 57600 (%lu bytes, %lu printable). Baud mismatch -> crystal not 11.0592 MHz? Check the ALE step.",
    (unsigned long)bytes, (unsigned long)printable);
  return R_FAIL;
}

/* ---------------- one step ---------------- */

Res runStep(const Step &s) {
  msg[0] = 0;
  switch (s.kind) {
  case K_LOW: case K_HIGH: case K_DC: {
    Dc d = measureDc(800);
    Serial.printf("   reads %s  (noise %.0f mV p-p)\n", vtxt(d.mean).c_str(), (d.mx - d.mn) * 1000);
    float mv = d.mean * 1000;
    if (s.kind == K_LOW)  { if (mv < 150) return R_PASS; snprintf(msg, sizeof msg, "%s", s.hintHigh); return R_FAIL; }
    if (s.kind == K_HIGH) { if (mv > SAT_MV) return R_PASS;
                            snprintf(msg, sizeof msg, "Reads %s, expected 5 V. %s", vtxt(d.mean).c_str(), s.hintLow); return R_FAIL; }
    if (d.mean < s.lo) { snprintf(msg, sizeof msg, "Reads %s, expected %.1f-%.1f V. %s", vtxt(d.mean).c_str(), s.lo, s.hi, s.hintLow); return R_FAIL; }
    if (d.mean > s.hi) { snprintf(msg, sizeof msg, "Reads %s, expected %.1f-%.1f V. %s", vtxt(d.mean).c_str(), s.lo, s.hi, s.hintHigh); return R_FAIL; }
    if ((d.mx - d.mn) > 0.3) { snprintf(msg, sizeof msg, "Level OK but noisy (%.0f mV p-p): loose wire or floating node?", (d.mx - d.mn) * 1000); return R_WARN; }
    return R_PASS;
  }
  case K_TAP: {
    Dc rest = measureDc(1000);
    Serial.printf("   at rest: %s\n", vtxt(rest.mean).c_str());
    if (rest.mean < s.lo) { snprintf(msg, sizeof msg, "Rest %s, expected ~2.5 V. %s", vtxt(rest.mean).c_str(), s.hintLow); return R_FAIL; }
    if (rest.mean > s.hi) { snprintf(msg, sizeof msg, "Rest %s, expected ~2.5 V. %s", vtxt(rest.mean).c_str(), s.hintHigh); return R_FAIL; }
    countdown("Tap the LEFT piezo firmly a few times", 2);
    Dc tap = measureDc(4000);
    float swing = max(tap.mx - rest.mean, rest.mean - tap.mn);
    Serial.printf("   while tapping: %.2f .. %.2f V  (swing %.0f mV)\n", tap.mn, tap.mx, swing * 1000);
    if (swing >= 0.15f) return R_PASS;
    snprintf(msg, sizeof msg,
      "Rest level OK but taps barely move it (%.0f mV). Piezo not reaching node X: red lead/10k to node X, black lead to VREF, or gain too low.", swing * 1000);
    return R_FAIL;
  }
  case K_CLK: case K_ACT: {
    float hz = measureHz(), hf = highFraction();
    Dc d = measureDc(200);
    Serial.printf("   frequency %s, high %.0f%% of the time, average %s\n", hztxt(hz).c_str(), hf * 100, vtxt(d.mean).c_str());
    if (s.kind == K_CLK && hz > 1e6 && hz < 2.2e6 && s.lo > 1e6)
      Serial.printf("   -> crystal = %.4f MHz (ALE x 6)\n", hz * 6 / 1e6);
    if (hz >= s.lo && hz <= s.hi) return R_PASS;
    if (hz < s.lo) {
      const char *lvl = hf > 0.95 ? "stuck HIGH" : hf < 0.05 ? "stuck LOW" : "slow";
      snprintf(msg, sizeof msg, "%s, expected %s-%s (line %s). %s", hztxt(hz).c_str(), hztxt(s.lo).c_str(), hztxt(s.hi).c_str(), lvl, s.hintLow);
    } else
      snprintf(msg, sizeof msg, "%s, expected %s-%s. %s", hztxt(hz).c_str(), hztxt(s.lo).c_str(), hztxt(s.hi).c_str(), s.hintHigh);
    return R_FAIL;
  }
  case K_UART:
    return checkUart();
  case K_PIR: {
    pinMode(PIN_D, INPUT);
    Serial.println("   wave a hand in front of the PIR for the next 8 s...");
    uint32_t t0 = millis(), hi = 0, n = 0; bool sawLow = false, sawHigh = false;
    while (millis() - t0 < 8000) { int v = digitalRead(PIN_D); hi += v; n++; if (v) sawHigh = true; else sawLow = true; delay(5); }
    Serial.printf("   high %.0f%% of the time\n", 100.0 * hi / n);
    if (sawHigh && sawLow) return R_PASS;
    if (sawHigh) { snprintf(msg, sizeof msg, "%s", s.hintHigh); return R_WARN; }
    snprintf(msg, sizeof msg, "%s", s.hintLow); return R_FAIL;
  }
  case K_LDR: {
    Dc open = measureDc(1000);
    Serial.printf("   uncovered: %s\n", vtxt(open.mean).c_str());
    countdown("Cover the LDR with your finger", 2);
    Dc cov = measureDc(2000);
    Serial.printf("   covered:   %s\n", vtxt(cov.mean).c_str());
    if (open.mean - cov.mean > 0.2f) return R_PASS;
    snprintf(msg, sizeof msg, "%s (%.2f V -> %.2f V)", s.hintLow, open.mean, cov.mean);
    return R_FAIL;
  }
  }
  return R_NONE;
}

/* ---------------- results in flash ---------------- */

void saveRes(int i, Res r, const char *m) {
  char k[6];
  snprintf(k, sizeof k, "r%d", i); prefs.putUChar(k, r);
  snprintf(k, sizeof k, "m%d", i); prefs.putString(k, m);
}
Res getRes(int i) { char k[6]; snprintf(k, sizeof k, "r%d", i); return (Res)prefs.getUChar(k, R_NONE); }
String getMsg(int i) { char k[6]; snprintf(k, sizeof k, "m%d", i); return prefs.getString(k, ""); }

void summary() {
  Serial.println("\n================ SUMMARY ================");
  int firstFail = -1;
  for (int i = 0; i < NSTEP; i++) {
    Res r = getRes(i);
    Serial.printf("%2d %s %-16s %s\n", i + 1, RES_TXT[r], S[i].name, getMsg(i).c_str());
    if (r == R_FAIL && firstFail < 0) firstFail = i;
  }
  if (firstFail >= 0)
    Serial.printf("\n>> Fix step %d (%s) first: later failures often follow from it.\n", firstFail + 1, S[firstFail].name);
  else
    Serial.println("\n>> No failures recorded.");
  Serial.println("=========================================");
}

void prompt() {
  if (cur >= NSTEP) { summary(); Serial.println("All steps done. Type a step number to redo one, x to clear."); return; }
  Serial.printf("\n--- Step %d/%d: %s ---\n   Put the probe on: %s\n   then type g + Enter  (s skip, b back, l summary)\n",
                cur + 1, NSTEP, S[cur].name, S[cur].where);
}

void setup() {
  Serial.begin(115200);
  delay(800);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_A, ADC_11db);
  pinMode(PIN_D, INPUT);
  prefs.begin("gwdoc", false);
  Serial.println("\n##### GroundWatch circuit doctor #####");
  Serial.println("Probe: wire -> 10k -> GPIO32.  ESP32 GND -> breadboard GND.  Board powered on.");
  bool any = false;
  for (int i = 0; i < NSTEP; i++) if (getRes(i) != R_NONE) any = true;
  if (any) { Serial.println("(results from an earlier run are kept - x clears them)"); summary(); }
  prompt();
}

void loop() {
  if (!Serial.available()) return;
  String in = Serial.readStringUntil('\n');
  in.trim(); in.toLowerCase();
  if (in.length() == 0 || in == "g") {
    if (cur >= NSTEP) { prompt(); return; }
    Serial.println("   measuring...");
    Res r = runStep(S[cur]);
    if (r == R_PASS && !msg[0]) snprintf(msg, sizeof msg, "ok");
    Serial.printf("   => %s %s\n", RES_TXT[r], msg);
    saveRes(cur, r, msg);
    cur++;
  } else if (in == "s") { saveRes(cur, R_SKIP, ""); cur++; }
  else if (in == "b") { if (cur > 0) cur--; }
  else if (in == "l") { summary(); }
  else if (in == "u") {          /* dump what arrives on GPIO16 at both baud rates */
    const long bauds[] = {9600, 57600};
    for (long bd : bauds) {
      Serial2.setRxBufferSize(4096);
      Serial2.begin(bd, SERIAL_8N1, PIN_RX, -1);
      delay(20); while (Serial2.available()) Serial2.read();
      uint32_t t0 = millis(); int n = 0; uint8_t b[96];
      while (millis() - t0 < 1500) while (Serial2.available()) { int c = Serial2.read(); if (n < 96) b[n] = c; n++; }
      Serial2.end();
      Serial.printf("DUMP %ld baud: %d bytes in 1.5 s:", bd, n);
      for (int i = 0; i < min(n, 96); i++) Serial.printf(" %02X", b[i]);
      Serial.println();
    }
    pinMode(PIN_RX, INPUT);
    uint32_t hi = 0, ed = 0; int last = digitalRead(PIN_RX);
    for (int i = 0; i < 50000; i++) { int v = digitalRead(PIN_RX); hi += v; if (v != last) ed++; last = v; }
    Serial.printf("DUMP GPIO16 high %lu%%, %lu edges in 50000 samples\n", hi * 100 / 50000, ed);
    return;
  }
  else if (in == "a") {          /* read every ADC1 pin: find where the probe really is */
    const int ains[] = {32, 33, 34, 35, 36, 39};
    for (int a : ains) {
      analogSetPinAttenuation(a, ADC_11db);
      int mn = 9999, mx = 0; long sum = 0;
      for (int i = 0; i < 300; i++) { int v = analogReadMilliVolts(a); sum += v; mn = min(mn, v); mx = max(mx, v); delayMicroseconds(300); }
      Serial.printf("ALLPINS: GPIO%d  %4ld mV  (p-p %4d)\n", a, sum / 300, mx - mn);
    }
    return;
  }
  else if (in == "y") {          /* find which pins are joined: drive each output pin, read each ADC pin */
    const int outs[] = {25, 26, 27, 14, 12, 13, 32, 33, 4, 5, 18, 19, 21, 22, 23};
    const int ains[] = {32, 33, 34, 35, 36, 39};
    for (int o : outs) {
      pinMode(o, OUTPUT);
      for (int a : ains) {
        if (a == o) continue;
        analogSetPinAttenuation(a, ADC_11db);
        digitalWrite(o, HIGH); delay(5); int h = analogReadMilliVolts(a); delay(2); h = (h + analogReadMilliVolts(a)) / 2;
        digitalWrite(o, LOW);  delay(5); int l = analogReadMilliVolts(a); delay(2); l = (l + analogReadMilliVolts(a)) / 2;
        if (h > 2800 && l < 300) Serial.printf("SCAN: GPIO%d is joined to GPIO%d\n", o, a);
      }
      pinMode(o, INPUT);
    }
    for (int a : ains) Serial.printf("SCAN: GPIO%d alone reads %d mV\n", a, analogReadMilliVolts(a));
    Serial.println("SCAN done");
    return;
  }
  else if (in == "z") {          /* self-test: GPIO27 drives, GPIO34 listens (they share a row) */
    pinMode(PIN_D, OUTPUT);
    digitalWrite(PIN_D, HIGH); delay(50); Dc h = measureDc(300);
    digitalWrite(PIN_D, LOW);  delay(50); Dc l = measureDc(300);
    pinMode(PIN_D, INPUT);
    Serial.printf("SELFTEST 27=HIGH -> 34 reads %.2f V (p-p %.0f mV);  27=LOW -> 34 reads %.2f V (p-p %.0f mV)\n",
                  h.mean, (h.mx - h.mn) * 1000, l.mean, (l.mx - l.mn) * 1000);
    Serial.println(h.mean > 2.8 && l.mean < 0.3 ? "SELFTEST: GPIO27 and GPIO34 ARE connected" :
                                                  "SELFTEST: GPIO27 and GPIO34 are NOT connected");
    return;
  }
  else if (in == "x") { prefs.clear(); cur = 0; Serial.println("results cleared"); }
  else if (in.toInt() >= 1 && in.toInt() <= NSTEP) { cur = in.toInt() - 1; }
  else { Serial.println("?  g=measure s=skip b=back l=summary x=clear <n>=go to step"); return; }
  prompt();
}
