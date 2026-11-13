/**
 * @file configuration.cpp
 * @brief The Configuration page's examples, compiled by the host build (never run).
 */
#include "alicat_basis2.hpp"
#include "alicat_basis2_kconfig.hpp"
#include "my_port.hpp"

// --8<-- [start:custom]
alicat_basis2::Config ModbusConfig() {
  alicat_basis2::Config c{}; // factory settings: unit ID 'A', 38400 baud, ASCII
  c.protocol = alicat_basis2::Protocol::ModbusRtu;
  c.modbus_address = 7;
  c.modbus_flow_decimals =
      1; // required for Modbus: the instrument's flow decimals are not readable there
  c.read_retries = 2;
  return c; // alicat_basis2::Validate(c) checks it without the instrument
}

alicat_basis2::Error StartModbus(MyPort& port) {
  static alicat_basis2::Bus<MyPort> bus(port);
  static alicat_basis2::Basis2<MyPort> meter(bus, ModbusConfig()); // stored, no I/O
  return meter.EnsureInitialized().error;
}
// --8<-- [end:custom]

// --8<-- [start:kconfig]
alicat_basis2::Error StartFromMenuconfig(MyPort& port) {
  static alicat_basis2::Bus<MyPort> bus(port);
  static alicat_basis2::Basis2<MyPort> meter(bus, alicat_basis2::KconfigDefaults());
  return meter.EnsureInitialized().error;
}
// --8<-- [end:kconfig]
