/**
 * @file test_bounds.cpp
 * @brief A shared line, a result nobody took, the wire value of a rounded setpoint, its reply
 * wait, the read-back of a line setting, and the edges of the text and argument parsers. Each case
 * fails on a one-edit change of the line it names.
 */
#include <array>
#include <cstdint>
#include <string>

#include "rig.hpp"

using alicat_basis2::ConfirmAddressChange;
using alicat_basis2::ConfirmBaudChange;
using alicat_basis2::ConfirmFactoryRestore;
using alicat_basis2::Error;
using alicat_basis2::Text;
using rig::E;
using rig::Last;
using rig::Start;

namespace {

/// Whether any write since `from` is a Modbus frame to `address`.
bool AddressedSince(const model::Instrument& m, std::size_t from, uint8_t address) {
  for (std::size_t i = from; i < m.writes.size(); ++i) {
    if (!m.writes[i].text.empty() && (model::ByteOf(m.writes[i].text[0]) == address)) {
      return true;
    }
  }
  return false;
}

/// A text of the given characters.
Text TextOf(const std::string& s) {
  Text t{};
  for (std::size_t i = 0U; (i < s.size()) && (i < t.chars.size()); ++i) {
    t.chars[i] = s[i];
    t.length = static_cast<uint8_t>(i + 1U);
  }
  return t;
}

} // namespace

// --- a line declared to hold several instruments (review 5, M2) ---------------------------------

TEST_CASE("shared line: SetModbusAddress is refused before anything is sent") {
  model::FullRig r(model::ModbusConfig());
  REQUIRE(Start(r));
  r.bus.SetDeviceCount(2);
  const std::size_t n = r.inst.writes.size();
  CHECK_EQ(E(r.drv.SetModbusAddress(7, ConfirmAddressChange{}).error), E(Error::GuardNotConfirmed));
  CHECK_EQ(r.inst.writes.size(), n);
  CHECK(!r.drv.IsStateUnknown());
  CHECK_EQ(r.inst.modbus_address, 1U);
}

TEST_CASE("shared line: FactoryRestore is refused before anything is sent, on both protocols") {
  model::FullRig a;
  REQUIRE(Start(a));
  a.bus.SetDeviceCount(2);
  const std::size_t na = a.inst.writes.size();
  CHECK_EQ(E(a.drv.FactoryRestore(ConfirmFactoryRestore{}).error), E(Error::GuardNotConfirmed));
  CHECK_EQ(a.inst.writes.size(), na);
  CHECK_EQ(a.inst.restores, 0U);
  model::FullRig m(model::ModbusConfig());
  REQUIRE(Start(m));
  m.bus.SetDeviceCount(2);
  const std::size_t nm = m.inst.writes.size();
  CHECK_EQ(E(m.drv.FactoryRestore(ConfirmFactoryRestore{}).error), E(Error::GuardNotConfirmed));
  CHECK_EQ(m.inst.writes.size(), nm);
  CHECK(m.drv.IsReady());
}

TEST_CASE(
    "shared line: a switch-off in doubt never adopts the address the driver was changing to") {
  model::FullRig r(model::ModbusConfig());
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.drop = true; // the instrument moves to 7, the reply is lost
  CHECK_EQ(E(r.drv.SetModbusAddress(7, ConfirmAddressChange{}).error), E(Error::StateUnknown));
  REQUIRE(r.inst.modbus_address == 7U);
  r.bus.SetDeviceCount(2); // from now on 7 could be a neighbour
  const std::size_t n = r.inst.writes.size();
  CHECK(!r.drv.SetSetpoint(0.0).ok());
  CHECK(!AddressedSince(r.inst, n, 7U));
  CHECK_EQ(r.drv.GetConfig().modbus_address, 1U);
  CHECK(r.drv.IsStateUnknown());
}

