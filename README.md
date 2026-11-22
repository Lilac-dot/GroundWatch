# GroundWatch

Seismic intrusion detection and localization on an 8051 microcontroller.

Three piezo discs buried along a boundary pick up ground vibration. A PIR sensor confirms that a warm body caused it, and an LDR switches day and night sensitivity. Every two seconds the controller reports what crossed (a person, digging or an animal) and which of three zones it was in, with a local alarm and a serial log for the PC.

Built for the Microprocessors and Microcontrollers (MPMC) course project by Tishya Yadlapalli, Dhanvitha Sankuratri and Naseeb.

**Status (Nov 2026):** the magnetometer and microphone have been dropped from the build, and the PIR now connects directly (active high, no inverter). Firmware is updated to match and the rule table passes its host-side tests (`tests/`). LDR, PIR and LCD are being wired first; the ADC0809 and the piezo amplifiers follow, then on-board testing and threshold tuning from real recordings.

## Why

A camera needs light and line of sight. A single PIR sensor gives false alarms and says nothing about what moved or where. Ground vibration works in the dark and through vegetation, and the timing across several sensors shows where the source is. GroundWatch combines the vibration data with a PIR sensor to separate real intrusions from wind, rain and wildlife.

## How it works

```
 piezo L ─┐                                   ┌─ 16x2 LCD    event + zone
 piezo C ─┼─ LM358 ─┐                         ├─ buzzer, LED alarm
 piezo R ─┘         ├─ ADC0809 ── P1 ─┐       │
 LDR ───────────────┘                 ├─ 8051 ┤
 PIR (HC-SR501) ─────────── P3.2 ─────┘       └─ UART ── PC: log + live plot
```

**Sampling.** Timer0 runs in 8-bit auto-reload mode at 4 kHz and cycles through four slots: piezo L, piezo C, piezo R, LDR. Each slot collects the ADC conversion started in the previous slot and immediately starts the next one, so the 70 µs conversion overlaps with the processing instead of the CPU waiting for EOC. Every channel is sampled at about 1 kHz, with no timing jitter.

**Detection.** Each piezo runs an STA/LTA trigger, the standard seismic event picker, with the adaptive-threshold idea from Koç & Yegin (2013):

```
d   = |sample - baseline|          baseline: ~256 ms average
STA = ~16 ms average of d          short-term envelope
LTA = ~2 s average of STA          background level (wind, traffic)
event on  when STA > max(minimum, 3 x LTA)
event off when STA < max(minimum / 2, 1.5 x LTA)
```

An event onset is a hit. Its time (1 ms resolution) gives the first-hit order for localization, and the gaps between onsets give the walking rhythm. Per 2 s window each piezo reports peak, hits, time inside events, first onset, shortest and longest gap, and background level. Raw samples are never stored, so this fits in a few dozen bytes of RAM. In the ucsim simulator the interrupt uses about 62% of the CPU.

**Raw mode.** With P3.3 jumpered to GND at reset, the board also streams every sample to the PC at 57600 baud (L, C, R, then a 0xFF sync byte). `tools/groundwatch_raw.py` records these into labelled CSV files for building a dataset, and `build/replay` runs any recording through the same detector code on the PC.

**Scheduling.** The main loop runs a countdown scheduler for the slower tasks:

| Period | Task |
|---|---|
| 250 µs (ISR) | One ADC slot: collect, start the next channel, run the STA/LTA step |
| 10 ms | Latch the PIR output, switch day/night from the LDR |
| 200 ms | Buzzer, LED, LCD |
| 2 s | Classify the window, raise the alarm, log to UART |

The PIR is polled rather than wired to INT0: the HC-SR501 output is active high, INT0 only triggers on a falling edge, and the module holds its output high for seconds, so a 10 ms poll can't miss it.

**Localization.** The zone is the piezo with the earliest event onset in the window. A tie goes to the stronger peak. Onset times are logged per piezo, so finer localization from the time differences can be done on the PC.

**Classification.** Each window goes through a fixed rule table. The first rule that matches wins:

| Condition | Event | Action |
|---|---|---|
| No piezo hit | none | log |
| No PIR detection | noise (wind, rain) | log |
| Medium peaks at a steady rhythm | person | alarm |
| Strong peaks (`STRONG_PEAK`), at least `DIG_HITS` hits, not steady | digging | alarm |
| Anything else with PIR detection | animal | log |

