/*
 * GroundWatch ESP32 bridge + diagnostics
 *
 * Reads the 8051's 9600-baud log on GPIO16 (RX2), passes every line on to the
 * laptop over USB at 115200, and explains each 2 s window in plain words:
 * what the piezos measured, and which rule stopped it from becoming an alarm.
 *
 * Wiring (5 V -> 3.3 V divider, the ESP32 is NOT 5 V tolerant):
 *
 *   8051 P3.1 (TXD) --- 10k ---+---> ESP32 GPIO16 (RX2)
 *                              |
 *                          10k + 10k
 *                              |
 *   8051 GND ------------------+---> ESP32 GND
 *
 *   EN must NOT be tied to GND (that holds the ESP32 off).
 *
 * Open the Serial Monitor at 115200 baud.
 * Thresholds below mirror src/config.h: keep them in sync when you tune.
 */

#define GW_RX_PIN       16
#define GW_BAUD         9600

/* --- copied from src/config.h --- */
#define TRIG_MIN_DAY    8
#define TRIG_MIN_NIGHT  5
#define MED_PEAK        40
#define MED_PEAK_NIGHT  25
#define STRONG_PEAK     100
#define DIG_HITS        3
#define ALARM_CONFIRM   2

/* one parsed "EV,..." line */
struct Win {
  char ev[10];
  char zone;            /* 'L' 'C' 'R' or '-' */
  int  peak[3], hits[3], active[3], first[3], gmin[3], gmax[3], noise[3];
  int  pir, night, ldr;
};

static const char ZN[3] = { 'L', 'C', 'R' };

/* ---- software UART receiver (9600 8N1) ----
 * On our board the ESP32's hardware UARTs misread the 8051's line even though
 * the signal itself is clean (every bit measures 104 us), so the bridge
 * timestamps edges in an interrupt and decodes the frames itself. */
#define BIT_US   (1000000UL / GW_BAUD)
#define NEDGE    2048
static volatile uint32_t eT[NEDGE];
static volatile uint8_t  eL[NEDGE];
static volatile uint32_t eHead = 0;
static uint32_t eTail = 0;

static void IRAM_ATTR onEdge()
{
  uint32_t h = eHead;
  eT[h % NEDGE] = micros();
  eL[h % NEDGE] = (uint8_t)digitalRead(GW_RX_PIN);
  eHead = h + 1;
}

/* level of the line at time t, searching edges from index i */
static int levelAt(uint32_t i, uint32_t head, uint32_t t)
{
  while (i + 1 < head && (int32_t)(eT[(i + 1) % NEDGE] - t) <= 0) i++;
  return eL[i % NEDGE];
}

/* next received byte, or -1 if none is complete yet */
static int softRead()
{
  uint32_t head = eHead;
  if (head - eTail > NEDGE) eTail = head - NEDGE;          /* overrun: drop old edges */
  while (eTail < head) {
    if (eL[eTail % NEDGE] != 0) { eTail++; continue; }      /* need a falling edge (start bit) */
    uint32_t t0 = eT[eTail % NEDGE];
    if ((int32_t)(micros() - (t0 + BIT_US * 10)) < 0) return -1;   /* frame not finished */
    uint8_t c = 0;
    for (int k = 0; k < 8; k++)
      c |= levelAt(eTail, head, t0 + BIT_US * 3 / 2 + BIT_US * k) << k;
    uint32_t tend = t0 + BIT_US * 9 + BIT_US / 2;            /* skip edges inside the frame */
    eTail++;
    while (eTail < head && (int32_t)(eT[eTail % NEDGE] - tend) < 0) eTail++;
    return c;
  }
  return -1;
}

static String   line;
static uint32_t lastRx = 0, lastNag = 0;
static uint32_t nWin = 0, nLines = 0, nGarbage = 0;
static int      ldrMin = 999, ldrMax = -1;
static int      peakEver[3] = { 0, 0, 0 };
static int      noiseEver[3] = { 0, 0, 0 };
static uint8_t  hist = 0;   /* last 3 windows, bit = PERSON/DIGGING */
static uint8_t  rawBuf[48];
static uint32_t rawN = 0, rawSinceLine = 0, lastDump = 0;

