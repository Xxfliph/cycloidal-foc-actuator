# PCB requirements (draft Nov 6, 2026; datasheet pass Nov 8)

Datasheets used for the Nov 8 pass (page numbers below refer to these): DRV8316C = TI SLVSH07 (Dec 2022); STM32G431 = ST DS12589 Rev 6; SN65HVD230 = TI SLOS346O; AO3401A = AOS; MT6701 = MagnTek Rev 1.9 (Chinese, 2024.05); passives = maker PDFs from LCSC (RUILON, MDD, Changjing, SXN, YXC, KNSCHA, FH, Samsung, Kefa, JST). Local copies: `C:\Users\jonny\EDA Agent\workspace\datasheets`.

## 1. Purpose
Replaces the dev rig (WeAct STM32, SimpleFOC Mini, MT6701 breakout) with one board:
STM32G431 + DRV8316 motor driver (built-in current sensing) + CAN transceiver + MT6701 encoder read over SPI instead of I2C (+ USB-C for logging if it fits).

## 2. Power in
- 12–15 V in (PD trigger or bench supply) → DRV8316 VM pins (3 pins). DRV8316C VM: 4.5–35 V recommended, 40 V abs max, ramp ≤ 4 V/µs (p6)
- 2× 0.1 µF ceramic per VM pin, right at the pins → fast spikes when a FET switches (pin table p5)
- Bulk, close to the chip (datasheet minimum: ≥ 10 µF, Table 8-1 p19):
  - 2× 10 µF 50 V X7R ceramic (1206) → carry the PWM ripple current (~half the phase current, ~0.5 A at 1 A; the 47 µF electrolytic is only rated 70 mA ripple, KNSCHA p1). DC bias at 15 V shrinks each to a few µF — fine together with the 47 µF
  - 1× 47 µF 50 V electrolytic → its internal resistance damps ringing when the supply is plugged in hot; VM ramp limit is 4 V/µs (abs max), which the bulk also slows
- All rated ≥ 35 V (TI: at least 2× the 15 V operating voltage)

**Input protection** (order: connector + → PTC → P-FET → TVS to GND → bulk caps → VM)
- PTC resettable fuse SMD1812P150TF/24: hold 1.5 A, trip 3 A, 24 V max, 35 A max fault current, ≤ 0.16 Ω after tripping (RUILON p4). Slow (seconds) → backup only; the DRV8316's own overcurrent protection acts in µs. Trips earlier when the housing is warm.
- Reverse polarity: P-FET (AO3401A) in the + line, **drain → input side (PTC), source → VM**, gate → 10 kΩ → PGND, 10 V zener gate ↔ source (cathode at source) to keep VGS within the FET's **±12 V limit (AO3401A p1, confirmed)**. BZT52C10: 9.4–10.6 V at 5 mA (Changjing p2). RDS(on) ≤ 50 mΩ at VGS −10 V (p2) → loss at 1 A ≈ 0.05 W vs 0.4 W for a diode.
- TVS SMBJ18A (unidirectional, RUILON): 18 V standoff, 20.0–22.1 V breakdown, 29.2 V clamp at 20.6 A (p2) → off at 15 V, clamps below the 40 V abs max. After the P-FET so it never sees reverse polarity. Band = cathode (p1).
- Power in: 2-pin 3.5 mm screw terminal (KF350), + / − marked on silkscreen. 7 A (UL), 24–18 AWG, 8.8 mm tall, Ø1.0 mm holes (Kefa drawing). Not polarized → the P-FET covers reversed wires. Cable: PD trigger screw terminal → 18 AWG lead with the 2 A inline fuse in + → board terminal (same lead + alligator clips onto the bench supply for first power-up, current-limited).
- Ground naming: the connector −, TVS, bulk caps and DRV8316 PGND pins are net **PGND** (motor current return); everything else is **GND**; the two meet at one net-tie next to the DRV8316 (p83).

