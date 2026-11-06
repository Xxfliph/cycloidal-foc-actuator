# PCB requirements (draft, Nov 6, 2026)

## 1. Purpose
Replaces the dev rig (WeAct STM32, SimpleFOC Mini, MT6701 breakout) with one board:
STM32G431 + DRV8316 motor driver (built-in current sensing) + CAN transceiver + MT6701 encoder read over SPI instead of I2C (+ USB-C for logging if it fits).

## 2. Power in
- 12–15 V in (PD trigger or bench supply) → DRV8316 VM pins (3 pins)
- 2× 0.1 µF ceramic per VM pin, right at the pins → fast spikes when a FET switches
- Bulk, close to the chip (datasheet minimum: ≥ 10 µF):
  - 2× 10 µF 50 V X7R ceramic (1206) → carry the PWM ripple current (~half the phase current, ~0.5 A at 1 A; small electrolytics are only rated ~70 mA)
  - 1× 47 µF 50 V electrolytic → its internal resistance damps ringing when the supply is plugged in hot; VM ramp limit is 4 V/µs (abs max), which the bulk also slows
- All rated ≥ 35 V (TI: at least 2× the 15 V operating voltage)

**Input protection** (order: connector + → PTC → P-FET → TVS to GND → bulk caps → VM)
- PTC resettable fuse: hold 1.5 A, trip 3 A, 24 V rated (> 15 V). Slow (seconds) → backup only; the DRV8316's own overcurrent protection acts in µs. Trips earlier when the housing is warm.
- Reverse polarity: P-FET (AO3401A) in the + line, gate → 10 kΩ → GND, 10 V zener gate ↔ source (cathode at source) to keep VGS within the FET's limit (±12 V for the AO3401A as I remember its datasheet — confirm). Loss at 1 A ≈ 0.05 W vs 0.4 W for a diode.
- TVS SMBJ18A (unidirectional): off at 15 V, clamps spikes to ~29 V (< 40 V abs max). After the P-FET so it never sees reverse polarity.
- XT30 power connector is polarized → reverse plugging is already hard; the P-FET covers miswired cables.

## 3. 3.3 V rail (STM32, MT6701, CAN transceiver)
- From the DRV8316's built-in buck, default 3.3 V → straight to the STM32 (no extra regulator)
- Budget ≈ 72 mA (STM32 ~40 + MT6701 ≤ 10 + CAN ≤ 17 + LEDs ~5; estimates) → 47 µH inductor (good to 200 mA; 22 µH is only good to 50 mA)
- Inductor saturation current ≥ 600 mA (buck peak); 22 µF, ≥ 10 V output cap (DRV8316C datasheet Table 8-1)
- FB_BK connected to the buck output after the inductor
- At 3.3 V buck output, AVDD must run from VM, not the buck → firmware sets BUCK_PS_DIS = 1 (datasheet 8.3.4.4: sequencing only supported at 5.0/5.7 V)

## 4. DRV8316 wake-up (nSLEEP)
- DRV8316 is asleep at power-on → buck off → STM32 has no power
- nSLEEP pulled high from VM: 10 kΩ from VM + 3.3 V zener to GND (resistor drops the extra ~11.7 V, zener holds nSLEEP at ~3.3 V, pin max 5.5 V)
- NOT from AVDD: AVDD is off while the chip sleeps
- nSLEEP not connected to the STM32 → firmware can never cut its own power
- Faults cleared over SPI instead

## 5. DRV8316 small support caps (ceramic X5R/X7R, right at the pins)
- Flying cap: 47 nF, ≥ 35 V, CPH ↔ CPL
- Storage cap: 1 µF, 16 V, CP ↔ VM (only sees ~10 V across it)
- AVDD cap: 1 µF, 6.3 V, AVDD ↔ AGND (keeps the internal 3.3 V regulator stable)
- Why the charge pump: high-side FETs need a gate voltage ~10 V above VM to turn fully on

