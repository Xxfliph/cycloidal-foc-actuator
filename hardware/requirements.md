# PCB requirements (draft, Nov 5, 2026)

## 1. Purpose
Replaces the dev rig (WeAct STM32, SimpleFOC Mini, MT6701 breakout) with one board:
STM32G431 + DRV8316 motor driver (built-in current sensing) + CAN transceiver + MT6701 encoder read over SPI instead of I2C.

## 2. Power in
- 12–15 V in (PD trigger or bench supply) → DRV8316 VM pins (3 pins)
- 2× 0.1 µF ceramic per VM pin, right at the pins → fast spikes when a FET switches
- 1× ~47 µF bulk cap close to the chip → bigger dips (datasheet minimum: ≥ 10 µF)
- All rated ≥ 35 V (TI: at least 2× the 15 V operating voltage)
- Bulk cap = electrolytic or polymer (its small internal resistance damps ringing when the supply is plugged in hot); VM ramp limit is 4 V/µs (abs max), which the bulk cap also slows

## 3. 3.3 V rail (STM32, MT6701, CAN transceiver)
- From the DRV8316's built-in buck, default 3.3 V → straight to the STM32 (no extra regulator)
- Budget ≈ 65 mA → 47 µH inductor (22 µH is only good to 50 mA)
- Inductor saturation current ≥ 600 mA (buck peak); 22 µF output cap (Table 8-1 lists 1 µF but that row's text is copied from the AVDD row; the electrical specs and TI's forum engineers use 22 µF)
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
- INHA / INHB / INHC ← STM32 TIM1 CH1 / CH2 / CH3 (same as dev rig IN1–3)
- INLA + INLB + INLC tied together → one STM32 GPIO ("phase enable"), low at boot (DRV8316 inputs have internal pull-downs → low before firmware runs)
- Boot order: write 3x mode over SPI → set INLx GPIO high → enable motor (TI: INHx and INLx low while changing PWM mode)

## 7. DRVOFF and nFAULT
**DRVOFF** (STM32 → DRV8316): motor on/off switch
- High = all 6 FETs off (motor coasts), low = normal
- Internal pull-down (~100 kΩ) would mean motor ON at boot → add 10 kΩ pull-up to 3.3 V (pin then sits at ~3.0 V = high)
- STM32 GPIO output, initial level High; `enable()` = low, `disable()` = high

**nFAULT** (DRV8316 → STM32): fault alarm, low = fault
- Open-drain → **5.1 kΩ pull-up to AVDD** (datasheet Table 8-1 value)
- NOT to the buck 3.3 V rail: datasheet says a pull-up supply must be > 2.2 V at power-up or the chip enters test mode, and the buck rail is still 0 V at that moment. AVDD is up as soon as the chip wakes.
- STM32 GPIO input; on fault → `disable()` + PI `reset()`, latched; read SPI status registers to see which fault

## 8. SPI (STM32 ↔ DRV8316R)
- SCLK ← SPI SCK, SDI ← SPI MOSI, SDO → SPI MISO, nSCS ← STM32 GPIO (low = selected)
- SDO is open-drain → needs a pull-up
- nSCS has an internal pull-up → deselected at boot, no resistor needed
- Own SPI bus, separate from the MT6701
- DRV settings reset every power-up → firmware writes them every boot (PWM mode, current-sense gain, overcurrent limit, ...)

## 9. Current sensing (DRV8316 → STM32 ADC)
- SOA / SOB / SOC, one per phase: $V_{SOx} = \frac{V_{REF}}{2} \pm GAIN \times I$ → solve for I
- Each SOx → its own STM32 ADC pin, through a small RC filter (TI recommendation)
- **VREF → AVDD**, 0.1 µF cap to AGND. Datasheet: VREF must be 2.8 V up to AVDD, and in 3x mode it also powers the current-sense amps. The buck rail can sit higher than AVDD (buck 3.1–3.5 V vs AVDD 3.1–3.465 V), so it must not feed VREF.
- Linear output range: 0.25 V to VREF − 0.25 V → max current ≈ 1.4 V / gain:

  | Gain | Range | Resolution (12-bit ADC) |
  |---|---|---|
  | 0.15 V/A | ±9.3 A | ~5.4 mA/count |
  | 0.3 V/A | ±4.7 A | ~2.7 mA/count |
  | 0.6 V/A | ±2.3 A | ~1.3 mA/count |
  | 1.2 V/A | ±1.2 A | ~0.7 mA/count |

