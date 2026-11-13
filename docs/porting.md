# Porting

Run the driver on a new MCU by writing one class: the port.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Alicat Scientific, Inc. DOC-MANUAL-BASIS2 Rev 4:

    - Connector pins and serial levels: Power and Signal Connections (pin table), [p. 7](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=7)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

The driver never calls your SDK. It calls hooks on a class you derive from `alicat_basis2::UartInterface<Self>` (in
[`inc/alicat_basis2_uart_interface.hpp`](../inc/alicat_basis2_uart_interface.hpp)). The base uses CRTP and declares no
hook itself; the driver template is constrained by the `alicat_basis2::UartPort` concept, so calls are resolved at
compile time (no virtual functions, nothing to allocate) and a missing or misdeclared hook is a compile error at the
point of use.

## What you implement

| Method | Required | Contract |
| --- | --- | --- |
| `bool EnsureInitialized() noexcept` | yes | Bring the UART up on the first call; return `true` at once afterwards |
| `bool Write(const uint8_t* data, std::size_t len) noexcept` | yes | Send all `len` bytes; return after the last byte has left the pin |
| `std::size_t Read(uint8_t* data, std::size_t capacity, uint32_t timeout_ms) noexcept` | yes | Return the bytes that have arrived, up to `capacity`; do not wait for more |
| `bool FlushInput() noexcept` | yes | Drop every received byte not read yet |
| `uint32_t NowMs() noexcept` | yes | A monotonic millisecond clock; it may wrap |
| `void DelayMs(uint32_t ms) noexcept` | no | Sleep at least `ms`; without it blocking calls poll `NowMs()` |
| `bool SetBaud(uint32_t baud) noexcept` | no | Change the line speed; needed only by `ChangeBaud()` |
| `void Log(LogLevel, const char* message) noexcept` | no | Print one line; `message` is plain text, never a format string |

## The port

```cpp title="my_port.hpp"
--8<-- "tests/docs/my_port.hpp:port"
```

Rules the port must follow:

- **Raw bytes only.** The driver builds the lines and frames and checks them; the port does not parse, frame or retry.
- **`Read()` is partial and does not wait.** It returns what has arrived. The driver polls, keeps its own time and
  applies its own deadlines. (This differs from the template's exact-length `Read()`: the length of an ASCII line is
  not known in advance.) `timeout_ms` is advisory.
- **`Write()` returns after the last byte has left the pin.** The driver times the silent interval from the end of
  your `Write()`, and an RS-485 direction pin must not be released early.
- **`FlushInput()` really flushes**: the driver calls it before each request so a stale byte cannot shift a reply.
- **8N1, no flow control**, at the speed in `Config::baud`.

## Bus requirements

The driver states what the instrument needs from the line as `constexpr` values in
[`inc/alicat_basis2_uart_interface.hpp`](../inc/alicat_basis2_uart_interface.hpp) (handbook KB-CPP-123). Check your port
settings against them with `static_assert`, as both example ports do:

| Constant | Value | Meaning |
| --- | --- | --- |
| `kPortDefaultBaud` | 38400 | The factory baud rate, 8N1 (manual p. 10) |
| `kMinBaudRate`, `kMaxBaudRate` | 4800, 115200 | The rates the instrument offers (p. 17) |
| `kSilentBitTimes` | 35 | The idle time before the instrument acts, 3.5 characters of ten bits (p. 10) |
| `kTurnaroundUs` | 50000 | Added to every reply wait (an assumption) |
| `kSupplyMinMv`, `kSupplyMaxMv` | 12000, 24000 | The supply on pin 4 only (p. 6) |

## RS-485

An RS-485 instrument is half duplex on one pair, and the driver knows nothing about the direction pin: the port owns
it. Two rules follow.

- Switch the transceiver to transmit before the first byte and back to receive **after the last byte has left the
  shift register**, not after it has been copied to a FIFO. On ESP-IDF that is the RS-485 half-duplex mode with
  `uart_wait_tx_done()`; on STM32 the hardware driver-enable or the transmission-complete interrupt; on Linux
  `TIOCSRS485`.
