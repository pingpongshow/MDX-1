# MDX-1 staged bring-up

First-power and commissioning procedure for the main board. The design has two independent means of keeping a stage inert: **firmware enable lines** (ANA_EN, VTX_EN, HV_EN, DRV_EN — all default off at reset) and **leaving parts unpopulated**. During first bring-up, use both: assemble the board in stages, populating and verifying one subsystem at a time, so a fault is contained to the stage you just added.

> **Safety.** The transmit stage switches up to **100 V** into an inductive load and stores energy in the VBRIDGE bank. Do not populate the 100 V stage until everything below it is verified. Always power the board from a **current-limited bench supply** (start at ~200 mA) through the battery input or a bench 3.7 V source, not a full battery, until Stage 3. Discharge the VBRIDGE bank (bleed) before touching the TX section. Wear eye protection once the HV stage is live.

## Enable lines vs depopulation

| Stage gated by | How it's held off | Hard or soft |
|---|---|---|
| ANA_EN → ±12 V / −6 V (U4) | GPIO, off at reset | soft |
| VTX_EN → VTX boost (U8) | GPIO, off at reset | soft |
| HV_EN → VBRIDGE 100 V boost (U9) | GPIO, off at reset | soft |
| DRV_EN → gate drivers (U10/U11) | GPIO, off/floating handling | soft |
| Any stage | **leave its parts unpopulated** | **hard** |

Firmware keeps stages off until commanded; depopulation guarantees a stage *cannot* energize regardless of firmware or a short. For the dangerous stages (VTX, VBRIDGE, H-bridge) use the hard method on the first build.

## Build/bring-up stages

Assemble and verify in this order. Do not proceed to the next stage until the current one passes.

### Stage 0 — Power input + digital rail + MCU (minimal core)

**Populate:** USB-C J1, ESD U2-side (USBLC6 J2), input TVS/CC resistors; charger **U1** (BQ24075) and its passives; **U3** (TPS63001) + **L1** → +3V3; **U22** (STM32H743) with its decoupling, VDDA ferrite, VCAP caps, 16.384 MHz crystal, 32.768 kHz crystal, BOOT0 pulldown, NRST; SWD header **J5**. Optionally the fuel gauge **U2** (BQ27441).

**Leave unpopulated:** everything analog, TX, HV and peripheral — U4, U5, U6, U7, U8, U9, U10, U11, U19, U20, U21, Q1, Q2, Q4–Q7, L4, L5, and all peripherals (U23–U27).

**Verify:**
- VBUS present at USB-C; VSYS at U1 OUT (≈ battery/USB voltage).
- +3V3 within tolerance at U3 output and at the MCU VDD pins.
- VDDA present (through the ferrite bead); VCAP1/VCAP2 ≈ 1.2 V once the core runs.
- SWD connects (ST-Link/J-Link at J5); flash a blink/heartbeat; USB enumerates.
- Current draw sane (tens of mA).

### Stage 1 — Analog rails, reference, ADCs

**Add:** **U4** (TPS65131) + **L2/L3** + **D2/D3** → +12 V / −6 V; **U5** (LT3045 → +5VA); **U6** (LT3094 → −5VA); **U7** (TPS7A2033 → +3V3A); **U21** (REF5025 → VREF2V5); +5V_LCD dropper (10R + cap); **U19/U20** (ADS127L01) with straps and clocks.

**Verify (bring ANA_EN up under firmware):**
- +12.0 V and −6.0 V from U4 (check the feedback set-points).
- +5VA and −5VA from the LDOs; low noise (this is the point of the LT3045/3094 — do not add trimmers here).
- +3V3A and VREF2V5 = 2.500 V.
- MCO2 = 16.384 MHz clock to the ADCs; ADC frame-sync/daisy-chain data streaming into the SAI.

### Stage 2 — RX front end

**Add:** **U15** (OPA1612), **U16/U17** (THS4551 FDAs), **U18** (OPA1656) and the VOCM divider, AC-coupling, BAS416/BAS116 clamps, feedback and anti-alias networks.

**Verify:**
- VOCM ≈ 1.654 V at the FDA common-mode pins.
- Inject a small test signal at the coil RX pins; confirm it appears in the demodulated lines at the expected gain (×20).

### Stage 3 — TX rail (VTX), low voltage first