- Gain is a firmware setting (SPI), not a schematic decision; pick from the motor's max current (likely 0.6 V/A)
- Firmware: measure each SOx with the motor disabled at boot → store as the zero-current offset (VREF/2 isn't exactly the ADC midpoint)
- Firmware: TIM1 triggers the ADC at the PWM centre (all low-side FETs on); cap max duty a bit below 1.0

## 10. Part choice + thermal pad
- DRV8316R (SPI): settings and fault reading in code. DRV8316T (resistor settings) ruled out.
- Package: 40-pin QFN, 7 × 5 mm, thermal pad underneath
- **Thermal pad → AGND** (DRV8316 pin table: "must be connected to analog ground"), top-layer ground area, many vias to a big bottom-layer ground
- PGND (motor current) and AGND (quiet, current sense) separate, joined at one point (datasheet: net-tie or wide resistor)
- Heat is small for this motor (~0.5 W → ~38 °C)

## 11. Mounting + encoder (decided Nov 5)
- MT6701 on the main board, centred over the magnet (magnet press-fit in a printed sleeve in the motor's back face)
- Board screwed to the motor's 4 outer back-face holes (confirmed stationary) on standoffs → round board, ~motor-face size
- Design gap ~2 mm (tested: 0 spread + smooth both ways at 2.8 mm)
- Layout: motor-current traces (VM, OUTA/B/C) away from the MT6701; steel screws away from the magnet

## 12. Assembly + manufacturer
- All SMD parts assembled by the factory (DRV8316 thermal pad can't be done with an iron)
- Through-hole connectors soldered by hand (or factory hand-assembly)
- Choose parts that JLCPCB/LCSC has in stock while designing
- When done: quote the same files at JLCPCB and PCBWay (student program: 50% off manufacturing + assembly charges, up to US$200; needs PCBWay logo on silkscreen) → pick the cheaper

## 13. Firmware boot settings (from the hardware choices above)
1. Write DRV8316 settings over SPI: PWM_MODE = 3x, BUCK_PS_DIS = 1, CSA_GAIN, OCP mode/level, OVP threshold (22 V or 34 V option; 22 V protects a 15 V system better but could trip on plug-in ringing)
2. Measure current-sense zero offsets (motor disabled)
3. INLx GPIO high → DRVOFF low (enable)
- Never touch nSLEEP (not connected)
- To verify when writing firmware: register reset values (not in the datasheet text I could read); whether setting DRVOFF high pulls nFAULT low (it does on the sister chip MCT8316Z); TIM1 trigger exactly at the peak (with repetition counter 1, the update event lands on either the peak or the valley → a CH4 compare near ARR is the usual trigger)

## 14. Open questions (decide before the schematic)
**Parts**
- DRV8316R vs DRV8316C (CR): JLCPCB stock
- Exact parts: 47 µH inductor (Isat ≥ 600 mA), ~47 µF 35 V+ bulk cap, 3.3 V zener
- Confirm the 3.3 V budget with real numbers (STM32G431, MT6701, CAN transceiver)

**STM32**
- USB on the board or not (PA11/PA12 clash with CAN on the G431)
- CAN pins vs BOOT0 (PB8 problem from MicroSpora)
- Which SPI for the DRV8316, which for the MT6701
- Crystal: keep it, add the series resistor
- Basics not covered yet: decoupling caps, NRST, BOOT0, SWD connector

**CAN**
- Transceiver part (in stock), termination resistor

**Board**
- Power input: fuse, reverse-polarity protection
- Connectors: power in, 3 motor phases, CAN, SWD
- Outline + standoff height (needs calipers: 4-hole spacing, thread, back-face Ø, magnet protrusion)
