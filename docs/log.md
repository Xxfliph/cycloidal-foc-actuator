# Build Log

One entry per work session. Write what you did, what broke, and what you decided. This becomes the "what broke and how I fixed it" section of the README.

---

## 2026-10-17
- Created repository and folder structure.

## 2026-10-2 to 2026-11-01 Learn FOC
- How a BLDC motor works (3 phases, pole pairs, GM3506)
- Six-step drive vs. FOC
- Core FOC idea: field 90° ahead of the magnet, d–q frame
- Current mode vs. voltage mode (back-EMF, why voltage mode first)
- PI control and cascaded loops
- Clarke and Park transforms (and their inverses)
- Sine PWM vs. SVPWM (+15% voltage, the hexagon)
- Center-aligned PWM on TIM1


## 2026-10-29: CubeMX/CubeIDE setup
- CubeIDE 2.x + standalone CubeMX; G474 project, 8 MHz HSE → PLL → 170 MHz, SWD, HAL tick on TIM6 (FreeRTOS later)
- C++ entry point app_main() in app.cpp
- USB CDC logging (HSI48 + CRS) → COM10
- TIM1 center-aligned 3-ch PWM, ARR 4250 → 20 kHz, verified 25/50/75% in PulseView (Zadig/WinUSB needed for this USB logic analyzer)
- Decided: pin map (PA8/9/10 PWM, PB0 DRV_EN, PA11/12 USB, PA15/PB7 I2C1); avoid PB8 (BOOT0)

## 2026-10-31: MT6701 and I2C test
- My MT6701 breakout doesn’t expose the SSI pins, so I’m using I2C (~8 kHz reads) for now; SSI on the custom PCB.
- I2C1 at 400 kHz on PA15/PB7; bus scan found the MT6701 at 0x06

## 2026-11-01: Writing FOC firmware
- FocController::computeDuties: inverse Park → inverse Clarke → SVPWM (min–max injection) → duties; ±6 V software limit (below the 8.66 V SVPWM max on 15 V)
- MotorDriver: CCR writes, PWM start, enable pin. Kept separate from the math so FocController is testable without hardware
- Verified SVPWM saddle waveforms on Serial Plotter: 154–846 ‰ at vq = 6 V / 15 V bus, matching 0.5 ± (√3/2)·vq/vbus
- Broke: max/min selection used strict `>`, so a two-way tie picked the smallest value as the max. Fixed with `>=`
- Broke: sign error in inverse Park Vβ (would reverse rotation); caught by checking θe = 0
- Broke: local `vbus` shadowed the member; member-name typos (vLimit vs vLimit_)
- Broke: newlib-nano can't printf floats → printed value ×1000 as int (alternative: -u _printf_float)

## 2026-11-02 - 2026-11-03: MT6701 Encoder - Encoder class, multi-turn, velocity

### Raw I2C read
- Angle = 14 bits across two registers: 0x03 = bits 13..6, 0x04 = bits 5..0 (in its upper 6 bits; low 2 bits ignored).
  `count = (reg03 << 6) | (reg04 >> 2)` → 0..16383 per mechanical turn (~0.022°/count).
- One `HAL_I2C_Mem_Read` of 2 bytes from 0x03 (register auto-increments), so both bytes come from the same
  sample. Two separate reads could stitch old + new bytes (~64-count glitch → velocity spike).
- HAL wants the 8-bit address form: 0x06 << 1 = 0x0C (R/W bit goes in bit 0). Plain 0x06 → HAL_ERROR.
- Timeout 5 ms (not HAL_MAX_DELAY) so a stuck bus can't freeze the loop while PWM keeps running.

### Encoder bench results (magnet by hand / taped, motor unpowered)
- Magnet hand-held over chip side: ~50 counts jitter (~1.1° mech ≈ 12° electrical).
- Magnet on back of board (PCB as spacer) or stuck to a header pin: ±1 count = sensor noise floor.
  → Gap and centring matter a lot: design a fixed ~1–2 mm, centred magnet gap into the mount.
- No magnet: random values (±~80 counts) but every read still HAL_OK.
  → "Bus worked" ≠ "data valid". TODO: read MT6701 field-strength status later.
- Turning: sawtooth 0 → 16383, wraps to 0. Screenshot: docs/img/<sawtooth>.png

### Encoder class (firmware/actuator_fw/Core/{Inc,Src}/encoder.*)
- `Encoder(I2C_HandleTypeDef* hi2c, uint8_t addr)`: takes the datasheet 7-bit address, shifts once in the
  initializer list.
- `update()`: one I2C read per call; updates state only on HAL_OK (keeps last good value on failure);
  returns the HAL status. Getters are const and never touch the bus, so each loop reads once and every
  user sees the same sample.
