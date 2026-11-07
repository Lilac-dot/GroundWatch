**GROUNDWATCH  
Complete Hardware Connection Checklist**

8051 development board • ADC0809/ADC0808-compatible interface • 74LS74 clock divider

# 0. Read before wiring

- Power OFF the 8051 board and disconnect USB/power before making or changing wires.

- This list follows the GroundWatch firmware pin map. The README calls the ADC0808; your part is marked ADC0809CCN. They are closely related, but verify the exact ADC0809CCN datasheet and package pin numbers before wiring physical ADC pins.

- Use a regulated +5 V supply for the 8051 board, ADC0809 and 74LS74. Connect all circuit grounds together. Do not connect a sensor output that can exceed the ADC input range or the IC supply rails.

- The clock divider is required if the board ALE is approximately 1.8432 MHz (11.0592 MHz crystal). The 74LS74 output should be approximately 921.6 kHz. A normal multimeter cannot reliably verify this frequency.

- The exact LM358 amplifier resistor/capacitor values and the exact sensor module pin labels are not specified in the repository. The sensor signal connections below identify the required signal path; build/test each analogue stage before connecting the full system.

# 1. Power and common ground

| **From / part**                                        | **Connect to**                                                                                                        | **Notes**                                                                         |
|--------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------|
| Regulated +5 V rail                                    | 8051 board +5 V / VCC rail, ADC VCC, 74LS74 pin 14, LM358 VCC                                                         | Only if these components are designed for 5 V. Check module voltage requirements. |
| Ground rail                                            | 8051 board GND, ADC GND, 74LS74 pin 7, LM358 GND, sensor grounds, LCD GND, buzzer-driver emitter ground, USB-UART GND | All grounds must be common.                                                       |
| 74LS74 pin 14                                          | +5 V                                                                                                                  | Logic supply.                                                                     |
| 74LS74 pin 7                                           | GND                                                                                                                   | Logic ground.                                                                     |
| 74LS74 pins 1 (/CLR1) and 4 (/PRE1)                    | +5 V                                                                                                                  | Active-low asynchronous controls held inactive; do not leave floating.            |
| 74LS74 unused second half: pins 10 (/PRE2), 13 (/CLR2) | +5 V                                                                                                                  | Hold inactive.                                                                    |
| 74LS74 unused second half: pins 12 (D2), 11 (CLK2)     | GND                                                                                                                   | Tie unused inputs to a defined logic level; do not leave floating.                |
| 74LS74                                                 | 100 nF capacitor between pins 14 and 7                                                                                | Place close to the IC supply pins.                                                |

# 2. 74LS74 clock divider → ADC clock

| **From**                       | **To**              | **Purpose**                                                   |
|--------------------------------|---------------------|---------------------------------------------------------------|
| 8051 board header labelled ALE | 74LS74 pin 3 (CLK1) | Clock input. Make sure this is ALE, not PSEN.                 |
| 74LS74 pin 6 (/Q1)             | 74LS74 pin 2 (D1)   | Feedback makes the first flip-flop divide by 2.               |
| 74LS74 pin 5 (Q1)              | ADC0809 CLK input   | Divided clock, expected ~921.6 kHz for a 11.0592 MHz crystal. |

Do NOT connect ALE directly to ADC CLK if ALE is ~1.8432 MHz; that is above the ADC0809 stated maximum clock frequency of 1.28 MHz. Confirm the IC pinout from the manufacturer datasheet and identify pin 1 using the notch/dot.

# 3. ADC0809 ↔ 8051 board