TEST_CASE("shared line: ChangeBaud is refused before anything is sent, on both protocols") {
  model::FullRig a;
  REQUIRE(Start(a));
  a.bus.SetDeviceCount(2);
  const std::size_t na = a.inst.writes.size();
  CHECK_EQ(E(a.drv.ChangeBaud(alicat_basis2::Baud::B19200, ConfirmBaudChange{}).error),
           E(Error::GuardNotConfirmed));
  CHECK_EQ(a.inst.writes.size(), na);
  CHECK_EQ(a.inst.port_baud, 38400U);
  model::FullRig m(model::ModbusConfig());
  REQUIRE(Start(m));
  m.bus.SetDeviceCount(2);
  const std::size_t nm = m.inst.writes.size();
  CHECK_EQ(E(m.drv.ChangeBaud(alicat_basis2::Baud::B19200, ConfirmBaudChange{}).error),
           E(Error::GuardNotConfirmed));
  CHECK_EQ(m.inst.writes.size(), nm);
}

TEST_CASE("shared line: a switch-off in doubt tries the other speed but puts the held one back") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.drop = true; // the instrument changes speed, the reply is lost
  CHECK(!r.drv.ChangeBaud(alicat_basis2::Baud::B19200, ConfirmBaudChange{}).ok());
  REQUIRE(r.inst.baud == 19200U);
  REQUIRE(r.drv.IsStateUnknown());
  r.bus.SetDeviceCount(2); // the neighbours stay at 38400
  CHECK(r.drv.SetSetpoint(0.0).ok());
  CHECK_STR(Last(r.inst), "AS 0.000"); // sent at 19200 to the held ID
  CHECK_EQ(r.drv.GetConfig().baud, 38400U);
  CHECK_EQ(r.inst.port_baud, 38400U);
  CHECK_CHAR(r.drv.GetConfig().unit_id, 'A');
  CHECK(r.drv.IsStateUnknown());
}

TEST_CASE("switch-off in doubt: when no alternative answers the held address is kept") {
  model::FullRig r(model::ModbusConfig());
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  CHECK_EQ(E(r.drv.SetModbusAddress(7, ConfirmAddressChange{}).error), E(Error::StateUnknown));
  r.inst.faults.count = 100; // nothing answers any more
  CHECK(!r.drv.SetSetpoint(0.0).ok());
  CHECK_EQ(r.drv.GetConfig().modbus_address, 1U);
}

// --- a line that never goes quiet (review 6, M2) ------------------------------------------------

namespace {
/// Back-to-back stray lines from a streaming peer, about `lines` x 0.57 ms at 38400 baud.
std::string Chatter(int lines) {
  std::string s;
  for (int i = 0; i < lines; ++i) {
    s += "@ +24.50 +1.0 +0.0 N2\r";
  }
  return s;
}
} // namespace

TEST_CASE("chatter: a switch-off on a line that never goes quiet ends unsent within its deadline") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.InjectStray(Chatter(4000), 0); // about 23 s of chatter
  const uint64_t before = r.inst.now_us;
  const std::size_t n = r.inst.writes.size();
  CHECK_EQ(E(r.drv.SetSetpoint(0.0).error), E(Error::Busy));
  CHECK_EQ(r.inst.writes.size(), n);
  CHECK((r.inst.now_us - before) < 500'000U);
  CHECK_EQ(E(r.drv.ReadFlow().error), E(Error::Busy));
  CHECK((r.inst.now_us - before) < 1'500'000U);
  CHECK(!r.drv.IsStateUnknown());
}

TEST_CASE("chatter: a guarded command that was never sent leaves nothing in doubt") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.InjectStray(Chatter(4000), 0);
  CHECK_EQ(E(r.drv.ChangeBaud(alicat_basis2::Baud::B19200, ConfirmBaudChange{}).error),
           E(Error::Busy));
  CHECK(!r.drv.IsStateUnknown());
  CHECK_EQ(r.inst.port_baud, 38400U);
  CHECK_EQ(E(r.drv.FactoryRestore(ConfirmFactoryRestore{}).error), E(Error::Busy));
  CHECK(!r.drv.IsStateUnknown());
  CHECK_EQ(r.inst.restores, 0U);
}