- A transceiver whose receiver stays enabled while it sends hears its own bytes. Set `Config::echo` to `EchoMode::Skip`
  and the driver drops a first reply line that equals the command it just sent.

A termination of 120 ohm at both ends of a long line and a common ground are general practice; the manual does not
describe them ([Hardware](hardware.md)).

## Baud switching

`ChangeBaud()` sends the baud command and then calls your `SetBaud()` right after the send, because the instrument's
reply arrives at the new speed (manual p. 17). Without a `SetBaud()` hook the call returns `Unsupported`. On failure the
driver sets the old speed back once and the state is `StateUnknown`; call `Recover()` after you have checked the
port's speed.

## Control pins

None. The instrument has no reset or enable pin on its connector, and the driver sets no pin. The port is the whole
platform interface.

## Optional hooks

```cpp title="every hook"
--8<-- "tests/docs/ports.cpp:full-port"
```

A hook that is declared with the wrong signature fails a `static_assert` that names it: a `DelayMs` that takes a
`uint16_t`, or a `SetBaud` that returns `void`, is a compile error rather than a silently ignored hook.

### Non-blocking use

`Basis2` calls `Bus::Execute()`, which blocks until the transaction is over (with `DelayMs()` when the port has it).
If your loop left a transaction on the bus, `Execute()` waits for it within its deadlines (reply wait, quarantine,
hold-off) and drops its result when nobody took it, so a switch-off is never held back by it.
`Bus` can also be driven from your own loop:

```cpp
--8<-- "tests/docs/ports.cpp:nonblocking"
```

## Reference implementations

Every port lives with its example, in `examples/<platform>/`; the driver has none. [Platforms](platforms.md) lists
them and how to add one.

- [`examples/host/host_alicat_basis2_port.hpp`](../examples/host/host_alicat_basis2_port.hpp): any PC, against a
  simulated instrument. Start from it for a host build or a test bench.
- [`examples/esp32/main/esp32_alicat_basis2_port.hpp`](../examples/esp32/main/esp32_alicat_basis2_port.hpp): ESP-IDF, with
  every hook and an RS-485 mode. Start from it for any ESP-IDF target.

## Check your port

1. Build it: the ports on this page are compiled by the host CI from [`tests/docs/`](../tests/docs/my_port.hpp), with
   the driver instantiated over them.
2. Call `EnsureInitialized()` and `Begin()`: `Ok` proves that writes and reads work and that the unit ID and baud
   rate are right.
3. Call `ReadFrame()` once a second and open the valve of a flow source: the flow rises.
4. Unplug the instrument: `Timeout`, and no stray output on the line afterwards.

## Threads

The driver is not thread-safe. Give each driver object one owner task, or guard every call with one mutex. When
several chips share a bus, the port must also serialise the bus. See [Contract](contract.md#threading).

## Editor setup

The clang-format and clang-tidy configuration is in `_config/`, and the repository root has no copy of it. Tools
that take a path use it directly; CI does the same:

| Tool | Setting |
| --- | --- |
| Command line | `clang-format --style=file:_config/.clang-format`, `clang-tidy --config-file=_config/.clang-tidy -p build` |
| VS Code (C/C++ extension) | `"C_Cpp.clang_format_style": "file:${workspaceFolder}/_config/.clang-format"` and `"C_Cpp.codeAnalysis.clangTidy.args": ["--config-file=${workspaceFolder}/_config/.clang-tidy"]` |
| clangd (VS Code clangd, Neovim, Emacs), CLion | These find `.clang-format` and `.clang-tidy` only by name, from the file upwards: copy both to the root once (`cp _config/.clang-format _config/.clang-tidy .`). `.gitignore` lists the copies, so they never reach a commit; copy again after `_config/` changes |

Configure with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (clangd: `--compile-commands-dir=build`) so the editor sees the
same flags as the build.
