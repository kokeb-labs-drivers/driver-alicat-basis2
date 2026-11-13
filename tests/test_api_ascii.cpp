/**
 * @file test_api_ascii.cpp
 * @brief Every API call over the ASCII protocol against the independent model, for a controller and
 * a meter.
 */
#include "rig.hpp"

using alicat_basis2::Baud;
using alicat_basis2::DataItem;
using alicat_basis2::Error;
using alicat_basis2::FullScaleItem;
using alicat_basis2::GasId;
using alicat_basis2::SetpointSource;
using alicat_basis2::TimeUnit;
using alicat_basis2::TotalizerLimit;
using rig::E;
using rig::Last;
using rig::Start;
using rig::Wire;

TEST_CASE("ascii: Begin reads, never writes, and learns the instrument") {
  model::FullRig r;
  REQUIRE(r.drv.EnsureInitialized().ok());
  REQUIRE(r.drv.Begin().ok());
  const auto& id = r.drv.GetIdentity();
  CHECK(id.kind == alicat_basis2::DeviceKind::Controller);
  CHECK_EQ(id.firmware, 0x305U);
  CHECK(id.firmware_known);
  CHECK(id.full_scale_known);
  CHECK_EQ(id.full_scale.ToMilli(), 500000);
  CHECK(id.flow_unit == alicat_basis2::FlowUnit::Sccm);
  CHECK(id.gas_known);
  CHECK(r.drv.IsReady());
  // Every byte of Begin() is a read: no setter appears on the wire.
  for (const auto& w : r.inst.writes) {
    const std::string body = w.text.substr(1);
    CHECK(body.find('=') == std::string::npos);
    CHECK(body.rfind("S ", 0) != 0);
    CHECK(body.rfind("V ", 0) != 0);
  }
}

TEST_CASE("ascii: data reads return the model's values") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.flow_milli = 123400;
  r.inst.setpoint_milli = 150000;
  r.inst.temperature_c100 = 2312;
  r.inst.total_milli = 21513000;
  r.inst.valve_milli = 45670;
  r.inst.batch_remaining_milli = 5500;
  r.inst.mov = true;
  r.inst.hld = true;
  CHECK_EQ(r.drv.ReadFlow().value.ToMilli(), 123400);
  CHECK_EQ(r.drv.ReadTemperature().value.ToMilli(), 23120);
  CHECK_EQ(r.drv.ReadTotal().value.ToMilli(), 21513000);
  CHECK_EQ(r.drv.ReadSetpointValue().value.ToMilli(), 150000);
  CHECK_EQ(r.drv.ReadValveDrive().value.ToMilli(), 45670);
  CHECK_EQ(r.drv.ReadBatchRemaining().value.ToMilli(), 5500);
  const auto st = r.drv.ReadStatus();
  CHECK(st.value.mass_flow_over);
  CHECK(st.value.valve_hold);
  CHECK(!st.value.temperature_over);
  const auto f = r.drv.ReadFrame();
  REQUIRE(f.ok());
  CHECK_EQ(f.value.flow.ToMilli(), 123400);
  CHECK(f.value.gas_known);
  CHECK_EQ(f.value.gas.number, 8U);
  const auto g = r.drv.ReadGas();
  CHECK_EQ(g.value.gas.number, 8U);
}

TEST_CASE("ascii: DV for every mask is decoded in table order") {
  for (int status_int = 0; status_int < 2; ++status_int) {
    model::FullRig r;
    r.inst.knobs.status_as_integer = status_int == 1;
    REQUIRE(Start(r));
    r.inst.flow_milli = 100000;
    r.inst.setpoint_milli = 110000;
    r.inst.temperature_c100 = 2000;
    r.inst.vtm = true;
    for (unsigned mask = 1; mask < 256; ++mask) {
      const auto f = r.drv.ReadData(static_cast<uint8_t>(mask));
      CHECK_EQ(E(f.error), E(Error::Ok));
      if (f.ok()) {
        CHECK_EQ(f.value.present, mask);
        if ((mask & 1U) != 0U) {
          CHECK_EQ(f.value.flow.ToMilli(), 100000);
        }
        if ((mask & 2U) != 0U) {
          CHECK_EQ(f.value.setpoint.ToMilli(), 110000);
        }
        if ((mask & 128U) != 0U) {
          CHECK(f.value.status.valve_thermal);
        }
      }
    }
  }
}

