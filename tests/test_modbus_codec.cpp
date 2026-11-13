/**
 * @file test_modbus_codec.cpp
 * @brief The Modbus RTU codec on its own: CRC vectors, exact request bytes, legality, response
 * parsing, and the firmware gates of the register table.
 */
#include <string>
#include <vector>

#include "mock_basis2.hpp"
#include "test_harness.hpp"

namespace {

namespace modbus = alicat_basis2::modbus;
using alicat_basis2::Error;

int E(Error e) {
  return static_cast<int>(e);
}

std::string Hex(const alicat_basis2::Request& r) {
  static const char* const kDigits = "0123456789ABCDEF";
  std::string s;
  for (const uint8_t b : r.Bytes()) {
    s.push_back(kDigits[b >> 4U]);
    s.push_back(kDigits[b & 15U]);
    s.push_back(' ');
  }
  if (!s.empty()) {
    s.pop_back();
  }
  return s;
}

std::string Frame(std::initializer_list<uint8_t> body) {
  std::vector<uint8_t> v(body);
  const uint16_t crc = modbus::Crc16(std::span<const uint8_t>{v.data(), v.size()});
  v.push_back(static_cast<uint8_t>(crc & 0xFFU));
  v.push_back(static_cast<uint8_t>(crc >> 8U));
  return {v.begin(), v.end()};
}

modbus::Response Parse(const std::string& f, uint8_t addr, uint8_t fn,
                       const alicat_basis2::Request& req, Error& e) {
  modbus::Response r{};
  e = modbus::ParseResponse(std::span<const char>{f.data(), f.size()}, addr, fn, req.Bytes(), r);
  return r;
}

} // namespace

