/**
 * @file test_safety_kills.cpp
 * @brief Tests for the safety behaviours that a mutation run found unguarded: no repeat of a Modbus
 * write, the wait for a slow command, write-access gating, gas verification, the unknown-source
 * rate limit, and `Recover()` refusing to declare the instrument known without proof.
 */
#include <array>
#include <cstdio>

#include "rig.hpp"

using alicat_basis2::Error;
using rig::E;
using rig::Start;

TEST_CASE("kill: a Modbus write is never repeated after a lost reply") {
  model::FullRig r(model::ModbusConfig());
  REQUIRE(Start(r));
  const std::size_t n = r.inst.writes.size();
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  const auto t = r.drv.ResetTotal();
  CHECK(!t.ok());
  CHECK_EQ(r.inst.writes.size() - n, 1U);
}

TEST_CASE("kill: a slow command waits slow_command_ms for its reply") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.delay_us = 600000;
  CHECK(r.drv.SetWatchdog(10).ok());
}

TEST_CASE("kill: SetGas reports VerifyMismatch when the instrument names another gas") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.replace = "A 5 N2O";
  CHECK_EQ(E(r.drv.SetGas(alicat_basis2::GasId{1}).error), E(Error::VerifyMismatch));
}

TEST_CASE("kill: SetGas with an undecodable reply is not reported as set") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.replace = "A hello world";
  CHECK(!r.drv.SetGas(alicat_basis2::GasId{1}).ok());
}

TEST_CASE("kill: a write of 0 to a read-only register is never built") {
  CHECK_EQ(E(alicat_basis2::modbus::BuildWriteSingle(1, 25, 0).error), E(Error::InvalidArgument));
  CHECK_EQ(E(alicat_basis2::modbus::BuildWriteSingle(1, 2101, 0).error), E(Error::InvalidArgument));
  CHECK_EQ(E(alicat_basis2::modbus::BuildWriteSingle(1, 32, 0).error), E(Error::InvalidArgument));
}

TEST_CASE("kill: a failed Recover leaves the driver unready and writes refused") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  static_cast<void>(r.drv.ChangeUnitId('C', alicat_basis2::ConfirmUnitIdChange{}));
  r.inst.faults.count = 100000;
  r.inst.faults.drop = true;
  CHECK(!r.drv.Recover().ok());
  CHECK(r.drv.IsStateUnknown());
  CHECK(!r.drv.IsReady());
  r.inst.faults = model::Faults{};
  const std::size_t n = r.inst.writes.size();
  CHECK_EQ(E(r.drv.SetGas(alicat_basis2::GasId{1}).error), E(Error::StateUnknown));
  CHECK_EQ(E(r.drv.ResetTotal().error), E(Error::StateUnknown));
  CHECK_EQ(E(r.drv.SetWatchdog(10).error), E(Error::StateUnknown));
  CHECK_EQ(r.inst.writes.size(), n);
}

TEST_CASE("kill: Recover on a bus with several instruments never adopts another unit ID") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.bus.SetDeviceCount(2);
  r.inst.unit_id = 'F';
  CHECK(!r.drv.Recover().ok());
  CHECK_CHAR(r.drv.GetConfig().unit_id, 'A');
  CHECK(r.drv.IsStateUnknown());
}

TEST_CASE("kill: Recover refuses a different instrument that answers under the old ID") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.serial = "999999";
  const auto rec = r.drv.Recover();
  CHECK_EQ(E(rec.error), E(Error::VerifyMismatch));
  CHECK(r.drv.IsStateUnknown());
  CHECK(!r.drv.IsReady());
}

TEST_CASE("kill: unknown source is treated as saved: a second setpoint is rate limited") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  static_cast<void>(r.drv.SetSetpointSource(alicat_basis2::SetpointSource::UnsavedDigital));
  CHECK(r.drv.SetSetpoint(10.0).ok());
  CHECK_EQ(E(r.drv.SetSetpoint(11.0).error), E(Error::RateLimited));
}

TEST_CASE(
    "Begin over Modbus works on every firmware: a register the instrument lacks is never needed") {
  // The model answers Bad Address for every register below its "Added in V" firmware.
  constexpr std::array<uint16_t, 5> kFirmwares{0x210, 0x240, 0x255, 0x300, 0x305};
  for (const uint16_t fw : kFirmwares) {
    model::FullRig r(model::ModbusConfig());
    r.inst.firmware = fw;
    REQUIRE(Start(r));
    CHECK_EQ(r.drv.GetIdentity().firmware, fw);
  }
}

