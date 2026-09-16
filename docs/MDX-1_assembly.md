# MDX-1 assembly

How to populate the main board (and UI board) for a first article. Read this together with the [staged bring-up procedure](MDX-1_bringup.md): for a first build you do **not** stuff the whole board at once — you assemble and verify in stages, leaving the transmit, high-voltage and peripheral sections unpopulated until the low-voltage core is proven. This document covers the mechanics of building; the bring-up doc covers the order and the tests.

> **Two ways to build.** A **full stuff** (all parts, then bring-up gated only by the firmware enable lines) is faster but puts the 100 V stage on the board from the first power-on. A **staged stuff** (populate per bring-up stage) is the recommended first-article path: a fault is contained to the last stage you added, and the 100 V section physically cannot energize until you fit it. Choose the staged path unless you have good reason not to.

## Boards

| Board | Size | Layers | Population |
|---|---|---|---|
| Main (`mdx1.kicad_pcb`) | 135.1 × 92.1 mm | 4 | **Single-sided** SMD (all parts on top); through-hole connectors and the two electrolytics from the top |
| UI (`mdx1_ui.kicad_pcb`) | 82.1 × 60.1 mm | 2 | Top: LCD, keypad switches, LEDs, front-light, connector. Bottom: the board-to-board FFC only |

Because the main board is single-sided SMD, it reflows in one pass with no bottom-side skips or glue.

## Build order

1. **Solder-paste** the main board from a stencil (the `fab/` package includes the paste layers). Standard SAC305 paste; the finest pitch is the QFN/BGA-adjacent ICs — U19/U20 (ADCs) and U22 (STM32H743, LQFP) — so align carefully.
2. **Place SMD**, heaviest/finest first (ICs, then passives, then small connectors). Watch the polarised and pin-1 parts called out below.
3. **Reflow** to the SAC305 profile.
4. **Inspect** before power: pin-1 orientation on every IC, the BAS116 diodes (see the trap below), no bridges on the ADC/MCU fine pitch, and the two boost/HV FET areas.
5. **Hand-solder the through-hole parts** last: USB-C (J1) if the chosen part is THT, the coil header (J4), the two electrolytics (**C31**, **C37** — tall radials), microSD (J6), audio jack (J8), battery/speaker JSTs (J3/J9), SWD header (J5). Support the tall electrolytics; C37 stands ~20 mm and is the tallest part on the board.
6. **Clean** (the front end is high-impedance — flux residue there degrades noise performance; a no-clean process still benefits from a wash under U15–U18).
7. **UI board**: populate the top (LCD connector, keypad switches, LEDs, front-light), then the FFC on the bottom.

For a **staged first article**, stop after the parts belonging to the current bring-up stage (see the bring-up doc) and power up before adding the next stage's parts.

## Critical placement notes

- **BAS116 diodes D10–D13 (SOT-23) — pin-map trap.** The BAS116 SOT-23 pinout is **pad 1 = cathode, pad 2 = no-connect, pad 3 = anode.** The generic SOT-23 diode footprint lands the anode net on pad 2. **The anode net must reach pad 3**, not pad 2 — verify the footprint's pad map (or hand-wire the anode to pad 3) before trusting these clamps. This is the one known footprint caveat on the board; check it at inspection.
- **VBRIDGE bank ceramics C38 / C39 / C48** are **2.2 µF 250 V in 2220** (large ceramic). They are stiff; keep reflow profile within the part's spec and avoid board flex near them after assembly (2220 ceramics crack on bend).
- **C37 = 47 µF 160 V electrolytic**, radial, ~20 mm tall — the VBRIDGE bulk. Observe polarity; it is the tallest part and sets the enclosure clearance.
- **C31 electrolytic** (VTX bulk) — polarised radial, fit from the top.
- **Power FETs**: Q1 (VTX boost), Q2 (VBRIDGE boost), Q4–Q7 (H-bridge), Q3 (HV bleed) — confirm pin 1 / source-drain orientation; these carry the switching currents.
- **Inductors**: L1 (3V3), L2/L3 (±/− analog), L4 (VTX 22 µH), L5 (VBRIDGE 47 µH) — L4/L5 are the power inductors; make sure the correct value goes in each spot.
- **Coil header J4**: 1×6 2.54 mm header. Pinout COIL_TX+/TX−/RX+/RX−/GND/COIL_ID. Leave **pin 6 (COIL_ID)** unpopulated on the mating side if you use a third-party coil (no ID chip).
- **MCU (U22)** decoupling: the 100 nF/1 µF cluster (C117–C126) sits tight to the VDD pins — do not omit or relocate.

## Mounting

- **Main board**: 4 × M3 (Ø3.2 mm holes) on a 128 × 85 mm rectangle, 3.55 mm inset from the board edges. Use standoffs tall enough to clear C37 (≥ 20 mm on that side) and the bottom-side FFC tail if routed under.
- **UI board**: 4 × M2.5 (Ø2.7 mm holes) on a 75 × 53 mm rectangle. The LCD adheres to the front of the UI board (3M VHB); the memory-LCD glass is 1.64 mm thick, the FH12 FFC connector 2.0 mm, keypad switches 4.30 mm tall — account for these in the front-panel gap.
- DXF outlines for both boards and the LCD are in `hardware/mdx1/fab/` for the enclosure model.

## Before power

- Verify no solder bridges on the ADC and MCU fine pitch.
- Confirm all polarised parts (electrolytics, diodes, FETs) and every IC pin-1.
- For a staged build, confirm the **TX/HV/peripheral sections are absent** as the current bring-up stage requires.
- Then follow the [staged bring-up](MDX-1_bringup.md) — always from a **current-limited** supply.
