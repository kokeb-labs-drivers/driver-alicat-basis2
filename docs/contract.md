# Contract

What the BASIS 2 driver promises, what it needs, and the rules a caller follows.

The driver inherits the [Kokeb Labs driver contract](https://github.com/kokeb-labs/.github/blob/main/standards/driver.md);
this page states only what is specific to it. A change to anything here is a breaking change, listed under
**Breaking** in the [changelog](changelog.md).

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Alicat Scientific, Inc. DOC-MANUAL-BASIS2 Rev 4:

    - Default serial settings and the link layer: Alicat ASCII Serial Communication, Establishing Communication, [p. 10](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=10)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Promises

- The constructors of `Bus` and `Basis2` are `noexcept`, store their arguments, do no I/O and cannot fail.
- `EnsureInitialized()` brings the port up and checks the stored configuration (`InvalidArgument`, nothing sent).
  `Begin()` is the first call that sends: it learns the instrument with read commands only. On any failure the object
  stays not ready and the next call starts again from the beginning.
- Every call that sends returns `Error::NotInitialized` before `EnsureInitialized()` and `Begin()` have succeeded where
  the call needs them. No call starts the instrument as a side effect.
- Nothing is ever sent outside the documented grammar. A `Request` can be made only by the factories, and every
  request is checked a second time by an independent validator before `Write()` (invariant I1).
- No command is sent while a transaction waits, or inside the quarantine that follows a failed one, or less than 3.5
  character times after the last byte on the line (I3, I4).
- The six commands that can strand an instrument are sent only for a call with its confirmation token (I5).
- A setpoint is never negative or above 102.5 % of the full scale, a valve hold never leaves 0 to 100 percent, and a
  saved setpoint is never changed faster than `Config::saved_setpoint_min_interval_ms` (I6).
- A failed call returns a zero-valued result: no value from a failed or partial reply is exposed (I8). A reply is used
  only when its first token is the unit ID addressed; a Modbus reply only when address, function and CRC match.
- Writes are never repeated. Reads are repeated at most `Config::read_retries` times.
- Every wait is bounded by a deadline computed from the baud rate and `Config`; `Poll()` always reaches a result or
  a terminal state within its deadlines: one line wait for a quiet line (else `Busy`, nothing sent, and a quarantine
  that needs only the quiet line), one reply wait for the reply, then the quarantine (I9). `Execute()` bounds both
  of its waits in time as well. The 32-bit millisecond clock may wrap and the line may stay quiet for weeks: every
  wait is a start time and a duration, compared as an unsigned elapsed time, so no wait depends on where the clock
  is (I11). A wait that is not polled for 2^32 ms (49.7 days) may be read as just begun and last its length once
  more; for the same reason a saved setpoint made exactly a multiple of 2^32 ms after the last one may be refused as
  `RateLimited` for up to `saved_setpoint_min_interval_ms` (the safe direction).
- `StateUnknown` is left only by a successful `Recover()` (I12), which needs a validated reply from the same
  instrument (serial number, firmware and kind unchanged). Only a switch-off (`SetSetpoint(0)`, `CloseValve()`)
  is still sent while it holds.
- A missing optional hook is not an error: no `DelayMs()` spins on `NowMs()`, no `SetBaud()` makes `ChangeBaud()`
  return `Unsupported`, no `Log()` logs nothing. A hook declared with the wrong signature is a compile error.

The fuzz targets check I1, I3 and I5 to I16 after every call (I11 through the clock offsets and the long quiet
periods of their sessions); I2 and I4 are proven by directed unit tests, and I9 and I11 by both
([Testing and CI](testing.md)).

## Depends on

| Dependency | Version | Why |
| --- | --- | --- |
| A C++20 compiler | GCC 10, Clang 13 or later | Concepts constrain the port |
| CMake | 3.16 or later | Generates `alicat_basis2_version.h` |
| Your port | | A serial line at 38400 baud 8N1 or what you configure, a millisecond clock, and for RS-485 the direction control inside `Write()` |

Nothing else: no RTOS, no SDK, no other repository of the organisation.

## Threading

The driver is not thread-safe and takes no lock. Use each `Bus` and every `Basis2` on it from one task at a time, or
guard every call with one mutex; the layer that owns the driver owns that lock. Nothing may run in an ISR. Separate
buses share no state.

## Errors

`alicat_basis2::Error` inside `alicat_basis2::Result<T>`. Values 0 to 9 are the shared base set; values from 16 are
specific to this driver, and each is returned by some call (a test checks it).

| `Error` | Value | Returned when |
| --- | --- | --- |
| `Ok` | 0 | The call succeeded |
| `NotInitialized` | 1 | `EnsureInitialized()` or `Begin()` has not succeeded |
| `InvalidArgument` | 2 | An argument or a `Config` field is out of range; nothing was sent |
| `Bus` | 3 | A port hook returned `false` |
| `Timeout` | 4 | No complete reply within the computed time |
| `DeviceIdMismatch` | 5 | The reply carried another unit ID or address |
| `Crc` | 6 | Modbus CRC-16 mismatch |
| `Busy` | 7 | The bus was not free or the line never went quiet: the transaction that met it was not sent; a writing call returns it only when its write was not sent ([Safety](safety.md#a-line-that-never-goes-quiet)) |
| `DeviceFault` | 8 | Reserved: the instrument reports faults as flags, which are data, not errors |
| `Unsupported` | 9 | Not available on this protocol, this kind, or this port |
| `BadReply` | 16 | The line does not match the command's reply grammar |
| `Rejected` | 17 | The instrument answered `?` or with a Modbus exception |
| `OutOfRange` | 18 | A decoded value is outside what the instrument can report |
| `Overflow` | 19 | The reply exceeded the line buffer without a terminator |
| `VerifyMismatch` | 20 | The read-back after a setter differs |
| `FirmwareTooOld` | 21 | The call needs a newer firmware |
| `StateUnknown` | 22 | A guarded command is not settled |
| `RateLimited` | 23 | A saved setpoint came too soon |
| `GuardNotConfirmed` | 24 | A guarded call carried the wrong token |

The [API guide](api-guide.md#handle-errors) says what to do about each.

## Memory

No heap, no exceptions, no RTTI, no `std::function`, no `std::string`, no `printf` and no static mutable state. A `Bus`
holds a 256-byte line buffer and a request; a `Basis2` holds a `Config`, an `Identity` and the shadow state.

## Platform

Platform-free: the same code runs on any target with a C++20 compiler, including the host tests. The ESP-IDF port in
[`examples/esp32`](examples.md) is an example, not part of the driver.

## Deviations from the standard

- **The port's `Read()` is partial.** It returns the number of bytes read (`std::size_t`), not an exact-length `bool`,
  because the reply length of an ASCII line is not known in advance. Documented in [Porting](porting.md).
- **The line buffer is 256 bytes, not 128**, because the gas list (`GS *`) reply can be that long.
- **The scans block.** `ScanUnitIds()` and `ScanModbusAddresses()` run to the end of the scan inside one call; every
  other call is bounded by one reply wait.
- **`DeviceFault` is never returned.** It stays in the base set, so the numeric values match the other drivers.