TEST_CASE("a Modbus read is as old as its newest register") {
  CHECK_EQ(alicat_basis2::modbus::detail::MinFirmwareOver(47, 2), 0x240U);
  CHECK_EQ(alicat_basis2::modbus::detail::MinFirmwareOver(2100, 10), 0x300U);
  CHECK_EQ(alicat_basis2::modbus::detail::MinFirmwareOver(25, 1), 0U);
  CHECK_EQ(alicat_basis2::modbus::detail::MinFirmwareOver(45, 4), 0x240U);
}

// --- A switch-off is never refused for doubt, and it reaches an instrument an unconfirmed command
// moved ---

namespace {
/// A rig whose unit-ID change was heard (the instrument is now `C`) but whose reply was lost.
void LoseUnitIdReply(model::FullRig& r) {
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  static_cast<void>(r.drv.ChangeUnitId('C', alicat_basis2::ConfirmUnitIdChange{}));
  r.inst.faults = model::Faults{};
}
} // namespace

TEST_CASE("switch-off in doubt: SetSetpoint(0) finds an instrument whose unit ID changed unseen") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.setpoint_milli = 5000;
  LoseUnitIdReply(r);
  CHECK(r.drv.IsStateUnknown());
  CHECK_CHAR(r.inst.unit_id, 'C');
  CHECK(r.drv.SetSetpoint(0.0).ok());
  CHECK_EQ(r.inst.setpoint_milli, 0);
  CHECK(r.drv.IsStateUnknown()); // the doubt ends only with Recover()
  CHECK_CHAR(r.drv.GetConfig().unit_id, 'C');
}

TEST_CASE("switch-off in doubt: CloseValve finds an instrument whose unit ID changed unseen") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.valve_milli = 50000;
  LoseUnitIdReply(r);
  CHECK(r.drv.CloseValve().ok());
  CHECK_EQ(r.inst.valve_milli, 0);
}

TEST_CASE("switch-off in doubt: a request that rounds to zero is still a switch-off") {
  model::FullRig r;
  REQUIRE(Start(r));
  LoseUnitIdReply(r);
  const std::size_t n = r.inst.writes.size();
  CHECK_EQ(E(r.drv.SetSetpoint(1.0).error), E(Error::StateUnknown)); // a switch-on is refused
  CHECK_EQ(r.inst.writes.size(), n);
  CHECK(r.drv.SetSetpoint(0.0004).ok()); // sent as 0.000: a switch-off
  CHECK(r.inst.writes.size() > n);
}

TEST_CASE("switch-off in doubt: success at the held settings is success, not StateUnknown") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.knobs.ignore_unit_change = true; // the instrument heard nothing: it is still at ID A
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  CHECK_EQ(E(r.drv.ChangeUnitId('C', alicat_basis2::ConfirmUnitIdChange{}).error),
           E(Error::StateUnknown));
  r.inst.faults = model::Faults{};
  r.inst.setpoint_milli = 5000;
  CHECK(r.drv.SetSetpoint(0.0).ok());
  CHECK_EQ(r.inst.setpoint_milli, 0);
  CHECK_CHAR(r.drv.GetConfig().unit_id, 'A');
}

TEST_CASE("switch-off in doubt: after a factory restore the instrument is back at the factory ID") {
  model::FullRig r;
  REQUIRE(Start(r));
  REQUIRE(r.drv.ChangeUnitId('B', alicat_basis2::ConfirmUnitIdChange{}).ok());
  // The restore answers under the factory ID, not the one the driver holds: not a clean success.
  static_cast<void>(r.drv.FactoryRestore(alicat_basis2::ConfirmFactoryRestore{}));
  CHECK(r.drv.IsStateUnknown());
  CHECK_CHAR(r.inst.unit_id, 'A');
  r.inst.Advance(5'000'000); // the instrument finishes restarting
  r.inst.source = 'U';       // the model restores the analog source, which ignores serial setpoints
  r.inst.setpoint_milli = 5000;
  CHECK(r.drv.SetSetpoint(0.0).ok());
  CHECK_EQ(r.inst.setpoint_milli, 0);
  CHECK_CHAR(r.drv.GetConfig().unit_id, 'A');
}

TEST_CASE("switch-off in doubt: after an unconfirmed baud change the instrument may run at the new "
          "rate") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  static_cast<void>(
      r.drv.ChangeBaud(alicat_basis2::Baud::B57600, alicat_basis2::ConfirmBaudChange{}));
  r.inst.faults = model::Faults{};
  CHECK_EQ(r.inst.baud, 57600U);
  CHECK_EQ(r.inst.port_baud, 38400U);
  r.inst.setpoint_milli = 5000;
  CHECK(r.drv.SetSetpoint(0.0).ok());
  CHECK_EQ(r.inst.setpoint_milli, 0);
  CHECK_EQ(r.inst.port_baud, 57600U);
}