## 3. 3.3 V rail (STM32, MT6701, CAN transceiver)
- From the DRV8316's built-in buck, default 3.3 V (BUCK_SEL reset = 3.3 V, p67) → straight to the STM32 (no extra regulator). Output 3.1–3.5 V (p8)
- Budget ≈ 76 mA (STM32 ~40 + MT6701 ≤ 14 (p7) + CAN ≤ 17 + LEDs ~5; STM32 number is an estimate) → 47 µH inductor (200 mA with 47 µH, 50 mA with 22 µH — Table 8-5 p26)
- Inductor saturation current ≥ **900 mA** = the buck current limit maximum (IBK_CL, BUCK_CL = 0: 360/600/900 mA min/typ/max, p10). SMNR5040-470M: 1.1 A saturation, 1.0 A heating, DCR ≤ 0.353 Ω (SXN p14) ✔
- Output cap 22 µF, ≥ 10 V, X5R/X7R (Table 8-1 p19) → Samsung CL21A226MAQNNNE 22 µF 25 V 0805 (Samsung list p21)
- FB_BK connected to the buck output after the inductor (p4)
- At 3.3 V buck output, AVDD must run from VM, not the buck → firmware sets BUCK_PS_DIS = 1 (reset value is 0; sequencing only supported at 5.0/5.7 V — 8.3.4.4 p29, Table 8-5 p26)

## 4. DRV8316 wake-up (nSLEEP)
- DRV8316 is asleep at power-on → buck off → STM32 has no power
- nSLEEP pulled high from VM: 10 kΩ from VM + 3.3 V zener (BZT52C3V3, MDD: 3.14–3.47 V at 5 mA, 500 mW, p3) to GND. Resistor drops the extra ~11.7 V (≈ 1.2 mA, 14 mW; ≈ 2.6 mA / 67 mW at a 29 V spike), zener holds nSLEEP at ~3 V. nSLEEP: high above 1.6 V, 150–300 kΩ internal pull-down (p10), logic max 5.5 V (p6)
- NOT from AVDD: AVDD is off while the chip sleeps (8.4.1.1 p53)
- nSLEEP not connected to the STM32 → firmware can never cut its own power
- Faults cleared over SPI (CLR_FLT) instead

## 5. DRV8316 small support caps (ceramic X5R/X7R, right at the pins)
- Flying cap: 47 nF, ≥ 35 V (TI: ≥ 2× operating voltage), CPH ↔ CPL
- Storage cap: 1 µF, 16 V, CP ↔ VM (only sees VCP = 3.6–5.25 V across it, p7)
- AVDD cap: 1 µF X7R, **≥ 16 V, 0603**, AVDD ↔ AGND. TI: effective capacitance must stay 0.7–1.3 µF at 3.3 V (Table 8-1 p19) — a 6.3 V part loses too much to DC bias
- Why the charge pump: high-side FETs need a gate voltage above VM to turn fully on

