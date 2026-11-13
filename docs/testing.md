# Testing and CI

How the driver is checked: host tests against an independent model of the instrument and its serial wire, fault
injection, four fuzz targets with checked invariants, compiled documentation, an install test, ESP32 and Cortex-M
builds, static analysis and measured quality gates, on every pull request.

## Host tests

The tests build the real driver against a model of a BASIS 2 instrument. The model
([`tests/mock_basis2.hpp`](../tests/mock_basis2.hpp)) is written from the manual, not from the driver: it has its own
ASCII grammar with plain strings and its own Modbus register file with plain integers, and shares no encoder,
tokenizer or table with the code under test. A wrong constant in the driver therefore cannot hide behind the same
wrong constant in the model (handbook KB-TST-030). The wire has a virtual clock in microseconds, ten bits per
character at the instrument's baud rate, and the instrument's wait of 3.5 characters of silence before it acts.

The model refuses to hear a reply sent at another speed than the port listens at. Knobs added for the safety tests:
K-IDIGNORE (a unit ID change answers but changes nothing) and K-BAUDREJECT (a baud change is refused and the speed
stays); the model also gates every Modbus register on the firmware that added it.

Every assumption of the manual is a knob of the model, so a test can show that the driver survives either answer:
column-aligned or single-spaced frames, bracketed or bare flags, an error reply with or without the unit ID, the
format of the firmware text, the reply of the setters, a status flag given as an integer. The tests build with the
firmware's flags, `-fno-exceptions -fno-rtti`, and warnings as errors, on GCC and Clang.

