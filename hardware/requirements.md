# PCB requirements (draft, Nov 5, 2026)

## 1. Purpose
Replaces the dev rig (WeAct STM32, SimpleFOC Mini, MT6701 breakout) with one board:
STM32G431 + DRV8316 motor driver (built-in current sensing) + CAN transceiver + MT6701 encoder read over SPI instead of I2C.

## 2. Power in
- 12–15 V in (PD trigger) → DRV8316 VM pins (3 pins)
- 2× 0.1 µF ceramic per VM pin, right at the pins → fast spikes when a FET switches
- 1× ~47 µF bulk cap close to the chip → bigger dips
- All rated ≥ 35 V (TI: at least 2× the 15 V operating voltage)

## 3. 3.3 V rail (STM32, MT6701, CAN transceiver)
- From the DRV8316's built-in buck, default 3.3 V → straight to the STM32 (no extra regulator)
- Budget ≈ 65 mA → 47 µH inductor (22 µH is only good to 50 mA)
- Inductor saturation current ≥ 600 mA (buck peak); 22 µF output cap
- FB_BK connected to the buck output after the inductor

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
- High = all 6 FETs off (motor coasts), low = motor ON
- Internal pull-down would mean motor ON at boot → add 10 kΩ pull-up to 3.3 V
- STM32 GPIO output, initial level High; `enable()` = low, `disable()` = high

**nFAULT** (DRV8316 → STM32): fault alarm, low = fault
- Open-drain → 10 kΩ pull-up to 3.3 V (otherwise floating pin → fake faults)
- STM32 GPIO input; on fault → `disable()` + PI `reset()`, latched; read SPI status registers to see which fault

## 8. SPI (STM32 ↔ DRV8316R)
- SCLK ← SPI SCK, SDI ← SPI MOSI, SDO → SPI MISO, nSCS ← STM32 GPIO (low = selected)
- SDO is open-drain → needs a pull-up (same reason as above)
- Own SPI bus, separate from the MT6701
- DRV settings reset every power-up → firmware writes them every boot (PWM mode, current-sense gain, overcurrent limit, ...)

## 9. Current sensing (DRV8316 → STM32 ADC)
- SOA / SOB / SOC, one per phase: $V_{SOx} = \frac{V_{REF}}{2} \pm GAIN \times I$ → solve for I
- Each SOx → its own STM32 ADC (Analog to Digital Converter) pin, through a small RC filter (TI recommendation)
- VREF → 3.3 V, 0.1 µF cap to GND
- Gain set over SPI (lowest = 0.15 V/A)
- Firmware: TIM1 triggers the ADC at the PWM centre (all low-side FETs on); cap max duty a bit below 1.0

## 10. Part choice + thermal pad
- DRV8316R (SPI): settings and fault reading in code. DRV8316T (resistor settings) ruled out.
- Package: 40-pin QFN, 7 × 5 mm, thermal pad underneath
- Thermal pad soldered to a top-layer ground area, many vias to a big bottom-layer ground
- PGND (motor current) and AGND (quiet, current sense) separate, joined at one point
- Heat is small for this motor (~0.5 W → ~38 °C)

## 11. Mounting + encoder (decided Nov 5)
- MT6701 on the main board, centred over the magnet (magnet press-fit in a printed sleeve in the motor's back face)
- Board screwed to the motor's 4 outer back-face holes on standoffs → round board, ~motor-face size
- Design gap ~2 mm (tested: 0 spread + smooth both ways at 2.8 mm)
- Layout: motor-current traces (VM, OUTA/B/C) away from the MT6701; steel screws away from the magnet

## 12. Assembly + manufacturer
- All SMD parts assembled by the factory (DRV8316 thermal pad can't be done with an iron)
- Through-hole connectors soldered by hand (or factory hand-assembly)
- Choose parts that JLCPCB/LCSC has in stock while designing
- When done: quote the same files at JLCPCB and PCBWay (student program: 50% off manufacturing + assembly charges, up to US$200; needs PCBWay logo on silkscreen) → pick the cheaper

## 13. Open questions (decide before the schematic)
**Parts**
- DRV8316R vs DRV8316C (CR): stock; thermal pad AGND vs PGND on the R
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
