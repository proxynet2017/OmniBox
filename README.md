<div align="center">

# OmniBox

**A public non-commercial ECU diagnostics and reprogramming interface platform**

OmniBox V1 is a KiCad-based USB-to-OBD and bench ECU interface designed for
serious diagnostic, protocol research and ECU reprogramming workflows.

![OmniBox V1 board render](docs/images/omnibox-v1-board-iso-complete.png)

[![Board](https://img.shields.io/badge/board-1.0%20beta-black)](https://github.com/proxynet2017/OmniBox/releases/tag/hardware-v1.0-beta)
[![Firmware](https://img.shields.io/badge/firmware-0.1%20dev-orange)](https://github.com/proxynet2017/OmniBox/releases/tag/firmware-v0.1-dev)
[![KiCad](https://img.shields.io/badge/KiCad-10-blue)](hardware/omnibox-v1/)
[![License](https://img.shields.io/badge/license-non--commercial-red)](LICENSE.md)

</div>

## What It Is

OmniBox is a public hardware and firmware project for building a capable vehicle
interface around documented, inspectable design files. The V1 board is intended
to be more than a simple USB-to-CAN adapter: it combines isolated USB, protected
vehicle power, multi-channel CAN/CAN-FD, K-line, J1850/SWCAN hardware stages,
programmable FEPS generation, measurement feedback and an OBD routing matrix.

The hardware is designed to support ECU communication and reprogramming
workflows both through OBD and on the bench. That includes the physical-layer
building blocks normally needed for flashing-oriented work: controlled vehicle
side power, routable diagnostic lines, FEPS generation, CAN/CAN-FD, K-line and
feedback measurements. Firmware support is still in early development, so this
should be read as a hardware capability and project direction, not a claim that
all reprogramming workflows are already validated.

## Why It Exists

Most low-cost diagnostic interfaces are closed, narrow or difficult to inspect.
OmniBox takes the opposite approach:

| Goal | What V1 Provides |
|---|---|
| Inspectable hardware | KiCad schematic, PCB, footprints, BOM and production files |
| Real ECU work | OBD and bench-oriented routing, FEPS and multiple bus interfaces |
| Electrical resilience | Isolation, TVS protection, protected power input and automotive parts |
| Firmware headroom | STM32H723 Cortex-M7 with native FDCAN and enough RAM for queues/traces |
| Community development | Public J2534/ELM327 firmware base and host-side test tools |
| Reproducibility | Gerbers, drill files, enclosure files and release assets |

## Current Releases

| Package | Version | Link |
|---|---:|---|
| Hardware manufacturing package | 1.0 beta | [hardware-v1.0-beta](https://github.com/proxynet2017/OmniBox/releases/tag/hardware-v1.0-beta) |
| Firmware source and binaries | 0.1 dev | [firmware-v0.1-dev](https://github.com/proxynet2017/OmniBox/releases/tag/firmware-v0.1-dev) |

Firmware release assets include `.elf`, `.bin`, `.hex`, `.map` and source
archive files.

## 3D Preview

The board can be inspected as a rendered image or as a STEP model:

- [board render PNG](docs/images/omnibox-v1-board-iso-complete.png)
- [board STEP model](docs/3d/omnibox-v1-board-complete.step)

## Hardware Highlights

| Area | OmniBox V1 |
|---|---|
| Host connection | Isolated USB Full-Speed with WinUSB and CDC interfaces |
| MCU | STM32H723ZGTx Cortex-M7 |
| CAN / CAN-FD | Five routable CAN-capable channels |
| CAN physical layer | NXP TJA1463 automotive CAN SIC transceivers |
| External CAN-FD | Microchip MCP2518FD controllers over SPI |
| K-line | Three ST L9637D ISO 9141-2 / ISO 14230 channels |
| SWCAN / J1850 | Dedicated Single-Wire CAN, VPW and PWM hardware stages |
| FEPS | Programmable boost stage with DAC control and ADC/current feedback |
| Routing | Relay and protected analog-switch matrix for OBD/J1962 pins |
| Protection | SM8S24A TVS, ESD/TVS protection, filtering and protected power input |
| Isolation | Isolated power, isolated I2C and digital isolators |
| Mechanical | Sheet-metal enclosure plus STEP/STL printable/mechanical files |

## Reprogramming-Oriented Design

OmniBox V1 was designed with ECU flashing and calibration workflows in mind,
including both in-vehicle OBD work and bench harness work.

Hardware features relevant to that goal:

- routable CAN/CAN-FD channels for modern ECU and gateway communication;
- multiple K-line channels for older ISO 9141-2 / ISO 14230 ECUs;
- J1850/SWCAN hardware stages for less common physical layers;
- programmable FEPS generation rather than a blind fixed rail;
- voltage and current feedback around the FEPS stage;
- controlled routing to OBD/J1962 pins;
- galvanic isolation between PC and vehicle domains;
- protected vehicle-side supply input and transient handling;
- enough MCU performance for transport queues, timing and future flashing logic.

The public firmware currently focuses on the J2534 and ELM327 base. The board is
intended to grow into a reliable open non-commercial flashing-capable platform,
but protocol coverage and validation still need contributors and hardware
testing.

## Main Component Choices

### STM32H723ZGTx

The STM32H723 gives the project room to grow. Its Cortex-M7 core, native FDCAN
peripherals, USB device support and memory headroom make it suitable for timing
sensitive transport work, buffering, trace collection and future protocol logic.

The firmware is bare-metal C using CMSIS, STM32H7 HAL and TinyUSB. This keeps
the stack understandable and avoids hiding protocol behavior behind a large
opaque framework.

### NXP TJA1463 and Microchip MCP2518FD

The board combines native STM32 FDCAN peripherals with external MCP2518FD
controllers to reach five CAN-capable channels. TJA1463 automotive CAN SIC
transceivers provide a modern physical layer suitable for CAN-FD experiments and
cleaner signaling margins than older generic CAN transceivers.

### ST L9637D K-line

Three L9637D channels are included so K-line support is not treated as a single
legacy pin. This matters for bench ECUs and vehicles where routing assumptions
are not always simple.

### FEPS Stage

The FEPS stage uses an LT8362 boost controller with MCP4922 DAC control, INA240
current measurement and MCP3426 ADC feedback. The point is controllability:
firmware can supervise and sequence a programming voltage instead of blindly
switching a fixed output.

### Isolation and Protection

The host side and vehicle side are separated with isolated digital channels,
isolated I2C and isolated power. Vehicle power is handled through a protected
input stage, high-energy TVS protection and an LM61460-Q1 regulator.

That architecture is deliberate. It reduces PC-side exposure during bench work,
makes ground domains easier to reason about and gives reviewers clear protection
blocks to inspect.

### Routing Matrix

The routing matrix uses relays and protected ADG5412F analog switches to map
interfaces to OBD/J1962 pins. This makes the board useful for more than one
fixed pinout and reduces external wiring changes during protocol development.

## Public Firmware

Firmware version `0.1 dev` is an early public development release.

Present today:

- TinyUSB device stack;
- WinUSB vendor bulk transport for J2534-oriented communication;
- CDC serial interface for ELM327-style commands;
- public USB mode switching command;
- multi-identity framework with empty public experimental slots;
- board driver structure for CAN/CAN-FD, K-line, J1850/SWCAN, FEPS and routing;
- host-side tests and virtual device tooling.

Still in progress:

- complete J2534 behavior and edge cases;
- protocol timing under real load;
- ISO-TP long transfers;
- K-line initialization and timing validation;
- SWCAN and J1850 firmware support;
- FEPS sequencing and safety interlocks;
- more ELM327 compatibility;
- Windows packaging and broader host validation;
- real ECU bench and OBD test matrix.

## Build Firmware

Prerequisites:

- `arm-none-eabi-gcc`;
- CMake;
- Ninja;
- Git access for pinned dependencies.

```bash
cmake -S firmware -B firmware/build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/arm-gcc.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build firmware/build
```

Expected outputs:

- `firmware/build/firmware.elf`
- `firmware/build/firmware.bin`
- `firmware/build/firmware.hex`
- `firmware/build/firmware.map`

## Manufacturing Files

| File | Purpose |
|---|---|
| `hardware/omnibox-v1/omnibox-v1.kicad_pro` | KiCad project |
| `hardware/omnibox-v1/omnibox-v1.kicad_sch` | Main schematic |
| `hardware/omnibox-v1/omnibox-v1.kicad_pcb` | PCB layout |
| `hardware/omnibox-v1/production/bom-consolide.csv` | Consolidated BOM |
| `hardware/omnibox-v1/production/j2534-schematics.pdf` | Schematic PDF |
| `hardware/omnibox-v1/production/local-finish/j2534-gerbers-jlcpcb.zip` | Gerber archive |
| `hardware/omnibox-v1/enclosure/` | Sheet-metal and printable enclosure files |

The local-finish Gerber package was regenerated with KiCad 10.0.4 from the
corrected PCB. `D_PWR1` uses the definitive `SM8S24A` orientation.

Before ordering boards, review the schematic, PCB, BOM, component orientation,
DRC warnings, stack-up, solder mask, paste layers and enclosure fit with your
manufacturer or assembler.

## Repository Layout

```text
hardware/omnibox-v1/    KiCad board, production files and enclosure
firmware/               STM32H723 firmware
host/                   J2534 DLL, virtual device and PC-side tools
docs/                   Images and public documentation
LICENSE.md              Public non-commercial license and commercial notice
SUPPORT.md              Donations, sponsoring and contribution information
FORUM_POST.md           Forum presentation draft
```

## Looking For Contributors

Useful help areas:

- board review and DRC/manufacturing feedback;
- automotive protection and isolation review;
- BOM validation and alternate parts;
- J2534 implementation and tests;
- ELM327 compatibility;
- K-line, J1850, SWCAN and FEPS firmware work;
- ECU bench validation;
- Windows host tooling and packaging;
- enclosure/mechanical improvements;
- documentation and test procedures.

If you can test on real ECUs, review automotive analog hardware or help turn the
existing hardware blocks into reliable firmware features, your contribution is
especially valuable.

## License

OmniBox is public for non-commercial use only.

Commercial use, commercial manufacturing, resale, paid services, commercial
integration and commercial distribution require prior written permission from the
maintainer. See [LICENSE.md](LICENSE.md) for the full terms and [SUPPORT.md](SUPPORT.md)
for donation, sponsoring and commercial licensing information.

## Safety Notice

OmniBox connects to vehicle communication lines and can generate programming
voltages. Hardware errors, assembly mistakes, firmware defects, incorrect
routing, unsuitable test conditions or misuse can damage ECUs, vehicles,
computers, tools or data.

Use the design only if you understand the electrical and legal risks. Review and
test the hardware before connecting it to anything valuable.