```bash
cmake -S . -B build -DALICAT_BASIS2_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

| File | Covers |
| --- | --- |
| [`tests/test_grammar.cpp`](../tests/test_grammar.cpp) | The request factories, the independent wire validators and the golden strings of every command |
| [`tests/test_decode.cpp`](../tests/test_decode.cpp) | The reply parsers: frames, `DV`, gas list, text replies, numbers, flags, malformed and hostile input |
| [`tests/test_api_ascii.cpp`](../tests/test_api_ascii.cpp) | The typed API over ASCII: start-up, reads, setpoints, ramp, hold, tare, gas, totalizer, scans, recovery |
| [`tests/test_api_modbus.cpp`](../tests/test_api_modbus.cpp) | The same over Modbus RTU, with the decimals, the firmware gates and the register table |
| [`tests/test_modbus_codec.cpp`](../tests/test_modbus_codec.cpp) | CRC-16, frame building, the legality check, reply lengths and exception frames |
| [`tests/test_wire.cpp`](../tests/test_wire.cpp) | The transaction machine: the gap, the deadlines, chunked and late replies, quarantine, stray bytes, clock wrap, hold-offs |
| [`tests/test_guarded.cpp`](../tests/test_guarded.cpp) | The six guarded commands, every confirmation outcome, `StateUnknown` and `Recover()` |
| [`tests/test_safety_kills.cpp`](../tests/test_safety_kills.cpp) | Behaviours a mutation run found unguarded: no write repeat, slow-command wait, gas verification, `Recover()` refusals, the unknown-source rate limit, a switch-off in doubt (unit ID, baud, address, factory restore), the saved-setpoint limit after a lost reply, character and length boundaries, the forged-request refusal |
| [`tests/check_doc_symbols.py`](../tests/check_doc_symbols.py) | Every API name a page cites in backticks exists in `inc/` or `src/` (CTest `alicat_basis2_doc_symbols`) |
| [`tests/test_errors.cpp`](../tests/test_errors.cpp) | The base `Error` values are 0 to 9; every chip-specific value is returned by some call and has a name |
| [`tests/test_version.cpp`](../tests/test_version.cpp) | The version getters follow the generated header (also against a 9.8.7 probe) |
| [`tests/test_kconfig.cpp`](../tests/test_kconfig.cpp) | `KconfigDefaults()` with and without `CONFIG_` values |
| [`tests/compile_fail/`](../tests/compile_fail/guarded_without_token.cpp) | Code that must not compile, each for its `EXPECT:` diagnostic: a guarded call without its token, a request built by hand, a hook with the wrong signature, a port without `NowMs()`, a configuration that is invalid in a constant expression |
| [`tests/fuzz/`](../tests/fuzz/fuzz_common.hpp) | The four fuzz targets; CTest replays their corpus (`alicat_basis2_fuzz_*_corpus`) |
| [`tests/check_version_copy.cmake`](../tests/check_version_copy.cmake) | `_config/Doxyfile` carries the CMake version |
| [`examples/host/`](../examples/host/README.md) | The host example, built with the test flags and run (`alicat_basis2_host_example`) |
| [`tests/check_install.cmake`](../tests/check_install.cmake) | Installs into a scratch prefix and builds [`tests/install_consumer/`](../tests/install_consumer/main.cpp) against it |
| [`tests/check_doxygen.cmake`](../tests/check_doxygen.cmake) | Doxygen with `_config/Doxyfile` and warnings as errors, when `doxygen` is installed |

### Fault injection

The model's wire injects, per transaction or from a given call on: a dropped reply, a delayed reply, a reply split into
chunks, a missing carriage return, extra and prefixed and suffixed bytes, a duplicated reply, a reply with the wrong
unit ID, a replaced line, a line feed after the carriage return, a bad CRC, a Modbus frame for the wrong address
(with a valid CRC), an error reply, a garbled byte, stray bytes between calls, a power cycle, a clamped setpoint and a
port that fails to write, flush or initialise. The tests fail every transfer in turn and check that every call returns
the right error, leaves the bus usable, and never sends an unlisted command.

## Fuzz tests

Everything the driver reads comes from the instrument, so the data read is what a noisy cable, a wrong baud rate or a
second instrument controls. There are four libFuzzer targets in [`tests/fuzz/`](../tests/fuzz/fuzz_common.hpp), each with
a seed corpus, replayed by CTest on every host build so a regression shows up without a fuzzing run:

| Target | What it feeds |
| --- | --- |
| `fuzz_ascii_replies` | Arbitrary bytes, split at fuzz-chosen points, as the reply to each ASCII command |
| `fuzz_ascii_session` | A random sequence of API calls, guarded ones with their tokens, against the model with random faults |
| `fuzz_modbus_replies` | Arbitrary bytes as the reply frame to each Modbus request |
| `fuzz_modbus_session` | The same session against the register model |

A wrapper port records every byte written and every clock read, and after every call the harness checks the
invariants marked "fuzz" below. A violation aborts the run. The others are proven by directed tests
([`test_wire.cpp`](../tests/test_wire.cpp), [`test_decode.cpp`](../tests/test_decode.cpp)) with a virtual clock: a
reply that arrives late, a clock that wraps, a decode that round-trips.

| ID | Checked by | Invariant |
| --- | --- | --- |
| I1 | fuzz | Every `Write()` buffer passes the independent validator of the grammar and ends the line correctly |
| I2 | test_decode | A successful decode round-trips: encoding the decoded numbers again gives the numbers parsed |
| I3 | fuzz | Nothing is written while a transaction waits, and the gap to the previous byte is at least 3.5 characters |
| I4 | test_wire | After a failed transaction the next write comes only after the quarantine; a late reply never answers a later command |
| I5 | fuzz | The six guarded texts never appear unless the harness made the matching guarded call |
| I6 | fuzz | No setpoint above 102.5 % of full scale, none negative, no valve hold above 100, no saved setpoint too soon |
| I7 | fuzz | Decoded values stay in their bounds; no read beyond the line buffer (under the sanitizers) |
| I8 | fuzz | A failed call returns a zero-valued result; a frame is exposed only for the unit ID addressed |
| I9 | test_wire, test_bounds, test_busy, fuzz | The machine makes progress or ends within its deadlines: a request whose line is not quiet within one reply wait ends `Busy` unsent; the fuzz targets bound every call by ten seconds, a scan's polls and a hold-off still running |
| I10 | fuzz | Modbus writes use functions 6 and 16 only, on writable registers, never to address 0, with valid CRC and legal magic values |
| I11 | test_wire, test_clock, fuzz | A clock wrap, a start in the clock's upper half or more than 2^31 ms of quiet never causes an early send, a lost send or an endless wait |
| I12 | fuzz | `StateUnknown` is left only by a successful `Recover()`; asserted after every step with `IsStateUnknown()` |
| I14 | fuzz | While `StateUnknown` the wire carries only a read request of the grammar, a zero setpoint or hold, or `P2` (Modbus: reads, a zero setpoint, register 56 = 0) |
| I13 | fuzz | A command sent into a reply still on the wire happens only in a session that chose a fault (ASCII) |
| I15 | fuzz | On a line declared to hold two instruments the unit ID, address, baud and factory-restore commands never reach the wire |
| I16 | fuzz | `SetSetpoint(0.0)` puts a frame on the wire unless it returns `NotInitialized`, `Unsupported`, `FirmwareTooOld`, or `Busy` while bytes were still arriving on the line |

Run a target locally with Clang:

```bash
cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DALICAT_BASIS2_BUILD_TESTS=ON -DALICAT_BASIS2_BUILD_FUZZERS=ON
cmake --build build-fuzz
build-fuzz/tests/fuzz/fuzz_ascii_session tests/fuzz/corpus/fuzz_ascii_session -max_total_time=60
```

## Quality gates

The `Host quality` job measures the library code, `inc/` and `src/` only (tests and examples are left out), and
fails when a value is over the limits of this repository's assurance level, `high` (set in
[`.kokeb/handbook.yml`](../.kokeb/handbook.yml), defined by the handbook's ADR-0020):

| Gate | Tool and how it is measured | Limit at `high` |
| --- | --- | --- |
| Complexity | lizard 1.24.1, modified cyclomatic complexity (`-m`: a `switch` counts once) per function | CCN at most 15 |
| Function length | lizard, lines of code per function | at most 60 |
| Parameters | lizard, per function | at most 6 |
| Line coverage | GCC 14 `--coverage -O0`, every CTest, gcovr 8.7 `--merge-lines` (a template's lines count once across its instances), `--exclude-unreachable-branches --exclude-throw-branches` | at least 85 % |
| Branch coverage | same run | at least 75 % |
| MC/DC | GCC `-fcondition-coverage`, same run | measured, not gated at `high` (100 % at `safety`) |
| Sanitizers | Clang, `-fsanitize=address,undefined -fno-sanitize-recover=all`, every CTest | no report |
| Fuzzing | libFuzzer with ASan and UBSan, 120 s per target | no broken promise |

The reports are in the run's `host-quality-reports` artifact and its step summary. To run the gates locally:

```bash
# Coverage (GCC 14 or later), then the complexity limits
cmake -S . -B build-cov -DCMAKE_CXX_COMPILER=g++-14 -DALICAT_BASIS2_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="--coverage -O0 -fcondition-coverage"
cmake --build build-cov && ctest --test-dir build-cov
gcovr --root . --filter inc/ --filter src/ --merge-lines --exclude-unreachable-branches --txt build-cov
lizard -m -C 15 -L 60 -a 6 inc src