TEST_CASE("ascii: setpoint, source, ramp, watchdog and valve") {
  model::FullRig r;
  REQUIRE(Start(r));
  CHECK(r.drv.SetSetpoint(150.0).ok());
  CHECK_STR(r.inst.writes[r.inst.writes.size() - 2U].text.substr(0, 7), std::string("AS 150."));
  CHECK_STR(Last(r.inst), std::string("ADV 2"));
  CHECK_EQ(r.inst.setpoint_milli, 150000);
  CHECK_EQ(E(r.drv.SetSetpoint(-1.0).error), E(Error::InvalidArgument));
  CHECK_EQ(E(r.drv.SetSetpoint(600.0).error), E(Error::InvalidArgument));
  CHECK_EQ(r.drv.ReadSetpointSource().value == SetpointSource::UnsavedDigital, true);
  CHECK(r.drv.SetSetpointSource(SetpointSource::Analog).ok());
  CHECK_STR(Last(r.inst), std::string("ALSS A"));
  CHECK(r.drv.ReadSetpointSource().value == SetpointSource::Analog);
  CHECK(r.drv.SetSetpointSource(SetpointSource::UnsavedDigital).ok());
  CHECK(r.drv.SetRamp(60.0, TimeUnit::Minute).ok());
  CHECK_EQ(r.inst.ramp_unit, 5);
  const auto ramp = r.drv.ReadRamp();
  CHECK_EQ(ramp.value.rate.ToMilli(), 60000);
  CHECK_EQ(ramp.value.time_unit, 5U);
  // With a ramp active the instrument lags on purpose: no read-back is made.
  const std::size_t before = r.inst.writes.size();
  CHECK(r.drv.SetSetpoint(100.0).ok());
  CHECK_EQ(r.inst.writes.size(), before + 1U);
  CHECK(r.drv.DisableRamp().ok());
  CHECK_EQ(r.inst.ramp_rate_milli, 0U);
  CHECK(r.drv.SetWatchdog(1000).ok());
  CHECK_EQ(r.drv.ReadWatchdog().value, 1000U);
  CHECK_EQ(E(r.drv.SetWatchdog(5001).error), E(Error::InvalidArgument));
  CHECK(r.drv.HoldValve(37.5).ok());
  CHECK(r.inst.hld);
  CHECK(r.drv.OpenValve().ok());
  CHECK_STR(Last(r.inst), std::string("AHPUR 100"));
  CHECK(r.drv.CloseValve().ok());
  CHECK_STR(Last(r.inst), std::string("AHPUR 0"));
  CHECK(r.drv.ResumeControl().ok());
  CHECK(!r.inst.hld);
  CHECK_EQ(E(r.drv.HoldValve(100.1).error), E(Error::InvalidArgument));
}

TEST_CASE("ascii: setpoint verify catches a differing read-back, a silent setter is a timeout") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.clamp_setpoint_milli = 100000;
  CHECK_EQ(E(r.drv.SetSetpoint(200.0).error), E(Error::VerifyMismatch));
  CHECK(r.drv.SetSetpoint(80.0).ok());
  alicat_basis2::Config off{};
  off.verify_setpoint = alicat_basis2::VerifyPolicy::Off;
  model::FullRig q(off);
  REQUIRE(Start(q));
  q.inst.clamp_setpoint_milli = 100000;
  CHECK(q.drv.SetSetpoint(200.0).ok()); // no read-back asked for
  model::FullRig s;
  REQUIRE(Start(s));
  s.inst.knobs.set_reply = model::SetReply::Silent;
  CHECK_EQ(E(s.drv.SetSetpoint(200.0).error), E(Error::Timeout));
}

TEST_CASE("ascii: tuning, measurement window and tare") {
  model::FullRig r;
  REQUIRE(Start(r));
  CHECK(r.drv.SetGains(120, 340).ok());
  const auto g = r.drv.ReadGains();
  CHECK_EQ(g.value.proportional, 120U);
  CHECK_EQ(g.value.integral, 340U);
  CHECK(r.drv.SetAutotare(true).ok());
  CHECK(r.drv.ReadAutotare().value);
  CHECK(r.drv.SetMeasurementTrigger(3).ok());
  CHECK_EQ(r.drv.ReadMeasurementTrigger().value, 3U);
  CHECK_EQ(E(r.drv.SetMeasurementTrigger(8).error), E(Error::InvalidArgument));
  r.inst.flow_milli = 200000;
  CHECK(r.drv.StartMeasurement(4000).ok());
  CHECK_STR(Last(r.inst), std::string("ADVAS 4000"));
  r.inst.measure_ms = 4000;
  r.inst.avg_flow_milli = 199000;
  r.inst.min_flow_milli = 198000;
  r.inst.max_flow_milli = 201000;
  const auto a = r.drv.ReadAverages();
  CHECK_EQ(a.value.time_ms, 4000U);
  CHECK_EQ(a.value.flow.ToMilli(), 199000);
  const auto rg = r.drv.ReadRanges();
  CHECK_EQ(rg.value.min_flow.ToMilli(), 198000);
  CHECK_EQ(rg.value.max_flow.ToMilli(), 201000);
  CHECK_EQ(E(r.drv.StartMeasurement(0).error), E(Error::InvalidArgument));
  CHECK(r.drv.Tare(500).ok());
  CHECK_STR(Last(r.inst), std::string("AV 500"));
  CHECK_EQ(r.inst.tares, 1U);
  CHECK(r.drv.TareDefault().ok());
  CHECK_EQ(E(r.drv.Tare(0).error), E(Error::InvalidArgument));
  CHECK_EQ(E(r.drv.Tare(32768).error), E(Error::InvalidArgument));
  CHECK_EQ(E(r.drv.ReadPreviousMeasurement().error), E(Error::Unsupported));
}

