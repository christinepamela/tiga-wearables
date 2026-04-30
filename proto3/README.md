# TIGA Proto 3 — Build Notes

**Version:** Proto 3 / firmware v6 (planned)
**Status:** Wiring in progress, awaiting replacement MAX30102 + new components
**Hardware base:** LilyGO T-Display-S3 (ESP32-S3R8) + LiPo 1000mAh

This document captures what changed from proto 2 (v5.2) to proto 3 (v6), the new wiring map, and the rationale for each decision.

---

## Why proto 3 exists

Proto 2 (v5.2) validated the firmware architecture — MPU6050 robustness, button handling, BLE structure, session reset. But the sensor suite was minimum-viable: a basic pulse sensor with poor accuracy, no SpO2, no elevation, no contact detection.

Proto 3 upgrades the sensor suite to medical-grade quality while removing components made redundant by the architecture decision (companion phone app handles GPS, time sync, and SMS gateway).

The result is a smaller component count with better data quality and simpler firmware.

---

## What's new in proto 3

| Component | Address/Pin | Replaces | Why it matters |
|-----------|-------------|----------|----------------|
| MAX30102 | I2C 0x57 | Pulse sensor + TTP223 contact pad | Medical-grade HR + SpO2, IR-based contact detection (no extra touch sensor needed) |
| BMP280 | I2C 0x76 | (new) | Elevation, floors climbed, stair detection, weather pressure correlation for joint pain tracking |
| Passive buzzer 3-12V | GPIO13 (PWM) | (new) | Variable-tone alerts: gentle ding for goals, urgent for falls, melodic for milestones |
| Piezo analog sensor | GPIO02 (ADC) | SW-420 vibration switch | Analog impact magnitude (not just on/off), enables tap-to-wake, fall severity, panic-tap detection |
| 10mm coin vibration motor | GPIO12 (via NPN) | (new) | Silent tactile alerts, important for elderly with hearing loss or in quiet environments |
| 0.1µF ceramic capacitors | Across each VCC/GND | (new) | I2C noise suppression — prevents bus glitches during MAX30102 LED pulses |
| 100µF electrolytic cap | Across 3V3 rail | (new) | Bulk power buffer for MAX30102 IR LED current spikes |
| Toggle/slide switch | Battery line | (new) | Real power switch, no more unplugging JST connectors |

---

## What's removed from proto 2

| Component | Reason for removal |
|-----------|-------------------|
| Original pulse sensor (GPIO1) | Replaced by MAX30102, which is more accurate and adds SpO2 |
| SW-420 vibration switch (GPIO3) | Replaced by analog piezo, which gives magnitude not just binary |
| TTP223 capacitive touch (GPIO13) | Replaced by MAX30102 IR signal validity (presence of finger = device worn) |
| DS3231 RTC (considered, not added) | Phone provides time via BLE on every connect; ESP32 internal RTC drift is covered by sync |
| SIM800L cellular module (considered, never added) | Companion phone app handles SMS/WhatsApp/Telegram fan-out to caregivers |
| NEO-6M GPS (kept but uncertain) | Phone provides location for app, but device GPS still useful for fall-location logging when phone is out of BLE range |

---

## Pin map — proto 3

```
LilyGO T-Display-S3 — proto 3 final assignments

P1 HEADER (right side):
  3V3      → Breadboard red rail (powers all sensors)
  GND      → Breadboard blue rail (common ground)
  GPIO43   → GPS RX (ESP TX)        [UART]
  GPIO44   → GPS TX (ESP RX)        [UART]
  GPIO18   → I2C SDA bus            [shared by 3 devices]
  GPIO17   → I2C SCL bus            [shared by 3 devices]
  GPIO21   → Button 1 — Scroll/Menu [pull-up, hold 3s = sleep]
  GPIO16   → Button 2 — Enter/Reset [pull-up, hold 3s = session reset]

P2 HEADER (left side):
  GPIO13   → Passive buzzer (+) via 100Ω resistor [PWM]
  GPIO12   → Vibration motor via 2N2222 + 1kΩ + 1N4148 [digital]
  GPIO02   → Piezo analog signal              [ADC1_CH1]
  GPIO11   → Reserved: MAX30102 INT (optional, for power-saving)
  GPIO10   → Reserved: future use
  GPIO03   → Reserved: future use
  GPIO01   → Reserved: future use

I2C BUS (SDA=18, SCL=17, 100kHz):
  0x57   MAX30102 (heart rate / SpO2 / contact)
  0x68   MPU6050  (accel + gyro)
  0x76   BMP280   (pressure / altitude)
```