TEST_CASE("switch-off in doubt: after an unconfirmed Modbus address change") {
  model::FullRig r(model::ModbusConfig());
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  static_cast<void>(r.drv.SetModbusAddress(7, alicat_basis2::ConfirmAddressChange{}));
  r.inst.faults = model::Faults{};
  CHECK_EQ(r.inst.modbus_address, 7U);
  r.inst.setpoint_milli = 5000;
  CHECK(r.drv.SetSetpoint(0.0).ok());
  CHECK_EQ(r.inst.setpoint_milli, 0);
  CHECK_EQ(r.drv.GetConfig().modbus_address, 7U);
}

TEST_CASE("switch-off in doubt: when nothing answers, the settings are as they were") {
  model::FullRig r;
  REQUIRE(Start(r));
  LoseUnitIdReply(r);
  r.inst.faults.count = 100000;
  r.inst.faults.drop = true;
  CHECK(!r.drv.SetSetpoint(0.0).ok());
  CHECK_CHAR(r.drv.GetConfig().unit_id, 'A');
  CHECK_EQ(r.drv.GetConfig().baud, 38400U);
}

// --- The saved-setpoint wear clock starts for every write that reached the wire ---

TEST_CASE("saved setpoint: a write whose reply was lost still starts the rate limit") {
  model::FullRig r;
  REQUIRE(Start(r));
  REQUIRE(r.drv.SetSetpointSource(alicat_basis2::SetpointSource::SavedDigital).ok());
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  CHECK_EQ(E(r.drv.SetSetpoint(10.0).error), E(Error::Timeout));
  r.inst.faults = model::Faults{};
  const std::size_t n = r.inst.writes.size();
  CHECK_EQ(E(r.drv.SetSetpoint(11.0).error), E(Error::RateLimited));
  CHECK_EQ(r.inst.writes.size(), n);
}

TEST_CASE("saved setpoint: a refused write does not start the rate limit") {
  model::FullRig r;
  REQUIRE(Start(r));
  REQUIRE(r.drv.SetSetpointSource(alicat_basis2::SetpointSource::SavedDigital).ok());
  r.inst.faults.count = 1;
  r.inst.faults.error_reply = true;
  CHECK_EQ(E(r.drv.SetSetpoint(10.0).error), E(Error::Rejected));
  r.inst.faults = model::Faults{};
  CHECK(r.drv.SetSetpoint(11.0).ok());
}

TEST_CASE("saved setpoint: the interval is the configured one, to the millisecond") {
  alicat_basis2::Config c{};
  c.saved_setpoint_min_interval_ms = 1000;
  model::FullRig r(c);
  REQUIRE(Start(r));
  REQUIRE(r.drv.SetSetpointSource(alicat_basis2::SetpointSource::SavedDigital).ok());
  REQUIRE(r.drv.SetSetpoint(10.0).ok());
  r.inst.Advance(900'000);
  CHECK_EQ(E(r.drv.SetSetpoint(11.0).error), E(Error::RateLimited));
  r.inst.Advance(200'000);
  CHECK(r.drv.SetSetpoint(11.0).ok());
}

// --- A unit-ID change is reported only when the instrument answers at the new ID ---

TEST_CASE(
    "unit ID change: an instrument that does not answer at the new ID is StateUnknown (ASCII)") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.knobs.ignore_unit_change = true; // the reply names the new ID, but nothing changed
  CHECK_EQ(E(r.drv.ChangeUnitId('C', alicat_basis2::ConfirmUnitIdChange{}).error),
           E(Error::StateUnknown));
  CHECK_CHAR(r.drv.GetConfig().unit_id, 'A');
  CHECK(r.drv.IsStateUnknown());
}

TEST_CASE("unit ID change: the read-back of register 46 must show the new ID (Modbus)") {
  model::FullRig r(model::ModbusConfig());
  REQUIRE(Start(r));
  r.inst.knobs.ignore_unit_change = true;
  CHECK_EQ(E(r.drv.ChangeUnitId('C', alicat_basis2::ConfirmUnitIdChange{}).error),
           E(Error::StateUnknown));
  CHECK(r.drv.IsStateUnknown());
}

// --- A reply from another unit never acknowledges a setter ---

TEST_CASE("a setter is not acknowledged by a reply from another unit ID") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.wrong_id = 'B';
  CHECK_EQ(E(r.drv.SetWatchdog(10).error), E(Error::DeviceIdMismatch));
  CHECK_EQ(r.drv.IsReady(), true);
}

