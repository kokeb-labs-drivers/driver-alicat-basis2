/**
 * @file test_guarded.cpp
 * @brief The six commands that can strand an instrument: tokens, verification, StateUnknown,
 * Recover, baud switching, scans, and the setpoint-wear limits.
 */
#include "rig.hpp"

using alicat_basis2::Baud;
using alicat_basis2::Error;
using alicat_basis2::SetpointSource;
using rig::E;
using rig::Last;
using rig::Start;

TEST_CASE("guarded: ChangeUnitId sends exactly A@=B, adopts the new ID and verifies with a poll") {
  model::FullRig r;
  REQUIRE(Start(r));
  CHECK(r.drv.ChangeUnitId('B', alicat_basis2::ConfirmUnitIdChange{}).ok());
  CHECK_CHAR(r.inst.unit_id, 'B');
  CHECK_CHAR(r.drv.GetConfig().unit_id, 'B');
  CHECK(r.drv.ReadFlow().ok());
  CHECK_STR(Last(r.inst), "BDV 1");
  CHECK_EQ(E(r.drv.ChangeUnitId('B', alicat_basis2::ConfirmUnitIdChange{}).error),
           E(Error::InvalidArgument));
  CHECK_EQ(E(r.drv.ChangeUnitId('1', alicat_basis2::ConfirmUnitIdChange{}).error),
           E(Error::InvalidArgument));
  r.bus.SetDeviceCount(2);
  CHECK_EQ(E(r.drv.ChangeUnitId('C', alicat_basis2::ConfirmUnitIdChange{}).error),
           E(Error::GuardNotConfirmed));
}

TEST_CASE("guarded: an unconfirmed guarded command leaves StateUnknown until Recover finds the "
          "instrument") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  const auto bad = r.drv.ChangeUnitId('C', alicat_basis2::ConfirmUnitIdChange{});
  CHECK_EQ(E(bad.error), E(Error::StateUnknown));
  CHECK(!r.drv.IsReady());
  CHECK_EQ(E(r.drv.ReadFlow().error), E(Error::StateUnknown));
  const std::size_t n = r.inst.writes.size();
  CHECK_EQ(E(r.drv.SetSetpoint(1.0).error), E(Error::StateUnknown));
  CHECK_EQ(r.inst.writes.size(), n); // nothing is sent while the state is unknown
  // A switch-off is never refused for doubt: both are sent (to the ID the driver still holds) and
  // return their own result, here a timeout because the instrument now answers to another ID.
  const auto off = r.drv.SetSetpoint(0.0);
  CHECK(off.error != Error::StateUnknown);
  CHECK(r.inst.writes.size() > n);
  const std::size_t n2 = r.inst.writes.size();
  const auto closed = r.drv.CloseValve();
  CHECK(closed.error != Error::StateUnknown);
  CHECK(r.inst.writes.size() > n2);
  // The instrument did change its ID (the reply was lost): Recover scans A to Z and relearns it.
  CHECK_CHAR(r.inst.unit_id, 'C');
  REQUIRE(r.drv.Recover().ok());
  CHECK_CHAR(r.drv.GetConfig().unit_id, 'C');
  CHECK(r.drv.IsReady());
  CHECK(r.drv.ReadFlow().ok());
}

TEST_CASE("guarded: a rejected guarded command leaves the state known") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.error_reply = true;
  CHECK_EQ(E(r.drv.SetModbusAddress(9, alicat_basis2::ConfirmAddressChange{}).error),
           E(Error::Rejected));
  CHECK(r.drv.IsReady());
  CHECK(r.drv.ReadFlow().ok());
}

TEST_CASE("guarded: SetModbusAddress over ASCII writes MA and reads it back") {
  model::FullRig r;
  REQUIRE(Start(r));
  CHECK(r.drv.SetModbusAddress(5, alicat_basis2::ConfirmAddressChange{}).ok());
  CHECK_EQ(r.inst.modbus_address, 5U);
  CHECK_EQ(E(r.drv.SetModbusAddress(0, alicat_basis2::ConfirmAddressChange{}).error),
           E(Error::InvalidArgument));
  CHECK_EQ(E(r.drv.SetModbusAddress(248, alicat_basis2::ConfirmAddressChange{}).error),
           E(Error::InvalidArgument));
}

TEST_CASE("guarded: SetModbusAddress and ChangeUnitId over Modbus use registers 45 and 46") {
  model::FullRig r(model::ModbusConfig());
  REQUIRE(Start(r));
  CHECK(r.drv.SetModbusAddress(9, alicat_basis2::ConfirmAddressChange{}).ok());
  CHECK_EQ(r.inst.modbus_address, 9U);
  CHECK_EQ(r.drv.GetConfig().modbus_address, 9U);
  CHECK(r.drv.ReadFlow().ok());
  CHECK(r.drv.ChangeUnitId('D', alicat_basis2::ConfirmUnitIdChange{}).ok());
  CHECK_CHAR(r.inst.unit_id, 'D');
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  CHECK_EQ(E(r.drv.SetModbusAddress(10, alicat_basis2::ConfirmAddressChange{}).error),
           E(Error::StateUnknown));
  r.inst.faults = model::Faults{};
  r.inst.modbus_address = 10; // the write did arrive
  REQUIRE(r.drv.Recover().ok());
}

