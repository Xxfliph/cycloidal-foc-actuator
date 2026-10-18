# Cycloidal FOC Actuator

A 10:1 3D-printed cycloidal-drive BLDC actuator with custom C++ field-oriented control (FOC) firmware on STM32, CAN control, and a custom PCB.

> 🚧 Work in progress — demo video and measured results coming January 2027.

<!-- Demo GIF goes here: ![demo](docs/img/demo.gif) -->

## Summary

_One paragraph: what it is, why cycloidal, what the firmware does, what it can demonstrate._

## Specs (measured)

| Parameter | Value |
|---|---|
| Gear ratio | 10:1 (11 ring pins, 10-lobe disc) |
| Motor | GM3506 gimbal BLDC |
| Supply voltage | 15 V (USB-C PD) |
| Control loop rate | _[X] kHz_ |
| Position step rise time | _[X] ms_ |
| Position error | _±[X]°_ |
| Backlash | _[X]°_ |
| Output torque (peak) | _[X] N·m_ |
| CAN telemetry rate | _[X] Hz_ |
| Mass | _[X] g_ |
| Cost | _C$[X]_ |

## System overview

<!-- Block diagram: docs/img/block-diagram.png -->
<!-- Control-loop diagram: docs/img/control-loops.png -->

| Subsystem | Hardware |
|---|---|
| MCU | STM32G474 (dev) → STM32G431 (custom PCB) |
| Motor driver | DRV8313 (SimpleFOC Mini, dev) → DRV8316 (custom PCB) |
| Encoder | MT6701 magnetic encoder, motor side |
| Communication | CAN (SN65HVD230), CANable 2.0 to laptop |

## Repository layout

| Folder | Contents |
|---|---|
| [`firmware/`](firmware/) | STM32CubeIDE C++ project: FOC, encoder, CAN, logging |
| [`hardware/`](hardware/) | Altium PCB project and fabrication outputs |
| [`mechanical/`](mechanical/) | SolidWorks parts, STEP/STL exports |
| [`sim/`](sim/) | Python FOC simulation and cycloid profile generation |
| [`host/`](host/) | Python CAN host tool and plotting scripts |
| [`docs/`](docs/) | Build log, images, notes |

## Results

_Plots: step response, simulation vs hardware, current/temperature._

## What broke and how I fixed it

_See [`docs/log.md`](docs/log.md) for the full build log._

## Bill of materials

_Cost breakdown._
