# Examples

One folder per platform under `examples/`, each with its own port, built by CI on every pull request. The driver
itself has no platform code; [Platforms](platforms.md) lists what exists.

## Host

[`examples/host/`](../examples/host/README.md) runs the driver on any PC against a simulated BASIS 2 instrument,
through the host port [`host_alicat_basis2_port.hpp`](../examples/host/host_alicat_basis2_port.hpp). No hardware:

```bash
cmake -S examples/host -B build-example && cmake --build build-example && ./build-example/alicat_basis2_host_example
```

It runs five scenarios, each a snippet below: read the flow ten times, ramp a controller with the unsaved source and
close the valve, tare and totalize, scan the line for unit IDs, and read over Modbus RTU. The host tests build it with
warnings as errors and run it (CTest `alicat_basis2_host_example`).

```cpp title="Read the flow"
--8<-- "examples/host/main.cpp:read_flow"
```

```cpp title="Ramp a controller and leave it safe"
--8<-- "examples/host/main.cpp:controller_ramp"
```

```cpp title="Tare and totalize"
--8<-- "examples/host/main.cpp:tare_and_total"
```

```cpp title="Scan the line"
--8<-- "examples/host/main.cpp:scan_bus"
```

```cpp title="The same readout over Modbus RTU"
--8<-- "examples/host/main.cpp:modbus_readout"
```

## ESP32

ESP-IDF applications that run the driver on an ESP32-C6, with pins chosen in menuconfig; the wiring is in the
[example folder's README](../examples/esp32/README.md) and the port in
[`esp32_alicat_basis2_port.hpp`](../examples/esp32/main/esp32_alicat_basis2_port.hpp). The project points
`EXTRA_COMPONENT_DIRS` at the repository root, which is the driver's ESP-IDF component.

### The apps

<!-- generated: examples -->
| App | What it shows | Source | CI |
| --- | --- | --- | --- |
| `alicat_basis2_minimal_example` :material-star:{ title="featured" } | One instrument at its factory settings on UART1 at 38400 baud: poll the flow once a second | [`alicat_basis2_minimal_example.cpp`](../examples/esp32/main/alicat_basis2_minimal_example.cpp) | yes |
| `alicat_basis2_full_example` | RS-485 half duplex: scan the line, identify, set a setpoint on the unsaved source, measure, map the flags | [`alicat_basis2_full_example.cpp`](../examples/esp32/main/alicat_basis2_full_example.cpp) | yes |

Build and flash one (from `examples/esp32`, ESP-IDF environment loaded):

```bash
./scripts/build_app.sh alicat_basis2_minimal_example Release
./scripts/flash_app.sh flash_monitor alicat_basis2_minimal_example Release
```

Source of this table: [`examples/esp32/app_config.yml`](../examples/esp32/app_config.yml).
<!-- /generated -->

### Set up once

```bash
git submodule update --init examples/esp32/scripts   # the ESP-IDF build scripts
cd examples/esp32
. $IDF_PATH/export.sh                                 # ESP-IDF v5.5
./scripts/build_app.sh list                           # every app and build type
```

The scripts read [`app_config.yml`](../examples/esp32/app_config.yml) (the same file this page's table comes
from) and build each app as its own firmware image.

### What to read in an example

| You want to | Read |
| --- | --- |
| See the lifecycle and every `Result` checked | `alicat_basis2_minimal_example.cpp` |
| See a scan, a setpoint on the unsaved source and a measurement window | `alicat_basis2_full_example.cpp` |
| Write an ESP-IDF port, with RS-485 half duplex | `esp32_alicat_basis2_port.hpp` |
| Choose the pins and the unit ID | `main/Kconfig.projbuild` |
| Use the driver's menuconfig values | `alicat_basis2_kconfig.hpp` and [`_config/Kconfig`](../_config/Kconfig) |
