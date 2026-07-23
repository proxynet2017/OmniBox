# OmniBox V1 Hardware Test Protocol

Step-by-step bring-up protocol for one assembled OmniBox V1 board.

Allowed equipment:

- Fluke 287 DMM;
- PC with USB-C;
- external 12 V bench supply with adjustable current limit.

No DB25 fixture, oscilloscope, logic analyzer, ECU or vehicle is required for this protocol.

## 1. Scope

This protocol validates:

- input protection sanity and no-short state;
- USB-side power, firmware startup and enumeration;
- isolated vehicle-side rails from USB;
- 12 V vehicle-domain power path;
- default safe routing state;
- CAN recessive bias on directly exposed CAN channels;
- K-line recessive levels;
- FEPS safe-off state;
- ELM327 CDC command surface;
- J2534 USB mode access where a Windows PC is available.

Still not validated by this protocol:

- real CAN/CAN-FD frames on a terminated bus;
- ISO9141/KWP traffic against a real ECU;
- J1850 waveform timing;
- SWCAN wake/signaling;
- FEPS regulation under load;
- vehicle/ECU compatibility.

Do not connect the board to a vehicle or ECU until those deferred tests have been completed.

## 2. Software Preparation

Run from the repository root:

```bash
cd /path/to/OmniBox   # repository root
./host/test/run_tests.sh

cd firmware
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-gcc.cmake
cmake --build build
```

Expected:

- all host tests pass;
- `firmware/build/firmware.elf`, `firmware/build/firmware.bin` and `firmware/build/firmware.hex` are generated;
- firmware version string is `OmniBox FW 0.1 dev HW:1.0 beta`.

Flash `firmware/build/firmware.bin` or `firmware/build/firmware.elf` using the STM32H723 flashing method used on your bench, then continue.

## 3. Measurement Rules

General rules:

- Unpowered resistance and diode checks: no USB, no 12 V.
- Voltage checks: black probe on the correct ground domain.
- Vehicle-domain reference: `GND_V`, DB25 pin 4, 5 or 17.
- Host-domain reference: `GND_H`, USB-side ground.
- Never intentionally short `GND_H` and `GND_V`.
- Use Fluke `REL` before low-resistance measurements.
- Current is read on the bench supply display unless a series current measurement is explicitly required.

Practical probe points:

| Net | Probe point |
|---|---|
| `VBAT` | DB25 pin 16, 23 or 24, or the 12 V input jack |
| `VBAT_PROT` | Q_PWR1 drain tab, C_PWR2 positive terminal, D_PWR1 cathode |
| `V5_BUCK` | L_PWR1 output, C_PWR7, D_PWR2 anode |
| `5V_V` | D_PWR2 cathode, U_KL1/U_KL2/U_KL3 pin 3 |
| `3V3_V` | U_ISO3 output, R_ISO4 vehicle-side LED resistor |
| `VFEPS` | DB25 pin 25 |
| `GND_V` | DB25 pin 4, 5 or 17 |
| `GND_H` | USB-side ground near the USB connector |
| `3V3_H` | USB-side 3.3 V rail |

DB25 orientation: viewed from the outside of the female connector, top row is pins 1 to 13, bottom row is pins 14 to 25.

## 4. DB25 Quick Reference

| Pin | Default function |
|---:|---|
| 1 | routed OEM pin, default open |
| 2 | J1850 BUS+ static path |
| 3 | routed OEM pin, default open |
| 4 | `GND_V` |
| 5 | `GND_V` |
| 6 | CAN1-H |
| 7 | K-line 1 fixed path |
| 8 | routed OEM pin, default open |
| 9 | routed OEM pin / FEPS candidate, default open |
| 10 | J1850 BUS- candidate, default open |
| 11 | routed OEM pin / FEPS candidate, default open |
| 12 | routed OEM pin / CAN swap / FEPS candidate, default open |
| 13 | routed OEM pin / CAN swap / FEPS candidate, default open |
| 14 | CAN1-L |
| 15 | routed OEM pin, default open |
| 16 | `VBAT` input |
| 17 | `GND_V` |
| 18 | GPT1 candidate, default open |
| 19 | GPT2 candidate, default open |
| 20 | CAN5-H |
| 21 | CAN5-L |
| 22 | K-line 3 fixed path |
| 23 | `VBAT` input |
| 24 | `VBAT` input |
| 25 | `VFEPS` output node with bleeder |