TEST_CASE("a reply from another unit ID puts the bus in quarantine") {
  alicat_basis2::Config c{};
  c.read_retries = 0;
  model::FullRig r(c);
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.wrong_id = 'B';
  CHECK_EQ(E(r.drv.ReadFlow().error), E(Error::DeviceIdMismatch));
  CHECK_EQ(static_cast<int>(r.bus.State()), static_cast<int>(alicat_basis2::BusState::Settling));
}

TEST_CASE("an undecodable SetGas reply puts the bus in quarantine") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.replace = "A hello world";
  CHECK(!r.drv.SetGas(alicat_basis2::GasId{1}).ok());
  CHECK_EQ(static_cast<int>(r.bus.State()), static_cast<int>(alicat_basis2::BusState::Settling));
}

TEST_CASE("reads are repeated after a wrong-ID reply and after a bad reply, writes never") {
  alicat_basis2::Config c{};
  c.read_retries = 1;
  model::FullRig r(c);
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.wrong_id = 'B';
  CHECK(r.drv.ReadFlow().ok());
  r.inst.faults.count = 1;
  r.inst.faults.garble_at = 2;
  CHECK(r.drv.ReadFlow().ok());
  r.inst.faults.count = 1;
  r.inst.faults.wrong_id = 'B';
  CHECK(!r.drv.SetWatchdog(10).ok());
}

// --- Recover compares the instrument with the one it knew, and puts everything back on failure ---

TEST_CASE("Recover: another firmware is another instrument") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.firmware = 0x300;
  CHECK_EQ(E(r.drv.Recover().error), E(Error::VerifyMismatch));
  CHECK(r.drv.IsStateUnknown());
}

TEST_CASE("Recover: a meter is not the controller it replaced") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.controller = false;
  CHECK_EQ(E(r.drv.Recover().error), E(Error::VerifyMismatch));
}

TEST_CASE("Recover: a serial number that only starts the same is another instrument") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.serial = "SN12345";
  CHECK_EQ(E(r.drv.Recover().error), E(Error::VerifyMismatch));
}

TEST_CASE("Recover: a failed scan or a mismatch leaves the held ID and address as they were") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.unit_id = 'F';
  r.inst.serial = "OTHER";
  CHECK(!r.drv.Recover().ok()); // found at F, but it is another instrument
  CHECK_CHAR(r.drv.GetConfig().unit_id, 'A');
  r.inst.faults.count = 100000;
  r.inst.faults.drop = true;
  CHECK(!r.drv.Recover().ok()); // nothing answers
  CHECK_CHAR(r.drv.GetConfig().unit_id, 'A');
  model::FullRig m(model::ModbusConfig());
  REQUIRE(Start(m));
  m.inst.modbus_address = 9;
  m.inst.serial = "OTHER";
  CHECK(!m.drv.Recover().ok());
  CHECK_EQ(m.drv.GetConfig().modbus_address, 1U);
}

TEST_CASE("Recover: an abandoned driver stays abandoned when the instrument cannot be found") {
  model::FullRig r;
  REQUIRE(Start(r));
  REQUIRE(r.drv.SwitchToProtocol1(alicat_basis2::ConfirmAbandonProtocol2{}).ok());
  r.inst.faults.count = 100000;
  r.inst.faults.drop = true;
  CHECK(!r.drv.Recover().ok());
  CHECK_EQ(E(r.drv.ReadFlow().error), E(Error::Unsupported));
}

TEST_CASE("Recover: a successful Recover after the instrument came back ends the doubt") {
  model::FullRig r;
  REQUIRE(Start(r));
  LoseUnitIdReply(r);
  REQUIRE(r.drv.Recover().ok());
  CHECK(!r.drv.IsStateUnknown());
  CHECK(r.drv.IsReady());
  CHECK_CHAR(r.drv.GetConfig().unit_id, 'C');
}

// --- Boundaries ---

TEST_CASE("boundaries: unknown status bits are counted from bit 5 up") {
  CHECK_EQ(alicat_basis2::StatusFromBits(0x1F).unknown_count, 0U);
  CHECK_EQ(alicat_basis2::StatusFromBits(0x20).unknown_count, 1U);
  CHECK_EQ(alicat_basis2::StatusFromBits(0x8000).unknown_count, 1U);
}

TEST_CASE("boundaries: the bytes of a reply line are 0x20 to 0x7E") {
  alicat_basis2::Config c{};
  c.read_retries = 0;
  model::FullRig r(c);
  REQUIRE(Start(r));
  const auto raw = [&r](const std::string& text) {
    r.inst.faults.count = 1;
    r.inst.faults.replace = text;
    return r.drv.ReadRawLine(alicat_basis2::Command::ReadFirmware);
  };
  CHECK(raw("A 3v0v5 ~ ").ok()); // 0x7E and the space (0x20) are printable
  CHECK_EQ(E(raw("A 3v0v5 \x7F").error), E(Error::BadReply));
  CHECK_EQ(E(raw("A 3v0v5 \x1F").error), E(Error::BadReply));
}