- `getRawCount()`, `getAngle()` (rad, 0..2π), `getMultiTurnAngle()` (rad, unbounded),
  `getRawVelocity()`, `getVelocity()` (filtered, rad/s).

### Multi-turn (unwrap)
- diff = new − prev (as int32_t); if diff > 8192 → −16384; if diff < −8192 → +16384; else unchanged.
  Always assumes the shorter way round, so the shaft must move < half a turn between reads
  (10 ms loop → max 50 rev/s; faster loop raises the limit). Threshold must be exactly half a turn.
- totalCount_ (int32_t) accumulates corrected diffs. First good read seeds prev/total (no fake jump).
- Verified: multi-turn angle climbs/falls smoothly across wraps, goes negative past start.

### Timing: DWT cycle counter
- Enabled once in app_main: `CoreDebug->DEMCR |= TRCENA`, `DWT->CTRL |= CYCCNTENA`.
  Read `DWT->CYCCNT` (170 MHz, ~6 ns resolution, wraps every ~25 s; unsigned subtraction handles the wrap).
- Measured loop period: ~1,870,000 cycles = 11.0 ms, not 10 ms (HAL_Delay adds a tick).
  Assuming 10 ms would have made velocity ~10% wrong → always measure Δt.

### Velocity
- velocity = (unwrapped diff → rad) / Δt, Δt = Δcycles / SystemCoreClock (float). Guard Δcycles > 0
  (avoids NaN, e.g. if the counter was never enabled).
- Still test: raw velocity is exactly 0 / ±34 mrad/s = ±1 count per 11 ms ((2π/16384)/0.011 ≈ 34.9).
  Matches prediction → angle conversion and Δt are correct.
- No-magnet test: ±4000 mrad/s (≈ ±115 counts per sample) → velocity amplifies angle noise.
  Faster loops make this worse (1 count at 1 kHz ≈ 380 mrad/s).

### Velocity low-pass filter (EMA)
- filt += α·(raw − filt), with α = Δt / (τ + Δt), τ = 20 ms. α recomputed each update from the measured Δt,
  so the reaction time stays ~τ regardless of loop rate or missed reads.
- At 11 ms: α ≈ 0.355. Still-test noise ±34 → ~±15 mrad/s; first step = 0.355 × 34 ≈ 12 and
  decay ×0.645 per step, both match the plot.
- Hand-spin test: filt is smoother, lags raw by ~1–2 samples (~20 ms), and shaves peaks
  (4200 → 3800, −6900 → −5800). Trade-off: bigger τ = smoother but laggier.
  Screenshot: docs/img/<raw_vs_filt>.png

## 2026-11-03 - 2026-11-04: First spin, calibration, closed-loop code

### Electrical angle (FocController)
- Added pole pairs, offset and direction to FocController (Encoder stays a pure sensor).
  `electricalAngle(mech) = fmodf(mech·11·dir − offset, 2π)`, +2π if negative.
- Verified by hand: elec = 11 × mech (mod 2π) at 5 positions.

### Dev rig + first open-loop spin (WARG bay bench supply)
- Keysight EDU36311A CH2: 12 V, 0.5 A limit → SimpleFOC Mini +/− screw terminal.
  Signals: PA8/PA9/PA10 → IN1/IN2/IN3, PB0 → EN, common GND. nRT/nSP/nFT/3.3V unconnected.
- Power-up with driver disabled: 11 mA.
- Open loop (vd 0, vq 1.5 V, θe +0.07 rad/loop): smooth spin, ~0.10 A. Reverse works.
- Stutters below vq ≈ 0.3–0.4 V (friction/cogging).
- Habit: supply output OFF before every flash.

### Encoder on the motor
- First try: flat encoder reading → magnet was on a part that doesn't rotate.
  Magnet must spin with the rotor, centred on the axis; chip fixed, facing it.
- Open-loop ramp vs encoder: same slope and direction → direction +1, 11 pole pairs.

### Calibration
- Hold vd at θe = 0 for 1 s, read encoder → offset; setCalibration(offset, +1).
- Mistake 1: calibrated with motor power off (firmware runs right after flashing) → wrong offset.
  Fix: output ON, then press NRST.
- Mistake 2: vd = 1.5 V too weak → friction/cogging left the rotor ~0.9 rad (electrical) off.
  Fix: calibrate at vd = 2.5 V.
- Verified: holding θe at 0 / π/2 / π / 3π/2 → encoder read 0 / 1570 / 3141 / 4712 mrad
  (within ~1 mrad). Offset ≈ 1.1 rad.

