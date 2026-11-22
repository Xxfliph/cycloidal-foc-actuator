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

## 2026-11-09 - 2026-11-10: Placement + placement review

### Getting the parts onto the board
- Design → Update PCB Document. First try did nothing: Validate only checks the changes, Execute is what adds them.
- Rooms off. Parts pulled in one sheet at a time with Cross Select Mode + Tools → Component Placement → Arrange Within Rectangle.

### Placement
- All 69 parts on the top side; board origin = centre, motor-wire window at 12 o'clock.
- U3 (MT6701) at the exact centre over the magnet; U1 (DRV8316) to its right with the OUT pins facing the motor pads
  (J5 pads in the same OUTC/OUTB/OUTA order as U1, no crossings), U2 (STM32) to its left, CAN at the bottom,
  power input lower right (J1 → F1 → Q1 → TVS → 47 µF → 10 µF next to the VM caps).
- Rules used: through-hole pins ≥ 21 mm from centre (motor face is Ø40, legs clipped flush), copper ≥ 0.3 mm from the edge,
  nothing near the mounting holes.
- JST GH side-entry: the small signal pads are at the back and the opening is on the big-tab side (JST datasheet).
  J2/J3 were facing inward at first → rotated 180°.

### Placement review (Claude, independent; full write-up in the project doc `pcb-placement-review.md`)
- Mounting-hole keepouts were drawn inside the holes (r 1.2 < hole r 1.35), so they protected nothing. Measured the screw
  heads (Ø4.7) → keepouts now r 2.8. Moved Q1 (its VM pad was under the screw head), TP1 and J3 off the holes.
- DRV8316 pins 5–8 (SW, CPL, CPH, CP) couldn't all be routed on top: C13 sat right in front of SW. Changed C13 to 0402
  (new CAP_0402 footprint + symbol, LCSC C307339), lined it up with pin 7, moved C11 onto pin 8's line and shifted the
  VM caps and R4 up. SW now has a straight path to the inductor.
- AVDD cap C12 was 6 mm from its pin (sitting next to the VREF cap instead) → moved next to pins 25/26.
- R5/R7 were in the path of the three motor-phase traces → moved next to the DRV8316 pins they connect to.
- Rotated U3 180° so its SPI pins face the STM32 (no crossing). Only changes the zero offset, which calibration measures.
- Crystal rotated 90° so OSC_IN/OSC_OUT don't cross; R9 in line with pin 6.
- J1 pulled 0.65 mm in (overhang 1.6 → ~1 mm), small VDDA caps swapped to be nearest the pins.
- Left as is: C17 (no room between the hole and the STM32), current-sense resistors (routable as is).
- Re-check of the whole board is clean (pad gaps, outlines, holes, edge).

### Other
- Altium caches libraries: the new footprint showed "Footprint Not Found" until I reopened the project.
- From the PCB, schematic changes come in with Design → Import Changes From <project>, NOT "Update Schematics".
- The ECO also listed 4 "Unavailable footprint" lines (Q1, U2, U3/U4, D2): same footprints, accepted, nothing moved.
- Housing notes: J1/J2/J3 need slots in the wall; rear cover needs ≥ 9 mm clear above the board
  (J1 8.8 mm, SWD header 8.5 mm, 47 µF 7.7 mm).

## 2026-11-10 - 2026-11-11: Stackup, rules, pours + routing (sections 1–5)

### Stackup, rules, ground pours
- 4 layers, JLC04161H-3313 (1.6 mm): Top / L2 GND / L3 signals + power / Bottom. Thin 0.1 mm prepreg so Top sits close to the L2 ground.
- Net classes: PWR_RAIL (VM, PGND, VIN), MOTOR_OUT (OUTA–C), SW, LOGIC_PWR (3V3, AVDD, VDDA). Clearance 0.15 general,
  0.2 for power nets and vias, 0.35 around the hand-soldered J1/J4. Vias tented on both sides (the bottom faces the motor).
- GND pours on L2 + Bottom with a PGND island under the power section (input, VM caps, motor-phase corridor),
  joined to GND only at the R4 net-tie. Remove Dead Copper is off on the PGND pours until the PGND vias exist.
- Mounting-hole keepouts on every copper layer; pin-1 dot added on U1.

### Routing
- DRV8316 corner: charge-pump caps and SW on Top with no vias (C13 is 0402 so CPL can pass under it and CP over it).
  The buck feedback (FB) moved to the right side to free the space under the current-sense pins.
- VM: a fill joins VM pins 9–11 to the first cap. The two cap columns are separated by a PGND column, so VM crosses
  between them on L3 through VM vias. 15 thermal vias in the DRV8316 pad.
- Motor phases: 0.5 mm, not the planned 0.8 (0.8 didn't fit between the pin pairs, the PGND vias, the screw-head ring
  and the PGND island edge). 0.5 mm carries ~2 A for ~10 °C rise; the motor needs ~1 A. The top bend of OUTA has to
  stay ≥ 3.25 mm from MH1's centre (keepout ring + power clearance); the first version failed DRC by 0.05 mm.
- Power input routed at 1.0 mm with PGND vias. Crystal on Top only, no vias, nothing else nearby.
- Current sense: SOA/SOB/SOC on L3 to the 330 Ω resistors at the left edge. The filtered nodes (R → 220 pF → ADC)
  run on Top through the 0.57 mm gaps between the resistor/cap rows (a via never fits in a gap, a 0.15 mm trace does).
  The cap grounds had to become vias in the middle of the pads (via-in-pad); decide at ordering whether to pay for
  plugged vias.
- Last DRC: 0 clearance/short errors; 90 nets left to route; silkscreen items unchanged.

### Lessons
- Repour after adding vias: old pours run straight through new vias and DRC reports them as shorts.
- A via that only touches a pad edge isn't connected — draw a trace.
- Keepout rings are checked with the normal clearance rules, so power traces need keepout + 0.2 mm.
- Check where the neighbouring vias are before picking a via spot (one suggested spot landed on another via).
- Running a thick trace straight through the middle of an inner layer walls it off for every signal that has to cross.

## 2026-11-11 - 2026-11-13: Routing finished (sections 6–8), fab outputs, review, JLC order placed

### Routing
- DRV SPI, INH/PHASE_EN/DRV_OFF/nFAULT and encoder SPI routed, mostly on L3 with short Top fan-outs.
- CANH/CANL routed as a 0.2 mm pair. Impedance control isn't needed on ~1 cm of trace.
- SWD runs ~10 cm around the board on L3. (Later check: it crosses the PGND islands, see "GND / PGND split check".)
- LEDs and UART routed. Every STM32 GND pin gets its own short via to the ground plane.
- 3V3 had a ratsnest line between two islands. There's only one 3V3 source (the DRV8316 buck, L1 → C15),
  so the two islands had to be joined.
