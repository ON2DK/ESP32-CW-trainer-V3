# ESP32 CW Trainer V3

Standalone ESP32-based CW/Morse trainer PCB for paddle and straight-key practice.

## V3 hardware revision

V3 standardizes the external connectors on real PCB-mounted parts instead of generic connector placeholders:

- **J4 DC input:** GCT DCJ200-10-A horizontal THT barrel jack
- **J10 Paddle:** CUI/Same Sky SJ1-3533NG 3.5 mm TRS jack
- **J11 Audio/headphone:** CUI/Same Sky SJ1-3533NG 3.5 mm TRS jack
- **J14 Straight Key:** CUI/Same Sky SJ1-3533NG 3.5 mm TRS jack

### Jack wiring

| Connector | Tip | Ring | Sleeve |
|---|---|---|---|
| J10 Paddle | DIT | DAH | GND |
| J11 Audio | Audio via 100 ohm | Audio via 100 ohm | GND |
| J14 Straight Key | STRAIGHT_KEY | Not connected | GND |

J4 uses pin 1 for the DC input to F1, pin 2 for GND, and pin 3 (internal switch contact) is intentionally unused.

## Main hardware

- ESP32-WROOM-32 / 38-pin DevKit footprint
- TFT interface
- Paddle input
- Straight-key input
- Rotary encoder with push switch
- LM386 audio amplifier
- Buzzer
- Volume control
- 12 V external input
- 500 mA resettable fuse
- TRACO TSR 1-2450 5 V regulator

## Design status

As of **19 September 2026**:

- KiCad: **10.0.4**
- ERC: **0 errors, 0 warnings**
- DRC: **0 violations**
- Unconnected PCB items: **0**
- Schematic parity problems: **0**
- Gerber production set regenerated after the V3 connector changes
- Hardware assembly/testing: **pending**

Two intentional ERC exceptions remain documented in the project: the project-specific ESP32 symbol/library difference and the GPIO34 input-only ADC check.

## Production files

The clean all-THT Gerber package contains:

- F.Cu / B.Cu
- F.Mask / B.Mask
- F.Silkscreen / B.Silkscreen
- Edge.Cuts
- PTH drill
- NPTH drill
- .gbrjob

Paste layers are not required for this THT board.

## Recommended repository files

- KiCad schematic (`.kicad_sch`)
- KiCad PCB (`.kicad_pcb`)
- KiCad project (`.kicad_pro`)
- Gerber ZIP
- Arduino firmware (`.ino`)
- Project logbook with BOM and photos
- PCB / schematic / 3D images

## Notes

Before ordering, inspect the final Gerbers in the PCB manufacturer's viewer, especially J4, J10, J11, J14, mounting holes and Edge.Cuts.

This project is currently electrically/design-rule checked, but the V3 hardware has not yet been validated on an assembled PCB.