TEST_CASE("guarded: ChangeBaud switches the port in time, the reply arrives at the new speed") {
  model::FullRig r;
  REQUIRE(Start(r));
  CHECK(r.drv.ChangeBaud(Baud::B19200, alicat_basis2::ConfirmBaudChange{}).ok());
  CHECK_EQ(r.inst.baud, 19200U);
  CHECK_EQ(r.inst.port_baud, 19200U);
  CHECK_EQ(r.drv.GetConfig().baud, 19200U);
  CHECK(r.drv.ReadFlow().ok());
  CHECK(r.drv.ChangeBaud(Baud::B115200, alicat_basis2::ConfirmBaudChange{}).ok());
  CHECK(r.drv.ReadFlow().ok());
}

TEST_CASE("guarded: ChangeBaud over Modbus uses register 21") {
  model::FullRig r(model::ModbusConfig());
  REQUIRE(Start(r));
  CHECK(r.drv.ChangeBaud(Baud::B9600, alicat_basis2::ConfirmBaudChange{}).ok());
  CHECK_EQ(r.inst.baud, 9600U);
  CHECK(r.drv.ReadFlow().ok());
}

TEST_CASE(
    "guarded: ChangeBaud failures return the port to the old speed once and leave StateUnknown") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  const auto bad = r.drv.ChangeBaud(Baud::B57600, alicat_basis2::ConfirmBaudChange{});
  CHECK_EQ(E(bad.error), E(Error::StateUnknown));
  CHECK_EQ(r.inst.port_baud, 38400U);
  CHECK_EQ(r.drv.GetConfig().baud, 38400U);
}

TEST_CASE("guarded: ChangeBaud needs the SetBaud hook and the firmware for 57600") {
  model::BareRig bare;
  REQUIRE(Start(bare));
  CHECK_EQ(E(bare.drv.ChangeBaud(Baud::B9600, alicat_basis2::ConfirmBaudChange{}).error),
           E(Error::Unsupported));
  model::FullRig old;
  old.inst.firmware = 0x210;
  REQUIRE(Start(old));
  CHECK_EQ(E(old.drv.ChangeBaud(Baud::B57600, alicat_basis2::ConfirmBaudChange{}).error),
           E(Error::FirmwareTooOld));
  CHECK(old.drv.ChangeBaud(Baud::B9600, alicat_basis2::ConfirmBaudChange{}).ok());
  CHECK_EQ(E(old.drv.ChangeBaud(Baud{9}, alicat_basis2::ConfirmBaudChange{}).error),
           E(Error::InvalidArgument));
}

TEST_CASE("guarded: the instrument rejects a baud change, the port goes back to the old speed") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.knobs.reject_baud = true;
  // On a wire the refusal comes at the old speed while the port listens at the new one: nothing
  // intelligible arrives, so the driver cannot tell and must treat the line as unconfirmed.
  const auto rej = r.drv.ChangeBaud(Baud::B9600, alicat_basis2::ConfirmBaudChange{});
  CHECK(!rej.ok());
  CHECK_EQ(r.inst.port_baud, r.inst.baud); // the port went back to the instrument's speed
  CHECK(r.drv.IsStateUnknown());
  CHECK(r.drv.Recover().ok());
  CHECK(r.drv.ReadFlow().ok());
}

TEST_CASE("guarded: protocol 1 is refused for SmL/s units and abandons the driver otherwise") {
  model::FullRig bad;
  bad.inst.unit_label = "SmL/s";
  bad.inst.unit_code = 4;
  REQUIRE(Start(bad));
  CHECK_EQ(E(bad.drv.SwitchToProtocol1(alicat_basis2::ConfirmAbandonProtocol2{}).error),
           E(Error::Unsupported));
  model::FullRig r;
  REQUIRE(Start(r));
  CHECK(r.drv.SwitchToProtocol1(alicat_basis2::ConfirmAbandonProtocol2{}).ok());
  CHECK_EQ(r.inst.protocol, 1);
  CHECK_EQ(E(r.drv.ReadFlow().error), E(Error::Unsupported));
  CHECK(r.drv.SwitchToProtocol2(alicat_basis2::ConfirmReturnToProtocol2{}).ok());
  CHECK_EQ(r.inst.protocol, 2);
  CHECK(r.drv.ReadFlow().ok());
}

TEST_CASE("guarded: protocol switch over Modbus") {
  model::FullRig r(model::ModbusConfig());
  REQUIRE(Start(r));
  CHECK_EQ(E(r.drv.SwitchToProtocol1(alicat_basis2::ConfirmAbandonProtocol2{}).error),
           E(Error::Ok));
  CHECK_EQ(E(r.drv.SwitchToProtocol2(alicat_basis2::ConfirmReturnToProtocol2{}).error),
           E(Error::Ok));
}