TEST_CASE("boundaries: a reply line holds 256 characters, not 257") {
  alicat_basis2::Config c{};
  c.read_retries = 0;
  c.baud = 115200;
  model::FullRig r(c);
  r.inst.baud = 115200;
  r.inst.port_baud = 115200;
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  std::string line = "A";
  for (int i = 0; i < 7; ++i) {
    line += " " + std::string(31, 'x');
  }
  r.inst.faults.replace = line + " " + std::string(30, 'x'); // 256 characters in nine tokens
  CHECK_EQ(E(r.drv.ReadRawLine(alicat_basis2::Command::ReadFirmware).error), 0);
  r.inst.faults.count = 1;
  r.inst.faults.replace = line + " " + std::string(31, 'x'); // 257 characters
  CHECK_EQ(E(r.drv.ReadRawLine(alicat_basis2::Command::ReadFirmware).error), E(Error::Overflow));
}

TEST_CASE("boundaries: argument limits of the setters") {
  model::FullRig r;
  REQUIRE(Start(r));
  CHECK(r.drv.Tare(32767).ok());
  CHECK_EQ(E(r.drv.Tare(32768).error), E(Error::InvalidArgument));
  CHECK(r.drv.HoldValve(100.0).ok());
  CHECK_EQ(E(r.drv.HoldValve(100.001).error), E(Error::InvalidArgument));
  CHECK(r.drv.SetGains(65535, 65535).ok());
  CHECK_EQ(E(r.drv.SetGains(65536, 0).error), E(Error::InvalidArgument));
  CHECK_EQ(E(r.drv.SetGains(0, 65536).error), E(Error::InvalidArgument));
  // 2.5 % over a full scale of 500: 512.5 goes, 512.501 does not.
  CHECK(r.drv.SetSetpoint(512.5).error != Error::InvalidArgument);
  CHECK_EQ(E(r.drv.SetSetpoint(512.501).error), E(Error::InvalidArgument));
}

TEST_CASE("kill: SwitchToProtocol2 does not end the doubt about the instrument") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  static_cast<void>(r.drv.ChangeUnitId('C', alicat_basis2::ConfirmUnitIdChange{}));
  REQUIRE(r.drv.IsStateUnknown());
  static_cast<void>(r.drv.SwitchToProtocol2(alicat_basis2::ConfirmReturnToProtocol2{}));
  CHECK(r.drv.IsStateUnknown());
  CHECK_EQ(E(r.drv.ResetTotal().error), E(Error::StateUnknown));
}

TEST_CASE("kill: the bus refuses a request that did not come from the grammar checks") {
  model::FullRig r;
  REQUIRE(Start(r));
  alicat_basis2::detail::RequestData d{};
  const std::string text = "A\x01S 1.0\r"; // a control character inside the line
  for (std::size_t i = 0; i < text.size(); ++i) {
    d.bytes[i] = static_cast<uint8_t>(text[i]);
  }
  d.length = static_cast<uint8_t>(text.size());
  d.id = 'A';
  const alicat_basis2::Request forged = alicat_basis2::detail::RequestBuilder::Build(d);
  const uint32_t before = r.bus.WireViolations();
  const std::size_t writes = r.inst.writes.size();
  CHECK_EQ(E(r.bus.Submit(forged, alicat_basis2::Timing{})), E(Error::InvalidArgument));
  CHECK_EQ(r.bus.WireViolations(), before + 1U);
  CHECK_EQ(r.inst.writes.size(), writes);
  alicat_basis2::detail::RequestData m{};
  m.modbus = true;
  m.bytes[0] = 1U;
  m.bytes[1] = 0x2BU; // a function code the driver never sends
  m.length = 8U;
  const alicat_basis2::Request bad = alicat_basis2::detail::RequestBuilder::Build(m);
  CHECK_EQ(E(r.bus.Submit(bad, alicat_basis2::Timing{})), E(Error::InvalidArgument));
  CHECK_EQ(r.bus.WireViolations(), before + 2U);
}