TEST_CASE("chatter: once the line is quiet again the switch-off goes out") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.InjectStray(Chatter(200), 0); // about 1.1 s
  CHECK_EQ(E(r.drv.SetSetpoint(0.0).error), E(Error::Busy));
  r.inst.AdvanceMs(2000);
  const std::size_t n = r.inst.writes.size();
  CHECK(r.drv.SetSetpoint(0.0).ok());
  CHECK_STR(rig::Wire(r.inst, n).substr(0, 9), "AS 0.000$");
}

TEST_CASE("chatter during a non-blocking request: the switch-off ends within the bus bound") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.InjectStray(Chatter(1000), 0); // about 5.7 s
  const auto poll = alicat_basis2::ascii::Make(alicat_basis2::Command::Poll, 'A',
                                               alicat_basis2::ascii::Args{}, false);
  REQUIRE(poll.ok());
  alicat_basis2::Timing t{};
  t.baud = 38400U;
  REQUIRE(r.bus.Submit(poll.value, t) == Error::Ok);
  const uint64_t before = r.inst.now_us;
  const std::size_t n = r.inst.writes.size();
  CHECK_EQ(E(r.drv.SetSetpoint(0.0).error), E(Error::Busy));
  CHECK_EQ(r.inst.writes.size(), n);
  CHECK((r.inst.now_us - before) < 1'000'000U);
}

// --- a non-blocking result nobody took (review 5, M3) -------------------------------------------

TEST_CASE("bus: a result nobody took does not hold a switch-off back, which ends within its "
          "deadlines") {
  model::FullRig r;
  REQUIRE(Start(r));
  const auto poll = alicat_basis2::ascii::Make(alicat_basis2::Command::Poll, 'A',
                                               alicat_basis2::ascii::Args{}, false);
  REQUIRE(poll.ok());
  alicat_basis2::Timing t{};
  t.baud = 38400U;
  REQUIRE(r.bus.Submit(poll.value, t) == Error::Ok);
  alicat_basis2::PollState s = alicat_basis2::PollState::Pending;
  for (int i = 0; (i < 10000) && (s == alicat_basis2::PollState::Pending); ++i) {
    r.inst.AdvanceMs(1);
    s = r.bus.Poll();
  }
  REQUIRE(s == alicat_basis2::PollState::Done); // and never taken
  const uint64_t before = r.inst.now_us;
  const std::size_t n = r.inst.writes.size();
  CHECK(r.drv.SetSetpoint(0.0).ok());
  REQUIRE(r.inst.writes.size() > n);
  CHECK_STR(rig::Wire(r.inst, n).substr(0, 9), "AS 0.000$");
  CHECK((r.inst.now_us - before) < 3'000'000U);
}

TEST_CASE("bus: a transaction still in flight is waited for and its result dropped") {
  model::FullRig r;
  REQUIRE(Start(r));
  const auto poll = alicat_basis2::ascii::Make(alicat_basis2::Command::Poll, 'A',
                                               alicat_basis2::ascii::Args{}, false);
  REQUIRE(poll.ok());
  alicat_basis2::Timing t{};
  t.baud = 38400U;
  REQUIRE(r.bus.Submit(poll.value, t) == Error::Ok); // never polled by the application
  const std::size_t n = r.inst.writes.size();
  CHECK(r.drv.SetSetpoint(0.0).ok());
  REQUIRE(r.inst.writes.size() >= (n + 2U));
  CHECK_STR(rig::Wire(r.inst, n).substr(0, 12), "A$|AS 0.000$");
}

// --- the setpoint as it goes on the wire (review 5, m5) -----------------------------------------

