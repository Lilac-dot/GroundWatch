/*
 * GroundWatch piezo tester (ESP32, standalone - no 8051 needed)
 *
 * Tells you, per channel, whether a piezo (or a piezo + LM358 stage) is
 * alive: resting level, resting noise, and every tap with its size in mV.
 *
 * Channels:  L = GPIO34   C = GPIO35   R = GPIO32   (all ADC1, input only)
 * Unused channels can be left open; they will just show as FLOATING.
 *
 * ---------------------------------------------------------------------------
 * TEST A - bare piezo disc (unplug it from the amplifier first)
 *
 *   3V3 ── 10k ──┬── 10k ── GND          <- "MID" node, about 1.65 V,
 *                │                          shared by all channels
 *               MID
 *                │
 *   piezo black ─┤
 *                │
 *               1M
 *                │
 *   piezo red ── 10k ──┴──────────► GPIO34 (or 35 / 32)
 *
 *   i.e. 1 MΩ between the GPIO and MID, piezo across it through 10 kΩ.
 *   Expected: REST about 1650 mV, taps of 100 mV to 1000+ mV.
 *   Tap the disc gently with a fingertip; don't whack it (no clamp diodes).
 *
 * TEST B - piezo + LM358 stage, in circuit (amp OUT pin, 5 V powered)
 *
 *   LM358 OUT ── 10k ──┬──► GPIO34 (or 35 / 32)
 *                      10k
 *                       │
 *   breadboard GND ─────┴──── ESP32 GND   (grounds MUST be joined)
 *
 *   The divider halves the signal so 5 V-side voltages stay under 3.3 V.
 *   Expected: REST about 1250 mV (= 2.5 V at OUT). Readings are x0.5, so
 *   a 100 mV tap here is a 200 mV swing at the op-amp output.
 *
 * NEVER put the LM358 output (or a bare piezo without the 10k) straight
 * onto an ESP32 pin.
 * ---------------------------------------------------------------------------
 *
 * Serial Monitor at 115200. Commands:
 *   t  text mode (default): status every second + a line per tap
 *   p  plot mode: deviation in mV for the Serial Plotter
 *   c  re-calibrate the resting level (keep everything still for 1 s)
 *   r  reset tap counters
 */

#define NCH            3
#define SAMPLE_US      500      /* 2 kHz per channel */
#define HIT_MV         60       /* deviation that counts as a tap */
#define HOLDOFF_MS     250      /* one tap = one count */
#define STATUS_MS      1000
#define PLOT_MS        20

const int  PIN[NCH]  = {34, 35, 32};
const char NAME[NCH] = {'L', 'C', 'R'};

float    base[NCH];             /* resting level, mV */
int      winMin[NCH], winMax[NCH];      /* status window */
int      plotPeak[NCH];                 /* plot window, signed */
int      hitPeak[NCH];
uint32_t hitStart[NCH], lastHit[NCH];
bool     inHit[NCH];
uint32_t taps[NCH];
bool     plotMode = false;
uint32_t tSample, tStatus, tPlot;

void calibrate() {
  Serial.println(F("# calibrating - keep the piezos still..."));
  for (int c = 0; c < NCH; c++) {
    uint32_t sum = 0;
    for (int i = 0; i < 400; i++) { sum += analogReadMilliVolts(PIN[c]); delayMicroseconds(1500); }
    base[c] = sum / 400.0f;
    inHit[c] = false;
  }
  resetWindow();
  Serial.printf("# rest: L=%d mV  C=%d mV  R=%d mV\n", (int)base[0], (int)base[1], (int)base[2]);
}

void resetWindow() {
  for (int c = 0; c < NCH; c++) { winMin[c] = 99999; winMax[c] = -1; }
}

const char *verdict(int c) {
  int pp = winMax[c] - winMin[c];
  int b  = (int)base[c];
  if (b < 80)    return "STUCK LOW (0 V: open/short to GND, no bias)";
  if (b > 3150)  return "STUCK HIGH (rail: bias/divider wrong)";
  if (pp > 400 && !inHit[c] && millis() - lastHit[c] > 1500)
                 return "FLOATING/NOISY (1M or wire missing?)";
  return "ok";
}

void setup() {
  Serial.begin(115200);
  delay(500);
  analogReadResolution(12);
  for (int c = 0; c < NCH; c++) analogSetPinAttenuation(PIN[c], ADC_11db);
  Serial.println(F("\n# GroundWatch piezo tester  (t=text p=plot c=calibrate r=reset)"));
  calibrate();
  tSample = micros(); tStatus = tPlot = millis();
}

void loop() {
  if (Serial.available()) {
    char k = Serial.read();
    if (k == 'p') plotMode = true;
    if (k == 't') plotMode = false;
    if (k == 'c') calibrate();
    if (k == 'r') { for (int c = 0; c < NCH; c++) taps[c] = 0; Serial.println(F("# counters reset")); }
  }

  if ((uint32_t)(micros() - tSample) < SAMPLE_US) return;
  tSample += SAMPLE_US;
  uint32_t now = millis();

  for (int c = 0; c < NCH; c++) {
    int mv  = analogReadMilliVolts(PIN[c]);
    int dev = mv - (int)base[c];
    int ad  = abs(dev);

    if (mv < winMin[c]) winMin[c] = mv;
    if (mv > winMax[c]) winMax[c] = mv;
    if (ad > abs(plotPeak[c])) plotPeak[c] = dev;

    if (ad > HIT_MV) {
      if (!inHit[c] && now - lastHit[c] > HOLDOFF_MS) {
        inHit[c] = true; hitStart[c] = now; hitPeak[c] = 0;
      }
      if (inHit[c] && ad > abs(hitPeak[c])) hitPeak[c] = dev;
    } else if (inHit[c] && now - hitStart[c] > 30) {
      inHit[c] = false; lastHit[c] = now; taps[c]++;
      if (!plotMode)
        Serial.printf("TAP %c  peak %+5d mV  (%lu ms)  total %lu\n",
                      NAME[c], hitPeak[c], (unsigned long)(now - hitStart[c]), (unsigned long)taps[c]);
    } else if (!inHit[c]) {
      base[c] += (mv - base[c]) * 0.0005f;   /* slow drift tracking at rest only */
    }
  }

  if (plotMode && now - tPlot >= PLOT_MS) {
    tPlot = now;
    Serial.printf("L:%d,C:%d,R:%d,hit+:%d,hit-:%d\n",
                  plotPeak[0], plotPeak[1], plotPeak[2], HIT_MV, -HIT_MV);
    for (int c = 0; c < NCH; c++) plotPeak[c] = 0;
  }

  if (!plotMode && now - tStatus >= STATUS_MS) {
    tStatus = now;
    for (int c = 0; c < NCH; c++)
      Serial.printf("%c rest %4d mV  p-p %4d mV  taps %3lu  %s\n", NAME[c], (int)base[c],
                    winMax[c] - winMin[c], (unsigned long)taps[c], verdict(c));
    Serial.println(F("--"));
    resetWindow();
  } else if (plotMode && now - tStatus >= STATUS_MS) {
    tStatus = now; resetWindow();
  }
}
