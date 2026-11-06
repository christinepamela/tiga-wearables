# TIGA — Wearable Health Tracker

An open-source health-tracking wearable designed for people whose health needs more attention than a typical adult — older adults living with chronic conditions, people recovering from illness, and anyone whose family wants reassurance they're okay. Built by an indie maker, for family.

## Project goal

Give wearers and their families meaningful, real-time health insight in a device that is simple to wear, beautiful to look at, and built with care. Treat the wearer as a capable adult — not a patient.

Primary user: adults 55 and above, or anyone with caretaking needs regardless of age. Read [`docs/DESIGN.md`](docs/DESIGN.md) for the full design philosophy.

## Core features

- Continuous heart rate and SpO2 monitoring (MAX30102)
- Fall detection and step counting (MPU6050)
- Altitude tracking for floors climbed (BMP280)
- GPS tracking for outdoor walks (NEO-6M)
- Tap-to-wake and tap-to-cycle via piezo
- Tactile alerts via vibration motor
- Audible alerts via passive buzzer
- Companion PWA for the wearer's phone and for caregivers
- Doctor-ready PDF reports
- Future: analog watch face system, multiple face designs, BLE pairing with mobile app

## Repository structure

```
tiga-wearables/
├── proto-1/    First breadboard prototype (archived)
├── proto-2/    Sensor exploration build (archived after v5.2 firmware)
├── proto-3/    Active build (current)
├── app-pwa/    Companion app, all versions (v0.1 to v0.4)
├── docs/       Design philosophy, brand identity, technical specs
└── README.md   This file
```

## Documentation

| Document | Purpose |
|----------|---------|
| [`docs/DESIGN.md`](docs/DESIGN.md) | Design philosophy — who we design for, what we want users to feel, what we avoid |
| [`docs/BRAND.md`](docs/BRAND.md) | Visual identity — wordmark, typography, color, watch design direction, photography style |
| [`docs/WATCH-FACES.md`](docs/WATCH-FACES.md) | Spec for the analog watch face system on the wearable |
| [`docs/PROTO3-WIRING.md`](docs/PROTO3-WIRING.md) | Wiring sequence and circuit diagrams for proto 3 |
| [`docs/TECHDEBT.md`](docs/TECHDEBT.md) | Backlog of deferred work across app, firmware, hardware |

## Build stages

| Stage | Status | Description |
|-------|--------|-------------|
| Proto 1 | Archived | First breadboard attempt — explored sensors, display, basic UI |
| Proto 2 | Archived | Clean rebuild with corrected pin map, robust MPU6050 handling, full firmware framework (v5.2) |
| Proto 3 | **Active** | Sensor expansion — MAX30102 (HR/SpO2), BMP280 (altitude), passive buzzer, vibration motor, piezo, power switch |
| Proto 4 | Planned | Custom PCB, round display, sensor evaluation (light, IR temp, GSR), battery optimization for watch form factor |
| Enclosure | Planned | 3D-printed case, then injection-molded titanium for production |
| Fabrication | Future | Gerber export, vendor selection, pilot run |

## Hardware (Proto 3)

### Main board
- LilyGO T-Display-S3 (ESP32-S3R8 + 1.9" ST7789 LCD)

### Sensors (all I2C, sharing SDA on GPIO18 and SCL on GPIO17)
- MPU6050 accelerometer/gyroscope — address 0x68
- MAX30102 pulse oximeter and heart rate sensor — address 0x57
- BMP280 barometric pressure and altitude sensor — address 0x76
- NEO-6M GPS module (UART on GPIO43/44)

### Inputs
- Piezo vibration sensor module (analog on GPIO02) — for tap detection
- Two tactile buttons (GPIO16, GPIO21) — for navigation and SOS

### Outputs
- Passive buzzer 3-12V (GPIO13 via 100Ω resistor) — for audible alerts
- 10mm flat coin vibration motor (GPIO12 via 2N2222 + 1kΩ + 1N4148) — for tactile alerts

### Power
- 3.7V 1000mAh LiPo battery
- SPDT slide switch inline with battery (+)
- USB-C charging via LilyGO board

### Decoupling
- 0.1µF ceramic capacitors across VCC/GND at each I2C sensor
- 100µF electrolytic capacitor across the 3V3 rail near MAX30102 for bulk power buffering

## Firmware

Current version: **v5.2** (proto 2 firmware, runs on proto 3 hardware with new components stubbed)

Next version: **v6** (in development) — full proto 3 sensor integration, foundation of analog watch face system, expanded BLE service, multi-modal alerts.

Firmware is written in **C++** using the Arduino framework on the ESP32 Arduino core.

See [`proto-2/arduino/tiga_main_v5_2/`](proto-2/arduino/tiga_main_v5_2/) for the current firmware.

## Companion app

A Progressive Web App (PWA) built with vanilla JavaScript, HTML, and CSS. No framework. Single-file deployment. Designed for iPhone Safari and Bluefy browser (for Web Bluetooth support on iOS).

Current version: **v0.4** at [`app-pwa/v04/`](app-pwa/v04/).

Features include:
- Personalized greetings
- Walk tracker with live duration, steps, distance, heart rate
- Leaflet maps with OpenStreetMap (free, no API key)
- 19-metric Advanced sensor readout with plain-English explanations
- Six self-tests (balance, mobility, strength, reaction time)
- Medical info logging (blood sugar, mood, symptoms, medications)
- PDF report export for doctors
- Caregiver mode with binary status
- Bitchat link for free private chat with family/friends

## Languages used

- **Firmware:** C++ (Arduino framework on ESP32 Arduino core)
- **Companion app:** JavaScript, HTML, CSS (vanilla, no framework, PWA)

## Design and brand

This project takes design seriously. The wearable is meant to be giftable — something a daughter can give her mother and feel proud of, something the wearer can wear without feeling labeled as "frail" or "patient." See [`docs/DESIGN.md`](docs/DESIGN.md) and [`docs/BRAND.md`](docs/BRAND.md).

## Build philosophy

We design for trust, dignity, and calm. We avoid medical-tracker coldness, fitness-app aggression, and childishness. We embrace whitespace, restraint, and depth on demand. We treat the wearer as a capable adult.

## License

Open source. Specific license TBD — likely MIT for software, CC-BY-SA for documentation and design files.

## Acknowledgments

Built by Pam for her mom Agnes, and everyone like her.

---

*Built with care. Not a medical device. Always consult your doctor for medical decisions.*