TEST_CASE("setpoint decimals 0: a request that rounds to 0 is a switch-off, sent in doubt") {
  alicat_basis2::Config c{};
  c.setpoint_decimals = 0;
  model::FullRig r(c);
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  CHECK_EQ(E(r.drv.ChangeBaud(alicat_basis2::Baud::B38400, ConfirmBaudChange{}).error),
           E(Error::StateUnknown));
  REQUIRE(r.drv.IsStateUnknown());
  const std::size_t n = r.inst.writes.size();
  CHECK(r.drv.SetSetpoint(0.4).ok());
  CHECK_EQ(r.inst.writes.size(), n + 1U);
  CHECK_STR(Last(r.inst), "AS 0");
}

TEST_CASE("setpoint decimals 0: a request that rounds up past 102.5 % is refused unsent") {
  alicat_basis2::Config c{};
  c.setpoint_decimals = 0;
  model::FullRig r(c);
  REQUIRE(Start(r)); // full scale 500.000
  const std::size_t n = r.inst.writes.size();
  CHECK_EQ(E(r.drv.SetSetpoint(512.5).error), E(Error::InvalidArgument)); // S 513 > 512.5
  CHECK_EQ(r.inst.writes.size(), n);
  CHECK(r.drv.SetSetpoint(512.4).ok()); // S 512
  CHECK_STR(rig::Wire(r.inst, n).substr(0, 7), "AS 512$");
}

// --- the reply wait of a setpoint that writes non-volatile memory (review 5, m6) ----------------

TEST_CASE("setpoint: with the saved source the reply may take the slow-command wait") {
  model::FullRig r;
  REQUIRE(Start(r));
  REQUIRE(r.drv.SetSetpointSource(alicat_basis2::SetpointSource::SavedDigital).ok());
  r.inst.AdvanceMs(r.drv.GetConfig().saved_setpoint_min_interval_ms);
  r.inst.faults.count = 1;
  r.inst.faults.delay_us = 300'000U; // beyond processing_ms (50), within slow_command_ms (1000)
  CHECK(r.drv.SetSetpoint(10.0).ok());
}

TEST_CASE("setpoint: with the unsaved source the reply wait stays the processing time") {
  model::FullRig r;
  REQUIRE(Start(r));
  REQUIRE(r.drv.SetSetpointSource(alicat_basis2::SetpointSource::UnsavedDigital).ok());
  r.inst.faults.count = 1;
  r.inst.faults.delay_us = 300'000U;
  CHECK_EQ(E(r.drv.SetSetpoint(10.0).error), E(Error::Timeout));
}

TEST_CASE("setpoint over Modbus: with the saved source the reply may take the slow wait") {
  model::FullRig r(model::ModbusConfig());
  r.inst.source = 'S';
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.delay_us = 300'000U;
  CHECK(r.drv.SetSetpoint(10.0).ok());
}

TEST_CASE("Begin over Modbus learns the setpoint source and the ramp, as over ASCII") {
  model::FullRig r(model::ModbusConfig());
  r.inst.source = 'U';
  r.inst.ramp_rate_milli = 5000;
  REQUIRE(Start(r));
  CHECK(r.drv.GetIdentity().source_known);
  CHECK(r.drv.GetIdentity().source == alicat_basis2::SetpointSource::UnsavedDigital);
  CHECK(r.drv.GetIdentity().ramp_active);
  model::FullRig q(model::ModbusConfig());
  q.inst.source = 'S';
  REQUIRE(Start(q));
  CHECK(q.drv.GetIdentity().source == alicat_basis2::SetpointSource::SavedDigital);
  CHECK(!q.drv.GetIdentity().ramp_active);
}

// --- read-backs of line settings compare the value (review 5, m7 and m9 M35/M37) ----------------

TEST_CASE("modbus ChangeBaud: register 21 must hold the new code") {
  model::FullRig r(model::ModbusConfig());
  REQUIRE(Start(r));
  r.inst.knobs.baud_report = 3; // 38400, the old speed
  CHECK_EQ(E(r.drv.ChangeBaud(alicat_basis2::Baud::B19200, ConfirmBaudChange{}).error),
           E(Error::StateUnknown));
  CHECK(r.drv.IsStateUnknown());
}

