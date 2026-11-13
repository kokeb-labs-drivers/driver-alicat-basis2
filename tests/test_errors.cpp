/**
 * @file test_errors.cpp
 * @brief The `Error` enum follows the standard, every chip-specific value is returned by a real
 * call, and the configuration bounds hold.
 *
 * Every chip-specific value (16 and up) MUST be returned by some call: the enumerators are found
 * through `ToString()` (-Wswitch-enum keeps it complete), so a new value fails this test until a
 * scenario below produces it. `DeviceFault` (8) is reserved: this instrument has no fault reply of
 * its own, and the API guide says so.
 */
#include <array>
#include <cstring>

#include "rig.hpp"

using alicat_basis2::Error;
using rig::E;

static_assert((static_cast<int>(Error::Ok) == 0) &&
                  (static_cast<int>(Error::NotInitialized) == 1) &&
                  (static_cast<int>(Error::InvalidArgument) == 2) &&
                  (static_cast<int>(Error::Bus) == 3) && (static_cast<int>(Error::Timeout) == 4) &&
                  (static_cast<int>(Error::DeviceIdMismatch) == 5) &&
                  (static_cast<int>(Error::Crc) == 6) && (static_cast<int>(Error::Busy) == 7) &&
                  (static_cast<int>(Error::DeviceFault) == 8) &&
                  (static_cast<int>(Error::Unsupported) == 9),
              "the base Error set has fixed values (standards/driver.md)");

namespace {

constexpr int kFirstChipSpecific = 16;

/// The error values a case has returned, kept in one function-local table.
std::array<bool, 256>& Seen() {
  static std::array<bool, 256> seen{};
  return seen;
}

void Record(Error e) {
  Seen()[static_cast<uint8_t>(e)] = true;
}

void RunScenarios() {
  {
    model::FullRig r;
    Record(r.drv.ReadFlow().error); // Ok: reads need no Begin
    r.inst.fail_init = true;
    Record(r.drv.EnsureInitialized().error); // Bus
    Record(r.drv.Begin().error);             // NotInitialized
    alicat_basis2::Config bad{};
    bad.unit_id = '@';
    model::FullRig b(bad);
    Record(b.drv.EnsureInitialized().error); // InvalidArgument
  }
  {
    model::FullRig r;
    static_cast<void>(r.drv.EnsureInitialized());
    static_cast<void>(r.drv.Begin());
    r.inst.faults = model::Faults{};
    r.inst.faults.count = 1;
    r.inst.faults.drop = true;
    Record(r.drv.SetGas(alicat_basis2::GasId{1}).error); // Timeout
    r.inst.faults = model::Faults{};
    r.inst.faults.count = 1;
    r.inst.faults.wrong_id = 'Q';
    Record(r.drv.SetGas(alicat_basis2::GasId{1}).error); // DeviceIdMismatch
    r.inst.faults = model::Faults{};
    r.inst.faults.count = 1;
    r.inst.faults.garble_at = 2;
    Record(r.drv.SetGas(alicat_basis2::GasId{1}).error); // BadReply
    r.inst.faults = model::Faults{};
    r.inst.faults.count = 1;
    r.inst.faults.error_reply = true;
    Record(r.drv.SetGas(alicat_basis2::GasId{1}).error); // Rejected
    r.inst.faults = model::Faults{};
    r.inst.faults.count = 1;
    r.inst.faults.extend = 400;
    r.inst.faults.count = 2;        // the retry of a read meets it again
    Record(r.drv.ReadFlow().error); // Overflow
    r.inst.flow_milli = 900000;
    Record(r.drv.ReadFlow().error); // OutOfRange
    r.inst.flow_milli = 0;
    r.inst.clamp_setpoint_milli = 1000;
    Record(r.drv.SetSetpoint(100.0).error); // VerifyMismatch
    r.inst.clamp_setpoint_milli = -1;
    Record(r.drv.SetSetpointSource(alicat_basis2::SetpointSource::SavedDigital).error);
    static_cast<void>(r.drv.SetSetpoint(1.0));
    Record(r.drv.SetSetpoint(2.0).error); // RateLimited
    Record(r.drv.ReadFlowUnit().error);   // Unsupported
    Record(r.drv.SetSetpoint(-1.0).error);
    r.bus.SetDeviceCount(2);
    Record(
        r.drv.ChangeUnitId('C', alicat_basis2::ConfirmUnitIdChange{}).error); // GuardNotConfirmed
    r.bus.SetDeviceCount(1);
    r.inst.faults = model::Faults{};
    r.inst.faults.count = 1;
    r.inst.faults.drop = true;
    Record(r.drv.ChangeUnitId('C', alicat_basis2::ConfirmUnitIdChange{}).error); // StateUnknown
  }
  {
    model::FullRig r(model::ModbusConfig());
    static_cast<void>(r.drv.EnsureInitialized());
    static_cast<void>(r.drv.Begin());
    r.inst.faults.count = 2;
    r.inst.faults.bad_crc = true;
    Record(r.drv.ReadFlow().error); // Crc
    r.inst.faults = model::Faults{};
    r.inst.firmware = 0x210;
    static_cast<void>(r.drv.Begin());
    Record(r.drv.ReadGasTable().error); // FirmwareTooOld
  }
  {
    model::FullRig r;
    static_cast<void>(r.drv.EnsureInitialized());
    const auto made = alicat_basis2::ascii::Make(alicat_basis2::Command::Poll, 'A',
                                                 alicat_basis2::ascii::Args{}, false);
    alicat_basis2::Timing t{};
    Record(r.bus.Submit(made.value, t));
    Record(r.bus.Submit(made.value, t)); // Busy
  }
}

} // namespace

