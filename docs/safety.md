# Safety notes

What can go wrong with a gas flow instrument on a serial line, and what the driver does about it. Nothing here has
been verified on an instrument: the behaviours the manual does not state are marked as assumptions on the
[Datasheet map](datasheet.md#assumptions-and-open-items).

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Alicat Scientific, Inc. DOC-MANUAL-BASIS2 Rev 4:

    - Mounting, LED and power-up behaviour: Getting Started, Mounting, [p. 5](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=5)
    - Power and signal connections, pin 4 only: Power and Signal Connections, [p. 6](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=6)
    - Setpoint, setpoint source and ramp: Setpoint; Setpoint Source; Maximum Setpoint Ramp Rate, [p. 11](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=11)
    - Communication watchdog: Communication Watchdog, [p. 12](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=12)
    - Hold valve, gains, reference temperature, averaging: Hold Valve Position; Loop Control Gains; Reference Temperature; Readings Averaging, [p. 13](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=13)
    - Tare and autotare: Taring the Instrument; Using Ground to Tare; Autotare, [p. 14](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=14)
    - Unit ID: Unit ID, [p. 16](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=16)
    - Modbus address, baud rate, command protocol: Modbus Address; Serial Baud Rate; Command Protocol, [p. 17](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=17)
    - Analog signals: Analog Signals, [p. 28](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=28)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Power

Connect power to **pin 4 only**, and switch it on only when every other pin is connected (manual p. 6). Power on any
other pin can permanently damage the instrument. The supply is 12 to 24 V; the manual states no absolute maximum, no
reverse polarity protection and no ESD rating, so provide your own. Never connect the instrument to a loop-powered
system: the manual says it will destroy parts of the circuitry (p. 28); use an isolator and a separate supply.

## The valve of a controller

A controller's valve is normally closed. The driver offers four ways to move it, and each has a failure mode that
the application must cover:

- **A setpoint** (`SetSetpoint()`) is checked against 0 to 2.5 % over full scale, and read back unless a ramp is active
  (the instrument then lags on purpose).
- **A hold** (`HoldValve(percent)`, `OpenValve()`, `CloseValve()`) overrides the loop: the valve stays where it is put
  until `ResumeControl()`. The status flag `HLD` and a slow red blink of the LED show it. A held valve does not
  follow a setpoint, so a program that holds the valve and then crashes leaves it held. Close it on every exit path.
- **A ramp** (`SetRamp()`, `DisableRamp()`) limits how fast the setpoint is followed. The unit of the rate is not
  defined in the manual; check it on the bench before relying on it.
- **Thermal management** (`VTM`) pulses the valve by itself when a setpoint gives no flow. It is an override, not a
  fault.

When in doubt, `SetSetpoint(0.0)` then `CloseValve()`, in that order, and leave the instrument safe: no flow, no ramp.
`SetSetpoint(0.0)` does **not** release a held valve (the `HLD` flag stays); `CloseValve()` does the closing. Over
Modbus every hold call (`HoldValve()`, `OpenValve()`, `CloseValve()`, `ResumeControl()`) is `Unsupported`: there
`SetSetpoint(0.0)` is the only way to close the valve. `FactoryRestore()` reverts the setpoint source to its factory
value, which the manual does not state; if it is analog, serial setpoints are ignored until the source is set again.

## The watchdog does not protect an ASCII program

`SetWatchdog()` forces a zero setpoint and closes the valve when no serial communication arrives within the time, but
the manual says it acts only when the setpoint was set over Modbus and the source is unsaved digital. With ASCII, with
the saved source or with the analog source it does nothing. The constexpr function
`Basis2::WatchdogProtects(protocol, source)` is that truth table. **Do not rely on the watchdog with ASCII.** The
application must stop commanding and then close the valve itself, and must treat a lost line as a reason to do so.

## Saved setpoints wear memory

`SetpointSource::SavedDigital` writes every setpoint to non-volatile memory and the instrument returns to the last
saved one at power-up. The manual warns against changing it more often than every few minutes. The driver refuses a
change sooner than `Config::saved_setpoint_min_interval_ms` (180 seconds) with `RateLimited`. For anything that changes
often, use `SetpointSource::UnsavedDigital`, and remember that a saved setpoint is what the valve does at the next
power-up before any program runs.

## Tare

`Tare()` makes the present reading the zero. Do it only with true zero flow through the instrument and only on the pure
process gas (manual p. 15). A tare with flow running sets a wrong zero that every later reading inherits. A meter can
also be tared by grounding pin 1 for at least 25 ms; the tare completes when the ground is removed.

## Guarded commands

The bus assumes **one instrument** until the application calls `Bus::SetDeviceCount(n)` (the default is one). On a
line with several instruments declare it before anything else: it is what makes `ChangeUnitId()`,
`SetModbusAddress()`, `ChangeBaud()` and `FactoryRestore()` refuse and `Recover()` never adopt another unit.

Six commands can leave the instrument unreachable, or change what the instrument does for every later program. They
cannot be called by accident: each takes a token whose type names the consequence.

| Call | Token | Consequence |
| --- | --- | --- |
| `ChangeUnitId(id, ConfirmUnitIdChange{})` | `ConfirmUnitIdChange` | Another unit ID; **connect one instrument at a time** (the call refuses when the bus declares more) |
| `SetModbusAddress(a, ConfirmAddressChange{})` | `ConfirmAddressChange` | Another Modbus address |
| `ChangeBaud(b, ConfirmBaudChange{})` | `ConfirmBaudChange` | The reply comes at the new speed; the port must switch in time (needs `SetBaud()`) |
| `SwitchToProtocol1(ConfirmAbandonProtocol2{})` | `ConfirmAbandonProtocol2` | The instrument speaks the old protocol, which the driver does not; every call is `Unsupported` afterwards |
| `SwitchToProtocol2(ConfirmReturnToProtocol2{})` | `ConfirmReturnToProtocol2` | Back to BASIS Protocol 2 |
| `FactoryRestore(ConfirmFactoryRestore{})` | `ConfirmFactoryRestore` | Unit ID, baud rate, Modbus address and protocol revert |

If a guarded command may have changed the line settings and the driver cannot confirm it, the state becomes
`StateUnknown`: every call returns that error until a successful `Recover()` finds the same instrument again, except a
switch-off (`SetSetpoint(0.0)` and `CloseValve()`), which is still sent with the settings the driver holds and returns
its own result. After an unconfirmed `@=`, baud change or `FACTORY RESTORE` that switch-off tries, in this order, the
held setting, the setting the driver was changing to, and the factory defaults, adopts the first one that answers, and
puts the held setting back when none does; a valve is never left open for doubt. `Begin()` does not end the doubt: it
refuses while the state is unknown. `Recover()` polls the configured ID or address first, and after an unconfirmed
baud change or factory restore it also tries the other speed (the one the driver was changing to, or 38400). Only when
the bus declares a single instrument (`Bus::DeviceCount()` is one) does it scan the unit IDs `A` to `Z` over ASCII or
the addresses 1 to 247 over Modbus; with several instruments it never adopts another unit. It then runs `Begin()` and
compares the serial number, firmware and kind with those learned before: a different instrument is `VerifyMismatch`.
On any failure the driver stays in `StateUnknown` and keeps its old ID; a `Recover()` that ends `Busy` (a line that
never went quiet) leaves the state as it was, unless its scan had already found the instrument at another ID, address
or speed: those settings are then kept, still in doubt. After `FactoryRestore()` power-cycle the instrument first;
when its reply is lost the bus still stays closed for `Config::restore_holdoff_ms` from the send.

On a line declared to hold several instruments, `ChangeUnitId()`, `SetModbusAddress()`, `ChangeBaud()` and
`FactoryRestore()` return `GuardNotConfirmed` before sending: an unconfirmed change could not be traced there, one
instrument cannot move the speed its neighbours keep, and the factory ID `A` and address 1 collide with a neighbour. A
switch-off in doubt there tries only another speed at the held ID and address, never another ID or address, and puts
the held speed back afterwards whether or not it answered: the line stays at the speed of the neighbours. `Recover()`
there polls only the held ID or address at the held speed.

## A line that never goes quiet

A request waits for 3.5 quiet character times before it is sent. When the line is not quiet within one line wait (its
reply wait without its own duration, such as a measurement window) because a streaming or babbling peer talks, the
request ends with `Busy` and **nothing is sent**, a switch-off included. As nothing was sent no reply can come: the
quarantine that follows ends as soon as the line is quiet, and at the latest after two line waits. A call that writes
returns `Busy` only when its write was not sent: a later step of it (a verify, a read-back, a second write, a retry of a
read) that meets a busy line returns `Timeout`, as for a lost reply. A call made only of reads may have sent its earlier
reads before it returns `Busy`. A scan (`ScanUnitIds()`, `ScanModbusAddresses()`) stops at the first poll that meets
such a line and returns `Busy` rather than a partial list, and `Recover()` then returns `Busy` and leaves the state as
it was (a doubt stays). A guarded command that ends `Busy` leaves nothing in doubt. Call the switch-off again once the
line is quiet, and find the peer that talks out of turn (`Bus::StreamingSuspected()`).

## A reset the driver does not see

The manual documents no reset flag, so the driver cannot know that the instrument lost power or restarted between two
calls and then answered normally. What the instrument does after a restart follows from the manual: the totalizer
restarts from zero, an **unsaved** digital setpoint is lost, and a **saved** setpoint (the `S` source) is applied
again. The driver does not compare a new total with the one before. An application that must notice a reset watches for
a total that drops, a setpoint read that differs from the last one set, or a reply that did not come (`Timeout`), and
then calls `Recover()`. Until it does, the driver trusts its own record of the setpoint source, gas and ramp. Reset
detection belongs to the application; the driver keeps the saved-setpoint write history across `Begin()` and
`Recover()`, and arms its rate limit whenever a write may have reached the instrument, even if the reply was lost.

Over Modbus the factory `BuildWriteSingle()` takes no guard flag: writes to the baud, address, ID, protocol and
factory-restore registers are protected only by the token types of `Basis2`. A switch-off over Modbus after
`SwitchToProtocol1()` is refused like every call (assumption: register 2053 may still answer, the manual does not say).

The driver never sends the broadcast prefix `*` and never sends a streaming command: a streaming peer is only
detected, counted and ignored.

Next: [API guide](api-guide.md).