TEST_CASE("kill: a failed Write quarantines the bus, a failed flush does not") {
  alicat_basis2::Config c{};
  c.read_retries = 0;
  model::FullRig r(c);
  REQUIRE(Start(r));
  r.inst.fail_flush = true;
  CHECK_EQ(E(r.drv.ReadFlow().error), E(Error::Bus));
  CHECK_EQ(static_cast<int>(r.bus.State()), static_cast<int>(alicat_basis2::BusState::Idle));
  r.inst.fail_flush = false;
  r.inst.fail_write = true;
  CHECK_EQ(E(r.drv.ReadFlow().error), E(Error::Bus));
  CHECK_EQ(static_cast<int>(r.bus.State()), static_cast<int>(alicat_basis2::BusState::Settling));
}

namespace {
using alicat_basis2::Basis2;
using alicat_basis2::Result;
/// Every call that changes the instrument, each as a call on a driver; none may reach the wire in
/// doubt.
template <typename Drv>
std::array<std::pair<const char*, Result<void> (*)(Drv&)>, 27> RefusedInDoubt() {
  using R = Result<void>;
  return {{
      {"SetSetpointSource",
       [](Drv& d) -> R {
         return d.SetSetpointSource(alicat_basis2::SetpointSource::UnsavedDigital);
       }},
      {"SetRamp", [](Drv& d) -> R { return d.SetRamp(1.0, alicat_basis2::TimeUnit::Second); }},
      {"DisableRamp", [](Drv& d) -> R { return d.DisableRamp(); }},
      {"SetWatchdog", [](Drv& d) -> R { return d.SetWatchdog(10); }},
      {"SetSetpoint", [](Drv& d) -> R { return d.SetSetpoint(1.0); }},
      {"HoldValve", [](Drv& d) -> R { return d.HoldValve(10.0); }},
      {"OpenValve", [](Drv& d) -> R { return d.OpenValve(); }},
      {"ResumeControl", [](Drv& d) -> R { return d.ResumeControl(); }},
      {"SetGains", [](Drv& d) -> R { return d.SetGains(10, 10); }},
      {"SetAutotare", [](Drv& d) -> R { return d.SetAutotare(true); }},
      {"SetMeasurementTrigger", [](Drv& d) -> R { return d.SetMeasurementTrigger(1); }},
      {"StartMeasurement", [](Drv& d) -> R { return d.StartMeasurement(100); }},
      {"Tare", [](Drv& d) -> R { return d.Tare(100); }},
      {"TareDefault", [](Drv& d) -> R { return d.TareDefault(); }},
      {"SetGas", [](Drv& d) -> R { return d.SetGas(alicat_basis2::GasId{1}); }},
      {"ResetTotal", [](Drv& d) -> R { return d.ResetTotal(); }},
      {"SetTotalizerLimit",
       [](Drv& d) -> R { return d.SetTotalizerLimit(alicat_basis2::TotalizerLimit{}); }},
      {"SetBatch", [](Drv& d) -> R { return d.SetBatch(1.0); }},
      {"DisableBatch", [](Drv& d) -> R { return d.DisableBatch(); }},
      {"SetAveraging", [](Drv& d) -> R { return d.SetAveraging(10); }},
      {"SetReferenceTemperature", [](Drv& d) -> R { return d.SetReferenceTemperature(20.0); }},
      {"ChangeUnitId",
       [](Drv& d) -> R { return d.ChangeUnitId('D', alicat_basis2::ConfirmUnitIdChange{}); }},
      {"SetModbusAddress",
       [](Drv& d) -> R { return d.SetModbusAddress(7, alicat_basis2::ConfirmAddressChange{}); }},
      {"ChangeBaud",
       [](Drv& d) -> R {
         return d.ChangeBaud(alicat_basis2::Baud::B9600, alicat_basis2::ConfirmBaudChange{});
       }},
      {"SwitchToProtocol1",
       [](Drv& d) -> R { return d.SwitchToProtocol1(alicat_basis2::ConfirmAbandonProtocol2{}); }},
      {"FactoryRestore",
       [](Drv& d) -> R { return d.FactoryRestore(alicat_basis2::ConfirmFactoryRestore{}); }},
      {"Begin", [](Drv& d) -> R { return d.Begin(); }},
  }};
}

template <typename Rig>
void CheckRefusedInDoubt(Rig& r) {
  const std::size_t writes = r.inst.writes.size();
  for (const auto& [name, call] : RefusedInDoubt<decltype(r.drv)>()) {
    const Error e = call(r.drv).error;
    if (e != Error::StateUnknown) {
      std::printf("not refused in doubt: %s\n", name);
    }
    CHECK_EQ(E(e), E(Error::StateUnknown));
    CHECK_EQ(r.inst.writes.size(), writes);
  }
}
} // namespace

TEST_CASE("StateUnknown refuses every instrument-changing call and sends nothing (ASCII)") {
  model::FullRig r;
  REQUIRE(Start(r));
  LoseUnitIdReply(r);
  REQUIRE(r.drv.IsStateUnknown());
  CheckRefusedInDoubt(r);
}