TEST_CASE("SetModbusAddress: the read-back must hold the new address, on both protocols") {
  model::FullRig m(model::ModbusConfig());
  REQUIRE(Start(m));
  m.inst.knobs.address_report = 9;
  CHECK(!m.drv.SetModbusAddress(7, ConfirmAddressChange{}).ok());
  CHECK(m.drv.IsStateUnknown());
  model::FullRig a;
  REQUIRE(Start(a));
  a.inst.knobs.address_report = 9;
  CHECK(!a.drv.SetModbusAddress(7, ConfirmAddressChange{}).ok());
  CHECK(a.drv.IsStateUnknown());
}

// --- parsers at their edges (review 5, m8, m11, m9) ---------------------------------------------

TEST_CASE("firmware text: a part has at most two digits, so a long run never wraps") {
  using alicat_basis2::detail::ParseFirmware;
  CHECK_EQ(ParseFirmware(TextOf("3v0v5")), 0x305U);
  CHECK_EQ(ParseFirmware(TextOf("12.0.5")), 0xC05U);
  CHECK_EQ(ParseFirmware(TextOf("4294967299v0v5")), 0U); // 2^32 + 3 would wrap to 3.0.5
  CHECK_EQ(ParseFirmware(TextOf("3.4294967296.5")), 0U);
  CHECK_EQ(ParseFirmware(TextOf("3.0.012")), 0U);
  CHECK_EQ(ParseFirmware(TextOf("3.0.5.1")), 0U); // a fourth part is not a version
}

TEST_CASE("modbus text: it ends at the first NUL") {
  alicat_basis2::modbus::Response resp{};
  resp.words[0] = 0x534EU; // "SN"
  resp.words[1] = 0x3100U; // "1", NUL
  resp.words[2] = 0x5A39U; // "Z9" after the NUL
  resp.count = 3U;
  const Text t = alicat_basis2::detail::RegistersToText(resp, 0U, 3U);
  CHECK_EQ(t.length, 3U);
  CHECK_STR(std::string(t.chars.data(), t.length), "SN1");
  resp.words[1] = 0x3101U; // a non-printable byte other than NUL is skipped
  const Text u = alicat_basis2::detail::RegistersToText(resp, 0U, 3U);
  CHECK_STR(std::string(u.chars.data(), u.length), "SN1Z9");
}

TEST_CASE("modbus: a read answered with fewer registers than asked is a bad reply") {
  alicat_basis2::Config c = model::ModbusConfig();
  c.read_retries = 0;
  model::FullRig r(c);
  REQUIRE(Start(r));
  // A well-formed reply with one register where two were asked (address 1, function 3).
  std::string frame;
  frame.push_back(model::CharOf(1U));
  frame.push_back(model::CharOf(3U));
  frame.push_back(model::CharOf(2U));
  frame.push_back(model::CharOf(0U));
  frame.push_back(model::CharOf(5U));
  const uint16_t crc =
      model::Instrument::Crc(reinterpret_cast<const uint8_t*>(frame.data()), frame.size());
  frame.push_back(model::CharOf(static_cast<uint8_t>(crc & 0xFFU)));
  frame.push_back(model::CharOf(static_cast<uint8_t>(crc >> 8U)));
  r.inst.faults.count = 1;
  r.inst.faults.replace = frame;
  CHECK_EQ(E(r.drv.ReadTotal().error), E(Error::BadReply));
}

TEST_CASE("HoldValve rounds half up: 0.0625 percent is held as 0.063") {
  model::FullRig r;
  REQUIRE(Start(r));
  CHECK(r.drv.HoldValve(0.0625).ok());
  CHECK_STR(Last(r.inst), "AHPUR 0.063");
}

TEST_CASE("setpoint verify: a read-back exactly at the tolerance passes, one more fails") {
  model::FullRig r;
  REQUIRE(Start(r));
  REQUIRE(r.drv.SetSetpointSource(alicat_basis2::SetpointSource::UnsavedDigital).ok());
  // Tolerance: 5 permille of 500.000 = 2.500.
  r.inst.clamp_setpoint_milli = 97'500;
  CHECK(r.drv.SetSetpoint(100.0).ok());
  r.inst.clamp_setpoint_milli = 97'400; // the frame prints one decimal
  CHECK_EQ(E(r.drv.SetSetpoint(100.0).error), E(Error::VerifyMismatch));
}

