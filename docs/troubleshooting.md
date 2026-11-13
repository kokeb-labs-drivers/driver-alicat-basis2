# Troubleshooting

Symptoms, causes and fixes, cheapest check first. Nothing here has been confirmed on an instrument; the causes are the
ones the manual and the line rules point to.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Alicat Scientific, Inc. DOC-MANUAL-BASIS2 Rev 4:

    - Unit ID: Unit ID, [p. 16](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=16)
    - Modbus address, baud rate, command protocol: Modbus Address; Serial Baud Rate; Command Protocol, [p. 17](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=17)
    - Troubleshooting: Troubleshooting, [p. 29](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=29)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## No reply

`Begin()` or any read returns `Timeout`: no complete line arrived within the reply wait (76 ms at 38400 baud).

| Check | Fix |
| --- | --- |
| The supply is on pin 4, 12 to 24 V, and the LED is green | Power the instrument; never connect power to another pin |
| TX and RX | Pin 5 is the instrument's receive (or RS-485 B), pin 6 its transmit (or A): cross them for RS-232 |
| RS-485 A and B | Swap the pair: a swapped pair gives garbage or silence, and the labels differ between vendors |
| RS-485 direction | The port must release the transmitter after the last byte has left the pin ([Porting](porting.md#rs-485)) |
| Baud rate | `Config::baud` must equal the instrument's (factory 38400); try the other five rates one at a time |
| Unit ID | `Config::unit_id` must equal the instrument's (factory `A`); `ScanUnitIds()` finds it |
| Protocol | The manual does not say whether ASCII and Modbus work at once on one line; try `Config::protocol` the other way |
| Levels | A TTL UART cannot talk to an RS-232 or RS-485 instrument without a level shifter or a transceiver |

## Garbage or `BadReply`

A line arrived but does not parse. The usual causes are a wrong baud rate (bytes that are not printable ASCII), a half
duplex wiring that hears its own transmission (set `Config::echo = EchoMode::Skip`), or an instrument speaking
BASIS Protocol 1 (the older protocol, which the driver does not speak; see below). A reply that is longer than the
256-byte buffer without a carriage return is `Overflow`: the baud rate is almost always wrong.

## `DeviceIdMismatch`

An instrument answered with another unit ID than the one addressed. Two instruments with one ID answer together and
corrupt each other's data (manual p. 16): connect them one at a time and give each its own ID, with
`ChangeUnitId()` on a line that has one instrument ([Safety notes](safety.md#guarded-commands)).

## `Crc` (Modbus)

Single corrupted frames point at noise or a missing termination. Every frame fails: a baud-rate or parity mismatch (the
driver assumes no parity), or ASCII wired to a Modbus instrument. A frame that fails the CRC is discarded whole and
retried (reads only).

## The flow is ten times wrong over Modbus

`Config::modbus_flow_decimals` does not match the instrument's factory decimals. Over ASCII the decimals are printed in
the frame; read one frame over ASCII, count the decimals and set the same number.

## `Rejected`

The instrument answered `?` or a Modbus exception. A controller-only command on a meter, a value outside the
instrument's range, or `P 1` with a flow unit that BASIS Protocol 1 does not have (only SCCM, SLPM, NCCM and NLPM
are allowed). The driver learns the kind at `Begin()` and returns `Unsupported` for a controller command on a meter
without sending.

## The setpoint is ignored

The source is `Analog`: a digital setpoint command is not accepted (`ReadSetpointSource()`; set it with
`SetSetpointSource(UnsavedDigital)`). Or a ramp limits the change (`VerifyMismatch` after a ramp is expected; the verify
is skipped while a ramp is active). Or the valve is held (`HLD` flag): call `ResumeControl()`.

## `RateLimited`

The source is `SavedDigital` and the last setpoint was less than three minutes ago. The saved source writes
non-volatile memory with every setpoint. Use `UnsavedDigital`.

## `VerifyMismatch`

The value read back differs from the one set by more than `Config::verify_tolerance_permille`. The instrument accepts a
setpoint up to 2.5 % over full scale and may round; a ramp or a clamp differs on purpose. Over Modbus, check the
decimals.

## `StateUnknown`

A guarded command may have changed the unit ID, the baud rate, the Modbus address or the protocol, and the driver could
not confirm it. Call `Recover()`: it polls the configured ID, scans the unit IDs (or addresses) when the bus has
one instrument, and relearns the instrument; a different instrument is `VerifyMismatch`. After a factory restore,
power-cycle first. If the baud rate changed, set the port's speed to the new rate before `Recover()`.

## Stray lines and a streaming instrument

`Bus::StrayLineCount()` counts lines that arrive while no transaction waits; `Bus::StreamingSuspected()` becomes true
when one begins with `@`. The driver never uses such a line and never sends a streaming command. Streaming mode is a
feature of the vendor's classic line that is not in the BASIS 2 manual; if an instrument streams, switch it off with
the vendor's software.

## An instrument is stuck in BASIS Protocol 1

`P 1` switches to the older protocol, whose grammar the driver does not implement. The manual says to return with
`P2`, but a Protocol 1 instrument may not parse the line the driver sends. Use the vendor's software or a terminal to
send `P2` (no space) with the unit ID, or restore the factory settings.

## The flow reads a few percent off

Tare with true zero flow on the pure process gas (`Tare()`), select the right gas (`SetGas()`), and check the
reference temperature (`ReadReferenceTemperature()`): the standard flow is referred to it.

## Wrong units

The driver prints and returns values in the instrument's own flow unit: `GetIdentity().unit_label.chars.data()`. Nothing
converts.
