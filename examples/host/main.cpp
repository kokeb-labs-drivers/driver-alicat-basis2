/**
 * @file main.cpp
 * @brief The BASIS 2 driver on your PC: five short programs against a simulated instrument.
 *
 * No hardware and no SDK: the host port (host_alicat_basis2_port.hpp) connects the real driver to
 * the instrument model the host tests use. The same calls run on an MCU with that MCU's port
 * (examples/esp32/). Exit code 0 when every call succeeded.
 */
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "alicat_basis2.hpp"
#include "host_alicat_basis2_port.hpp"

namespace {

/// The unit IDs in order: bit i of `ScanUnitIds().found` is `kUnitIds[i]`.
constexpr std::string_view kUnitIds = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

using alicat_basis2::Error;

/// Print a failed call and return false.
template <typename R>
bool Ok(const R& result, const char* what) {
  if (!result.ok()) {
    static_cast<void>(std::printf("%s failed: %s\n", what, alicat_basis2::ToString(result.error)));
  }
  return result.ok();
}

// --8<-- [start:read_flow]
/// Read the flow, temperature, total and flags ten times, the way a logger at 10 Hz would.
bool ReadFlow() {
  model::Instrument chip;
  chip.flow_milli = 123400;
  HostPort port(chip);
  alicat_basis2::Bus<HostPort> bus(port);
  alicat_basis2::Basis2<HostPort> meter(
      bus); // 'A', 38400 baud, ASCII protocol: the factory settings

  if (!Ok(meter.EnsureInitialized(), "EnsureInitialized") || !Ok(meter.Begin(), "Begin")) {
    return false;
  }
  for (int i = 0; i < 10; ++i) {
    const auto frame = meter.ReadFrame();
    if (!Ok(frame, "ReadFrame")) {
      return false;
    }
    static_cast<void>(
        std::printf("%.1f %s, %.2f C, total %.1f, gas %s%s\n", frame.value.flow.ToDouble(),
                    meter.GetIdentity().unit_label.chars.data(), frame.value.temperature.ToDouble(),
                    frame.value.total.ToDouble(), frame.value.gas_name.data(),
                    frame.value.status.mass_flow_over ? " [MOV]" : ""));
    port.DelayMs(100);
  }
  return true;
}
// --8<-- [end:read_flow]

// --8<-- [start:controller_ramp]
/// Step a controller through a ramp with the unsaved setpoint source, hold the valve, and close it
/// on exit.
bool ControllerRamp() {
  model::Instrument chip;
  HostPort port(chip);
  alicat_basis2::Bus<HostPort> bus(port);
  alicat_basis2::Basis2<HostPort> controller(bus);
  if (!Ok(controller.EnsureInitialized(), "EnsureInitialized") ||
      !Ok(controller.Begin(), "Begin")) {
    return false;
  }
  // The unsaved source never wears the non-volatile memory; use it for anything that changes often.
  bool ok = Ok(controller.SetSetpointSource(alicat_basis2::SetpointSource::UnsavedDigital),
               "SetSetpointSource");
  ok = ok && Ok(controller.SetRamp(50.0, alicat_basis2::TimeUnit::Second), "SetRamp");
  for (const double target : {100.0, 250.0, 100.0}) {
    ok = ok && Ok(controller.SetSetpoint(target), "SetSetpoint");
  }
  ok = ok && Ok(controller.HoldValve(40.0), "HoldValve") &&
       Ok(controller.ResumeControl(), "ResumeControl");
  // Leave the instrument safe: no flow, no ramp.
  ok = ok && Ok(controller.DisableRamp(), "DisableRamp") &&
       Ok(controller.SetSetpoint(0.0), "SetSetpoint");
  return ok && Ok(controller.CloseValve(), "CloseValve");
}
// --8<-- [end:controller_ramp]

// --8<-- [start:tare_and_total]
/// Tare on zero flow, reset the totalizer, set a batch.
bool TareAndTotal() {
  model::Instrument chip;
  HostPort port(chip);
  alicat_basis2::Bus<HostPort> bus(port);
  alicat_basis2::Basis2<HostPort> controller(bus);
  if (!Ok(controller.EnsureInitialized(), "EnsureInitialized") ||
      !Ok(controller.Begin(), "Begin")) {
    return false;
  }
  // Only tare with no flow through the instrument: it takes the present reading as zero (M p.15).
  bool ok = Ok(controller.Tare(1000), "Tare") && Ok(controller.ResetTotal(), "ResetTotal");
  ok = ok && Ok(controller.SetTotalizerLimit(alicat_basis2::TotalizerLimit::HoldWithError),
                "SetTotalizerLimit");
  return ok && Ok(controller.SetBatch(25.0), "SetBatch") &&
         Ok(controller.ReadBatchRemaining(), "ReadBatchRemaining");
}
// --8<-- [end:tare_and_total]

// --8<-- [start:scan_bus]
/// Find the instruments on a line: 26 polls, one per unit ID. This call blocks for the whole scan.
bool ScanBus() {
  model::Instrument chip;
  chip.unit_id = 'F';
  HostPort port(chip);
  alicat_basis2::Bus<HostPort> bus(port);
  alicat_basis2::Config config;
  config.unit_id = 'F';
  alicat_basis2::Basis2<HostPort> meter(bus, config);
  if (!Ok(meter.EnsureInitialized(), "EnsureInitialized") || !Ok(meter.Begin(), "Begin")) {
    return false;
  }
  const auto scan = meter.ScanUnitIds();
  if (!Ok(scan, "ScanUnitIds")) {
    return false;
  }
  for (uint32_t i = 0U; i < static_cast<uint32_t>(kUnitIds.size()); ++i) {
    const char id = kUnitIds[i]; // bit i of `found` is the i-th unit ID, A to Z
    if ((scan.value.found & (1U << i)) != 0U) {
      static_cast<void>(std::printf("instrument with unit ID %c\n", id));
    }
  }
  return scan.value.found == (1U << static_cast<uint32_t>(kUnitIds.find('F')));
}
// --8<-- [end:scan_bus]

// --8<-- [start:modbus_readout]
/// The same readout over Modbus RTU. The factory flow decimals are not readable over Modbus: say
/// them here.
bool ModbusReadout() {
  model::Instrument chip;
  chip.flow_milli = 12300;
  HostPort port(chip);
  alicat_basis2::Bus<HostPort> bus(port);
  alicat_basis2::Config config;
  config.protocol = alicat_basis2::Protocol::ModbusRtu;
  config.modbus_address = 1;
  config.modbus_flow_decimals = 1; // required: there is no default
  alicat_basis2::Basis2<HostPort> meter(bus, config);
  if (!Ok(meter.EnsureInitialized(), "EnsureInitialized") || !Ok(meter.Begin(), "Begin")) {
    return false;
  }
  const auto flow = meter.ReadFlow();
  if (!Ok(flow, "ReadFlow")) {
    return false;
  }
  static_cast<void>(std::printf("Modbus: %.1f, full scale %.1f\n", flow.value.ToDouble(),
                                meter.GetIdentity().full_scale.ToDouble()));
  return flow.value.ToMilli() == 12300;
}
// --8<-- [end:modbus_readout]

} // namespace

int main() {
  static_cast<void>(
      std::printf("BASIS 2 driver %s on the host\n", alicat_basis2::GetDriverVersion()));
  const bool ok = ReadFlow() && ControllerRamp() && TareAndTotal() && ScanBus() && ModbusReadout();
  return ok ? 0 : 1;
}