| **ADC signal**            | **8051 board connection**                               | **Notes**                                                                                                                 |
|---------------------------|---------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------|
| D0 (LSB) through D7 (MSB) | P1.0 through P1.7, respectively                         | D0 → P1.0, D1 → P1.1, …, D7 → P1.7.                                                                                       |
| Address A                 | P0.0                                                    | Selects ADC channel bit A.                                                                                                |
| Address B                 | P0.1                                                    | Selects ADC channel bit B.                                                                                                |
| Address C                 | P0.2                                                    | Selects ADC channel bit C.                                                                                                |
| ALE                       | P0.3                                                    | Tie ADC ALE and START together to P0.3 as required by firmware.                                                           |
| START / SOC               | P0.3                                                    | Tie to the same P0.3 node as ADC ALE.                                                                                     |
| OE                        | P0.4                                                    | Output enable.                                                                                                            |
| EOC                       | P0.5                                                    | End-of-conversion signal.                                                                                                 |
| CLK                       | 74LS74 pin 5 (Q1)                                       | Use divided clock from section 2.                                                                                         |
| VCC and GND               | +5 V and common GND                                     | Use the exact ADC0809CCN datasheet for physical pin numbers.                                                              |
| VREF+ and VREF−           | Reference voltage and GND, as required by the datasheet | For a basic 0–5 V range, commonly VREF+ = +5 V and VREF− = GND; verify exact part and design.                             |
| Unused analogue inputs    | Do not leave floating during testing                    | Tie to a safe defined voltage or leave disconnected only if the datasheet/application allows; test one channel at a time. |

Port 0 on a classic 8051 is open-drain. Add external 10 kΩ pull-ups to +5 V on P0.0–P0.5 (six pull-ups), unless the development board already provides suitable pull-ups. Check the board schematic first.

# 4. Analogue sensors → ADC channels

| **Sensor / stage**         | **Signal connection**                                     | **Power / notes**                                                                                                   |
|----------------------------|-----------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------|
| Left piezo disc            | Piezo → LM358 amplifier stage → ADC IN0                   | Use a suitable piezo input/protection and amplifier circuit; do not connect the bare piezo directly to the ADC.     |
| Centre piezo disc          | Piezo → LM358 amplifier stage → ADC IN1                   | Second/appropriate amplifier channel.                                                                               |
| Right piezo disc           | Piezo → LM358 amplifier stage → ADC IN2                   | Use a stable ground/reference and keep signal wiring short.                                                         |
| Analogue microphone module | Analogue OUT → ADC IN3                                    | Use the analogue output, not a digital threshold output. Check module output stays within ADC range.                |
| LDR divider                | +5 V → LDR → ADC IN4 node → 10 kΩ resistor → GND          | Connect the ADC IN4 wire to the junction between LDR and 10 kΩ resistor.                                            |
| LM358                      | VCC → +5 V; GND/V− → GND; output(s) → assigned ADC inputs | Pin numbers depend on package; check LM358 datasheet. Ensure output is never negative or above ADC reference range. |

# 5. PIR motion sensor (HC-SR501)

| **From**                      | **To**                                                   | **Notes**                                                                                                    |
|-------------------------------|----------------------------------------------------------|--------------------------------------------------------------------------------------------------------------|
| HC-SR501 VCC                  | Module-rated supply (often 5 V)                          | Check the exact module marking/specification.                                                                |
| HC-SR501 GND                  | Common GND                                               | Required.                                                                                                    |
| HC-SR501 OUT                  | NPN inverter input/base through a suitable base resistor | Firmware expects motion to pull P3.2 low, so use the inverter stage.                                         |
| NPN inverter collector/output | P3.2 / INT0, with pull-up to +5 V                        | Confirm transistor pinout before wiring; emitter to GND. Output logic should be HIGH idle and LOW on motion. |

# 6. QMC5883L / GY-271 magnetometer

| **Module pin**       | **Connect to**                                         | **Notes**                                                                                                                       |
|----------------------|--------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------|
| VCC                  | Supply supported by your specific module               | Do not assume all GY-271 boards have the same regulator/level shifting.                                                         |
| GND                  | Common GND                                             | Required.                                                                                                                       |
| SCL                  | 8051 P3.4                                              | I²C clock.                                                                                                                      |
| SDA                  | 8051 P3.5                                              | I²C data.                                                                                                                       |
| SCL and SDA pull-ups | 4.7 kΩ to the correct logic rail if not already fitted | Avoid duplicate/incorrect pull-ups. Confirm voltage compatibility; add level shifting if required by module and 5 V 8051 logic. |

# 7. 16×2 LCD (HD44780-compatible, 4-bit mode)