"Steady rhythm" means the longest gap between hits is at most twice the shortest. Footsteps pass this check; random taps from small animals usually fail it. At night the LDR lowers the minimum trigger level.

## Repository layout

```
connections.md   every wire for the current design
src/
  config.h       pin map, thresholds, event codes (the only file you should need to edit)
  main.c         scheduler and tasks
  sense.c        1 ms tick and per-channel feature extraction (ISR)
  fusion.c       zone selection and rule table (plain C, unit-tested on the host)
  tree.h         interface for a trained classifier (tree.c is generated by tools/train_tree.py)
  adc0808.c      ADC0808 / ADC0809 driver
  lcd.c          HD44780 16x2 LCD, 4-bit mode
  uart.c         9600 baud serial output
tests/
  test_fusion.c  host-side tests for the rule table
tools/
  groundwatch_pc.py  serial logger, CSV export, live plot, cloud upload
  groundwatch_raw.py raw-mode recorder (labelled CSV)
  train_tree.py      train the classifier on recordings, export src/tree.c
  footprintid_features.py  check the detector on the FootprintID walking dataset
dashboard/       live web dashboard (static HTML/JS, reads Supabase)
database/
  schema.sql     Supabase table, summary view and access rules
docs/
  slides.pdf     project presentation
  hardware_connections.md   older wiring checklist (.docx alongside)
```

## Hardware

| Part | Purpose |
|---|---|
| AT89S52 or P89V51 (8052-compatible), 11.0592 MHz | controller |
| 3x piezo disc, 2x LM358 | vibration sensing and amplification |
| ADC0808 / ADC0809 | 8-channel 8-bit ADC |
| HC-SR501 | PIR body detection |
| LDR + 10k | day/night |
| 16x2 LCD + contrast pot, buzzer + BC547/BC548, LED | local alert |
| USB-UART adapter | PC logging |

The firmware needs about 4 KB of flash and uses the upper 128 bytes of internal RAM, so it targets an 8052-compatible part. A plain 8051 with 4 KB of flash and 128 bytes of RAM would need a trimmed build.

### Pin map

**Wiring:** [`connections.md`](connections.md) has every connection for the current design, with physical pin numbers and the bring-up order. The older checklist in [`docs/hardware_connections.md`](docs/hardware_connections.md) also covers power, decoupling and the clock divider.

| Signal | Pin | Notes |
|---|---|---|
| ADC0808 D0-D7 | P1.0-P1.7 | ADC pin 2^-8 (LSB) to P1.0 |
| ADC0808 ADD A, B, C | P0.0-P0.2 | 10k pull-ups on P0.0-P0.5 (Port 0 is open-drain) |
| ADC0808 ALE + START | P0.3 | tie both ADC pins together |
| ADC0808 OE | P0.4 | |
| ADC0808 EOC | P0.5 | |
| ADC0808 CLOCK | 8051 ALE / 2 | 74HC74 / 74LS74 divider gives 921.6 kHz |
| Piezo L, C, R | ADC IN0-IN2 | through LM358 stages |
| (unused) | ADC IN3, IN5-IN7 | |
| LDR divider | ADC IN4 | LDR to 5 V, 10k to GND |
| PIR output | P3.2 | direct, active high (HC-SR501 gives 3.3 V; 8051 VIH is about 1.9 V) |
| LCD RS, EN | P2.0, P2.1 | RW tied to GND |
| LCD D4-D7 | P2.4-P2.7 | |
| Buzzer | P2.2 | through a BC547 / BC548 low-side switch, high = on |
| Alarm LED | P2.3 | active low |
| UART TX | P3.1 | to the USB-UART RX |

## Building