TEST_CASE("modbus codec: CRC-16 known vectors") {
  const uint8_t a[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
  CHECK_EQ(modbus::Crc16(std::span<const uint8_t>{a, 6}), 0xCDC5U); // sent low byte first: C5 CD
  const uint8_t b[] = {0x01, 0x04, 0x02, 0xFF, 0xFF};
  CHECK_EQ(modbus::Crc16(std::span<const uint8_t>{b, 5}), 0x80B8U);
  CHECK_EQ(modbus::Crc16(std::span<const uint8_t>{}), 0xFFFFU);
}

TEST_CASE("modbus codec: requests are built byte for byte") {
  CHECK_STR(Hex(modbus::BuildRead(1, 25, 1).value), "01 03 00 19 00 01 55 CD");
  CHECK_STR(Hex(modbus::BuildRead(1, 2100, 10).value).substr(0, 17), "01 03 08 34 00 0A");
  CHECK_STR(Hex(modbus::BuildWriteSingle(1, 39, 0xAA55).value).substr(0, 17), "01 06 00 27 AA 55");
  CHECK_STR(Hex(modbus::BuildWriteSingle(1, 80, 0x5214).value).substr(0, 17), "01 06 00 50 52 14");
  CHECK_STR(Hex(modbus::BuildWriteMultiple(1, 2053, 500000).value).substr(0, 32),
            "01 10 08 05 00 02 04 00 07 A1 20");
  CHECK_STR(Hex(modbus::BuildWriteMultiple(7, 524, 1'000'000'000U).value).substr(0, 32),
            "07 10 02 0C 00 02 04 3B 9A CA 00");
  for (uint32_t addr = 1; addr <= 247; addr += 41) {
    CHECK(modbus::IsLegalModbusRequest(
        modbus::BuildRead(static_cast<uint8_t>(addr), 25, 1).value.Bytes()));
  }
}

TEST_CASE("modbus codec: illegal requests are never built") {
  CHECK_EQ(E(modbus::BuildRead(0, 25, 1).error), E(Error::InvalidArgument));
  CHECK_EQ(E(modbus::BuildRead(248, 25, 1).error), E(Error::InvalidArgument));
  CHECK_EQ(E(modbus::BuildRead(1, 25, 0).error), E(Error::InvalidArgument));
  CHECK_EQ(E(modbus::BuildRead(1, 25, 61).error), E(Error::InvalidArgument));
  CHECK_EQ(E(modbus::BuildRead(1, 0, 1).error),
           E(Error::InvalidArgument)); // address 0 is not a register
  CHECK_EQ(E(modbus::BuildRead(1, 39, 1).error), E(Error::InvalidArgument)); // write-only register
  CHECK_EQ(E(modbus::BuildRead(1, 3000, 1).error), E(Error::InvalidArgument));      // undefined
  CHECK_EQ(E(modbus::BuildWriteSingle(1, 25, 1).error), E(Error::InvalidArgument)); // read-only
  CHECK_EQ(E(modbus::BuildWriteSingle(1, 39, 0x1234).error),
           E(Error::InvalidArgument)); // wrong magic
  CHECK_EQ(E(modbus::BuildWriteSingle(1, 80, 0xAA55).error), E(Error::InvalidArgument));
  CHECK_EQ(E(modbus::BuildWriteSingle(1, 53, 0x5214).error), E(Error::InvalidArgument));
  CHECK_EQ(E(modbus::BuildWriteSingle(1, 514, 5001).error), E(Error::InvalidArgument));
  CHECK_EQ(E(modbus::BuildWriteSingle(1, 516, 3).error), E(Error::InvalidArgument));
  CHECK_EQ(E(modbus::BuildWriteSingle(1, 45, 0).error), E(Error::InvalidArgument));
  CHECK_EQ(E(modbus::BuildWriteSingle(1, 46, 64).error), E(Error::InvalidArgument));
  CHECK_EQ(E(modbus::BuildWriteSingle(1, 2054, 1).error),
           E(Error::InvalidArgument)); // the high register is written first
  CHECK_EQ(E(modbus::BuildWriteMultiple(1, 524, 1'000'000'001U).error), E(Error::InvalidArgument));
  CHECK_EQ(E(modbus::BuildWriteMultiple(1, 25, 1).error), E(Error::InvalidArgument));
  CHECK_EQ(E(modbus::BuildWriteMultiple(0, 2053, 1).error), E(Error::InvalidArgument));
}

TEST_CASE("modbus codec: the independent legality check rejects tampered frames") {
  const auto good = modbus::BuildWriteMultiple(1, 2053, 500000).value;
  std::vector<uint8_t> b(good.Bytes().begin(), good.Bytes().end());
  CHECK(modbus::IsLegalModbusRequest(std::span<const uint8_t>{b.data(), b.size()}));
  for (std::size_t i = 0; i < b.size(); ++i) {
    std::vector<uint8_t> t = b;
    t[i] = static_cast<uint8_t>(t[i] ^ 0x01U);
    CHECK(!modbus::IsLegalModbusRequest(std::span<const uint8_t>{t.data(), t.size()}));
  }
  CHECK(!modbus::IsLegalModbusRequest(std::span<const uint8_t>{b.data(), 7}));
  std::vector<uint8_t> f8 = {0x01, 0x08, 0x00, 0x00, 0x00, 0x00};
  const uint16_t crc = modbus::Crc16(std::span<const uint8_t>{f8.data(), f8.size()});
  f8.push_back(static_cast<uint8_t>(crc & 0xFF));
  f8.push_back(static_cast<uint8_t>(crc >> 8));
  CHECK(!modbus::IsLegalModbusRequest(
      std::span<const uint8_t>{f8.data(), f8.size()})); // function 8 is never sent
}

TEST_CASE("modbus codec: responses are validated") {
  const auto req = modbus::BuildRead(1, 25, 2).value;
  Error e{};
  auto r = Parse(Frame({1, 3, 4, 0x03, 0x05, 0x00, 0x07}), 1, 3, req, e);
  CHECK_EQ(E(e), E(Error::Ok));
  CHECK_EQ(r.count, 2U);
  CHECK_EQ(r.words[0], 0x0305U);
  static_cast<void>(Parse(Frame({1, 3, 4, 0x03, 0x05, 0x00, 0x07}) + "x", 1, 3, req, e));
  CHECK(e != Error::Ok);
  static_cast<void>(Parse(Frame({1, 3, 4, 0x03, 0x05, 0x00, 0x07}).substr(0, 6), 1, 3, req, e));
  CHECK_EQ(E(e), E(Error::Crc)); // a truncated frame fails its CRC
  std::string bad = Frame({1, 3, 4, 0x03, 0x05, 0x00, 0x07});
  bad[3] = static_cast<char>(static_cast<uint32_t>(bad[3]) ^ 1U);
  static_cast<void>(Parse(bad, 1, 3, req, e));
  CHECK_EQ(E(e), E(Error::Crc));
  static_cast<void>(Parse(Frame({2, 3, 4, 0x03, 0x05, 0x00, 0x07}), 1, 3, req, e));
  CHECK_EQ(E(e), E(Error::DeviceIdMismatch));
  static_cast<void>(Parse(Frame({1, 4, 4, 0x03, 0x05, 0x00, 0x07}), 1, 3, req, e));
  CHECK_EQ(E(e), E(Error::BadReply)); // wrong function
  static_cast<void>(Parse(Frame({1, 3, 5, 0x03, 0x05, 0x00, 0x07}), 1, 3, req, e));
  CHECK_EQ(E(e), E(Error::BadReply)); // byte count and length disagree
  static_cast<void>(Parse(Frame({1, 3, 3, 0x03, 0x05, 0x00}), 1, 3, req, e));
  CHECK_EQ(E(e), E(Error::BadReply)); // odd byte count
  static_cast<void>(Parse(Frame({1, 3, 0}), 1, 3, req, e));
  CHECK_EQ(E(e), E(Error::BadReply)); // empty read
  static_cast<void>(Parse(std::string("ab"), 1, 3, req, e));
  CHECK_EQ(E(e), E(Error::BadReply));
  for (uint8_t code = 1; code <= 6; ++code) {
    r = Parse(Frame({1, 0x83, code}), 1, 3, req, e);
    CHECK_EQ(E(e), E(Error::Rejected));
    CHECK_EQ(r.exception, code);
  }
  static_cast<void>(Parse(Frame({1, 0x83, 2, 9}), 1, 3, req, e));
  CHECK_EQ(E(e), E(Error::BadReply)); // an exception reply of the wrong length
  // A reply with more registers than the driver ever asks for is refused, not copied past the end.
  std::vector<uint8_t> big{1, 3, 124};
  big.resize(3U + 124U, 0x11);
  const uint16_t crc = modbus::Crc16(std::span<const uint8_t>{big.data(), big.size()});
  big.push_back(static_cast<uint8_t>(crc & 0xFFU));
  big.push_back(static_cast<uint8_t>(crc >> 8U));
  const std::string long_reply(big.begin(), big.end());
  static_cast<void>(Parse(long_reply, 1, 3, req, e));
  CHECK_EQ(E(e), E(Error::BadReply));
}

TEST_CASE("modbus codec: a write's echo must match, for function 6 the value written") {
  const auto w6 = modbus::BuildWriteSingle(1, 514, 1000).value;
  Error e{};
  const std::string echo(reinterpret_cast<const char*>(w6.Bytes().data()), 8);
  static_cast<void>(Parse(echo, 1, 6, w6, e));
  CHECK_EQ(E(e), E(Error::Ok));
  const std::string other = Frame({1, 6, 2, 2, 0x03, 0xE9});
  static_cast<void>(Parse(other, 1, 6, w6, e));
  CHECK_EQ(E(e), E(Error::BadReply));
  const auto w16 = modbus::BuildWriteMultiple(1, 2053, 1000).value;
  const std::string ack = Frame({1, 16, 8, 5, 0, 2});
  static_cast<void>(Parse(ack, 1, 16, w16, e));
  CHECK_EQ(E(e), E(Error::Ok));
  static_cast<void>(Parse(Frame({1, 16, 8, 5, 0, 3}), 1, 16, w16, e));
  CHECK_EQ(E(e), E(Error::BadReply));
}

TEST_CASE("modbus codec: ReplyLength follows the function code") {
  const auto len = [](const std::string& s) {
    return modbus::ReplyLength(std::span<const char>{s.data(), s.size()});
  };
  CHECK_EQ(len(std::string("\x01")), 0U);
  CHECK_EQ(len(std::string("\x01\x03")), 0U);
  CHECK_EQ(len(std::string("\x01\x03\x04")), 9U);
  CHECK_EQ(len(std::string("\x01\x83")), 5U);
  CHECK_EQ(len(std::string("\x01\x06")), 8U);
  CHECK_EQ(len(std::string("\x01\x10")), 8U);
  CHECK(len(std::string("\x01\x08")) > alicat_basis2::kMaxLine);
}

TEST_CASE("firmware: a call that needs a newer firmware fails without sending, Ignore sends") {
  for (const uint16_t firmware : {uint16_t{0x210}, uint16_t{0x240}}) {
    alicat_basis2::Config cfg = model::ModbusConfig();
    cfg.full_scale_override_milli = 500000; // register 47 exists from firmware 2.4.0 only
    model::FullRig r(cfg);
    r.inst.firmware = firmware;
    REQUIRE(r.drv.EnsureInitialized().ok());
    const auto begin = r.drv.Begin();
    CHECK(begin.ok());
    const std::size_t n = r.inst.writes.size();
    const auto gas = r.drv.ReadGasTable(); // needs 3.0.5
    CHECK_EQ(E(gas.error), E(Error::FirmwareTooOld));
    CHECK_EQ(r.inst.writes.size(), n);
    const auto tot = r.drv.ResetTotal(); // needs 2.5.5
    CHECK_EQ(E(tot.error), E(Error::FirmwareTooOld));
    CHECK_EQ(r.inst.writes.size(), n);
    CHECK_EQ(E(r.drv.SetSetpoint(1.0).error), E(Error::Ok)); // register 2053 exists from 2.1.0
  }
  alicat_basis2::Config c = model::ModbusConfig();
  c.firmware_gate = alicat_basis2::FirmwareGate::Ignore;
  model::FullRig ign(c);
  ign.inst.firmware = 0x210;
  REQUIRE(ign.drv.EnsureInitialized().ok());
  const std::size_t n = ign.inst.writes.size();
  static_cast<void>(ign.drv.ReadGasTable());
  CHECK(ign.inst.writes.size() > n); // sent, the instrument answers with its exception
}

TEST_CASE("firmware: ASCII GS * is gated, unknown firmware follows assume_firmware") {
  model::FullRig r;
  r.inst.firmware = 0x300;
  REQUIRE(r.drv.EnsureInitialized().ok());
  REQUIRE(r.drv.Begin().ok());
  const std::size_t n = r.inst.writes.size();
  CHECK_EQ(E(r.drv.ReadGasList().error), E(Error::FirmwareTooOld));
  CHECK_EQ(r.inst.writes.size(), n);
  model::FullRig unknown;
  unknown.inst.knobs.fw_format = model::FwFormat::Garbage;
  unknown.inst.firmware = 0x200;
  REQUIRE(unknown.drv.EnsureInitialized().ok());
  REQUIRE(unknown.drv.Begin().ok());
  CHECK(!unknown.drv.GetIdentity().firmware_known);
  const std::size_t m = unknown.inst.writes.size();
  CHECK_EQ(E(unknown.drv.ReadGasList().error),
           E(Error::Rejected)); // unknown firmware: sent, the instrument says no
  CHECK(unknown.inst.writes.size() > m);
  alicat_basis2::Config c{};
  c.assume_firmware = 0x210;
  model::FullRig assumed(c);
  assumed.inst.knobs.fw_format = model::FwFormat::Garbage;
  REQUIRE(assumed.drv.EnsureInitialized().ok());
  REQUIRE(assumed.drv.Begin().ok());
  const std::size_t k = assumed.inst.writes.size();
  CHECK_EQ(E(assumed.drv.ReadGasList().error), E(Error::FirmwareTooOld));
  CHECK_EQ(assumed.inst.writes.size(), k);
}

TEST_CASE("modbus codec: a stuck or floating line is never a reply") {
  const auto req = modbus::BuildRead(1, 25, 1).value;
  Error e{};
  for (std::size_t n = 1; n <= 16; ++n) {
    for (const char fill : {'\x00', '\xFF', '\x01', '\x03'}) {
      const std::string stuck(n, fill);
      static_cast<void>(Parse(stuck, 1, 3, req, e));
      CHECK(e != Error::Ok);
    }
  }
  const std::string echo(reinterpret_cast<const char*>(req.Bytes().data()), req.Bytes().size());
  static_cast<void>(
      Parse(echo, 1, 3, req, e)); // the request echoed back (a half-duplex line that hears itself)
  CHECK(e != Error::Ok);
}
