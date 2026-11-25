# GroundWatch: wiring connections

Every connection on the board, as built. It matches the firmware pin map in [`src/config.h`](src/config.h).

**Design as of Nov 2026:** 8051 (AT89S52 / P89V51, 11.0592 MHz), HC-SR501 PIR, LDR, three piezo discs through LM358 amplifiers, ADC0809 with a 74LS74 clock divider, 16×2 LCD, buzzer and LED. The magnetometer and microphone have been removed. The PIR connects directly with no inverter.

| Subsystem | Status |
|---|---|
| Power rails + 100 nF decoupling | built |
| 16×2 LCD + contrast pot | built |
| PIR → P3.2 | built |
| Alarm LED | built |
| Buzzer + BC547 | built, **unplugged** until the board has a real 5 V supply (see Power notes) |
| LDR divider → IN4 | built |
| Port 0 pull-ups | built |
| 74LS74 clock divider | built |
| ADC0809 | built |
| 2.5 V reference (VREF) | built |
| Piezo L + amplifier → IN0 | built |
| Piezo C + amplifier → IN1 | pending (IN1 tied to **GND** until built) |
| Piezo R + amplifier → IN2 | pending (IN2 tied to **GND** until built) |

### Power notes (bring-up, 24 Nov 2026)

- Powered from the laptop through the board's USB programmer (the ATmega8A section), the "5 V" rail measures only **~4.2 V**. The AT89S52 needs ≥ 4.0 V, so it runs, but with no margin.
- **Buzzer:** at reset every port pin is high, so P2.2 switches the buzzer on before `main()` can turn it off. On the 4.2 V supply that current drops the rail below 4.0 V and the 8051 never starts (LCD flickers, buzzer rings, no serial). Keep the buzzer's + wire unplugged until the board gets a real 5 V (phone charger + cut USB cable into the 5 V / GND header pins; never together with the board's USB).
- **Amplifier supply:** the LM358(s), the top 10 kΩ of the VREF divider and the upper 1N4148 clamps run from a separate **AMP +** rail fed by the ESP32's **VIN** pin (~5 V). Everything else stays on the board's rail. Grounds are common. VREF then measures ~2.6 V. Don't leave the ESP32 powered with the 8051 board off.

> Power off before changing any wire. All grounds are common. Pin numbers are the standard DIP pinouts: find pin 1 from the notch or dot, and check against the datasheet for your exact part.

---

## Parts list

| Part | Qty | Used for |
|---|---|---|
| AT89S52 / P89V51 board, 11.0592 MHz | 1 | controller |
| ADC0809 (or ADC0808) | 1 | 8-channel 8-bit ADC |
| 74LS74 (or 74HC74) | 1 | ADC clock divider |
| LM358N | 2 | piezo amplifiers (3 of the 4 op-amps used) |
| Piezo disc | 3 | vibration sensors, one per zone |
| HC-SR501 PIR | 1 | body detection |
| LDR | 1 | day / night |
| 16×2 LCD (HD44780) | 1 | local display |
| 10 kΩ pot (marked 103) | 1 | LCD contrast |
| BC547 (or BC548) | 1 | buzzer switch |
| Buzzer (5 V active) | 1 | alarm |
| LED | 1 | alarm |
| 1N4148 diode | 6 | piezo input protection (2 per channel) |
| 100 nF ceramic capacitor (marked 104) | 4 | decoupling, one per IC |
| 10 µF electrolytic capacitor | 1 | VREF smoothing |
| 10 kΩ resistor | 17 (+1 if the centre channel needs gain 21) | see sections below |
| 1 kΩ resistor | 5 | see sections below |
| 1 MΩ resistor | 3 | piezo bias |
| USB-UART adapter | 1 | PC logging (optional if the board has its own serial port) |

Only 1 kΩ, 10 kΩ and 1 MΩ resistors are used. Values differ slightly from the original design (330 Ω LED resistor, 4.7 kΩ buzzer pull-up, 100 kΩ feedback): the substitutes below work the same way.

---

## 1. Power and ground

| From | To |
|---|---|
| +5 V (board header) | breadboard + rail (join both halves of each rail) |
| GND (board header) | breadboard − rail |
| +5 V | ADC0809 pins 11 and 12, 74LS74 pins 1, 4, 10, 13, 14, PIR VCC, LCD pins 2 and 15, LDR, LED resistor, buzzer +, Port 0 pull-ups, buzzer pull-up |
| AMP + (ESP32 VIN) | LM358 pin 8 (both), top 10 kΩ of the VREF divider, upper 1N4148 clamps (see Power notes) |
| GND | ADC0809 pins 13 and 16, 74LS74 pins 7, 11, 12, LM358 pin 4 (both), PIR GND, LCD pins 1, 5, 16, LDR 10 kΩ, BC547 emitter, VREF divider, USB-UART GND |

