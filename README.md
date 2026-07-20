# OmniBox

OmniBox is a public non-commercial USB-to-OBD hardware and firmware project for
automotive diagnostics, protocol research and bench ECU work.

The goal of the V1 board is to provide a serious, inspectable and reproducible
interface platform: KiCad sources, schematic, PCB, BOM, manufacturing outputs,
enclosure files, firmware and host-side tools are published so the design can be
reviewed, built, improved and tested by the community.

OmniBox is not a minimal USB-to-CAN adapter. It is designed as a wider vehicle
interface board with galvanic isolation, protected vehicle-side power,
multi-channel CAN/CAN-FD, K-line, J1850/SWCAN hardware stages, programmable
FEPS generation, measurement feedback and a routable OBD pin matrix.

## Release Status

| Item | Version | Status |
|---|---:|---|
| Board | 1.0 beta | Public V1 hardware release |
| Firmware | 0.1 dev | Early J2534 + ELM327 development firmware |
| Host tools | 0.1 dev | J2534 DLL, virtual device and test tools |

Current releases:

- Hardware package: <https://github.com/proxynet2017/OmniBox/releases/tag/hardware-v1.0-beta>
- Firmware package: <https://github.com/proxynet2017/OmniBox/releases/tag/firmware-v0.1-dev>

## What Is Included

The repository contains the public OmniBox V1 package:

- complete KiCad 10 project for the board;
- schematic, PCB layout, local symbols and local footprints;
- consolidated BOM;
- regenerated Gerbers, drill files and manufacturing archive;
- schematic PDF;
- sheet-metal enclosure files and auxiliary 3D-printable parts;
- bare-metal STM32H723 firmware source;
- public USB identity framework with empty experimental slots;
- J2534 transport/core work in progress;
- ELM327-compatible CDC serial work in progress;
- host-side J2534 DLL sources, virtual device and test tools.

Only the public J2534 and ELM327-oriented firmware is included. The
multi-identity architecture remains present, but the public release ships only
empty experimental identity slots.

## Hardware Overview

| Area | V1 capability |
|---|---|
| Host connection | Isolated USB Full-Speed with WinUSB and CDC interfaces |
| Main controller | STM32H723ZGTx Cortex-M7 |
| CAN / CAN-FD | Five routable CAN channels |
| CAN transceivers | NXP TJA1463 automotive CAN SIC transceivers |
| External CAN-FD controllers | Microchip MCP2518FD over SPI |
| K-line | Three ST L9637D ISO 9141-2 / ISO 14230 transceiver channels |
| SWCAN / J1850 | Dedicated hardware stages for Single-Wire CAN, VPW and PWM work |
| FEPS | Programmable voltage generation with current and voltage feedback |
| Routing | Relay and analog-switch matrix for adapting interfaces to OBD pins |
| Isolation | Isolated power and digital barrier between host and vehicle domains |
| Protection | Vehicle input protection, TVS clamps, ESD protection and supervised power |
| Enclosure | Sheet-metal enclosure plus generated STEP/STL files |

## Design Goals

OmniBox V1 was designed around a few practical engineering goals:

- keep the board inspectable and modifiable in KiCad;
- use documented components from known manufacturers;
- separate the PC side from the vehicle side with galvanic isolation;
- support more than the common two-pin CAN use case;
- expose enough routing flexibility for real diagnostic and bench scenarios;
- leave firmware headroom for timing-sensitive protocol work;
- provide manufacturing files that can be reviewed and ordered without hidden
  private assets;
- make the public project useful even before the full firmware feature set is
  complete.

## Main Hardware Choices

### STM32H723ZGTx MCU

The board uses an STM32H723ZGTx Cortex-M7 microcontroller to leave substantial
headroom for USB transport, protocol scheduling, buffering, timestamping and
future firmware work.

The STM32H7 family is a good fit for this project because it provides:

- high single-core performance for real-time protocol handling;
- native FDCAN peripherals;
- enough RAM for queues, traces and transport buffers;
- USB device capability;
- mature GCC/CMake support;
- a large ecosystem of public documentation and examples.

The firmware is bare-metal C using CMSIS, STM32H7 HAL and TinyUSB. This keeps the
runtime understandable and avoids hiding protocol behavior behind a large RTOS
or opaque vendor framework.

### CAN and CAN-FD

OmniBox V1 exposes five CAN-capable channels. The design combines the STM32H7
native FDCAN peripherals with external Microchip MCP2518FD controllers where
additional channels are needed.

The board uses NXP TJA1463 automotive CAN SIC transceivers. The choice favors a
modern automotive CAN physical layer, CAN-FD capability and better signal
integrity margins than older generic transceiver choices.

This gives the hardware enough flexibility for:

- standard OBD CAN use;
- multi-bus ECUs and gateways;
- bench harnesses with more than one CAN network;
- CAN-FD experiments;
- future J2534 channel work.

### K-line

Three ST L9637D transceiver channels are included for ISO 9141-2 and ISO 14230
style K-line work. Keeping multiple hardware K-line channels avoids treating
K-line as an afterthought and gives the routing matrix useful options for ECUs
and vehicles that do not follow the simplest pinout assumptions.

### SWCAN and J1850

The V1 board includes dedicated hardware stages for Single-Wire CAN and J1850
VPW/PWM work. These parts of the design are important because many interfaces
focus only on CAN and leave older or less common physical layers unsupported.

Firmware support for these areas is still work in progress. The hardware exists
so contributors can validate, measure and implement protocol support without
needing a new board revision first.

### FEPS Generation and Measurement

OmniBox includes a programmable FEPS supply stage built around an LT8362 boost
controller, MCP4922 DAC control, INA240 current measurement and MCP3426 ADC
feedback.

The intent is not simply to switch a fixed voltage. The board is designed to
generate and supervise a controlled programming voltage, with feedback available
to firmware. That makes it more suitable for controlled experiments and safer
firmware development than a blind always-on rail.

### Isolation Barrier

The host and vehicle domains are separated. The design uses isolated digital
channels, isolated I2C and an isolated power stage built around parts such as
TI ISO774x digital isolators, ISO1640 isolated I2C and an SN6505B transformer
driver.

This isolation strategy is a deliberate board-level choice. It reduces the risk
of ground loops and helps protect the PC side when working with noisy vehicle
or bench environments. It also makes the schematic easier to reason about:
host-side logic and vehicle-side physical layers are clearly separated.

### Power Input and Protection

Vehicle power is noisy and unforgiving. OmniBox V1 includes a protected input
stage, a definitive SM8S24A TVS orientation on the local-finish manufacturing
package, filtering and a TI LM61460-Q1 buck regulator.

The use of an automotive-grade regulator and high-energy TVS protection is a
pragmatic choice: the board is expected to be connected to real harnesses,
bench supplies and vehicle-like conditions where transients, reverse events and
operator mistakes must be considered during design review.

### Routing Matrix

The routing matrix is one of the most important parts of the board. It combines
relays and protected analog switches, including ADG5412F devices, to route
physical interfaces to OBD/J1962 pins.

The goal is to support more diagnostic topologies without rewiring the bench
every time:

- route CAN channels to different OBD pins;
- expose K-line options;
- switch FEPS where supported by the design;
- handle less common pin assignments;
- keep firmware in control of safe idle states.

### Mechanical Design

The recommended enclosure is the sheet-metal design under
`hardware/omnibox-v1/enclosure/sheetmetal/`.

Generated STEP/STL files are included so the mechanical design can be reviewed,
quoted or adapted without running private scripts. Auxiliary printable parts are
available under `hardware/omnibox-v1/enclosure/print3d/`.

## Firmware Status

Firmware version `0.1 dev` is an early public development release.

