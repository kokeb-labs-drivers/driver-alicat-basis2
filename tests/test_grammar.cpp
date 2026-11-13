/**
 * @file test_grammar.cpp
 * @brief The ASCII command table against the manual: golden bytes for every row, the independent
 * wire validator, mutations, and argument bounds.
 */
#include <array>
#include <string>

#include "mock_basis2.hpp"
#include "test_harness.hpp"

namespace {

using alicat_basis2::Command;
using alicat_basis2::Error;
namespace ascii = alicat_basis2::ascii;

struct Golden {
  Command command;
  ascii::Args args;
  const char* wire; ///< Without the CR.
};

ascii::Args A(int64_t a = 0, int64_t b = 0, char c = '\0', uint8_t dec = 3, bool trim = true) {
  return ascii::Args{a, b, c, dec, trim};
}

const std::array<Golden, 46> kGolden{{
    {Command::Poll, A(), "A"},
    {Command::ReadData, A(12), "ADV 12"},
    {Command::SetSetpoint, A(15440, 0, '\0', 3, false), "AS 15.440"},
    {Command::ReadSetpointSource, A(), "ALSS"},
    {Command::SetSetpointSource, A(0, 0, 'A'), "ALSS A"},
    {Command::ReadRamp, A(), "ASR"},
    {Command::SetRamp, A(600000, 3), "ASR 600 3"},
    {Command::DisableRamp, A(0), "ASR 0"},
    {Command::ReadWatchdog, A(), "AWD"},
    {Command::SetWatchdog, A(10), "AWD 10"},
    {Command::HoldValve, A(10000), "AHPUR 10"},
    {Command::ResumeControl, A(), "AC"},
    {Command::ReadGains, A(), "ALCG"},
    {Command::SetGains, A(500, 5000), "ALCG 500 5000"},
    {Command::ReadReferenceTemperature, A(), "ART"},
    {Command::SetReferenceTemperature, A(22000), "ART 22"},
    {Command::ReadAveraging, A(), "ADCA"},
    {Command::SetAveraging, A(400), "ADCA 400"},
    {Command::ReadTrigger, A(), "AMT"},
    {Command::SetTrigger, A(3), "AMT 3"},
    {Command::StartMeasurement, A(4000), "ADVAS 4000"},
    {Command::ReadAverages, A(), "ADVAA"},
    {Command::ReadRanges, A(), "ADVAR"},
    {Command::Tare, A(1000), "AV 1000"},
    {Command::ReadAutotare, A(), "AZCA"},
    {Command::SetAutotare, A(1), "AZCA 1"},
    {Command::ReadGas, A(), "AGS"},
    {Command::ReadGasList, A(), "AGS *"},
    {Command::SetGas, A(8), "AGS 8"},
    {Command::ResetTotal, A(), "AT"},
    {Command::SetBatch, A(200000), "ATB 200"},
    {Command::DisableBatch, A(0), "ATB 0"},
    {Command::ReadTotalizerLimit, A(), "ATC"},
    {Command::SetTotalizerLimit, A(1), "ATC 1"},
    {Command::ReadFullScale, A(2), "AFPF 2"},
    {Command::ChangeUnitId, A(0, 0, 'B'), "A@=B"},
    {Command::ReadSerialNumber, A(), "ASN"},
    {Command::ReadFirmware, A(), "AVE"},
    {Command::ReadModbusAddress, A(), "AMA"},
    {Command::SetModbusAddress, A(5), "AMA 5"},
    {Command::ReadBaud, A(), "ANCB"},
    {Command::ChangeBaud, A(19200), "ANCB 19200"},
    {Command::ReadProtocol, A(), "AP"},
    {Command::SwitchToProtocol1, A(1), "AP 1"},
    {Command::SwitchToProtocol2, A(), "AP2"},
    {Command::FactoryRestore, A(), "AFACTORY RESTORE"},
}};

std::string Text(const alicat_basis2::Request& r) {
  std::string s;
  for (const uint8_t b : r.Bytes()) {
    s.push_back(model::CharOf(b));
  }
  return s;
}

bool Legal(const std::string& s) {
  return ascii::IsLegalRequest(
      std::span<const uint8_t>{reinterpret_cast<const uint8_t*>(s.data()), s.size()});
}

} // namespace