# Sanitizers (Clang)
cmake -S . -B build-san -DCMAKE_CXX_COMPILER=clang++ -DALICAT_BASIS2_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -O1 -g"
cmake --build build-san && ctest --test-dir build-san --output-on-failure
```

The fuzz gate runs locally as shown in [Fuzz tests](#fuzz-tests).

clang-tidy 20 ([`_config/.clang-tidy`](../_config/.clang-tidy): bugprone, CERT, clang-analyzer,
cppcoreguidelines, modernize, performance, portability and readability, each disabled check with its reason) and
cppcheck (strict) run in the `C++ lint` and `Repository checks` jobs and fail on any finding.

The `MISRA` job ([`ci-misra.yml`](../.github/workflows/ci-misra.yml)) runs the shared `ru-misra` workflow of
ci-tools at level `high`: it traces a host build and the Cortex-M build (`alicat_basis2_optimised_build`) of every
translation unit, checks them against MISRA C++:2023 and the handbook's C++20 profile (the CodeQL coding-standards
and profile packs, plus the validated clang-tidy and cppcheck checks), and reports each finding against the
handbook's compliance matrix and deviation records. A finding is fixed in the code; a deliberate exception names
its deviation on the line before it (`// codeql::misra_deviation_next_line(MD-CPP-009)` above the `.ipp` include
of `inc/alicat_basis2.hpp`). Until the repository is public the job runs with `open_source_prerelease: true`, the
declaration that the code is released under the licence in `LICENSE` and is to be published.

