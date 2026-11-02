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