TEST_CASE("SetSetpoint on a meter over Modbus is Unsupported and sends nothing") {
  alicat_basis2::Config c = model::ModbusConfig();
  c.kind = alicat_basis2::DeviceKind::Meter;
  model::FullRig r(c);
  r.inst.controller = false;
  REQUIRE(Start(r));
  const std::size_t n = r.inst.writes.size();
  CHECK_EQ(E(r.drv.SetSetpoint(10.0).error), E(Error::Unsupported));
  CHECK_EQ(r.inst.writes.size(), n);
}

TEST_CASE("SetRamp over Modbus before the full scale is known is NotInitialized") {
  model::FullRig r(model::ModbusConfig());
  REQUIRE(r.drv.EnsureInitialized().ok()); // no Begin(): the full scale is not known
  const std::size_t n = r.inst.writes.size();
  CHECK_EQ(E(r.drv.SetRamp(1.0, alicat_basis2::TimeUnit::Second).error), E(Error::NotInitialized));
  CHECK_EQ(r.inst.writes.size(), n);
}

TEST_CASE("SetRamp refuses the time unit after minutes, on both protocols") {
  model::FullRig r;
  REQUIRE(Start(r));
  const std::size_t n = r.inst.writes.size();
  CHECK_EQ(E(r.drv.SetRamp(1.0, static_cast<alicat_basis2::TimeUnit>(6U)).error),
           E(Error::InvalidArgument));
  CHECK_EQ(r.inst.writes.size(), n);
  model::FullRig m(model::ModbusConfig()); // no grammar check behind the driver's here
  REQUIRE(Start(m));
  const std::size_t k = m.inst.writes.size();
  CHECK_EQ(E(m.drv.SetRamp(1.0, static_cast<alicat_basis2::TimeUnit>(6U)).error),
           E(Error::InvalidArgument));
  CHECK_EQ(m.inst.writes.size(), k);
}

TEST_CASE("DisableRamp turns the setpoint verify back on") {
  model::FullRig r;
  REQUIRE(Start(r));
  REQUIRE(r.drv.SetSetpointSource(alicat_basis2::SetpointSource::UnsavedDigital).ok());
  REQUIRE(r.drv.SetRamp(1.0, alicat_basis2::TimeUnit::Second).ok());
  REQUIRE(r.drv.DisableRamp().ok());
  r.inst.clamp_setpoint_milli = 50'000;
  CHECK_EQ(E(r.drv.SetSetpoint(100.0).error), E(Error::VerifyMismatch));
}

TEST_CASE("Tare over Modbus: 32767 ms is the longest, 32768 is refused unsent") {
  model::FullRig r(model::ModbusConfig());
  REQUIRE(Start(r));
  const std::size_t n = r.inst.writes.size();
  CHECK_EQ(E(r.drv.Tare(32768U).error), E(Error::InvalidArgument));
  CHECK_EQ(r.inst.writes.size(), n);
  CHECK(r.drv.Tare(32767U).ok());
}

TEST_CASE("previous measurement over Modbus: 4214 is the high word (A25)") {
  model::FullRig r(model::ModbusConfig());
  REQUIRE(Start(r));
  r.inst.prev_avg_milli = 7'000'000; // 7000.0 at one decimal: 70000 raw, above 16 bits
  const auto p = r.drv.ReadPreviousMeasurement();
  REQUIRE(p.ok());
  CHECK_EQ(p.value.average_flow.mantissa, 70000);
  r.inst.knobs.prev_avg_low_first = true; // the other reading of M p.22
  const auto q = r.drv.ReadPreviousMeasurement();
  REQUIRE(q.ok());
  CHECK(q.value.average_flow.mantissa != 70000);
}