## 5. Phase 0 - Visual Inspection

Board disconnected from everything.

1. Inspect all fine-pitch packages for bridges: STM32, LM61460, MCP2518FD, TJA1463, PCA9555, ADG5412F and L9637.
2. Confirm polarized parts:
   - D_PWR1 is `SM8S24A`, not a bidirectional variant;
   - D_PWR1 cathode is on `VBAT_PROT`;
   - D_PWR1 anode is on `GND_V`;
   - D_PWR2 cathode is on `5V_V`;
   - D_FE1 cathode is on `VFEPS_RAW`;
   - D_ISO1 and D_ISO2 cathodes are on `5V_V`.
3. Confirm relay orientation against silkscreen.
4. Confirm no cracked, lifted or tombstoned component.

Pass: no visible fault and D_PWR1 orientation is certain.

## 6. Phase 1 - Unpowered Rail Checks

Board disconnected from USB and 12 V.

| Step | Fluke mode | Measurement | Expected result | Stop condition |
|---:|---|---|---|---|
| 1.1 | Ohms | DB25 pin 16 to `GND_V` | about 113 kOhm to 120 kOhm | below 10 kOhm |
| 1.2 | Ohms | `VBAT_PROT` to `GND_V` | no short, normally high impedance | below 10 kOhm |
| 1.3 | Ohms | `5V_V` to `GND_V` | above 10 kOhm | below 1 kOhm |
| 1.4 | Ohms | `3V3_V` to `GND_V` | above 10 kOhm | below 1 kOhm |
| 1.5 | Ohms | `3V3_H` to `GND_H` | no short | below 1 kOhm |
| 1.6 | Ohms | `VBUS` to `GND_H` | no short | below 1 kOhm |
| 1.7 | Ohms | `GND_H` to `GND_V` | above 10 MOhm | below 1 MOhm |
| 1.8 | Capacitance | `GND_H` to `GND_V` | about 2 nF to 3 nF | above 10 nF |

If any stop condition occurs, do not power the board.

## 7. Phase 2 - Unpowered Diode Checks

Board disconnected from USB and 12 V.

| Step | Fluke mode | Red lead | Black lead | Expected result |
|---:|---|---|---|---|
| 2.1 | Diode | `GND_V` | `VBAT_PROT` | about 0.5 V to 0.8 V |
| 2.2 | Diode | `VBAT_PROT` | `GND_V` | OL |
| 2.3 | Diode | `V5_BUCK` | `5V_V` | about 0.25 V to 0.45 V |
| 2.4 | Diode | `5V_V` | `V5_BUCK` | OL |
| 2.5 | Diode | DB25 pin 16 | `VBAT_PROT` | about 0.4 V to 0.7 V |
| 2.6 | Diode | `VBAT_PROT` | DB25 pin 16 | OL |

Meaning:

- 2.1 and 2.2 validate the definitive `SM8S24A` polarity.
- 2.3 and 2.4 validate the buck-to-vehicle-5V OR-ing diode.
- 2.5 and 2.6 validate input-path continuity through F1/body-diode and reverse blocking.

## 8. Phase 3 - Default Static Routing Checks

Board unpowered.

| Step | Fluke mode | Measurement | Expected result |
|---:|---|---|---|
| 3.1 | Ohms, REL | DB25 pin 6 to pin 14 | about 121 Ohm |
| 3.2 | Ohms | DB25 pin 20 to pin 21 | above 10 kOhm |
| 3.3 | Ohms, REL | DB25 pin 7 to `VBAT_PROT` | about 510 Ohm |
| 3.4 | Ohms, REL | DB25 pin 22 to `VBAT_PROT` | about 510 Ohm |
| 3.5 | Ohms, REL | DB25 pin 7 to pin 22 | about 1020 Ohm |
| 3.6 | Ohms | DB25 pin 2 to `GND_V` | about 7.5 kOhm |
| 3.7 | Ohms | DB25 pin 10 to `GND_V` | high impedance |
| 3.8 | Ohms | DB25 pin 25 to `GND_V` | about 4.5 kOhm |
| 3.9 | Ohms | DB25 pin 18 to pin 25 | OL |
| 3.10 | Ohms | DB25 pin 19 to pin 25 | OL |
| 3.11 | Ohms | DB25 pins 9, 11, 12, 13 to pin 25 | OL |
| 3.12 | Ohms | DB25 pin pairs 3-11, 12-13, 1-9 | OL |