---

## Wiring details and circuits

### I2C bus layout (3 sensors in parallel)

All three I2C devices share the same SDA and SCL lines. They each have their own unique address so the master (ESP32) addresses them individually. The bus topology looks like:

```
ESP GPIO18 (SDA) ─┬─ MPU6050 SDA
                  ├─ MAX30102 SDA
                  └─ BMP280 SDA

ESP GPIO17 (SCL) ─┬─ MPU6050 SCL
                  ├─ MAX30102 SCL
                  └─ BMP280 SCL

3V3 rail ────────┬─ MPU6050 VCC + 0.1µF cap to GND
                 ├─ MAX30102 VIN + 0.1µF cap to GND
                 └─ BMP280 VCC + 0.1µF cap to GND
                 (plus one 100µF electrolytic across the rail)

GND rail ────────┴─ All sensors GND
```

**Why the 0.1µF capacitors:** Each I2C sensor draws current in short bursts during operation. Without local decoupling caps, these bursts cause voltage dips on the shared 3V3 rail, which can corrupt I2C data being transferred at the same moment. The 0.1µF cap acts as a tiny local battery that smooths these dips. Place each cap as close as physically possible to the sensor's VCC pin — short leads matter.

**Why the 100µF electrolytic:** The MAX30102's IR LED draws ~30mA during pulses, much more than the steady-state current. The electrolytic provides bulk capacitance for these larger transient demands without affecting the rest of the bus.

### Vibration motor circuit (transistor switch)

The 10mm coin motor draws 68-110mA. This exceeds the ESP32-S3 GPIO maximum of 40mA, so direct drive would damage the pin. We use an NPN transistor as a switch:

```
                  3V3
                   │
                   │
                   ◯  ← Vibration motor
                   │
                   ┃
              ──┤──┃   ← 1N4148 diode (cathode/banded end UP, anode DOWN)
              cathode↑ ┃     across the motor terminals
              ──┤──┃
                anode↓
                   │
                   ┃← Collector (pin 3)
                   ┃
GPIO12 ──[1kΩ]──── ┃ Base (pin 2) — 2N2222
                   ┃
                   ┃← Emitter (pin 1)
                   │
                  GND
```

**Pin order on 2N2222 (TO-92 package, flat side facing you):**
- Left pin = Emitter → GND
- Middle pin = Base → 1kΩ resistor → GPIO12
- Right pin = Collector → motor low side

**Why each component:**
- **1kΩ resistor on the base:** Limits current from GPIO into the transistor base. Without it, the GPIO would deliver too much current and burn out.
- **1N4148 diode (flyback):** When GPIO12 goes LOW and the motor switches off, the motor's coil collapses its magnetic field and generates a reverse-voltage spike (could be 50V+ for a moment). The diode shorts this spike harmlessly back across the motor. Without this diode, the spike kills the transistor over time.
- **Diode polarity:** Cathode (banded end) toward 3V3, anode toward the motor's low side. The diode is reverse-biased during normal operation (no current flow), and only conducts when the spike happens.

### Buzzer circuit

```
GPIO13 ──[100Ω]──── Buzzer (+) ──── Buzzer (-) ──── GND
```

The 100Ω resistor limits inrush current. The buzzer is passive (no internal oscillator), so the firmware uses `tone(13, frequency)` to generate audio. Different frequencies produce different sounds — gentle ding at 800Hz for goals, urgent 2kHz pulses for fall alerts, melodic patterns for milestones.