## 6. PWM (STM32 → DRV8316), 3x PWM mode
- INHA / INHB / INHC ← STM32 TIM1 CH1 / CH2 / CH3 (PA8 / PA9 / PA10, same as dev rig)
- INLA + INLB + INLC tied together → one STM32 GPIO (`PHASE_EN`, PB1), low at boot (DRV8316 inputs have internal pull-downs → low before firmware runs)
- Boot order: write 3x mode over SPI → set INLx GPIO high → enable motor (TI: INHx and INLx low while changing PWM mode)

## 7. DRVOFF and nFAULT
**DRVOFF** (STM32 → DRV8316): motor on/off switch
- High = all 6 FETs off (motor coasts), low = normal
- Internal pull-down (~100 kΩ) would mean motor ON at boot → add 10 kΩ pull-up to 3.3 V (pin then sits at ~3.0 V = high). Second safety layer: INLx is also low at boot.
- STM32 GPIO output (`DRV_OFF`, PB0), initial level High; `enable()` = low, `disable()` = high

**nFAULT** (DRV8316 → STM32): fault alarm, low = fault
- Open-drain → **5.1 kΩ pull-up to AVDD** (datasheet Table 8-1 value)
- NOT to the buck 3.3 V rail: datasheet says a pull-up supply must be > 2.2 V at power-up or the chip enters test mode, and the buck rail is still 0 V at that moment. AVDD is up as soon as the chip wakes.
- STM32 EXTI input (`DRV_FAULT`, PB2, falling edge); on fault → `disable()` + PI `reset()`, latched; read SPI status registers to see which fault

## 8. SPI (STM32 ↔ DRV8316CR)
- SPI3: SCLK ← PB3, SDO → PB4 (MISO), SDI ← PB5 (MOSI); nSCS ← PA15 (`DRV_CS`, low = selected)
- SDO is open-drain → needs a pull-up (kept even though push-pull mode exists: simplest setup for bring-up)
- nSCS has an internal pull-up → deselected at boot, no resistor needed
- Own SPI bus, separate from the MT6701
- DRV settings reset every power-up → firmware writes them every boot (PWM mode, current-sense gain, overcurrent limit, ...)

