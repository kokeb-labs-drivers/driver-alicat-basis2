/**
 * @file test_decode.cpp
 * @brief The reply decoders on their own: numbers, tokens, frames, plausibility, and the short
 * reply forms.
 */
#include <string>

#include "alicat_basis2.hpp"
#include "test_harness.hpp"

namespace {

namespace ascii = alicat_basis2::ascii;
using alicat_basis2::Decimal;
using alicat_basis2::Error;

std::span<const char> S(const std::string& s) {
  return std::span<const char>{s.data(), s.size()};
}

bool Parse(const std::string& s, Decimal& d) {
  return ascii::ParseDecimal(S(s), d);
}

alicat_basis2::Frame Decode(const std::string& line, Error& e, const ascii::FrameRules& rules) {
  alicat_basis2::Frame f{};
  alicat_basis2::FrameLayout layout{};
  e = ascii::DecodeFrame(S(line), rules, f, layout);
  return f;
}

ascii::FrameRules Rules() {
  ascii::FrameRules r{};
  r.unit_id = 'A';
  r.full_scale_milli = 500000;
  r.full_scale_known = true;
  return r;
}

int E(Error e) {
  return static_cast<int>(e);
}

Decimal Dec(int64_t m, uint8_t d) {
  return Decimal{m, d};
}

} // namespace

TEST_CASE("numbers: accepted forms keep their decimal count") {
  Decimal d{};
  CHECK(Parse("+24.57", d));
  CHECK_EQ(d.mantissa, 2457);
  CHECK_EQ(d.decimals, 2U);
  CHECK(Parse("-0.03", d));
  CHECK_EQ(d.mantissa, -3);
  CHECK(Parse("0", d));
  CHECK_EQ(d.decimals, 0U);
  CHECK(Parse("+0021513.0", d));
  CHECK_EQ(d.mantissa, 215130);
  CHECK_EQ(d.decimals, 1U);
  CHECK(Parse("100", d));
  CHECK(Parse("123456789012", d));
  CHECK(Parse("1.123456", d));
  CHECK_EQ(Dec(2457, 2).ToMilli(), 24570);
  CHECK_EQ(Dec(-15, 1).ToMilli(), -1500);
  CHECK_EQ(Dec(12345, 4).ToMilli(), 1235); // rounds half away from zero
  CHECK_EQ(Dec(-12345, 4).ToMilli(), -1235);
  CHECK((Dec(25, 1).ToDouble() > 2.49) && (Dec(25, 1).ToDouble() < 2.51));
  CHECK(Dec(5, 0) == Dec(5, 0));
  CHECK(!(Dec(5, 0) == Dec(50, 1)));
}

TEST_CASE("numbers: everything else is refused") {
  Decimal d{};
  const char* const bad[] = {"",    "+",   "-",   ".5",    "5.",  "1e3",           "0x10",
                             "nan", "inf", "--1", "1.2.3", "+-1", "1234567890123", "1.1234567",
                             "1,5", "1 2", "A1"};
  for (const char* b : bad) {
    CHECK(!Parse(b, d));
  }
  const std::string nul("1\0"
                        "2",
                        3);
  CHECK(!Parse(nul, d));
}

TEST_CASE("tokens: runs of spaces split, control characters and long tokens are refused") {
  ascii::Tokens t{};
  const std::string ok = "A   +1.0  N2";
  CHECK_EQ(E(ascii::Tokenize(S(ok), t)), E(Error::Ok));
  CHECK_EQ(t.count, 3U);
  CHECK_EQ(E(ascii::Tokenize(S(std::string("")), t)), E(Error::BadReply));
  CHECK_EQ(E(ascii::Tokenize(S(std::string("   ")), t)), E(Error::BadReply));
  CHECK_EQ(E(ascii::Tokenize(S(std::string("A\x01")), t)), E(Error::BadReply));
  CHECK_EQ(E(ascii::Tokenize(S(std::string("A \x80")), t)), E(Error::BadReply));
  CHECK_EQ(E(ascii::Tokenize(S(std::string("A ") + std::string(40, 'X')), t)), E(Error::BadReply));
  std::string many = "A";
  for (int i = 0; i < 30; ++i) {
    many += " 1";
  }
  CHECK_EQ(E(ascii::Tokenize(S(many), t)), E(Error::BadReply));
}