Pass:

- CAN1 default termination is present.
- CAN5 has no onboard termination.
- K-line fixed pull-ups are present.
- FEPS and GPT are open in the default safe state.

## 9. Phase 4 - USB-Only Power-Up

DB25 disconnected. Bench supply disconnected.

1. Connect USB-C to the PC.
2. Watch for a stable USB attach event.
3. Check enumeration.

Linux:

```bash
dmesg --follow
lsusb
ls -l /dev/serial/by-id/ 2>/dev/null || true
```

Windows:

```powershell
Get-PnpDevice -PresentOnly | findstr /i "OmniBox USB Serial WinUSB"
```

Expected:

- stable enumeration;
- no connect/disconnect loop;
- in mode 0, one WinUSB vendor interface and one CDC serial interface.

If the PC reports USB over-current, unplug immediately and return to Phase 1.

## 10. Phase 5 - USB-Only Rail Measurements

Board connected to USB only.

| Step | Measurement | Reference | Expected result |
|---:|---|---|---|
| 5.1 | `VBUS` | `GND_H` | about 5.0 V |
| 5.2 | `3V3_H` | `GND_H` | about 3.3 V |
| 5.3 | `5V_V` | `GND_V` | about 5.0 V to 5.5 V |
| 5.4 | `3V3_V` | `GND_V` | about 3.3 V |
| 5.5 | `VBAT_PROT` | `GND_V` | near 0 V |
| 5.6 | DB25 pin 25 | `GND_V` | near 0 V |

Pass:

- host and isolated vehicle rails are present;
- USB does not significantly back-power `VBAT_PROT`;
- FEPS output remains off.

## 11. Phase 6 - 12 V Power-Up Without USB

USB disconnected. DB25 disconnected except power input.

1. Set the bench supply to 12.0 V.
2. Set current limit to 200 mA.
3. Connect supply positive to DB25 pin 16, 23 or 24, or to the 12 V input jack.
4. Connect supply negative to DB25 pin 4, 5 or 17.
5. Turn the supply on.
6. Observe current for 30 seconds.

Expected:

- brief inrush is acceptable;
- steady current should be about 60 mA to 160 mA;
- no component should become hot;
- U_FE1 must remain cold because FEPS is off.

Stop immediately if:

- current stays at the 200 mA limit;
- a component heats quickly;
- `VFEPS` rises unexpectedly.

## 12. Phase 7 - 12 V Rail Measurements

Board powered from 12 V bench supply only.

| Step | Measurement | Reference | Expected result |
|---:|---|---|---|
| 7.1 | DB25 pin 16 | `GND_V` | bench voltage, about 12.0 V |
| 7.2 | `VBAT_PROT` | `GND_V` | about 11.95 V to 12.05 V |
| 7.3 | `V5_BUCK` | `GND_V` | about 5.4 V |
| 7.4 | `5V_V` | `GND_V` | about 5.0 V to 5.2 V |
| 7.5 | `3V3_V` | `GND_V` | about 3.3 V |
| 7.6 | DB25 pin 25, `VFEPS` | `GND_V` | near 0 V |
| 7.7 | FEPS controller `INTVCC`, if accessible | `GND_V` | near 0 V |
| 7.8 | VBATT sense divider node, if accessible | `GND_V` | about 1.36 V to 1.38 V at 12.0 V input |
| 7.9 | INA240 output, if accessible | `GND_V` | near 0 V |
| 7.10 | ADG5412F VDD, if accessible | `GND_V` | about `VBAT_PROT` |

