# GroundWatch: wiring connections

The current list of every connection on the board. It matches the firmware pin map in [`src/config.h`](src/config.h).

**Design as of Nov 2026:** 8051 (AT89S52, 11.0592 MHz), PIR, LDR, three piezo discs through LM358 amplifiers, ADC0809, 16×2 LCD, buzzer and LED. The **magnetometer and microphone have been removed**. The **PIR connects directly** with no inverter.

| Subsystem | Status |
|---|---|
| LDR divider | built; IN4 wire goes in with the ADC |
| PIR | power connected; OUT → P3.2 pending |
| 16×2 LCD + contrast pot | pending |
| Buzzer + BC548, LED | pending |
| ADC0809 + 74LS74 clock | deferred (waiting on jumper wires) |
| Piezo + LM358 amplifiers | circuit values still to be finalized |

> Power off before changing any wire. All grounds are common. Physical pin numbers below are the standard DIP pinouts, so check each one against the datasheet for your exact part and find pin 1 from the notch or dot.

---

## 1. Power and ground

| From | To |
|---|---|
| +5 V (regulated) | 8051 board VCC, ADC0809 pin 11 (VCC), ADC0809 pin 12 (VREF+), 74LS74 pin 14, LM358 pin 8 (both chips), PIR VCC, LCD pin 2, LDR, LED, buzzer + |
| GND | 8051 board GND, ADC0809 pin 13 (GND), ADC0809 pin 16 (VREF−), 74LS74 pin 7, LM358 pin 4 (both chips), PIR GND, LCD pins 1 and 5, 10 kΩ LDR resistor, BC548 emitter, USB-UART GND |
| 100 nF capacitor | across VCC and GND at each IC (ADC0809, 74LS74, each LM358), as close to the pins as possible |

## 2. LDR (light level) → ADC IN4

| From | To |
|---|---|
| LDR leg 1 | +5 V |
| LDR leg 2 | sense junction |
| 10 kΩ leg 1 | sense junction |
| 10 kΩ leg 2 | GND |
| Sense junction | ADC0809 IN4 (pin 2), connected together with the ADC |

The junction voltage rises with light. Firmware: below `LDR_DARK` = night (more sensitive piezo threshold), above `LDR_LIGHT` = day.

## 3. PIR (HC-SR501) → P3.2

| PIR pin | To | Notes |
|---|---|---|
| VCC | +5 V | module accepts 4.5–20 V |
| GND | GND | |
| OUT | **8051 P3.2, direct** | active high: about 3.3 V on motion, 0 V idle. The AT89S52 reads anything above about 1.9 V as high, so no level shifter or inverter is needed. |

- **No BC547/BC548 inverter on the PIR.** The firmware treats P3.2 high as motion and checks the pin every 10 ms.
- **Add a 10 kΩ resistor from P3.2 to GND.** The 8051 has a weak internal pull-up, so if the PIR wire comes loose, an unconnected P3.2 reads high and the board sees motion all the time. With the resistor, a loose wire reads as "no motion". The PIR still drives the pin high easily.
- Set the module's jumper to **H (repeat trigger)** and turn the **time-delay pot fully counter-clockwise** (shortest hold).
- After power-on, the HC-SR501 needs **about 60 s to settle** before its readings are reliable.

## 4. 16×2 LCD (HD44780, 4-bit mode) → Port 2

| LCD pin | Name | To |
|---|---|---|
| 1 | VSS | GND |
| 2 | VDD | +5 V |
| 3 | VO | contrast pot wiper (middle pin) |
| 4 | RS | P2.0 |
| 5 | RW | GND (write-only) |
| 6 | E | P2.1 |
| 7–10 | D0–D3 | not connected |
| 11 | D4 | P2.4 |
| 12 | D5 | P2.5 |
| 13 | D6 | P2.6 |
| 14 | D7 | P2.7 |
| 15 | LED+ (A) | +5 V. Most modules already have a backlight resistor (often 100 Ω, marked R8). If yours doesn't, add about 100 Ω in series. |
| 16 | LED− (K) | GND |

**Contrast pot (5 kΩ or 10 kΩ):** one outer pin to +5 V, the other outer pin to GND, and the middle pin to LCD pin 3. If you see a row of solid blocks or a blank screen, turn the pot.

## 5. Buzzer (BC548 low-side switch) → P2.2