### Piezo analog circuit

```
Piezo pin 1 ──── GPIO02 (ADC1_CH1)
Piezo pin 2 ──── GND
```

Piezo elements generate their own voltage when stressed (no external power needed). The ESP32 ADC reads the voltage spike directly. We threshold this in firmware: ADC value > X = tap detected, value > Y = strong impact (potential fall confirmation).

### Power switch

```
LiPo battery (+) ──── Switch ──── ESP32 battery input (+)
LiPo battery (-) ──────────────── ESP32 battery input (-)
```

A simple SPST switch on the positive line cuts power when off. Saves the JST connector from wear and gives a real "off" experience.

---

## Wiring sequence (incremental, with verification)

**Do not skip steps.** Each step is gated by the I2C scanner running successfully before moving to the next. This isolates problems early.

### Step 1: Power rails
- Bridge the two breadboards' power rails with short jumpers (red-to-red, blue-to-blue)
- Run 3V3 from ESP P1 → red rail
- Run GND from ESP P1 → blue rail
- Verify with multimeter: 3.3V between red and blue rail

### Step 2: First I2C sensor (MPU6050, already done from v5.2)
- Connect VCC → red rail, GND → blue rail
- Connect SDA → GPIO18, SCL → GPIO17
- Add 0.1µF cap across MPU's VCC and GND
- Flash `proto3_i2c_scanner.ino`
- Confirm: scanner shows `0x68 -> MPU6050`

### Step 3: Add BMP280
- Plug into breadboard near MPU
- Wire VCC → red rail, GND → blue rail
- Wire SDA → I2C bus column, SCL → I2C bus column
- Add 0.1µF cap across BMP's VCC and GND
- Leave CSB and SDO disconnected (defaults to I2C mode at 0x76)
- Re-run scanner
- Confirm: `0x68 -> MPU6050` AND `0x76 -> BMP280`

### Step 4: Add MAX30102 (when replacement arrives)
- Solder ONLY 4 pins this time: VIN, SDA, SCL, GND (not the optional INT/RD/IRD/3V3)
- Plug in with chip facing up
- Wire VIN → red rail, GND → blue rail
- Wire SDA → I2C bus column, SCL → I2C bus column
- Add 0.1µF cap across VIN and GND
- Add 100µF electrolytic across the 3V3 rail nearby
- Re-run scanner
- Confirm: `0x57 -> MAX30102` AND `0x68 -> MPU6050` AND `0x76 -> BMP280`

### Step 5: Buzzer
- Insert buzzer with + pin in line with a free row, - pin in another row
- Wire + pin → 100Ω resistor → GPIO13
- Wire - pin → blue rail (GND)
- Test with a `tone(13, 1000); delay(500); noTone(13);` sketch — should beep

### Step 6: Piezo
- Insert piezo with one pin to GPIO02, other pin to blue rail
- Test with `Serial.println(analogRead(2))` — values should jump when you tap the piezo, sit near 0 when still

### Step 7: Vibration motor circuit
- Place 2N2222 transistor on breadboard, flat side facing you
- Wire emitter (left) → blue rail (GND)
- Wire base (middle) → 1kΩ resistor → GPIO12
- Wire collector (right) → motor (-)
- Wire motor (+) → red rail (3V3)
- Wire 1N4148 diode across motor: cathode (banded) to (+), anode to (-)
- Test with `digitalWrite(12, HIGH); delay(500); digitalWrite(12, LOW);` — should buzz

### Step 8: Power switch
- Cut the LiPo (+) wire path
- Insert switch in series
- Solder both ends to switch terminals
- Verify ESP powers on/off as switch is flipped

### Step 9: GPS (optional, if proceeding with GPS in proto 3)
- Wire GPS VCC → red rail, GND → blue rail
- Wire GPS TX → GPIO44 (ESP RX)
- Wire GPS RX → GPIO43 (ESP TX)
- Test with NMEA parsing sketch outdoors — should get fix in 30-60 seconds

