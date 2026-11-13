# API guide

The API by task. Every call returns `Result<T>` (a value and an `Error`) or a bare `Error`; every call is `noexcept`.
The code blocks are compiled by the host build. For every class and member see the [API reference](api-reference.md).

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Alicat Scientific, Inc. DOC-MANUAL-BASIS2 Rev 4:

    - Data frame: Data Frame Format, [p. 10](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=10)
    - Query specific data: Query Specific Data, [p. 11](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=11)
    - Setpoint, setpoint source and ramp: Setpoint; Setpoint Source; Maximum Setpoint Ramp Rate, [p. 11](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=11)
    - Communication watchdog: Communication Watchdog, [p. 12](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=12)
    - Hold valve, gains, reference temperature, averaging: Hold Valve Position; Loop Control Gains; Reference Temperature; Readings Averaging, [p. 13](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=13)
    - Measurement triggering, averages and ranges: Configure Measurement Triggering; Start a Measurement; Measurement Averages and Ranges, [p. 13](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=13)
    - Tare and autotare: Taring the Instrument; Using Ground to Tare; Autotare, [p. 14](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=14)
    - Active gas: Active Gas, [p. 15](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=15)
    - Totalizer and batch: Totalizer Reset; Totalizer Batch; Totalizer Configuration, [p. 15](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=15)
    - Full scale flow, serial number, firmware: Full Scale Flow; Unit ID; Version Number, [p. 16](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=16)
    - Unit ID: Unit ID, [p. 16](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=16)
    - Modbus address, baud rate, command protocol: Modbus Address; Serial Baud Rate; Command Protocol, [p. 17](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=17)
    - Engineering units: Reference, Engineering Units, [p. 31](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=31)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Objects

A `Bus<Port>` owns one serial line and runs one transaction at a time. A `Basis2<Port>` is one instrument on a bus,
described by a [`Config`](configuration.md). Neither does any I/O when constructed.

```cpp
alicat_basis2::Bus<MyPort> bus(port);
alicat_basis2::Basis2<MyPort> meter(bus);   // unit ID 'A', 38400 baud, ASCII
```

Several `Basis2` objects with different unit IDs (or Modbus addresses) can share one bus. Call
`bus.SetDeviceCount(n)` when more than one instrument is connected, so that a unit ID change is refused.

## Start up

`EnsureInitialized()` brings the port up (nothing is sent). `Begin()` learns the instrument and is the first call that
sends. Both can be repeated; after a failure they start from the beginning, and once `Begin()` has succeeded every
other call may be used.

```cpp
--8<-- "tests/docs/api_tasks.cpp:lifecycle"
```

`Begin()` reads the serial number, firmware, full scale and its unit, the gas, the setpoint source and the ramp
(read commands only), and checks the first data frame. After it, `GetIdentity()` returns an `Identity`: the kind
(meter or controller), firmware, full scale, flow unit and label, gas and source. `IsReady()` is false while the
state is unknown, and `IsStateUnknown()` tells that case apart.

## Read

| Call | Returns |
| --- | --- |
| `ReadFrame()` | The whole frame: flow, temperature, total, for a controller setpoint and valve drive, gas and flags |
| `ReadFlow()`, `ReadTemperature()`, `ReadTotal()` | One `Decimal` |
| `ReadValveDrive()`, `ReadSetpointValue()`, `ReadBatchRemaining()` | One `Decimal` (controllers) |
| `ReadStatus()` | `StatusFlags`: `mass_flow_over`, `temperature_over`, `totalizer_over`, `valve_hold`, `valve_thermal` |
| `ReadData(items)` | A frame with only the items you ask for (a `DataItem` mask) |

A `Decimal` is an integer mantissa and a count of decimals, so nothing is lost to floating point; `ToDouble()` and
`ToMilli()` convert. Values are in the instrument's own flow unit, which `GetIdentity().unit_label` prints. A flow
beyond `Config::flow_margin_permille` of the full scale, or a temperature outside the plausibility limits, is
`OutOfRange` and no value is returned.

Reads are retried up to `Config::read_retries` times after a failed transaction; nothing that changes the instrument is
ever retried.

## Set a setpoint

```cpp
--8<-- "tests/docs/api_tasks.cpp:setpoint"
```

The setpoint is checked against 0 to 2.5 % over full scale before anything is sent, and read back unless a ramp is
active. A meter returns `Unsupported`. While the source is saved a change sooner than three minutes after the last
is `RateLimited`. Read [Safety notes](safety.md) before you move a valve.