Known-good bring-up references from the V1 hardware session:

- `VBAT_PROT`: about 12.0 V from 12.05 V input;
- `V5_BUCK`: about 5.4 V;
- `5V_V`: about 5.1 V;
- `3V3_V`: about 3.3 V;
- `VFEPS`: about 0.028 V;
- VBATT sense: about 1.358 V at 12 V input.

Pass: all accessible rails are in range and steady.

## 13. Phase 8 - 12 V Static Bus Levels

Board powered from 12 V bench supply only. USB still disconnected.

| Step | Measurement | Reference | Expected result |
|---:|---|---|---|
| 8.1 | DB25 pin 6, CAN1-H | `GND_V` | about 2.5 V |
| 8.2 | DB25 pin 14, CAN1-L | `GND_V` | about 2.5 V |
| 8.3 | DB25 pin 6 to pin 14 | differential V DC | near 0 V |
| 8.4 | DB25 pin 20, CAN5-H | `GND_V` | about 2.5 V |
| 8.5 | DB25 pin 21, CAN5-L | `GND_V` | about 2.5 V |
| 8.6 | DB25 pin 7, K-line 1 | `GND_V` | may be dominant without USB; record value |
| 8.7 | DB25 pin 22, K-line 3 | `GND_V` | may be dominant without USB; record value |
| 8.8 | DB25 pin 25, `VFEPS` | `GND_V` | near 0 V |

Notes:

- CAN recessive levels around 2.5 V validate transceiver power and idle bias.
- K-line levels without USB can be misleading because MCU-side control is not active. The decisive K-line check is Phase 10 with firmware running.

## 14. Phase 9 - Combined USB + 12 V Power

1. Keep the 12 V bench supply connected and current-limited to 300 mA.
2. Connect USB to the PC.
3. Wait for USB enumeration.
4. Observe 12 V input current.

Expected:

- stable USB enumeration;
- no reset loop;
- 12 V current remains stable, typically below 160 mA;
- `5V_V` remains about 5.0 V to 5.2 V;
- `3V3_V` remains about 3.3 V;
- `VFEPS` remains near 0 V.

Stop if the 12 V supply current jumps unexpectedly or if USB repeatedly disconnects.

## 15. Phase 10 - Firmware-Controlled Static Levels

Board powered from USB and 12 V. Firmware running in mode 0.

Set the USB identity to the public J2534 + ELM327 mode.

Windows hardware command:

```powershell
.\j2534mode.exe get
.\j2534mode.exe set j2534
```

Linux can only use `j2534mode` against the virtual TCP device in the current host tools; direct WinUSB hardware access is Windows-only.

Measure:

| Step | Measurement | Reference | Expected result |
|---:|---|---|---|
| 10.1 | DB25 pin 7, K-line 1 | `GND_V` | about 12 V, recessive |
| 10.2 | DB25 pin 22, K-line 3 | `GND_V` | about 12 V, recessive |
| 10.3 | DB25 pin 25, `VFEPS` | `GND_V` | near 0 V |
| 10.4 | DB25 pin 6, CAN1-H | `GND_V` | about 2.5 V |
| 10.5 | DB25 pin 14, CAN1-L | `GND_V` | about 2.5 V |
| 10.6 | DB25 pin 20, CAN5-H | `GND_V` | about 2.5 V |
| 10.7 | DB25 pin 21, CAN5-L | `GND_V` | about 2.5 V |

Pass:

- K-lines are recessive with firmware active;
- FEPS stays off;
- CAN buses remain recessive.

## 16. Phase 11 - ELM327 CDC Smoke Test

Board powered from USB. 12 V may remain connected.

Linux:

