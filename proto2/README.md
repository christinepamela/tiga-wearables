# Proto 2 — Active Build

Clean rebuild from scratch with corrected wiring, proper pin assignments, and a complete test menu designed around elderly users.

## Confirmed pin map

| Function | GPIO | Notes |
|---|---|---|
| MPU6050 SDA | GPIO18 | LilyGO confirmed I2C SDA |
| MPU6050 SCL | GPIO17 | LilyGO confirmed I2C SCL |
| Pulse sensor Signal | GPIO01 | ADC1_CH0 — analog |
| SW-420 Vibration DO | GPIO03 | Digital input |
| TTP223 Touch I/O | GPIO13 | Digital input |
| Button 1 (Scroll) | GPIO21 | INPUT_PULLUP |
| Button 2 (Enter) | GPIO16 | INPUT_PULLUP |
| Battery voltage | GPIO04 | Internal ADC — LCD_BAT_VOLT |
| LCD power enable | GPIO15 | Must set HIGH for battery use |
| GPS TX (reserved v3) | GPIO43 | Freed from sensors |
| GPS RX (reserved v3) | GPIO44 | Freed from sensors |

## Files

- `arduino/tiga_diagnostic/` — sensor diagnostic sketch (run first)
- `arduino/tiga_main/` — full application (in progress)
- `kicad/` — corrected schematic (in progress)
- `docs/TIGA_prototype_build.html` — living build document

## Build sequence

1. ✅ Rewire breadboard (3 wires moved)
2. 🔄 Flash diagnostic sketch — confirm all sensors alive
3. ⬜ Full integration code
4. ⬜ Complete UI + test menu
5. ⬜ User test session (mom)
6. ⬜ KiCad schematic rebuild
7. ⬜ Daughterboard PCB design
8. ⬜ 3D enclosure
9. ⬜ Fabrication

## Key fix from Proto 1

`pinMode(15, OUTPUT); digitalWrite(15, HIGH);` must be in `setup()` for display to work on battery power.