## 6. PWM (STM32 → DRV8316), 3x PWM mode
- INHA / INHB / INHC ← STM32 TIM1 CH1 / CH2 / CH3 (PA8 / PA9 / PA10, same as dev rig)
- INLA + INLB + INLC tied together → one STM32 GPIO (`PHASE_EN`, PB1), low at boot (DRV8316 inputs have internal ~100 kΩ pull-downs, p10 → low before firmware runs)
- 3x mode truth table: INLx = 0 → phase Hi-Z; INLx = 1 → phase follows INHx (Table 8-4 p22)
- Reset mode is 6x (PWM_MODE = 00b, p63); with all inputs pulled low every phase is Hi-Z → safe at boot
- Boot order: write 3x mode over SPI → set INLx GPIO high → enable motor (TI: don't change PWM mode while switching, p20)

## 7. DRVOFF and nFAULT
**DRVOFF** (STM32 → DRV8316): motor on/off switch
- High = all 6 FETs off (motor coasts), low = normal (p4, 8.4.2 p53)
- Internal pull-down (~100 kΩ, p10) would mean motor ON at boot → add 10 kΩ pull-up to 3.3 V (pin then sits at ~3.0 V = high, VIH 1.5 V). Second safety layer: INLx is also low at boot.
- STM32 GPIO output (`DRV_OFF`, PB0), initial level High; `enable()` = low, `disable()` = high
- DRVOFF high can itself pull nFAULT low (Note p53) → at boot nFAULT may already be low: enable the fault interrupt only after DRV_OFF goes low, then CLR_FLT

**nFAULT** (DRV8316 → STM32): fault alarm, low = fault
- Open-drain → **5.1 kΩ pull-up to AVDD** (Table 8-1 p19)
- NOT to the buck 3.3 V rail: a pull-up supply must be > 2.2 V at power-up or the chip enters test mode (p5, p53), and the buck rail is still 0 V at that moment. AVDD comes up when the chip wakes (inference: TI doesn't say which rail it samples first) and can source 30 mA (p7).
- STM32 EXTI input (`DRV_FAULT`, **PB11**, falling edge, EXTI15_10 interrupt); on fault → `disable()` + PI `reset()`, latched; read SPI status registers to see which fault
- **Why PB11, not PB2 (changed Nov 8):** PB2 is a TT pin (3.6 V tolerant, DS p53) whose input may only go to VDD + 0.3 V while running (DS p71). AVDD can be 3.465 V (p7) while the buck VDD can be 3.1 V (p8) → up to 65 mV over the limit. PB11 is 5 V tolerant (FT_a, DS p54) and was free.

## 8. SPI (STM32 ↔ DRV8316CR)
- SPI3: SCLK ← PB3, SDO → PB4 (MISO), SDI ← PB5 (MOSI); nSCS ← PA15 (`DRV_CS`, low = selected). 16-bit frames, up to 5 MHz (p1)
- SDO resets to **push-pull** (SDO_MODE = 1h, p63). Keep a 10 kΩ pull-up to 3.3 V anyway: the pin table still asks for one (p5), it's harmless in push-pull, and it lets open-drain mode work if we ever switch
- nSCS has an internal pull-up (80–130 kΩ, p10) → deselected at boot, no resistor needed
- Own SPI bus, separate from the MT6701
- DRV settings reset every power-up → firmware writes them every boot (PWM mode, current-sense gain, overcurrent limit, ...)
- PB4 gotcha: a hidden 5.1 kΩ pull-down (USB-C "dead battery") turns on whenever PA10 (INHC PWM) is high, unless UCPD1_DBDIS = 1 in PWR_CR3 (DS p60 note 6). Firmware must call `HAL_PWREx_DisableUCPDDeadBattery()` at boot (CubeMX usually generates it — check). Same for PB6 when PA9 is high.

## 9. Current sensing (DRV8316 → STM32 ADC)
- SOA / SOB / SOC, one per phase: $V_{SOx} = \frac{V_{REF}}{2} \pm GAIN \times I$ → solve for I (p38, p73)
- SOA → PA0, SOB → PA1, SOC → PA2 (ADC1_IN1–IN3; PA0/PA1 are ADC12_IN1/IN2, DS p52), each through an RC filter (TI: CSA supports only capacitive load, use an RC filter, p39)
- **RC filter: 330 Ω + 220 pF C0G → cutoff ≈ 2.2 MHz** (changed Nov 8 from 2.2 nF / 220 kHz). TI: cutoff ≥ 100× the PWM frequency for sinusoidal control and suggests 330 Ω + 22 pF (p73); 220 pF = 110× at 20 kHz and still gives the ADC's ~5 pF sampling cap a 40× charge reservoir. Tune on the bench.
- **VREF → AVDD**, 0.1 µF cap to AGND. Datasheet: VREF must be 2.8 V up to AVDD (p6), and in 3x mode it is the CSA reference (p20). The buck rail can sit higher than AVDD (buck 3.1–3.5 V vs AVDD 3.1–3.465 V), so it must not feed VREF.
- Linear output range: 0.25 V to VREF − 0.25 V (p12) → max current = (VREF/2 − 0.25) / gain. Worst case VREF = 3.1 V:

  | Gain | Range (VREF 3.1 V) | Resolution (12-bit ADC) |
  |---|---|---|
  | 0.15 V/A | ±8.7 A | ~5.4 mA/count |
  | 0.3 V/A | ±4.3 A | ~2.7 mA/count |
  | 0.6 V/A | ±2.17 A | ~1.3 mA/count |
  | 1.2 V/A | ±1.08 A | ~0.7 mA/count |

- Gain is a firmware setting (CSA_GAIN, reset 0.15 V/A, p66), not a schematic decision; pick from the motor's max current (likely 0.6 V/A)
- PA0/PA1 are also reachable by ADC2 → later upgrade: sample two phases at the same instant
- Firmware: measure each SOx with the motor disabled at boot → store as the zero-current offset (VREF/2 isn't exactly the ADC midpoint)
- Firmware: TIM1 triggers the ADC at the PWM centre (all low-side FETs on); cap max duty a bit below 1.0
- Small corner: SOx can approach AVDD (≤ 3.465 V) while VDDA can be 3.1 V. Only at full-scale current, through 330 Ω, below the TT pins' 4.0 V abs max (DS p69) → accepted.

**VM sense:** 100 kΩ / 10 kΩ divider → PA3 (ADC1_IN4) + 100 nF (÷11: 15 V → 1.36 V, 36 V → 3.27 V). Firmware reads the real bus voltage instead of hard-coding vbus.

## 10. Parts
**Chips**
- **STM32G431CBT6**: 48-pin LQFP, 128 KB flash, 38 I/O. Chosen over CBU6 (UFQFPN): cheaper, better stock, visible legs → hand rework and probing possible. CubeMX must use STM32G431CBTx (pin numbers differ by package). Symbol pins 1–48 checked against DS Table 12 (p51–60).
- **DRV8316CRRGFR**: SPI version of the newer "C" chip. TI says its main change is improved current-sense amp specs and recommends it for new designs. Symbol pins 1–41 checked against Table 6-1 (p4–5). Backup: DRV8316RRGFR (C5218861).
- **MT6701CT-STD-R**: SOP-8 like the breakout; SSI on pins 6 (DO) / 7 (CLK) / 8 (CSN) (p10); VDD 3.0–5.5 V, ≤ 14 mA, ready ≤ 32 ms after power-up (p7); MODE and CSN have internal 200 kΩ pull-ups (p3); MODE → 3.3 V (the datasheet doesn't say in words which level selects I2C/SSI; its I2C and SSI reference circuits leave MODE on the pull-up = high (p20, p24), and the breakout runs I2C with MODE at VDD); 0.1 µF on VDD; the reference circuit's 6 V TVS on VDD is "recommended for better reliability" (p24) — skipped, on-board rail; SSI clock period ≥ 64 ns → ≤ 15 MHz (p25); 24-bit reads incl. 6-bit CRC
- **SN65HVD230DR** (TI original): pins D1 GND2 VCC3 R4 Vref5 CANL6 CANH7 Rs8 (p5); Rs → GND (high-speed mode, p5), Vref unconnected, 0.1 µF on VCC; VCC 3.0–3.6 V (p6); 120 Ω termination via solder jumper (only end-of-bus boards terminate)

**Parts table (JLCPCB / LCSC numbers, Nov 5–6; ratings checked against maker datasheets Nov 8)**

| Part | MFR part # | LCSC # | Type |
|---|---|---|---|
| MCU | STM32G431CBT6 | C529355 | Extended |
| Motor driver | DRV8316CRRGFR | C5447274 | Extended |
| Encoder | MT6701CT-STD-R (SOP-8, SSI) | C3003196 | Extended |
| CAN transceiver | SN65HVD230DR (TI) | C12084 | Promotional Extended |
| Buck inductor | SXN SMNR5040-470MT (47 µH, shielded, 1.1 A sat, 1.0 A heat, ≤ 0.353 Ω) | C9392 | Extended |
| Bulk electrolytic | KNSCHA RVT47UF50V67RV0029 (47 µF 50 V, 6.3×7.7 mm, 70 mA ripple) | C2887275 | Extended |
| Bulk ceramics (×2) | FH 1206B106K500NT (10 µF 50 V X7R 1206) | C303950 | Extended |
| Buck output cap | Samsung CL21A226MAQNNNE (22 µF 25 V X5R 0805) | C45783 | Basic |
| nSLEEP zener | BZT52C3V3 (3.3 V, 500 mW, SOD-123), MDD | C173413 | Extended |
| Crystal | YXC X50328MSB2GI (8 MHz, 20 pF load, ESR ≤ 80 Ω, C0 ≤ 5 pF, drive ≤ 300 µW, 5×3.2 mm) | C115962 | Basic |
| PTC fuse | RUILON SMD1812P150TF/24 (1.5 A hold, 3 A trip, 24 V, 1812) | C32404 | check |
| Reverse-polarity FET | AO3401A (P-ch, −30 V, 4 A, ≤ 50 mΩ @ −10 V, ±12 V gate, SOT-23) | C15127 | Basic |
| FET gate clamp zener | BZT52C10 (10 V, SOD-123), Changjing | C2103 | Extended |
| TVS | SMBJ18A (18 V standoff, 29.2 V clamp, SMB), RUILON | C10212 | check |
| Power connector | Cixi Kefa KF350-3.5-2P (screw terminal, 7 A UL, 24–18 AWG, 8.8 mm tall; through-hole, hand-soldered) | C474892 | hand |
| CAN connectors (×2, daisy-chain in/out) | JST GH 4-pin SM04B-GHS-TB (SMD; 1 A, 50 V, AWG 30–26); pinout 1 VCC (n/c for now), 2 CANH, 3 CANL, 4 GND | C189895 | check |
| Motor phases | 3 pads on top beside the edge notch, wires soldered directly | — | — |
| Mounting hardware (not on BOM) | 4× M2.5 × 8 mm stainless/nylon screws + 4× ~1 mm nylon M2.5 washers | — | buy |
| SWD | 1×5 2.54 mm header: 3V3, SWDIO, SWCLK, NRST, GND (hand-soldered) | — | hand |
| Debug UART (backup) | 2 test pads on PB6 / PB7 (USART1) | — | — |

- ~8 Extended part types (~US$21–24 loading fees); everything else Basic
- Stock/price: confirm when uploading the BOM (JLCPCB's BOM tool flags shortages)

**Small parts (all Basic, 0603 unless noted; exact LCSC numbers from JLCPCB's Basic list when building the BOM)**
- Caps: 0.1 µF 50 V X7R (VM ×6, MT6701, CAN, STM32 VDD ×3, VREF, VM sense), 1 µF X7R ≥ 16 V (CP, AVDD, VDDA, VREF+), 47 nF 50 V (CPH–CPL), 4.7 µF (STM32), 10 nF (VDDA), **20 pF C0G ×2 (crystal)**, **220 pF C0G ×3 (current-sense filter)**
- Resistors: 10 kΩ (nSLEEP from VM, DRVOFF pull-up, SDO pull-up, P-FET gate), 5.1 kΩ (nFAULT → AVDD), 330 Ω ×3 (current-sense filter), 100 kΩ + 10 kΩ (VM sense), 120 Ω (CAN termination), 0 Ω (crystal series), LED resistors
- Ferrite bead (VDDA feed), status LED + power LED

## 11. STM32 support, pin map, clocks
**Support (DS12589 Fig. 16 p68 unless noted)**
- Each VDD pin (24, 36, 48): 100 nF at the pin + one 4.7 µF for the chip
- **VBAT (pin 1) → 3.3 V** (added Nov 8): VDD, VDDA and VBAT must always be connected (DS p69 note 1). No RTC battery; shares the pin-48 100 nF (pins 1 and 48 are neighbours)
- VDDA (ADC supply): 1 µF + 10 nF, fed from 3.3 V through a ferrite bead (cleaner ADC → better current sensing); 0 Ω fallback footprint
- VREF+ (separate pin 20 on LQFP-48): connect to VDDA + 1 µF + 100 nF. It's the ADC's reference → decides current-sense accuracy.
- NRST: 100 nF to GND (internal pull-up already exists, DS p117), also on the SWD header
- BOOT0 / PB8 = FDCAN1_RX: no resistor. Option bytes **nSWBOOT0 = 0, nBOOT0 = 1** (the datasheet calls the first one nBOOT_SEL, DS p19) → always boots from flash, PB8 level ignored (ST's own B-G431B-ESC1 ships this way). Set once with STM32CubeProgrammer over SWD.
- Crystal: **2× 20 pF C0G** load caps. $C = 2(C_L - C_{stray}) = 2(20 - 10)$ with ST's 10 pF stray estimate (DS p100). 0 Ω series resistor on OSC_OUT (swap if drive level is too high; crystal max 300 µW). Startup margin with the crystal's real numbers (ESR ≤ 80 Ω, C0 ≤ 5 pF): $g_{m,crit} = 4 \cdot ESR \cdot (2\pi f)^2 (C_0 + C_L)^2 \approx 0.51$ mA/V vs G4 max 1.5 mA/V (DS p100) ✔

**Pin map (CubeMX `hardware/pinplan/pinplan.ioc`, done Nov 6; DRV_FAULT moved Nov 8)**

| Pin | Function | Connects to | Notes |
|---|---|---|---|
| PA8 / PA9 / PA10 | TIM1_CH1 / CH2 / CH3 | DRV8316 INHA / INHB / INHC | same as dev rig |
| PB1 | GPIO out `PHASE_EN`, init Low | DRV8316 INLA + INLB + INLC | |
| PB0 | GPIO out `DRV_OFF`, init High | DRV8316 DRVOFF | + 10 kΩ pull-up to 3.3 V |
| **PB11** | EXTI11 falling edge `DRV_FAULT` | DRV8316 nFAULT | + 5.1 kΩ pull-up to AVDD; FT pin (was PB2) |
| PB3 / PB4 / PB5 | SPI3 SCK / MISO / MOSI | DRV8316 SCLK / SDO / SDI | SDO pull-up; see PB4 gotcha §8 |
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
| VBAT (pin 1) | power | 3.3 V | must not float |
| Free | PA4–PA7 (ADC2-capable), PB2, PB15, PC13–PC15 | | |

- Alternate functions checked against DS Table 12 (p52–60): PA8–10 TIM1_CH1–3; PB3/4/5 SPI3; PB13/14 SPI2; PB8/9 FDCAN1; PA0–3 ADC.
- All GPIOs: no internal pull (the board or the other chip handles it), speed Low
- **Clocks:** HSE 8 MHz → PLL /M 2, ×N 85, /R 2 → 170 MHz SYSCLK; AHB / APB1 / APB2 /1 (same as dev rig); USB ← HSI48 (+ CRS synced to USB); FDCAN ← PCLK1 170 MHz (divides evenly into 1 Mbps)

## 12. Mounting + encoder (decided Nov 5–6)
- Motor: GM3506, back face Ø40 mm; 4 stationary M2.5 threaded holes on a **24.0 × 24.0 mm square** (Ø34.0 circle), ~6.3 mm usable thread depth; everything on the face flush except the motor wires leaving the recessed pad pocket (wire pocket sits midway between two holes); magnet 0.1 mm proud
- **Low mount:** board ~1 mm above the face on nylon M2.5 washers; M2.5 × 8 mm stainless (A2) or nylon screws (≈5.4 mm thread engagement). No standoffs.
- **Board: Ø50 mm round**, centred on the motor axis; 4 × Ø2.7 mm holes on the 24 × 24 mm square
- **MT6701 on top, exact centre**: gap ≈ 1 mm washer + 1.6 mm PCB − 0.1 mm magnet ≈ 2.5 mm to the board top (≈ 3.25 mm to the die) — the 2.8 mm paper test passed with the die at ~3.55 mm
- **No through-hole legs inside the Ø40 motor circle** (only flat SMD there); screw terminal + SWD header in the 5 mm overhang ring, legs trimmed flush; bottom copper under solder mask (insulates from the metal motor face)
- **Window in the board above the wire pocket** (~8 × 13 mm); the 3 motor wires come up through it and solder to 3 pads on top beside it
- Layout: motor-current traces (VM, OUTA/B/C) away from the MT6701; shielded inductor; steel screws away from the magnet; separate SW_BK and FB_BK with ground, wide FB_BK trace (p83)
- Connectors on the board edge (screw terminal wire side facing out, USB-C, GH); screwdriver access to the terminal screws once in the housing → reachable through the housing wall

## 13. Assembly + manufacturer
- All SMD parts assembled by the factory (DRV8316 thermal pad can't be done with an iron); single-sided placement (Economic assembly)
- Through-hole connectors (screw terminal, SWD header) soldered by hand
- Choose parts that JLCPCB/LCSC has in stock while designing
- When done: quote the same files at JLCPCB and PCBWay (student program: 50% off manufacturing + assembly charges, up to US$200; needs PCBWay logo on silkscreen) → pick the cheaper
- Thermal pad → AGND, top-layer ground area, many vias to a big bottom-layer ground; PGND and AGND separate, joined at one point (net-tie or wide resistor) (p83)

## 14. Firmware boot settings (from the hardware choices above)
1. Call `HAL_PWREx_DisableUCPDDeadBattery()` (UCPD1_DBDIS = 1) before any PWM — PB4/PB6 pull-down gotcha (§8)
2. Write DRV8316 settings over SPI: PWM_MODE = 3x, BUCK_PS_DIS = 1, CSA_GAIN, OCP mode/level. OVP: 34 V and enabled are already the reset values (OVP_SEL = 0, OVP_EN = 1, p64) — write them anyway so the intent is in code (the TVS clamps spikes to ~29 V, which would trip the 22 V setting)
3. Measure current-sense zero offsets (motor disabled)
4. Wait ≥ 32 ms after power-up before the first MT6701 read (p7)
5. PHASE_EN high → DRV_OFF low (enable) → CLR_FLT (DRVOFF high may have pulled nFAULT low, p53) → enable the PB11 interrupt
- Never touch nSLEEP (not connected)
- One-time: option bytes nSWBOOT0 = 0, nBOOT0 = 1
- To verify when writing firmware: register reset values; TIM1 ADC trigger exactly at the peak (with repetition counter 1, the update event lands on either the peak or the valley → a CH4 compare near ARR is the usual trigger)

## 15. Open questions
- Board outline: done in Onshape on the motor model (Ø50, 4 holes on 24 × 24 mm, ~8 × 13 mm window over the motor pads, MT6701 centred; 1 mm nylon washers; 1.6 mm board) → DXF to `hardware/outline/board-outline.dxf` (outline + holes + window only). Motor wires must go up through the window (shorten them), never out over the rim under the board.
- Layer count: plan 4 layers (solid ground planes for driver + MCU + current sense; MicroSpora used 6); confirm price at quote
- USB: works electrically; keep only if the USB-C connector fits (decide during placement). If kept: pick USB-C connector + ESD chip
- Connector placement (types decided; positions need the outline)
- Closed Nov 8 (datasheet pass): AO3401A VGS ±12 V ✔; MT6701 MODE level ✔ (inference, strong); STM32 decoupling from DS Fig. 16 ✔ (AN5093 not needed); crystal startup margin ✔; 3.3 V budget ✔ (~76 mA of 200 mA)
- Still open: real STM32 run current (measure on the bench); nFAULT-to-AVDD at power-up is an inference — check on first power-up that the chip doesn't enter test mode

## 16. Library status (Nov 8)
All parts are in `actuator-driver.SchLib` / `.PcbLib`, footprints checked against the maker's land pattern; solder mask Manual 0.05 mm (AO3401A on Rule → set board rule Mask → Solder Mask Expansion to 0.05 mm before layout).
- Chips: DRV8316C (TI land pattern, pads/thermal pad/paste fixed), STM32G431 (ST Fig. 50), SN65HVD230 (TI D0008A), MT6701 (reuses D0008A)
- Discretes: AO3401A, SMBJ18A, BZT52C3V3/C10 (SOD-123, MDD), PTC 1812 (RUILON), crystal 5032 2-pad (YXC), SMNR5040, 47 µF can (EasyEDA, pad 1 = +), LED_0603 (pad 1 = cathode)
- Generic: RES_0603, CAP_0603/0805/1206, FB_0603 (Comment = `=Value`)
- Connectors / board-only: KF350 screw terminal, JST GH SM04B ×2 (tabs 5/6 unconnected), SWD 1×5 header, TP_1.5 test pad, SJ_2P solder jumper (ships open), MOTOR_PADS_3 (2.0 × 3.0 mm at 3 mm pitch; motor wire ≈ 0.5 mm)
- Tallest parts: screw terminal 8.8 mm, SWD header 8.5 mm, 47 µF cap 7.7 mm → housing must clear ≥ 9 mm above the board, or use a low-profile/right-angle header
- To buy: JST GH 4-pin pre-crimped cables (CANable ↔ board)
- BOM step: add LCSC number field to every part; fix 47 µF Comment case
- **To fix before drawing (Nov 8):** 47 µF symbol pin 1 renamed to `+` (polarity visible on the schematic)
- **To fix before BOM (Nov 8):** generic CAP/FB/RES symbols carry copied resistor parameters (Resistance 10kR, Case 0603 on the 1206 cap, Voltage 75 V) → strip them; BZT52C3V3/C10 symbols carry Diodes Inc. SOD-323 / 200 mW metadata and the C10 says Vz 3.3 V → strip
