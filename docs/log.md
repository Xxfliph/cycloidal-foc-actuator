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

### PIController (pure math, PC-tested)
- `PIController(kp, ki, outLimit)`, `update(error, dt)`, `reset()`.
  Caller computes the error, so it works for the velocity and position loops.
- update(): if dt > 0 → integral += Ki·e·dt, clamped to ±outLimit;
  output = Kp·e + integral, clamped to ±outLimit.
- Anti-windup: clamp the integral itself, not just the output.
  Otherwise a stuck rotor winds it up far past the limit and the motor stays at full vq after release.
- if dt ≤ 0 → skip integration only. reset() → integral = 0 (call after calibration / any disable).
