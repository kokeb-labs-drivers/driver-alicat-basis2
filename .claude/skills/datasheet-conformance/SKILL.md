---
name: datasheet-conformance
description: Check driver code against the chip's datasheet, citing sections, test first. Use when writing or reviewing register maps, timing, reset values or error handling.
---

# Datasheet conformance

The datasheet: not linked yet. Fill in `docs/datasheets.yml` (vendor, number, revision, date, `url`,
`pdf`, `sha256` and the topics); never commit the vendor PDF.

`docs/datasheets.yml` is the record of that source (handbook ADR-0019): fill in the document (vendor, number,
revision, date, `url`, `pdf` as a stable direct link or `null`, and the `sha256` of the PDF you used; never commit
the PDF) and one topic per thing the driver takes from it (section, printed page, driver API, docs pages). A driver
made from the template starts with the fictional ALICAT_BASIS2 placeholder there: replace all of it. Then run
`kokeb-docs gen`, which rebuilds the README card, the Datasheet map and the box on each page.

`docs/hardware.yml` is the same for the chip's hardware: the package, the pins, the hooks, the blocks, the
circuit, and the package facts the README's chip card and the outline figure are drawn from. Take
`package.outline` (body length and width in millimetres, height, pitch) from the datasheet's package outline
drawing and `package.variants` (the other packages, with their part-number suffixes) from its ordering
information; give each its section and printed page, and leave a number out when the datasheet does not state it.
`kokeb-docs hardware --check` validates the file; `kokeb-docs figures && kokeb-docs gen` redraw from it.

Every number in the driver has a source. Work one subsystem at a time (reset, identification, one mode, one
register block).

1. **Collect the facts** from the datasheet into a table in the PR description: register, address, bits,
   reset value, access (R, W, R/W, self-clearing, clear-on-read), timing (minimum, typical, maximum), and the
   section, table or figure number. Use the maximum for waits and the minimum for pulses.
2. **Write the model first.** Make `tests/mock_<chip>.hpp` follow the table: reset values, read-only and
   self-clearing bits, clear-on-read flags, timing, invalid writes.
3. **Write the tests** that pin each fact: reset values read back after `EnsureInitialized()`, the exact bytes of each
   frame, the waits (sum of `DelayUs()` against the datasheet maximum), every documented error condition.
4. **Then change the driver** until the tests pass. Each register access and wait carries a comment with the
   section (`// t_RST, section 8.2`); add or update the topic for it in `docs/datasheets.yml`, with the docs pages
   that explain it, so the page boxes and the Datasheet map cite the same section.
5. **Cross-check** `Config{}` against the reset values, `Validate()` against the documented ranges, and every
   `Error` against a datasheet condition or a port failure.
6. **Mark what you could not verify**: values the datasheet leaves open, errata, anything inferred. Say so in the
   code comment and the PR, and list it under "not verified on hardware" with a bench step.

Never invent a register, bit or timing. If the datasheet is silent, ask, or leave the feature out.

Write what the bench taught as the driver's design: the behaviour, the datasheet section that explains it, and
the test that pins it. Where no section explains it, write it as a design decision with its reasoning and a bench
step. The repository describes this chip only: name no other board, product, project or driver, and keep dated
history out of the code, docs and CHANGELOG entries (`tools/check_standalone.py`).