## Other tasks

| Task | Calls |
| --- | --- |
| Hold or release the valve | `HoldValve()`, `OpenValve()`, `CloseValve()`, `ResumeControl()` (ASCII only) |
| Ramp | `SetRamp(rate, unit)`, `DisableRamp()`, `ReadRamp()` |
| Tare | `Tare(duration_ms)`, `TareDefault()`, `SetAutotare()` |
| Gas | `SetGas()`, `ReadGas()`, `ReadGasList()` (`GasName()` names the nine gases) |
| Totalizer and batch | `ResetTotal()`, `SetTotalizerLimit()`, `SetBatch()`, `DisableBatch()` |
| Reference temperature, averaging | `SetReferenceTemperature()`, `SetAveraging()` |
| Measurement window | `SetMeasurementTrigger()`, `StartMeasurement()`, `ReadAverages()`, `ReadRanges()` |
| Identify | `ReadSerialNumber()`, `ReadFirmware()`, `ReadFullScale()` |
| Scan | `ScanUnitIds()` (ASCII), `ScanModbusAddresses()` (Modbus) |
| Raw access, for diagnostics | `ReadRegisterRaw()` (Modbus), `ReadRawLine()` (ASCII), `GetLastReply()` (no I/O) |

The raw reads send one request and never retry; a reply is still accepted only after its CRC, address and length (or
unit ID) check. `GetLastReply()` returns the last reply any call accepted, without touching the bus.

`ScanUnitIds()` and `ScanModbusAddresses()` **block** for the whole scan: 26 polls, each up to a reply wait, or one
read per address. Run them from a task that may wait, and not from a loop with a deadline.

## Guarded commands

```cpp
--8<-- "tests/docs/api_tasks.cpp:guarded"
```

The six guarded commands are listed with their tokens in [Safety notes](safety.md#guarded-commands).

## Recover

```cpp
--8<-- "tests/docs/api_tasks.cpp:recover"
```

`Recover()` finds the instrument again after a guarded command that was not confirmed or an unexplained failure: it
polls the configured ID or address and, when the bus declares one instrument, scans the unit IDs over ASCII or the
Modbus addresses over Modbus. It then relearns the identity and compares the serial number, firmware and kind with the
earlier ones: another instrument is `VerifyMismatch`. The state stays unknown (`IsStateUnknown()`) until all of that
succeeds.

## Handle errors

`Error` values 0 to 9 are the base set shared by every driver of the organisation; values from 16 are this driver's.

```cpp
--8<-- "tests/docs/api_tasks.cpp:errors"
```

| `Error` | Meaning | What to do |
| --- | --- | --- |
| `Bus` | `Write()` returned false | Check your port |
| `Timeout` | No complete reply in time | Wiring, A and B, baud rate, unit ID, direction pin: [Troubleshooting](troubleshooting.md#no-reply) |
| `DeviceIdMismatch` | Another unit answered | Two instruments with one ID |
| `Crc` | Modbus CRC-16 mismatch | Line noise, termination |
| `BadReply`, `Overflow` | The line does not match the grammar, or has no end | A wrong protocol, a wrong baud rate, noise |
| `Rejected` | The instrument answered `?` or a Modbus exception | The command is not valid in this state or on this model |
| `OutOfRange` | A decoded value is impossible | Check the unit and the decimals |
| `VerifyMismatch` | The read-back differs from what was set | A ramp, a clamp, or an analog source |
| `FirmwareTooOld` | The call needs a newer firmware | Update or `FirmwareGate::Ignore` |
| `RateLimited` | A saved setpoint came too soon | Use the unsaved source |
| `StateUnknown`, `GuardNotConfirmed` | A guarded command is not settled, or the token is wrong | `Recover()` |
| `Busy` | The bus was not free, or the line never went quiet: the transaction that met it was not sent. A writing call returns it only when its write was not sent | Wait, retry; find the peer that talks |
| `Unsupported` | Not on this protocol, this kind, or this port | Check the table above |

## Not blocking

`Bus` can also be driven without blocking: `Submit()` a request, call `Poll()` from your loop and `Take()` the result.
`Basis2` itself calls `Execute()`, which waits. Take every result before the next `Basis2` call: `Execute()` drops a
result nobody took (its transaction is over) and waits at most as long as the transaction on the bus can still take.
See [Porting](porting.md#non-blocking-use).

Next: [Configuration](configuration.md).
