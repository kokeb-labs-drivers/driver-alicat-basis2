# Datasheet map

The driver follows the manual of the BASIS 2 meters and controllers, with the specification sheets for electrical and
mechanical facts. This page says which revision, where to get it, and where each part of the driver is described in
it, so you can check any behaviour against the source. Section names and page numbers are the documents' own (the
printed page numbers, which equal the PDF's).

<!-- generated: datasheet-map -->
- **Datasheet manual: Operating Manual for Mass Flow Instruments, BASIS 2 Meters and Controllers**, Alicat Scientific,
  Inc. DOC-MANUAL-BASIS2, revision Rev 4 (2025-05-23). Get it from the [vendor page](https://www.alicat.com/basisoem) or
  as a [PDF](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf).
  SHA-256 of the file used: `388d686bd1a943e48d62ae13582a2114afd25d874600aeecdee33333f1a287d8`.
- **Datasheet specification sheet: Technical Data for BASIS 2.0 Mass Flow Meters and Controllers**, Alicat Scientific,
  Inc. DOC-SPECS-BASIS, revision Rev 19 (2026-08-17). Get it from the [vendor page](https://www.alicat.com/basisoem) or as
  a [PDF](https://documents.alicat.com/specifications/DOC-SPECS-BASIS.pdf).
  SHA-256 of the file used: `f3f11b4c949caf92b6952b8a6f5cc0b9b2ca4d6a325d4ea3b878c6db51d9a507`.
- **Datasheet specification sheet: Technical Data for BASIS 2 (pages 1 to 3 of the combined manual and specification
  file)**, Alicat Scientific, Inc. MANUAL+SPECS_BASIS2, revision Rev 16 (2024-03-01). Get it from the [vendor
  page](https://www.alicat.com/basisoem) or as a
  [PDF](https://documents.alicat.com/specifications/MANUAL+SPECS_BASIS2.pdf).
  SHA-256 of the file used: `f8ec4537221306cac68f851ef9a4dfb6886a75f6c8e6dcb6eef648e306c4c988`.
- **Datasheet manual: Alicat Modbus Manual (generic, written for the classic registers)**, Alicat Scientific, Inc.
  DOC-MANUAL-MODBUS, revision Rev 7 (2024-11-01). Get it from the [vendor page](https://www.alicat.com/basisoem) or as a
  [PDF](https://documents.alicat.com/manuals/DOC-MANUAL-MODBUS.pdf).
  SHA-256 of the file used: `4363886366ccd572c40fa47fdcc3cf9f98e6a397c7cfee720dfe9bdb2049de28`.
- **Datasheet application note: Alicat Serial Communications Primer (classic line, firmware 4v to 10v)**, Alicat
  Scientific, Inc. Alicat-Serial-Primer, revision Rev 2 (2023-02-01). Get it from the [vendor
  page](https://www.alicat.com/basisoem) or as a [PDF](https://documents.alicat.com/Alicat-Serial-Primer.pdf).
  SHA-256 of the file used: `e80684d7529eb62ce0725829207c567b35480d9efe655e7fb0fef2d978dc5e34`.

| Topic | Datasheet section | Page | In the driver | Explained on |
| --- | --- | --- | --- | --- |
| Models, ranges and options | DOC-MANUAL-BASIS2: Introduction | [p. 3](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=3) | `DeviceKind`, `Config::kind` | [BASIS 2 Driver](index.md), [Hardware](hardware.md), [Quickstart](quickstart.md) |
| Mounting, LED and power-up behaviour | DOC-MANUAL-BASIS2: Getting Started, Mounting | [p. 5](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=5) | - | [Hardware](hardware.md), [Safety notes](safety.md) |
| Power and signal connections, pin 4 only | DOC-MANUAL-BASIS2: Power and Signal Connections | [p. 6](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=6) | - | [Hardware](hardware.md), [Safety notes](safety.md) |
| Connector pins and serial levels | DOC-MANUAL-BASIS2: Power and Signal Connections (pin table) | [p. 7](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=7) | `UartPort::Write`, `UartPort::Read` | [Hardware](hardware.md), [Porting](porting.md), [Quickstart](quickstart.md) |
| Default serial settings and the link layer | DOC-MANUAL-BASIS2: Alicat ASCII Serial Communication, Establishing Communication | [p. 10](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=10) | `Bus`, `Config::baud`, `Config::unit_id`, `ascii::Make` | [Architecture](architecture.md), [Protocols](protocols.md), [Contract](contract.md) |
| Data frame | DOC-MANUAL-BASIS2: Data Frame Format | [p. 10](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=10) | `Frame`, `Decimal`, `StatusFlags`, `Basis2::ReadFrame()` | [Protocols](protocols.md), [API guide](api-guide.md) |
| Query specific data | DOC-MANUAL-BASIS2: Query Specific Data | [p. 11](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=11) | `Basis2::ReadData()`, `ReadFlow()`, `ReadTemperature()`, `ReadTotal()`, `ReadValveDrive()` | [API guide](api-guide.md), [Protocols](protocols.md) |
| Setpoint, setpoint source and ramp | DOC-MANUAL-BASIS2: Setpoint; Setpoint Source; Maximum Setpoint Ramp Rate | [p. 11](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=11) | `SetSetpoint()`, `SetSetpointSource()`, `SetRamp()`, `Config::saved_setpoint_min_interval_ms` | [API guide](api-guide.md), [Safety notes](safety.md) |
| Communication watchdog | DOC-MANUAL-BASIS2: Communication Watchdog | [p. 12](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=12) | `SetWatchdog()`, `WatchdogProtects()` | [Safety notes](safety.md), [API guide](api-guide.md) |
| Hold valve, gains, reference temperature, averaging | DOC-MANUAL-BASIS2: Hold Valve Position; Loop Control Gains; Reference Temperature; Readings Averaging | [p. 13](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=13) | `HoldValve()`, `SetGains()`, `SetReferenceTemperature()`, `SetAveraging()` | [API guide](api-guide.md), [Safety notes](safety.md) |
| Measurement triggering, averages and ranges | DOC-MANUAL-BASIS2: Configure Measurement Triggering; Start a Measurement; Measurement Averages and Ranges | [p. 13](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=13) | `SetMeasurementTrigger()`, `StartMeasurement()`, `ReadAverages()`, `ReadRanges()` | [API guide](api-guide.md) |
| Tare and autotare | DOC-MANUAL-BASIS2: Taring the Instrument; Using Ground to Tare; Autotare | [p. 14](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=14) | `Tare()`, `TareDefault()`, `SetAutotare()` | [API guide](api-guide.md), [Safety notes](safety.md), [Hardware](hardware.md) |
| Active gas | DOC-MANUAL-BASIS2: Active Gas | [p. 15](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=15) | `SetGas()`, `ReadGasList()`, `GasId`, `GasName()` | [API guide](api-guide.md), [Protocols](protocols.md) |
| Totalizer and batch | DOC-MANUAL-BASIS2: Totalizer Reset; Totalizer Batch; Totalizer Configuration | [p. 15](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=15) | `ResetTotal()`, `SetBatch()`, `DisableBatch()`, `SetTotalizerLimit()` | [API guide](api-guide.md) |
| Full scale flow, serial number, firmware | DOC-MANUAL-BASIS2: Full Scale Flow; Unit ID; Version Number | [p. 16](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=16) | `ReadFullScale()`, `ReadSerialNumber()`, `ReadFirmware()`, `Identity` | [API guide](api-guide.md), [Configuration](configuration.md) |
| Unit ID | DOC-MANUAL-BASIS2: Unit ID | [p. 16](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=16) | `ChangeUnitId()`, `FactoryRestore()` | [Safety notes](safety.md), [API guide](api-guide.md), [Troubleshooting](troubleshooting.md) |
| Modbus address, baud rate, command protocol | DOC-MANUAL-BASIS2: Modbus Address; Serial Baud Rate; Command Protocol | [p. 17](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=17) | `SetModbusAddress()`, `ChangeBaud()`, `SwitchToProtocol1()`, `FactoryRestore()` | [Safety notes](safety.md), [API guide](api-guide.md), [Troubleshooting](troubleshooting.md) |
| Modbus RTU framing, address and baud | DOC-MANUAL-BASIS2: Modbus-RTU; Communicating Via Modbus-RTU; Changing the Baud Rate | [p. 18](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=18) | `modbus::Build*()`, `modbus::ParseResponse()`, `Config::modbus_address` | [Protocols](protocols.md), [Architecture](architecture.md) |
| Modbus setpoint, watchdog and tare | DOC-MANUAL-BASIS2: Setpoint Type; Changing the Setpoint; Communication Watchdog; Taring the Instrument | [p. 19](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=19) | `Config::protocol`, `Basis2::SetSetpoint()`, `Tare()` | [Protocols](protocols.md) |
| Modbus averaging, totalizer, batch, gas table | DOC-MANUAL-BASIS2: Flow Averaging; Totalizer; Batch Dispensing; Changing the Gas | [p. 20](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=20) | `Basis2::SetAveraging()`, `ResetTotal()`, `SetBatch()`, `SetGas()` | [Protocols](protocols.md) |
| Modbus register tables | DOC-MANUAL-BASIS2: User, Instantaneous Data, Measurement Data and Configuration and Function Registers | [p. 21](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=21) | `inc/alicat_basis2_registers.hpp`, `reg::kRegisterRows` | [Protocols](protocols.md), [Datasheet map](datasheet.md) |
| Analog signals | DOC-MANUAL-BASIS2: Analog Signals | [p. 28](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=28) | - | [Hardware](hardware.md), [Safety notes](safety.md) |
| Troubleshooting | DOC-MANUAL-BASIS2: Troubleshooting | [p. 29](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=29) | `Error` | [Troubleshooting](troubleshooting.md) |
| Engineering units | DOC-MANUAL-BASIS2: Reference, Engineering Units | [p. 31](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=31) | `FlowUnit`, `TimeUnit`, `Identity::unit_label` | [API guide](api-guide.md), [Protocols](protocols.md) |
| Supply current, accuracy and update rate | DOC-SPECS-BASIS: Technical Data | [p. 1](https://documents.alicat.com/specifications/DOC-SPECS-BASIS.pdf#page=1) | `Config::flow_margin_permille`, `Config::processing_ms` | [Hardware](hardware.md), [Configuration](configuration.md) |
| Size classes and dimensions | DOC-SPECS-BASIS: Dimensions | [p. 3](https://documents.alicat.com/specifications/DOC-SPECS-BASIS.pdf#page=3) | - | [Hardware](hardware.md) |
<!-- /generated -->

The vendor URL of the manual served a file with other bytes (SHA-256
`0f6b51bd13b98c2b4387b55a4c58aa19780db43371dbbb2d59123837e0b53c2e`) on the day it was fetched; its text equals the
recorded file after whitespace normalisation, so both are Rev 4. The 35-page combined file `MANUAL+SPECS_BASIS2.pdf`
embeds an older manual (Rev 3) whose command and register tables are the same; it differs in a few sentences (no
"ground pin 1 to tare" for meters, `FACTORY RESTORE` "in ALL CAPITAL LETTERS with exactly one space").

## Which document is used for what

| Document | Used for |
| --- | --- |
| Operating manual Rev 4 | Every command, register, pin, LED and warning: the only source of the protocols |
| Technical data Rev 19 and Rev 16 | Supply current, accuracy, update rate, size classes and weights |
| Modbus manual Rev 7 and serial primer Rev 2 | Generic facts of the vendor's **classic** line only (big-endian registers, the `?` error token, streaming): each is an assumption for BASIS 2 and is marked below |

## Assumptions and open items

Everything the documents do not state is a knob of the test model and, where the driver has a choice, a `Config`
field. Each assumption stays open until one capture from an instrument settles it (**not verified on hardware**).

| ID | Doubt | What the driver does meanwhile |
| --- | --- | --- |
| A1 | A baud change answers at the new rate; port support for switching is not universal | `ChangeBaud()` is `Unsupported` without `SetBaud()` |
| A2 | The frame of a **meter** and whether flags carry brackets are not shown (only a controller example) | `FrameLayout::Auto` accepts three or five numbers, bracketed or bare flags |
| A3 | Replies to setters and the error reply are not documented | The reply is accepted as a frame, a read form or short text; `?` is `Rejected`; only `SetSetpoint()`, `SetGas()` and the unit ID change are checked (see below) |
| A4 | The reply of `DV n` with several items is not shown | Strict token counts per item mask; status as flags or as an integer |
| A5 | The formats of `GS *`, `VE` and `SN` are not shown | Own parsers; an unparsable gas list is kept raw |
| A6 | Time commands write non-volatile memory; their latency is unknown | `Config::slow_command_ms` |
| A7 | When `V d` replies (at once or after d), and the Modbus samples-to-time mapping | The bus is held for the duration after the send |
| A8 | The watchdog protects only Modbus-set setpoints | `WatchdogProtects()`; do not rely on it with ASCII |
| A9 | The ramp rate unit ("the default units of the loop control variable") is undefined | ASCII passes the number and unit code through; Modbus converts with the documented formula |
| A10 | The reference temperature range for ASCII is not stated | 0 to 30.00 C on both protocols |
| A11 | Modbus flow decimals are factory-set and not readable | `Config::modbus_flow_decimals` is required; the setpoint and total widths follow |
| A12 | The broadcast `*` and the reply ID after `@=` | Broadcast is never sent; either ID is accepted for the `@=` reply, then verified |
| A13 | Modbus "pin 6" wording, address 0, the factory address, parallel ASCII and Modbus | One protocol per object; address 1 by default; broadcast never sent |
| A14 | BASIS Protocol 1 is unknown (its manual could not be obtained) | `P 1` is guarded; the driver is then abandoned |
| A15 | The two specification sheets differ in a few dimensions | Revision 19 is used; the differences are listed on the [Hardware](hardware.md) page |
| A16 | Standard temperature references differ between the sheets (0 C against 21 C) | The A10 range |
| A17 | A batch reset by register 53 duplicates the totalizer reset; reading register 4214 may start a measurement | `ResetTotal()` only; a warning on `ReadPreviousMeasurement()` |
| A18 | Whether `DVAS`, `DVAA`, `DVAR` exist on meters | Sent on both; a `?` is `Rejected` |
| A19 | Streaming mode exists on the classic line but is not in the manual | Never sent; stray lines counted and ignored |
| A20 | "Open", "close" and "exhaust" valve actions are buttons in the vendor's software, not commands | `OpenValve()` holds 100 percent, `CloseValve()` holds 0 |
| A21 | The setpoint resolution over ASCII | `Config::setpoint_decimals` |
| A22 | Whether the temperature and flow limits differ from the instrument's own | Plausibility limits in `Config` |
| A23 | Whether the 50 and 100 SLPM models are still BASIS 2 | Data only |
| A24 | The factory Modbus address is 1: inferred from the default unit ID, not stated in the manual | A switch-off in doubt after `FactoryRestore()` tries address 1; `Recover()` scans all addresses (model knob K-FACTORYADDR) |
| A25 | The word order of the previous average flow, 4214-4215: M p.22 calls 4214 "the least significant register", which can mean the lower-numbered register or the low word; every other 4-byte value is high word first | Decoded high word in 4214, like the other pairs; the literal reading favours low word first, so a bench read decides ([Testing](testing.md#on-hardware), model knob K-PREVAVG) |

### Searched, not obtained

| Item | Result |
| --- | --- |
| The BASIS 1 ASCII manual | Not served; needed only to know what `P 1` switches to |
| A BASIS 2 specific Modbus manual | None exists; the manual's own register tables are the source |
| Firmware release notes; the reply formats of the setters, `DV`, `GS *`, `VE`, `SN` | Not published in any document read |
| The vendor's LabVIEW library and desktop software | Not downloaded: binary archives, not documents; they would show the real reply formats |

## Not in the driver

- The analog setpoint and output are wired signals, not serial commands.
- `Basis2` speaks one protocol per object; it does not mix ASCII and Modbus on one line.
- The vendor's BASIS Protocol 1 and the classic line's streaming commands.