TEST_CASE(
    "grammar: every row encodes to the documented bytes and passes the independent validator") {
  for (const Golden& g : kGolden) {
    const auto made = ascii::Make(g.command, 'A', g.args, true);
    REQUIRE(made.ok());
    CHECK_STR(Text(made.value), std::string(g.wire) + "\r");
    CHECK(Legal(Text(made.value)));
    CHECK(made.value.GetCommand() == g.command);
    CHECK(!made.value.IsModbus());
  }
}

TEST_CASE("grammar: the six guarded rows need the guard, the others never do") {
  int count = 0;
  for (const Golden& g : kGolden) {
    const auto denied = ascii::Make(g.command, 'A', g.args, false);
    if (!denied.ok()) {
      ++count;
      CHECK(ascii::Spec(g.command).guarded);
      CHECK(ascii::Make(g.command, 'A', g.args, true).ok());
    }
  }
  CHECK_EQ(count, 6); // @=, MA n, NCB n, P 1, P2, FACTORY RESTORE
}

TEST_CASE("grammar: spaces sit exactly where the manual puts them") {
  CHECK_STR(Text(ascii::Make(Command::StartMeasurement, 'A', A(4000), false).value),
            "ADVAS 4000\r");
  CHECK_STR(Text(ascii::Make(Command::SwitchToProtocol2, 'A', A(), true).value), "AP2\r");
  CHECK_STR(Text(ascii::Make(Command::SwitchToProtocol1, 'A', A(1), true).value), "AP 1\r");
  CHECK_STR(Text(ascii::Make(Command::ChangeUnitId, 'A', A(0, 0, 'Z'), true).value), "A@=Z\r");
}

TEST_CASE("grammar: every unit ID A to Z is accepted, nothing else") {
  for (const char id : model::kUpper) {
    const auto r = ascii::Make(Command::Poll, id, A(), false);
    REQUIRE(r.ok());
    CHECK_CHAR(Text(r.value)[0], id);
  }
  const char bad[] = {'@', '[', 'a', '*', ' ', '0', '\0'};
  for (const char id : bad) {
    CHECK_EQ(static_cast<int>(ascii::Make(Command::Poll, id, A(), false).error),
             static_cast<int>(Error::InvalidArgument));
  }
}

TEST_CASE("grammar: argument bounds are InvalidArgument, nothing is formed") {
  struct Case {
    Command c;
    ascii::Args a;
  };
  const Case bad[] = {
      {Command::ReadData, A(0)},
      {Command::ReadData, A(256)},
      {Command::SetSetpoint, A(-1)},
      {Command::HoldValve, A(100001)},
      {Command::HoldValve, A(-1)},
      {Command::SetBatch, A(-5)},
      {Command::SetBatch, A(0)},
      {Command::SetRamp, A(1000, 2)},
      {Command::SetRamp, A(1000, 6)},
      {Command::SetRamp, A(0, 3)},
      {Command::SetGains, A(65536, 1)},
      {Command::SetGains, A(1, 65536)},
      {Command::SetWatchdog, A(5001)},
      {Command::SetAveraging, A(2501)},
      {Command::Tare, A(0)},
      {Command::Tare, A(32768)},
      {Command::SetModbusAddress, A(0)},
      {Command::SetModbusAddress, A(248)},
      {Command::ChangeBaud, A(12345)},
      {Command::SetGas, A(256)},
      {Command::SetTrigger, A(8)},
      {Command::ReadFullScale, A(3)},
      {Command::SetTotalizerLimit, A(4)},
      {Command::SetAutotare, A(2)},
      {Command::SetSetpointSource, A(0, 0, 'Q')},
      {Command::ChangeUnitId, A(0, 0, 'a')},
      {Command::StartMeasurement, A(0)},
      {Command::StartMeasurement, A(163838)},
      {Command::SetReferenceTemperature, A(30001)},
      {Command::SetSetpoint, A(1000, 0, '\0', 7)},
  };
  for (const Case& c : bad) {
    CHECK_EQ(static_cast<int>(ascii::Make(c.c, 'A', c.a, true).error),
             static_cast<int>(Error::InvalidArgument));
  }
  CHECK_EQ(static_cast<int>(ascii::Make(Command::Modbus, 'A', A(), true).error),
           static_cast<int>(Error::InvalidArgument));
}