- 7 GND vias were shorting L3 traces (INHB, 3V3, SOA, VM, UART_TX), because vias go through every layer.
  Moved them next to C12/C19/C14/C30/C31/D3/U4 pin 2.
- SO vias at x −22.25 needed Top stubs to the resistor inputs. They aren't dead ends.

### Pours, stitching, Remove Dead Copper
- GND stitching vias added around the board, none under the crystal.
- Remove Dead Copper stays OFF on the PGND pours. The island is made of 9 overlapping rectangles, each
  rectangle is judged on its own, and turning it on deleted the whole island. The merged copper is one
  piece with plenty of PGND vias, so nothing is actually floating.

### DRC and board sweep (EDA tool, beyond DRC)
- 0 unrouted, 0 shorts, 0 clearance/width/via errors. Schematic vs PCB matches (69/69). ERC 0.
- Accepted leftovers (30 after the OUTB fill): silk-to-mask (footprint outlines on pads), 1 silk-to-silk,
  board-outline, net antennae (thermal-pad GND vias).
- Silkscreen 1 mm height / 0.15 mm stroke; small passives' designators hidden where they sat on pads.
  J1 overhangs the board edge by ~0.96 mm on purpose (connector lip).
- Netlist: no floating pins; the 18 single-pin nets are all intentional (NC pins, spare GPIO, MT6701 OUT/PUSH,
  SN65 Vref, USB not fitted, J2/J3 pin 1). Every pour is one piece per layer.
- Component Clearance rule wasn't in the DRC → enabled: 0. The placement-audit overlaps (R3/J4, F1/J1,
  U4/J3) were bounding-box false alarms; 3D view agrees.
- Real short found: a UART_TX via sat inside the GND pours on L2/Bottom (pours not redone after the via was
  added). Repour All fixed it. The first Gerbers would have shipped with UART_TX shorted to GND.

### Fab outputs (hardware/fab/)
- Gerbers in mm 4:4, 11 layers, board centre = (0,0) in every file, drill files line up. NC drill mm 4:4,
  plated / non-plated separate. Re-exported after every save.
- actuator-driver_gerbers.zip (Gerbers + drill), actuator-driver_BOM_JLC.csv (LCSC numbers),
  actuator-driver_CPL_JLC.csv (Altium pick-and-place, mm from board centre), actuator-driver_BOM_PCBWay.csv.
- Small-part picks: all 100 nF = 50 V (C14663, the VM caps need it), 1 µF = C15849 (50 V X5R), 220 pF filter
  = C1603 (X7R, not C0G — free Basic part, fine at 2 MHz cutoff), 20 pF C0G = C1648, ferrite GZ1608D601TF
  (C1002, 200 mA), resistors UNI-ROYAL 0603.
- Stock: LED C72043 out of stock → final pick KT-0603YG (C2289). J2/J3 short by one → J3 not assembled
  (J2/J3 are in parallel; hand-solder J3 later for daisy-chaining). Assemble 2 boards, not 5.

### Pre-order review (fresh eyes)
- Checked against datasheets: power path, polarity of every diode/LED/electrolytic, pin 1 of every IC, U1 footprint
  vs TI's land pattern, MT6701 / SN65HVD230 / STM32 pinouts.
- Scanned the actual Gerbers for every one of the 249 pads: copper, mask opening, paste, gaps, slivers, annular ring.
- Similar projects (MicroSpora DRV8316 + STM32G431 thread and other open-source FOC boards): every hardware problem
  they hit is already handled here — DRVOFF pull-up, buck feeds 3.3 V direct (no diode/LDO), BOOT0/PB8 via option
  bytes, crystal series resistor, separate SPI buses, SWD header, nSLEEP wake, thermal vias.

### Fixes before ordering
- Q1 (AO3401A) and C13 had NO solder-mask openings: their pads were on mask "from rule", which covered them.
  Set to manual 0.05 mm expansion. Found from JLC's preview showing no Q1 pads; the first scans only checked
  copper and drill, which is why it was missed.
- Closed a 0.04 mm copper sliver at U1 pin 16 with a small Top fill (OUTB).
- PCBWay's audit found a 0.05 mm gap between two 1 mm PGND traces on L3 (left of C1). Same net, so DRC ignored
  it, but a fab can't etch it. Deleted the redundant short trace (its vias stay connected through the PGND
  island on L2/Bottom), repoured.
- Final vias: 81 small vias at 0.3 mm hole / 0.45 mm pad (ring 0.075). Avoids JLC's small-via + 4-wire Kelvin
  test fees (~$33.5). Annular ring rule set to 0.07 (exactly-at-limit values were flagged by rounding).

### Second opinion (another AI, given the Gerbers/BOM/CPL)
- Its own pad-by-pad Gerber check agreed: every pad has copper, mask and paste.
- Its two "critical" findings were wrong: MT6701 MODE high = I2C/SSI (the dev-rig breakout has MODE at VDD and runs
  I2C), and the SMBJ18A complaint came from the handoff saying 12–24 V. The design is 12–15 V.
- Everything else was already known or harmless (BOOT0 via option bytes, J3 not assembled on purpose, L2 and
  Bottom both GND pours, 0.075 mm via ring OK with epoxy-filled vias).

### GND / PGND split check
- Checked every L3 and Top signal trace against the 9 PGND islands. Only SWDIO and SWCLK cross the split.
  All motor-control signals (DRV SPI, INHx, PHASE_EN, SOx, encoder SPI, CAN, UART) are over solid GND.
- Accepted for rev 1: if SWD is flaky, lower the ST-Link clock (≤ 1 MHz); worst case, solder wires straight to
  the PA13/PA14 legs.

### Quotes and fab choice
- PCBWay (4 layers, S1000H TG150, 5/5 mil, 0.3 mm min hole, HASL lead-free): its human audit caught the L3 gap.
  Student program = 50% of board + assembly labour only (not parts, shipping, duties) and needs "PCBWay" on the
  silkscreen → not used. Chose JLC (only a bit more expensive, faster).
- JLC: the 3313 stackup doesn't allow Economic assembly → default stackup is fine (no impedance-controlled traces).
  Customs description: DIY Hobby Circuit Board (HS 902300).
- Placement preview: all ICs (U1–U4) rotated 90° CW and Q1 180° by hand to match the pin-1 dots and pads.
  Diode polarity checked against pad nets: D1 cathode on VM pad (top), D2 cathode on VM pad (left),
  D3 cathode on nSLEEP pad (right), D4/D5 cathode on GND pad (left); Q1 pin 1 = gate.

