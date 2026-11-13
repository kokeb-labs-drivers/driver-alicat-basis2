# Changelog

All notable changes to this repository are listed here. Versions follow [Semantic Versioning](https://semver.org/).
Entries under **Breaking** need source changes in code that used the affected API; any of them calls for a major
version bump (a minor bump before 1.0).

## [Unreleased]

### Added

- BASIS 2 driver, first version: the Alicat BASIS 2 gas mass flow meters (B series) and controllers (BC series) over a
  serial line.
- ASCII "BASIS Protocol 2" as the default protocol and Modbus RTU in full behind the same typed API
  (`Config::protocol`); the Modbus flow decimals are a required field with no default.
- `Bus<Port>`: one serial line, one transaction at a time, with the 3.5 character gap, deadlines from the baud rate,
  quarantine after a failed transaction, stray line detection and a non-blocking `Poll()`; `Basis2<Port>`: one
  instrument, up to 26 per line.
- Six guarded commands (`ChangeUnitId`, `SetModbusAddress`, `ChangeBaud`, `SwitchToProtocol1`, `SwitchToProtocol2`,
  `FactoryRestore`), each behind a confirmation token; `Recover()` and the two scans.
- Tests against an independent model instrument with fault injection, four fuzz targets and a corpus, compile-fail
  tests, a host example and ESP-IDF examples (minimal and RS-485 full).