**Decoupling:** one 100 nF (104) capacitor as close as possible to each chip:

| Chip | Between |
|---|---|
| ADC0809 | pin 11 ↔ pin 13 |
| 74LS74 | pin 14 ↔ pin 7 |
| LM358 #1 | pin 8 ↔ pin 4 |
| LM358 #2 | pin 8 ↔ pin 4 |

## 2. 16×2 LCD (HD44780, 4-bit mode) → Port 2

| LCD pin | Name | To |
|---|---|---|
| 1 | VSS | GND |
| 2 | VDD | +5 V |
| 3 | VO | contrast pot middle pin (outer pins to +5 V and GND) |
| 4 | RS | P2.0 |
| 5 | RW | GND (write only) |
| 6 | E | P2.1 |
| 7–10 | D0–D3 | not connected |
| 11 | D4 | P2.4 |
| 12 | D5 | P2.5 |
| 13 | D6 | P2.6 |
| 14 | D7 | P2.7 |
| 15 | LED+ | +5 V (module has its own backlight resistor) |
| 16 | LED− | GND |

No pot? LCD pin 3 → 1 kΩ → GND also gives readable text. Never leave pin 3 unconnected. If you see a blank screen or a row of solid blocks, turn the pot.

## 3. PIR (HC-SR501) → P3.2

| PIR pin | To | Notes |
|---|---|---|
| VCC | +5 V | accepts 4.5–20 V |
| GND | GND | |
| OUT | P3.2, direct | active high: about 3.3 V on motion, 0 V idle. The 8051 reads above about 1.9 V as high, so no transistor or level shifter is needed. |

Set the jumper to **H** (repeat trigger) and turn the time-delay pot fully anticlockwise. The module needs about 60 s after power-on to settle; the LCD shows `PIR warm-up` meanwhile.

## 4. Alarm LED → P2.3 (active low)

```
+5 V ── 1 kΩ ── LED anode (long leg) ── LED cathode (short leg) ── P2.3
```

P2.3 low = LED on. About 3 mA, well within the 8051's sink limit.

## 5. Buzzer (BC547 low-side switch) → P2.2

BC547 pins, flat side facing you, legs down: **C – B – E** (left to right).

| From | Through | To |
|---|---|---|
| P2.2 | 10 kΩ | +5 V (pull-up) |
| P2.2 | 1 kΩ | BC547 base (B) |
| BC547 emitter (E) | wire | GND |
| BC547 collector (C) | wire | buzzer − |
| Buzzer + | wire | +5 V |
| 1N4148 across the buzzer, band to + | | only for a magnetic buzzer |

```
 +5V ──10k──┐
            │
 P2.2 ──────┴──1k── B   BC547   C ── buzzer(−)   buzzer(+) ── +5V
                                E ── GND
```

P2.2 high = buzzer on. A high 8051 pin supplies almost no current on its own, so the 10 kΩ pull-up provides the base current (about 0.4 mA, enough to switch about 40 mA).

## 6. LDR (light level) → ADC IN4

```
+5 V ── LDR ──┬── 10 kΩ ── GND
              │
        ADC0809 pin 2 (IN4)
```

The junction voltage rises with light. Below `LDR_DARK` the firmware switches to night mode (more sensitive piezo threshold).

## 7. Port 0 pull-ups

Port 0 is open-drain and has no internal pull-ups, and the 8051 can't enable them in software. Fit one **10 kΩ from each of P0.0, P0.1, P0.2, P0.3, P0.4 and P0.5 to +5 V** (6 resistors).

Skip these only if the board already has a resistor pack on Port 0. To check: power on with nothing connected to P0 and measure P0.0 to GND. About 5 V means pull-ups are already fitted.

## 8. 74LS74 clock divider → ADC CLOCK

The 8051's ALE pin runs at 11.0592 MHz / 6 = 1.84 MHz, above the ADC0809's 1.28 MHz limit. One flip-flop halves it to 921.6 kHz.

