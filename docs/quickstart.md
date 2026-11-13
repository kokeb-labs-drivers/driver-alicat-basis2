# Quickstart

Run the driver on your PC first, with no hardware; then read the flow from your MCU: wire the instrument, write a
small port class, call three driver functions.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Alicat Scientific, Inc. DOC-MANUAL-BASIS2 Rev 4:

    - Models, ranges and options: Introduction, [p. 3](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=3)
    - Connector pins and serial levels: Power and Signal Connections (pin table), [p. 7](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf#page=7)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Try it on your PC

[`examples/host/`](../examples/host/README.md) runs the real driver against a simulated instrument, through a host
port. From the repository root, with CMake and a C++20 compiler:

```bash
cmake -S examples/host -B build-example
cmake --build build-example
./build-example/alicat_basis2_host_example
```

The program runs five short scenarios (read the flow, ramp a controller, tare and totalize, scan the line, read over
Modbus) and exits with 0 when every call succeeded. The calls in [`main.cpp`](../examples/host/main.cpp) are the ones
below; only the port differs on an MCU. CI builds and runs this example on every pull request.

## 1. Wire it

| MCU | Instrument connector | Notes |
| --- | --- | --- |
| UART TX | pin 5 (RX or B) | 38400 baud, 8N1; RS-232 or RS-485 levels by model, so a level shifter or a transceiver |
| UART RX | pin 6 (TX or A) | |
| 12 to 24 V supply | pin 4 | **Power on pin 4 only.** Any other pin can damage the instrument |
| Ground | pin 3 | One common ground |

The [Hardware](hardware.md) page has the connector, both circuits and the warnings. Connect every pin before you
switch on the supply.

## 2. Add the driver

Add the repository as a submodule and link the `alicat_basis2::alicat_basis2` CMake target. [Installation](installation.md)
covers CMake on any MCU, the installed package and ESP-IDF.

```bash
git submodule add https://github.com/kokeb-labs-drivers/driver-alicat-basis2.git external/driver-alicat-basis2
```

## 3. Write the port

The port is the only platform code: it brings the UART up, moves bytes and tells the time. Replace the `platform_*`
calls with your SDK's.

```cpp title="my_port.hpp"
--8<-- "tests/docs/my_port.hpp:port"
```

[Porting](porting.md) explains each method, the optional hooks and the rules for RS-485.

## 4. Read the flow

```cpp title="main.cpp"
--8<-- "tests/docs/quickstart.cpp:quickstart"
```

What each step does:

1. `EnsureInitialized()` calls your port's `EnsureInitialized()`. No traffic yet.
2. `Begin()` learns the instrument: its kind (meter or controller), firmware, full scale, flow unit, gas and
   setpoint source. It sends read commands only.
3. `ReadFlow()` sends one poll and returns the flow as a `Decimal` in the instrument's flow unit.

The factory settings are unit ID `A`, 38400 baud, ASCII protocol. For another unit ID or for Modbus, change
[`Config`](configuration.md). This example is built by the host CI, so it compiles against the current headers.

## If it does not work

| Symptom | First thing to check |
| --- | --- |
| `EnsureInitialized()` returns `Bus` | Your port's `EnsureInitialized()`: the UART did not come up |
| `Begin()` returns `Timeout` | Wiring, A and B swapped (RS-485), baud rate and unit ID ([Troubleshooting](troubleshooting.md#no-reply)) |
| `Begin()` returns `DeviceIdMismatch` | Another instrument answered: two instruments with one ID |
| `Begin()` returns `InvalidArgument` | In Modbus mode `modbus_flow_decimals` is required: set it ([Configuration](configuration.md)) |

## Next

- [API guide](api-guide.md): setpoints, scanning, recovery, errors.
- [Protocols](protocols.md): ASCII and Modbus RTU side by side.
- [Configuration](configuration.md): every `Config` field and Kconfig.
- [Examples](examples.md): the host example and the ESP32 examples.