### JLC order (W2026111309131766), paid Nov 13, In Production
- PCB: 5 pcs, 4-layer, 1.6 mm, ENIG, Epoxy Filled & Capped vias (13 small pads have via-in-pad, incl. the
  current-sense caps), flying probe, JLC mark removed. PCBA: Economic, top side, 2 assembled, nitrogen reflow,
  assembly remark for polarity.
- Review Before Payment; approved in ~3 h (PCBA review takes longer than PCB).
- Paid US$139.17 (boards $124.17 + DHL $15) after the shipping coupon and JLCONE discount; about C$185.
  Expect Canadian tax (~C$25–35) from DHL before delivery.
- Still to do: confirm the production-file and parts-placement previews when JLC sends them.

### Rev 2 list
- Route SWD over solid GND (avoid the PGND islands).
- Set the AO3401A and CAP_0402 library footprints to manual 0.05 mm mask so the Q1/C13 fault can't come back.
- Crystal load caps: 20 pF gives ~14 pF effective vs the crystal's 20 pF → 30 pF would be closer.
- Optional: silkscreen labels (+/− J1, A/B/C J5, CAN, SWD, name/version), VM stub to the via at 0.4 mm,
  C2/C3 PGND stubs at 0.5 mm or a second via.

### Before bring-up
- Buy: CP2102 or CH340 USB-serial adapter (3.3 V), JST GH 4-pin pre-crimped cables (check pin order vs the CANable).
- Board runs on 12–15 V only (TVS starts conducting ~20 V).
- First power-up: 12 V, 50 mA limit, no motor; check 3.3 V. Then set option bytes nSWBOOT0 = 0, nBOOT0 = 1.
- Firmware: BUCK_PS_DIS = 1 first; NEVER set the DRV8316 buck to 5 V (SimpleFOC examples do; it feeds the
  STM32 here); read back registers after writing (a constructor flag didn't set PWM mode in MicroSpora);
  ADC at the PWM centre, cap the duty.

### Lessons
- Check what's on L3 before placing a via. A GND via on an L3 trace is a short.
- A ratsnest line means two copper islands of the same net aren't joined. Join them anywhere.
- The tool's "unrouted" list can be stale; trust the DRC counts.
- Repour after any via change, and re-export after any save.
- DRC ignores pad-to-pad gaps inside a footprint and gaps between same-net copper; a fab's CAM check doesn't.
- Check mask and paste from the Gerbers too, not just copper: a pad on "mask from rule" can end up covered.
- Turn on the Component Clearance rule; it wasn't checked by default.
- Zeners/TVS face backwards on purpose: the cathode goes to the higher voltage. Orient diodes by the pad nets,
  not by "GND = −".
- Fab part models can be 90° off from the footprint: check every IC's pin 1 in the placement preview.
- A manufacturer's "approved" means they can build it, not that the circuit is right.

## Nov 13–15: board_fw builds, safety basics, first test program, start of Drv8316

### board_fw now builds
- Copied FocController, PIController, MotorDriver, Encoder and app from actuator_fw.
- MotorDriver on the board: enable = DRV_OFF low, disable = DRV_OFF high.
- encoder.cpp excluded from the build (still the I2C version; gets rewritten for SSI on SPI2).
- The old dev-rig loop is parked as `focVelocityTest()` (not called). It turns the driver on before any
  DRV8316 setup, so it must not run on the board until the boot order exists.
- Build: 0 errors; the only warnings are unused variables in the parked function.

### Safety basics
- `__HAL_DBGMCU_FREEZE_TIM1()` right after `HAL_Init()`: when the debugger halts the CPU, TIM1 stops and its
  outputs are switched off, so a breakpoint can't leave a fixed voltage on the motor.
- HardFault_Handler and Error_Handler first set DRV_OFF high (driver off) and PHASE_EN low (phases Hi-Z),
  with direct BSRR writes (one register write per pin, no function calls).
- Before GPIO init those writes do nothing, but the board is still safe: the 10 k pull-up holds DRV_OFF high
  and the DRV8316's pull-downs hold INLx low.
- Still to do: independent watchdog (IWDG).

### First bring-up program (ready to flash)
- `app_main()`: LED on PB10 toggles every second and prints "blink N" on USART1 (PB6, 115200).
  Never touches the motor driver. Proves power, SWD flashing, clock and code running.

### Drv8316 class (in progress)
- SPI frame: bit 15 R/W (1 = read), bits 14–9 register address, bit 8 even parity, bits 7–0 data.
- `buildFrame()` (private): puts each part in place with shifts, counts the 1s and sets the parity bit if the
  count is odd. Tested on PC: write reg 3 data 3 → 0 000011 0 00000011, read reg 0 → 1 000000 1 00000000,
  all 32,768 possible frames come out with even parity.
- `transfer()` (private): CS low → HAL_SPI_TransmitReceive (one 16-bit item, 5 ms timeout) → CS high →
  return the status. CS always goes back high, even if the transfer fails.
- `writeRegister(address, data)` (public): buildFrame → transfer.
- Next: `readRegister(address, data)`, then the setup sequence (unlock, BUCK_PS_DIS = 1 first, 3x PWM,
  current-sense gain, overcurrent settings) with a read-back of every register, then fault read/clear.

### Lessons
- CubeIDE builds the saved file on disk: an unsaved tab (`*`) means you're building the old version.
- Incremental builds only re-print warnings for files that changed; Clean → Build to see them all.
- C++ is case-sensitive: `DRV_OFF_Pin` and `DRV_OFF_PIN` are different names.
- Parity only detects a single flipped bit (two flips cancel out) and doesn't fix anything: the DRV8316
  ignores a bad frame. Reading every register back after writing it is what catches and fixes errors.
- `reinterpret_cast` relabels an address's type without changing anything in memory (for HAL's `uint8_t*`
  buffers); `static_cast` converts a value (float → int, 16-bit → 8-bit).
- Each setup step for the motor driver must happen before DRV_OFF goes low; the first test program should
  never enable the driver.

## Nov 15–16: Drv8316 setup code, cross-check, cycloid drive designed + first SolidWorks parts

### Firmware: cross-check before writing the setup code
- Compared the final Altium netlist, board_fw.ioc + generated init code, and the firmware against the
  DRV8316C (SLVSH07) and MT6701 (Rev 1.9) PDFs. Every STM32 pin agrees with the PCB.