TEST_CASE("StateUnknown refuses every instrument-changing call and sends nothing (Modbus)") {
  model::FullRig r(model::ModbusConfig());
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  static_cast<void>(r.drv.SetModbusAddress(9, alicat_basis2::ConfirmAddressChange{}));
  r.inst.faults = model::Faults{};
  REQUIRE(r.drv.IsStateUnknown());
  CheckRefusedInDoubt(r);
}

TEST_CASE("switch-off in doubt: after a factory restore ID and speed are both back at the factory "
          "values") {
  alicat_basis2::Config c{};
  c.unit_id = 'B';
  c.baud = 57600;
  model::FullRig r(c);
  r.inst.unit_id = 'B';
  r.inst.baud = 57600;
  r.inst.port_baud = 57600;
  REQUIRE(Start(r));
  static_cast<void>(r.drv.FactoryRestore(alicat_basis2::ConfirmFactoryRestore{}));
  REQUIRE(r.drv.IsStateUnknown());
  CHECK_CHAR(r.inst.unit_id, 'A');
  CHECK_EQ(r.inst.baud, 38400U);
  r.inst.Advance(5'000'000);
  r.inst.source = 'U';
  r.inst.setpoint_milli = 5000;
  CHECK(r.drv.SetSetpoint(0.0).ok());
  CHECK_EQ(r.inst.setpoint_milli, 0);
  CHECK_EQ(r.inst.port_baud, 38400U);
  CHECK_CHAR(r.drv.GetConfig().unit_id, 'A');
}

TEST_CASE(
    "switch-off in doubt: after a Modbus factory restore address and speed are factory values") {
  alicat_basis2::Config c = model::ModbusConfig();
  c.modbus_address = 7;
  c.baud = 57600;
  model::FullRig r(c);
  r.inst.modbus_address = 7;
  r.inst.baud = 57600;
  r.inst.port_baud = 57600;
  REQUIRE(Start(r));
  static_cast<void>(r.drv.FactoryRestore(alicat_basis2::ConfirmFactoryRestore{}));
  REQUIRE(r.drv.IsStateUnknown());
  r.inst.Advance(5'000'000);
  r.inst.source = 'U';
  r.inst.setpoint_milli = 5000;
  CHECK(r.drv.SetSetpoint(0.0).ok());
  CHECK_EQ(r.inst.setpoint_milli, 0);
  CHECK_EQ(r.inst.port_baud, 38400U);
  CHECK_EQ(r.drv.GetConfig().modbus_address, 1U);
}

TEST_CASE(
    "Recover: finds an instrument that runs at the new speed after an unconfirmed baud change") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  static_cast<void>(
      r.drv.ChangeBaud(alicat_basis2::Baud::B57600, alicat_basis2::ConfirmBaudChange{}));
  r.inst.faults = model::Faults{};
  REQUIRE(r.drv.IsStateUnknown());
  CHECK_EQ(r.inst.port_baud, 38400U);
  CHECK(r.drv.Recover().ok());
  CHECK(!r.drv.IsStateUnknown());
  CHECK_EQ(r.inst.port_baud, 57600U);
  CHECK_EQ(r.drv.GetConfig().baud, 57600U);
  CHECK(r.drv.ReadFlow().ok());
}

TEST_CASE("Recover: a failed Recover puts the port back at the held speed") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  static_cast<void>(
      r.drv.ChangeBaud(alicat_basis2::Baud::B57600, alicat_basis2::ConfirmBaudChange{}));
  r.inst.faults = model::Faults{};
  r.inst.serial = "999999"; // another instrument answers at the new speed
  CHECK(!r.drv.Recover().ok());
  CHECK(r.drv.IsStateUnknown());
  CHECK_EQ(r.inst.port_baud, 38400U);
  CHECK_EQ(r.drv.GetConfig().baud, 38400U);
}

TEST_CASE("kill: SwitchToProtocol2 reaches its success path in doubt and keeps the doubt and the "
          "alternatives") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.knobs.ignore_unit_change = true;
  static_cast<void>(r.drv.ChangeUnitId('C', alicat_basis2::ConfirmUnitIdChange{}));
  r.inst.knobs.ignore_unit_change = false;
  REQUIRE(r.drv.IsStateUnknown());
  CHECK(r.drv.SwitchToProtocol2(alicat_basis2::ConfirmReturnToProtocol2{}).ok());
  CHECK(r.drv.IsStateUnknown());
  // The alternatives survive: a switch-off still tries the unit ID the driver was changing to.
  r.inst.unit_id = 'C';
  r.inst.setpoint_milli = 5000;
  CHECK(r.drv.SetSetpoint(0.0).ok());
  CHECK_EQ(r.inst.setpoint_milli, 0);
}

