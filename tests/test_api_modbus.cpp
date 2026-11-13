/**
 * @file test_api_modbus.cpp
 * @brief Every API call over Modbus RTU against the independent register model.
 */
#include "rig.hpp"

using alicat_basis2::Baud;
using alicat_basis2::Error;
using alicat_basis2::GasId;
using alicat_basis2::SetpointSource;
using alicat_basis2::TimeUnit;
using alicat_basis2::TotalizerLimit;
using rig::E;
using rig::Start;

TEST_CASE("modbus: Begin needs the flow decimals and learns the instrument") {
  alicat_basis2::Config c = model::ModbusConfig();
  c.modbus_flow_decimals = alicat_basis2::kDecimalsUnset;
  model::FullRig bad(c);
  REQUIRE(bad.drv.EnsureInitialized().ok());
  CHECK_EQ(E(bad.drv.Begin().error), E(Error::InvalidArgument));
  CHECK_EQ(E(bad.drv.ReadFrame().error), E(Error::InvalidArgument));
  CHECK_EQ(bad.inst.writes.size(), 0U);
  model::FullRig r(model::ModbusConfig());
  REQUIRE(Start(r));
  const auto& id = r.drv.GetIdentity();
  CHECK_EQ(id.firmware, 0x305U);
  CHECK(id.full_scale_known);
  CHECK_EQ(id.full_scale.ToMilli(), 500000);
  CHECK(id.gas_known);
  for (const auto& w : r.inst.writes) {
    CHECK(static_cast<uint8_t>(w.text[1]) == 3U); // only function 3 during Begin
  }
}

TEST_CASE("modbus: data reads scale by the decimals") {
  model::FullRig r(model::ModbusConfig());
  REQUIRE(Start(r));
  r.inst.flow_milli = 12300;
  r.inst.setpoint_milli = 15000;
  r.inst.temperature_c100 = 2312;
  r.inst.total_milli = 2151300;
  r.inst.valve_milli = 45670;
  r.inst.batch_remaining_milli = 5500;
  r.inst.mov = true;
  r.inst.vtm = true;
  const auto f = r.drv.ReadFrame();
  REQUIRE(f.ok());
  CHECK_EQ(f.value.flow.ToMilli(), 12300);
  CHECK_EQ(f.value.temperature.ToMilli(), 23120);
  CHECK_EQ(f.value.total.ToMilli(), 2151300);
  CHECK_EQ(f.value.setpoint.ToMilli(), 15000);
  CHECK_EQ(f.value.valve_drive.ToMilli(), 45670);
  CHECK_EQ(f.value.batch_remaining.ToMilli(), 5500);
  CHECK(f.value.status.mass_flow_over);
  CHECK(f.value.status.valve_thermal);
  CHECK(f.value.gas_known);
  CHECK_STR(f.value.gas_name.data(), "CH4");
  CHECK_EQ(r.drv.ReadFlow().value.ToMilli(), 12300);
  CHECK_EQ(r.drv.ReadTemperature().value.ToMilli(), 23120);
  CHECK_EQ(r.drv.ReadTotal().value.ToMilli(), 2151300);
  CHECK_EQ(r.drv.ReadValveDrive().value.ToMilli(), 45670);
  CHECK_EQ(r.drv.ReadSetpointValue().value.ToMilli(), 15000);
  CHECK_EQ(r.drv.ReadBatchRemaining().value.ToMilli(), 5500);
  CHECK(r.drv.ReadStatus().value.mass_flow_over);
  CHECK_EQ(r.drv.ReadGas().value.gas.number, 8U);
  CHECK_EQ(E(r.drv.ReadData(0).error), E(Error::InvalidArgument));
}

TEST_CASE("modbus: the worked example of the manual, 500 SCCM, is written as 0007 A120") {
  model::FullRig r(model::ModbusConfig());
  REQUIRE(Start(r));
  const std::size_t n = r.inst.writes.size();
  CHECK(r.drv.SetSetpoint(500.0).ok());
  const std::string& w = r.inst.writes[n].text;
  REQUIRE(w.size() == 13U);
  CHECK_EQ(static_cast<uint8_t>(w[1]), 16U);
  CHECK_EQ((static_cast<uint8_t>(w[2]) * 256U) + static_cast<uint8_t>(w[3]), 2053U);
  CHECK_EQ((static_cast<uint8_t>(w[7]) * 256U) + static_cast<uint8_t>(w[8]), 7U);
  CHECK_EQ((static_cast<uint8_t>(w[9]) * 256U) + static_cast<uint8_t>(w[10]), 0xA120U);
  CHECK_EQ(r.inst.setpoint_milli, 500000);
  CHECK_EQ(E(r.drv.SetSetpoint(520.0).error), E(Error::InvalidArgument));
}