| 74LS74 pin | To |
|---|---|
| 14 | +5 V |
| 7 | GND |
| 3 (CLK1) | 8051 ALE pin (not PSEN) |
| 2 (D1) | pin 6 (Q̅1): feedback that divides by 2 |
| 5 (Q1) | ADC0809 pin 10 (CLOCK) |
| 1 (C̅L̅R̅1), 4 (P̅R̅1) | +5 V |
| 10 (P̅R̅2), 13 (C̅L̅R̅2) | +5 V (unused half) |
| 11 (CLK2), 12 (D2) | GND (unused half) |

## 9. ADC0809 → 8051

| ADC pin | Signal | To |
|---|---|---|
| 11 | VCC | +5 V |
| 12 | VREF+ | +5 V |
| 13 | GND | GND |
| 16 | VREF− | GND |
| 10 | CLOCK | 74LS74 pin 5 |
| 17 | 2⁻⁸ = D0 (LSB) | P1.0 |
| 14 | 2⁻⁷ = D1 | P1.1 |
| 15 | 2⁻⁶ = D2 | P1.2 |
| 8 | 2⁻⁵ = D3 | P1.3 |
| 18 | 2⁻⁴ = D4 | P1.4 |
| 19 | 2⁻³ = D5 | P1.5 |
| 20 | 2⁻² = D6 | P1.6 |
| 21 | 2⁻¹ = D7 (MSB) | P1.7 |
| 25 | ADD A | P0.0 |
| 24 | ADD B | P0.1 |
| 23 | ADD C | P0.2 |
| 22 | ALE | P0.3 (same wire as START) |
| 6 | START | P0.3 (same wire as ALE) |
| 9 | OE | P0.4 |
| 7 | EOC | P0.5 |
| 26 | IN0 | piezo L amplifier output |
| 27 | IN1 | piezo C amplifier output (**GND** until built) |
| 28 | IN2 | piezo R amplifier output (**GND** until built) |
| 2 | IN4 | LDR junction |
| 1, 3, 4, 5 | IN3, IN5, IN6, IN7 | GND (unused) |

The data bits are numbered backwards on the ADC0809: pin 17 (2⁻⁸) is the least significant bit and pin 21 (2⁻¹) the most.

## 10. 2.5 V reference (VREF), shared by all three amplifiers

```
+5 V ── 10 kΩ ──┬── 10 kΩ ── GND
                │
              VREF (2.5 V)
                │
         10 µF + leg ;  − leg (stripe) ── GND
```

Measure about 2.5 V from VREF to GND. Each amplifier rests at this level when nothing is moving, which the firmware learns as the baseline during `Calibrating...`.

## 11. Piezo amplifiers (LM358) → ADC IN0–IN2

### What the circuit does
A footstep makes the piezo produce a small, fast signal (tens of mV). Each LM358 op-amp is a non-inverting amplifier with gain 1 + 10k/1k = **11**:

```
output = 2.5 V + 11 × (piezo signal)
```

At rest the output sits at 2.5 V (ADC reads about 128). A footstep moves it away from 2.5 V and the firmware measures how far. The LM358 output only reaches about 3.5 V on a 5 V supply, so upward swings clip about 1 V above rest; downward swings have 2.5 V of room.

### LM358 pinout (DIP-8)

```
        ┌──U──┐
 OUT A 1│     │8 AMP +
 IN− A 2│     │7 OUT B
 IN+ A 3│     │6 IN− B
   GND 4│     │5 IN+ B
        └─────┘
```

### One channel

```
                  AMP + (~5 V)
                        │
                     1N4148  (band towards AMP +)
                        │
Piezo red ── 10 kΩ ── [node X] ───────────── IN+
                        │     │
                     1N4148   1 MΩ
                (band towards │
                   node X)    │
                        │     │
                       GND    │
Piezo black ─────────────── VREF ── 1 kΩ ── IN− ── 10 kΩ ── OUT ──► ADC input
```

**Node X** is one breadboard row where the series resistor, both diodes, the 1 MΩ and the op-amp + input meet. **Each channel has its own node X. Never join the node X of two channels**, or the signals mix and the zone can't be found. All channels share one VREF.

| # | Part | From | To | Purpose |
|---|---|---|---|---|
| 1 | 10 kΩ | piezo red | node X | limits current from spikes |
| 2 | 1N4148 | node X | AMP + (band at AMP +) | clamps node X below 5 V |
| 3 | 1N4148 | GND | node X (band at node X) | clamps node X above 0 V |
| 4 | 1 MΩ | node X | VREF | holds the input at 2.5 V at rest, drains charge |
| 5 | wire | piezo black | VREF | the signal swings around 2.5 V |
| 6 | wire | node X | IN+ | signal into the op-amp |
| 7 | 1 kΩ | IN− | VREF | gain |
| 8 | 10 kΩ | IN− | OUT | gain (feedback) |
| 9 | wire | OUT | ADC input | amplified signal |