static int num(const char *s) { return (s[0] == '-' && s[1] == 0) ? -1 : atoi(s); }

static bool parse(const String &l, Win &w)
{
  char buf[200];
  char *tok[32];
  int n = 0;

  if (!l.startsWith("EV,") || l.length() >= sizeof(buf)) return false;
  l.toCharArray(buf, sizeof(buf));
  for (char *p = strtok(buf, ","); p && n < 32; p = strtok(NULL, ",")) tok[n++] = p;
  if (n != 27) return false;          /* EV,name,zone + 7x3 + pir,night,ldr */

  strncpy(w.ev, tok[1], sizeof(w.ev) - 1); w.ev[sizeof(w.ev) - 1] = 0;
  w.zone = tok[2][0];
  for (int i = 0; i < 3; i++) {
    w.peak[i]   = num(tok[3 + i]);
    w.hits[i]   = num(tok[6 + i]);
    w.active[i] = num(tok[9 + i]);
    w.first[i]  = num(tok[12 + i]);
    w.gmin[i]   = num(tok[15 + i]);
    w.gmax[i]   = num(tok[18 + i]);
    w.noise[i]  = num(tok[21 + i]);
  }
  w.pir = num(tok[24]); w.night = num(tok[25]); w.ldr = num(tok[26]);
  return true;
}

static void explain(const Win &w)
{
  int z = (w.zone == 'L') ? 0 : (w.zone == 'C') ? 1 : (w.zone == 'R') ? 2 : -1;
  int med = w.night ? MED_PEAK_NIGHT : MED_PEAK;
  int trig = w.night ? TRIG_MIN_NIGHT : TRIG_MIN_DAY;
  bool alarmEv = !strcmp(w.ev, "PERSON") || !strcmp(w.ev, "DIGGING");

  nWin++;
  hist = ((hist << 1) | (alarmEv ? 1 : 0)) & 7;
  if (w.ldr < ldrMin) ldrMin = w.ldr;
  if (w.ldr > ldrMax) ldrMax = w.ldr;
  for (int i = 0; i < 3; i++) {
    if (w.peak[i]  > peakEver[i])  peakEver[i]  = w.peak[i];
    if (w.noise[i] > noiseEver[i]) noiseEver[i] = w.noise[i];
  }

  Serial.printf("\n#%lu  %-7s zone %c   PIR %s   %s (LDR %d)\n",
                (unsigned long)nWin, w.ev, w.zone, w.pir ? "YES" : "no",
                w.night ? "NIGHT" : "DAY", w.ldr);
  Serial.println("        peak  hits  noise  gaps(ms)");
  for (int i = 0; i < 3; i++) {
    Serial.printf("   %c   %4d  %4d  %5d   ", ZN[i], w.peak[i], w.hits[i], w.noise[i]);
    if (w.gmin[i] < 0) Serial.println("-");
    else Serial.printf("%d..%d\n", w.gmin[i], w.gmax[i]);
  }

  Serial.print("   why: ");
  if (z < 0) {
    Serial.printf("no piezo crossed the trigger (needs a jump of about %d+ counts, "
                  "louder than %dx its own noise)\n", trig, 3);
  } else if (!w.pir) {
    Serial.printf("piezo %c was hit (peak %d) but the PIR saw no motion in this "
                  "window -> treated as wind/rain (NOISE). Move in front of the PIR "
                  "while tapping.\n", w.zone, w.peak[z]);
  } else if (alarmEv) {
    Serial.printf("ALARM-worthy: %s in zone %c. LED on. Buzzer needs %d of the last "
                  "3 windows like this (now %d).\n", w.ev, w.zone, ALARM_CONFIRM,
                  ((hist & 1) != 0) + ((hist & 2) != 0) + ((hist & 4) != 0));
  } else {                              /* ANIMAL: say which rule failed */
    bool strong = w.peak[z] >= med;
    bool regular = w.gmin[z] >= 0 && w.gmax[z] <= 2 * w.gmin[z];
    Serial.printf("PIR + piezo %c, but only ANIMAL because:", w.zone);
    if (!strong)
      Serial.printf(" peak %d < %d needed for a person;", w.peak[z], med);
    if (w.gmin[z] < 0)
      Serial.print(" only one hit, no step rhythm (tap 2+ times, ~0.5 s apart);");
    else if (!regular)
      Serial.printf(" uneven rhythm (gaps %d..%d ms, longest must be <= 2x shortest);",
                    w.gmin[z], w.gmax[z]);
    if (w.peak[z] < STRONG_PEAK || w.hits[z] < DIG_HITS)
      Serial.printf(" not digging (needs peak >= %d and %d+ hits)", STRONG_PEAK, DIG_HITS);
    Serial.println();
  }

  /* hardware hints, once there is enough history */
  if (nWin >= 5) {
    if (ldrMin == ldrMax)
      Serial.printf("   check: LDR has read %d every window. Cover it: if it never "
                    "changes, the ADC0809 isn't converting (P0 pull-ups, clock, VREF).\n",
                    ldrMin);
    for (int i = 0; i < 3; i++)
      if (peakEver[i] == 0 && noiseEver[i] == 0)
        Serial.printf("   check: piezo %c has shown zero signal so far "
                      "(amp output stuck, not wired, or ADC not converting).\n", ZN[i]);
  }
}