TEST_CASE("grammar: the validator rejects every definite mutation of every golden string") {
  for (const Golden& g : kGolden) {
    const std::string good = std::string(g.wire) + "\r";
    REQUIRE(Legal(good));
    // A lower-case letter anywhere.
    for (std::size_t i = 0; i < good.size(); ++i) {
      if (model::In(model::kUpper, good[i])) {
        std::string m = good;
        m[i] = model::kLower[model::kUpper.find(m[i])];
        CHECK(!Legal(m));
      }
    }
    // A character outside the alphabet, before the CR.
    for (const char bad : {'#', '?', ',', ';', '\n', '\t', '/', '_'}) {
      std::string m = good;
      m.insert(m.size() - 1U, 1U, bad);
      CHECK(!Legal(m));
    }
    // No CR, two CRs, CR in the middle.
    CHECK(!Legal(good.substr(0, good.size() - 1U)));
    CHECK(!Legal(good + "\r"));
    CHECK(!Legal(good + "A\r"));
    // A trailing space and a leading space.
    CHECK(!Legal(good.substr(0, good.size() - 1U) + " \r"));
    CHECK(!Legal(" " + good));
    // The unit ID replaced by a digit.
    std::string d = good;
    d[0] = '1';
    CHECK(!Legal(d));
  }
  CHECK(!Legal(std::string()));
  CHECK(!Legal(std::string("\r")));
  CHECK(!Legal(std::string("A")));
  CHECK(!Legal(std::string(60, 'A') + "\r"));
}

TEST_CASE("grammar: the validator knows the manual's argument limits on its own") {
  CHECK(Legal("ADV 255\r"));
  CHECK(!Legal("ADV 256\r"));
  CHECK(!Legal("ADV 0\r"));
  CHECK(Legal("ADVAS 163837\r"));
  CHECK(!Legal("ADVAS 163838\r"));
  CHECK(Legal("AHPUR 100.000\r"));
  CHECK(!Legal("AHPUR 100.001\r"));
  CHECK(Legal("ANCB 115200\r"));
  CHECK(!Legal("ANCB 12345\r"));
  CHECK(Legal("AMA 247\r"));
  CHECK(!Legal("AMA 248\r"));
  CHECK(Legal("ASR 0.5 5\r"));
  CHECK(!Legal("ASR 0.5 6\r"));
  CHECK(Legal("ALCG 65535 0\r"));
  CHECK(!Legal("ALCG 65536 0\r"));
  CHECK(Legal("AS 0\r"));
  CHECK(!Legal("AS -1\r"));
  CHECK(Legal("A*\r") == false);
  CHECK(Legal("AGS *\r"));
  CHECK(Legal("ALSS U\r"));
  CHECK(!Legal("ALSS X\r"));
  CHECK(Legal("A@=Z\r"));
  CHECK(!Legal("A@=1\r"));
}

TEST_CASE("grammar: the bus refuses a request outside the grammar and counts it") {
  model::FullRig r;
  alicat_basis2::Request empty{};
  const auto e = r.bus.Execute(empty, alicat_basis2::Timing{});
  CHECK_EQ(static_cast<int>(e.error), static_cast<int>(Error::InvalidArgument));
  CHECK_EQ(r.inst.writes.size(), 0U);
}