### Pin assignment

| Channel | Zone | Chip | IN+ (node X) | IN− | OUT | ADC pin |
|---|---|---|---|---|---|---|
| Piezo 1 | Left | LM358 #1 | 3 | 2 | 1 | 26 (IN0) |
| Piezo 2 | Centre | LM358 #1 | 5 | 6 | 7 | 27 (IN1) |
| Piezo 3 | Right | LM358 #2 | 3 | 2 | 1 | 28 (IN2) |
| spare | — | LM358 #2 | 5 → GND | 6 joined to 7 | 7 | — |

The spare op-amp is tied off (+ to GND, − to OUT) so it doesn't oscillate.

### Wiring the centre (C) and right (R) channels

Left is already built on LM358 #1 pins 1–3. Centre uses the other half of the same chip, and right uses LM358 #2.

**Power off first, then:**

1. **Remove the GND wires from ADC pin 27 (IN1) and pin 28 (IN2).** An op-amp output wired to a grounded pin is a short.
2. If LM358 #2 isn't fitted yet, power it:

   | LM358 #2 pin | To |
   |---|---|
   | 8 | AMP + rail (ESP32 VIN, the same rail as LM358 #1) |
   | 4 | GND |
   | 100 nF (104) | pin 8 ↔ pin 4, close to the chip |
   | 5 | GND (spare op-amp) |
   | 6 | joined to pin 7 (spare op-amp) |

3. Wire each channel. Pick a free breadboard row for each channel's **node X** and never join it to another channel's node X.

**Centre (C) → LM358 #1, pins 5 / 6 / 7 → ADC pin 27 (IN1)**

| # | Part | From | To |
|---|---|---|---|
| 1 | 10 kΩ | piezo C red | node X(C) |
| 2 | 1N4148 | node X(C) | AMP + (band at AMP +) |
| 3 | 1N4148 | GND | node X(C) (band at node X) |
| 4 | 1 MΩ | node X(C) | VREF |
| 5 | wire | piezo C black | VREF |
| 6 | wire | node X(C) | LM358 #1 pin 5 (IN+ B) |
| 7 | 1 kΩ | LM358 #1 pin 6 (IN− B) | VREF |
| 8 | 10 kΩ | LM358 #1 pin 6 | LM358 #1 pin 7 (OUT B) |
| 9 | wire | LM358 #1 pin 7 | ADC pin 27 (IN1) |

**Right (R) → LM358 #2, pins 3 / 2 / 1 → ADC pin 28 (IN2)**

| # | Part | From | To |
|---|---|---|---|
| 1 | 10 kΩ | piezo R red | node X(R) |
| 2 | 1N4148 | node X(R) | AMP + (band at AMP +) |
| 3 | 1N4148 | GND | node X(R) (band at node X) |
| 4 | 1 MΩ | node X(R) | VREF |
| 5 | wire | piezo R black | VREF |
| 6 | wire | node X(R) | LM358 #2 pin 3 (IN+ A) |
| 7 | 1 kΩ | LM358 #2 pin 2 (IN− A) | VREF |
| 8 | 10 kΩ | LM358 #2 pin 2 | LM358 #2 pin 1 (OUT A) |
| 9 | wire | LM358 #2 pin 1 | ADC pin 28 (IN2) |

**Extra parts for these two channels:** 2 × 10 kΩ (series), 2 × 1 kΩ (gain), 2 × 10 kΩ (feedback), 2 × 1 MΩ, 4 × 1N4148, 1 × 100 nF (LM358 #2), plus jumpers. VREF is shared: don't build a second divider.

**Smaller centre piezo.** The centre disc is smaller than the other two, so it may give a weaker signal for the same step, and the zone decision compares peaks between zones. Build it at gain 11 first, then tap L and C the same way and compare `peakL` and `peakC` in the serial log. If C peaks below about 70 % of L, replace C's 10 kΩ feedback with **two 10 kΩ in series (20 kΩ, gain 21)**. Matching gains this way calibrates the channels for sensor mismatch, so the same step reads about the same in every zone.

**Check before tapping (power on, nothing touching the zones):** node X, IN− and OUT of each new channel should all read about the same as VREF (~2.5–2.6 V). Then tap L, C and R one at a time: each tap must show first in its own zone (`firstL` / `firstC` / `firstR`). If a tap shows in the wrong zone, the piezo wires are swapped at the amplifier inputs.

### Checks and tuning

With nothing moving, node X, IN− and OUT should all read about 2.5 V.

| Symptom | Likely cause / fix |
|---|---|
| OUT stuck near 0 V or 3.5 V | VREF missing, 1 kΩ / 10 kΩ on the wrong pin, or a diode reversed |
| Taps barely show | raise the gain: change the 10 kΩ feedback to 47 kΩ or 100 kΩ |
| Every light tap gives the maximum | lower the gain: 1 kΩ to VREF → 2.2 kΩ |
| Random hits with nobody near | 1 MΩ missing, or loose piezo wires |

> Never connect a bare piezo straight to the ADC or the op-amp: a sharp knock can produce tens of volts.

## 12. Sensing platform (cardboard zones)

The test platform is a strip of three cardboard squares joined by narrow strips:

```
┌────────┐   ┌────────┐   ┌────────┐
│ Zone L │═══│ Zone C │═══│ Zone R │      PIR at the side, facing across
│   (●)  │   │   (●)  │   │   (●)  │      LDR face-up, in room light
└────────┘   └────────┘   └────────┘
  IN0          IN1          IN2
```

- One piezo glued or taped firmly under the centre of each square.
- Keep the joining strips narrow so a step on one square shakes its neighbours less.
- Twist each piezo's two wires together and label them L, C, R.
- Test: a tap on the left square must appear first in `peakL` / `firstL`. If it shows in another channel, swap the wires at the amplifier inputs.

## 13. Serial log → P3.1

| From | To |
|---|---|
| 8051 P3.1 (TXD) | USB-UART adapter RX |
| 8051 GND | USB-UART GND |

9600 baud, 8N1, one `EV,...` line every 2 s. Open it with PuTTY or `python tools/groundwatch_pc.py <port>`. If you use the board's own DB9 / USB serial port instead, it is already wired to TXD, so don't connect a second adapter.

## 14. Raw-mode jumper → P3.3

| P3.3 at reset | Mode |
|---|---|
| unconnected | normal: classification at 9600 baud |
| jumpered to GND | raw: every sample streamed at 57600 baud for `tools/groundwatch_raw.py` |

Leave P3.3 unconnected for normal use.

---

## 8051 pin summary

| Pin | Use | Direction |
|---|---|---|
| P0.0–P0.2 | ADC ADD A, B, C | out (10 kΩ pull-ups) |
| P0.3 | ADC ALE + START | out (10 kΩ pull-up) |
| P0.4 | ADC OE | out (10 kΩ pull-up) |
| P0.5 | ADC EOC | in (10 kΩ pull-up) |
| P1.0–P1.7 | ADC D0–D7 | in |
| P2.0 | LCD RS | out |
| P2.1 | LCD E | out |
| P2.2 | buzzer (high = on) | out |
| P2.3 | LED (low = on) | out |
| P2.4–P2.7 | LCD D4–D7 | out |
| P3.1 | UART TXD | out |
| P3.2 | PIR OUT (high = motion) | in |
| P3.3 | raw-mode jumper | in |
| ALE | 74LS74 CLK1 | clock out |

Free pins: P3.0, P3.4–P3.7, P0.6, P0.7.

## Bring-up order

1. Power off: check polarity, shorts, chip orientation and the common ground. Measure 5 V on the rails.
2. LCD + contrast pot. Flash the firmware: `GroundWatch` / `Calibrating...`, then `PIR warm-up`, then `Listening   DAY` / `No events`.
3. PIR → P3.2. After 60 s, wave a hand: the top line shows `Motion`.
4. LED and buzzer.
5. Port 0 pull-ups, 74LS74 and ADC0809. Connect the LDR junction to IN4 and cover the LDR: the last number in the serial line (`ldr`) should drop.
6. VREF, then the left piezo channel on IN0, with IN1 and IN2 tied to GND. (Tied to VREF, IN1 dragged VREF down to ~2 V on our ADC and IN2 echoed every left tap, so the zone came out wrong.) Tap the left square: `peakL` and `hitsL` rise.
7. Centre and right channels.
8. Record real events with `tools/groundwatch_pc.py --label ...` and tune the thresholds in `src/config.h`.
