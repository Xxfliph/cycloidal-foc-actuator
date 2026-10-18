# Firmware

STM32CubeIDE C++ project for the actuator.

- **Dev target:** WeAct STM32G474CEU6 core board + SimpleFOC Mini (DRV8313)
- **Final target:** STM32G431 on the custom PCB with DRV8316
- **Structure:** C++ application classes (`Encoder`, `FocController`, `MotorDriver`, `CanInterface`, `Logger`) on top of ST's C HAL/LL and CubeMX-generated init code.

Commit the `.ioc` file so the CubeMX configuration can be regenerated. Build folders (`Debug/`, `Release/`) are ignored.