**Add:** **U8** (LT3757) + **L4** + **Q1** (SiR680ADP) + **D4** (B560C) + VTX bulk caps (C31/C32/C33).

**Verify:**
- Enable VTX with the DAC set for the **lowest** rail first (recall VTX = 29.8 V at 0 V DAC, 7.9 V at 3.3 V DAC — so command a high DAC value to start low), then sweep up.
- Rail follows the DAC across 7.9–29.8 V; input current reasonable; loop stable (scope the rail, no oscillation — this is where the VTX loop-comp R29/C30 may need tuning).

### Stage 4 — TX driver and H-bridge

Do this in **two sub-steps** so you validate gate timing before any power device can conduct.

**4a — gate drivers only:** Add **U10/U11** (UCC27714), gate resistors (4R7), bootstrap diodes/caps, **U12** (INA241 current sense), **U13/U14** (TLV3501 OC comparators). **Leave Q4–Q7 unpopulated.**
- Verify HI/LI → HO/LO logic and the dead-time; confirm DRV_EN gates the outputs; confirm the OC comparator thresholds (R56/R57 → 4.5 V, R58/R59 → 0.5 V) and the TX_FAULT wired-OR into TIM1 BKIN.

**4b — power stage:** Populate **Q4–Q7** (BSC320N20NS3) and the snubber (R61/C47). Drive into a **dummy resistive/inductive load or the coil at low VTX**.
- Scope the switch node (SW_B, COIL_TX): clean edges, no shoot-through, dead-time correct. Tune the snubber (R61/C47) for acceptable ring; tune gate resistors if needed.
- Verify current sense (ITX_SENSE from U12) tracks coil current; verify OC trip shuts the bridge via TX_FAULT.

### Stage 5 — HV / pulse-induction (100 V) — last and most hazardous

**Add:** **U9** (LT3757) + **L5** (47 µH) + **Q2** (BSC190N15NS3) + **D5** (STPS2150) + VBRIDGE bulk caps (**C37 47 µF, C38/C39/C48 2.2 µF 250 V**) + bleed FET **Q3**.

**Verify:**
- With HV_EN, bring VBRIDGE up **gradually** (start with a reduced set-point if you fitted a fail-safe divider option; otherwise watch it ramp). Confirm ≈ 100 V, loop stable (HV loop-comp R39/C36).
- Confirm the **bleed** (Q3/HV_BLEED) discharges the bank in ~1.4 s when HV is disabled — test this before trusting the section.
- Run PI bursts at low VBRIDGE first, then full: confirm reversal waveform, decay sampling on the RX winding, and that the firmware auto-shutdown/OC protections fire correctly.

### Stage 6 — Peripherals

**Add:** **U23** (LSM6DSO32 IMU), **U24** (TPA6132 headphone amp), **U25** (PAM8302 speaker amp), **U26** (BM83 Bluetooth), **U27** (MAX-M10S GNSS) + external active antenna via **J13** (u.FL), the microSD (J6), and connect the UI board (LCD, keypad) over the FFC.

**Verify:** I²C IMU responds; audio out both paths; BLE advertises and the phone app connects; GNSS fix; microSD mounts; LCD draws and keypad reads (UI_DET detection).

## Nodes to probe and things you will likely tune

Keep a scope and DMM on these during the relevant stage:

- **Rails:** VSYS, +3V3, +12 V, −6 V, +5VA, −5VA, +3V3A, VREF2V5, VTX, VBRIDGE.
- **Switch nodes:** SW3V3, SW_POS/SW_NEG, SW_VTX, SW_HV, COIL_TX+/−, SW_B.
- **Analog:** VOCM, ITX_SENSE, OC_THR_H/L.
- **Likely tuned empirically:** the **TX snubber** (R61/C47), the **loop compensation** (VTX R29/C30, VBRIDGE R39/C36), the **gate resistors**, and the **OC thresholds**. These are 0402/1206/2512 and can be reworked without a respin; see the design-review notes on adding option pads for these in a future revision.

## Do-not-adjust nodes

Leave these fixed — a trimmer or casual swap degrades performance:

- LT3045/LT3094 SET resistors (R25/R26) — ultra-low-noise LDOs; a trimmer adds noise/drift.
- RX preamp gain (R71/R72, R73) — precision, matched; calibrated in firmware.
- REF5025 (VREF2V5) — precision reference.
- VTX feedback (R30/R31/R32) — the rail is already DAC-steered from firmware.