TEST_CASE("modbus: control settings") {
  model::FullRig r(model::ModbusConfig());
  REQUIRE(Start(r));
  CHECK(r.drv.SetSetpointSource(SetpointSource::SavedDigital).ok());
  CHECK(r.drv.ReadSetpointSource().value == SetpointSource::SavedDigital);
  CHECK(r.drv.SetSetpointSource(SetpointSource::UnsavedDigital).ok());
  CHECK(r.drv.SetRamp(30.0, TimeUnit::Second).ok());
  CHECK_EQ(r.inst.ramp_rate_milli > 0U, true);
  CHECK(r.drv.ReadRamp().ok());
  CHECK(r.drv.DisableRamp().ok());
  CHECK_EQ(r.inst.ramp_rate_milli, 0U);
  CHECK(r.drv.SetWatchdog(2500).ok());
  CHECK_EQ(r.drv.ReadWatchdog().value, 2500U);
  CHECK(r.drv.SetGains(10, 20).ok());
  CHECK_EQ(r.drv.ReadGains().value.integral, 20U);
  CHECK(r.drv.SetAutotare(true).ok());
  CHECK(r.drv.ReadAutotare().value);
  CHECK(r.drv.SetMeasurementTrigger(5).ok());
  CHECK_EQ(r.drv.ReadMeasurementTrigger().value, 5U);
  CHECK_EQ(E(r.drv.HoldValve(10.0).error), E(Error::Unsupported));
  CHECK_EQ(E(r.drv.ResumeControl().error), E(Error::Unsupported));
}

TEST_CASE("modbus: measurement window, tare, totalizer, gas") {
  model::FullRig r(model::ModbusConfig());
  REQUIRE(Start(r));
  r.inst.flow_milli = 20000;
  CHECK(r.drv.StartMeasurement(1000).ok());
  r.inst.avg_flow_milli = 19900;
  r.inst.min_flow_milli = 19500;
  r.inst.max_flow_milli = 20500;
  const auto a = r.drv.ReadAverages();
  CHECK_EQ(a.value.flow.ToMilli(), 19900);
  const auto g = r.drv.ReadRanges();
  CHECK_EQ(g.value.min_flow.ToMilli(), 19500);
  CHECK_EQ(g.value.max_flow.ToMilli(), 20500);
  r.inst.prev_min_milli = 1000;
  r.inst.prev_max_milli = 3000;
  r.inst.prev_avg_milli = 2000;
  r.inst.samples_prev = 77;
  const auto p = r.drv.ReadPreviousMeasurement();
  REQUIRE(p.ok());
  CHECK_EQ(p.value.samples, 77U);
  CHECK_EQ(p.value.average_flow.ToMilli(), 2000);
  CHECK(r.drv.Tare(500).ok());
  CHECK_EQ(r.inst.tares, 1U);
  CHECK_EQ(r.inst.tare_samples, 200U);
  CHECK(r.drv.ResetTotal().ok());
  CHECK_EQ(r.inst.total_resets, 1U);
  CHECK(r.drv.SetTotalizerLimit(TotalizerLimit::HoldWithError).ok());
  CHECK(r.drv.ReadTotalizerLimit().value == TotalizerLimit::HoldWithError);
  CHECK(r.drv.SetGas(GasId{4}).ok());
  CHECK_EQ(r.inst.gas, 4);
  CHECK_EQ(E(r.drv.SetGas(GasId{50}).error),
           E(Error::VerifyMismatch)); // an absent gas leaves the selection
  CHECK(r.drv.SetBatch(12.5).ok());
  CHECK_EQ(r.inst.batch_milli, 12500);
  CHECK(r.drv.DisableBatch().ok());
  CHECK(r.drv.SetAveraging(100).ok());
  CHECK_EQ(r.drv.ReadAveraging().value, 100U);
  CHECK(r.drv.SetReferenceTemperature(21.0).ok());
  CHECK_EQ(r.drv.ReadReferenceTemperature().value.ToMilli(), 21000);
  const auto list = r.drv.ReadGasList();
  CHECK_EQ(list.value.count, 9U);
  const auto table = r.drv.ReadGasTable();
  REQUIRE(table.ok());
  CHECK_EQ(table.value.entries[3].k_factor, 4096U);
  CHECK_STR(table.value.entries[2].label.data(), "CO2");
}

