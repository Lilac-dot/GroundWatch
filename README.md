# GroundWatch

Seismic intrusion detection and localization on an 8051 microcontroller.

Three piezo discs buried along a boundary pick up ground vibration. A PIR sensor, a magnetometer and a microphone are used to confirm what caused it. Every two seconds the controller reports what crossed (a person, vehicle, digging or an animal) and which of three zones it was in, with a local alarm and a serial log for the PC.

Built for the Microprocessors and Microcontrollers (MPMC) course project by Tishya Yadlapalli, Dhanvitha Sankuratri and Naseeb.

## Why

A camera needs light and line of sight. A single PIR sensor gives false alarms and says nothing about what moved or where. Ground vibration works in the dark and through vegetation, and the timing across several sensors shows where the source is. GroundWatch combines the vibration data with three cheap sensors to separate real intrusions from wind, rain and wildlife.

## How it works

```
 piezo L ─┐                                   ┌─ 16x2 LCD    event + zone
 piezo C ─┼─ LM358 ─┐                         ├─ buzzer, LED alarm
 piezo R ─┘         ├─ ADC0808 ── P1 ─┐       │
 microphone ────────┤                 │       │
 LDR ───────────────┘                 ├─ 8051 ┤
 PIR (HC-SR501) ─────────── INT0 ─────┤       │
 QMC5883L magnetometer ──── I2C ──────┘       └─ UART ── PC: log + live plot
```

**Sampling.** Timer0 interrupts every 1 ms. The interrupt reads the three piezos and the microphone. For each piezo it updates a few running values for the current window: peak amplitude, number of distinct hits, time above threshold, time of the first hit, and the shortest and longest gap between hits. Raw samples are never stored, so the whole sensing state fits in a few dozen bytes of RAM.

**Scheduling.** The main loop runs a countdown scheduler for the slower tasks:

| Period | Task |
|---|---|
| 1 ms (ISR) | Sample piezos and mic, update per-channel features |
| 10 ms | Count sound bursts, switch day/night from the LDR |
| 100 ms | Read the magnetometer, compare with its baseline |
| 200 ms | Buzzer, LED, LCD |
| 2 s | Classify the window, raise the alarm, log to UART |
| on interrupt | PIR motion flag (INT0) |

**Localization.** The zone is the piezo that registered the first hit in the window. A tie goes to the stronger peak.

**Classification.** Each window goes through a fixed rule table. The first rule that matches wins:

| Condition | Event | Action |
|---|---|---|
| No piezo hit | none | log |
| Magnetic field change above `MAG_THR` | vehicle | alarm |
| No PIR detection | noise (wind, rain) | log |
| Strong peaks and repeated sound bursts | digging | alarm |
| Medium peaks at a steady rhythm | person | alarm |
| Anything else with PIR detection | animal | log |

"Steady rhythm" means the longest gap between hits is at most twice the shortest. Footsteps pass this check; random taps from small animals usually fail it. At night the LDR lowers the piezo threshold.

## Repository layout

```
src/
  config.h       pin map, thresholds, event codes (the only file you should need to edit)
  main.c         scheduler and tasks
  sense.c        1 ms tick and per-channel feature extraction (ISR)
  fusion.c       zone selection and rule table (plain C, unit-tested on the host)
  adc0808.c      ADC0808 driver
  i2c.c          bit-banged I2C master
  qmc5883l.c     magnetometer driver
  lcd.c          HD44780 16x2 LCD, 4-bit mode
  uart.c         9600 baud serial output
tests/
  test_fusion.c  host-side tests for the rule table
tools/
  groundwatch_pc.py  serial logger, CSV export and live plot
docs/
  slides.pdf     project presentation
```

## Hardware

| Part | Purpose |
|---|---|
| AT89S52 or P89V51 (8052-compatible), 11.0592 MHz | controller |
| 3x piezo disc, 2x LM358 | vibration sensing and amplification |
| ADC0808 | 8-channel 8-bit ADC |
| HC-SR501 | PIR body detection |
| QMC5883L (GY-271) | vehicle detection via magnetic field |
| Analog microphone module | digging and tool noise |
| LDR + 10k | day/night |
| 16x2 LCD, buzzer, LED | local alert |
| USB-UART adapter | PC logging |

The firmware needs about 5 KB of flash and uses the upper 128 bytes of internal RAM, so it targets an 8052-compatible part. A plain 8051 with 4 KB of flash and 128 bytes of RAM would need a trimmed build.

### Pin map

| Signal | Pin | Notes |
|---|---|---|
| ADC0808 D0-D7 | P1.0-P1.7 | ADC pin 2^-8 (LSB) to P1.0 |
| ADC0808 ADD A, B, C | P0.0-P0.2 | 10k pull-ups on P0.0-P0.5 (Port 0 is open-drain) |
| ADC0808 ALE + START | P0.3 | tie both ADC pins together |
| ADC0808 OE | P0.4 | |
| ADC0808 EOC | P0.5 | |
| ADC0808 CLOCK | 8051 ALE / 2 | 74HC74 divider gives 921.6 kHz |
| Piezo L, C, R | ADC IN0-IN2 | through LM358 stages |
| Microphone | ADC IN3 | |
| LDR divider | ADC IN4 | LDR to 5 V, 10k to GND |
| PIR output | P3.2 (INT0) | via NPN inverter so motion pulls the pin low |
| QMC5883L SCL, SDA | P3.4, P3.5 | 4.7k pull-ups; level-shift if the module has none |
| LCD RS, EN | P2.0, P2.1 | RW tied to GND |
| LCD D4-D7 | P2.4-P2.7 | |
| Buzzer | P2.2 | through a BC547, high = on |
| Alarm LED | P2.3 | active low |
| UART TX | P3.1 | to the USB-UART RX |

## Building

Requires [SDCC](https://sdcc.sourceforge.net/) and `make`.

```sh
make          # build/groundwatch.hex
make test     # run the rule-table tests on the host
make clean
```

Flash `build/groundwatch.hex` with the programmer for your board (for example a USBasp with `avrdude -p 89s52`, or a serial bootloader for the P89V51).

A VS Code workspace is included: `Cmd/Ctrl+Shift+B` builds, and the other tasks (tests, serial logger, demo plot) are under *Terminal > Run Task*.

## Logging and tuning

The thresholds in `src/config.h` are starting values. To set them from real data, record each kind of event on site:

```sh
pip install pyserial matplotlib
python3 tools/groundwatch_pc.py --list                                   # find the port
python3 tools/groundwatch_pc.py /dev/cu.usbserial-0001 --label footstep  # log + live plot
python3 tools/groundwatch_pc.py --demo                                   # try it without hardware
```

Each 2-second window becomes one labeled row in `windows.csv`. Comparing peaks, hit counts, magnetometer deviation and sound bursts across labels gives the threshold values.

The board prints one line per window at 9600 8N1:

```
EV,<event>,<zone>,peakL,peakC,peakR,hitsL,hitsC,hitsR,activeL,activeC,activeR,firstL,firstC,firstR,mag_dev,sound_bursts,pir,night,ldr
```

## Future work

- Replace the rule table with a small neural network on an Artix-7 FPGA, trained on the logged windows
- LoRa link for alerts to a phone