TEST_CASE("frame: the manual's example, with every flag, in both flag styles") {
  Error e{};
  auto f = Decode("A  +24.57 +100.0 +0021513.0 +100.0  +55.13  N2 [TOV][MOV][OVR][HLD][VTM]", e,
                  Rules());
  CHECK_EQ(E(e), E(Error::Ok));
  CHECK(f.status.temperature_over && f.status.mass_flow_over && f.status.totalizer_over &&
        f.status.valve_hold && f.status.valve_thermal);
  CHECK_EQ(f.temperature.ToMilli(), 24570);
  CHECK_EQ(f.setpoint.ToMilli(), 100000);
  CHECK_EQ(f.valve_drive.ToMilli(), 55130);
  CHECK_STR(f.gas_name.data(), "N2");
  CHECK_EQ(f.gas.number, 3U);
  f = Decode("A +24.57 +100.0 +0021513.0 +100.0 +55.13 N2 TOV MOV", e, Rules());
  CHECK_EQ(E(e), E(Error::Ok));
  CHECK(f.status.temperature_over && f.status.mass_flow_over && !f.status.valve_hold);
  f = Decode("A +24.57 +100.0 +21513.0 N2", e, Rules());
  CHECK_EQ(E(e), E(Error::Ok));
  CHECK_EQ(f.present & 2U, 0U); // a meter's frame has no setpoint
  f = Decode("a +24.57 +100.0 +21513.0 n2 [hld]", e, Rules());
  CHECK_EQ(E(e), E(Error::Ok)); // the case of the ID, gas and flags does not matter
  CHECK(f.status.valve_hold);
}

TEST_CASE("frame: malformed frames are refused and expose nothing") {
  Error e{};
  const char* const bad[] = {
      "A +24.57 +100.0 +21513.0",               // no gas
      "A +24.57 +100.0 N2",                     // two numbers
      "A +24.57 +100.0 +1 +2 +3 +4 N2",         // six numbers
      "A +24.57 +100.0 +21513.0 N2 [HLD][HLD]", // duplicate flag
      "A +24.57 +100.0 +21513.0 N2 [HLD",       // unterminated bracket
      "A +24.57 +100.0 +21513.0 N2 []",         // empty bracket
      "A +24.57 +100.0 +21513.0 NITROGEN9",     // gas token of nine characters
      "A +24.57 +100.0 +21513.0 N#",            // non-alphanumeric gas
      "A +2e1 +100.0 +21513.0 N2",              // exponent
      "A +24.57 nan +21513.0 N2",               // nan
      "",
  };
  for (const char* b : bad) {
    const auto f = Decode(b, e, Rules());
    CHECK(e != Error::Ok);
    CHECK_EQ(f.flow.mantissa, 0);
  }
  static_cast<void>(Decode("B +24.57 +100.0 +21513.0 N2", e, Rules()));
  CHECK_EQ(E(e), E(Error::DeviceIdMismatch));
  static_cast<void>(Decode("AB +24.57 +100.0 +21513.0 N2", e, Rules()));
  CHECK_EQ(E(e), E(Error::DeviceIdMismatch));
}

TEST_CASE("frame: implausible values are OutOfRange, never exposed") {
  Error e{};
  static_cast<void>(
      Decode("A +24.57 +900.0 +21513.0 N2", e, Rules())); // flow above full scale x 1.1
  CHECK_EQ(E(e), E(Error::OutOfRange));
  static_cast<void>(Decode("A +24.57 -900.0 +21513.0 N2", e, Rules()));
  CHECK_EQ(E(e), E(Error::OutOfRange));
  static_cast<void>(Decode("A +324.57 +100.0 +21513.0 N2", e, Rules())); // temperature
  CHECK_EQ(E(e), E(Error::OutOfRange));
  static_cast<void>(Decode("A +24.57 +100.0 -5.0 N2", e, Rules())); // negative total
  CHECK_EQ(E(e), E(Error::OutOfRange));
  static_cast<void>(
      Decode("A +24.57 +100.0 +1.0 +900.0 +10.0 N2", e, Rules())); // setpoint above 102.5 %
  CHECK_EQ(E(e), E(Error::OutOfRange));
  static_cast<void>(
      Decode("A +24.57 +100.0 +1.0 +100.0 +150.0 N2", e, Rules())); // valve drive above 100.5 %
  CHECK_EQ(E(e), E(Error::OutOfRange));
  static_cast<void>(
      Decode("A +24.57 +100.0 +1.0 +100.0 +100.4 N2", e, Rules())); // 100.4 is within the margin
  CHECK_EQ(E(e), E(Error::Ok));
  ascii::FrameRules none = Rules();
  none.full_scale_known = false;
  // without a full scale there is nothing to compare with
  static_cast<void>(Decode("A +24.57 +900.0 +21513.0 N2", e, none));
  CHECK_EQ(E(e), E(Error::Ok));
}

