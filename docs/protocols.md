# Protocols

The instrument speaks two protocols on the same line settings (38400 baud, 8N1 by default). The driver implements
both completely behind one API: `Config::protocol` selects one for each `Basis2` object, and the calls do not change.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Alicat Scientific, Inc. DOC-MANUAL-BASIS2 Rev 4:

    - Default serial settings and the link layer: Alicat ASCII Serial Communication, Establishing Communication, [p. 10](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=10)
    - Data frame: Data Frame Format, [p. 10](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=10)
    - Query specific data: Query Specific Data, [p. 11](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=11)
    - Active gas: Active Gas, [p. 15](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=15)
    - Modbus RTU framing, address and baud: Modbus-RTU; Communicating Via Modbus-RTU; Changing the Baud Rate, [p. 18](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=18)
    - Modbus setpoint, watchdog and tare: Setpoint Type; Changing the Setpoint; Communication Watchdog; Taring the Instrument, [p. 19](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=19)
    - Modbus averaging, totalizer, batch, gas table: Flow Averaging; Totalizer; Batch Dispensing; Changing the Gas, [p. 20](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=20)
    - Modbus register tables: User, Instantaneous Data, Measurement Data and Configuration and Function Registers, [p. 21](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=21)
    - Engineering units: Reference, Engineering Units, [p. 31](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=31)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

| | ASCII "BASIS Protocol 2" (default) | Modbus RTU |
| --- | --- | --- |
| Addressing | Unit ID `A` to `Z`, up to 26 instruments per line | Address 1 to 247 |
| Framing | One text line ending in a carriage return, in both directions | Binary frames with a CRC-16, separated by 3.5 characters of silence |
| Units and decimals | Printed in the text; the flow unit is learned from the `FPF` reply | Scaled integers; the flow decimals are **not readable** (`Config::modbus_flow_decimals`) |
| Gas names | Printed in the frame; the gas table is a text list | Gas numbers; names only from a gas table in firmware 3.0.5 or later |
| Functions | The 46 commands of the manual | 3 (read), 6 (write one), 16 (write several) |
| Best for | A PC or MCU on a point to point link, text logs, bring-up | A PLC style RS-485 network; a few registers exist only here |