### Closed-loop voltage-mode FOC (written, compiles, not yet tested)
- Calibrate at boot (fail → driver off, print "cal failed"), then loop with no delay:
  encoder → θe → computeDuties(vd 0, vq 1 V, θe) → setDuty.
- 20 consecutive failed encoder reads → driver off (latched until reset).

### Encoder magnet choice
- Noise test (spread = max − min over 50 still reads):
  - off-centre, next to a steel header pin: big magnet 20–30, small 60–80
  - centred on the back of the board (~1.6 mm gap): big 0, small 2
- → Using the bigger magnet.
- Mount plan: magnet flush in the shaft end (printed centring)
  MT6701 centred under it at ~1.5–2 mm. Quick bracket/tape rig for now;
  final encoder placement comes with the custom PCB.

### PIController
- `PIController(kp, ki, outLimit)`, `update(error, dt)`, `reset()`.
  Caller computes the error, so it works for the velocity and position loops.
- update(): if dt > 0 → integral += Ki·e·dt, clamped to ±outLimit;
  output = Kp·e + integral, clamped to ±outLimit.
- Anti-windup: clamp the integral itself, not just the output.
  Otherwise a stuck rotor winds it up far past the limit and the motor stays at full vq after release.
- if dt ≤ 0 → skip integration only. reset() → integral = 0 (call after calibration / any disable).

## 2026-11-04 - 2026-11-05: Velocity loop, closed-loop FOC on hardware, tuning

### Velocity loop code
- Velocity PI runs every ~1 ms; FOC runs every pass with the latest vq.
  Timing: lap timer on DWT->CYCCNT (elapsed = now − last, uint32 handles the ~25 s wrap);
  dt = elapsed / 170e6 (measured, not assumed).
- error = target − getVelocity(); vq = PI.update(error, dt). vq and target declared
  before the loop so they persist between passes.
- Step command: target 0 until STEP_DELAY_MS after loop start (HAL_GetTick), then 10 rad/s.
- USE_VELOCITY_LOOP switch: false = constant VQ (FOC test), true = PI sets vq.

### Encoder placement test
- Test app.cpp: per 50 reads prints count, spread (still noise), max jump, direction flips, I2C errors.
- Still: spread 0. Full turn: smooth count, no jump spikes, err 0.
- Flips only when nudging by hand (cogging snap-back); continuous spin → 0 flips. Passes.

### Closed-loop voltage-mode FOC (first hardware test)
- VQ = +1 V: spins by itself at ~17.6 rad/s. VQ = −1 V: ~−17.9 rad/s. Symmetric.
- Slow ±1 rad/s ripple, ~once per revolution (mechanical/placement, not firmware).
- Mistake: vLimit 2 V clipped calibration's vd 2.5 V (computeDuties clamps vd too).
  Fix: FocController vLimit 3 V, PI limit 2 V as the safety cap.
- "cal failed" after an NRST: MT6701 I2C bus stuck (encoder stays powered through NRST).
  Fix: full power cycle (unplug motor power + STM32 USB).

### Velocity loop tuning (step 0 → 10 rad/s)
| Kp  | Ki | Peak | Settles     | Notes                                    |
|-----|----|------|-------------|------------------------------------------|
| 0.1 | 0  | 6.7  | 5.7         | vq 0.42 V = Kp·e (P-only balance point)  |
| 0.2 | 0  | 9.4  | 7.5         | vq hits 2 V limit at the step            |
| 0.4 | 0  | 11.8 | 8.6         | rings, vq goes negative (braking)        |
| 0.3 | 3  | 11.6 | 10.0 ± 0.35 | chosen: ~16% overshoot, on target ~0.2 s |
- P-only overshoot comes from lag (EMA τ 20 ms vs ~25 ms rise) → limits Kp.
- Integral learns the steady push: vq ~0.6–0.7 V at 10 rad/s, P ≈ 0.
- Pinch test: speed dips, recovers to 10 while still pinched.

## 2026-11-04 - 2026-11-06: PCB prep — DRV8316 datasheet, requirements, parts, pin plan

### DRV8316 datasheet
- Went through pins + typical application, PWM modes, current sensing, variants, thermal pad.
- Learned along the way: FETs and half-bridges, why the high side needs a charge pump,
  decoupling vs bulk caps, buck vs LDO, pull-ups, open-drain, floating inputs, zener clamps.

