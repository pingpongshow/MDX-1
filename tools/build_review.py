"""docs/MDX-1_review_bom.html: datasheet strap review, MCU pin-map verification, cost estimate, full BOM."""
import os, json
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
bom = json.load(open(os.path.join(ROOT, "tools", "bom_data.json")))

STRAPS = [
 ("ADS127L01 ×2", "FORMAT (pin 30)", "was 0 (SPI)", "0 = SPI, 1 = frame-sync (Table: pin functions)", "Changed to 100 kΩ to +3V3A (frame-sync)", "fixed"),
 ("ADS127L01 ×2", "FSMODE (pin 14)", "was 1", "0 = slave, 1 = master; applies to frame-sync only", "Changed to 100 kΩ to GND (slave: MCU SAI drives FSYNC/SCLK)", "fixed"),
 ("ADS127L01 ×2", "DAISYIN (pin 18)", "B DOUT → A DAISYIN, B to GND", "Daisy chain supported in frame-sync slave mode (8.5.2.2.6); not in master mode", "OK with FSMODE = 0", "ok"),
 ("ADS127L01 ×2", "CAP1 / CAP2 / CAP3", "1 µF / 1 µF / 100 nF", "1 µF to AGND, 1 µF to AGND, 1 µF to DGND", "CAP3 changed to 1 µF", "fixed"),
 ("ADS127L01 ×2", "INTLDO (pin 8)", "10 kΩ to GND", "Pull low through 10 kΩ for the internal LDO; LVDD then gets a 1 µF cap", "OK", "ok"),
 ("ADS127L01 ×2", "REXT (pin 7)", "60.4 kΩ", "60.4 kΩ for HR and LP modes", "OK", "ok"),
 ("ADS127L01 ×2", "HR (pin 29)", "100 kΩ to +3V3A", "1 = high-resolution mode; pull through 100 kΩ", "OK", "ok"),
 ("ADS127L01 ×2", "FILTER[1:0], OSR[1:0]", "MCU GPIO", "00 = wideband-1, 00 = OSR 32 (512 kSPS at 16.384 MHz); change only with START low", "OK (firmware drives them, START low during change)", "ok"),
 ("ADS127L01 ×2", "REFP / REFN", "REF5025 2.5 V, ≥ 10 µF", "0.5 V to AVDD; ≥ 10 µF REFP–REFN", "OK", "ok"),
 ("ADS127L01 ×2", "AVDD / DVDD", "+3V3A both", "AVDD 2.7–3.6 V; DVDD 1.7–3.6 V; digital inputs ≤ DVDD + 0.3", "OK (MCU is 3.3 V logic)", "ok"),
 ("TPS65131", "CP / CN", "300 kΩ + 1 nF each", "10 nF at CP, 4.7 nF at CN, capacitor only (8.2.2.6.2)", "Changed to 10 nF and 4.7 nF to GND", "fixed"),
 ("TPS65131", "VIN vs INN", "both on VSYS", "RC filter ≥ 100 Ω + 0.1 µF between INN and VIN recommended (8.2.3)", "Added 100 Ω + 100 nF to VIN", "fixed"),
 ("TPS65131", "FBP divider", "1 MΩ / 113 kΩ", "VPOS = 1.213 V × (1 + R1/R2) → 11.95 V", "OK (+12 V)", "ok"),
 ("TPS65131", "FBN divider", "499 kΩ / 100 kΩ to VREF", "VNEG = −1.213 V × R3/R4 → −6.05 V", "OK (−6 V)", "ok"),
 ("TPS65131", "PSP / PSN", "GND", "0 V = power-save disabled (forced PWM)", "OK, low ripple", "ok"),
 ("TPS65131", "BSW", "open", "Gate control for an optional external battery switch", "OK", "ok"),
 ("TPS65131", "ENP / ENN", "MCU ANA_EN", "VIH 1.4 V", "OK", "ok"),
 ("TPS63001", "PS/SYNC", "VSYS (high)", "1 = power-save disabled", "OK", "ok"),
 ("TPS63001", "FB", "tied to VOUT", "Fixed versions: FB must connect to VOUT", "OK", "ok"),
 ("TPS63001", "EN, VINA", "VSYS", "EN high enables; VINA is the control supply", "OK", "ok"),
 ("TPS7A20 (3.3 V)", "EN", "tied to IN (+5VA)", "Internal 500 kΩ pull-down; VIN 1.6–6.0 V", "OK", "ok"),
 ("LT3045 (+5VA)", "SET", "was 100 kΩ (assumed 50 µA)", "SET sources 100 µA; VOUT = 100 µA × RSET", "Changed to 49.9 kΩ (5.0 V)", "fixed"),
 ("LT3045", "ILIM", "was 1 kΩ", "R_ILIM = 150 mA·kΩ / I_LIMIT; tie to GND if not needed", "Changed to 0 Ω to GND (internal limit)", "fixed"),
 ("LT3045", "PGFB", "was GND", "Tie PGFB to IN if power-good and fast start-up are not needed", "Changed to IN (+12 V)", "fixed"),
 ("LT3045", "EN/UV", "tied to IN", "If unused, tie EN/UV to IN", "OK", "ok"),
 ("LT3094 (−5VA)", "SET", "49.9 kΩ", "SET sinks 100 µA; VOUT = −100 µA × RSET", "OK (−5.0 V)", "ok"),
 ("LT3094", "ILIM", "was 1 kΩ", "If not used, tie ILIM to GND", "Changed to 0 Ω to GND", "fixed"),
 ("LT3094", "VIOC", "was GND", "If unused, float the VIOC pin", "Changed to no-connect", "fixed"),
 ("LT3094", "PGFB", "was GND", "Tie PGFB to IN if not needed", "Changed to IN (−6 V)", "fixed"),
 ("LT3094", "EN/UV", "tied to IN", "If unused, tie the EN/UV pin to IN", "OK", "ok"),
 ("LT3757 ×2", "FBX divider", "442k/40.2k (+66.5k DAC); 1.238M/20k", "FBX regulates at 1.6 V (1.569–1.631)", "OK: 7.9–29.8 V programmable; 100.6 V", "ok"),
 ("LT3757 ×2", "RT", "41.2 kΩ; 140 kΩ", "41.2 kΩ → 300 kHz (270–330); 140 kΩ ≈ 100 kHz per application table", "OK", "ok"),
 ("LT3757 ×2", "SENSE", "20 mΩ; 50 mΩ", "VSENSE(MAX) 108 mV → 5.4 A and 2.2 A switch peak", "OK for 1.5 W and 2 W converters", "ok"),
 ("LT3757 ×2", "SS, VC, INTVCC, SYNC", "100 n/220 n; 22 k + 4.7 n/10 n; 4.7 µF; GND", "Typical values per datasheet applications; SYNC to GND if unused", "OK", "ok"),
 ("LT3757 ×2", "SHDN/UVLO", "MCU GPIO 3.3 V", "Rising threshold 1.22 V", "OK", "ok"),
 ("UCC27714 ×2", "EN/NC (pin 4)", "MCU DRV_EN", "Low disables both outputs; high or floating enables; VENH 2.3 V typ", "OK with 3.3 V logic", "ok"),
 ("UCC27714 ×2", "HB–HS bootstrap", "220 nF, US1D 200 V diode", "≈ 10× gate capacitance", "OK (Qg 30 nC → Ciss ≈ 3 nF)", "ok"),
 ("INA241A2", "REF1 = REF2", "VREF2V5", "0 V to VS; REF1 = REF2 = VREF gives bidirectional output around VREF", "OK: 2.5 V ± 0.4 V/A", "ok"),
 ("INA241A2", "IN± common mode", "0 to 100 V (VBRIDGE)", "−5 V to 110 V", "OK", "ok"),
 ("TLV3501 ×2", "SHDN", "GND", "Enabled when more than 1.7 V below V+; disabled within 0.9 V of V+", "OK", "ok"),
 ("OPA1612", "supply", "±5 V", "±2.25 V to ±18 V", "OK", "ok"),
 ("THS4551 ×2", "PD, VOCM, FB±", "PD → VS+; VOCM 1.65 V; FB± as feedback nodes", "PD active low; single supply 2.7–5.4 V; FB± are output feedback pins", "OK", "ok"),
 ("BQ24075", "EN1 / EN2", "EN1 = 0, EN2 = VBUS", "EN2 = 1, EN1 = 0: input limit set by ILIM resistor (Table 7-2)", "OK", "ok"),
 ("BQ24075", "ILIM / ISET / TMR / TS", "1.1 kΩ / 887 Ω / 47 kΩ / 10 kΩ", "ILIM 1.1–8 kΩ; ISET 590–8900 Ω; TMR 18–72 kΩ; TS 10 kΩ if no NTC", "OK (1.4 A in, 1.0 A charge)", "ok"),
 ("BQ24075", "SYSOFF, CE", "GND, GND", "SYSOFF low = system connected; CE low = charging enabled", "OK", "ok"),
 ("BQ27441-G1", "SRP / SRN / BAT", "SRP pack+, SRN system side, 10 mΩ high-side", "High-side 10 mΩ between PACK+ and VSYS; SRP to pack side", "OK", "ok"),
 ("BQ27441-G1", "BIN, GPOUT", "10 kΩ to VSS; 10 kΩ pull-up to VDD", "10 kΩ pull-down required; GPOUT should not float, 10 kΩ pull-up to VDD", "OK", "ok"),
 ("TPA6132A2", "G0 / G1, EN", "+3V3 / GND; MCU HP_EN", "G0 ≥ 1.3 V, G1 ≤ 0.5 V → 0 dB; EN high = active", "OK", "ok"),
 ("TPA6132A2", "CPP–CPN, HPVSS", "1 µF, 1 µF", "1 µF flying cap, 1 µF at HPVSS", "OK", "ok"),
 ("MAX-M10S", "VIO_SEL", "open", "Open = 3.3 V I/O; GND = 1.8 V", "OK", "ok"),
 ("MAX-M10S", "SAFEBOOT_N / TIMEPULSE", "open / MCU input", "Shared through 1 kΩ; must not be low at start-up", "OK (PD0 is an input with no pull-down)", "ok"),
 ("MAX-M10S", "RESET_N, EXTINT, V_BCKP", "MCU GPIO, open, +3V3", "Active low ≥ 1 ms; leave EXTINT open; V_BCKP ≤ 3.6 V", "OK", "ok"),
 ("LSM6DSM", "CS, SDO/SA0", "+3V3; 0 Ω to GND", "CS = 1 enables I2C; SA0 = 0 → address 0x6A", "OK", "ok"),
 ("PAM8302A", "~SD", "MCU SPK_SD", "Low = shutdown", "OK (firmware drives high to enable)", "ok"),
 ("STM32H743", "BOOT0, PDR_ON, NRST, VCAP, VREF+", "10 kΩ down; VDD; 100 nF; 2 × 2.2 µF; VDDA", "Standard reference design", "OK", "ok"),
 ("Crystals", "HSE 16.384 MHz / LSE 32.768 kHz", "12 pF / 10 pF loads", "CL 8 pF and 7 pF crystals: C = 2·(CL − 2 pF stray)", "LSE caps changed 6.8 → 10 pF", "fixed"),
 ("Electrolytics", "100 µF 50 V, 22 µF 160 V", "were SMD 6.3 and 10 mm cans", "Ratings need 8 mm and 10 mm radial cans", "Footprints changed to through-hole radials", "fixed"),
 ("Rev A3 schematic review", "all rail capacitors", "label-only rails: caps not associated with a pin", "Layout needs to know which capacitor belongs to which pin", "Every rail-to-ground capacitor is drawn wired to its pin (stub, junction, rail, cap, ground) and carries an Attach field; the PCB builder places it beside that pin", "fixed"),
 ("LT3045 / LT3094", "IN", "no local input capacitor", "Datasheet: 4.7 to 10 µF at IN", "4.7 µF 25 V added at each IN", "fixed"),
 ("LT3757 ×2", "VIN", "no local capacitor", "Bypass VIN close to the pin", "4.7 µF added at each VIN", "fixed"),
 ("USB-C", "D+ / D−", "no ESD protection", "USB 2.0 data lines exposed at the connector", "USBLC6-2SC6 ESD array added", "fixed"),
 ("Memory LCD LS027B7DH01", "VDD / VDDA", "was +3V3", "VDD 4.8 to 5.5 V; VIH 2.7 V min (3.3 V logic OK)", "Powered from +5VA through 10 Ω / 10 µF (+5V_LCD)", "fixed"),
 ("MAX-M10S", "RF_IN", "direct to the antenna", "Vendor matching network for the ground-plane size", "Series 0 Ω and shunt DNP pads added", "fixed"),
 ("HV bleed", "resistor", "4.7 kΩ 1 W (2.1 W peak)", "Pulse power for a 100 V bank", "10 kΩ 1 W, 1.4 s discharge", "fixed"),
 ("BQ27441-G1", "BAT", "no bypass", "0.1 µF at BAT recommended", "100 nF added", "fixed"),
 ("PAM8302A", "IN± coupling, VDD bypass", "1 µF, 100 nF", "0.1 to 0.22 µF input coupling; 1 µF at VDD", "220 nF and 1 µF", "fixed"),
 ("Bring-up", "test points", "none", "-", "15 test pads: ITX, fault, coil TX/RX, VOCM, ADC inputs, reference, SAI lines, SWO, NRST", "fixed"),
 ("Rev A4: STM32H743 VDD", "14 VDD pins", "one capacitor per pin (11)", "Owner's call: VDD pins on the +3V3 plane, one bulk + one HF capacitor", "4.7 µF + 100 nF at the package; VCAP1/2 keep their 2.2 µF (core-regulator outputs, required by ST); VDDA keeps bead + 1 µF / 100 nF", "fixed"),
 ("Rev A4: keypad", "8 keys + power", "MENU BACK UP DOWN LEFT RIGHT PINPOINT GB POWER", "Owner's call: simplify", "MENU UP DOWN LEFT RIGHT + PINPOINT trigger + POWER; BACK = MENU held 0.8 s, ground grab = PINPOINT held 1.5 s; PG15 and PB4 freed", "fixed"),
 ("Rev A5: headless option", "UI link pin 8, PG15", "second GND", "Owner's call: run with or without the UI board", "Pin 8 = UI_DET: grounded on the UI board, 10 k pull-up to +3V3 on the main board, read at boot (MDX_UI_BOARD 0/1/2); main-board POWER button and status LED added in parallel with the UI board's", "fixed"),
 ("Rev A4: UI board", "display, keys, LEDs, front-light", "on the main board", "Owner's call: separate PCB", "82 × 60 mm two-layer board (sheet 7, mdx1_ui.kicad_pcb), 20-way 0.5 mm FFC (Hirose FH12-20S both ends): GND ×2, +3V3, +5V_LCD, VSYS, 5 LCD lines, 7 keys, 2 LEDs, front-light cathode", "fixed"),
]
PINS = [
 ("PE9 / PE8", "TIM1_CH1 / CH1N", "AF1", "Table 10 p.94", "bridge A high / low gates"), ("PE11 / PE10", "TIM1_CH2 / CH2N", "AF1", "Table 10 p.94", "bridge B gates"),
 ("PE15", "TIM1_BKIN", "AF1", "Table 10 p.94", "over-current break"), ("PC9", "MCO2", "AF0", "Table 10 p.93", "16.384 MHz to both ADCs; PC9 is also SDMMC1_D1, so the SD card runs 1-bit"),
 ("PF8 / PF9 / PF6", "SAI1_SCK_B / FS_B / SD_B", "AF6", "Table 10 p.95", "ADC frame-sync interface"), ("PE2 / PE4 / PE5 / PE6", "SAI1_MCLK_A / FS_A / SCK_A / SD_A", "AF6", "Table 10 p.94", "I2S to the Bluetooth module"),
 ("PE12 / PE14", "SPI4_SCK / MOSI", "AF5", "Table 10 p.94", "memory LCD"), ("PD15", "TIM4_CH4", "AF2", "Table 10 p.93", "front-light PWM"),
 ("PB8 / PB9", "I2C1_SCL / SDA", "AF4", "Table 10 p.91", "fuel gauge, IMU"), ("PC10 / PC11", "UART4_TX / RX", "AF8", "Table 10 p.93", "GNSS"),
 ("PA9 / PA10", "USART1_TX / RX", "AF7", "Table 9 p.80", "Bluetooth module; PA9 also carries OTG_FS_VBUS, so USB VBUS sensing is disabled in firmware"),
 ("PA11 / PA12", "OTG_FS_DM / DP", "AF10", "Table 9 p.80", "USB-C"), ("PC8 / PC12 / PD2", "SDMMC1_D0 / CK / CMD", "AF12", "Table 9 p.80–83", "microSD, 1-bit"),
 ("PA0 / PA1 / PA2 / PA3", "ADC1_INP16 / INP17 / ADC12_INP14 / INP15", "analog", "Table 9 p.68", "VTX, VBRIDGE, VBAT, VBUS monitors"), ("PA4 / PA5", "DAC1_OUT1 / OUT2", "analog", "Table 9 p.68–69", "VTX set-point, audio"),
 ("PA13 / PA14 / PB3", "SWDIO / SWCLK / TRACESWO", "AF0", "Table 9/10", "debug"), ("PC13", "WKUP4 (RTC_TS)", "-", "Table 9 p.66", "power key; HAL constant corrected to PWR_WAKEUP_PIN4"),
 ("PH0 / PH1, PC14 / PC15", "OSC_IN / OUT, OSC32_IN / OUT", "-", "Table 9", "crystals"), ("PG2–PG6, PG9–PG15, PB4, PF0–PF5, PF10, PD0–PD14 (used ones)", "GPIO", "-", "-", "enables, keys, straps, status inputs; PB4 is NJTRST at reset, fine as an input key"),
]
rows = bom["rows"]; off = bom["offboard"]
tot1, tot100 = bom["total1"], bom["total100"]
pcb_run, pcb100, setup, asm1, asm100, attr = 85.0, 9.0, 60.0, 45.0, 18.0, 0.08
proto = tot1 + pcb_run + setup + asm1 + attr * tot1
five = tot1 + pcb_run / 5 + setup / 5 + asm1 + attr * tot1
hund = tot100 + pcb100 + setup / 100 + asm100 + attr * tot100
off1 = sum(o[6] * o[1] for o in off); off100 = sum(o[7] * o[1] for o in off)
cats = []
for r in rows:
    if r["Category"] not in cats: cats.append(r["Category"])