TEST_CASE("frame: unknown flags follow the policy, a fixed layout rejects the other") {
  Error e{};
  auto f = Decode("A +24.57 +100.0 +21513.0 N2 [XYZ]", e, Rules());
  CHECK_EQ(E(e), E(Error::Ok));
  CHECK_EQ(f.status.unknown_count, 1U);
  ascii::FrameRules strict = Rules();
  strict.flags = alicat_basis2::UnknownFlagPolicy::Reject;
  static_cast<void>(Decode("A +24.57 +100.0 +21513.0 N2 [XYZ]", e, strict));
  CHECK_EQ(E(e), E(Error::BadReply));
  ascii::FrameRules meter = Rules();
  meter.layout = alicat_basis2::FrameLayout::Meter;
  static_cast<void>(Decode("A +24.57 +100.0 +21513.0 +100.0 +55.13 N2", e, meter));
  CHECK_EQ(E(e), E(Error::BadReply));
  ascii::FrameRules ctrl = Rules();
  ctrl.layout = alicat_basis2::FrameLayout::Controller;
  static_cast<void>(Decode("A +24.57 +100.0 +21513.0 N2", e, ctrl));
  CHECK_EQ(E(e), E(Error::BadReply));
}

TEST_CASE("frame: gas names, an unknown token is kept raw") {
  Error e{};
  const char* const names[9] = {"Air", "Ar", "CO2", "N2", "O2", "N2O", "H2", "He", "CH4"};
  for (uint8_t i = 0; i < 9; ++i) {
    const auto f = Decode(std::string("A +24.57 +100.0 +21513.0 ").append(names[i]), e, Rules());
    CHECK_EQ(E(e), E(Error::Ok));
    CHECK(f.gas_known);
    CHECK_EQ(f.gas.number, i);
    CHECK_STR(alicat_basis2::GasName(i), names[i]);
  }
  const auto f = Decode("A +24.57 +100.0 +21513.0 Xe8", e, Rules());
  CHECK(!f.gas_known);
  CHECK_STR(f.gas_name.data(), "Xe8");
  CHECK(alicat_basis2::GasName(200) == nullptr);
}

TEST_CASE("short replies: values, letters, text, gas, full scale, windows and the error token") {
  ascii::Values v{};
  CHECK_EQ(E(ascii::DecodeValues(S("A 1 2"), 'A', 2, v)), E(Error::Ok));
  CHECK_EQ(E(ascii::DecodeValues(S("A 1"), 'A', 2, v)), E(Error::BadReply));
  CHECK_EQ(E(ascii::DecodeValues(S("B 1 2"), 'A', 2, v)), E(Error::DeviceIdMismatch));
  CHECK_EQ(E(ascii::DecodeValues(S("A 1 x"), 'A', 2, v)), E(Error::BadReply));
  CHECK_EQ(E(ascii::DecodeValues(S("A 1 2"), 'A', 0, v)), E(Error::BadReply));
  char c = 0;
  CHECK_EQ(E(ascii::DecodeLetter(S("A u"), 'A', "ASU", c)), E(Error::Ok));
  CHECK_CHAR(c, 'U');
  CHECK_EQ(E(ascii::DecodeLetter(S("A Q"), 'A', "ASU", c)), E(Error::BadReply));
  CHECK_EQ(E(ascii::DecodeLetter(S("A AS"), 'A', "ASU", c)), E(Error::BadReply));
  alicat_basis2::Text t{};
  CHECK_EQ(E(ascii::DecodeText(S("A V3.0.5  "), 'A', t)), E(Error::Ok));
  CHECK_STR(t.chars.data(), "V3.0.5");
  CHECK_EQ(E(ascii::DecodeText(S("A"), 'A', t)), E(Error::BadReply));
  alicat_basis2::Frame g{};
  CHECK_EQ(E(ascii::DecodeGas(S("A 8 CH4"), 'A', g)), E(Error::Ok));
  CHECK_EQ(g.gas.number, 8U);
  CHECK_EQ(E(ascii::DecodeGas(S("A 300 CH4"), 'A', g)), E(Error::BadReply));
  CHECK_EQ(E(ascii::DecodeGas(S("A 8"), 'A', g)), E(Error::BadReply));
  alicat_basis2::FullScale fs{};
  CHECK_EQ(E(ascii::DecodeFullScale(S("A +500.000 SCCM"), 'A', fs)), E(Error::Ok));
  CHECK_EQ(fs.value.ToMilli(), 500000);
  CHECK_STR(fs.unit.chars.data(), "SCCM");
  CHECK_EQ(E(ascii::DecodeFullScale(S("A +500.000"), 'A', fs)), E(Error::BadReply));
  uint32_t ms = 0;
  CHECK_EQ(E(ascii::DecodeWindow(S("A 4000 +25.00 +99.0"), 'A', 2, ms, v)), E(Error::Ok));
  CHECK_EQ(ms, 4000U);
  CHECK_EQ(E(ascii::DecodeWindow(S("A -4 +25.00 +99.0"), 'A', 2, ms, v)), E(Error::BadReply));
  CHECK_EQ(E(ascii::DecodeWindow(S("A 4.5 +25.00 +99.0"), 'A', 2, ms, v)), E(Error::BadReply));
  CHECK(ascii::IsErrorToken(S("?"), 'A'));
  CHECK(ascii::IsErrorToken(S("A ?"), 'A'));
  CHECK(!ascii::IsErrorToken(S("B ?"), 'A'));
  CHECK(!ascii::IsErrorToken(S("A ? ?"), 'A'));
  CHECK(!ascii::IsErrorToken(S("A +1"), 'A'));
}