### Key decisions
- 3.3 V from the DRV8316's buck at its default 3.3 V, straight to the STM32 (47 µH inductor, ~72 mA budget).
- nSLEEP woken from VM (10 kΩ + 3.3 V zener), not connected to the STM32 → firmware can't cut its own power.
- 3x PWM mode: TIM1 CH1–3 → INHx (same as dev rig), all INLx on one enable GPIO.
- DRV8316CR (newer "C", better current-sense amps) + STM32G431CBT6 (LQFP-48, hand-reworkable).
- MT6701 goes on the main board, centred over the magnet; board screws to the motor's 4 stationary back-face holes.
- JLCPCB assembles the SMD parts (thermal pad can't be done with an iron); quote PCBWay too (student 50%).

### Mistakes caught (datasheet re-check)
- nSLEEP pull-up to AVDD wouldn't work: AVDD is off while the chip sleeps → chicken-and-egg. Fix: from VM + zener.
- nFAULT pull-up to the buck rail: chip can enter test mode if the pull-up supply is < 2.2 V at power-up. Fix: to AVDD.
- VREF from the buck rail: VREF must not exceed AVDD. Fix: VREF → AVDD.
- Current-sense gain: lowest gain (0.15 V/A) is too coarse for ~1 A → firmware setting, likely 0.6 V/A.
- Bulk cap: small electrolytics can't take the PWM ripple current → added 2× 10 µF ceramics.

### Parts + pin plan
- All parts picked with LCSC numbers (chips, power, input protection, crystal, connectors); passives as Basic parts at BOM time.
- CubeMX pin plan (`hardware/pinplan/pinplan.ioc`): CAN on PB8/PB9 + USB on PA11/PA12 both fit.
  BOOT0 clash on PB8 solved with option bytes (nSWBOOT0 = 0, nBOOT0 = 1), same as ST's B-G431B-ESC1.
- Clocks same as dev rig (8 MHz HSE → 170 MHz), USB from HSI48 + CRS, FDCAN from PCLK1.

## 2026-11-06 - 2026-11-07: Motor measurements, board outline, Altium project

### Motor back-face measurements (calipers)
- Back face Ø40 mm. 4 stationary mounting holes, **M2.5**, on a **24.0 × 24.0 mm square**
  (34.0 mm diagonal; centre-to-centre = average of near-edge and far-edge readings). ~6.3 mm usable thread.
- Magnet 0.1 mm sticking out. Everything else flush, except the motor wires leaving the recessed pad pocket
  (pocket sits midway between two holes).

### Mounting decision: low mount, MT6701 on top
- Board sits 1 mm above the motor face on nylon M2.5 washers (no standoffs), M2.5 × 8 mm stainless/nylon screws.
- MT6701 on the top side, centred over the magnet. Magnet → die ≈ 1 + 1.6 − 0.1 + ~0.75 ≈ 3.25 mm.
  The passing 2.8 mm paper test had the die at ~3.55 mm (chip faced the magnet), so the board is closer.
- Why a gap at all: the magnet sleeve spins, through-hole legs poke out the bottom, and the motor face is metal.
- Rules: no through-hole legs inside the Ø40 motor circle; motor wires go up through a window in the board
  (shortened), never out over the rim under the board.
- Board Ø50 mm: Ø40 is too tight for single-sided parts + routing, and the overhang ring holds the
  through-hole connectors. 50 mm = largest round board inside the planned ~60 mm housing → re-check after the cycloid CAD.

### Board outline (Onshape)
- Built on a downloaded GM3506 model, checked against the caliper numbers.
- Ø50 board, 4 holes on the 24 × 24 mm square, ~8 × 13 mm window over the motor pads, MT6701 box at the centre.
- Exported `hardware/outline/board-outline.dxf` (outline + holes + window) and `board-on-motor.step` (fit check later).

### Altium project
- `hardware/altium/actuator-driver/` (local project): PrjPcb + SchDoc + PcbDoc.
- Imported the DXF, set the origin at the board centre (outline arc reads centre 0,0, radius 25 mm),
  defined the board shape from the circle and the window as a board cutout.
- Mounting holes: 4 unplated drilled pads MH1–MH4, 2.7 mm, at (±12.021, ±12.021) mm.
- Mistakes: origin was ~0.4 mm off the centre after import → fixed with arc-centre snapping and checked by number.
  First made the 4 holes as routed cutouts → redone as drilled holes (standard, more accurate).

## 2026-11-07 - 2026-11-08: Part libraries

### Setup
- Project libraries `actuator-driver.SchLib` + `.PcbLib`. Downloaded parts (Ultra Librarian, Altium Manufacturer
  Part Search, EasyEDA export) go in a downloads folder, get checked, then copied in.
- Every footprint checked against the maker's land pattern. Solder mask expansion 0.05 mm on every pad.

### Chips
- DRV8316C: Ultra Librarian footprint was wrong (pads too long, thermal pad too big, 0.14 mm gap to signal pads).
  Fixed to TI's pattern: 0.25 × 0.6 mm pads, 3.7 × 5.7 mm thermal pad, 0.25 mm gap, TI's 12 paste windows (69%).
- STM32G431 (ST's recommended LQFP-48), SN65HVD230 (TI D0008A), MT6701 (same SOIC-8 footprint as the SN65).

### Discretes and connectors
- AO3401A (gate limit ±12 V, so the 10 V zener clamp is fine), SMBJ18A (pin 1 = cathode = band).
- Drew by hand from datasheets: BZT52 zeners (SOD-123; Altium only had SOD-323), PTC 1812, 8 MHz crystal (2-pad),
  47 µH inductor, SWD header, test pad, solder jumper, motor pads (2.0 × 3.0 mm, 3 mm pitch).
- EasyEDA exports (checked): 47 µF cap (pad 1 = +), JST GH CAN connector.
- Generic RES/CAP/FB parts (Comment = value): 0603 R/C share one footprint, plus CAP_0805 and CAP_1206. LED_0603 (pad 1 = cathode).
- Swapped the XT30 for a 2-pin 3.5 mm screw terminal. Power comes from the PD trigger (15 V) through an 18 AWG lead with the 2 A fuse.

### Other
- Accidentally cut a motor wire; 10+ cm left, so re-strip and reconnect.
- Set up the Salitronic EDA agent: Claude can read/check Altium directly. Both libraries pass Altium's rule checks
  (only duplicate outline lines on the DRV8316's Mechanical 13 layer, harmless).
- Before layout: board rule Mask → Solder Mask Expansion = 0.05 mm (the AO3401A uses the rule).
- Next: schematic.

## 2026-11-08 - 2026-11-09: Datasheet check + schematic

### Datasheet check (before drawing)
- Re-checked every part and circuit choice against the maker's datasheet; page numbers are in `hardware/requirements.md`.
  The STM32 (48 pins) and DRV8316 (41 pins) symbols match the datasheets.
- Fixes:
  - nFAULT moved PB2 → PB11. PB2 only tolerates 3.6 V, but nFAULT is pulled up to AVDD, which can sit above the 3.3 V rail.
    PB11 is 5 V-tolerant. Updated `pinplan.ioc` (EXTI11, falling edge) and checked it in CubeMX.
  - VBAT → 3.3 V (it was left unconnected; the datasheet says it must be connected).
  - Crystal caps 30 → 20 pF (worked out from the crystal's load capacitance).
  - Current-sense filter cap 2.2 nF → 220 pF; with the 330 Ω this is fast enough for 20 kHz PWM.
  - AVDD cap rated ≥ 16 V, because a ceramic cap loses capacitance under voltage.
- Corrections to my notes: the 47 µH inductor must handle more than the 900 mA buck current limit (it's rated 1.1 A, OK);
  the MT6701 needs 32 ms after power-up before it reads correctly; the DRV8316 SDO pin is push-pull after reset.
- Library cleanup: removed junk parameters, set maker + LCSC numbers on the zeners and the TVS, named the 47 µF cap pins +/-.

### Schematic (6 sheets, global net names)
1. Power input: screw terminal → PTC → P-FET reverse protection → TVS → bulk caps → VM; VM sense divider ÷11.
2. DRV8316: buck to 3.3 V, charge pump caps, nSLEEP from VM (10k + 3.3 V zener), nFAULT 5.1k to AVDD,
   VREF to AVDD, PGND and GND joined by a 0 Ω net-tie (R4).
3. STM32: decoupling caps per the datasheet, ferrite bead to VDDA, 8 MHz crystal, 330 Ω + 220 pF filter on each current-sense
   signal (SOx → SOx_ADC), status LED (PB10), power LED.
4. Encoder: MT6701 over SSI (SPI2, receive only).
5. CAN: SN65HVD230, 120 Ω terminator behind a solder jumper (open by default).
6. Connectors: 2× JST GH CAN, SWD header, motor pads, test pads.
- Mistakes caught while drawing (worth remembering): reversed diode, PGND/GND mixed up, caps accidentally in series,
  power ports used for signals (use net labels), a power port left a cap pin floating, a shorted crystal, a filter that was
  bypassed by a net name, SOA/SOC swapped.
- Claude checked the full netlist (241 pin connections) against the requirements, and it matches.
- Compile is clean. No ERC on: USB_DM/DP (USB-C not decided yet), CAN_RX/DRV_FAULT/DRV_MISO (Altium flags IO vs Output,
  which is fine here), and nSLEEP (it's driven by a resistor, not a chip pin).