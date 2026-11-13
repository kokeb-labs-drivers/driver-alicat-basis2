---
hide:
  - navigation
  - toc
---

<div class="kb-hero" markdown>
<span class="kb-kicker">Driver</span>

# BASIS 2 Driver

<p>A C++20 driver for the Alicat BASIS 2 gas mass flow meters (B series) and controllers (BC series). It reads
flow, temperature and totals, sets setpoints and valve holds, and speaks both the ASCII protocol and Modbus RTU
behind one API, on any MCU.</p>

[Quickstart](quickstart.md){ .md-button .md-button--primary }
[API guide](api-guide.md){ .md-button }
[Source on GitHub](https://github.com/kokeb-labs-drivers/driver-alicat-basis2){ .md-button }
</div>

The driver has no platform code. You give it a small class that moves bytes on a serial line and tells the time; it
does the rest: the unit ID and silent-interval rules of the line, frame parsing, unit and decimal handling, Modbus
framing and CRC, retries of reads, and the guards around the commands that can strand an instrument.

<div class="kb-chip" markdown>
<div class="kb-chip__head">
<span class="kb-chip__kicker">Gas mass flow meters and controllers</span>
<span class="kb-chip__name">BASIS 2</span>
<span class="kb-chip__vendor">Alicat Scientific, B and BC series</span>
</div>

Function
:   Mass flow, temperature and total of nine gases, 100 SCCM to 100 SLPM full scale; controllers add a setpoint, a
    ramp and a valve

Interface
:   RS-232 or RS-485 by model, 38400 baud 8N1 by default; ASCII protocol with unit IDs A to Z, or Modbus RTU

Protocols
:   ASCII "BASIS Protocol 2" (the default) and Modbus RTU, both complete, one API

Supply
:   12 to 24 V on pin 4 only; 210 mA for a controller, 12 mA for a meter

Connector
:   Six-pin JST GH: analog setpoint, analog output, ground, power, serial B and serial A
</div>

## What you get

- **One object per instrument.** `Basis2<YourPort>` sits on a `Bus<YourPort>` that owns the serial line, so up to 26
  instruments share one line. See [API guide](api-guide.md).
- **Two protocols, one API.** `Config::protocol` selects ASCII or Modbus RTU; the calls do not change. See
  [Protocols](protocols.md).
- **Errors you can act on.** Every call returns `Result<T>` with an `Error` such as `Timeout`, `DeviceIdMismatch` or
  `VerifyMismatch`. See [Troubleshooting](troubleshooting.md).
- **Guarded commands.** The six commands that can leave an instrument unreachable need a token that names the
  consequence. See [Safety notes](safety.md).
- **A port of five hooks.** `Write`, `Read`, `FlushInput`, `NowMs` and `EnsureInitialized`, plus optional `DelayMs`,
  `SetBaud` and `Log`. See [Porting](porting.md).
- **No heap, no exceptions, no RTTI.** See [Contract](contract.md).
- **Tested without hardware.** An independent model instrument with a byte-accurate wire, fault injection and four
  fuzz targets. See [Testing and CI](testing.md). Nothing has been run on an instrument yet.

## Datasheet

<!-- generated: datasheet-card -->
| Document | Revision | Get it |
| --- | --- | --- |
| Datasheet manual: Operating Manual for Mass Flow Instruments, BASIS 2 Meters and Controllers (Alicat Scientific, Inc. DOC-MANUAL-BASIS2) | Rev 4, 2025-05-23 | [PDF](https://documents.alicat.com/manuals/DOC-MANUAL-BASIS2.pdf) · [vendor page](https://www.alicat.com/basisoem) |
| Datasheet specification sheet: Technical Data for BASIS 2.0 Mass Flow Meters and Controllers (Alicat Scientific, Inc. DOC-SPECS-BASIS) | Rev 19, 2026-08-17 | [PDF](https://documents.alicat.com/specifications/DOC-SPECS-BASIS.pdf) · [vendor page](https://www.alicat.com/basisoem) |
| Datasheet specification sheet: Technical Data for BASIS 2 (pages 1 to 3 of the combined manual and specification file) (Alicat Scientific, Inc. MANUAL+SPECS_BASIS2) | Rev 16, 2024-03-01 | [PDF](https://documents.alicat.com/specifications/MANUAL+SPECS_BASIS2.pdf) · [vendor page](https://www.alicat.com/basisoem) |
| Datasheet manual: Alicat Modbus Manual (generic, written for the classic registers) (Alicat Scientific, Inc. DOC-MANUAL-MODBUS) | Rev 7, 2024-11-01 | [PDF](https://documents.alicat.com/manuals/DOC-MANUAL-MODBUS.pdf) · [vendor page](https://www.alicat.com/basisoem) |
| Datasheet application note: Alicat Serial Communications Primer (classic line, firmware 4v to 10v) (Alicat Scientific, Inc. Alicat-Serial-Primer) | Rev 2, 2023-02-01 | [PDF](https://documents.alicat.com/Alicat-Serial-Primer.pdf) · [vendor page](https://www.alicat.com/basisoem) |

Written against these revisions; the SHA-256 of each file is in [`docs/datasheets.yml`](datasheets.yml). Where each part
of the driver is in the datasheet: [Datasheet map](datasheet.md).
<!-- /generated -->

## Where to go next

<div class="grid cards" markdown>

-   :material-rocket-launch-outline: **[Quickstart](quickstart.md)**

    ---

    Wire the instrument, write the port, read a flow in about 20 lines.

-   :material-chip: **[Hardware](hardware.md)**

    ---

    The connector, wiring for RS-232 and RS-485, power, LED and the size classes.

-   :material-shield-alert-outline: **[Safety notes](safety.md)**

    ---

    Valve holds, the watchdog, saved setpoints, tare, and the guarded commands.

-   :material-code-braces: **[API guide](api-guide.md)**

    ---

    Start up, read, set, scan and recover, task by task.

-   :material-swap-horizontal: **[Porting](porting.md)**

    ---

    Implement the port, RS-485 direction and baud switching.

-   :material-book-open-variant: **[API reference](api-reference.md)**

    ---

    Every class and member, generated from the headers.

</div>