The instrument keeps no memory of which protocol a command came from, and the manual does not say whether both can
be used at once on one line (the vendor's classic line says they run in parallel). The driver uses one protocol per
object and never mixes them.

## ASCII

Every command starts with the unit ID, ends with a carriage return and gets one line in reply. The driver sends upper
case only, and no command is started before the previous reply has ended and the line has been silent for 3.5
character times. The default `Config` addresses unit `A`.

### The data frame

A poll (`A`) returns the whole frame: unit ID, then temperature, flow, total, and for a controller setpoint and valve
drive, then the gas name and zero or more status flags. The parser splits on runs of spaces, accepts bracketed or bare
flags, learns the layout (three or five numbers) from the first valid frame and rejects a later frame of the other
layout. The manual shows only a controller's frame; the meter layout is an assumption.

| Flag | Meaning | In `StatusFlags` |
| --- | --- | --- |
| `MOV` | Mass flow over range | `mass_flow_over` |
| `TOV` | Temperature over range | `temperature_over` |
| `OVR` | Totalizer over range | `totalizer_over` |
| `HLD` | Valve held by the user (an override, not a fault) | `valve_hold` |
| `VTM` | Valve thermal management (an override, not a fault) | `valve_thermal` |

### Commands and the API

| Command | Does | API |
| --- | --- | --- |
| `a` | Poll the full frame | `ReadFrame()` |
| `a DV n` | Read selected data items | `ReadData()`, `ReadFlow()`, `ReadTemperature()`, `ReadTotal()`, `ReadValveDrive()`, `ReadSetpointValue()`, `ReadBatchRemaining()`, `ReadStatus()` |
| `a S f` | Set the setpoint | `SetSetpoint()` |
| `a LSS`, `a LSS x` | Read or set the setpoint source (`a`, `s`, `u`) | `ReadSetpointSource()`, `SetSetpointSource()` |
| `a SR`, `a SR r t`, `a SR 0` | Read, set or disable the ramp | `ReadRamp()`, `SetRamp()`, `DisableRamp()` |
| `a WD`, `a WD t` | Read or set the watchdog | `ReadWatchdog()`, `SetWatchdog()` |
| `a HPUR p`, `a C` | Hold the valve, resume control | `HoldValve()`, `OpenValve()`, `CloseValve()`, `ResumeControl()` |
| `a LCG`, `a LCG p i` | Loop gains | `ReadGains()`, `SetGains()` |
| `a RT`, `a RT t` | Reference temperature | `ReadReferenceTemperature()`, `SetReferenceTemperature()` |
| `a DCA`, `a DCA t` | Flow averaging | `ReadAveraging()`, `SetAveraging()` |
| `a MT`, `a MT m` | Measurement trigger mode | `ReadMeasurementTrigger()`, `SetMeasurementTrigger()` |
| `a DVAS t`, `a DVAA`, `a DVAR` | Measurement window, averages, ranges | `StartMeasurement()`, `ReadAverages()`, `ReadRanges()` |
| `a V d` | Tare over d ms | `Tare()`, `TareDefault()` |
| `a ZCA`, `a ZCA b` | Autotare | `ReadAutotare()`, `SetAutotare()` |
| `a GS`, `a GS *`, `a GS n` | Read, list, set the gas | `ReadGas()`, `ReadGasList()`, `SetGas()` |
| `a T` | Reset the totalizer | `ResetTotal()` |
| `a TB v`, `a TB 0` | Batch volume | `SetBatch()`, `DisableBatch()` |
| `a TC`, `a TC m` | Totalizer limit mode | `ReadTotalizerLimit()`, `SetTotalizerLimit()` |
| `a FPF v` | Full scale values with their units | `ReadFullScale()` |
| `a SN`, `a VE` | Serial number, firmware | `ReadSerialNumber()`, `ReadFirmware()` |
| `a MA`, `a NCB`, `a P` | Read Modbus address, baud rate, protocol | `ReadModbusAddress()`, `ReadBaud()`, `ReadProtocol()` |
| `a @=b` | Change the unit ID | `ChangeUnitId()` (guarded) |
| `a MA n` | Set the Modbus address | `SetModbusAddress()` (guarded) |
| `a NCB b` | Set the baud rate | `ChangeBaud()` (guarded) |
| `a P 1`, `a P2` | Switch protocol | `SwitchToProtocol1()`, `SwitchToProtocol2()` (guarded) |
| `a FACTORY RESTORE` | Restore factory settings | `FactoryRestore()` (guarded) |

The manual documents the format of the data frame and of a few replies only. The replies to the setters, the error
reply, the `DV` reply with several items, the gas list, `VE` and `SN` are not shown. The driver accepts the
plausible forms and treats the `?` token as `Rejected`. Three setters are checked: `SetSetpoint()` reads the value back
(not while a ramp is active), `SetGas()` reads register 2100 back in Modbus mode and decodes the reply in ASCII mode
(an undecodable reply is an error), and `ChangeUnitId()` reads the new ID back. Every other setter reports only that the
instrument accepted it: no reply is `Timeout`, a `?` is `Rejected`, and a lost reply to a write is never repeated. Each
of these is an assumption recorded in the [Datasheet map](datasheet.md#assumptions-and-open-items) and
settled by one capture from an instrument.

### Timing

One character is ten bits. The reply wait is 3.5 character times, plus the line time of the longest reply of the
command, plus `processing_ms` (50 ms), or `slow_command_ms` for commands that may write non-volatile memory.

| Baud | 3.5 characters | Poll reply wait |
| --- | --- | --- |
| 4800 | 7.3 ms | 258 ms |
| 9600 | 3.7 ms | 154 ms |
| 19200 | 1.8 ms | 102 ms |
| 38400 | 0.9 ms | 76 ms |
| 57600 | 0.6 ms | 68 ms |
| 115200 | 0.3 ms | 60 ms |

These are derived from the character time, not measured.

## Modbus RTU

Frames carry the address, a function code, data and a CRC-16 (polynomial `0xA001`, low byte first); registers are
big-endian. The driver sends functions 3, 6 and 16 only, never to address 0 (broadcast) or above 247, never to a
read-only register, and with the two magic values only on their registers: `0xAA55` to tare or reset the totalizer
and `0x5214` to restore factory settings. A reply with a bad CRC, the wrong address or a function code with the high
bit set (an exception) is an error, and the frame is discarded as a whole.

| Quantity | Registers | Scale |
| --- | --- | --- |
| Flow | 2103 | Signed 16 bit, decimals from `Config::modbus_flow_decimals` |
| Total | 2104 to 2105 | Unsigned 32 bit, same decimals |
| Temperature | 2102 | Signed, divided by 100 |
| Valve drive | 2107 | Divided by 100 |
| Status | 2101 | Bits as the flags above |
| Setpoint | 2053 to 2054 (write), 2106 (current) | Signed 32 bit divided by 1000; the write completes when 2054 is written |
| Gas | 2100 | Gas number |
| Ramp | 524 to 525 | Percent of full scale per millisecond times 10^7 |
| Watchdog | 514 | Milliseconds, 0 disables |
| Setpoint source | 516 | 0 analog, 1 digital saved, 2 digital unsaved |
| Tare | 39 (write `0xAA55`), 51 (samples of about 2.5 ms) | |
| Firmware | 25 | `256a + 16b + c` |

The register table is `inc/alicat_basis2_registers.hpp`; each row carries the firmware that added it, and a call that
needs a newer firmware than the identity says returns `FirmwareTooOld` without sending (`Config::firmware_gate`).

### Differences you will see

- The flow, setpoint and total depend on the decimals you configured; a wrong value is off by a power of ten.
- The setpoint verify reads registers 2053 and 2054 back. Both they and 2106 report the ramped value while a ramp runs
  (M p.21), so the verify is skipped then (`VerifyPolicy::WhenNoRamp`).
- The ramp conversion from a rate and a time unit to percent per millisecond uses the manual's formula; the unit the
  ASCII command expects ("the default units of the loop control variable") is undefined in the manual, so the two
  protocols may differ for a ramp. Check a ramp on the bench.
- Reading register 4214 starts a measurement when trigger mode +4 is set; `ReadPreviousMeasurement()` warns in its
  documentation.

Next: [Safety notes](safety.md).