## Checks switched off for the tests

`tests/.clang-tidy` inherits the organisation's configuration and turns these checks off, because the code they
would change is test scaffolding that is never shipped. The driver in `inc/` and `src/` has no suppression and no
relaxation.

| Check | Reason |
| --- | --- |
| `readability-make-member-function-const` | The mock port hooks keep the port contract's non-const signature. |
| `clang-analyzer-optin.performance.Padding` | The mock chip lists its registers in the manual's order, not the best packing. |
| `clang-analyzer-optin.core.EnumCastOutOfRange` | The tests cast out-of-range values on purpose to reach the rejection paths. |
| `cppcoreguidelines-pro-type-reinterpret-cast` | The tests view a string's bytes as `uint8_t` to build frames. |
| `cppcoreguidelines-pro-type-member-init` | Golden-case structs are filled by aggregate initialisation. |
| `performance-inefficient-string-concatenation` | Test messages and expected wires are built once. |

The template's own relaxations (`convert-member-functions-to-static`, `non-private-member-variables-in-classes`,
`macro-usage`, the C-array and designated-initialiser checks for the test registry, and `pro-type-vararg` for the
report printing) stay as they were.

## Documentation snippets

Every C++ block on this site is included from [`tests/docs/`](../tests/docs/my_port.hpp), which the host build
compiles (target `alicat_basis2_doc_snippets`) with the driver instantiated over the documented port. A change to
the API that breaks a page breaks the build first. The README's example is a generated copy of the same file;
`kokeb-docs check` fails when it drifts.

## CI workflows

Everything runs from [`ci.yml`](../.github/workflows/ci.yml), which calls one workflow file per check and ends in
the `CI gate` job, the single required check (`CI gate / gate`).

| Job | What it checks |
| --- | --- |
| Host tests | GCC, then Clang (one job), build and run every test above, then build and run `examples/host` on its own |
| Host quality | The gates above: complexity, coverage, sanitizers, fuzzing |
| Cross-compile | `arm-none-eabi-g++` for Cortex-M0+, M4F and M7F with the strict flags, warnings as errors (nothing linked) |
| C++ lint | clang-format and clang-tidy over the sources |
| MISRA | MISRA C++:2023 and the C++20 profile on traced host and Cortex-M builds, at level `high` (see [Quality gates](#quality-gates)) |
| Docs | Strict site build (`_config/mkdocs.yml`), figure and generated-region drift, Doxygen (`_config/Doxyfile`), offline link check, Lighthouse |
| Repository checks | One job: DCO on pull requests, the org standard, Markdown and YAML lint, the [Kokeb Labs handbook](https://github.com/kokeb-labs/handbook) rules for `.kokeb/handbook.yml` (by rule ID), template drift (warns only) and cppcheck over the driver sources; a second job runs the handbook's source checks (include guards, bus requirements, raw access) |
| CI gate | Passes only if every job above passed: the one required check |

The **ESP32 examples** job builds every app in `examples/esp32/app_config.yml`, for each listed ESP-IDF version and
build type, with the repository root as the driver's component, and lints the example sources. The gate needs it
too.

The workflows call the shared, versioned workflows of
[kokeb-labs/ci-tools](https://github.com/kokeb-labs/ci-tools) at `@v1`.

## On hardware

Nothing has been run on an instrument. The MCU examples ([Examples](examples.md)) are the bench test: the minimal one
polls the flow once a second, the full one scans, identifies and sets a setpoint. A bench session also settles the
assumptions listed on the [Datasheet map](datasheet.md#assumptions-and-open-items): capture the replies of the setters,
`DV` with several items, `GS *`, `VE`, `SN` and the error reply, and the meter's data frame, with a terminal program,
and set the model's knobs to match. Over Modbus, read 4210 to 4215 after a measurement of a known average above
6553.5 (A25): the literal reading of M p.22 puts the low word in 4214, the driver decodes it high word first like every
other pair, and K-PREVAVG switches the model. Record the model, the firmware and the result in the pull request's bench-status
section.