## 9. Current sensing (DRV8316 → STM32 ADC)
- SOA / SOB / SOC, one per phase: $V_{SOx} = \frac{V_{REF}}{2} \pm GAIN \times I$ → solve for I
- SOA → PA0, SOB → PA1, SOC → PA2 (ADC1_IN1–IN3), each through a small RC filter (TI recommendation)
- RC filter starting value: 330 Ω + 2.2 nF C0G → cutoff ≈ 220 kHz (removes switching spikes, doesn't slow the reading); tune on the bench
- **VREF → AVDD**, 0.1 µF cap to AGND. Datasheet: VREF must be 2.8 V up to AVDD, and in 3x mode it also powers the current-sense amps. The buck rail can sit higher than AVDD (buck 3.1–3.5 V vs AVDD 3.1–3.465 V), so it must not feed VREF.
- Linear output range: 0.25 V to VREF − 0.25 V → max current ≈ 1.4 V / gain:

  | Gain | Range | Resolution (12-bit ADC) |
  |---|---|---|
  | 0.15 V/A | ±9.3 A | ~5.4 mA/count |
  | 0.3 V/A | ±4.7 A | ~2.7 mA/count |
  | 0.6 V/A | ±2.3 A | ~1.3 mA/count |
  | 1.2 V/A | ±1.2 A | ~0.7 mA/count |

- Gain is a firmware setting (SPI), not a schematic decision; pick from the motor's max current (likely 0.6 V/A)
- PA0/PA1 are also reachable by ADC2 → later upgrade: sample two phases at the same instant
- Firmware: measure each SOx with the motor disabled at boot → store as the zero-current offset (VREF/2 isn't exactly the ADC midpoint)
- Firmware: TIM1 triggers the ADC at the PWM centre (all low-side FETs on); cap max duty a bit below 1.0

**VM sense:** 100 kΩ / 10 kΩ divider → PA3 (ADC1_IN4) + 100 nF (÷11: 15 V → 1.36 V, 36 V → 3.27 V). Firmware reads the real bus voltage instead of hard-coding vbus.

## 10. Parts
**Chips**
- **STM32G431CBT6**: 48-pin LQFP, 128 KB flash, 38 I/O. Chosen over CBU6 (UFQFPN): cheaper, better stock, visible legs → hand rework and probing possible. CubeMX must use STM32G431CBTx (pin numbers differ by package).
- **DRV8316CRRGFR**: SPI version of the newer "C" chip. TI says its main change is improved current-sense amp specs and recommends it for new designs. Requirements re-checked against the DRV8316C datasheet (same package, pins, caps, nFAULT/VREF/AVDD rules, pull-downs, PWM-mode rule). Backup: DRV8316RRGFR (C5218861).
- **MT6701CT-STD-R**: SOP-8 like the breakout; SSI on pins 6 (DO) / 7 (CLK) / 8 (CSN); MODE → 3.3 V (inferred from the breakout, which runs I2C/SSI with MODE at VDD — confirm); 0.1 µF on VDD (datasheet also suggests an optional TVS on VDD); 24-bit reads incl. 6-bit CRC
- **SN65HVD230DR** (TI original): Rs (pin 8) → GND (full speed), Vref (pin 5) unconnected, 0.1 µF on VCC; 120 Ω termination via solder jumper (only end-of-bus boards terminate)

**Parts table (JLCPCB / LCSC numbers, Nov 5–6)**

| Part | MFR part # | LCSC # | Type |
|---|---|---|---|
| MCU | STM32G431CBT6 | C529355 | Extended |
| Motor driver | DRV8316CRRGFR | C5447274 | Extended |
| Encoder | MT6701CT-STD-R (SOP-8, SSI) | C3003196 | Extended |
| CAN transceiver | SN65HVD230DR (TI) | C12084 | Promotional Extended |
| Buck inductor | SMNR5040-470MT (47 µH, shielded, 5×5 mm, ≥ 0.9 A) | C9392 | Extended |
| Bulk electrolytic | KNSCHA RVT47UF50V67RV0029 (47 µF 50 V, 6.3×7.7 mm) | C2887275 | Extended |
| Bulk ceramics (×2) | FH 1206B106K500NT (10 µF 50 V X7R 1206) | C303950 | Extended |
| Buck output cap | Samsung CL21A226MAQNNNE (22 µF 25 V X5R 0805) | C45783 | Basic |
| nSLEEP zener | BZT52C3V3 (3.3 V, 500 mW, SOD-123), MDD | C173413 | Extended |
| Crystal | YXC X50328MSB2GI (8 MHz, 20 pF load, ESR 80 Ω, 5×3.2 mm) | C115962 | Basic |
| PTC fuse | RUILON SMD1812P150TF/24 (1.5 A hold, 3 A trip, 24 V, 1812) | C32404 | check |
| Reverse-polarity FET | AO3401A (P-ch, −30 V, 4 A, 47 mΩ @ 10 V, SOT-23) | C15127 | Basic |
| FET gate clamp zener | BZT52C10 (10 V, SOD-123), Changjing | C2103 | Extended |
| TVS | SMBJ18A (18 V standoff, ~29.2 V clamp, SMB), RUILON | C10212 | check |
| Power connector | AMASS XT30PW-M (right-angle male, 15 A, polarized; through-hole, hand-soldered) | C431092 | hand |
| CAN connectors (×2, daisy-chain in/out) | JST GH 4-pin SM04B-GHS-TB (SMD); pinout 1 VCC (n/c for now), 2 CANH, 3 CANL, 4 GND | C189895 | check |
| Motor phases | 3 plated holes, wires soldered directly | — | — |
| SWD | 1×5 2.54 mm header: 3V3, SWDIO, SWCLK, NRST, GND (hand-soldered) | — | hand |
| Debug UART (backup) | 2 test pads on PB6 / PB7 (USART1) | — | — |

- ~8 Extended part types (~US$21–24 loading fees); everything else Basic
- Stock/price: confirm when uploading the BOM (JLCPCB's BOM tool flags shortages)

**Small parts (all Basic, 0603 unless noted; exact LCSC numbers from JLCPCB's Basic list when building the BOM)**
- Caps: 0.1 µF 50 V X7R (VM ×6, MT6701, CAN, STM32 VDD ×n, VREF, VM sense), 1 µF (CP 16 V, AVDD, VDDA, VREF+), 47 nF 50 V (CPH–CPL), 4.7 µF (STM32), 10 nF (VDDA), 30 pF C0G ×2 (crystal), 2.2 nF C0G ×3 (current-sense filter)
- Resistors: 10 kΩ (nSLEEP from VM, DRVOFF pull-up, SDO pull-up, P-FET gate), 5.1 kΩ (nFAULT → AVDD), 330 Ω ×3 (current-sense filter), 100 kΩ + 10 kΩ (VM sense), 120 Ω (CAN termination), 0 Ω (crystal series), LED resistors
- Ferrite bead (VDDA feed), status LED + power LED

## 11. STM32 support, pin map, clocks
**Support (values from ST's G4 hardware guide AN5093 as I remember it — confirm at schematic time)**
- Each VDD pin: 100 nF at the pin + one 4.7 µF for the chip
- VDDA (ADC supply): 1 µF + 10 nF, fed from 3.3 V through a ferrite bead (cleaner ADC → better current sensing); 0 Ω fallback footprint
- VREF+ (separate pin on LQFP-48): connect to VDDA + 1 µF + 100 nF. It's the ADC's reference → decides current-sense accuracy.
- NRST: 100 nF to GND (internal pull-up already exists), also on the SWD header
- BOOT0 / PB8 = FDCAN1_RX: no resistor. Option bytes **nSWBOOT0 = 0, nBOOT0 = 1** → always boots from flash, PB8 level ignored (ST's own B-G431B-ESC1 ships this way). Set once with STM32CubeProgrammer over SWD.
- Crystal: 2× 30 pF C0G load caps ($C = 2(C_L - C_{stray}) = 2(20 - 5)$); 0 Ω series resistor on OSC_OUT (swap if drive level is too high). Startup margin: $g_{m,crit} \approx 0.43$ mA/V vs G4 limit ~1.5 mA/V (confirm limit in the G431 datasheet).

**Pin map (CubeMX `hardware/pinplan/pinplan.ioc`, done Nov 6)**

| Pin | Function | Connects to | Notes |
|---|---|---|---|
| PA8 / PA9 / PA10 | TIM1_CH1 / CH2 / CH3 | DRV8316 INHA / INHB / INHC | same as dev rig |
| PB1 | GPIO out `PHASE_EN`, init Low | DRV8316 INLA + INLB + INLC | |
| PB0 | GPIO out `DRV_OFF`, init High | DRV8316 DRVOFF | + 10 kΩ pull-up to 3.3 V |
| PB2 | EXTI2 falling edge `DRV_FAULT` | DRV8316 nFAULT | + 5.1 kΩ pull-up to AVDD |
| PB3 / PB4 / PB5 | SPI3 SCK / MISO / MOSI | DRV8316 SCLK / SDO / SDI | SDO pull-up |
| PA15 | GPIO out `DRV_CS`, init High | DRV8316 nSCS | |
| PB13 / PB14 | SPI2 SCK / MISO (receive only) | MT6701 CLK (pin 7) / DO (pin 6) | |
| PB12 | GPIO out `ENC_CS`, init High | MT6701 CSN (pin 8) | |
| PA0 / PA1 / PA2 | ADC1_IN1 / IN2 / IN3 | DRV8316 SOA / SOB / SOC (via RC) | PA0/PA1 also reachable by ADC2 |
| PA3 | ADC1_IN4 | VM divider (100 k / 10 k) | |
| PB8 / PB9 | FDCAN1 RX / TX | SN65HVD230 R / D | BOOT0 via option bytes |
| PA11 / PA12 | USB DM / DP | USB-C (if it fits) | |
| PA13 / PA14 | SWDIO / SWCLK | SWD header | |
| PF0 / PF1 | HSE OSC_IN / OSC_OUT | 8 MHz crystal | 0 Ω on OSC_OUT side |
| PB10 | GPIO out `LED`, init Low | status LED | |
| PG10 | NRST | SWD header + 100 nF | |
| PB6 / PB7 | USART1 TX / RX (not configured yet) | test pads | backup debug UART |
| Free | PA4–PA7 (ADC2-capable), PB11, PB15, PC13–PC15 | | |

- All GPIOs: no internal pull (the board or the other chip handles it), speed Low
- **Clocks:** HSE 8 MHz → PLL /M 2, ×N 85, /R 2 → 170 MHz SYSCLK; AHB / APB1 / APB2 /1 (same as dev rig); USB ← HSI48 (+ CRS synced to USB); FDCAN ← PCLK1 170 MHz (divides evenly into 1 Mbps)

## 12. Mounting + encoder (decided Nov 5)
- MT6701 on the main board, centred over the magnet (magnet press-fit in a printed sleeve in the motor's back face)
- Board screwed to the motor's 4 outer back-face holes (confirmed stationary) on standoffs → round board
- Design gap ~2 mm (tested: 0 spread + smooth both ways at 2.8 mm)
- Layout: motor-current traces (VM, OUTA/B/C) away from the MT6701; shielded inductor; steel screws away from the magnet
- Connectors on the board edge (XT30, USB-C, GH) → reachable through the housing wall
- Size: 40 mm Ø is tight for single-sided assembly (~650 mm² of parts ×2 for routing ≈ 1,300 mm² vs 1,257 mm²); ~50 mm Ø is comfortable → decide in CAD from the housing

## 13. Assembly + manufacturer
- All SMD parts assembled by the factory (DRV8316 thermal pad can't be done with an iron); single-sided placement (Economic assembly)
- Through-hole connectors (XT30, SWD header) soldered by hand
- Choose parts that JLCPCB/LCSC has in stock while designing
- When done: quote the same files at JLCPCB and PCBWay (student program: 50% off manufacturing + assembly charges, up to US$200; needs PCBWay logo on silkscreen) → pick the cheaper
- Thermal pad → AGND, top-layer ground area, many vias to a big bottom-layer ground; PGND and AGND separate, joined at one point (net-tie or wide resistor)

## 14. Firmware boot settings (from the hardware choices above)
1. Write DRV8316 settings over SPI: PWM_MODE = 3x, BUCK_PS_DIS = 1, CSA_GAIN, OCP mode/level, OVP threshold = 34 V setting (the TVS clamps spikes to ~29 V, which would trip the 22 V setting)
2. Measure current-sense zero offsets (motor disabled)
3. PHASE_EN high → DRV_OFF low (enable)
- Never touch nSLEEP (not connected)
- One-time: option bytes nSWBOOT0 = 0, nBOOT0 = 1
- To verify when writing firmware: register reset values; whether setting DRVOFF high pulls nFAULT low (it does on the sister chip MCT8316Z); TIM1 ADC trigger exactly at the peak (with repetition counter 1, the update event lands on either the peak or the valley → a CH4 compare near ARR is the usual trigger)

## 15. Open questions
- Board outline + standoff height (needs calipers: 4-hole spacing, thread, back-face Ø, magnet protrusion, housing inner Ø) → decides USB
- USB: works electrically; keep only if the USB-C connector fits. If kept: pick USB-C connector + ESD chip
- Connector placement (types decided; positions need the outline)
- Confirm at schematic time: AO3401A VGS limit, MT6701 MODE level, AN5093 decoupling values, G4 crystal gm limit, real 3.3 V current numbers
