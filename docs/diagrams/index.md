# Figures

<!-- Written by kokeb-docs figures from docs/diagrams/figures/*.py. Do not edit by hand. -->

Every figure on this site is drawn by a Python module from facts extracted from the code, so it is rebuilt,
not redrawn, when the code changes. CI runs `kokeb-docs figures --check` and fails when a
committed figure no longer matches the code. Each figure below lists the files it is drawn from.

## ASCII poll of the data frame

<!-- figure: ascii-transaction -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="ascii-transaction.dark.svg">
  <img src="ascii-transaction.svg"
    alt="One ASCII poll of a BASIS 2: the host sends the unit ID and a carriage return, the line stays idle
    for the serial idle timeout, and the instrument answers with one line of the data frame ended by a
    carriage return.">
</picture>
<!-- /figure -->

Hardware description: [`docs/model/ascii-transaction.timing.yml`](../model/ascii-transaction.timing.yml)

Drawn from:

- [`docs/model/ascii-transaction.timing.yml`](../model/ascii-transaction.timing.yml)

## Inside the BASIS 2

<!-- figure: chip-blocks -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-blocks.dark.svg">
  <img src="chip-blocks.svg"
    alt="Block diagram of the BASIS 2: its function blocks, how they connect and which package pins reach
    each block.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/alicat_basis2_uart_interface.hpp`](../../inc/alicat_basis2_uart_interface.hpp)

## What the BASIS 2 does

<!-- figure: chip-capabilities -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-capabilities.dark.svg">
  <img src="chip-capabilities.svg"
    alt="Capability map of the BASIS 2: Flow, temperature and total (supported): Polls the whole data frame
    or single items and decodes each number exactly; Setpoint, ramp and watchdog (supported): Sets the
    flow of a controller, limits how fast it changes and closes the valve if the line goes quiet; Valve
    hold and loop gains (supported): Holds the valve at a fixed drive, opens or closes it, and tunes the
    closed loop; Tare and autotare (supported): Zeroes the flow reading at no flow, and on a controller
    zeroes it again at a closed setpoint; Gas selection (supported): Selects one of the nine calibrated
    gases and lists the gases the instrument holds; Totalizer and batch (supported): Counts the gas that
    has passed, sets what happens at the limit, and dispenses a set volume; Measurement window
    (supported): Starts a timed measurement, by command or by an event, and reads its averages and
    ranges; Unit ID, baud and protocol (guarded): Changes the unit ID, address, baud rate or protocol,
    restores factory settings, and scans a line; Analog setpoint and output (not covered): A 0 to 5 V or
    4 to 20 mA setpoint input and flow output on pins 1 and 2, wired outside the driver.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/alicat_basis2_uart_interface.hpp`](../../inc/alicat_basis2_uart_interface.hpp)

## BASIS 2: RS-232 point to point

<!-- figure: chip-circuit-rs232 -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-circuit-rs232.dark.svg">
  <img src="chip-circuit-rs232.svg"
    alt="Schematic of the BASIS 2 rs-232 point to point: the chip with U2 MCU, U3 RS-232 level shifter, J1 12
    to 24 V supply; the MCU wires carry the port hooks Read(), Write().">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/alicat_basis2_uart_interface.hpp`](../../inc/alicat_basis2_uart_interface.hpp)

## BASIS 2: RS-485 multidrop line

<!-- figure: chip-circuit-rs485-multidrop -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-circuit-rs485-multidrop.dark.svg">
  <img src="chip-circuit-rs485-multidrop.svg"
    alt="Schematic of the BASIS 2 rs-485 multidrop line: the chip with U2 MCU, U3 RS-485 transceiver, J1 12
    to 24 V supply; the MCU wires carry the port hooks Write().">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/alicat_basis2_uart_interface.hpp`](../../inc/alicat_basis2_uart_interface.hpp)

## BASIS 2

<!-- figure: chip-hero -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-hero.dark.svg">
  <img src="chip-hero.svg"
    alt="Overview of the BASIS 2, a family of gas mass flow meters (B series) and controllers (BC series): it
    reports the flow, temperature and total of nine gases, and a controller sets the flow with its own
    valve. A MCU talks to it over RS-232 or RS-485; it drives Valve; it reads back Gas flow, Gas
    temperature. Headline figures: 12 to 24 V supply on pin 4 only; 1000:1 turndown, 0.1 to 100 % of
    full scale; 9 gases selectable, stored in the instrument; 176 Hz digital update rate at best.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/alicat_basis2_uart_interface.hpp`](../../inc/alicat_basis2_uart_interface.hpp)

## BASIS 2 package, JST GH 6-pin connector

<!-- figure: chip-outline -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-outline.dark.svg">
  <img src="chip-outline.svg"
    alt="Outline of the BASIS 2 in JST GH 6-pin connector drawn to scale from above: body 68.6 x 22.2 x 39.4
    mm, 6 pins.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/alicat_basis2_uart_interface.hpp`](../../inc/alicat_basis2_uart_interface.hpp)

## BASIS 2 pinout, JST GH 6-pin connector

<!-- figure: chip-pinout -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-pinout.dark.svg">
  <img src="chip-pinout.svg"
    alt="Pinout of the BASIS 2 in JST GH 6-pin connector, top view: 6 pins, each with its name, its
    electrical type as a colour and the driver hook that uses it.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/alicat_basis2_uart_interface.hpp`](../../inc/alicat_basis2_uart_interface.hpp)

## Wiring: RS-232 point to point

<!-- figure: chip-wiring-rs232 -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-wiring-rs232.dark.svg">
  <img src="chip-wiring-rs232.svg"
    alt="Wiring of the BASIS 2 for the RS-232 point to point: MCU UART TX to RS-232 level shifter T1IN
    (Write()); MCU UART RX to RS-232 level shifter R1OUT (Read()); RS-232 level shifter T1OUT to RX/B
    (Write()); RS-232 level shifter R1IN to TX/A (Read()). Supplies, grounds and the support parts are
    listed under the drawing.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/alicat_basis2_uart_interface.hpp`](../../inc/alicat_basis2_uart_interface.hpp)

## Wiring: RS-485 multidrop line

<!-- figure: chip-wiring-rs485-multidrop -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-wiring-rs485-multidrop.dark.svg">
  <img src="chip-wiring-rs485-multidrop.svg"
    alt="Wiring of the BASIS 2 for the RS-485 multidrop line: MCU UART TX to RS-485 transceiver DI (Write());
    MCU UART RX to RS-485 transceiver RO (Read()); MCU GPIO (direction) to RS-485 transceiver DE, RE
    (Write()); RS-485 transceiver A to TX/A (Write() and Read()); RS-485 transceiver B to RX/B (Write()
    and Read()). Supplies, grounds and the support parts are listed under the drawing.">
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

## One call through the layers

<!-- figure: one-call-through-the-layers -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="one-call-through-the-layers.dark.svg">
  <img src="one-call-through-the-layers.svg"
    alt="One ReadFlow call: the device class builds the request, the bus layer waits for silence, writes it
    through your port and reads until a carriage return, and the reply is checked and decoded into a
    flow value.">
</picture>
<!-- /figure -->

Hardware description: [`docs/model/one-call-through-the-layers.seq.yml`](../model/one-call-through-the-layers.seq.yml)

Drawn from:

- [`docs/model/one-call-through-the-layers.seq.yml`](../model/one-call-through-the-layers.seq.yml)

## Several BASIS 2 on one RS-485 line

<!-- figure: rs485-topology -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="rs485-topology.dark.svg">
  <img src="rs485-topology.svg"
    alt="An RS-485 line on which an MCU and up to 26 BASIS 2 instruments share the B (+) and A (-) wires,
    each answering to its own unit ID in ASCII or its own Modbus address in Modbus RTU.">
</picture>
<!-- /figure -->

Hardware description: [`docs/model/rs485-topology.topology.yml`](../model/rs485-topology.topology.yml)

Drawn from:

- [`docs/model/rs485-topology.topology.yml`](../model/rs485-topology.topology.yml)

## UART character

<!-- figure: uart-timing -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="uart-timing.dark.svg">
  <img src="uart-timing.svg"
    alt="One serial character of the BASIS 2 at its factory settings: a start bit, eight data bits least
    significant first and one stop bit with no parity, at 38400 bit/s unless the baud rate was changed.">
</picture>
<!-- /figure -->

Hardware description: [`docs/model/uart-timing.timing.yml`](../model/uart-timing.timing.yml)

Drawn from:

- [`docs/model/uart-timing.timing.yml`](../model/uart-timing.timing.yml)