TEST_CASE("guarded: FactoryRestore holds the bus, leaves StateUnknown and Recover relearns the "
          "instrument") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.unit_id = 'A';
  CHECK(r.drv.FactoryRestore(alicat_basis2::ConfirmFactoryRestore{}).ok());
  CHECK_EQ(r.inst.restores, 1U);
  CHECK_EQ(E(r.drv.ReadFlow().error), E(Error::StateUnknown));
  REQUIRE(r.drv.Recover().ok());
  CHECK(r.drv.ReadFlow().ok());
  model::FullRig m(model::ModbusConfig());
  REQUIRE(Start(m));
  CHECK(m.drv.FactoryRestore(alicat_basis2::ConfirmFactoryRestore{}).ok());
  CHECK_EQ(m.inst.restores, 1U);
  REQUIRE(m.drv.Recover().ok());
}

TEST_CASE("guarded: the setpoint source saved is rate limited, unsaved and analog are not") {
  alicat_basis2::Config c{};
  model::FullRig r(c);
  REQUIRE(Start(r));
  CHECK(r.drv.SetSetpointSource(SetpointSource::SavedDigital).ok());
  CHECK(r.drv.SetSetpoint(10.0).ok());
  CHECK_EQ(E(r.drv.SetSetpoint(20.0).error), E(Error::RateLimited));
  CHECK_EQ(r.inst.nvm_writes, 2U);    // the source and one setpoint
  CHECK(r.drv.SetSetpoint(0.0).ok()); // a switch-off is never rate limited
  CHECK_EQ(r.inst.nvm_writes, 3U);
  r.inst.AdvanceMs(181000);
  CHECK(r.drv.SetSetpoint(20.0).ok());
  CHECK(r.drv.SetSetpointSource(SetpointSource::UnsavedDigital).ok());
  CHECK(r.drv.SetSetpoint(30.0).ok());
  CHECK(r.drv.SetSetpoint(40.0).ok());
}

TEST_CASE("guarded: the watchdog protects only Modbus setpoints with the unsaved source") {
  using alicat_basis2::Basis2;
  using alicat_basis2::Protocol;
  using P = Basis2<model::FullPort>;
  CHECK(P::WatchdogProtects(Protocol::ModbusRtu, SetpointSource::UnsavedDigital));
  CHECK(!P::WatchdogProtects(Protocol::ModbusRtu, SetpointSource::SavedDigital));
  CHECK(!P::WatchdogProtects(Protocol::ModbusRtu, SetpointSource::Analog));
  CHECK(!P::WatchdogProtects(Protocol::Ascii, SetpointSource::UnsavedDigital));
  CHECK(!P::WatchdogProtects(Protocol::Ascii, SetpointSource::SavedDigital));
}

TEST_CASE("scan: ASCII scan sends only polls, finds the instrument, ignores other IDs") {
  model::FullRig r;
  r.inst.unit_id = 'K';
  alicat_basis2::Config c{};
  c.unit_id = 'K';
  model::FullRig k(c);
  k.inst.unit_id = 'K';
  REQUIRE(Start(k));
  const std::size_t n = k.inst.writes.size();
  const auto s = k.drv.ScanUnitIds();
  REQUIRE(s.ok());
  CHECK_EQ(s.value.found, 1U << static_cast<uint32_t>(model::kUpper.find('K')));
  CHECK_EQ(k.inst.writes.size() - n, 26U);
  for (std::size_t i = n; i < k.inst.writes.size(); ++i) {
    CHECK_EQ(k.inst.writes[i].text.size(), 2U); // an ID and a CR
  }
  CHECK_CHAR(k.drv.GetConfig().unit_id, 'K');
}

TEST_CASE("scan: Modbus scan sends only function 3 on one register, over the asked range") {
  model::FullRig r(model::ModbusConfig());
  r.inst.modbus_address = 3;
  REQUIRE(r.drv.EnsureInitialized().ok());
  alicat_basis2::Config c = model::ModbusConfig();
  c.modbus_address = 3;
  model::FullRig m(c);
  m.inst.modbus_address = 3;
  REQUIRE(Start(m));
  const std::size_t n = m.inst.writes.size();
  const auto s = m.drv.ScanModbusAddresses(1, 6);
  REQUIRE(s.ok());
  CHECK_EQ(s.value.found[0], 1U << 3);
  CHECK_EQ(m.inst.writes.size() - n, 6U);
  for (std::size_t i = n; i < m.inst.writes.size(); ++i) {
    CHECK_EQ(static_cast<uint8_t>(m.inst.writes[i].text[1]), 3U);
  }
  CHECK_EQ(E(m.drv.ScanModbusAddresses(0, 6).error), E(Error::InvalidArgument));
  CHECK_EQ(E(m.drv.ScanModbusAddresses(5, 4).error), E(Error::InvalidArgument));
  CHECK_EQ(E(m.drv.ScanModbusAddresses(1, 248).error), E(Error::InvalidArgument));
  CHECK_EQ(E(m.drv.ScanUnitIds().error), E(Error::Unsupported));
  model::FullRig a;
  REQUIRE(Start(a));
  CHECK_EQ(E(a.drv.ScanModbusAddresses(1, 2).error), E(Error::Unsupported));
}
