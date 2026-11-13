/**
 * @file fuzz_common.hpp
 * @brief Shared parts of the four fuzz targets: the input reader, the call table, and the invariant
 * checks.
 *
 * Invariants checked after every call (a violation aborts and names the line):
 *
 * - I1 every `Write()` buffer is a legal request of the grammar (independent validators), ends the
 * line correctly, and was produced for a call the harness made;
 * - I3 the gap between a write and the end of the previous traffic is at least t3.5, nothing is
 * written while a reply is in flight;
 * - I5 the six guarded texts never appear unless the harness made the matching guarded call;
 * - I6 no setpoint above 102.5 % of the full scale, no negative one, no `HPUR` above 100;
 * - I7 decoded values stay in their documented bounds (mantissa below 10^15);
 * - I8 a failed call returns a zero-valued result;
 * - I10 Modbus writes use function 6 or 16 only, on writable registers, with legal magic values;
 * - I12 `StateUnknown` is left only by a successful `Recover()` (asserted after every step);
 * - I14 while `StateUnknown` the ASCII wire carries only a read request of the grammar, a zero
 * setpoint or hold, or `P2`; over Modbus only reads, a zero setpoint and a protocol-2 write;
 * - I13 no command is sent while a reply is in flight unless the session chose a fault;
 * - I15 on a line declared to hold several instruments (`Bus::SetDeviceCount(2)`, chosen by the
 * session input) the unit ID, Modbus address and factory-restore commands are never sent (they
 * are refused), so no other unit is ever adopted: checked through I5, which allows their texts
 * only for a single instrument;
 * - I16 liveness: `SetSetpoint(0.0)` puts at least one frame on the wire unless it returns
 * `NotInitialized`, `Unsupported`, `FirmwareTooOld` (no setpoint register before 2.1.0), or `Busy`
 * when bytes (chatter, a late or a repeated reply) kept arriving for the switch-off's own line
 * wait after the call started (`OffQuietWaitMs()`), so a line that went quiet sooner cannot excuse
 * it; doubt, rate
 * limits and a result nobody took never hold it back.
 * - I9: every call ends within `CallBoundMs()` of the virtual clock: ten seconds, a scan's polls
 * and a hold-off still running.
 * - On a shared line the baud change is refused as well (I15).
 * - I11: the session targets start the clock just below 2^31 or 2^32 ms on some inputs and let
 * 2^31 ms and more of quiet pass between steps, so every wait crosses the half range and the wrap.
 *
 * I2, I4, I9 and I11 are checked by unit tests ([Testing and CI](testing.md)).
 */
#ifndef ALICAT_BASIS2_FUZZ_COMMON_HPP
#define ALICAT_BASIS2_FUZZ_COMMON_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "mock_basis2.hpp"