void setup()
{
  Serial.begin(115200);
  pinMode(GW_RX_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(GW_RX_PIN), onEdge, CHANGE);   /* software UART, RX only */
  delay(300);
  Serial.println("\nGroundWatch bridge: listening to the 8051 on GPIO16 @ 9600");
  lastRx = millis();
}

void loop()
{
  for (int r; (r = softRead()) >= 0; ) {
    char c = (char)r;
    lastRx = millis();
    rawBuf[rawSinceLine % sizeof(rawBuf)] = (uint8_t)c;   /* ring: keeps the latest bytes */
    rawSinceLine++; rawN++;
    if (c == '\r') continue;
    if (c != '\n') {
      if (line.length() < 250) line += c;
      continue;
    }
    if (line.length()) {
      Win w;
      nLines++;
      Serial.print("8051> ");
      Serial.println(line);
      if (parse(line, w)) explain(w);
      else if (!line.startsWith("GW,")) nGarbage++;
      if (nGarbage == 5)
        Serial.println("   check: lines are garbled. Wrong baud, P3.3 grounded (raw "
                       "mode, 57600), or the divider/ground is loose.");
    }
    line = "";
    rawSinceLine = 0;
  }

  /* bytes arrived but the line never finished (stalled 1 s): show them raw */
  if (rawSinceLine && millis() - lastRx > 1000 && millis() - lastDump > 2000) {
    lastDump = millis();
    Serial.printf("[raw: %lu bytes without a line end, total %lu] ",
                  (unsigned long)rawSinceLine, (unsigned long)rawN);
    uint32_t n = rawSinceLine < sizeof(rawBuf) ? rawSinceLine : sizeof(rawBuf);
    for (uint32_t i = rawSinceLine - n; i < rawSinceLine; i++) Serial.printf("%02X ", rawBuf[i % sizeof(rawBuf)]);
    Serial.println();
  }

  if (millis() - lastRx > 5000 && millis() - lastNag > 5000) {
    /* what the wire itself is doing: idle UART = steady high (~3.3 V) */
    int hi = 0;
    for (int i = 0; i < 500; i++) { hi += digitalRead(GW_RX_PIN); delayMicroseconds(200); }
    lastNag = millis();
    Serial.printf("[GPIO16 high %d%% of 100 ms: %s]\n", hi / 5,
                  hi == 500 ? "steady HIGH = wire idle, 8051 not sending" :
                  hi == 0   ? "steady LOW = divider/wire wrong, or 8051 TX held low" :
                              "toggling = data present but not arriving as text (baud?)");
    Serial.println(nLines ? "(no data from the 8051 for 5 s: board reset or wire loose?)"
                          : "(nothing from the 8051 yet: check P3.1 -> 10k -> GPIO16, "
                            "10k+10k to GND, common GND, and that the 8051 is running)");
  }
}