TEST_CASE("units: every label of the manual maps to its code, an unknown one is Other") {
  const char* const labels[20] = {"SCCM",  "NCCM",  "SLPM", "NLPM", "SmL/s", "NmL/s", "SmL/m",
                                  "NmL/m", "SL/h",  "NL/h", "SCCS", "NCCS",  "Sm3/h", "Nm3/h",
                                  "Sm3/d", "Nm3/d", "SCIM", "SCFM", "SCFH",  "SCFD"};
  for (uint8_t i = 0; i < 20; ++i) {
    CHECK(ascii::FlowUnitFromLabel(S(labels[i])) == static_cast<alicat_basis2::FlowUnit>(i));
    CHECK(alicat_basis2::reg::FlowUnitFromCode(i) == static_cast<alicat_basis2::FlowUnit>(i));
  }
  CHECK(ascii::FlowUnitFromLabel(S("sccm")) == alicat_basis2::FlowUnit::Sccm);
  CHECK(ascii::FlowUnitFromLabel(S("gal/h")) == alicat_basis2::FlowUnit::Other);
  CHECK(alicat_basis2::reg::FlowUnitFromCode(20) == alicat_basis2::FlowUnit::Other);
}

TEST_CASE("gas list: nine entries, twelve entries, free text is kept raw") {
  alicat_basis2::GasList g{};
  CHECK_EQ(E(ascii::DecodeGasList(S("A 0 Air 3 N2 8 CH4"), 'A', g)), E(Error::Ok));
  CHECK_EQ(g.count, 3U);
  CHECK(!g.raw);
  std::string twelve = "A";
  for (int i = 0; i < 12; ++i) {
    twelve += " " + std::to_string(i) + " G" + std::to_string(i);
  }
  CHECK_EQ(E(ascii::DecodeGasList(S(twelve), 'A', g)), E(Error::Ok));
  CHECK_EQ(g.count, 12U);
  CHECK_EQ(E(ascii::DecodeGasList(S("A some text here"), 'A', g)), E(Error::Ok));
  CHECK(g.raw);
  CHECK_EQ(E(ascii::DecodeGasList(S("B 0 Air"), 'A', g)), E(Error::DeviceIdMismatch));
}

TEST_CASE("data: a DV reply decodes status flags and integers, refuses extra tokens") {
  ascii::FrameRules r = Rules();
  alicat_basis2::Frame f{};
  CHECK_EQ(E(ascii::DecodeData(S("A +100.0 N2 [HLD]"), 1U | 16U | 128U, r, f)), E(Error::Ok));
  CHECK(f.status.valve_hold);
  CHECK_EQ(E(ascii::DecodeData(S("A +100.0 N2 9"), 1U | 16U | 128U, r, f)),
           E(Error::Ok)); // 9 = MOV + HLD
  CHECK(f.status.valve_hold && f.status.mass_flow_over);
  CHECK_EQ(E(ascii::DecodeData(S("A +100.0 extra"), 1U, r, f)), E(Error::BadReply));
  CHECK_EQ(E(ascii::DecodeData(S("A"), 1U, r, f)), E(Error::BadReply));
  CHECK_EQ(E(ascii::DecodeData(S("B +100.0"), 1U, r, f)), E(Error::DeviceIdMismatch));
  CHECK_EQ(E(ascii::DecodeData(S("A +x"), 1U, r, f)), E(Error::BadReply));
  CHECK_EQ(E(ascii::DecodeData(S("A +999.0"), 1U, r, f)), E(Error::OutOfRange));
}

TEST_CASE("data: a stuck or floating line is never a frame") {
  Error e{};
  for (std::size_t n = 1; n <= 40; ++n) {
    for (const char fill : {'\x00', '\xFF', ' ', '+', '0'}) {
      static_cast<void>(Decode(std::string(n, fill), e, Rules()));
      CHECK(e != Error::Ok);
    }
  }
  static_cast<void>(Decode("AA", e, Rules())); // the command echoed back
  CHECK(e != Error::Ok);
}