Implemented or present:

- TinyUSB-based USB device stack;
- WinUSB vendor bulk transport for J2534-oriented communication;
- CDC serial interface for ELM327-style commands;
- public USB mode switching command;
- multi-identity infrastructure with empty public slots;
- board driver structure for CAN/CAN-FD, K-line, J1850/SWCAN, FEPS and routing;
- host-side tests and a virtual device for transport validation.

Still to test, complete or improve:

- complete J2534 API behavior across all supported protocols;
- robust timing, filtering and queue behavior under load;
- ISO-TP edge cases and long-transfer validation;
- K-line initialization and timing validation on real ECUs;
- SWCAN and J1850 firmware support;
- FEPS sequencing and safety interlocks;
- more ELM327 command compatibility;
- Windows installer/driver packaging;
- real-vehicle and bench ECU test matrix.

## Building the Firmware

Prerequisites:

- `arm-none-eabi-gcc`;
- CMake;
- Ninja;
- Git access for the pinned dependencies.

Build:

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

The first configure downloads pinned versions of CMSIS, STM32H7 HAL and TinyUSB
through CMake FetchContent.

## Manufacturing

Main hardware files:

- `hardware/omnibox-v1/omnibox-v1.kicad_pro`
- `hardware/omnibox-v1/omnibox-v1.kicad_sch`
- `hardware/omnibox-v1/omnibox-v1.kicad_pcb`
- `hardware/omnibox-v1/production/bom-consolide.csv`
- `hardware/omnibox-v1/production/j2534-schematics.pdf`
- `hardware/omnibox-v1/production/local-finish/gerber/`
- `hardware/omnibox-v1/production/local-finish/j2534-gerbers-jlcpcb.zip`
- `hardware/omnibox-v1/enclosure/`

The local-finish Gerber package was regenerated with KiCad 10.0.4 from the
corrected PCB. The `D_PWR1` footprint uses the definitive `SM8S24A` orientation.

Before ordering boards:

- review the schematic and PCB in KiCad;
- verify the BOM against current distributor availability;
- verify component orientation with the assembler;
- review DRC warnings and vendor manufacturing rules;
- check stack-up, drill files, solder mask and paste layers;
- validate enclosure dimensions against your connectors and assembly process.

## Repository Layout

```text
hardware/omnibox-v1/    Board 1.0 beta KiCad sources, production files and enclosure
firmware/               Firmware 0.1 dev for STM32H723
host/                   J2534 DLL, virtual device and PC-side tools
docs/                   Additional public notes
LICENSE.md              Public non-commercial license and commercial license notice
SUPPORT.md              Donations, sponsoring and contribution information
FORUM_POST.md           Forum presentation draft
```

## Contributing

Useful contribution areas:

- schematic and PCB review;
- automotive protection and isolation review;
- BOM validation and second-source suggestions;
- enclosure and mechanical improvements;
- firmware drivers for the existing hardware blocks;
- J2534 behavior, tests and host compatibility;
- ELM327 command compatibility;
- protocol timing validation on bench ECUs;
- documentation, diagrams and setup notes.

The project especially benefits from contributors who can test on real hardware,
review automotive analog design, improve Windows host integration or help turn
the existing hardware blocks into reliable firmware features.

## License

OmniBox uses a dual-license model.

The public repository is available for non-commercial use only. Commercial use,
commercial manufacturing, paid services, commercial integration and resale
require prior written permission from the maintainer.

See `LICENSE.md` for the full terms. See `SUPPORT.md` for donation, sponsoring
and commercial licensing information.

## Safety Notice

OmniBox connects to vehicle communication lines and can generate programming
voltages. Hardware errors, assembly mistakes, firmware defects, incorrect
routing, unsuitable test conditions or misuse can damage ECUs, vehicles,
computers, tools or data.

Use the design only if you understand the electrical and legal risks. Review and
test the hardware before connecting it to anything valuable.
