# MDX-1

**An open, simultaneous multi-frequency + pulse-induction metal detector — hardware, search coil and firmware.**

MDX-1 is a from-scratch detector design: a KiCad schematic and 4-layer main board, a 2-layer user-interface board, a hand-wound search coil, portable C firmware with a host simulator and test bench, and iOS/Android companion apps. The receive and transmit engine is specified around a coherent, DSP-first architecture:

- **Five simultaneous VLF frequencies** — 5.12, 10.24, 15.36, 20.48 and 40.96 kHz (harmonics 1, 2, 3, 4, 8 of a 5.12 kHz fundamental) from a single optimised 3-level look-up-table waveform, so every line falls exactly on a DFT bin and demodulates with one coherent transform.
- **Two 24-bit, 512 kSPS converters** — one digitises the receive coil, the second digitises the transmit current for exact V<sub>RX</sub>/I<sub>TX</sub> normalisation. Fixed gain, no AGC or blanking.
- **A 100 V bipolar pulse-induction mode** on the same H-bridge, plus a hybrid mode that time-interleaves PI bursts between VLF periods.
- **STM32H743** (Cortex-M7, 480 MHz) with single-crystal-coherent timing, a Bluetooth audio module, a Sharp memory LCD, GNSS find-logging and an IMU for swing-aware ground tracking.

> ### Status: experimental, design stage
> This is a **paper design that has not been built or field-tested.** The schematic is ERC-checked, the PCBs are placed and routed, the firmware core passes its host test bench, and the STM32 target layer is written against the CubeH7 HAL but has not run on hardware. Nothing here has been fabricated, assembled or measured. Component values, footprints and firmware behaviour may be wrong. **Verify everything against the primary datasheets before ordering or building.**
>
> The transmit stage switches up to **100 V** through an H-bridge into an inductive load. Treat it as a hazardous-voltage circuit: it can injure you and destroy parts if mis-built. Build and power it only if you are competent with high-voltage switching electronics.

## Layout

| Path | What |
|---|---|
| `hardware/mdx1/mdx1.kicad_pro` | KiCad project. Open this. |
| `hardware/mdx1/*.kicad_sch` | Root sheet + hierarchical sheets (power, tx_driver, rx_frontend, adc, mcu, ui_audio_rf, coil). |
| `hardware/mdx1/mdx1.kicad_pcb` | 135 × 92 mm 4-layer main board — footprints, nets, pours, routing, outline. |
| `hardware/mdx1/mdx1_ui.kicad_pcb` | 82 × 60 mm 2-layer UI board (LCD, keypad, pinpoint trigger, power key, LEDs, front-light). |
| `hardware/mdx1/MDX*.kicad_sym`, `MDX.pretty/` | Project-local symbol/footprint libraries. |
| `hardware/mdx1/fab/` | JLCPCB fabrication packages: Gerbers, drill, assembly BOM and pick-and-place for both boards; interconnect BOM. |
| `firmware/` | Portable core (`core/`), host physics simulator + test bench (`sim/`), STM32H743 target layer (`stm32/`). `make test` runs the bench. |
| `app/ios/` | SwiftUI companion app (BLE): live status, on-detector menu, finds map, diagnostics; built-in demo detector. |
| `app/android/` | Kotlin/Compose port of the companion app. |
| `docs/MDX-1_design_report.html` | Hardware design report. |
| `docs/MDX-1_coil_manual.html` | Search-coil build manual and off-the-shelf coil path. |
| `docs/MDX-1_firmware_guide.html` | Firmware guide with test evidence and screen renders. |
| `docs/MDX-1_link_protocol.md` | Phone-link protocol (frames, item table, status packet, actions). |
| `docs/MDX-1_schematic.pdf` | All schematic sheets. |
| `docs/datasheets/` | Third-party component datasheets used during the design review — **not included in this repository** (vendor copyright); part numbers are in the BOM under `hardware/mdx1/fab/`. |
| `tools/` | Design toolchain — see below. |

## The toolchain

**The KiCad schematic and boards are the source of truth — they are hand-placed and hand-routed.** The remaining tools are read-only against the boards or operate on design inputs; none of them modify the `.kicad_pcb` / `.kicad_sch` files. (The original placement/schematic generators were removed to prevent accidental overwrites of the finished layout.)

| Tool | Purpose |
|---|---|
| `tools/fab_jlc.py` | Reads the boards and writes the JLCPCB fabrication package: Gerbers + drill, assembly BOM and CPL for both boards. Does not modify the boards. |
| `tools/bom_data.json` | Maintained part database (manufacturer part numbers, packages, pricing) read by `fab_jlc.py` and `build_review.py`. |
| `tools/tx_waveform_opt.py` | Simulated-annealing optimiser for the 3-level, 5-line transmit waveform → `tx_waveform.json`. |
| `tools/coil_calc.py` | Coil electromagnetics (Neumann self/mutual inductance, null geometry, field) → `coil_calc.json`. |
| `tools/build_review.py`, `build_coil_manual.py` | Regenerate the BOM-review and coil-manual documents under `docs/`. |
| `tools/gen_fonts.py`, `pbm2png.py` | LCD bitmap fonts; simulator screen-render conversion. |

To regenerate the fabrication package after any board change (from KiCad's bundled Python):

```bash
/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 tools/fab_jlc.py
```

## Firmware

```bash
cd firmware
make test        # build the portable core against the host simulator and run the test bench
make screens     # render the UI screens from the simulator
```

The core (`firmware/core/`) is target-independent C: DSP, detector logic, UI, phone link, audio. The simulator (`firmware/sim/`) provides a synthetic coil/target/ground model so the whole signal chain runs and is tested on a desktop. The STM32 layer (`firmware/stm32/`) maps the core onto the H743 peripherals.

## Before fabricating

- Run DRC to clean and re-confirm the design rules for your fab.
- Get a live distributor quote against the current BOM (part availability changes).
- Re-verify every configuration strap and value against the primary datasheets in `docs/datasheets/`.

## License

No license is set yet. Until a `LICENSE` file is added, all rights are reserved and this is published for reference only. Add a license before treating any part of this as reusable.

## Disclaimer

Provided as-is, with no warranty. This is an unbuilt, untested hobby design involving hazardous voltages. You are responsible for verifying correctness and safety before building or powering anything. Comply with the radio-emissions and other regulations in your jurisdiction before operating a transmitter of any kind.