TEST_CASE("ascii: gas, totalizer, batch, averaging and reference temperature") {
  model::FullRig r;
  REQUIRE(Start(r));
  CHECK(r.drv.SetGas(GasId{3}).ok());
  CHECK_EQ(r.inst.gas, 3);
  CHECK_EQ(E(r.drv.SetGas(GasId{200}).error), E(Error::Rejected));
  const auto list = r.drv.ReadGasList();
  REQUIRE(list.ok());
  CHECK_EQ(list.value.count, 9U);
  CHECK(!list.value.raw);
  r.inst.knobs.list_format_ok = false;
  const auto raw = r.drv.ReadGasList();
  CHECK(raw.value.raw);
  CHECK(r.drv.ResetTotal().ok());
  CHECK_EQ(r.inst.total_resets, 1U);
  CHECK(r.drv.SetTotalizerLimit(TotalizerLimit::ResetWithError).ok());
  CHECK(r.drv.ReadTotalizerLimit().value == TotalizerLimit::ResetWithError);
  CHECK(r.drv.SetBatch(12.5).ok());
  CHECK_EQ(r.inst.batch_milli, 12500);
  CHECK(r.drv.DisableBatch().ok());
  CHECK_EQ(r.inst.batch_milli, 0);
  CHECK_EQ(E(r.drv.SetBatch(0.0).error), E(Error::InvalidArgument));
  CHECK(r.drv.SetAveraging(500).ok());
  CHECK_EQ(r.drv.ReadAveraging().value, 500U);
  CHECK_EQ(E(r.drv.SetAveraging(2501).error), E(Error::InvalidArgument));
  CHECK(r.drv.SetReferenceTemperature(25.0).ok());
  CHECK_EQ(r.drv.ReadReferenceTemperature().value.ToMilli(), 25000);
  CHECK_EQ(E(r.drv.SetReferenceTemperature(31.0).error), E(Error::InvalidArgument));
}

TEST_CASE("ascii: identity reads") {
  model::FullRig r;
  REQUIRE(Start(r));
  const auto sn = r.drv.ReadSerialNumber();
  CHECK_STR(std::string(sn.value.chars.data()), std::string("SN123456"));
  CHECK_STR(std::string(r.drv.ReadFirmware().value.chars.data()), std::string("3.0.5"));
  for (int item = 0; item < 3; ++item) {
    const auto fs = r.drv.ReadFullScale(static_cast<FullScaleItem>(item));
    CHECK_EQ(E(fs.error), E(Error::Ok));
  }
  CHECK_EQ(r.drv.ReadModbusAddress().value, 1U);
  CHECK(r.drv.ReadBaud().value == Baud::B38400);
  CHECK(r.drv.ReadProtocol().value == alicat_basis2::CommandProtocol::BasisProtocol2);
  CHECK_EQ(E(r.drv.ReadFlowUnit().error), E(Error::Unsupported));
  CHECK_EQ(E(r.drv.ReadFullScaleSccm().error), E(Error::Unsupported));
  CHECK_EQ(E(r.drv.ReadSensorOffset().error), E(Error::Unsupported));
  CHECK_EQ(E(r.drv.ReadGasTable().error), E(Error::Unsupported));
}