def money(x): return "$%.2f" % x
bom_html = ""
for c in cats:
    items = [r for r in rows if r["Category"] == c]
    sub1 = sum(r["Qty"] * r["Unit price qty 1 (USD)"] for r in items); sub100 = sum(r["Qty"] * r["Unit price qty 100 (USD)"] for r in items)
    bom_html += '<tr class="cat"><td colspan="7">%s</td><td class="num">%s</td><td class="num">%s</td></tr>' % (c, money(sub1), money(sub100))
    for r in items:
        bom_html += "<tr><td class='num'>%d</td><td class='mono small'>%s</td><td>%s</td><td>%s</td><td>%s</td><td class='mono'>%s</td><td>%s</td><td class='num'>%s</td><td class='num'>%s</td></tr>" % (
            r["Qty"], r["References"], r["Description"], r["Package"], r["Rating"], r["MPN"], r["Manufacturer"], money(r["Qty"] * r["Unit price qty 1 (USD)"]), money(r["Qty"] * r["Unit price qty 100 (USD)"]))
off_html = "".join("<tr><td>%s</td><td class='num'>%d</td><td>%s</td><td>%s</td><td class='mono'>%s</td><td>%s</td><td class='num'>%s</td><td class='num'>%s</td></tr>" % (g, q, it, m, mpn, rt, money(p1 * q), money(p100 * q)) for g, q, it, m, mpn, rt, p1, p100 in off)
strap_html = "".join('<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td><span class="v %s">%s</span></td></tr>' % (a, b, c, d, e, f, "changed" if f == "fixed" else "confirmed") for a, b, c, d, e, f in STRAPS)
pin_html = "".join("<tr><td class='mono'>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>" % p for p in PINS)
n_fixed = sum(1 for s in STRAPS if s[5] == "fixed")