TEST_CASE("modbus: identity reads") {
  model::FullRig r(model::ModbusConfig());
  REQUIRE(Start(r));
  CHECK_STR(r.drv.ReadSerialNumber().value.chars.data(), "SN123456");
  CHECK_STR(r.drv.ReadFirmware().value.chars.data(), "3.0.5");
  CHECK_EQ(r.drv.ReadFullScale(alicat_basis2::FullScaleItem::Flow).value.value.ToMilli(), 500000);
  CHECK_EQ(E(r.drv.ReadFullScale(alicat_basis2::FullScaleItem::TotalVolume).error),
           E(Error::Unsupported));
  CHECK_EQ(r.drv.ReadModbusAddress().value, 1U);
  CHECK(r.drv.ReadBaud().value == Baud::B38400);
  CHECK(r.drv.ReadProtocol().value == alicat_basis2::CommandProtocol::BasisProtocol2);
  CHECK(r.drv.ReadFlowUnit().value == alicat_basis2::FlowUnit::Sccm);
  CHECK_EQ(r.drv.ReadFullScaleSccm().value, 500U);
  CHECK_EQ(r.drv.ReadSensorOffset().value, 8192U);
}

TEST_CASE("modbus: exceptions and faults are reported, the exception code is kept") {
  model::FullRig r(model::ModbusConfig());
  REQUIRE(Start(r));
  r.inst.firmware = 0x305;
  // An undefined register: ReadReg of 1 is not defined in the model.
  const auto bad = r.drv.SetWatchdog(5001);
  CHECK_EQ(E(bad.error), E(Error::InvalidArgument)); // checked before sending
  r.inst.faults.count = 1;
  r.inst.faults.bad_crc = true;
  CHECK_EQ(E(r.drv.ReadFlow().error), E(Error::Ok)); // one retry
  r.inst.faults.count = 2;
  r.inst.faults.bad_crc = true;
  CHECK_EQ(E(r.drv.ReadFlow().error), E(Error::Crc));
  r.inst.faults = model::Faults{};
  r.inst.faults.count = 2;
  r.inst.faults.wrong_address = true;
  CHECK_EQ(E(r.drv.ReadFlow().error), E(Error::DeviceIdMismatch));
  r.inst.faults = model::Faults{};
  r.inst.faults.count = 2;
  r.inst.faults.drop = true;
  CHECK_EQ(E(r.drv.ReadFlow().error), E(Error::Timeout));
  r.inst.faults = model::Faults{};
  CHECK(r.drv.ReadFlow().ok());
}

TEST_CASE("modbus: raw register reads are single shot and the last reply is kept") {
  model::FullRig r(model::ModbusConfig());
  REQUIRE(Start(r));
  const auto fw = r.drv.ReadRegisterRaw(25, 1);
  REQUIRE(fw.ok());
  CHECK_EQ(fw.value.count, 1U);
  CHECK_EQ(fw.value.words[0], 0x0305U);
  CHECK_EQ(r.drv.GetLastReply().length, 7U); // address, function, count, two data bytes, CRC
  CHECK_EQ(E(r.drv.ReadRegisterRaw(25, 0).error), E(Error::InvalidArgument));
  CHECK_EQ(E(r.drv.ReadRegisterRaw(25, 61).error), E(Error::InvalidArgument));
  // One frame, no retry: a lost reply is a timeout after exactly one request.
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  const std::size_t n = r.inst.writes.size();
  CHECK(!r.drv.ReadRegisterRaw(25, 1).ok());
  CHECK_EQ(r.inst.writes.size(), n + 1U);
}
