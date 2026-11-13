# BASIS 2 ESP32 examples

ESP-IDF applications that run the BASIS 2 driver on an ESP32-C6, built by CI on every pull request. Nothing was run
on an instrument: they are **not verified on hardware**.

## Wiring

The pins are menuconfig values (`BASIS 2 example`); these are the defaults.

| ESP32-C6 | Instrument connector | Notes |
| --- | --- | --- |
| GPIO4 (TX) | pin 5 (RX or B) | UART1, 38400 8N1; an RS-232 model needs a level shifter |
| GPIO5 (RX) | pin 6 (TX or A) | |
| GPIO6 (RTS) | transceiver DE and RE | RS-485 only, `ALICAT_BASIS2_EXAMPLE_RS485` |
| 12 to 24 V supply | pin 4 | **Power on pin 4 only** |
| Supply ground and MCU ground | pin 3 | One common ground |

With an RS-485 transceiver, MCU TX goes to DI and RX to RO; transceiver A goes to pin 6 and B to pin 5. The
[Hardware](../../docs/hardware.md) page has the circuits and the warnings.

## Build and flash

```bash
git submodule update --init scripts      # the ESP-IDF build scripts
. $IDF_PATH/export.sh                     # ESP-IDF v5.5
./scripts/build_app.sh list
./scripts/build_app.sh alicat_basis2_minimal_example Debug
./scripts/flash_app.sh flash_monitor alicat_basis2_minimal_example Debug
```

## Expected output

```text
I (312) basis2_minimal: BASIS 2 driver 0.1.0
I (1320) basis2_minimal: 12.30 SLPM, 24.80 C
```

A `start-up failed: Timeout` line means no reply: check the unit ID, the baud rate, A and B (RS-485) and the
direction pin. See [Troubleshooting](../../docs/troubleshooting.md).

## Files

| File | What it is |
| --- | --- |
| [`main/alicat_basis2_minimal_example.cpp`](main/alicat_basis2_minimal_example.cpp) | Poll one instrument once a second |
| [`main/alicat_basis2_full_example.cpp`](main/alicat_basis2_full_example.cpp) | Scan, identify, setpoint, measurement window, flags |
| [`main/esp32_alicat_basis2_port.hpp`](main/esp32_alicat_basis2_port.hpp) | The ESP-IDF port: UART, clock, delay, baud, log |
| [`main/Kconfig.projbuild`](main/Kconfig.projbuild) | Pins, RS-485 mode and unit ID |
| [`CMakeLists.txt`](CMakeLists.txt) | The ESP-IDF project; `EXTRA_COMPONENT_DIRS` points at the repository root |
| [`app_config.yml`](app_config.yml) | The app matrix for the scripts, CI and the docs |