| From | To |
|---|---|
| 8051 P2.2 | 4.7 kΩ pull-up to +5 V **and** a 1 kΩ resistor to the BC548 base |
| BC548 emitter | GND |
| BC548 collector | buzzer − |
| Buzzer + | +5 V (or the buzzer's rated supply) |
| 1N4148 diode (only for a magnetic/inductive buzzer) | across the buzzer, cathode (band) to + |

- Firmware: **P2.2 high = buzzer on.** The buzzer beeps on and off while the alarm is active.
- **Why the 4.7 kΩ pull-up:** a high 8051 port pin only supplies tens of µA, which isn't enough base current to switch the transistor fully on. With the pull-up, about 0.7 mA flows into the base. When the pin is low, it only has to sink about 1 mA.
- The BC548's leg order (C-B-E) varies by manufacturer. Check the datasheet for your part's markings before wiring.

## 6. Alarm LED → P2.3 (active low)

```
+5 V ── 330 Ω ── LED anode (long leg) ── LED cathode (flat side) ── P2.3
```

Firmware: **P2.3 low = LED on.** The pin sinks about 10 mA, which is within the 8051's limit.

## 7. ADC0809 → 8051 (deferred)

| ADC0809 pin | Signal | To |
|---|---|---|
| 26, 27, 28 | IN0, IN1, IN2 | left, centre and right piezo amplifier outputs |
| 1 | IN3 | GND (unused, was the microphone) |
| 2 | IN4 | LDR sense junction |
| 3, 4, 5 | IN5, IN6, IN7 | GND (unused) |
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
| 22 | ALE | P0.3 |
| 6 | START | P0.3 (same wire as ALE) |
| 9 | OE | P0.4 |
| 7 | EOC | P0.5 |
| 10 | CLOCK | 74LS74 pin 5 (Q1) |
| 11 / 13 | VCC / GND | +5 V / GND |
| 12 / 16 | VREF+ / VREF− | +5 V / GND (0–5 V input range) |

- **The data bits are numbered backwards.** On the ADC0809, pin 17 (labelled 2⁻⁸) is the *least* significant bit and pin 21 (2⁻¹) is the *most*.
- **Port 0 needs pull-ups.** It is open-drain, so fit six **10 kΩ pull-ups to +5 V on P0.0–P0.5**, unless the board already has a resistor pack on Port 0. Check the board first.

### ADC clock: 74LS74 divide-by-2

The 8051's ALE pin runs at 11.0592 MHz / 6 = 1.84 MHz, which is above the ADC0809's 1.28 MHz limit. Halving it gives 921.6 kHz.

| 74LS74 pin | To |
|---|---|
| 3 (CLK1) | 8051 board **ALE** header (not PSEN) |
| 2 (D1) | pin 6 (Q̅1), the feedback that makes it divide by 2 |
| 5 (Q1) | ADC0809 pin 10 (CLOCK) |
| 1 (C̅L̅R̅1), 4 (P̅R̅1) | +5 V |
| 10 (P̅R̅2), 13 (C̅L̅R̅2) | +5 V (unused half) |
| 11 (CLK2), 12 (D2) | GND (unused half) |
| 14 / 7 | +5 V / GND |

A multimeter can't measure 921.6 kHz. Use a scope or logic analyser if the lab has one, or just check that ADC readings change when you turn a pot on IN0.

## 8. Piezo discs + LM358 → ADC IN0–IN2

| Piezo | Position | Amplifier → ADC |
|---|---|---|
| Piezo 1 | left | LM358 channel → IN0 (pin 26) |
| Piezo 2 | centre | LM358 channel → IN1 (pin 27) |
| Piezo 3 | right | LM358 channel → IN2 (pin 28) |

LM358 DIP-8 pinout: 1 OUT A, 2 IN A−, 3 IN A+, **4 GND**, 5 IN B+, 6 IN B−, 7 OUT B, **8 +5 V**. Three channels need **two LM358s**. On the spare op-amp, tie + to GND and connect − to OUT so it doesn't oscillate.

- **Never connect a bare piezo to the ADC.** A sharp knock can produce tens of volts.
- Each channel needs a load resistor across the piezo (about 1 MΩ), input clamping, and a gain stage biased so its output stays between 0 V and 5 V. The firmware measures deviation from a resting baseline, so a mid-rail bias of about 2.5 V works best.
- Resistor, capacitor and gain values are **still to be finalized**. Build and test one channel before copying it to the other two.

## 9. Serial log (optional) → P3.1

| From | To |
|---|---|
| 8051 P3.1 (TXD) | USB-UART adapter RX |
| 8051 GND | USB-UART GND |

Settings are 9600 baud, 8N1, and one line every 2 s. In raw mode it's 57600 baud, binary. Run `python3 tools/groundwatch_pc.py <port>` to log and plot. If you use the board's own DB9/RS-232 port instead, it already connects to TXD, so don't add a TTL adapter at the same time.

---

## 8051 pin summary

| Pin | Use | Direction |
|---|---|---|
| P0.0–P0.2 | ADC ADD A, B, C | out (pull-ups) |
| P0.3 | ADC ALE + START | out (pull-up) |
| P0.4 | ADC OE | out (pull-up) |
| P0.5 | ADC EOC | in (pull-up) |
| P1.0–P1.7 | ADC D0–D7 | in |
| P2.0 | LCD RS | out |
| P2.1 | LCD E | out |
| P2.2 | Buzzer (high = on) | out |
| P2.3 | LED (low = on) | out |
| P2.4–P2.7 | LCD D4–D7 | out |
| P3.1 | UART TXD | out |
| P3.2 | PIR OUT (high = motion), 10 kΩ to GND | in |
| P3.3 | raw-mode jumper (GND at reset = raw) | in |
| ALE | 74LS74 CLK1 | clock out |

Free pins: P3.0, P3.4–P3.7, P0.6, P0.7.

## Raw-mode jumper (optional) → P3.3

| From | To |
|---|---|
| 8051 P3.3 | GND through a jumper, only when you want raw mode |

Leave P3.3 open for normal operation. The internal pull-up reads it as high. If P3.3 is grounded at power-up or reset, the board streams every piezo sample to the PC at **57600 baud**, and the LCD shows `Raw 57600`. Record the stream with `python3 tools/groundwatch_raw.py PORT --label footstep`. Detection, the LCD and the alarm keep working. Only the text log is replaced.

## Bring-up order

1. With power off, check polarity, shorts, IC orientation and the common ground.
2. Wire the **LCD and contrast pot**, then flash the firmware. The firmware still runs without the ADC. You should see `GroundWatch / Calibrating...`, then `Listening   DAY / No events`.
3. Connect **PIR OUT → P3.2**. Wait 60 s, then wave a hand in front of it. The top line should change to `Motion`.
4. Add the **LED and buzzer**.
5. Wire the **ADC0809 and 74LS74**. Test with a pot on IN0 (ends to +5 V and GND, wiper to IN0) and watch the serial log.
6. Connect the **LDR junction to IN4**.
7. Build **one piezo channel** on IN0 and check it, then add the centre and right channels.
8. Record real events with `tools/groundwatch_pc.py --label ...` and tune the thresholds in `src/config.h`.
