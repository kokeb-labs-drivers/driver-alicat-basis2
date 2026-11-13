# Figures

<!-- Written by kokeb-docs figures from docs/diagrams/figures/*.py. Do not edit by hand. -->

Every figure on this site is drawn by a Python module from facts extracted from the code, so it is rebuilt,
not redrawn, when the code changes. CI runs `kokeb-docs figures --check` and fails when a
committed figure no longer matches the code. Each figure below lists the files it is drawn from.

## Inside the BASIS 2 (B and BC series)

<!-- figure: chip-blocks -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-blocks.dark.svg">
  <img src="chip-blocks.svg"
    alt="Block diagram of the BASIS 2 (B and BC series): its function blocks, how they connect and which
    package pins reach each block.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/alicat_basis2_uart_interface.hpp`](../../inc/alicat_basis2_uart_interface.hpp)

## BASIS 2 (B and BC series): RS-232 point to point

<!-- figure: chip-circuit-rs232 -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-circuit-rs232.dark.svg">
  <img src="chip-circuit-rs232.svg"
    alt="Schematic of the BASIS 2 (B and BC series) rs-232 point to point: the chip with U2 MCU, U3 RS-232
    level shifter, C1 capacitor 100 nF, J1 12 to 24 V supply; the MCU wires carry the port hooks Read(),
    Write().">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/alicat_basis2_uart_interface.hpp`](../../inc/alicat_basis2_uart_interface.hpp)

## BASIS 2 (B and BC series): RS-485 multidrop line

<!-- figure: chip-circuit-rs485-multidrop -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-circuit-rs485-multidrop.dark.svg">
  <img src="chip-circuit-rs485-multidrop.svg"
    alt="Schematic of the BASIS 2 (B and BC series) rs-485 multidrop line: the chip with U2 MCU, U3 RS-485
    transceiver, J1 12 to 24 V supply, C1 capacitor 100 nF; the MCU wires carry the port hooks Read(),
    Write().">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/alicat_basis2_uart_interface.hpp`](../../inc/alicat_basis2_uart_interface.hpp)

## BASIS 2 (B and BC series) package, JST GH 6-pin connector

<!-- figure: chip-outline -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-outline.dark.svg">
  <img src="chip-outline.svg"
    alt="Outline of the BASIS 2 (B and BC series) in JST GH 6-pin connector drawn to scale from above: body
    68.6 x 22.2 x 39.4 mm, 6 pins.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/alicat_basis2_uart_interface.hpp`](../../inc/alicat_basis2_uart_interface.hpp)

## BASIS 2 (B and BC series) pinout, JST GH 6-pin connector

<!-- figure: chip-pinout -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-pinout.dark.svg">
  <img src="chip-pinout.svg"
    alt="Pinout of the BASIS 2 (B and BC series) in JST GH 6-pin connector, top view: 6 pins, each with its
    name, its electrical type as a colour and the driver hook that uses it.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/alicat_basis2_uart_interface.hpp`](../../inc/alicat_basis2_uart_interface.hpp)

## The layers, one of them yours

<!-- figure: driver-layers -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="driver-layers.dark.svg">
  <img src="driver-layers.svg"
    alt="Layer diagram: your application calls the Basis2 core, which sends register reads and writes through
    the UART port contract to the port class you write, which drives the chip; the core and the contract
    are the same on every MCU.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/driver-layers.py`](../../docs/diagrams/figures/driver-layers.py)

Drawn from:

- `public calls` in [`inc/alicat_basis2.hpp`](../../inc/alicat_basis2.hpp)
- `UartInterface` in [`inc/alicat_basis2_uart_interface.hpp`](../../inc/alicat_basis2_uart_interface.hpp)