TEST_CASE("errors: every chip-specific value is returned by some call, each has a name") {
  RunScenarios();
  for (int v = 0; v < 256; ++v) {
    const auto e = static_cast<Error>(v);
    const char* name = alicat_basis2::ToString(e);
    const bool named = std::strcmp(name, "Unknown") != 0;
    if ((v >= kFirstChipSpecific) && named) {
      CHECK(Seen()[static_cast<std::size_t>(v)]);
    }
    if (Seen()[static_cast<std::size_t>(v)]) {
      CHECK(named);
    }
  }
  for (int v = 0; v <= 9; ++v) {
    if (v != 8) { // DeviceFault is reserved
      CHECK(Seen()[static_cast<std::size_t>(v)]);
    }
  }
  CHECK_STR(alicat_basis2::ToString(static_cast<Error>(200)), "Unknown");
}

TEST_CASE("config: Validate accepts the defaults and refuses every out-of-range field") {
  using alicat_basis2::Config;
  CHECK(alicat_basis2::Validate(Config{}) == Error::Ok);
  const auto bad = [](auto edit) {
    Config c{};
    edit(c);
    return alicat_basis2::Validate(c) == Error::InvalidArgument;
  };
  CHECK(bad([](Config& c) { c.unit_id = '@'; }));
  CHECK(bad([](Config& c) { c.unit_id = '['; }));
  CHECK(bad([](Config& c) { c.modbus_address = 0; }));
  CHECK(bad([](Config& c) { c.modbus_address = 248; }));
  CHECK(bad([](Config& c) { c.baud = 12345; }));
  CHECK(bad([](Config& c) { c.modbus_flow_decimals = 7; }));
  CHECK(bad([](Config& c) { c.setpoint_decimals = 7; }));
  CHECK(bad([](Config& c) { c.temperature_min_c = 200; }));
  CHECK(bad([](Config& c) { c.processing_ms = 0; }));
  CHECK(bad([](Config& c) { c.slow_command_ms = 10; }));
  CHECK(bad([](Config& c) { c.restore_holdoff_ms = 0; }));
  CHECK(bad([](Config& c) { c.flow_margin_permille = 999; }));
  CHECK(bad([](Config& c) { c.protocol = static_cast<alicat_basis2::Protocol>(2); }));
  CHECK(bad([](Config& c) { c.kind = static_cast<alicat_basis2::DeviceKind>(3); }));
  CHECK(bad([](Config& c) { c.frame_layout = static_cast<alicat_basis2::FrameLayout>(3); }));
  CHECK(bad([](Config& c) { c.unknown_flags = static_cast<alicat_basis2::UnknownFlagPolicy>(2); }));
  CHECK(bad([](Config& c) { c.echo = static_cast<alicat_basis2::EchoMode>(2); }));
  CHECK(bad([](Config& c) { c.verify_setpoint = static_cast<alicat_basis2::VerifyPolicy>(2); }));
  CHECK(bad([](Config& c) { c.firmware_gate = static_cast<alicat_basis2::FirmwareGate>(2); }));
  for (const uint32_t b : {4800U, 9600U, 19200U, 38400U, 57600U, 115200U}) {
    Config c{};
    c.baud = b;
    CHECK(alicat_basis2::Validate(c) == Error::Ok);
  }
  static_assert(alicat_basis2::Validate(Config{}) == Error::Ok, "constexpr validation");
}

TEST_CASE("config: the defaults are the instrument's factory values and the documented knobs") {
  const alicat_basis2::Config c{};
  CHECK_CHAR(c.unit_id, 'A');
  CHECK(c.protocol == alicat_basis2::Protocol::Ascii);
  CHECK_EQ(c.baud, 38400U);
  CHECK_EQ(c.modbus_address, 1U);
  CHECK_EQ(c.modbus_flow_decimals, alicat_basis2::kDecimalsUnset);
  CHECK_EQ(c.saved_setpoint_min_interval_ms, 180000U);
  CHECK(c.verify_setpoint == alicat_basis2::VerifyPolicy::WhenNoRamp);
  CHECK(c.firmware_gate == alicat_basis2::FirmwareGate::Enforce);
  CHECK_EQ(c.read_retries, 1U);
}

TEST_CASE("config: an invalid configuration is refused by the constructor path, nothing is sent") {
  alicat_basis2::Config bad{};
  bad.baud = 1;
  model::FullRig r(bad);
  CHECK_EQ(E(r.drv.EnsureInitialized().error), E(Error::InvalidArgument));
  CHECK_EQ(r.inst.writes.size(), 0U);
  CHECK(!r.drv.IsInitialized());
}
