# Configuration

Every field of `alicat_basis2::Config`, how a configuration is checked, and the optional Kconfig layer. A `Config`
describes the instrument and the line; it is stored by the `Basis2` constructor with no I/O.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    - Full scale flow, serial number, firmware: Alicat Scientific, Inc. DOC-MANUAL-BASIS2 Rev 4, Full Scale Flow; Unit ID; Version Number, [p. 16](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=16)
    - Supply current, accuracy and update rate: Alicat Scientific, Inc. DOC-SPECS-BASIS Rev 19, Technical Data, [p. 1](https://documents.alicat.com/specifications/DOC-SPECS-BASIS.pdf#page=1)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Fields

`Config{}` holds the instrument's factory settings (unit ID `A`, 38400 baud, ASCII), so it works on an instrument
nobody has changed. Fields with a unit carry it in the name. Fields marked "assumption" cover something the manual
does not state; each is a knob, so that one bench capture can settle it ([Datasheet map](datasheet.md)).

| Field | Default | Range | What it does |
| --- | --- | --- | --- |
| `unit_id` | `'A'` | `'A'` to `'Z'` | ASCII unit ID the object addresses |
| `protocol` | `Protocol::Ascii` | `Ascii`, `ModbusRtu` | The protocol this object speaks |
| `modbus_address` | 1 | 1 to 247 | Modbus address; address 0 (broadcast) is never sent |
| `baud` | 38400 | 4800, 9600, 19200, 38400, 57600, 115200 | What the port runs at; it sizes every timeout |
| `kind` | `DeviceKind::Auto` | `Auto`, `Meter`, `Controller` | Learned at `Begin()` over ASCII; over Modbus no register tells, so `Auto` means `Controller`: set it for a meter |
| `processing_ms` | 50 | 1 to 1000 | Instrument turnaround added to every reply wait |
| `slow_command_ms` | 1000 | 1 to 10000 | Reply wait of commands that may write non-volatile memory (assumption) |
| `restore_holdoff_ms` | 3000 | | Quiet time after `FACTORY RESTORE` |
| `read_retries` | 1 | 0 to 3 | Retries of a failed **read**; writes and guarded commands are never repeated |
| `frame_layout` | `Auto` | `Auto`, `Meter`, `Controller` | The data frame of a meter is not in the manual (assumption) |
| `unknown_flags` | `Report` | `Report`, `Reject` | A status flag the manual does not list |
| `echo` | `None` | `None`, `Skip` | `Skip` drops a first line equal to the command, for wiring that hears itself |
| `flow_margin_permille` | 1100 | | Plausibility: the flow must not exceed full scale times this over 1000 |
| `temperature_min_c`, `temperature_max_c` | -50, 150 | min below max | Plausibility limits of the temperature |
| `setpoint_decimals` | 3 | 0 to 6 | Digits sent after the point in a setpoint |
| `verify_setpoint` | `WhenNoRamp` | `Off`, `WhenNoRamp` | Read a setpoint back (not while a ramp is active) |
| `verify_tolerance_permille` | 5 | | Read-back tolerance in permille of full scale |
| `saved_setpoint_min_interval_ms` | 180000 | | Minimum time between setpoints while the source is saved |
| `firmware_gate` | `Enforce` | `Enforce`, `Ignore` | Refuse calls the firmware is too old for |
| `assume_firmware` | 0 | `256a + 16b + c` | Used when the instrument reports no firmware |
| `modbus_flow_decimals` | **unset** | 0 to 6 | **Required for Modbus**; see below |
| `full_scale_override_milli` | 0 | | 0 learns the full scale at `Begin()`; otherwise flow units times 1000 |

Start from `Config{}` and change fields:

```cpp
--8<-- "tests/docs/configuration.cpp:custom"
```

## The Modbus flow decimals

Over Modbus the flow is a signed 16-bit integer whose decimal point is factory-set and not readable. The driver
therefore has no default: `Begin()` fails with `InvalidArgument` in Modbus mode until `modbus_flow_decimals` is set,
because a silent default would be a flow that is off by a factor of ten. Read the value from the instrument's
calibration sheet, or take it from an ASCII reading (`ReadFrame()` over ASCII shows the printed decimals). The flow
must fit a signed 16-bit integer, so the full scale times 10 to the power of the decimals stays below 32768: 1
decimal covers a full scale up to 3276.7.

## Validation

`alicat_basis2::Validate(config)` checks a configuration without touching the instrument, and `EnsureInitialized()`
calls it before any I/O. It returns `Error::InvalidArgument` when the unit ID or the Modbus address is out of range,
the baud rate is not one of the six, a decimal count is above 6, the temperature limits are not ordered, or an
enumeration holds a value that is not an enumerator.

## Kconfig

Under ESP-IDF, `idf.py menuconfig` shows **BASIS 2 mass flow meter** when the driver is a component of the project
([Installation](installation.md)); the menu is [`_config/Kconfig`](../_config/Kconfig). The options reach the code only
through [`inc/alicat_basis2_kconfig.hpp`](../inc/alicat_basis2_kconfig.hpp), never through a driver signature:

```cpp
--8<-- "tests/docs/configuration.cpp:kconfig"
```

| Option | Default | `Config` field |
| --- | --- | --- |
| `CONFIG_ALICAT_BASIS2_MODBUS` | n | `protocol` |
| `CONFIG_ALICAT_BASIS2_BAUD_*` | 38400 | `baud` |
| `CONFIG_ALICAT_BASIS2_PROCESSING_MS` | 50 | `processing_ms` |
| `CONFIG_ALICAT_BASIS2_SLOW_COMMAND_MS` | 1000 | `slow_command_ms` |
| `CONFIG_ALICAT_BASIS2_READ_RETRIES` | 1 | `read_retries` |

Without Kconfig (plain CMake, other platforms) `KconfigDefaults()` returns `Config{}`. Both cases are host tests.
The Modbus flow decimals have no Kconfig option on purpose: set them in code.

Next: [Porting](porting.md).