namespace fuzz {

#define FUZZ_REQUIRE(cond)                                                                         \
  do {                                                                                             \
    if (cond) {                                                                                    \
      break;                                                                                       \
    }                                                                                              \
    static_cast<void>(                                                                             \
        std::fprintf(stderr, "invariant violated at %s:%d: %s\n", __FILE__, __LINE__, #cond));     \
    std::abort();                                                                                  \
  } while (0)

/// The fuzzer's bytes, read front to back; zeros once they run out.
class Input {
public:
  Input(const uint8_t* data, std::size_t size) noexcept : data_(data), size_(size) {}
  [[nodiscard]] bool Empty() const noexcept {
    return pos_ >= size_;
  }
  uint8_t Byte() noexcept {
    return pos_ < size_ ? data_[pos_++] : uint8_t{0};
  }
  uint16_t Word() noexcept {
    const auto high = Byte();
    return static_cast<uint16_t>((static_cast<uint32_t>(high) << 8U) | Byte());
  }
  /// The rest of the input as a string.
  std::string Rest() {
    std::string s;
    while (!Empty()) {
      s.push_back(static_cast<char>(Byte()));
    }
    return s;
  }

private:
  const uint8_t* data_;
  std::size_t size_;
  std::size_t pos_ = 0;
};

/// I16: how long the line must stay busy before a switch-off may end `Busy` unsent: its line wait
/// (t3.5, the reply's line time, the turnaround) less one t3.5 (the gap may already have run) and
/// 2 ms of poll slack. The request is built as the driver builds it, to get its reply length.
[[nodiscard]] inline uint64_t OffQuietWaitMs(const alicat_basis2::Config& c, bool modbus) {
  using namespace alicat_basis2;
  uint32_t chars = 0U;
  uint32_t gap = 0U;
  uint32_t line = 0U;
  if (modbus) {
    const auto req = modbus::BuildWriteMultiple(c.modbus_address, reg::kSetpoint, 0U);
    chars = req.ok() ? req.value.ReplyChars() : 0U;
    gap = detail::ModbusGapMs(c.baud);
    line = detail::ModbusFrameMs(chars, c.baud);
  } else {
    ascii::Args args{};
    args.dec = c.setpoint_decimals;
    const auto req = ascii::Make(Command::SetSetpoint, c.unit_id, args, false);
    chars = req.ok() ? req.value.ReplyChars() : 0U;
    gap = detail::T35Ms(c.baud);
    line = detail::LineMs(chars, c.baud);
  }
  const uint32_t line_wait = gap + line + c.processing_ms;
  return (line_wait > (gap + 2U)) ? (line_wait - gap - 2U) : 0U;
}

/// What a session knows across its steps (I9, I15, I16).
struct SessionState {
  bool shared{false};        ///< The line is declared to hold two instruments (I15).
  uint64_t hold_until_us{0}; ///< The end of a measurement window or restore hold-off asked for.
};

/// I9: how long one call may take, in milliseconds. Ten seconds for any call (a tare of up to 3 s,
/// a few reads with retries and quarantines), plus a hold-off the session asked for earlier and
/// that is still running, plus a scan's polls: up to 247 addresses (26 IDs over ASCII) at up to
/// three speeds, two tries each, half a second per try (gap, request, reply wait, quarantine).
[[nodiscard]] inline uint64_t CallBoundMs(uint8_t op, bool modbus, uint64_t pending_ms) {
  const uint64_t ids = modbus ? 247U : 26U;
  uint64_t scan = 0U;
  if (op == 39U) {
    scan = (ids * 3U * 2U * 500U) + 20'000U; // Recover: the scan, then Begin()
  } else if (op == 40U) {
    scan = 10'000U; // Begin(): about a dozen reads
  } else if ((op == 41U) && !modbus) {
    scan = uint64_t{26U} * 2U * 500U; // ScanUnitIds
  } else {
    scan = 0U;
  }
  return 10'000U + pending_ms + scan;
}

/// Faults chosen by the input: bit fields for the kinds, and small parameters.
inline model::Faults ChooseFaults(Input& in) {
  model::Faults f;
  const uint8_t kinds = in.Byte();
  const uint8_t more = in.Byte();
  f.count = (kinds & 0x80U) != 0U ? 1U + (more & 3U) : 0U;
  f.drop = (kinds & 1U) != 0U;
  f.no_cr = (kinds & 2U) != 0U;
  f.duplicate = (kinds & 4U) != 0U;
  f.error_reply = (kinds & 8U) != 0U;
  f.lf_after_cr = (kinds & 16U) != 0U;
  f.bad_crc = (kinds & 32U) != 0U;
  f.wrong_address = (kinds & 64U) != 0U;
  f.chunks = static_cast<uint8_t>(1U + (more >> 5U));
  f.extend = (more & 16U) != 0U ? 300U : 0U;
  f.delay_us = (more & 8U) != 0U ? static_cast<uint32_t>(in.Byte()) * 1000U : 0U;
  f.wrong_id = (more & 4U) != 0U ? model::kUpper[in.Byte() % 26U] : '\0';
  f.garble_at = (more & 64U) != 0U ? static_cast<int>(in.Byte() % 40U) : -1;
  if ((more & 128U) != 0U) {
    const auto n = static_cast<uint8_t>(in.Byte() % 24U);
    for (uint8_t i = 0; i < n; ++i) {
      f.replace.push_back(static_cast<char>(in.Byte()));
    }
  }
  return f;
}

/// Checks the recorded writes of one instrument against the invariants.
class WireChecker {
public:
  explicit WireChecker(model::Instrument& inst, int64_t full_scale_milli)
      : inst_(inst), full_scale_(full_scale_milli) {}

  /// When the last byte on the line (a reply, a late reply or chatter) arrives, in microseconds.
  [[nodiscard]] uint64_t LastByteUs() const {
    return inst_.LastByteUs();
  }

  /// The model's clock, in microseconds.
  [[nodiscard]] uint64_t NowUs() const {
    return inst_.now_us;
  }

  /// The number of writes the instrument has seen.
  [[nodiscard]] std::size_t Writes() const {
    return inst_.writes.size();
  }

  /// Declare which guarded commands the harness has asked for since the last check.
  void Allow(const char* token) {
    allowed_.push_back(token);
  }

  /// Check every write since the last call.
  void Check(bool modbus, bool unknown = false) {
    for (; seen_ < inst_.writes.size(); ++seen_) {
      const model::WriteRecord& w = inst_.writes[seen_];
      const std::span<const uint8_t> bytes{reinterpret_cast<const uint8_t*>(w.text.data()),
                                           w.text.size()};
      if (modbus) {
        FUZZ_REQUIRE(alicat_basis2::modbus::IsLegalModbusRequest(bytes));
        checkModbus(w.text);
        if (unknown) {
          checkModbusUnknown(w.text);
        }
      } else {
        FUZZ_REQUIRE(alicat_basis2::ascii::IsLegalRequest(bytes));
        FUZZ_REQUIRE((w.text.size() >= 2U) && (w.text.size() <= alicat_basis2::kMaxRequest));
        checkAscii(w.text.substr(1, w.text.size() - 2U));
        if (unknown) {
          checkAsciiUnknown(w.text.substr(1, w.text.size() - 2U));
        }
      }
      if (seen_ > 0U) {
        const uint64_t prev_end = std::max(inst_.writes[seen_ - 1U].at_us, last_reply_end_);
        const uint64_t t35 = model::Instrument::T35Us(inst_.port_baud);
        FUZZ_REQUIRE(((w.at_us + 1000U) >= prev_end) ||
                     ((w.at_us + 1000U) >= (inst_.writes[seen_ - 1U].at_us + t35)));
      }
      last_reply_end_ = std::max(last_reply_end_, inst_.ReplyEndUs());
    }
    allowed_.clear();
  }

private:
  bool isAllowed(const char* token) const {
    return std::ranges::any_of(allowed_,
                               [token](const char* a) { return std::string(a) == token; });
  }

  void checkAscii(const std::string& body) {
    if (body.starts_with("FACTORY RESTORE")) {
      FUZZ_REQUIRE(isAllowed("restore"));
    } else if (body.starts_with("P 1")) {
      FUZZ_REQUIRE(isAllowed("protocol1"));
    } else if (body == "P2") {
      FUZZ_REQUIRE(isAllowed("protocol2"));
    } else if (body.starts_with("NCB ")) {
      FUZZ_REQUIRE(isAllowed("baud"));
    } else if (body.starts_with("MA ")) {
      FUZZ_REQUIRE(isAllowed("address"));
    } else if (body.starts_with("@=")) {
      FUZZ_REQUIRE(isAllowed("unitid"));
    } else if (body.starts_with("LSS S")) {
      FUZZ_REQUIRE(isAllowed("saved"));
    } else if (body.starts_with("S ") && (full_scale_ > 0)) {
      const double v = std::strtod(body.c_str() + 2, nullptr);
      FUZZ_REQUIRE((v >= 0.0) &&
                   ((v * 1000.0) <= ((static_cast<double>(full_scale_) * 1.025) + 1.0)));
    } else if (body.starts_with("HPUR ")) {
      const double v = std::strtod(body.c_str() + 5, nullptr);
      FUZZ_REQUIRE((v >= 0.0) && (v <= 100.0));
    } else {
      // Every other request is a read or a documented setter.
    }
  }

  /// Whether `body` (a request without its unit ID and CR) is one of the read requests of the
  /// grammar.
  static bool isReadBody(const std::string& body) {
    using alicat_basis2::Command;
    static constexpr std::array<Command, 21> kReads{Command::Poll,
                                                    Command::ReadData,
                                                    Command::ReadSetpointSource,
                                                    Command::ReadRamp,
                                                    Command::ReadWatchdog,
                                                    Command::ReadGains,
                                                    Command::ReadReferenceTemperature,
                                                    Command::ReadAveraging,
                                                    Command::ReadTrigger,
                                                    Command::ReadAverages,
                                                    Command::ReadRanges,
                                                    Command::ReadAutotare,
                                                    Command::ReadGas,
                                                    Command::ReadGasList,
                                                    Command::ReadTotalizerLimit,
                                                    Command::ReadFullScale,
                                                    Command::ReadSerialNumber,
                                                    Command::ReadFirmware,
                                                    Command::ReadModbusAddress,
                                                    Command::ReadBaud,
                                                    Command::ReadProtocol};
    for (const Command c : kReads) {
      for (int64_t arg = 0; arg < 10; ++arg) {
        alicat_basis2::ascii::Args args{};
        args.a = arg;
        const auto made = alicat_basis2::ascii::Make(c, 'A', args, false);
        if (made.ok()) {
          const auto bytes = made.value.Bytes();
          const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
          if (text.substr(1, text.size() - 2U) == body) {
            return true;
          }
        }
      }
    }
    return false;
  }

  /// I14 for ASCII: while the state is unknown only a read, a switch-off (`S 0...`, `HPUR 0...`)
  /// and the way back to Protocol 2 (`P2`) may be sent.
  static void checkAsciiUnknown(const std::string& body) {
    if (body == "P2") {
      return;
    }
    if (body.starts_with("S ")) {
      FUZZ_REQUIRE(std::strtod(body.c_str() + 2, nullptr) == 0.0);
      return;
    }
    if (body.starts_with("HPUR ")) {
      FUZZ_REQUIRE(std::strtod(body.c_str() + 5, nullptr) == 0.0);
      return;
    }
    FUZZ_REQUIRE(isReadBody(body));
  }

  /// I14 for Modbus: while the state is unknown only reads, a zero setpoint (2053) and the way back
  /// to Protocol 2 (register 56 = 0) may be written.
  static void checkModbusUnknown(const std::string& frame) {
    const auto fn = static_cast<uint8_t>(frame[1]);
    if (fn == 3U) {
      return;
    }
    const uint32_t addr = (static_cast<uint32_t>(static_cast<uint8_t>(frame[2])) << 8U) |
                          static_cast<uint8_t>(frame[3]);
    if ((addr == alicat_basis2::reg::kSetpoint) && (fn == 16U) && (frame.size() >= 13U)) {
      for (std::size_t i = 7U; i < 11U; ++i) {
        FUZZ_REQUIRE(model::SameChar(frame[i], '\0'));
      }
      return;
    }
    FUZZ_REQUIRE((addr == alicat_basis2::reg::kProtocol) && (fn == 6U) &&
                 (model::SameChar(frame[4], '\0')) && (model::SameChar(frame[5], '\0')));
  }

  void checkModbus(const std::string& frame) {
    const auto fn = static_cast<uint8_t>(frame[1]);
    FUZZ_REQUIRE((fn == 3U) || (fn == 6U) || (fn == 16U));
    if ((fn == 6U) || (fn == 16U)) {
      const uint32_t addr = (static_cast<uint32_t>(static_cast<uint8_t>(frame[2])) << 8U) |
                            static_cast<uint8_t>(frame[3]);
      FUZZ_REQUIRE(addr != 0U);
      if (addr == 80U) {
        FUZZ_REQUIRE(isAllowed("restore"));
      }
      if (addr == 21U) {
        FUZZ_REQUIRE(isAllowed("baud"));
      }
      if (addr == 45U) {
        FUZZ_REQUIRE(isAllowed("address"));
      }
      if (addr == 46U) {
        FUZZ_REQUIRE(isAllowed("unitid"));
      }
      if (addr == 56U) {
        FUZZ_REQUIRE(isAllowed("protocol1") || isAllowed("protocol2"));
      }
    }
  }

  model::Instrument& inst_;
  int64_t full_scale_;
  std::size_t seen_{0};
  uint64_t last_reply_end_{0};
  std::vector<const char*> allowed_;
};

/// A failed call returns a zero-valued result: checked for the value types that carry numbers.
template <typename R>
inline void CheckFailedIsZero(const R& r) {
  static_cast<void>(r);
}
inline void CheckFailedIsZero(const alicat_basis2::Result<alicat_basis2::Decimal>& r) {
  if (!r.ok()) {
    FUZZ_REQUIRE((r.value.mantissa == 0) && (r.value.decimals == 0U));
  } else {
    FUZZ_REQUIRE((r.value.mantissa > -1'000'000'000'000'000LL) &&
                 (r.value.mantissa < 1'000'000'000'000'000LL));
    FUZZ_REQUIRE(r.value.decimals <= 6U);
  }
}
inline void CheckFailedIsZero(const alicat_basis2::Result<alicat_basis2::Frame>& r) {
  if (!r.ok()) {
    FUZZ_REQUIRE((r.value.present == 0U) && (r.value.flow.mantissa == 0) &&
                 !r.value.status.valve_hold);
  } else {
    FUZZ_REQUIRE((r.value.flow.mantissa > -1'000'000'000'000'000LL) &&
                 (r.value.flow.mantissa < 1'000'000'000'000'000LL));
  }
}

/// One step of a session: the call `op` of the API (both protocols), with arguments taken from the
/// input.
template <typename Drv>
inline void SessionStep(Drv& d, WireChecker& w, Input& in, bool modbus, SessionState& st) {
  using namespace alicat_basis2;
  const bool shared = st.shared;
  const uint64_t start_us = w.NowUs();
  const uint64_t start_hold_us = st.hold_until_us;
  const bool was_unknown = d.IsStateUnknown();
  bool recovered = false;
  const auto op = static_cast<uint8_t>(in.Byte() % 42U);
  const uint8_t a = in.Byte();
  const uint16_t b = in.Word();
  switch (op) {
  case 0:
    CheckFailedIsZero(d.ReadFlow());
    break;
  case 1:
    CheckFailedIsZero(d.ReadFrame());
    break;
  case 2:
    CheckFailedIsZero(d.ReadData(a));
    break;
  case 3:
    CheckFailedIsZero(d.ReadTotal());
    break;
  case 4:
    if ((a & 1U) != 0U) {
      // I16: a switch-off reaches the wire unless the driver cannot send it at all.
      const std::size_t before = w.Writes();
      const Result<void> off = d.SetSetpoint(0.0);
      FUZZ_REQUIRE(
          (off.error == Error::NotInitialized) || (off.error == Error::Unsupported) ||
          (off.error == Error::FirmwareTooOld) ||
          ((off.error == Error::Busy) &&
           (w.LastByteUs() >= (start_us + (OffQuietWaitMs(d.GetConfig(), modbus) * 1000U)))) ||
          (w.Writes() > before));
    } else {
      static_cast<void>(d.SetSetpoint((static_cast<double>(b) / 50.0) - 10.0));
    }
    break;
  case 5:
    static_cast<void>(d.ReadSetpointSource());
    break;
  case 6:
    static_cast<void>(d.SetSetpointSource(static_cast<SetpointSource>(a % 4U)));
    break;
  case 7:
    static_cast<void>(
        d.SetRamp(static_cast<double>(b) / 10.0, static_cast<TimeUnit>(2U + (a % 5U))));
    break;
  case 8:
    static_cast<void>(d.DisableRamp());
    break;
  case 9:
    static_cast<void>(d.SetWatchdog(b));
    break;
  case 10:
    static_cast<void>(d.HoldValve((static_cast<double>(b) / 100.0) - 5.0));
    break;
  case 11:
    static_cast<void>(d.ResumeControl());
    break;
  case 12:
    static_cast<void>(d.SetGains(b, a));
    break;
  case 13:
    static_cast<void>(d.ReadGains());
    break;
  case 14:
    static_cast<void>(d.SetAutotare((a & 1U) != 0U));
    break;
  case 15:
    static_cast<void>(d.SetMeasurementTrigger(a));
    break;
  case 16: {
    // A window that reached the wire runs on the instrument whatever the reply said.
    const std::size_t sent_before = w.Writes();
    static_cast<void>(d.StartMeasurement((static_cast<uint32_t>(b) % 2048U) * 3U)); // up to 6.1 s
    if (w.Writes() > sent_before) {
      // After a lost reply the bus quarantines for twice the window (a late reply may come).
      st.hold_until_us = w.NowUs() + ((static_cast<uint64_t>(b) % 2048U) * 6000U);
    }
    break;
  }
  case 17:
    static_cast<void>(d.ReadAverages());
    break;
  case 18:
    static_cast<void>(d.ReadRanges());
    break;
  case 19:
    static_cast<void>(d.Tare(static_cast<uint32_t>(b % 3000U)));
    break;
  case 20:
    static_cast<void>(d.SetGas(GasId{a}));
    break;
  case 21:
    static_cast<void>(d.ReadGasList());
    break;
  case 22:
    static_cast<void>(d.ResetTotal());
    break;
  case 23:
    static_cast<void>(d.SetTotalizerLimit(static_cast<TotalizerLimit>(a % 6U)));
    break;
  case 24:
    static_cast<void>(d.SetBatch((static_cast<double>(b) / 7.0) - 3.0));
    break;
  case 25:
    static_cast<void>(d.DisableBatch());
    break;
  case 26:
    static_cast<void>(d.SetAveraging(b));
    break;
  case 27:
    static_cast<void>(d.SetReferenceTemperature(static_cast<double>(b) / 1000.0));
    break;
  case 28:
    static_cast<void>(d.ReadFullScale(static_cast<FullScaleItem>(a % 4U)));
    break;
  case 29:
    static_cast<void>(d.ReadSerialNumber());
    break;
  case 30:
    static_cast<void>(d.ReadFirmware());
    break;
  case 31:
    static_cast<void>(d.ReadBaud());
    break;
  case 32:
    static_cast<void>(d.ReadProtocol());
    break;
  case 33:
    if (!shared) {
      w.Allow("unitid"); // I15: refused on a shared line, so never on the wire there
    }
    static_cast<void>(d.ChangeUnitId(std::string_view("@ABCDEFGHIJKLMNOPQRSTUVWXYZ[")[a % 28U],
                                     ConfirmUnitIdChange{}));
    break;
  case 34:
    if (!shared) {
      w.Allow("address");
    }
    static_cast<void>(d.SetModbusAddress(a, ConfirmAddressChange{}));
    break;
  case 35:
    if (!shared) {
      w.Allow("baud"); // I15: one instrument cannot change a shared line's speed
    }
    static_cast<void>(d.ChangeBaud(static_cast<Baud>(a % 8U), ConfirmBaudChange{}));
    break;
  case 36:
    w.Allow("protocol1");
    static_cast<void>(d.SwitchToProtocol1(ConfirmAbandonProtocol2{}));
    break;
  case 37:
    w.Allow("protocol2");
    static_cast<void>(d.SwitchToProtocol2(ConfirmReturnToProtocol2{}));
    break;
  case 38:
    if (!shared) {
      w.Allow("restore");
    }
    static_cast<void>(d.FactoryRestore(ConfirmFactoryRestore{}));
    break;
  case 39:
    recovered = d.Recover().ok();
    break;
  case 40:
    static_cast<void>(d.Begin());
    break;
  default:
    static_cast<void>((modbus ? d.ReadPreviousMeasurement().ok() : d.ScanUnitIds().ok()));
    break;
  }
  if (op == 6) {
    w.Allow("saved");
  }
  if (op == 38U) {
    st.hold_until_us = std::max(st.hold_until_us, w.NowUs() + 3'000'000U); // restore hold-off
  }
  // I9: the call ended within its bound.
  const uint64_t pending_ms =
      (start_hold_us > start_us) ? ((start_hold_us - start_us) / 1000U) : 0U;
  const uint64_t own_ms =
      (op == 16U) ? ((static_cast<uint64_t>(b) % 2048U) * 3U) : 0U; // its window
  FUZZ_REQUIRE((w.NowUs() - start_us) <= ((CallBoundMs(op, modbus, pending_ms) + own_ms) * 1000U));
  w.Check(modbus, was_unknown);
  // I12: the doubt about the instrument's state ends only with a successful `Recover()`.
  FUZZ_REQUIRE(!was_unknown || d.IsStateUnknown() || ((op == 39U) && recovered));
}

} // namespace fuzz

#endif // ALICAT_BASIS2_FUZZ_COMMON_HPP