Requires [SDCC](https://sdcc.sourceforge.net/) and `make`.

```sh
make          # build/groundwatch.hex
make test     # rule-table tests, plus the detector run on synthetic footsteps and wind
make replay   # build/replay: run a raw recording through the detector
make clean
```

Flash `build/groundwatch.hex` with the programmer for your board (for example a USBasp with `avrdude -p 89s52`, or a serial bootloader for the P89V51).

A VS Code workspace is included: `Cmd/Ctrl+Shift+B` builds, and the other tasks (tests, serial logger, demo plot) are under *Terminal > Run Task*.

## Logging and tuning

The thresholds in `src/config.h` are starting values. To set them from real data, record raw samples of each kind of event on site (P3.3 to GND at reset), then replay them through the detector while you adjust `config.h`:

```sh
python3 tools/groundwatch_raw.py /dev/cu.usbserial-0001 --label footstep --seconds 60
make replay && ./build/replay recordings/footstep_*.csv
```

The detector tests (`make test`) also print the detection rate against step size in wind. With the current settings, steps under about 4x the noise amplitude are ignored and steps over 6x are always caught.

For the feature log in normal mode:

```sh
pip install pyserial matplotlib
python3 tools/groundwatch_pc.py --list                                   # find the port
python3 tools/groundwatch_pc.py /dev/cu.usbserial-0001 --label footstep  # log + live plot
python3 tools/groundwatch_pc.py --demo                                   # try it without hardware
```

Each 2-second window becomes one labeled row in `windows.csv`. Comparing peaks, hit counts and gaps across labels gives the threshold values. Times are in ms; gaps are `-` with fewer than two hits; noise is each piezo's background level in ADC counts.

The board prints one line per window at 9600 8N1:

```
EV,<event>,<zone>,peakL,peakC,peakR,hitsL,hitsC,hitsR,activeL,activeC,activeR,firstL,firstC,firstR,
   gapminL,gapminC,gapminR,gapmaxL,gapmaxC,gapmaxR,noiseL,noiseC,noiseR,pir,night,ldr
```

## Cloud database

Windows can also go to a Supabase (Postgres) database, so the whole team sees the same data on the dashboard. The schema and access rules are in [`database/schema.sql`](database/schema.sql).

```sh
cp .env.example .env      # then paste the secret key from Supabase > Project Settings > API Keys
python3 tools/groundwatch_pc.py /dev/cu.usbserial-0001 --cloud --label footstep
python3 tools/groundwatch_pc.py --demo --cloud            # fake rows, stored with device = demo
```

The laptop is the bridge: it reads the board's serial line and uploads each window in the background, holding rows in memory if the network drops. `.env` is git-ignored; the secret key can write to the database, so never commit it. The dashboard only reads, and only for signed-in team members.

## Classifier

The firmware classifies each window with the hand-written rules in `src/fusion.c` until a classifier has been trained on recordings from this board. `tools/train_tree.py` trains a small decision tree on labelled raw recordings, validates it on recordings it never saw, compares it with the rules, and exports it as integer C (`src/tree.c`). When that file exists, the build uses it automatically. The PIR rule always applies. How to record the data: [`docs/recording_checklist.md`](docs/recording_checklist.md).

Public footstep datasets use geophones on concrete, so they can't train a classifier for piezo discs in soil. `tools/footprintid_features.py` uses the FootprintID dataset only to check the detector and rules on real walking. The detector found footsteps in 99.8% of FootprintID's walking windows. That check also showed the old digging rule (strong peak and 3+ hits) labelled a third of real walkers DIGGING. The rules now check for a steady person rhythm first.

## Dashboard

`dashboard/` is a static web page (no build step) that shows the live state of the boundary. It has a status card that turns red on an alarm, a map of the three zones, per-zone vibration over time, recent detections, alarms per hour, hits by zone and the light level. It reads from Supabase and updates live as the bridge uploads windows. Each chart has a table view.

```sh
cd dashboard && python3 -m http.server 8000     # then open http://localhost:8000
```

Open `http://localhost:8000/?preview` to see it with sample data generated in the browser, without signing in.

**Accounts.** Only signed-in team members can read data:

1. In Supabase, turn off **Authentication > Sign In / Providers > Allow new users to sign up**.
2. Under **Authentication > Users > Add user**, create each teammate with an email and password, and tick **Auto Confirm**.
3. Add each email to the allow list in the SQL editor:
   `insert into private.dashboard_users (email) values ('name@example.com');`

The page uses the project's publishable key. That key is meant to be public, and on its own it can't read or write anything.

## Future work

- Replace the rule table with a small neural network on an Artix-7 FPGA, trained on the logged windows
- LoRa link for alerts to a phone