html = r'''<title>MDX-1 Review and BOM</title>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Semi+Condensed:wght@500;600;700&family=IBM+Plex+Sans:ital,wght@0,400;0,500;0,600;1,400&family=IBM+Plex+Mono:wght@400;500&display=swap">
<style>
:root{--bg:#f2f4f6;--surface:#fff;--ink:#1b2129;--ink2:#4c5866;--mute:#7b8794;--line:#d5dbe2;--cu:#b5661c;--cu-soft:#f3e3d3;--sig:#2a6bb3;--sig-soft:#dfe9f6;--ok:#2f7d4f;--ok-soft:#dcefe3;--warn:#9a6b0a;--warn-soft:#f6ecd2;--code:#eef1f4}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){--bg:#11151b;--surface:#181e26;--ink:#e6e9ee;--ink2:#b3bcc7;--mute:#7f8a97;--line:#2b333d;--cu:#d98a3f;--cu-soft:#3a2a1a;--sig:#6fa8e6;--sig-soft:#1d2a3a;--ok:#66b784;--ok-soft:#1b2f23;--warn:#d9b24c;--warn-soft:#332a14;--code:#1f262f}}
:root[data-theme="dark"]{--bg:#11151b;--surface:#181e26;--ink:#e6e9ee;--ink2:#b3bcc7;--mute:#7f8a97;--line:#2b333d;--cu:#d98a3f;--cu-soft:#3a2a1a;--sig:#6fa8e6;--sig-soft:#1d2a3a;--ok:#66b784;--ok-soft:#1b2f23;--warn:#d9b24c;--warn-soft:#332a14;--code:#1f262f}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font-family:"IBM Plex Sans",system-ui,sans-serif;font-size:15px;line-height:1.5}
h1,h2,h3{font-family:"Barlow Semi Condensed","Arial Narrow",sans-serif;text-wrap:balance;margin:0;line-height:1.1}h1{font-size:clamp(2.2rem,5vw,3.2rem);font-weight:700}h2{font-size:1.6rem;font-weight:600}h3{font-size:1.1rem;font-weight:600}
.wrap{max-width:1240px;margin:0 auto;padding:0 clamp(16px,3vw,40px)}header.top{border-bottom:1px solid var(--line);background:var(--surface)}header.top .wrap{padding:36px 0 24px;display:grid;gap:14px}
.eyebrow{font-family:"IBM Plex Mono",monospace;font-size:.78rem;letter-spacing:.14em;text-transform:uppercase;color:var(--cu)}.lede{font-size:1.08rem;color:var(--ink2);max-width:72ch}
.chips{display:flex;flex-wrap:wrap;gap:8px}.chip{display:inline-flex;padding:4px 10px;border-radius:999px;font-family:"IBM Plex Mono",monospace;font-size:.76rem;border:1px solid var(--line);background:var(--bg)}.chip.ok{background:var(--ok-soft);color:var(--ok);border-color:transparent}.chip.warn{background:var(--warn-soft);color:var(--warn);border-color:transparent}.chip.cu{background:var(--cu-soft);color:var(--cu);border-color:transparent}
nav.toc{position:sticky;top:0;z-index:5;background:var(--surface);border-bottom:1px solid var(--line)}nav.toc .wrap{display:flex;gap:2px;overflow-x:auto}nav.toc a{white-space:nowrap;padding:10px 12px;font-family:"IBM Plex Mono",monospace;font-size:.76rem;color:var(--ink2);text-decoration:none;border-bottom:2px solid transparent}nav.toc a:hover{color:var(--cu);border-bottom-color:var(--cu)}
section{padding:36px 0 6px}section .wrap{display:grid;gap:16px}.secnum{font-family:"IBM Plex Mono",monospace;color:var(--cu);font-size:.82rem;letter-spacing:.1em}
p{margin:0;max-width:76ch}ul{margin:0;padding-left:1.2em;max-width:78ch}li{margin:.3em 0}li::marker{color:var(--cu)}
.grid2{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:16px;align-items:start}.panel{background:var(--surface);border:1px solid var(--line);border-radius:6px;padding:16px 18px;display:grid;gap:8px}
table{border-collapse:collapse;width:100%;font-size:.86rem;font-variant-numeric:tabular-nums}.tablewrap{overflow-x:auto;background:var(--surface);border:1px solid var(--line);border-radius:6px}th,td{padding:6px 10px;text-align:left;vertical-align:top;border-bottom:1px solid var(--line)}th{font-family:"Barlow Semi Condensed",sans-serif;font-weight:600;color:var(--ink2);background:var(--code);position:sticky;top:38px}tr:last-child td{border-bottom:none}td.num,th.num{text-align:right;white-space:nowrap}
tr.cat td{background:var(--cu-soft);font-family:"Barlow Semi Condensed",sans-serif;font-weight:600;font-size:.95rem}
.mono{font-family:"IBM Plex Mono",monospace;font-size:.82em}.small{font-size:.78em;color:var(--ink2)}
.v{display:inline-block;padding:2px 8px;border-radius:999px;font-family:"IBM Plex Mono",monospace;font-size:.72rem}.v.ok{background:var(--ok-soft);color:var(--ok)}.v.fixed{background:var(--warn-soft);color:var(--warn)}
.status{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:12px}.status .panel{padding:12px 14px}.status .big{font-family:"Barlow Semi Condensed",sans-serif;font-size:1.9rem;font-weight:600;line-height:1}.status .lab{font-size:.78rem;color:var(--mute);font-family:"IBM Plex Mono",monospace}
.callout{border-left:3px solid var(--cu);background:var(--cu-soft);padding:12px 16px;border-radius:0 6px 6px 0;max-width:82ch}.callout.warn{border-color:var(--warn);background:var(--warn-soft)}
footer{padding:36px 0 60px;color:var(--mute);font-size:.84rem}
</style>
<header class="top"><div class="wrap">
<div class="eyebrow">Design review · hardware rev A5 · 2026-09-06</div>
<h1>MDX-1 review and bill of materials</h1>
<p class="lede">Every configuration pin, strap and feedback network checked against the manufacturer datasheets; every MCU pin checked against the STM32H743 alternate-function tables; a priced, grouped bill of materials with manufacturer part numbers; and a build cost estimate for the board alone and for a complete detector.</p>
<div class="chips"><span class="chip ok">''' + str(len(STRAPS)) + r''' strap/configuration checks</span><span class="chip warn">''' + str(n_fixed) + r''' corrected in the schematic (rev A2)</span><span class="chip ok">36 MCU pin functions verified in DS12110</span><span class="chip cu">''' + str(bom["lines"]) + r''' BOM lines · ''' + str(bom["parts"]) + r''' parts</span></div>
</div></header>
<nav class="toc"><div class="wrap"><a href="#straps">Strap review</a><a href="#pins">MCU pin map</a><a href="#cost">Cost estimate</a><a href="#bom">Full BOM</a><a href="#off">Off-board</a></div></nav>

<section id="straps"><div class="wrap"><div class="secnum">1</div><h2>Datasheet review of configuration pins</h2>
<p>Sources: TI datasheets for ADS127L01, TPS65131, TPS63001, TPS7A20, UCC27714, INA241A, TLV3501, OPA1612, THS4551, BQ24075, BQ27441-G1, TPA6132A2, REF5025; Analog Devices LT3045, LT3094, LT3757; Infineon BSC190N15NS3; Diodes PAM8302A (from your <span class="mono">docs/datasheets</span> folder); ST LSM6DSM and STM32H743 DS12110 rev 11; u-blox MAX-M10S; Sharp LS027B7DH01. Items marked <span class="v fixed">changed</span> were wrong or missing and are corrected in the current schematic; rows marked "Owner's call" are design decisions taken on the owner's instruction. ERC and the netlist were re-checked afterwards (ERC clean: 0 errors, 0 warnings).</p>
<div class="tablewrap"><table><thead><tr><th>Part</th><th>Pin / network</th><th>As designed (rev A1)</th><th>Datasheet rule</th><th>Action</th><th>Result</th></tr></thead><tbody>''' + strap_html + r'''</tbody></table></div>
<div class="callout warn">Two of the corrections mattered functionally: the ADS127L01 straps selected SPI mode and frame-sync <em>master</em>, the opposite of what the rev A1 SAI interface needs, and the LT3045 SET resistor would have produced 10 V on the +5VA rail (the OPA1612 and THS4551 would have survived, the ADS127L01 reference path would not). Both are fixed. The rest were housekeeping: compensation capacitors, unused-pin terminations, capacitor values and can sizes.</div>
</div></section>

<section id="pins"><div class="wrap"><div class="secnum">2</div><h2>MCU pin map verified against DS12110</h2>
<p>CubeMX was not available in this environment, so the check was done against the same tables CubeMX uses: Table 9 (pin definitions, additional analog functions) and Table 10 (alternate functions AF0–AF15) of the STM32H743 datasheet. Every assigned function exists on its pin at the stated alternate-function number, and no two used functions collide. The results are also captured in <span class="mono">firmware/stm32/README.md</span>.</p>
<div class="tablewrap"><table><thead><tr><th>Pins</th><th>Function</th><th>AF</th><th>Where verified</th><th>Use / note</th></tr></thead><tbody>''' + pin_html + r'''</tbody></table></div>
<div class="grid2"><div class="panel"><h3>Corrections from this check</h3><ul><li>PC13 is WKUP4 on this device (PA0 is WKUP1, PA2 is WKUP2). The standby wake-up constant in <span class="mono">hal_stm32.c</span> is now <span class="mono">PWR_WAKEUP_PIN4_LOW</span>.</li><li>PA9 doubles as the USB VBUS sense input. It is used as USART1_TX, so the USB stack must run with VBUS sensing disabled (already the configuration in the target code).</li><li>PC9 (MCO2 to the ADCs) is also SDMMC1_D1; the SD card therefore runs in 1-bit mode, which the pin map already assumed.</li></ul></div>
<div class="panel"><h3>What CubeMX would add</h3><p>When the project is opened in CubeMX, the remaining things to confirm are the clock tree (PLL1 N = 56 from a 16.384 MHz HSE, PLL2 for the 32.768 MHz SAI kernel clock), the DMA stream assignments used in the target code (DMA1 streams 0–2) and the SAI1 block B TDM settings. None of those are pin questions.</p></div></div>
</div></section>

<section id="cost"><div class="wrap"><div class="secnum">3</div><h2>Cost estimate</h2>
<div class="status">
<div class="panel"><div class="big">''' + money(tot1) + r'''</div><div class="lab">board components, one unit at qty-1 prices</div></div>
<div class="panel"><div class="big">''' + money(proto) + r'''</div><div class="lab">one assembled prototype board: components + 5-board PCB order + assembly setup and one board + 8 % attrition</div></div>
<div class="panel"><div class="big">''' + money(five) + r'''</div><div class="lab">per board when five are built in one run</div></div>
<div class="panel"><div class="big">''' + money(hund) + r'''</div><div class="lab">per board at 100 units (qty-100 prices)</div></div>
<div class="panel"><div class="big">''' + money(proto + off1) + r'''</div><div class="lab">complete prototype detector: board + coil + display + battery + enclosure + shaft</div></div>
<div class="panel"><div class="big">''' + money(hund + off100) + r'''</div><div class="lab">complete detector at 100 units</div></div>
</div>
<div class="grid2">
<div class="tablewrap"><table><thead><tr><th>Assumption</th><th class="num">Value</th></tr></thead><tbody>
<tr><td>Bare PCB, 135 × 92 mm, 4-layer, 1.6 mm, ENIG, 0.2 mm vias, 5 pieces</td><td class="num">$85 per run</td></tr>
<tr><td>Bare PCB at 100 pieces</td><td class="num">$9.00 each</td></tr>
<tr><td>SMT assembly setup and stencil per run</td><td class="num">$60</td></tr>
<tr><td>Assembly per board (about 330 placements, one side, fine pitch)</td><td class="num">$45 (qty 5), $18 (qty 100)</td></tr>
<tr><td>Component attrition and cut-tape minimums</td><td class="num">8 %</td></tr>
<tr><td>Off-board items (coil, display, battery, enclosure, shaft, keys, speaker, panel connector)</td><td class="num">''' + money(off1) + r''' proto, ''' + money(off100) + r''' at 100</td></tr>
</tbody></table></div>
<div class="panel"><h3>How to read these numbers</h3><ul><li>The board-only figure answers "what does it cost to build the PCB": about ''' + money(proto) + r''' for the first assembled board, falling to ''' + money(five) + r''' each for five and ''' + money(hund) + r''' at a hundred.</li><li>Quantity-1 unit prices are taken from the actual DigiKey, Mouser and Coilcraft shopping carts (September 2026); six parts not in those carts (fuel gauge, 16.384 MHz crystal, coil header, JST 2-pin) keep a catalogue-typical estimate. The quantity-100 column is a volume estimate, not a quote. Re-price against a live quote before ordering.</li><li>Buying qty-1 passives means bags of 10–100, which adds roughly $30–50 to a first build beyond the attrition line.</li><li>The two most expensive board components are the two ADCs and the MCU; the display is the most expensive single part overall. There is no realistic saving on the ADC without giving up the design's main advantage.</li></ul></div>
</div>
<p>The consolidated, orderable per-board bills of materials (grouped by manufacturer part number, with pick-and-place files) are under <span class="mono">hardware/mdx1/fab/BOM/</span> and <span class="mono">hardware/mdx1/fab/PNP/</span>, generated from the board by <span class="mono">tools/fab_jlc.py</span>.</p>
</div></section>

<section id="bom"><div class="wrap"><div class="secnum">4</div><h2>Bill of materials, main board and UI board (rev A5)</h2>
<p>Grouped by function, like parts merged. Extended prices are per one board at quantity-1 and quantity-100 unit prices. All 0402 resistors are 1 % thick film unless marked 0.1 %; all MLCCs are X7R/X5R except those marked C0G; voltage ratings are in the rating column.</p>
<div class="tablewrap"><table><thead><tr><th class="num">Qty</th><th>References</th><th>Description</th><th>Package</th><th>Rating</th><th>MPN</th><th>Manufacturer</th><th class="num">Ext $ (qty 1)</th><th class="num">Ext $ (qty 100)</th></tr></thead><tbody>''' + bom_html + r'''<tr class="cat"><td colspan="7">Board total</td><td class="num">''' + money(tot1) + r'''</td><td class="num">''' + money(tot100) + r'''</td></tr></tbody></table></div>
</div></section>

<section id="off"><div class="wrap"><div class="secnum">5</div><h2>Off-board items for a complete detector</h2>
<div class="tablewrap"><table><thead><tr><th>Group</th><th class="num">Qty</th><th>Item</th><th>Source</th><th>MPN</th><th>Rating</th><th class="num">Ext $ (proto)</th><th class="num">Ext $ (100)</th></tr></thead><tbody>''' + off_html + r'''<tr class="cat"><td colspan="6">Off-board total</td><td class="num">''' + money(off1) + r'''</td><td class="num">''' + money(off100) + r'''</td></tr></tbody></table></div>
</div></section>
<footer><div class="wrap"><p>Generated by <span class="mono">tools/build_review.py</span> from <span class="mono">tools/bom_data.json</span> (UI-board lines grouped under their own headings). The fabrication BOMs and pick-and-place files are under <span class="mono">hardware/mdx1/fab/</span>.</p></div></footer>
'''
out = os.path.join(ROOT, "docs", "MDX-1_review_bom.html")
open(out, "w").write(html)
print("wrote", out, len(html) // 1024, "KB; proto board $%.2f, five $%.2f, hundred $%.2f, complete proto $%.2f, complete 100 $%.2f" % (proto, five, hund, proto + off1, hund + off100))