- DRV8316C facts confirmed from the PDF: frame format matches buildFrame; SCLK falling-edge capture
  (= CPOL low, CPHA 2 edge); reply = 8 status + 8 data (on a write, the register's old value);
  SPI ready 1 ms after power-up; CS high ≥ 400 ns between frames.
- PWM_MODE must be 2 (3x). Mode 3 turns VREF into a current-limit input and disables SOA/SOB/SOC.
- BUCK_SEL stays 0 (3.3 V); BUCK_PS_DIS = 1 (power sequencing only works at 5.0/5.7 V).
- Found for later: SPI2 (MT6701) mode is wrong in CubeMX. It samples on the rising edge, but the
  MT6701 changes DO on the rising edge. Fix at the encoder step (likely CPOL high, CPHA 1 edge) and
  confirm with the logic analyzer.

### Firmware: Drv8316 class
- `readRegister(address, data&)`: read frame → if HAL_OK, data = bottom 8 bits of the reply.
- `writeVerified(address, data)`: write → read back → compare → retry up to 3 times, else HAL_ERROR.
- `modifyRegister(address, mask, newBits)`: read → `(value & ~mask) | (newBits & mask)` → writeVerified.
  Needed because most registers pack several settings, and some reserved bits reset to 1.
- `transfer()` now waits 80 CPU cycles (~470 ns) after CS goes high, timed with the DWT cycle counter.
  DWT is switched on at the top of `app_main()` (otherwise the wait would hang forever).
- `init()`: wait 1 ms → unlock (plain write) → BUCK_PS_DIS = 1 → PWM_MODE = 3x → CSA_GAIN = 0.6 V/A.
  Stops at the first failed step. Overvoltage (34 V) and overcurrent (latched) left at power-up values.
- Tested on PC against a fake DRV8316: every register ends at the expected value, other bits untouched,
  dropped writes get retried, and a dead SPI stops init() without changing anything.
- Next: `clearFaults()` (read CR2, set bit 0, plain write), then the boot order in app.cpp.

### Mechanical: cycloid drive (started Nov 13)
**Fit check against parts on hand**
- Bearings already bought both work: 6803 (17×26×5) as the cam bearing, 6806 (30×42×7) as the output bearing.
  One of each per disc/output; 5 of each on hand.
- Ring-pin circle moved from Ø50 → Ø54: at Ø50 the plastic between the disc's output holes and the bore is only
  1.5 mm; at Ø54 it's 2.5 mm. Housing grows to ~Ø64.
- Final geometry: 11 ring pins (3 mm dowels), 10-lobe discs → 10:1, eccentricity 1.5 mm (e·N/R = 0.61),
  6 output pins on r 18.5, disc output holes Ø6.2 (pin + 2e + 0.2 clearance). Tightest profile curve
  8.45 mm radius vs 1.5 mm pin radius → no undercut.
- Rotor bolt pattern measured: 4× M2.5 on a Ø20.0 circle (diagonals 18.0 inner / 22.0 outer), ~5.2 mm deep.

**Research (printed cycloidal drives)**
- Two discs 180° apart (vibration from one disc showed up in other small builds). Three discs rejected:
  needs longer dowels and a three-piece cam for a benefit too small at this size.
- Kept steel dowel pins, free to spin in greased slip-fit holes. Bearings on every pin = most efficient but needs
  ~22 tiny bearings and a bigger pin circle; printed lobes instead of pins = plastic on plastic, wears fast.
- Expected: 55–66% efficiency in hobby tests → ~0.5–0.65 N·m at the output from ~0.1 N·m motor torque.
  Play ~0.2–0.25° in a lab build. Printed discs need 4+ walls (one cracked at the bearing with fewer).
- With two discs the output pins need ~19 mm → the 20 mm dowels work everywhere, nothing to buy.

**Screws (all on hand, nothing to buy)**
- Cam → rotor: countersunk M2.5 × 10 (head Ø4.6). 0.6 mm × 45° chamfer on a Ø2.8 hole → head ~0.3 mm proud,
  ~4.4 mm of thread in the 5.2 mm rotor hole, head stays below the bearing.
- Housing → motor: socket-head M2.5 × 10 through the PCB + a 2.0 mm printed plate: 9.7 − 1.6 − 2.0 = 6.1 mm
  in the ~6.3 mm stator thread. If they bottom out: small washers on top of the PCB.
- Plate replaces the 1 mm nylon washers → magnet sleeve gets reprinted 1 mm taller.

**SolidWorks parts done**
- Global variables in a linked `mechanical/solidworks/vars.txt` (fits like `disc_clr`, `seat_6803`, `insert_hole`
  can be changed after the fit coupons without editing sketches).
- `disc.SLDPRT`: profile from the epitrochoid equations in an Equation Driven Curve; output holes at 9° from a
  lobe valley so disc B is the same part flipped over and turned 18°.
- `cam_1.SLDPRT`: flange + offset boss A; screw access pockets cut upward only (two notch the boss, unavoidable);
  key pegs Ø3.0 / Ø2.4 at ±5.8 (different sizes → cam 2 only fits one way); M3 insert hole 6.5 deep.
- `cam_plug.SLDPRT`: Ø8.4 × 6.3 centring pin for the motor's hollow shaft.
- `cam_2.SLDPRT`: lens-shaped 0.5 mm spacer (touches only the bearings' inner rings) + boss B offset the other way;
  M3 counterbore. Prints boss-down (spacer-down would leave a 1.9 mm overhang).
- Next: output carrier + plate, then housings, assembly, fit coupons, first print.

### Lessons
**Firmware**
- Switches vs buttons: most settings stay where you put them (verify them); CLR_FLT resets itself and
  REG_LOCK is a "password" write, so those two can't be verified and use a plain write.
- `|` can only turn bits on. To change a setting: clear its bits with `& ~mask`, then `|` the new value.
- mask = which bits (all 1s across the setting); newBits = the option number. Both are slid by the
  setting's lowest bit number: `0b11 << 1`, `0b10 << 1`.
- `&`, `|`, `~` and `<<` widen uint8_t to a 32-bit int first; static_cast<uint8_t> squeezes it back.
- A do-nothing loop gets deleted by the compiler unless the counter is `volatile`; the DWT counter is
  the exact way to wait a short time (cycles = time × 170 MHz).
- Check the real folder before assuming a file name: the files are Drv8316.h/.cpp (capital D).

**Mechanical**
- SolidWorks wouldn't take a 0 → 2π equation curve in one go: enter it as two halves (0 → π, π → 2π).
- `vars.txt` must end with a line break, and after editing it, re-link it (the "…" button) or SolidWorks keeps
  the old values. Save Notepad first.
- Rule I'm using: if a number depends on the printer, it gets a variable; if it follows from the design, type it.
- A cut sketched on a bottom face is seen from underneath, so left/right flips. Sketch on the Top plane instead.
- Symmetric key pegs let a part go on 180° wrong; making them different sizes makes it foolproof.
- Do the screw-length arithmetic before drawing: my first insert hole was 0.2 mm too shallow for the M3 × 8.
- Check overhangs per print orientation: cam 2 had to flip to print without sagging.

## Nov 16–17: Rest of the parts drawn, fit coupon results, first motion test ready to print

### SolidWorks parts done
- `output_carrier.SLDPRT` (15.3 tall, prints flange-down): Ø44 × 8 flange, Ø33 × 0.5 neck, hub_30 hub
  (7 − hub_short tall, sits in the 6806's bore), Ø12 bore. 6 output pin holes pin_press × 7 deep on r18.5 at
  9° + 60k (lined up with the disc holes). 4 M3 inserts on r10.5 at 0/90/180/270 for the output plate.
- `output_plate.SLDPRT`: Ø33 × 0.5 lip + Ø36 × 3 plate, Ø12 bore. Clamps the 6806 from the other side
  with 4 × M3 into the carrier's inserts (Ø3.4 on r10.5). 4 × M3 tap holes (Ø2.8) on r14 at 45° + 90k to
  bolt the next link/load onto the output. First drawn with 6 tap holes; fixed to 4.
- Housings changed to option B: plain Ø74 cylinder instead of Ø64 + lugs (the lugs needed supports).
  4 M3 screws on r32.5, halfway between ring pins: 16.36°, 114.55°, 180°, 278.18° (360·(k+0.5)/11, k = 0, 3, 5, 8).
- `housing_rear.SLDPRT` (25.05 tall, prints plate-down): 2.0 plate, Ø40.4 motor ring, Ø43 can bore, Ø28
  opening, wire window, 4 × Ø2.8 motor screw holes at ±12.021, 11 ring-pin holes pin_slip × 4.75 deep,
  4 M3 inserts. Window bottom moved Y 13 → 11 to remove two sharp horns. Ø41–52 × 1.0 relief ring under
  the plate so the PCB's THT solder joints (J1/J4) don't touch it.
- `housing_front.SLDPRT` (28.25 tall): Ø57.6 disc cavity, Ø45 shelf, 6806 seat, Ø39 lip, 11 ring-pin holes,
  4 × M3×16 through with Ø6.2 counterbores 16.25 deep (1 mm floor above the cavity).
- Test spacers: 4 × Ø5.5 rings, Ø2.8 hole, 1.6 thick. They stand in for the PCB between the motor and
  housing_rear until the board arrives.

### Fit coupon results → vars.txt
- Bearing seats: 6803 in Ø26.1 and 6806 in Ø42.1 both "very good" → seat_6803 26.1, seat_6806 42.1.
- Shaft stubs too loose (bearings fall off with a light push) → boss_17 17.15, hub_30 30.25 for now.
  Shaft coupon v2 (Ø17.10/17.20 stubs, Ø30.20/30.30 tubes) printed to confirm.
- Dowel holes: 3.3 = dowel spins freely (pin_slip), 3.2 = press fit (pin_press), 3.1 too tight.
- Heat inserts: 4.4 cleanest → insert_hole 4.4. Disc output hole clearance 0.2 → out_hole_clr 0.5 (Ø6.5).
- Motor shaft bore measured 8.5 → plug_d 8.6 (was 8.4); cam_1 plug pocket = plug_d + 0.1.
- Belt plate: holes line up, but the screws are too long to bite, so it wobbles (screw length, not the holes).
- Printer pattern: holes print ~0.2–0.3 small, outside diameters ~0.15–0.25 small.

### Test discs
- disc_clr in the equation curve didn't update the geometry, even with Ctrl+Q. Workaround: typed pin radius
  per disc (1.5 / 1.55 / 1.6) → three files at clearance 0.00 / 0.05 / 0.10 (valley at 24.00 / 23.95 / 23.90).
- Over-defined sketch on the 0.05 disc: both curve halves have to be changed, not just one.
- Disc orientation dot still not cut (needed before the final disc pair).

### Export + print setup
- STL: binary, mm, custom deviation 0.02 mm (type 0.0008: the box works in inches), angle 5°.
- Checked housing_rear and two discs from the STL files: every dimension matches the design.
- Bambu plate: housing_rear, 3 discs, plug, 4 spacers, shaft coupon v2. Discs per-object 0.16 mm,
  5 walls, 40% gyroid. Brim only on spacers + plug. ~2 h 9 min, 55 g. cam_1 waits for the shaft coupon.

### Lessons
- Leave room for what sticks out of the PCB: THT solder joints needed a 1 mm relief under the housing.
- When a rectangle cut ends inside a circle's edge, it leaves thin horns. Start it inside the hole instead.
- A global variable inside an equation-driven curve doesn't reliably rebuild the curve. Type the number, or
  offset a fixed curve.
- Bambu Studio: per-object settings only apply to the objects with the orange icon; check every one.
- SolidWorks STLs come in Y-up: use Lay on Face (F) in Bambu Studio.

## Nov 18–19: First motion test, fits corrected, front housing + test disc batch; Drv8316 fault functions

### Shaft coupon v2 → vars.txt
- Stubs printed 0.3 small (17.20 → 16.9, 17.10 → 16.8); tubes the same (30.20 → 29.8, 30.30 → 30.0).
- 16.9 stub = firm push on the 6803 → boss_17 17.20. 29.8 tube held the 6806, 29.9 would be perfect →
  hub_30 stays 30.25.
- Printed cam_1, cam_2, output carrier, output plate (~1 h 6 min). Linked sketch dimensions DO follow
  vars.txt (Σ next to the value); only the disc's equation curve didn't.

### First motion test (rear half only)
- Assembly order: plug → cam_1 + 4 countersunk M2.5 → 6803 pressed on (it covers the cam screws, so the cam
  must be screwed down first) → motor into housing_rear, socket-head M2.5 through spacer + plate into the
  stator → 11 greased ring pins → disc.
- cam_1 heat insert: centre hole on the boss/peg side, pressed flush (M3×8 from cam_2 reaches ~5.7 mm).
- Both bearings needed plastic scraped off before they'd go on: 6803 on the 17.20 boss, 6806 on the carrier
  hub. Real parts print fatter than the thin coupons.
- Some ring-pin holes dragged on the dowels. Plug was loose in the motor shaft (8.6 prints ~8.4 in an 8.5 bore).
- All three test discs turned, with some resistance spots. Hard to judge: the ring pins are only held 4.75 mm
  deep at the bottom and lean.
- Forgot to label the discs. Measured tip to tip: 53.5 / 53.4 / 53.3 = 0.00 / 0.05 / 0.10 (design 54.00 /
  53.90 / 53.80). Discs print ~0.5 small across the tips → real clearance ≈ 0.25 / 0.30 / 0.35.
- Two discs + output carrier: the gearing works (carrier dowels pass through both discs and get driven
  round). The whole carrier tilts and the top disc climbs, because nothing holds the 6806 or the pin tops
  yet. That's the front housing's job; the dowels themselves were solid in the carrier.

### Changes
- vars.txt: boss_17 17.20 → 17.13, hub_30 30.25 → 30.18, pin_slip 3.30 → 3.35, new plug_shaft_d 8.8.
- Stepped cam plug: Ø8.8 × 3.8 (motor shaft, chamfered) + Ø8.6 × 2.5 (cam_1 pocket). Kept separate so
  cam_1's pocket doesn't change.
- New test discs with negative clearance (pin radius 1.45 / 1.40 / 1.35 → valley 24.05 / 24.10 / 24.15),
  1 / 2 / 3 dots to tell them apart. Printed in pairs, plus a second 0.00 and 0.05 to make pairs.
- New cams exported. STL check: bosses came out 17.13, not 17.10 (vars value to double-check).
- `output_arm.SLDPRT`: Ø36 hub + 12 × 5 bar to 80 mm, chamfered tip, 4 × Ø3.4 on r14 at 45° + 90k (output
  plate tap holes, M3×8), Ø12 centre. For turning the output by hand and measuring backlash:
  backlash (°) = tip movement / 80 × 57.3.
- Checked housing_front.STL: 57.6 cavity 0–11, Ø45 shelf to 19.75, 6806 seat to 26.75, Ø39 lip, pin holes
  3.35 × 15.75, M3 counterbores to 12 (1 mm floor). Prints lip face down: every bore widens upward, no supports.
- Batch plate: housing_front, 9 discs, new cam_1 + cam_2, stepped plug, output arm. ~4 h 54 min, 114 g.

### Firmware: Drv8316 class
- `clearFaults()`: read CR2 (reg 4) → set CLR_FLT (bit 0) → plain `writeRegister`. Not writeVerified:
  CLR_FLT is a button. The chip clears the faults and sets the bit back to 0 by itself, so a read-back
  would always fail and report an error after a clear that worked. Must run after `init()` (registers
  have to be unlocked first).
- `readFaults(faults&)`: reads IC Status (reg 0). Boxes: 6 BK_FLT, 5 SPI_FLT, 4 OCP, 3 NPOR, 2 OVP,
  1 OT, 0 FAULT. Read-only register.
- Added a comment above writeVerified explaining switches (writeVerified) vs buttons (writeRegister).

### Firmware: app.cpp
- `printFaults(faults)`: plain function, not part of the class (printing is the app's job). Pulls each
  box out with `(faults >> n) & 0b1` and prints one line over USART1:
  `FAULT=0 OT=0 OVP=0 NPOR=1 OCP=0 SPI=0 BK=0`. Prints 0s and 1s, not just the names of set flags,
  because NPOR is inverted.
- NPOR is inverted (checked in the datasheet): 0 = power-on reset detected, 1 = normal. It stays 0 after
  power-up until CLR_FLT is written, so a healthy board shows NPOR=0 before clearFaults() and
  NPOR=1 after.
- `Drv8316 myDrv8316(&hspi3, DRV_CS_GPIO_Port, DRV_CS_Pin)` created in `app_main()` after the DWT
  lines (transfer() needs DWT running). Added `#include "spi.h"` for hspi3.
- Not tested on hardware yet (board not here).

### Next
- Mechanical:
  - Cams: tell how hard the bearings push on. Free the stiff housing_rear pin holes with a 3.3–3.4 mm drill bit.
  - Heat inserts in housing_rear (4) once the front housing fits, then full build: cam_1 + bearing A → disc A →
    cam_2 + bearing B → disc B (flipped) → carrier → front housing.
  - Test disc pairs loosest to tightest; keep the tightest pair that turns smoothly all the way round.
  - Carrier inserts + output plate + arm → friction and backlash.
- Firmware:
  - Boot order in app_main(): init() → PHASE_EN high → DRV_OFF low → clearFaults() →
    readFaults() → printFaults() + register dump over UART.

### Lessons
- Coupons print smaller than real parts: thin rings at default walls vs solid parts at 5 walls. Leave margin.
- Label test parts before printing (dots or a marker), or measure them apart afterwards.
- Disc B is the same part as disc A, flipped. Their holes sit 3 mm apart (2 × eccentricity) on purpose;
  the output dowel sits halfway between, and each Ø6.5 hole rolls around it.
- Build order matters: disc A can't get past cam_2's bearing, and the cam screws hide under bearing A.
- The cam plug centres cam_1 on the motor axis. The screws alone allow ~0.15 mm of slop, more than the
  clearance steps being tested.
- The rear half alone can't hold the output straight; test the drive with the front housing on.
- Heat inserts are semi-permanent: dry-fit everything first, insert last.

## Nov 19: Output-hole binding found, final disc test plate; boot sequence (bootDriver), PHASE_EN in MotorDriver

### Mechanical: batch results (black PETG, second P2S)
- Printed housing_front, 9 test discs, new cam_1 + cam_2 (boss 17.13), stepped plug, output arm.
- Holes print 0.3 small on both printers and both colours (disc output hole 6.5 → 6.2, grey and black).
  Printer/colour switch made no measurable difference.
- housing_front M3×16: 3.6 mm of thread reaches the rear inserts (designed 4.0). Lost 0.4 because the
  counterbore floor printed as a bridge (lip face down) and sagged. Enough for a brass insert.
  CAD counterbore 16.25 → 16.75 for next time. Counterbores sit over solid wall (r32.5 vs cavity r28.8),
  not over the cavity.

### Mechanical: disc mesh test (front housing held by hand, no carrier, dry)
- 0.00 smooth; −0.05 turns but rough; −0.10 binds. 0.05 / 0.10 (single grey discs) loosest.
- Felt the discs nudge the front housing: strong at −0.05, less at 0.00, barely at 0.05 / 0.10. Means real
  contact force each turn; unknown whether screwing the housing down makes 0.00 better or worse.
- Ring pins don't spin; the disc slides on them. Normal for plain dowel pins (disc-pin friction is lower than
  pin-hole friction). Grease is what helps, not a looser hole.

### Mechanical: output carrier binding
- Carrier dowels wouldn't turn with both discs on. Cause: disc output holes too small.
- Simulated a full output turn: every hole centre stays exactly e = 1.5 from its dowel centre, so the hole
  needs Ø ≥ 6.0 (dowel 3.0 + 2e). Printed 6.2 left only 0.1 per side for dowel placement, hole accuracy and
  carrier-to-cam misalignment, which isn't enough.
- How the output works: the 6806 in the front housing keeps the carrier centred and upright; the dowels only
  turn it. Loose output holes give backlash (≈ extra clearance / 18.5 mm rad), not wobble. At any moment
  disc A drives the dowels on one half of the circle and disc B the other half; the sideways pushes cancel,
  so the carrier gets a pure twist. The dividing line goes round once per motor turn.
- Why the dowels "only fit sometimes": each disc seats on the pins in 10 positions (36° apart), holes repeat
  every 60°. Unflipped disc B never lines up; flipped disc B lines up in only 2 of its 10 positions. Fix: drop
  B in, check all 6 hole pairs overlap the same way, else turn B one lobe and retry; pencil a line across
  both rims once found.

### Mechanical: changes
- vars.txt: seat_6803 26.1 → 26.02 (discs slid on/off the bearing too easily), out_hole_clr per test disc,
  pin_slip 3.35 (one rear pin hole still dragged at 3.30). hub_30 settled at 30.20 (previous entry's 30.18
  was a typo); only matters for the next carrier print.
- New housing_rear: pin holes 3.35, plus 4 × M3 insert holes in the bottom face for a PCB cover at r31,
  45° + 90k, 5.5 deep. First export had the holes drifting (r31/32.5, 42–48°) from an under-defined sketch;
  fixed with one circle + circular pattern. r31 leaves 3.5 mm of outer wall (r32.5 left 2.0) and stays
  clear of the Ø50 PCB and the relief ring.
- Final test discs (all checked from the STLs):
  - 2 dots: 0.00 profile, out_hole_clr 0.7 (Ø6.7, ~6.4 printed, 0.2 spare per side)
  - 4 dots: 0.00 profile, out_hole_clr 0.9 (Ø6.9)
  - 6 dots: +0.05 profile (pin radius 1.55), out_hole_clr 0.8 (Ø6.8), backup if 0.00 tightens when screwed down
  - All bore 26.02. Dots came out as matching pairs on one face (1/2/3 → 2/4/6).
- Plate: new housing_rear + 2 of each disc, same printer and filament, ~3 h 30 min.
- Plan: PCB cover as a cup on housing_rear's bottom face (≥ ~12 mm inside, cable exit) once the PCB STEP is in.

### Firmware: MotorDriver
- PHASE_EN moved into the class: constructor now also takes `phasePort, phasePin`.
  `enable()` = PHASE_EN high → DRV_OFF low; `disable()` = PHASE_EN low → DRV_OFF high.
  "On" and "off" are now one call each and can't be done half-way.
- The two pins work in opposite directions: PHASE_EN high = phases connected, DRV_OFF high = driver off.
- Fixed the stale "DRV_EN" comments; they now say what each pin level does.

### Firmware: app.cpp
- `printRegister(address, value)`: prints one register as its 8 boxes, bit 7 first (same order as the
  datasheet tables), e.g. `reg 4 = 01100100`. No hex.
- `bootDriver(Drv8316& drv, MotorDriver& motor)`: the whole boot sequence, printing each step:
  init() → register dump CR1–CR6 (driver still off) → PWM start at 0 duty → enable() → 1 ms settle →
  read + print faults ("before clear") → clearFaults() → read + print faults ("after clear") →
  FAULT bit still 1 → error. Any failure prints which step failed and returns HAL_ERROR.
- Before clear = history since power-up; after clear = problems still happening. Decide on "after".
  The NPOR 0 → 1 change also proves clearFaults() works on the real chip.
- Turn-on order: PWM at 0 → PHASE_EN high → DRV_OFF low. With all duties 0, PHASE_EN high just ties every
  phase to ground (braking), so nothing can drive current.
- `app_main()`: DWT on → create Drv8316 (`&hspi3, DRV_CS_GPIO_Port, DRV_CS_Pin`) + MotorDriver →
  `bootDriver()`; on failure: disable() → print "boot failed, driver off" → stop forever (written once).
  Then the blink loop. No blinking = boot failed, even without a serial cable.
- Expected output on a healthy board: reg 3..8 = 00000011 / 01100100 / 01000110 / 00010000 / 00000010 /
  00010000 (reg 3 might read 0, which is fine if 4/7/8 hold their values), "after clear" all 0 with NPOR=1,
  then "drv ready".
- Builds with 0 errors. Not tested on hardware yet (board not here).

### Next
- Mechanical:
  - 8 inserts into the new housing_rear before the motor goes in (4 top for the front housing, 4 bottom for
    the cover). Swap the motor/cam unit across: 4 M2.5 from underneath, motor + cam + bearing lift out as one piece.
  - Test with the front housing screwed down: 0.00/0.7 first, then 0.00/0.9, then +0.05/0.8. Pick the smallest
    holes and tightest profile that turn smoothly; set out_hole_clr in vars.txt to the winner.
  - Buy Super Lube (synthetic grease with PTFE); retry the −0.05 pair once greased.
  - Then carrier inserts → output plate → arm → friction and backlash.
- Firmware:
  - MT6701 encoder over SPI2: fix SPI mode in CubeMX, then 3-byte read + CRC in `Encoder::update()`.
  - nFAULT interrupt (driver off, latched) and IWDG watchdog.

### Lessons
**Mechanical**
- Make each geometric requirement explicit and check it: the output holes needed Ø ≥ pin + 2e, and the
  printed size was only 0.2 above that.
- A sketch that says "Under Defined" can move when one dimension changes. Pattern one fully defined circle
  instead of placing four by hand.
- A bigger hole is a looser fit. Check the direction before changing a value.
- 0.02 mm changes are below what the printer can repeat (~±0.05); don't re-export for them.
- Keep fit-critical parts on the same printer and filament where possible, and note which one was used.
- Counterbore floors printed as bridges sag a few tenths; add a little depth.
- When a test needs a feature that isn't printed yet (front housing screwed down), results are only a guide.

**Firmware**
- C++: `==` is worked out before `&`, so write `((x >> n) & 0b1) == 1` with the extra brackets.
- C++: a variable name can be created once per level of braces; after that, reuse it (`len = ...`, no `int`).
- A function isn't only for reuse: bootDriver() runs once, but it gives failure handling one place and
  gives the sequence a name.

## Nov 20–21: Full-assembly tests, tracking down the output binding, final profile chosen

### Setup
- New housing_rear printed (pin holes 3.35, cover inserts at r31) + disc pairs: 2 dots = 0.00/0.7 holes,
  4 dots = 0.00/0.9, 6 dots = +0.05/0.8. Same printer + black filament.
- 8 inserts in the new housing_rear (4 top for the front housing, 4 bottom for the cover). Motor + cam_1 +
  bearing lifted out of the old housing as one unit (4 M2.5 from underneath) and dropped into the new one.
- Cranking: hex/screwdriver on cam_2's M3, clockwise only (anticlockwise unscrews it). Target effort =
  fingertip twist on a screwdriver handle: 0.02 N·m input ≈ 1.6 N at a 12.5 mm handle radius.

### Test 1: hand-held vs screwed-down front housing
- Hand-held: turned but stiff, and the front housing shifted under my hand. The 6806 + carrier live in the
  front housing, so every time it moves the carrier goes off the cam axis. Hand-held results only a guide.

### Test 2: disc pairs with front housing screwed down + carrier
- 2 dots (0.00 / 0.7): won't turn. 6 dots (+0.05 / 0.8): won't turn. 4 dots (0.00 / 0.9): turns, stiff.
- Bigger output holes help but even 0.9 (~0.3 mm spare per side) isn't enough → something systematic,
  not random print error.

### Test 3: isolate the problem (one change at a time)
- No carrier, front housing screwed down → mesh spins fine.
- Carrier + disc A only + front housing → easy.
- Both discs + carrier, no front housing → turns, but the carrier tilts at the same point every motor turn.
- Conclusion: disc A alone is fine; adding disc B makes the two discs disagree on where the dowels are,
  and the dowels get squeezed between them.
- Why both discs must agree: if disc B's centre is off by δ, its hole-to-dowel distance swings between
  1.5 − δ and 1.5 + δ every motor turn, so each hole needs r ≥ 1.5 + 1.5 + δ. With the front housing on,
  the carrier can't move → friction/jam. Without it → the carrier gets shoved over → periodic tilt.

### Test 4: is it cam_2?
- Theory: cam_2 can slide ~0.1–0.15 on its pegs (Ø3.3 holes on Ø3.0 pegs, peg_clr 0.3) before the M3
  clamps it, putting disc B off-centre.
- Checked new_cam_1/new_cam_2 STLs: big peg in big hole → boss A +1.5, boss B −1.5, exactly opposite.
  Design is correct; only the real-world slop is in question.
- Bench test with cam_2's M3 removed (no front housing): turned better and tilted less at first; on a
  second check the tilt looked about the same. Inconclusive.
- Backdriving the output by hand (hex key in a carrier insert hole) is useless as a test: 10× the friction
  through a short lever, dry. Even a healthy printed cycloid is hard to backdrive that way.
- Some carrier tilt with no front housing is normal anyway: disc A pushes the dowels low, disc B high, in
  opposite directions (a couple). The 6806 in the front housing exists to take it.

### Test 5: the pinch is at a fixed spot
- Pulling the carrier out: 1–2 dowels stick, then free up after turning the rotor. If the holes were just
  too small everywhere, every dowel would be tight all the time → it's a position error, not size.
- The pinch always happens at one place on the housing (bottom right), whatever dowel is there. A cam_2
  error would move round with the motor, so this points at something fixed to the housing.
- 3 ring pins at bottom right lock up during the pinch while the rest stay free (the squeeze reacts into
  the pins there).
- One ring pin was sticky: swapped some pins → the +0.05 pair then ran almost smoothly all the way round.
  0.00 pair: smooth for about half a turn, resistance on the other half → pin circle slightly off-centre
  from the motor axis (or loose pins), absorbed by the extra +0.05.
- Disc mesh is 180° by design (checked). Quick visual check: front housing off, lower disc sticks out
  ~3 mm on one side, upper disc on the opposite side.

### Test 6: ring pins move in their holes
- Measured neighbouring ring pins outside-to-outside (design 2 · 27 · sin(180°/11) + 3 = 18.22):
  free 17.8–17.9 (one 18.0), loaded and stiff 18.1–18.2 → pins shift 0.2–0.4 mm.
- Each pin is only held ~4.5 mm at each end (rear 4.75, front ~4.6). The middle ~11 mm spans the disc stack
  (5 + 0.5 + 5); the cavity height is set by the discs and can't shrink, and the 20 mm pins can't go deeper.
- The pins don't spin anyway (the disc slides on them), so there's no reason for slip holes. Fix them.
- Tape test: one layer of tape on both pin ends, only inside the holes (the discs touch the bare middle,
  so no added friction).
  - Full assembly, +0.05 / 0.8 pair: can't turn.
  - Same taped pins, no carrier: mesh spins freely.
  → Mesh is fine with rigid pins; the output holes are the limit. The loose pins had been hiding part of
    the error by shuffling.

### Conclusions
- Final disc profile: +0.05 (pin radius 1.55) for now. +0.10 felt smoothest of all; decide after grease
  (+0.10 costs only ~0.4° extra backlash).
- Output holes need to be bigger than 0.9. Backlash cost: total play ≈ 2c / 18.5 mm rad (c = spare per
  side): 0.8 → ~1.5°, 1.0 → ~2°, 1.2 → ~2.8°. Bigger holes also mean fewer dowels share the load and
  thinner disc walls (~1.9 mm at Ø7.1), so keep them as small as works.
- Keeping the tape on the pin ends for v1. If the pinch comes back, reprint housing_front with 3.2 press
  holes (only the front; the rear stays 3.35 slip).

### Changes / next plate
- vars.txt: boss_17 17.15 (confirmed), peg_clr 0.3 → 0.18 (Ø3.18 / Ø2.58), out_hole_clr testing 1.0 / 1.2.
- Plate (all checked from the STLs): 2 × (+0.05, Ø7.0, 1 dot), 2 × (+0.05, Ø7.2, 2 dots), cam_2 (peg 0.18,
  boss-down). Old 6-dot pair = +0.05 / 0.8.

### Lessons
- Change one thing per test. Mesh only → one disc + carrier → both discs found the problem in three tries.
- Where the problem happens is a clue: something that turns with the motor gives a pinch that moves round;
  something fixed to the housing gives a pinch in one spot.
- "Too small everywhere" and "off-position" feel different: the first is tight all the time, the second
  only at certain angles.
- A clearance that looks fine can be hidden by parts that shuffle (loose pins). Hold them still before judging.
- Pick a "good enough" line for v1: smooth by hand, backlash measured and written down, weak spots on a
  v2 list. Don't chase the last 0.2°.
- Backdriving a reducer by hand isn't a friction test; drive it from the input.