TEST_CASE("ascii: a meter refuses controller commands without sending them") {
  model::FullRig r;
  r.inst.controller = false;
  REQUIRE(Start(r));
  CHECK(r.drv.GetIdentity().kind == alicat_basis2::DeviceKind::Meter);
  const std::size_t n = r.inst.writes.size();
  CHECK_EQ(E(r.drv.SetSetpoint(10.0).error), E(Error::Unsupported));
  CHECK_EQ(E(r.drv.HoldValve(10.0).error), E(Error::Unsupported));
  CHECK_EQ(E(r.drv.ReadSetpointSource().error), E(Error::Unsupported));
  CHECK_EQ(E(r.drv.SetSetpointSource(SetpointSource::Analog).error), E(Error::Unsupported));
  CHECK_EQ(E(r.drv.ReadRamp().error), E(Error::Unsupported));
  CHECK_EQ(E(r.drv.SetRamp(1.0, TimeUnit::Second).error), E(Error::Unsupported));
  CHECK_EQ(E(r.drv.DisableRamp().error), E(Error::Unsupported));
  CHECK_EQ(E(r.drv.ReadWatchdog().error), E(Error::Unsupported));
  CHECK_EQ(E(r.drv.SetWatchdog(1).error), E(Error::Unsupported));
  CHECK_EQ(E(r.drv.ReadGains().error), E(Error::Unsupported));
  CHECK_EQ(E(r.drv.SetGains(1, 1).error), E(Error::Unsupported));
  CHECK_EQ(E(r.drv.ReadAutotare().error), E(Error::Unsupported));
  CHECK_EQ(E(r.drv.SetAutotare(true).error), E(Error::Unsupported));
  CHECK_EQ(E(r.drv.ReadMeasurementTrigger().error), E(Error::Unsupported));
  CHECK_EQ(E(r.drv.SetMeasurementTrigger(1).error), E(Error::Unsupported));
  CHECK_EQ(E(r.drv.SetBatch(1.0).error), E(Error::Unsupported));
  CHECK_EQ(E(r.drv.DisableBatch().error), E(Error::Unsupported));
  CHECK_EQ(E(r.drv.ReadData(2).error), E(Error::Unsupported));
  CHECK_EQ(r.inst.writes.size(), n);
  CHECK(r.drv.ReadFlow().ok());
}

TEST_CASE("ascii: frames of both layouts, spacings and flag styles are decoded") {
  for (int cols = 0; cols < 2; ++cols) {
    for (int brackets = 0; brackets < 2; ++brackets) {
      for (int ctrl = 0; ctrl < 2; ++ctrl) {
        model::FullRig r;
        r.inst.knobs.aligned_columns = cols == 1;
        r.inst.knobs.bracket_flags = brackets == 1;
        r.inst.controller = ctrl == 1;
        r.inst.hld = true;
        r.inst.vtm = true;
        r.inst.flow_milli = 50000;
        REQUIRE(Start(r));
        const auto f = r.drv.ReadFrame();
        CHECK_EQ(E(f.error), E(Error::Ok));
        CHECK(f.value.status.valve_hold);
        CHECK(f.value.status.valve_thermal);
        CHECK_EQ(f.value.flow.ToMilli(), 50000);
      }
    }
  }
}

TEST_CASE("ascii: firmware text variants are parsed, garbage leaves it unknown") {
  const model::FwFormat formats[4] = {model::FwFormat::Dotted, model::FwFormat::V3v0v5,
                                      model::FwFormat::VDotted, model::FwFormat::Garbage};
  for (const auto fmt : formats) {
    model::FullRig r;
    r.inst.knobs.fw_format = fmt;
    REQUIRE(Start(r));
    if (fmt == model::FwFormat::Garbage) {
      CHECK(!r.drv.GetIdentity().firmware_known);
    } else {
      CHECK_EQ(r.drv.GetIdentity().firmware, 0x305U);
    }
  }
}

TEST_CASE("ascii: a raw line read returns the reply as received, reads only and without retry") {
  model::FullRig r(alicat_basis2::Config{});
  REQUIRE(Start(r));
  const auto sn = r.drv.ReadRawLine(alicat_basis2::Command::ReadSerialNumber);
  REQUIRE(sn.ok());
  CHECK_CHAR(sn.value.data[0], 'A');
  CHECK_EQ(r.drv.GetLastReply().length, sn.value.length);
  CHECK_EQ(E(r.drv.ReadRawLine(alicat_basis2::Command::SetSetpoint).error),
           E(Error::InvalidArgument));
  CHECK_EQ(E(r.drv.ReadRawLine(alicat_basis2::Command::Tare).error), E(Error::InvalidArgument));
  CHECK_EQ(E(r.drv.ReadRegisterRaw(25, 1).error), E(Error::Unsupported));
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  const std::size_t n = r.inst.writes.size();
  CHECK(!r.drv.ReadRawLine(alicat_basis2::Command::ReadFirmware).ok());
  CHECK_EQ(r.inst.writes.size(), n + 1U);
}
