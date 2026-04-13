# Proto 1 — Archived

First breadboard prototype. Explored sensors, display integration, and basic UI structure.

## What was built
- Basic state machine (splash → main → test menu)
- MPU6050 integration (I2C)
- Pulse sensor (analog)
- TFT display with 2×2 health card layout
- Button navigation
- Bitchat stub (BLE placeholder)
- KiCad schematic v4 (had pin conflicts — corrected in Proto 2)

## Known issues (fixed in Proto 2)
- GPIO43/44 used for sensors — conflict with UART0 (Serial debug)
- Schematic used MAX30102 and IIS30WB — didn't match actual bench components
- Battery % was simulated, not read from ADC
- IO15 not set HIGH — display wouldn't work on battery power

## Files
- `arduino/` — original .ino and helper files
- `kicad/` — schematic and PCB layout v4
- `photos/` — breadboard photos from first build session

*Archived October 2025. Do not use as reference for new builds.*