### Step 10: Buttons (already done from v5.2)
- Verify they still work as expected

---

## Firmware architecture changes for v6

The firmware will inherit v5.2's structure (MPU robustness, button handling, BLE, session reset) and add:

- **MAX30102 driver** (SparkFun library): heart rate, SpO2, contact detection, IR signal quality
- **BMP280 driver** (Adafruit library): pressure → altitude conversion, floor counting via altitude delta
- **Piezo threshold detector**: continuous ADC sampling, threshold-based tap and impact detection
- **Buzzer tones**: dedicated alert function with pre-defined patterns (`tone_goal()`, `tone_fall()`, `tone_lowbat()`, etc.)
- **Vibration motor patterns**: similar pattern library for tactile alerts
- **Multi-modal alerts**: severity-based combinations (low = vibration only, med = vibration + soft tone, high = vibration + loud + display)
- **New display screens**: SpO2 reading, elevation card, alert history
- **BLE service expansion**: add SpO2 characteristic, altitude characteristic, alert events characteristic

---

## Open questions / decisions still pending

1. **GPS in proto 3 — keep or skip?** Currently leaning keep, for fall-location logging when phone is out of range. Adds 50mA power draw which affects battery life noticeably.

2. **MAX30102 INT pin wiring** — adds a free GPIO11 wire but enables sleep-between-readings power saving. Worth it for v6, or defer to v7?

3. **Vibration motor patterns** — what library of tactile alerts to standardize on? Need to research what elderly users find comfortable vs alarming.

4. **Form factor next step** — once proto 3 firmware works, design custom PCB to replace breadboard. This is the path to actual wearable form factor.

---

## Status checklist

- [x] BMP280 soldered and ready
- [x] MAX30102 soldered (orientation issue, replacement on order)
- [x] Buttons placed
- [x] Piezo on hand
- [x] Buzzer on hand
- [x] Vibration motor on hand
- [ ] Replacement MAX30102 (ordered, ~2 days)
- [ ] 2N2222 transistors (to order)
- [ ] 1N4148 diodes (to order)
- [ ] 1kΩ resistors (to order)
- [ ] 100Ω resistors (to order)
- [ ] 0.1µF ceramic capacitors x10 (to order)
- [ ] 100µF electrolytic capacitor x2 (to order)
- [ ] Toggle/slide switch (to order)
- [ ] Spare breadboard (to order)

- [ ] Step 1: Power rails verified
- [ ] Step 2: MPU6050 wired with cap
- [ ] Step 3: BMP280 added, scanner shows 0x68 + 0x76
- [ ] Step 4: MAX30102 added, scanner shows 0x57 + 0x68 + 0x76
- [ ] Step 5: Buzzer wired and tested
- [ ] Step 6: Piezo wired and tested
- [ ] Step 7: Vibration motor circuit assembled and tested
- [ ] Step 8: Power switch installed
- [ ] Step 9: GPS wired (decision pending)
- [ ] Step 10: Buttons verified
- [ ] Firmware v6 written
- [ ] Field test 1 (indoor)
- [ ] Field test 2 (park walk)
- [ ] Field test 3 (extended battery test)

---

## Approximate cost (proto 3 upgrade only)

| Item | Cost (RM) |
|------|-----------|
| MAX30102 replacement | 13.50 |
| 2N2222 transistors x3 | 0.60 |
| 1kΩ resistors x5 | 0.65 |
| 100Ω resistors x5 | 0.65 |
| 1N4148 diode pack | 1.00 |
| 0.1µF ceramic cap x10 | 1.00 |
| 100µF electrolytic cap x2 | 0.60 |
| Toggle switch | 1.00 |
| Spare breadboard | 6.00 |
| **Total proto 3 upgrade** | **~25 RM** (~$5.30 USD) |

Total project cost to date is well under RM200, including the original LilyGO board and all sensors across both prototypes.

---

*This README lives at `/proto3/README.md` in the GitHub repo.*
*Last updated: build session, 2026.*