```bash
python3 - <<'PY'
import glob, time
import serial

ports = glob.glob('/dev/serial/by-id/*') + glob.glob('/dev/ttyACM*')
if not ports:
    raise SystemExit('No CDC serial port found')

p = serial.Serial(ports[0], 38400, timeout=0.05)

def transact(cmd, timeout=1.5):
    p.reset_input_buffer()
    p.write((cmd + '\r').encode())
    end = time.time() + timeout
    data = bytearray()
    while time.time() < end:
        chunk = p.read(64)
        if chunk:
            data.extend(chunk)
            if b'>' in data:
                break
    text = data.decode(errors='replace').replace('\r', '\n')
    lines = [line.strip() for line in text.split('\n')
             if line.strip() and line.strip() != '>']
    print(f'{cmd}: raw={bytes(data)!r} lines={lines}')

for cmd in ['ATZ', 'ATI', 'ATE0', 'ATL0', 'ATS0', 'ATSP0']:
    transact(cmd)
p.close()
PY
```

Windows PowerShell:

```powershell
$port = "COMx"
$sp = New-Object System.IO.Ports.SerialPort $port,38400,None,8,one
$sp.ReadTimeout = 1000
$sp.Open()
foreach ($cmd in "ATZ`r","ATI`r","ATE0`r","ATL0`r","ATS0`r","ATSP0`r") {
  $sp.Write($cmd)
  Start-Sleep -Milliseconds 200
  $sp.ReadExisting()
}
$sp.Close()
```

Expected:

| Command | Expected response |
|---|---|
| `ATZ` | reset banner or `ELM327 v1.5` |
| `ATI` | `ELM327 v1.5` |
| `ATE0` | `OK` |
| `ATL0` | `OK` |
| `ATS0` | `OK` |
| `ATSP0` | `OK` |

Do not expect `0100` or any OBD query to succeed without an ECU or simulator.

## 17. Phase 12 - J2534 PC Registration Check

Windows only. This checks PC integration, not bus traffic.

1. Build or install the OmniBox DLLs.
2. Edit `host/dll/j2534_register.reg` so `FunctionLibrary` points to the actual DLL path.
3. Import the registry file as Administrator.
4. Run:

```powershell
reg query "HKLM\SOFTWARE\PassThruSupport.04.04\OmniBox"
reg query "HKLM\SOFTWARE\WOW6432Node\PassThruSupport.04.04\OmniBox"
```

Expected:

- OmniBox is listed in both 64-bit and 32-bit PassThru locations;
- the registry does not advertise experimental protocols as stable.

## 18. Acceptance Sheet

Record each item as `PASS`, `FAIL` or `DEFERRED`.

| Item | Required result |
|---|---|
| Visual inspection | PASS |
| Unpowered rail checks | PASS |
| D_PWR1 `SM8S24A` diode orientation | PASS |
| D_PWR2 diode orientation | PASS |
| `GND_H` to `GND_V` isolation | PASS |
| Default CAN1 termination | PASS |
| Default FEPS/GPT open state | PASS |
| USB enumeration | PASS |
| USB-only `3V3_H` | PASS |
| USB-only `5V_V` and `3V3_V` | PASS |
| USB does not back-power `VBAT_PROT` | PASS |
| 12 V current at 200 mA limit | PASS |
| `VBAT_PROT`, `V5_BUCK`, `5V_V`, `3V3_V` from 12 V | PASS |
| `VFEPS` safe-off from 12 V | PASS |
| CAN1 and CAN5 recessive static levels | PASS |
| K-line 1 and K-line 3 recessive with firmware active | PASS |
| ELM327 AT command smoke test | PASS |
| J2534 Windows registration | PASS or DEFERRED if no Windows PC |
| Real CAN/CAN-FD traffic | DEFERRED |
| Real ISO9141/KWP traffic | DEFERRED |
| Real J1850/SWCAN traffic | DEFERRED |
| FEPS under load | DEFERRED |

The board passes this hardware protocol only when every non-deferred item is `PASS`.

## 19. Failure Rules

- If any low-voltage rail is below 1 kOhm to ground, do not power the board.
- If `GND_H` to `GND_V` is below 1 MOhm, do not power the board.
- If D_PWR1 diode polarity is wrong, do not apply 12 V.
- If 12 V current stays at the current limit, switch off immediately.
- If `VFEPS` rises without an explicit firmware command, switch off immediately.
- If a K-line remains dominant with firmware active, do not connect to an ECU.
- If CANH/CANL differential voltage is not near 0 V at idle, do not connect to a CAN bus.
