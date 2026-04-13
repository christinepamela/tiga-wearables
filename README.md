# TIGA — Wearable Health Tracker for the Elderly

A low-cost, open-source wearable health tracker designed specifically for elderly users managing CVD, hypertension, and diabetes. Built by an indie maker, for family.

## Project goal

Give elderly users — and their families — meaningful, real-time health insight in a device that is simple to wear, easy to read, and built with love.

Core features: heart rate monitoring, fall detection, step counting, touch-based dexterity tests, and emergency SOS.

## Repository structure

```
tiga-wearables/
├── proto-1/    First breadboard prototype (archived)
├── proto-2/    Current active build
└── README.md   This file
```

## Build stages

| Stage | Status | Description |
|---|---|---|
| Proto 1 | Archived | First attempt — explored sensors, display, basic UI |
| Proto 2 | Active | Clean rebuild with corrected pin map, full test menu, diagnostic tools |
| Daughterboard | Planned | Custom PCB plugging into T-Display-S3 headers |
| 3D Enclosure | Planned | Bracelet or watch form factor based on user feedback |
| Fabrication | Future | Gerber export, vendor selection, pilot run |

## Hardware (Proto 2)

- LilyGO T-Display-S3 (ESP32-S3R8 + 1.9" ST7789 LCD)
- MPU6050 accelerometer/gyroscope
- Pulse sensor (pulsesensor.com analog)
- TTP223 capacitive touch sensor
- SW-420 vibration/tilt sensor
- 2× tactile buttons
- 3.7V 1000mAh LiPo battery

## Target user

Elderly adults with cardiovascular conditions. The device prioritises safety alerts, fall detection, and simple UI over performance metrics.

---
*Built with care. Not a medical device.*