TEST_CASE("kill: a hold that opens the valve needs a validated Begin, a hold of zero does not") {
  model::FullRig r;
  REQUIRE(r.drv.EnsureInitialized().ok());
  const std::size_t n = r.inst.writes.size();
  CHECK_EQ(E(r.drv.HoldValve(10.0).error), E(Error::NotInitialized));
  CHECK_EQ(r.inst.writes.size(), n);
  CHECK(r.drv.HoldValve(0.0).error != Error::NotInitialized);
}

TEST_CASE("kill: a SetRamp whose reply was lost leaves the ramp marked active") {
  alicat_basis2::Config c{};
  c.read_retries = 0;
  model::FullRig r(c);
  REQUIRE(Start(r));
  REQUIRE(!r.drv.GetIdentity().ramp_active);
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  CHECK(!r.drv.SetRamp(1.0, alicat_basis2::TimeUnit::Second).ok());
  CHECK(r.drv.GetIdentity().ramp_active);
}

TEST_CASE("saved setpoint: a garbled or a wrong-ID reply starts the rate limit as well") {
  for (int kind = 0; kind < 2; ++kind) {
    alicat_basis2::Config c{};
    c.read_retries = 0;
    model::FullRig r(c);
    REQUIRE(Start(r));
    REQUIRE(r.drv.SetSetpointSource(alicat_basis2::SetpointSource::SavedDigital).ok());
    r.inst.faults.count = 1;
    if (kind == 0) {
      r.inst.faults.garble_at = 0;
    } else {
      r.inst.faults.wrong_id = 'B';
    }
    CHECK(!r.drv.SetSetpoint(10.0).ok());
    r.inst.faults = model::Faults{};
    r.inst.Advance(10'000);
    CHECK_EQ(E(r.drv.SetSetpoint(11.0).error), E(Error::RateLimited));
  }
}

TEST_CASE(
    "switch-off in doubt: after a failed try at another speed the port is back at the held one") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  static_cast<void>(
      r.drv.ChangeBaud(alicat_basis2::Baud::B57600, alicat_basis2::ConfirmBaudChange{}));
  r.inst.faults = model::Faults{};
  r.inst.faults.count = 100000;
  r.inst.faults.drop = true;
  CHECK(!r.drv.SetSetpoint(0.0).ok());
  CHECK_EQ(r.inst.port_baud, 38400U);
  CHECK_EQ(r.drv.GetConfig().baud, 38400U);
}

TEST_CASE("Recover: finds the instrument at a factory Modbus address other than the inferred 1") {
  model::FullRig r(model::ModbusConfig());
  r.inst.knobs.factory_modbus_address = 5;
  REQUIRE(Start(r));
  static_cast<void>(r.drv.FactoryRestore(alicat_basis2::ConfirmFactoryRestore{}));
  REQUIRE(r.drv.IsStateUnknown());
  r.inst.Advance(5'000'000);
  CHECK(r.drv.Recover().ok());
  CHECK_EQ(r.drv.GetConfig().modbus_address, 5U);
}

TEST_CASE("kill: reads and scans are refused in doubt, ResumeControl needs a validated Begin") {
  model::FullRig r;
  REQUIRE(Start(r));
  LoseUnitIdReply(r);
  REQUIRE(r.drv.IsStateUnknown());
  const std::size_t n = r.inst.writes.size();
  CHECK_EQ(E(r.drv.ReadFlow().error), E(Error::StateUnknown));
  CHECK_EQ(E(r.drv.ReadFrame().error), E(Error::StateUnknown));
  CHECK_EQ(E(r.drv.ReadTotal().error), E(Error::StateUnknown));
  CHECK_EQ(E(r.drv.ReadSetpointSource().error), E(Error::StateUnknown));
  CHECK_EQ(E(r.drv.ReadGains().error), E(Error::StateUnknown));
  CHECK_EQ(E(r.drv.ReadFirmware().error), E(Error::StateUnknown));
  CHECK_EQ(E(r.drv.ScanUnitIds().error), E(Error::StateUnknown));
  CHECK_EQ(E(r.drv.ScanModbusAddresses(1, 3).error), E(Error::StateUnknown));
  CHECK_EQ(r.inst.writes.size(), n);
  model::FullRig q;
  REQUIRE(q.drv.EnsureInitialized().ok());
  const std::size_t m = q.inst.writes.size();
  CHECK_EQ(E(q.drv.ResumeControl().error), E(Error::NotInitialized));
  CHECK_EQ(q.inst.writes.size(), m);
}