| **LCD signal**    | **8051 connection**                                           | **Notes**                                     |
|-------------------|---------------------------------------------------------------|-----------------------------------------------|
| VSS               | GND                                                           | Ground.                                       |
| VDD               | +5 V                                                          | Check LCD module rating.                      |
| VO / contrast     | Middle pin of a 10 kΩ potentiometer; pot ends to +5 V and GND | Contrast adjustment.                          |
| RS                | P2.0                                                          | Register select.                              |
| RW                | GND                                                           | Write-only mode.                              |
| E / EN            | P2.1                                                          | Enable.                                       |
| D4                | P2.4                                                          | 4-bit data.                                   |
| D5                | P2.5                                                          | 4-bit data.                                   |
| D6                | P2.6                                                          | 4-bit data.                                   |
| D7                | P2.7                                                          | 4-bit data.                                   |
| D0–D3             | Not connected                                                 | Unused in 4-bit mode; follow LCD datasheet.   |
| LED backlight A/K | As specified by module; use current limiting if not built in  | Do not assume the module includes a resistor. |

# 8. Buzzer and alarm LED

| **From**                          | **To**                             | **Notes**                                                                                                   |
|-----------------------------------|------------------------------------|-------------------------------------------------------------------------------------------------------------|
| 8051 P2.2                         | BC547 base through a base resistor | Firmware: HIGH = buzzer ON. Do not drive a high-current buzzer directly from the MCU pin.                   |
| BC547 emitter                     | GND                                | Confirm transistor pinout for your exact package.                                                           |
| BC547 collector                   | Buzzer negative terminal           | Buzzer positive terminal to its rated supply. For magnetic/inductive buzzers, add a suitable flyback diode. |
| +5 V → 330 Ω resistor → LED anode | LED cathode → P2.3                 | Firmware LED is active-low: P2.3 LOW = ON. Confirm polarity.                                                |

# 9. USB-UART serial logging

| **From**         | **To**                         | **Notes**                                                      |
|------------------|--------------------------------|----------------------------------------------------------------|
| 8051 P3.1 / TXD  | USB-UART RX                    | Cross TX to RX.                                                |
| 8051 GND         | USB-UART GND                   | Common ground.                                                 |
| USB-UART TX      | 8051 P3.0 / RXD only if needed | Not required for one-way logging; use compatible logic levels. |
| USB-UART adapter | Computer USB                   | Use 9600 baud, 8 data bits, no parity, 1 stop bit (9600 8N1).  |

# 10. Recommended bring-up order

1.  With power OFF, inspect polarity, shorts, IC orientation and common ground.

2.  Power only the 8051 board and verify its supply is approximately 5 V.

3.  Wire the 74LS74 divider. Use a scope/logic analyser/frequency counter if available; a multimeter is not sufficient to verify a ~921.6 kHz clock.

4.  Wire ADC power, reference, clock and control/data lines. Test one ADC channel using a potentiometer as a safe adjustable voltage source (pot ends to reference/GND, wiper to IN0).

5.  Only after ADC readings change correctly, connect one piezo amplifier to IN0; then add centre and right piezos.

6.  Add microphone and LDR, then LCD, LED and transistor-driven buzzer.

7.  Add PIR inverter and magnetometer last; verify module voltage compatibility before connection.

8.  Connect USB-UART and check the serial log at 9600 8N1.

# 11. Do not guess these details

- Physical ADC0809CCN pin numbers: use the datasheet for the exact package and match each named signal before wiring.

- LM358 amplifier resistor/capacitor values and piezo protection: use a validated schematic; this checklist does not specify a complete analogue amplifier design.

- Board header locations: use the MYTechCare MY8051 V2.1 silkscreen/schematic. The ALE header is not the PSEN header.

- Sensor module power and I²C voltage: check the actual module revision, not only the sensor chip name.

**Firmware reference:** https://github.com/Lilac-dot/GroundWatch (README.md and src/config.h)

**Safety note:** This is a signal-level connection checklist, not a substitute for the exact datasheets and board schematic. Confirm physical pin numbers and supply limits before applying power.
