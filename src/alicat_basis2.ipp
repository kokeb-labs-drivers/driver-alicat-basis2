/**
 * @file alicat_basis2.ipp
 * @brief ALICAT_BASIS2 driver implementation, included by `alicat_basis2.hpp`.
 * @copyright Copyright (c) 2026 The driver-alicat-basis2 authors. SPDX-License-Identifier: MIT
 *
 * "M p.N" cites the printed page of the operating manual DOC-MANUAL-BASIS2 Rev 4.
 */
#ifndef ALICAT_BASIS2_IPP
#define ALICAT_BASIS2_IPP

namespace alicat_basis2 {

namespace detail {

/// The setpoint source for its protocol letter (`A`, `S`; anything else is `U`).
[[nodiscard]] inline SetpointSource SourceFromLetter(char c) noexcept {
  if (detail::SameChar(c, 'A')) {
    return SetpointSource::Analog;
  }
  if (detail::SameChar(c, 'S')) {
    return SetpointSource::SavedDigital;
  }
  return SetpointSource::UnsavedDigital;
}

/// The protocol letter of a setpoint source.
[[nodiscard]] inline char LetterFromSource(SetpointSource s) noexcept {
  if (s == SetpointSource::Analog) {
    return 'A';
  }
  if (s == SetpointSource::SavedDigital) {
    return 'S';
  }
  return 'U';
}

/// Milliseconds in one ramp time unit (3 is ms, 4 is s, anything else is minutes).
[[nodiscard]] inline double RampUnitMs(uint32_t u) noexcept {
  if (u == 3U) {
    return 1.0;
  }
  if (u == 4U) {
    return 1000.0;
  }
  return 60000.0;
}

/// `256a + 16b + c` of three parts, each at most 15; 0 when `complete` is false or a part is too
/// large.
[[nodiscard]] inline uint16_t PackFirmware(const std::array<uint32_t, 3>& parts,
                                           bool complete) noexcept {
  if (!complete || (parts[0] > 15U) || (parts[1] > 15U) || (parts[2] > 15U)) {
    return 0U;
  }
  return static_cast<uint16_t>((parts[0] * 256U) + (parts[1] * 16U) + parts[2]);
}

/// Whether two learned identities are one instrument: serial number, firmware and kind agree. A
/// `before` that never learned a serial number matches anything.
[[nodiscard]] inline bool SameInstrument(const Identity& before, const Identity& now) noexcept {
  if (before.serial.length == 0U) {
    return true;
  }
  const bool same_serial =
      (before.serial.length == now.serial.length) &&
      std::equal(before.serial.chars.begin(), before.serial.chars.begin() + before.serial.length,
                 now.serial.chars.begin());
  return same_serial && (before.firmware == now.firmware) && (before.kind == now.kind);
}

/// The six baud rates in register 21 order (M p.18).
inline constexpr std::array<uint32_t, 6> kBaudRates{4800U, 9600U, 19200U, 38400U, 57600U, 115200U};

/// A firmware text `3v0v5`, `V3.0.5` or `3.0.5` as `256a + 16b + c`; 0 when it does not parse.
/// A part has one or two digits: a longer run is garbage, not a version (it could wrap).
[[nodiscard]] inline uint16_t ParseFirmware(const Text& text) noexcept {
  std::array<uint32_t, 3> parts{0U, 0U, 0U};
  std::array<uint32_t, 3> digits{0U, 0U, 0U};
  std::size_t idx = 0U;
  bool digit_seen = false;
  for (std::size_t i = 0U; i < text.length; ++i) {
    const char ch = text.chars[i];
    if (IsDigit(ch)) {
      if (digits[idx] >= 2U) {
        return 0U;
      }
      parts[idx] = (parts[idx] * 10U) + static_cast<uint32_t>(DigitValue(ch));
      ++digits[idx];
      digit_seen = true;
    } else if (((detail::SameChar(ch, 'v')) || (detail::SameChar(ch, 'V')) ||
                (detail::SameChar(ch, '.'))) &&
               digit_seen && (idx < 2U)) {
      ++idx;
      digit_seen = false;
    } else if (((detail::SameChar(ch, 'v')) || (detail::SameChar(ch, 'V'))) && (i == 0U)) {
      // A leading V: `V3.0.5`.
    } else {
      return 0U;
    }
  }
  return PackFirmware(parts, (idx == 2U) && digit_seen);
}

/// The error of a step that follows a transaction which reached the wire (a verify, a read-back,
/// a second write, a retry): `Busy` there means only that this step was not sent, so the call
/// reports `Timeout`, as for a lost reply. A writing call thus returns `Busy` only when its write
/// was not sent; a call made only of reads may have sent earlier reads.
[[nodiscard]] constexpr Error AfterSent(Error e) noexcept {
  return (e == Error::Busy) ? Error::Timeout : e;
}

/// Whether an error is worth repeating for a read.
[[nodiscard]] constexpr bool Retryable(Error e) noexcept {
  return (e == Error::Timeout) || (e == Error::Overflow) || (e == Error::DeviceIdMismatch) ||
         (e == Error::BadReply) || (e == Error::Crc);
}

/// Round a non-negative double to an integer; false when it does not fit below `cap`.
[[nodiscard]] inline bool ScaleNonNegative(double value, double scale, int64_t cap,
                                           int64_t& out) noexcept {
  if (!(value >= 0.0) || !(value <= (static_cast<double>(cap) / scale))) {
    return false;
  }
  const double scaled = value * scale;
  out = static_cast<int64_t>(scaled);
  if ((scaled - static_cast<double>(out)) >= 0.5) {
    ++out;
  }
  return out <= cap;
}

/// A Modbus text of `regs` registers (two characters each) up to the first NUL; other
/// non-printable bytes before it are skipped.
[[nodiscard]] inline Text RegistersToText(const modbus::Response& r, std::size_t first,
                                          std::size_t regs) noexcept {
  Text t{};
  bool ended = false;
  for (std::size_t i = 0U; i < regs; ++i) {
    for (uint32_t half = 0U; half < 2U; ++half) {
      const uint32_t shift = (half == 0U) ? 8U : 0U;
      const auto byte =
          static_cast<uint8_t>((static_cast<uint32_t>(r.words[first + i]) >> shift) & 0xFFU);
      ended = ended || (byte == 0U);
      if (!ended && IsPrintableByte(byte) && (t.length < kTextMax)) {
        t.chars[t.length] = CharOf(byte);
        ++t.length;
      }
    }
  }
  return t;
}

/// 32-bit value from two registers, high register first.
[[nodiscard]] constexpr uint32_t Combine(uint16_t high, uint16_t low) noexcept {
  return (static_cast<uint32_t>(high) << 16U) | static_cast<uint32_t>(low);
}

/// A signed 16-bit register value.
[[nodiscard]] constexpr int64_t Signed16(uint16_t v) noexcept {
  return (v >= 0x8000U) ? (static_cast<int64_t>(v) - 65536) : static_cast<int64_t>(v);
}

/// A signed 32-bit combined value.
[[nodiscard]] constexpr int64_t Signed32(uint32_t v) noexcept {
  return (v >= 0x8000'0000U) ? (static_cast<int64_t>(v) - 4294967296LL) : static_cast<int64_t>(v);
}

/// Controller-only items of a `DV` mask.
inline constexpr uint8_t kControllerItems = 2U | 8U | 64U;

} // namespace detail

// -------------------------------------------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------------------------------------------

template <UartPort Port>
Timing Basis2<Port>::timing() const noexcept {
  Timing t{};
  t.baud = config_.baud;
  t.processing_ms = config_.processing_ms;
  t.slow_ms = config_.slow_command_ms;
  t.echo = config_.echo;
  return t;
}

template <UartPort Port>
int64_t Basis2<Port>::fullScaleMilli() const noexcept {
  if (config_.full_scale_override_milli != 0U) {
    return static_cast<int64_t>(config_.full_scale_override_milli);
  }
  return identity_.full_scale_known ? identity_.full_scale.ToMilli() : 0;
}

template <UartPort Port>
ascii::FrameRules Basis2<Port>::rules() const noexcept {
  ascii::FrameRules r{};
  r.unit_id = config_.unit_id;
  r.layout = layout_known_ ? layout_ : config_.frame_layout;
  r.flags = config_.unknown_flags;
  r.full_scale_milli = fullScaleMilli();
  r.full_scale_known = r.full_scale_milli > 0;
  r.flow_margin_permille = config_.flow_margin_permille;
  r.temperature_min_c = config_.temperature_min_c;
  r.temperature_max_c = config_.temperature_max_c;
  return r;
}

template <UartPort Port>
Error Basis2<Port>::ready(bool needs_identity, bool switch_off) const noexcept {
  if (!initialized_) {
    return Error::NotInitialized;
  }
  if (abandoned_) {
    return Error::Unsupported;
  }
  if (switch_off) {
    // A switch-off (setpoint 0, valve closed) is never refused for doubt about the instrument's
    // state: it is sent with the settings the driver holds and its own result is returned.
    return Error::Ok;
  }
  if (state_unknown_) {
    return Error::StateUnknown;
  }
  return (needs_identity && !ready_) ? Error::NotInitialized : Error::Ok;
}

template <UartPort Port>
Error Basis2<Port>::needController() const noexcept {
  const DeviceKind kind = (config_.kind != DeviceKind::Auto) ? config_.kind : identity_.kind;
  return (kind == DeviceKind::Meter) ? Error::Unsupported : Error::Ok;
}

template <UartPort Port>
Error Basis2<Port>::needAscii() const noexcept {
  return (config_.protocol == Protocol::Ascii) ? Error::Ok : Error::Unsupported;
}

template <UartPort Port>
Error Basis2<Port>::needModbus() const noexcept {
  return (config_.protocol == Protocol::ModbusRtu) ? Error::Ok : Error::Unsupported;
}

template <UartPort Port>
Error Basis2<Port>::gate(uint16_t min_fw) const noexcept {
  if ((config_.firmware_gate == FirmwareGate::Ignore) || (min_fw == 0U)) {
    return Error::Ok;
  }
  const uint16_t fw = identity_.firmware_known ? identity_.firmware : config_.assume_firmware;
  return ((fw != 0U) && (fw < min_fw)) ? Error::FirmwareTooOld : Error::Ok;
}

template <UartPort Port>
void Basis2<Port>::invalidate() noexcept {
  identity_.source_known = false;
}

template <UartPort Port>
Result<Reply> Basis2<Port>::runOnce(const Request& request) noexcept {
  const Result<Reply> r = bus_.Execute(request, timing());
  if (r.ok()) {
    last_reply_ = r.value;
  }
  return r;
}

template <UartPort Port>
Result<modbus::Response> Basis2<Port>::ReadRegisterRaw(uint16_t first, uint16_t count) noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<modbus::Response>::Failure(e);
  }
  if (const Error e = needModbus(); e != Error::Ok) {
    return Result<modbus::Response>::Failure(e);
  }
  const Result<Request> built = modbus::BuildRead(config_.modbus_address, first, count);
  if (!built.ok()) {
    return Result<modbus::Response>::Failure(built.error);
  }
  return mbReadOnce(built.value, count);
}

template <UartPort Port>
Result<Reply> Basis2<Port>::ReadRawLine(Command command) noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<Reply>::Failure(e);
  }
  if (const Error e = needAscii(); e != Error::Ok) {
    return Result<Reply>::Failure(e);
  }
  if (!ascii::detail::IsRead(command) || (ascii::Spec(command).arg != ascii::ArgKind::None)) {
    return Result<Reply>::Failure(Error::InvalidArgument);
  }
  const Result<Request> made = ascii::Make(command, config_.unit_id, ascii::Args{}, false);
  if (!made.ok()) {
    return Result<Reply>::Failure(made.error);
  }
  return runOnce(made.value);
}

template <UartPort Port>
Result<Reply> Basis2<Port>::run(const Request& request) noexcept {
  Result<Reply> r = runOnce(request);
  for (uint8_t i = 0U; (i < config_.read_retries) && !r.ok() && request.IsIdempotent() &&
                       detail::Retryable(r.error);
       ++i) {
    r = runOnce(request);
    r.error = detail::AfterSent(r.error); // the first try was sent: a retry's Busy is a Timeout
  }
  return r;
}

template <UartPort Port>
Result<Reply> Basis2<Port>::asciiRun(Command command, const ascii::Args& args) noexcept {
  const ascii::CommandSpec& spec = ascii::Spec(command);
  if (const Error e = gate(spec.min_firmware); e != Error::Ok) {
    return Result<Reply>::Failure(e);
  }
  if (spec.controller_only) {
    if (const Error e = needController(); e != Error::Ok) {
      return Result<Reply>::Failure(e);
    }
  }
  Result<Request> made = ascii::Make(command, config_.unit_id, args, false);
  if (!made.ok()) {
    return Result<Reply>::Failure(made.error);
  }
  if (command == Command::Tare) {
    made.value.SetTiming(static_cast<uint32_t>(args.a),
                         static_cast<uint32_t>(args.a) + config_.processing_ms);
  } else if (command == Command::StartMeasurement) {
    made.value.SetTiming(static_cast<uint32_t>(args.a), 0U);
  } else if (command == Command::SetSetpoint) {
    made.value.SetTiming(setpointExtraMs(), 0U);
  } else {
    // No duration to add.
  }
  return run(made.value);
}

template <UartPort Port>
Result<void> Basis2<Port>::asciiSet(Command command, const ascii::Args& args) noexcept {
  const Result<Reply> r = asciiRun(command, args);
  return r.ok() ? Result<void>::Success() : Result<void>::Failure(r.error);
}

template <UartPort Port>
Result<ascii::Values> Basis2<Port>::asciiValues(Command command, uint8_t count) noexcept {
  const Result<Reply> r = asciiRun(command, ascii::Args{});
  if (!r.ok()) {
    return Result<ascii::Values>::Failure(r.error);
  }
  ascii::Values v{};
  const Error e = ascii::DecodeValues(std::span<const char>{r.value.data.data(), r.value.length},
                                      config_.unit_id, count, v);
  if (e != Error::Ok) {
    bus_.EnterQuarantine();
    return Result<ascii::Values>::Failure(e);
  }
  return Result<ascii::Values>::Success(v);
}

template <UartPort Port>
Result<modbus::Response> Basis2<Port>::mbReadOnce(const Request& request, uint16_t count) noexcept {
  const Result<Reply> r = runOnce(request);
  if (!r.ok()) {
    return Result<modbus::Response>::Failure(r.error);
  }
  modbus::Response resp{};
  const Error e =
      modbus::ParseResponse(std::span<const char>{r.value.data.data(), r.value.length},
                            config_.modbus_address, modbus::kReadHolding, request.Bytes(), resp);
  if (e == Error::Rejected) {
    last_exception_ = resp.exception;
  } else if (e != Error::Ok) {
    bus_.EnterQuarantine();
  } else if (resp.count != count) {
    bus_.EnterQuarantine();
    return Result<modbus::Response>::Failure(Error::BadReply);
  } else {
    // A complete, matching response.
  }
  return (e == Error::Ok) ? Result<modbus::Response>::Success(resp)
                          : Result<modbus::Response>::Failure(e);
}

template <UartPort Port>
Result<modbus::Response> Basis2<Port>::mbRead(uint16_t first, uint16_t count) noexcept {
  // A read is as old as its newest register: an older instrument answers a read that spans one
  // it lacks with a Bad Address exception, so the whole read is refused before it is sent.
  if (const Error e = gate(modbus::detail::MinFirmwareOver(first, count)); e != Error::Ok) {
    return Result<modbus::Response>::Failure(e);
  }
  const Result<Request> built = modbus::BuildRead(config_.modbus_address, first, count);
  if (!built.ok()) {
    return Result<modbus::Response>::Failure(built.error);
  }
  Result<modbus::Response> r = mbReadOnce(built.value, count);
  for (uint8_t i = 0U; (i < config_.read_retries) && !r.ok() && detail::Retryable(r.error); ++i) {
    r = mbReadOnce(built.value, count);
    r.error = detail::AfterSent(r.error); // the first try was sent: a retry's Busy is a Timeout
  }
  return r;
}

template <UartPort Port>
Result<void> Basis2<Port>::mbWriteBuilt(const Result<Request>& built, uint32_t extra_ms,
                                        uint32_t hold_ms) noexcept {
  if (!built.ok()) {
    return Result<void>::Failure(built.error);
  }
  Request req = built.value;
  if ((extra_ms > 0U) || (hold_ms > 0U)) {
    req.SetTiming(extra_ms, hold_ms);
  }
  const Result<Reply> r = run(req);
  if (!r.ok()) {
    return Result<void>::Failure(r.error);
  }
  modbus::Response resp{};
  const Error e = modbus::ParseResponse(std::span<const char>{r.value.data.data(), r.value.length},
                                        config_.modbus_address, req.Function(), req.Bytes(), resp);
  if (e == Error::Rejected) {
    last_exception_ = resp.exception;
  } else if (e != Error::Ok) {
    bus_.EnterQuarantine();
  } else {
    // The echo matched.
  }
  return (e == Error::Ok) ? Result<void>::Success() : Result<void>::Failure(e);
}

template <UartPort Port>
Result<void> Basis2<Port>::mbWrite1(uint16_t target, uint16_t value, uint32_t extra_ms,
                                    uint32_t hold_ms) noexcept {
  const reg::RegisterSpec* row = modbus::detail::RowContaining(target);
  if ((row != nullptr) && (gate(row->min_fw) != Error::Ok)) {
    return Result<void>::Failure(Error::FirmwareTooOld);
  }
  return mbWriteBuilt(modbus::BuildWriteSingle(config_.modbus_address, target, value), extra_ms,
                      hold_ms);
}

template <UartPort Port>
Result<void> Basis2<Port>::mbWrite2(uint16_t target, uint32_t value, uint32_t extra_ms) noexcept {
  const reg::RegisterSpec* row = modbus::detail::RowContaining(target);
  if ((row != nullptr) && (gate(row->min_fw) != Error::Ok)) {
    return Result<void>::Failure(Error::FirmwareTooOld);
  }
  return mbWriteBuilt(modbus::BuildWriteMultiple(config_.modbus_address, target, value), extra_ms,
                      0U);
}

template <UartPort Port>
Result<uint32_t> Basis2<Port>::readInt(Command command, uint16_t target, uint32_t hi) noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<uint32_t>::Failure(e);
  }
  if (config_.protocol == Protocol::ModbusRtu) {
    const auto r = mbRead(target, 1U);
    if (!r.ok()) {
      return Result<uint32_t>::Failure(r.error);
    }
    return (r.value.words[0] <= hi) ? Result<uint32_t>::Success(r.value.words[0])
                                    : Result<uint32_t>::Failure(Error::OutOfRange);
  }
  const auto v = asciiValues(command, 1U);
  if (!v.ok()) {
    return Result<uint32_t>::Failure(v.error);
  }
  const Decimal d = v.value.v[0];
  if ((d.decimals != 0U) || (d.mantissa < 0) || std::cmp_greater(d.mantissa, hi)) {
    bus_.EnterQuarantine();
    return Result<uint32_t>::Failure(Error::BadReply);
  }
  return Result<uint32_t>::Success(static_cast<uint32_t>(d.mantissa));
}

template <UartPort Port>
Result<void> Basis2<Port>::setInt(Command command, uint16_t target, int64_t value) noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if ((value < 0) || (value > 65535)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (config_.protocol == Protocol::ModbusRtu) {
    return mbWrite1(target, static_cast<uint16_t>(value));
  }
  ascii::Args a{};
  a.a = value;
  return asciiSet(command, a);
}

// -------------------------------------------------------------------------------------------------------------
// Lifecycle
// -------------------------------------------------------------------------------------------------------------

template <UartPort Port>
Result<void> Basis2<Port>::EnsureInitialized() noexcept {
  if (initialized_) {
    return Result<void>::Success();
  }
  if (Validate(config_) != Error::Ok) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (!bus_.GetPort().EnsureInitialized()) {
    return Result<void>::Failure(Error::Bus);
  }
  initialized_ = true;
  return Result<void>::Success();
}

template <UartPort Port>
Result<void> Basis2<Port>::learnFullScale() noexcept {
  const auto fs = ReadFullScale(FullScaleItem::Flow);
  if (!fs.ok()) {
    return Result<void>::Failure(fs.error);
  }
  identity_.full_scale = fs.value.value;
  identity_.full_scale_known = true;
  identity_.unit_label = fs.value.unit;
  identity_.flow_unit = ascii::FlowUnitFromLabel(
      std::span<const char>{fs.value.unit.chars.data(), fs.value.unit.length});
  return Result<void>::Success();
}

template <UartPort Port>
Result<void> Basis2<Port>::learnMore() noexcept {
  if (const auto g = ReadGas(); g.ok()) {
    identity_.gas = g.value.gas;
    identity_.gas_known = true;
  } else if (g.error != Error::Rejected) {
    return Result<void>::Failure(g.error);
  } else {
    // A rejected gas read leaves the gas unknown.
  }
  if (const auto t = ReadTotalizerLimit(); !t.ok() && (t.error != Error::Rejected)) {
    return Result<void>::Failure(t.error);
  }
  if (const auto a = ReadAveraging(); !a.ok() && (a.error != Error::Rejected)) {
    return Result<void>::Failure(a.error);
  }
  if (const auto r = ReadReferenceTemperature(); !r.ok() && (r.error != Error::Rejected)) {
    return Result<void>::Failure(r.error);
  }
  return Result<void>::Success();
}

template <UartPort Port>
Result<void> Basis2<Port>::learnAscii() noexcept {
  const auto frame = ReadFrame();
  if (!frame.ok()) {
    return Result<void>::Failure(frame.error);
  }
  identity_.kind = ((frame.value.present & static_cast<uint8_t>(DataItem::Setpoint)) != 0U)
                       ? DeviceKind::Controller
                       : DeviceKind::Meter;
  const auto fw = ReadFirmware();
  if (!fw.ok()) {
    return Result<void>::Failure(fw.error);
  }
  identity_.firmware_text = fw.value;
  identity_.firmware = detail::ParseFirmware(fw.value);
  identity_.firmware_known = identity_.firmware != 0U;
  if (const auto sn = ReadSerialNumber(); sn.ok()) {
    identity_.serial = sn.value;
  } else {
    return Result<void>::Failure(sn.error);
  }
  if (const auto e = learnFullScale(); !e.ok()) {
    return e;
  }
  if (const auto e = learnMore(); !e.ok()) {
    return e;
  }
  if (identity_.kind == DeviceKind::Controller) {
    if (const auto s = ReadSetpointSource(); s.ok()) {
      identity_.source = s.value;
      identity_.source_known = true;
    } else if (s.error != Error::Rejected) {
      return Result<void>::Failure(s.error);
    } else {
      // Rejected: source unknown.
    }
    if (const auto r = ReadRamp(); r.ok()) {
      identity_.ramp_active = r.value.rate.mantissa != 0;
    } else if (r.error != Error::Rejected) {
      return Result<void>::Failure(r.error);
    } else {
      // Rejected: ramp unknown, treated as inactive.
    }
  }
  return Result<void>::Success();
}

template <UartPort Port>
Result<void> Basis2<Port>::learnModbus() noexcept {
  identity_.kind = (config_.kind == DeviceKind::Auto) ? DeviceKind::Controller : config_.kind;
  const auto fw = mbRead(reg::kFirmware, 1U);
  if (!fw.ok()) {
    return Result<void>::Failure(fw.error);
  }
  identity_.firmware = fw.value.words[0];
  identity_.firmware_known = identity_.firmware != 0U;
  if (const auto sn = ReadSerialNumber(); sn.ok()) {
    identity_.serial = sn.value;
  } else {
    return Result<void>::Failure(sn.error);
  }
  if (const auto fs = ReadFullScale(FullScaleItem::Flow); fs.ok()) {
    identity_.full_scale = fs.value.value;
    identity_.full_scale_known = true;
    identity_.unit_label = fs.value.unit;
    identity_.flow_unit = ascii::FlowUnitFromLabel(
        std::span<const char>{fs.value.unit.chars.data(), fs.value.unit.length});
  } else if (fs.error != Error::FirmwareTooOld) {
    return Result<void>::Failure(fs.error);
  } else {
    // Firmware before 2.4.0 has no register 47: the full scale stays unknown unless overridden.
  }
  if (const auto unit = ReadFlowUnit(); unit.ok()) {
    identity_.flow_unit = unit.value;
  } else if ((unit.error != Error::FirmwareTooOld) && (unit.error != Error::Rejected)) {
    return Result<void>::Failure(unit.error);
  } else {
    // Register 49 (more units) appeared with firmware 3.0.0: an older instrument may answer a
    // read of it with a Bad Address exception, and the unit then comes from register 47's label.
  }
  const auto frame = ReadFrame();
  if (frame.ok()) {
    identity_.gas = frame.value.gas;
    identity_.gas_known = frame.value.gas_known;
  } else if (frame.error != Error::FirmwareTooOld) {
    return Result<void>::Failure(frame.error);
  } else {
    // The data block 2100 to 2109 appeared with firmware 3.0.0: older firmware offers no data
    // frame.
  }
  return (identity_.kind == DeviceKind::Controller) ? learnControlModbus()
                                                    : Result<void>::Success();
}

template <UartPort Port>
Result<void> Basis2<Port>::learnControlModbus() noexcept {
  // As over ASCII: the source decides a setpoint's reply wait and rate limit, the ramp its verify
  // (registers 516 and 524-525, M p.21 and p.27); each read records what it finds. An older or a
  // refusing instrument leaves them unknown (source treated as saved, ramp as inactive).
  const auto src = ReadSetpointSource();
  if (!src.ok() && (src.error != Error::FirmwareTooOld) && (src.error != Error::Rejected)) {
    return Result<void>::Failure(src.error);
  }
  const auto ramp = ReadRamp();
  if (!ramp.ok() && (ramp.error != Error::FirmwareTooOld) && (ramp.error != Error::Rejected)) {
    return Result<void>::Failure(ramp.error);
  }
  return Result<void>::Success();
}

template <UartPort Port>
Result<void> Basis2<Port>::Begin() noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if ((config_.protocol == Protocol::ModbusRtu) &&
      (config_.modbus_flow_decimals == kDecimalsUnset)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  ready_ = false;
  identity_ = Identity{};
  layout_known_ = false;
  const Result<void> r = (config_.protocol == Protocol::Ascii) ? learnAscii() : learnModbus();
  ready_ = r.ok();
  return r;
}

template <UartPort Port>
bool Basis2<Port>::scanAddresses(bool alone) noexcept {
  // A poll that meets a line that never goes quiet stops the scan: nothing can be learned there.
  bool found = false;
  line_busy_ = false;
  if (config_.protocol == Protocol::Ascii) {
    const char original = config_.unit_id;
    const uint32_t last = alone ? 26U : 0U;
    for (uint32_t k = 0U; (k <= last) && !found && !line_busy_; ++k) {
      config_.unit_id = (k == 0U) ? original : detail::UpperChar(k - 1U);
      layout_known_ = false;
      const Error e = ReadFrame().error;
      found = e == Error::Ok;
      line_busy_ = e == Error::Busy;
    }
    if (!found) {
      config_.unit_id = original;
    }
  } else {
    const uint8_t original = config_.modbus_address;
    const uint32_t last = alone ? 247U : 0U;
    for (uint32_t a = 0U; (a <= last) && !found && !line_busy_; ++a) {
      config_.modbus_address = (a == 0U) ? original : static_cast<uint8_t>(a);
      const Error e = mbRead(reg::kFirmware, 1U).error;
      found = e == Error::Ok;
      line_busy_ = e == Error::Busy;
    }
    if (!found) {
      config_.modbus_address = original;
    }
  }
  return found;
}

template <UartPort Port>
bool Basis2<Port>::scanAtSpeed(uint32_t speed, bool alone) noexcept {
  const uint32_t held = config_.baud;
  if ((speed == 0U) || (speed == held) || !Bus<Port>::CanSetBaud() || !bus_.ApplyBaud(speed).ok()) {
    return false;
  }
  config_.baud = speed;
  if (scanAddresses(alone)) {
    return true;
  }
  config_.baud = held;
  static_cast<void>(bus_.ApplyBaud(held));
  return false;
}

template <UartPort Port>
bool Basis2<Port>::scanForUnit() noexcept {
  // On a bus with several instruments only the configured address is tried: a scan would bind to
  // a neighbour and later writes would reach the wrong device.
  const bool alone = bus_.DeviceCount() <= 1U;
  // After an unconfirmed baud change or factory restore the instrument may run at another speed;
  // on a shared line the neighbours keep the held one, so no other speed is tried there.
  return scanAddresses(alone) ||
         (alone && !line_busy_ &&
          (scanAtSpeed(alt_.baud, alone) ||
           (!line_busy_ && scanAtSpeed(alt_.factory ? 38400U : 0U, alone))));
}

template <UartPort Port>
void Basis2<Port>::afterFailedRecover(const Held& held, bool found, Error error) noexcept {
  // A busy line in the scan or in the Begin() after it confirmed nothing: the state stays as it
  // was. If the scan had found the instrument at other settings (another ID, address or speed)
  // before Begin() met the busy line, that evidence is kept: those settings stay, in doubt.
  const bool moved =
      found && ((!detail::SameChar(config_.unit_id, held.unit_id)) ||
                (config_.modbus_address != held.address) || (config_.baud != held.baud));
  const bool learnt_nothing = (error == Error::Busy) && !moved;
  state_unknown_ = learnt_nothing ? held.unknown : true;
  abandoned_ = held.abandoned;
  ready_ = learnt_nothing && held.ready;
  layout_known_ = learnt_nothing && held.layout_known;
  identity_ = held.identity;
  if (!((error == Error::Busy) && moved)) {
    config_.unit_id = held.unit_id;
    config_.modbus_address = held.address;
    if (config_.baud != held.baud) {
      config_.baud = held.baud;
      static_cast<void>(bus_.ApplyBaud(held.baud));
    }
  }
}

template <UartPort Port>
Result<void> Basis2<Port>::Recover() noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  // The doubt about the instrument's state ends only with a validated reply from the same
  // instrument (serial number, firmware and kind unchanged); until then it stays.
  const Held held{.abandoned = abandoned_,
                  .unknown = state_unknown_,
                  .ready = ready_,
                  .layout_known = layout_known_,
                  .identity = identity_,
                  .unit_id = config_.unit_id,
                  .address = config_.modbus_address,
                  .baud = config_.baud};
  state_unknown_ = false;
  abandoned_ = false;
  ready_ = false;
  const bool found = scanForUnit();
  Result<void> r =
      found ? Begin() : Result<void>::Failure(line_busy_ ? Error::Busy : Error::Timeout);
  if (r.ok() && !detail::SameInstrument(held.identity, identity_)) {
    r = Result<void>::Failure(Error::VerifyMismatch);
  }
  if (r.ok()) {
    alt_ = detail::Alternatives{};
  } else {
    afterFailedRecover(held, found, r.error);
  }
  return r;
}

// -------------------------------------------------------------------------------------------------------------
// Data
// -------------------------------------------------------------------------------------------------------------

template <UartPort Port>
Result<Frame> Basis2<Port>::asciiFrame(Command command, uint8_t items) noexcept {
  ascii::Args a{};
  a.a = items;
  const Result<Reply> r = asciiRun(command, a);
  if (!r.ok()) {
    return Result<Frame>::Failure(r.error);
  }
  const std::span<const char> line{r.value.data.data(), r.value.length};
  Frame f{};
  FrameLayout seen = FrameLayout::Auto;
  const Error e = (command == Command::Poll) ? ascii::DecodeFrame(line, rules(), f, seen)
                                             : ascii::DecodeData(line, items, rules(), f);
  if (e != Error::Ok) {
    bus_.EnterQuarantine();
    return Result<Frame>::Failure(e);
  }
  if ((command == Command::Poll) && !layout_known_ && (config_.frame_layout == FrameLayout::Auto)) {
    layout_known_ = true;
    layout_ = seen;
  }
  return Result<Frame>::Success(f);
}

template <UartPort Port>
Result<Frame> Basis2<Port>::modbusFrame(uint8_t items) noexcept {
  if (config_.modbus_flow_decimals == kDecimalsUnset) {
    return Result<Frame>::Failure(Error::InvalidArgument);
  }
  const auto r = mbRead(reg::kGas, 10U);
  if (!r.ok()) {
    return Result<Frame>::Failure(r.error);
  }
  const auto& w = r.value.words;
  const uint8_t d = config_.modbus_flow_decimals;
  Frame f{};
  f.unit_id = config_.unit_id;
  f.present = items;
  f.gas = GasId{static_cast<uint8_t>(w[0] & 0xFFU)};
  f.gas_known = (w[0] <= 8U);
  if (f.gas_known) {
    // Copied up to its NUL, and never past the field's last place (which keeps the NUL).
    const char* const name = GasName(f.gas.number);
    bool ended = false;
    for (std::size_t i = 0U; (i + 1U) < f.gas_name.size(); ++i) {
      ended = ended || detail::SameChar(name[i], '\0');
      f.gas_name[i] = ended ? '\0' : name[i];
    }
  }
  f.status = StatusFromBits(w[1]);
  f.temperature = Decimal{.mantissa = detail::Signed16(w[2]), .decimals = 2U};
  f.flow = Decimal{.mantissa = detail::Signed16(w[3]), .decimals = d};
  f.total = Decimal{.mantissa = static_cast<int64_t>(detail::Combine(w[4], w[5])), .decimals = d};
  f.setpoint = Decimal{.mantissa = detail::Signed16(w[6]), .decimals = d};
  f.valve_drive = Decimal{.mantissa = static_cast<int64_t>(w[7]), .decimals = 2U};
  f.batch_remaining =
      Decimal{.mantissa = static_cast<int64_t>(detail::Combine(w[8], w[9])), .decimals = d};
  if (const Error e = ascii::detail::CheckFrame(f, rules()); e != Error::Ok) {
    return Result<Frame>::Failure(e);
  }
  return Result<Frame>::Success(f);
}

template <UartPort Port>
Result<Frame> Basis2<Port>::ReadFrame() noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<Frame>::Failure(e);
  }
  if (config_.protocol == Protocol::Ascii) {
    return asciiFrame(Command::Poll, 0U);
  }
  const bool controller = needController() == Error::Ok;
  uint8_t items = DataItem::Flow | DataItem::Temperature | DataItem::Gas | DataItem::TotalVolume |
                  DataItem::Status;
  if (controller) {
    items = items | DataItem::Setpoint | DataItem::ValveDrive | DataItem::BatchRemaining;
  }
  return modbusFrame(items);
}

template <UartPort Port>
Result<Frame> Basis2<Port>::ReadData(uint8_t items) noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<Frame>::Failure(e);
  }
  if (!detail::MaskValid(items)) {
    return Result<Frame>::Failure(Error::InvalidArgument);
  }
  if (((static_cast<uint32_t>(items) & detail::kControllerItems) != 0U) &&
      (needController() != Error::Ok)) {
    return Result<Frame>::Failure(Error::Unsupported);
  }
  return (config_.protocol == Protocol::Ascii) ? asciiFrame(Command::ReadData, items)
                                               : modbusFrame(items);
}

template <UartPort Port>
Result<Decimal> Basis2<Port>::ReadFlow() noexcept {
  const auto f = ReadData(static_cast<uint8_t>(DataItem::Flow));
  return f.ok() ? Result<Decimal>::Success(f.value.flow) : Result<Decimal>::Failure(f.error);
}

template <UartPort Port>
Result<Decimal> Basis2<Port>::ReadTemperature() noexcept {
  const auto f = ReadData(static_cast<uint8_t>(DataItem::Temperature));
  return f.ok() ? Result<Decimal>::Success(f.value.temperature) : Result<Decimal>::Failure(f.error);
}

template <UartPort Port>
Result<Decimal> Basis2<Port>::ReadTotal() noexcept {
  const auto f = ReadData(static_cast<uint8_t>(DataItem::TotalVolume));
  return f.ok() ? Result<Decimal>::Success(f.value.total) : Result<Decimal>::Failure(f.error);
}

template <UartPort Port>
Result<Decimal> Basis2<Port>::ReadValveDrive() noexcept {
  const auto f = ReadData(static_cast<uint8_t>(DataItem::ValveDrive));
  return f.ok() ? Result<Decimal>::Success(f.value.valve_drive) : Result<Decimal>::Failure(f.error);
}

template <UartPort Port>
Result<Decimal> Basis2<Port>::ReadSetpointValue() noexcept {
  const auto f = ReadData(static_cast<uint8_t>(DataItem::Setpoint));
  return f.ok() ? Result<Decimal>::Success(f.value.setpoint) : Result<Decimal>::Failure(f.error);
}

template <UartPort Port>
Result<Decimal> Basis2<Port>::ReadBatchRemaining() noexcept {
  const auto f = ReadData(static_cast<uint8_t>(DataItem::BatchRemaining));
  return f.ok() ? Result<Decimal>::Success(f.value.batch_remaining)
                : Result<Decimal>::Failure(f.error);
}

template <UartPort Port>
Result<StatusFlags> Basis2<Port>::ReadStatus() noexcept {
  const auto f = ReadData(static_cast<uint8_t>(DataItem::Status));
  return f.ok() ? Result<StatusFlags>::Success(f.value.status)
                : Result<StatusFlags>::Failure(f.error);
}

template <UartPort Port>
Result<Frame> Basis2<Port>::ReadGas() noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<Frame>::Failure(e);
  }
  if (config_.protocol == Protocol::ModbusRtu) {
    const auto r = mbRead(reg::kGas, 1U);
    if (!r.ok()) {
      return Result<Frame>::Failure(r.error);
    }
    Frame f{};
    f.present = static_cast<uint8_t>(DataItem::Gas);
    f.gas = GasId{static_cast<uint8_t>(r.value.words[0] & 0xFFU)};
    f.gas_known = r.value.words[0] <= 8U;
    return Result<Frame>::Success(f);
  }
  const Result<Reply> r = asciiRun(Command::ReadGas, ascii::Args{});
  if (!r.ok()) {
    return Result<Frame>::Failure(r.error);
  }
  Frame f{};
  const Error e = ascii::DecodeGas(std::span<const char>{r.value.data.data(), r.value.length},
                                   config_.unit_id, f);
  if (e != Error::Ok) {
    bus_.EnterQuarantine();
    return Result<Frame>::Failure(e);
  }
  return Result<Frame>::Success(f);
}

// -------------------------------------------------------------------------------------------------------------
// Setpoint and valve
// -------------------------------------------------------------------------------------------------------------

template <UartPort Port>
Result<void> Basis2<Port>::verifySetpoint(int64_t milli) noexcept {
  if ((config_.verify_setpoint == VerifyPolicy::Off) ||
      ((config_.verify_setpoint == VerifyPolicy::WhenNoRamp) && identity_.ramp_active)) {
    return Result<void>::Success();
  }
  int64_t read_back = 0;
  if (config_.protocol == Protocol::ModbusRtu) {
    // Registers 2053 and 2054 read as the current setpoint (M p.21); they exist from firmware 2.1.0
    // like the write.
    const auto sp = mbRead(reg::kSetpoint, 2U);
    if (!sp.ok()) {
      return Result<void>::Failure(sp.error);
    }
    read_back = detail::Signed32(detail::Combine(sp.value.words[0], sp.value.words[1]));
  } else {
    const auto sp = ReadSetpointValue();
    if (!sp.ok()) {
      return Result<void>::Failure(sp.error);
    }
    read_back = sp.value.ToMilli();
  }
  int64_t tol = (fullScaleMilli() * config_.verify_tolerance_permille) / 1000;
  tol = (tol < 1) ? 1 : tol;
  const int64_t diff = read_back - milli;
  return (((diff < 0) ? -diff : diff) <= tol) ? Result<void>::Success()
                                              : Result<void>::Failure(Error::VerifyMismatch);
}

template <UartPort Port>
Error Basis2<Port>::setpointAllowed(int64_t milli) const noexcept {
  const int64_t fs = fullScaleMilli();
  if (fs <= 0) {
    return Error::NotInitialized;
  }
  if (milli > ((fs * 1025) / 1000)) {
    return Error::InvalidArgument; // the instrument accepts 2.5 % over full scale (M p.11)
  }
  const bool saved = !identity_.source_known || (identity_.source == SetpointSource::SavedDigital);
  const uint32_t now = bus_.GetPort().NowMs();
  if (saved && have_setpoint_ &&
      (static_cast<uint32_t>(now - last_setpoint_ms_) < config_.saved_setpoint_min_interval_ms)) {
    return Error::RateLimited;
  }
  return Error::Ok;
}

template <UartPort Port>
Result<void> Basis2<Port>::writeSetpoint(int64_t milli) noexcept {
  if (config_.protocol == Protocol::Ascii) {
    ascii::Args a{};
    a.a = milli;
    a.dec = config_.setpoint_decimals;
    a.trim = false;
    return asciiSet(Command::SetSetpoint, a);
  }
  return mbWrite2(reg::kSetpoint, static_cast<uint32_t>(milli), setpointExtraMs());
}

template <UartPort Port>
uint32_t Basis2<Port>::setpointExtraMs() const noexcept {
  // With the saved source, or one not read yet, a setpoint is written to non-volatile memory
  // (M p.11): it gets the reply wait of the other commands that may write it (A6).
  const bool saved = !identity_.source_known || (identity_.source == SetpointSource::SavedDigital);
  const auto slow = static_cast<uint32_t>(config_.slow_command_ms);
  const auto quick = static_cast<uint32_t>(config_.processing_ms);
  return saved ? (slow - quick) : 0U;
}

template <UartPort Port>
Result<void> Basis2<Port>::offOnce(bool setpoint) noexcept {
  return setpoint ? writeSetpoint(0) : holdValve(0);
}

template <UartPort Port>
Result<void> Basis2<Port>::offAt(bool setpoint, const detail::Alternatives& at,
                                 bool adopt) noexcept {
  const char id = config_.unit_id;
  const uint8_t address = config_.modbus_address;
  const uint32_t baud = config_.baud;
  const bool rebaud = (at.baud != 0U) && (at.baud != baud) && Bus<Port>::CanSetBaud();
  if ((at.baud != 0U) && (at.baud != baud) && !rebaud) {
    return Result<void>::Failure(Error::Unsupported);
  }
  if (!detail::SameChar(at.id, '\0')) {
    config_.unit_id = at.id;
  }
  if (at.address != 0U) {
    config_.modbus_address = at.address;
  }
  if (rebaud && !bus_.ApplyBaud(at.baud).ok()) {
    config_.unit_id = id;
    config_.modbus_address = address;
    return Result<void>::Failure(Error::Timeout);
  }
  config_.baud = rebaud ? at.baud : baud;
  const Result<void> r = offOnce(setpoint);
  if (!r.ok() || !adopt) {
    config_.unit_id = id;
    config_.modbus_address = address;
    config_.baud = baud;
    if (rebaud) {
      static_cast<void>(bus_.ApplyBaud(baud));
    }
  }
  return r;
}

template <UartPort Port>
Result<void> Basis2<Port>::offInDoubt(bool setpoint) noexcept {
  Result<void> r = offOnce(setpoint);
  if (r.ok() || !state_unknown_ || !detail::Retryable(r.error)) {
    return r;
  }
  // The instrument did not answer at the settings the driver holds. An unconfirmed guarded command
  // may have moved it: try each setting it could have, and keep the first that answers (the doubt
  // itself stays until `Recover()`), so that a switch-off is never refused for doubt.
  const std::array<detail::Alternatives, 4> tries{{
      {.id = alt_.id, .address = 0U, .baud = 0U, .factory = false},
      {.id = '\0', .address = alt_.address, .baud = 0U, .factory = false},
      {.id = '\0', .address = 0U, .baud = alt_.baud, .factory = false},
      {.id = alt_.factory ? 'A' : '\0',
       .address = alt_.factory ? uint8_t{1} : uint8_t{0},
       .baud = alt_.factory ? 38400U : 0U,
       .factory = alt_.factory},
  }};
  // On a line declared to hold several instruments another ID or address may be a neighbour's: only
  // another speed at the held ID and address is tried there, and the held speed is always put back
  // (the neighbours stay at it), so no unverified unit and no other speed is ever adopted.
  const bool shared = bus_.DeviceCount() > 1U;
  for (const detail::Alternatives& at : tries) {
    const bool moves = (!detail::SameChar(at.id, '\0')) || (at.address != 0U);
    const bool any = moves || (at.baud != 0U);
    if (any && !(shared && moves)) {
      const Result<void> again = offAt(setpoint, at, !shared);
      if (again.ok()) {
        return again;
      }
    }
  }
  return r;
}

template <UartPort Port>
Result<void> Basis2<Port>::SetSetpoint(double flow) noexcept {
  // "Off" is what goes on the wire: a request that rounds to 0.000 is a switch-off.
  int64_t milli = 0;
  const bool scaled = detail::ScaleNonNegative(flow, 1000.0, ascii::kMilliCap, milli);
  if (scaled && (config_.protocol == Protocol::Ascii)) {
    // Fewer than three decimals round what is sent: the off decision, the 102.5 % check and the
    // verify all use that value, not the request.
    milli = ascii::detail::WireMilli(milli, config_.setpoint_decimals);
  }
  const bool off = scaled ? (milli == 0) : !(flow > 0.0);
  if (const Error e = ready(!off, off); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if (const Error e = needController(); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if (!scaled) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (!off) {
    if (const Error e = setpointAllowed(milli); e != Error::Ok) {
      return Result<void>::Failure(e);
    }
  }
  const Result<void> r = off ? offInDoubt(true) : writeSetpoint(milli);
  // A write whose reply was lost may already be in flash: the wear clock starts for every write
  // that reached the wire, except one the instrument refused.
  if (r.ok() || detail::Retryable(r.error)) {
    have_setpoint_ = true;
    last_setpoint_ms_ = bus_.GetPort().NowMs();
  }
  return (!r.ok() || (off && state_unknown_)) ? r : verifiedAfterWrite(milli);
}

template <UartPort Port>
Result<void> Basis2<Port>::verifiedAfterWrite(int64_t milli) noexcept {
  // The setpoint is on the wire: a verify that meets a busy line reports Timeout, not Busy.
  const Result<void> v = verifySetpoint(milli);
  return v.ok() ? v : Result<void>::Failure(detail::AfterSent(v.error));
}

template <UartPort Port>
Result<SetpointSource> Basis2<Port>::ReadSetpointSource() noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<SetpointSource>::Failure(e);
  }
  if (const Error e = needController(); e != Error::Ok) {
    return Result<SetpointSource>::Failure(e);
  }
  if (config_.protocol == Protocol::ModbusRtu) {
    const auto r = readInt(Command::ReadSetpointSource, reg::kSetpointType, 2U);
    if (!r.ok()) {
      return Result<SetpointSource>::Failure(r.error);
    }
    // `readInt` proved the value is 0 to 2: every value is an enumerator.
    return learnSource(static_cast<SetpointSource>(r.value));
  }
  const Result<Reply> r = asciiRun(Command::ReadSetpointSource, ascii::Args{});
  if (!r.ok()) {
    return Result<SetpointSource>::Failure(r.error);
  }
  char c = '\0';
  const Error e = ascii::DecodeLetter(std::span<const char>{r.value.data.data(), r.value.length},
                                      config_.unit_id, "ASU", c);
  if (e != Error::Ok) {
    bus_.EnterQuarantine();
    return Result<SetpointSource>::Failure(e);
  }
  return learnSource(detail::SourceFromLetter(c));
}

template <UartPort Port>
Result<SetpointSource> Basis2<Port>::learnSource(SetpointSource source) noexcept {
  identity_.source = source;
  identity_.source_known = true;
  return Result<SetpointSource>::Success(source);
}

template <UartPort Port>
Result<void> Basis2<Port>::SetSetpointSource(SetpointSource source) noexcept {
  if (!detail::IsEnumerator(source)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if (const Error e = needController(); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  Result<void> r{};
  if (config_.protocol == Protocol::ModbusRtu) {
    r = mbWrite1(reg::kSetpointType, static_cast<uint16_t>(detail::ToUnderlying(source)));
  } else {
    ascii::Args a{};
    a.c = detail::LetterFromSource(source);
    r = asciiSet(Command::SetSetpointSource, a);
  }
  if (r.ok()) {
    identity_.source = source;
    identity_.source_known = true;
  } else {
    invalidate();
  }
  return r;
}

template <UartPort Port>
Result<Ramp> Basis2<Port>::ReadRamp() noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<Ramp>::Failure(e);
  }
  if (const Error e = needController(); e != Error::Ok) {
    return Result<Ramp>::Failure(e);
  }
  Ramp ramp{};
  if (config_.protocol == Protocol::ModbusRtu) {
    const auto r = mbRead(reg::kRamp, 2U);
    if (!r.ok()) {
      return Result<Ramp>::Failure(r.error);
    }
    ramp.rate = Decimal{
        .mantissa = static_cast<int64_t>(detail::Combine(r.value.words[0], r.value.words[1])),
        .decimals = 0U};
  } else {
    const auto v = asciiValues(Command::ReadRamp, 2U);
    if (!v.ok()) {
      return Result<Ramp>::Failure(v.error);
    }
    const Decimal unit = v.value.v[1];
    if ((unit.decimals != 0U) || (unit.mantissa < 3) || (unit.mantissa > 5) ||
        (v.value.v[0].mantissa < 0)) {
      bus_.EnterQuarantine();
      return Result<Ramp>::Failure(Error::BadReply);
    }
    ramp.rate = v.value.v[0];
    ramp.time_unit = static_cast<uint8_t>(unit.mantissa);
  }
  identity_.ramp_active = ramp.rate.mantissa != 0;
  return Result<Ramp>::Success(ramp);
}

template <UartPort Port>
Result<void> Basis2<Port>::SetRamp(double rate, TimeUnit unit) noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if (const Error e = needController(); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  int64_t milli = 0;
  const uint32_t u = detail::ToUnderlying(unit);
  if (!detail::ScaleNonNegative(rate, 1000.0, ascii::kMilliCap, milli) || (milli < 1) || (u < 3U) ||
      (u > 5U)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  Result<void> r{};
  if (config_.protocol == Protocol::Ascii) {
    ascii::Args a{};
    a.a = milli;
    a.b = static_cast<int64_t>(u);
    r = asciiSet(Command::SetRamp, a);
  } else {
    // Percent of full scale per ms times 10^7 (M p.27); the conversion needs the full scale (A9).
    const int64_t fs = fullScaleMilli();
    const double unit_ms = detail::RampUnitMs(u);
    int64_t raw = 0;
    if (fs <= 0) {
      return Result<void>::Failure(Error::NotInitialized);
    }
    if (!detail::ScaleNonNegative(rate * 1.0e9 / (unit_ms * (static_cast<double>(fs) / 1000.0)),
                                  1.0, 1'000'000'000, raw) ||
        (raw < 1)) {
      return Result<void>::Failure(Error::InvalidArgument);
    }
    r = mbWrite2(reg::kRamp, static_cast<uint32_t>(raw));
  }
  if (r.ok() || ((r.error != Error::Rejected) && (r.error != Error::Busy))) {
    identity_.ramp_active = true; // a write that may have landed: the setpoint may lag on purpose
  }
  return r;
}

template <UartPort Port>
Result<void> Basis2<Port>::DisableRamp() noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if (const Error e = needController(); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  Result<void> r{};
  if (config_.protocol == Protocol::Ascii) {
    r = asciiSet(Command::DisableRamp, ascii::Args{});
  } else {
    r = mbWrite2(reg::kRamp, 0U);
  }
  if (r.ok()) {
    identity_.ramp_active = false;
  }
  return r;
}

template <UartPort Port>
Result<uint32_t> Basis2<Port>::ReadWatchdog() noexcept {
  if (const Error e = needController(); e != Error::Ok) {
    return Result<uint32_t>::Failure(e);
  }
  return readInt(Command::ReadWatchdog, reg::kWatchdog, 5000U);
}

template <UartPort Port>
Result<void> Basis2<Port>::SetWatchdog(uint32_t ms) noexcept {
  if (const Error e = needController(); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  return setInt(Command::SetWatchdog, reg::kWatchdog, static_cast<int64_t>(ms));
}

template <UartPort Port>
Result<void> Basis2<Port>::holdValve(int64_t milli) noexcept {
  ascii::Args a{};
  a.a = milli;
  return asciiSet(Command::HoldValve, a);
}

template <UartPort Port>
Result<void> Basis2<Port>::HoldValve(double percent) noexcept {
  // "Off" is what goes on the wire; a hold that opens the valve needs a validated `Begin()`.
  int64_t milli = 0;
  const bool scaled = detail::ScaleNonNegative(percent, 1000.0, 100'000, milli);
  const bool off = scaled ? (milli == 0) : !(percent > 0.0);
  if (const Error e = ready(!off, off); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if (const Error e = needController(); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if (const Error e = needAscii(); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if (!scaled) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  return (milli == 0) ? offInDoubt(false) : holdValve(milli);
}

template <UartPort Port>
Result<void> Basis2<Port>::ResumeControl() noexcept {
  if (const Error e = ready(true); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if (const Error e = needAscii(); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  return asciiSet(Command::ResumeControl, ascii::Args{});
}

template <UartPort Port>
Result<void> Basis2<Port>::OpenValve() noexcept {
  return HoldValve(100.0);
}

template <UartPort Port>
Result<void> Basis2<Port>::CloseValve() noexcept {
  return HoldValve(0.0);
}

// -------------------------------------------------------------------------------------------------------------
// Control tuning
// -------------------------------------------------------------------------------------------------------------

template <UartPort Port>
Result<Gains> Basis2<Port>::ReadGains() noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<Gains>::Failure(e);
  }
  if (const Error e = needController(); e != Error::Ok) {
    return Result<Gains>::Failure(e);
  }
  Gains g{};
  if (config_.protocol == Protocol::ModbusRtu) {
    const auto r = mbRead(reg::kGainP, 2U);
    if (!r.ok()) {
      return Result<Gains>::Failure(r.error);
    }
    g.proportional = r.value.words[0];
    g.integral = r.value.words[1];
    return Result<Gains>::Success(g);
  }
  const auto v = asciiValues(Command::ReadGains, 2U);
  if (!v.ok()) {
    return Result<Gains>::Failure(v.error);
  }
  const Decimal p = v.value.v[0];
  const Decimal i = v.value.v[1];
  if ((p.decimals != 0U) || (i.decimals != 0U) || (p.mantissa < 0) || (p.mantissa > 65535) ||
      (i.mantissa < 0) || (i.mantissa > 65535)) {
    bus_.EnterQuarantine();
    return Result<Gains>::Failure(Error::BadReply);
  }
  g.proportional = static_cast<uint16_t>(p.mantissa);
  g.integral = static_cast<uint16_t>(i.mantissa);
  return Result<Gains>::Success(g);
}

template <UartPort Port>
Result<void> Basis2<Port>::SetGains(uint32_t p, uint32_t i) noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if (const Error e = needController(); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if ((p > 65535U) || (i > 65535U)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (config_.protocol == Protocol::ModbusRtu) {
    if (const auto r = mbWrite1(reg::kGainP, static_cast<uint16_t>(p)); !r.ok()) {
      return r;
    }
    const Result<void> w = mbWrite1(reg::kGainI, static_cast<uint16_t>(i));
    return w.ok() ? w : Result<void>::Failure(detail::AfterSent(w.error));
  }
  ascii::Args a{};
  a.a = static_cast<int64_t>(p);
  a.b = static_cast<int64_t>(i);
  return asciiSet(Command::SetGains, a);
}

template <UartPort Port>
Result<bool> Basis2<Port>::ReadAutotare() noexcept {
  if (const Error e = needController(); e != Error::Ok) {
    return Result<bool>::Failure(e);
  }
  const auto r = readInt(Command::ReadAutotare, reg::kAutotare, 1U);
  return r.ok() ? Result<bool>::Success(r.value != 0U) : Result<bool>::Failure(r.error);
}

template <UartPort Port>
Result<void> Basis2<Port>::SetAutotare(bool on) noexcept {
  if (const Error e = needController(); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  return setInt(Command::SetAutotare, reg::kAutotare, on ? 1 : 0);
}

template <UartPort Port>
Result<uint8_t> Basis2<Port>::ReadMeasurementTrigger() noexcept {
  if (const Error e = needController(); e != Error::Ok) {
    return Result<uint8_t>::Failure(e);
  }
  const auto r = readInt(Command::ReadTrigger, reg::kTrigger, 7U);
  return r.ok() ? Result<uint8_t>::Success(static_cast<uint8_t>(r.value))
                : Result<uint8_t>::Failure(r.error);
}

template <UartPort Port>
Result<void> Basis2<Port>::SetMeasurementTrigger(uint8_t mask) noexcept {
  if (const Error e = needController(); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  return setInt(Command::SetTrigger, reg::kTrigger, mask);
}

// -------------------------------------------------------------------------------------------------------------
// Measurement window and tare
// -------------------------------------------------------------------------------------------------------------

template <UartPort Port>
Result<void> Basis2<Port>::StartMeasurement(uint32_t duration_ms) noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if ((duration_ms < 1U) || (duration_ms > static_cast<uint32_t>(ascii::kMeasureMaxMs))) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (config_.protocol == Protocol::ModbusRtu) {
    const uint32_t samples =
        detail::CeilDiv(static_cast<uint64_t>(duration_ms) * 10U, reg::kSampleTenthMs);
    return mbWrite1(reg::kSamples, static_cast<uint16_t>(samples));
  }
  ascii::Args a{};
  a.a = static_cast<int64_t>(duration_ms);
  return asciiSet(Command::StartMeasurement, a);
}

template <UartPort Port>
Result<Averages> Basis2<Port>::ReadAverages() noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<Averages>::Failure(e);
  }
  Averages out{};
  if (config_.protocol == Protocol::ModbusRtu) {
    const auto r = mbRead(reg::kMeasureBlock, 8U);
    if (!r.ok()) {
      return Result<Averages>::Failure(r.error);
    }
    const auto& w = r.value.words;
    out.time_ms = detail::CeilDiv(static_cast<uint64_t>(w[4]) * reg::kSampleTenthMs, 10U);
    out.temperature = Decimal{.mantissa = detail::Signed16(w[5]), .decimals = 2U};
    out.flow =
        Decimal{.mantissa = detail::Signed16(w[6]), .decimals = config_.modbus_flow_decimals};
    out.valve_drive = Decimal{.mantissa = static_cast<int64_t>(w[7]), .decimals = 2U};
    return Result<Averages>::Success(out);
  }
  const Result<Reply> r = asciiRun(Command::ReadAverages, ascii::Args{});
  if (!r.ok()) {
    return Result<Averages>::Failure(r.error);
  }
  ascii::Values v{};
  const Error e = ascii::DecodeWindow(std::span<const char>{r.value.data.data(), r.value.length},
                                      config_.unit_id, 2U, out.time_ms, v);
  if (e != Error::Ok) {
    bus_.EnterQuarantine();
    return Result<Averages>::Failure(e);
  }
  out.temperature = v.v[0];
  out.flow = v.v[1];
  return Result<Averages>::Success(out);
}

template <UartPort Port>
Result<Ranges> Basis2<Port>::ReadRanges() noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<Ranges>::Failure(e);
  }
  Ranges out{};
  if (config_.protocol == Protocol::ModbusRtu) {
    const auto r = mbRead(reg::kMeasureBlock, 8U);
    if (!r.ok()) {
      return Result<Ranges>::Failure(r.error);
    }
    const auto& w = r.value.words;
    const uint8_t d = config_.modbus_flow_decimals;
    out.min_temperature = Decimal{.mantissa = detail::Signed16(w[0]), .decimals = 2U};
    out.max_temperature = Decimal{.mantissa = detail::Signed16(w[1]), .decimals = 2U};
    out.min_flow = Decimal{.mantissa = detail::Signed16(w[2]), .decimals = d};
    out.max_flow = Decimal{.mantissa = detail::Signed16(w[3]), .decimals = d};
    out.time_ms = detail::CeilDiv(static_cast<uint64_t>(w[4]) * reg::kSampleTenthMs, 10U);
    return Result<Ranges>::Success(out);
  }
  const Result<Reply> r = asciiRun(Command::ReadRanges, ascii::Args{});
  if (!r.ok()) {
    return Result<Ranges>::Failure(r.error);
  }
  ascii::Values v{};
  const Error e = ascii::DecodeWindow(std::span<const char>{r.value.data.data(), r.value.length},
                                      config_.unit_id, 4U, out.time_ms, v);
  if (e != Error::Ok) {
    bus_.EnterQuarantine();
    return Result<Ranges>::Failure(e);
  }
  out.min_temperature = v.v[0];
  out.max_temperature = v.v[1];
  out.min_flow = v.v[2];
  out.max_flow = v.v[3];
  return Result<Ranges>::Success(out);
}

template <UartPort Port>
Result<PreviousMeasurement> Basis2<Port>::ReadPreviousMeasurement() noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<PreviousMeasurement>::Failure(e);
  }
  if (const Error e = needModbus(); e != Error::Ok) {
    return Result<PreviousMeasurement>::Failure(e);
  }
  const auto r = mbRead(reg::kPreviousBlock, 6U);
  if (!r.ok()) {
    return Result<PreviousMeasurement>::Failure(r.error);
  }
  const auto& w = r.value.words;
  const uint8_t d = config_.modbus_flow_decimals;
  PreviousMeasurement p{};
  p.min_flow = Decimal{.mantissa = detail::Signed16(w[0]), .decimals = d};
  p.max_flow = Decimal{.mantissa = detail::Signed16(w[1]), .decimals = d};
  p.samples = w[2];
  p.temperature = Decimal{.mantissa = detail::Signed16(w[3]), .decimals = 2U};
  p.average_flow =
      Decimal{.mantissa = detail::Signed32(detail::Combine(w[4], w[5])), .decimals = d};
  return Result<PreviousMeasurement>::Success(p);
}

template <UartPort Port>
Result<void> Basis2<Port>::Tare(uint32_t duration_ms) noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if ((duration_ms < 1U) || (duration_ms > 32767U)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (config_.protocol == Protocol::Ascii) {
    ascii::Args a{};
    a.a = static_cast<int64_t>(duration_ms);
    return asciiSet(Command::Tare, a);
  }
  const uint32_t samples =
      detail::CeilDiv(static_cast<uint64_t>(duration_ms) * 10U, reg::kSampleTenthMs);
  if (const auto r = mbWrite1(reg::kTareSamples, static_cast<uint16_t>(samples)); !r.ok()) {
    return r;
  }
  const Result<void> t = mbWrite1(reg::kTare, reg::kMagicAction, duration_ms, 0U);
  return t.ok() ? t : Result<void>::Failure(detail::AfterSent(t.error));
}

template <UartPort Port>
Result<void> Basis2<Port>::TareDefault() noexcept {
  return Tare(1000U);
}

// -------------------------------------------------------------------------------------------------------------
// Gas, totalizer, processing
// -------------------------------------------------------------------------------------------------------------

template <UartPort Port>
Result<void> Basis2<Port>::SetGas(GasId gas) noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if (config_.protocol == Protocol::ModbusRtu) {
    if (const auto r = mbWrite1(reg::kGas, gas.number); !r.ok()) {
      return r;
    }
    const auto back = mbRead(reg::kGas, 1U);
    if (!back.ok()) {
      return Result<void>::Failure(detail::AfterSent(back.error));
    }
    return (back.value.words[0] == gas.number) ? Result<void>::Success()
                                               : Result<void>::Failure(Error::VerifyMismatch);
  }
  ascii::Args a{};
  a.a = gas.number;
  const Result<Reply> r = asciiRun(Command::SetGas, a);
  if (!r.ok()) {
    return Result<void>::Failure(r.error);
  }
  Frame f{};
  const Error e = ascii::DecodeGas(std::span<const char>{r.value.data.data(), r.value.length},
                                   config_.unit_id, f);
  if (e != Error::Ok) {
    // The gas sets the units and calibration of every later reading: an undecodable reply leaves
    // it uncertain, so it is not reported as set.
    bus_.EnterQuarantine();
    return Result<void>::Failure(e);
  }
  return (f.gas.number == gas.number) ? Result<void>::Success()
                                      : Result<void>::Failure(Error::VerifyMismatch);
}

template <UartPort Port>
Result<GasTable> Basis2<Port>::ReadGasTable() noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<GasTable>::Failure(e);
  }
  if (const Error e = needModbus(); e != Error::Ok) {
    return Result<GasTable>::Failure(e);
  }
  const auto r = mbRead(reg::kGasTable, 60U);
  if (!r.ok()) {
    return Result<GasTable>::Failure(r.error);
  }
  GasTable t{};
  for (std::size_t i = 0U; i < 12U; ++i) {
    const std::size_t base = i * 5U;
    t.entries[i].number = static_cast<uint8_t>(r.value.words[base] & 0xFFU);
    t.entries[i].k_factor = r.value.words[base + 1U];
    const Text label = detail::RegistersToText(r.value, base + 2U, 3U);
    for (std::size_t k = 0U; (k < label.length) && (k < 6U); ++k) {
      t.entries[i].label[k] = label.chars[k];
    }
  }
  return Result<GasTable>::Success(t);
}

template <UartPort Port>
Result<GasList> Basis2<Port>::ReadGasList() noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<GasList>::Failure(e);
  }
  GasList g{};
  if (config_.protocol == Protocol::ModbusRtu) {
    const auto t = ReadGasTable();
    if (!t.ok()) {
      return Result<GasList>::Failure(t.error);
    }
    for (const GasEntry& entry : t.value.entries) {
      if ((!detail::SameChar(entry.label[0], '\0')) || (entry.k_factor != 0U)) {
        g.numbers[g.count] = entry.number;
        ++g.count;
      }
    }
    return Result<GasList>::Success(g);
  }
  const Result<Reply> r = asciiRun(Command::ReadGasList, ascii::Args{});
  if (!r.ok()) {
    return Result<GasList>::Failure(r.error);
  }
  const Error e = ascii::DecodeGasList(std::span<const char>{r.value.data.data(), r.value.length},
                                       config_.unit_id, g);
  if (e != Error::Ok) {
    bus_.EnterQuarantine();
    return Result<GasList>::Failure(e);
  }
  return Result<GasList>::Success(g);
}

template <UartPort Port>
Result<void> Basis2<Port>::ResetTotal() noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if (config_.protocol == Protocol::ModbusRtu) {
    return mbWrite1(reg::kTotalReset, reg::kMagicAction);
  }
  return asciiSet(Command::ResetTotal, ascii::Args{});
}

template <UartPort Port>
Result<TotalizerLimit> Basis2<Port>::ReadTotalizerLimit() noexcept {
  const auto r = readInt(Command::ReadTotalizerLimit, reg::kTotalLimit, 3U);
  return r.ok() ? Result<TotalizerLimit>::Success(static_cast<TotalizerLimit>(r.value))
                : Result<TotalizerLimit>::Failure(r.error);
}

template <UartPort Port>
Result<void> Basis2<Port>::SetTotalizerLimit(TotalizerLimit mode) noexcept {
  if (!detail::IsEnumerator(mode)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  return setInt(Command::SetTotalizerLimit, reg::kTotalLimit, detail::ToUnderlying(mode));
}

template <UartPort Port>
Result<void> Basis2<Port>::SetBatch(double volume) noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if (const Error e = needController(); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  int64_t milli = 0;
  if (!detail::ScaleNonNegative(volume, 1000.0, ascii::kMilliCap, milli) || (milli < 1)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (config_.protocol == Protocol::Ascii) {
    ascii::Args a{};
    a.a = milli;
    return asciiSet(Command::SetBatch, a);
  }
  if (config_.modbus_flow_decimals == kDecimalsUnset) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  int64_t scaled = 0;
  double scale = 1.0;
  for (uint8_t i = 0U; i < config_.modbus_flow_decimals; ++i) {
    scale *= 10.0;
  }
  if (!detail::ScaleNonNegative(volume, scale, 0xFFFF'FFFFLL, scaled) || (scaled < 1)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  return mbWrite2(reg::kBatch, static_cast<uint32_t>(scaled));
}

template <UartPort Port>
Result<void> Basis2<Port>::DisableBatch() noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if (const Error e = needController(); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  return (config_.protocol == Protocol::Ascii) ? asciiSet(Command::DisableBatch, ascii::Args{})
                                               : mbWrite2(reg::kBatch, 0U);
}

template <UartPort Port>
Result<uint32_t> Basis2<Port>::ReadAveraging() noexcept {
  return readInt(Command::ReadAveraging, reg::kAveraging, 2500U);
}

template <UartPort Port>
Result<void> Basis2<Port>::SetAveraging(uint32_t ms) noexcept {
  return setInt(Command::SetAveraging, reg::kAveraging, static_cast<int64_t>(ms));
}

template <UartPort Port>
Result<Decimal> Basis2<Port>::ReadReferenceTemperature() noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<Decimal>::Failure(e);
  }
  if (config_.protocol == Protocol::ModbusRtu) {
    const auto r = mbRead(reg::kRefTemperature, 1U);
    return r.ok() ? Result<Decimal>::Success(
                        Decimal{.mantissa = static_cast<int64_t>(r.value.words[0]), .decimals = 2U})
                  : Result<Decimal>::Failure(r.error);
  }
  const auto v = asciiValues(Command::ReadReferenceTemperature, 1U);
  return v.ok() ? Result<Decimal>::Success(v.value.v[0]) : Result<Decimal>::Failure(v.error);
}

template <UartPort Port>
Result<void> Basis2<Port>::SetReferenceTemperature(double celsius) noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  int64_t milli = 0;
  if (!detail::ScaleNonNegative(celsius, 1000.0, 30'000, milli)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (config_.protocol == Protocol::ModbusRtu) {
    return mbWrite1(reg::kRefTemperature, static_cast<uint16_t>((milli + 5) / 10));
  }
  ascii::Args a{};
  a.a = milli;
  return asciiSet(Command::SetReferenceTemperature, a);
}

// -------------------------------------------------------------------------------------------------------------
// Identity
// -------------------------------------------------------------------------------------------------------------

template <UartPort Port>
Result<FullScale> Basis2<Port>::ReadFullScale(FullScaleItem item) noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<FullScale>::Failure(e);
  }
  if (!detail::IsEnumerator(item)) {
    return Result<FullScale>::Failure(Error::InvalidArgument);
  }
  if (config_.protocol == Protocol::ModbusRtu) {
    if (item != FullScaleItem::Flow) {
      return Result<FullScale>::Failure(Error::Unsupported);
    }
    const auto r = mbRead(reg::kFullScale, 2U); // registers 47 and 48 only: 49 is read on its own
    if (!r.ok()) {
      return Result<FullScale>::Failure(r.error);
    }
    FullScale fs{};
    fs.value = Decimal{
        .mantissa = static_cast<int64_t>(detail::Combine(r.value.words[0], r.value.words[1])),
        .decimals = 3U};
    return Result<FullScale>::Success(fs);
  }
  ascii::Args a{};
  a.a = detail::ToUnderlying(item);
  const Result<Reply> r = asciiRun(Command::ReadFullScale, a);
  if (!r.ok()) {
    return Result<FullScale>::Failure(r.error);
  }
  FullScale fs{};
  const Error e = ascii::DecodeFullScale(std::span<const char>{r.value.data.data(), r.value.length},
                                         config_.unit_id, fs);
  if (e != Error::Ok) {
    bus_.EnterQuarantine();
    return Result<FullScale>::Failure(e);
  }
  return Result<FullScale>::Success(fs);
}

template <UartPort Port>
Result<Text> Basis2<Port>::modbusText(uint16_t first, uint16_t count) noexcept {
  const auto r = mbRead(first, count);
  return r.ok() ? Result<Text>::Success(detail::RegistersToText(r.value, 0U, count))
                : Result<Text>::Failure(r.error);
}

template <UartPort Port>
Result<Text> Basis2<Port>::ReadSerialNumber() noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<Text>::Failure(e);
  }
  if (config_.protocol == Protocol::ModbusRtu) {
    return modbusText(reg::kSerial, 6U);
  }
  const Result<Reply> r = asciiRun(Command::ReadSerialNumber, ascii::Args{});
  if (!r.ok()) {
    return Result<Text>::Failure(r.error);
  }
  Text t{};
  const Error e = ascii::DecodeText(std::span<const char>{r.value.data.data(), r.value.length},
                                    config_.unit_id, t);
  if (e != Error::Ok) {
    bus_.EnterQuarantine();
    return Result<Text>::Failure(e);
  }
  return Result<Text>::Success(t);
}

template <UartPort Port>
Result<Text> Basis2<Port>::ReadFirmware() noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<Text>::Failure(e);
  }
  if (config_.protocol == Protocol::ModbusRtu) {
    const auto r = mbRead(reg::kFirmware, 1U);
    if (!r.ok()) {
      return Result<Text>::Failure(r.error);
    }
    const uint32_t v = r.value.words[0];
    Text t{};
    const std::array<char, 6> text{detail::DigitChar((v >> 8U) & 0xFU), '.',
                                   detail::DigitChar((v >> 4U) & 0xFU), '.',
                                   detail::DigitChar(v & 0xFU),         '\0'};
    for (std::size_t i = 0U; i < 5U; ++i) {
      t.chars[i] = text[i];
    }
    t.length = 5U;
    return Result<Text>::Success(t);
  }
  const Result<Reply> r = asciiRun(Command::ReadFirmware, ascii::Args{});
  if (!r.ok()) {
    return Result<Text>::Failure(r.error);
  }
  Text t{};
  const Error e = ascii::DecodeText(std::span<const char>{r.value.data.data(), r.value.length},
                                    config_.unit_id, t);
  if (e != Error::Ok) {
    bus_.EnterQuarantine();
    return Result<Text>::Failure(e);
  }
  return Result<Text>::Success(t);
}

template <UartPort Port>
Result<uint32_t> Basis2<Port>::ReadModbusAddress() noexcept {
  return readInt(Command::ReadModbusAddress, reg::kModbusAddress, 247U);
}

template <UartPort Port>
Result<Baud> Basis2<Port>::ReadBaud() noexcept {
  const bool is_modbus = config_.protocol == Protocol::ModbusRtu;
  const auto r = readInt(Command::ReadBaud, reg::kBaud, is_modbus ? 5U : 115200U);
  if (!r.ok()) {
    return Result<Baud>::Failure(r.error);
  }
  if (is_modbus) {
    return Result<Baud>::Success(static_cast<Baud>(r.value));
  }
  const auto found = std::find(detail::kBaudRates.begin(), detail::kBaudRates.end(), r.value);
  if (found != detail::kBaudRates.end()) {
    return Result<Baud>::Success(static_cast<Baud>(found - detail::kBaudRates.begin()));
  }
  bus_.EnterQuarantine();
  return Result<Baud>::Failure(Error::BadReply);
}

template <UartPort Port>
Result<CommandProtocol> Basis2<Port>::ReadProtocol() noexcept {
  const bool is_modbus = config_.protocol == Protocol::ModbusRtu;
  const auto r = readInt(Command::ReadProtocol, reg::kProtocol, is_modbus ? 1U : 2U);
  if (!r.ok()) {
    return Result<CommandProtocol>::Failure(r.error);
  }
  // Modbus 56: 0 is BASIS 2, 1 is BASIS 1. The ASCII reply is assumed to carry the protocol number
  // (A3).
  const bool one = (r.value == 1U);
  if (!is_modbus && (r.value == 0U)) {
    bus_.EnterQuarantine();
    return Result<CommandProtocol>::Failure(Error::BadReply);
  }
  return Result<CommandProtocol>::Success(one ? CommandProtocol::BasisProtocol1
                                              : CommandProtocol::BasisProtocol2);
}

template <UartPort Port>
Result<FlowUnit> Basis2<Port>::ReadFlowUnit() noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<FlowUnit>::Failure(e);
  }
  if (const Error e = needModbus(); e != Error::Ok) {
    return Result<FlowUnit>::Failure(e);
  }
  const auto r = mbRead(reg::kFlowUnit, 1U);
  return r.ok() ? Result<FlowUnit>::Success(reg::FlowUnitFromCode(r.value.words[0]))
                : Result<FlowUnit>::Failure(r.error);
}

template <UartPort Port>
Result<uint32_t> Basis2<Port>::ReadFullScaleSccm() noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<uint32_t>::Failure(e);
  }
  if (const Error e = needModbus(); e != Error::Ok) {
    return Result<uint32_t>::Failure(e);
  }
  const auto r = mbRead(reg::kFullScaleSccm, 2U);
  return r.ok() ? Result<uint32_t>::Success(detail::Combine(r.value.words[0], r.value.words[1]))
                : Result<uint32_t>::Failure(r.error);
}

template <UartPort Port>
Result<uint16_t> Basis2<Port>::ReadSensorOffset() noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<uint16_t>::Failure(e);
  }
  if (const Error e = needModbus(); e != Error::Ok) {
    return Result<uint16_t>::Failure(e);
  }
  const auto r = mbRead(reg::kSensorOffset, 1U);
  return r.ok() ? Result<uint16_t>::Success(r.value.words[0]) : Result<uint16_t>::Failure(r.error);
}

// -------------------------------------------------------------------------------------------------------------
// Guarded commands
// -------------------------------------------------------------------------------------------------------------

template <UartPort Port>
Result<void> Basis2<Port>::failState(Error error) noexcept {
  if (error == Error::Busy) {
    return Result<void>::Failure(Error::Busy); // the line never went quiet: nothing was sent
  }
  state_unknown_ = true;
  ready_ = false;
  return Result<void>::Failure((error == Error::Rejected) ? Error::Rejected : Error::StateUnknown);
}

template <UartPort Port>
Result<void> Basis2<Port>::guardedAscii(Command command, const ascii::Args& args) noexcept {
  Result<Request> made = ascii::Make(command, config_.unit_id, args, true);
  if (!made.ok()) {
    return Result<void>::Failure(made.error);
  }
  if (command == Command::FactoryRestore) {
    made.value.SetTiming(0U, config_.restore_holdoff_ms);
  }
  const Result<Reply> r = runOnce(made.value);
  if (r.ok()) {
    return Result<void>::Success();
  }
  return (r.error == Error::Rejected) ? Result<void>::Failure(Error::Rejected) : failState(r.error);
}

template <UartPort Port>
Result<void> Basis2<Port>::guardedFinish(char new_id) noexcept {
  const char old_id = config_.unit_id;
  config_.unit_id = new_id;
  layout_known_ = false;
  if (ReadFrame().ok()) {
    return Result<void>::Success();
  }
  config_.unit_id = old_id;
  return failState(Error::Timeout);
}

template <UartPort Port>
Result<void> Basis2<Port>::ChangeUnitId(char new_id, ConfirmUnitIdChange /*confirm*/) noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if (!detail::IsUpper(new_id) || (detail::SameChar(new_id, config_.unit_id))) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (bus_.DeviceCount() > 1U) {
    return Result<void>::Failure(Error::GuardNotConfirmed);
  }
  alt_ = detail::Alternatives{.id = new_id, .address = 0U, .baud = 0U, .factory = false};
  if (config_.protocol == Protocol::ModbusRtu) {
    const auto w = mbWrite1(reg::kUnitId, static_cast<uint16_t>(detail::ByteOf(new_id)));
    if (!w.ok()) {
      return (w.error == Error::Rejected) ? w : failState(w.error);
    }
    const auto back = mbRead(reg::kUnitId, 1U);
    if (!back.ok() || (back.value.words[0] != static_cast<uint16_t>(detail::ByteOf(new_id)))) {
      return failState(Error::Timeout);
    }
    config_.unit_id = new_id;
    return Result<void>::Success();
  }
  ascii::Args a{};
  a.c = new_id;
  if (const auto r = guardedAscii(Command::ChangeUnitId, a); !r.ok()) {
    return r;
  }
  return guardedFinish(new_id);
}

template <UartPort Port>
Result<void> Basis2<Port>::setAddressOnBus(uint8_t address, uint16_t target) noexcept {
  const uint8_t old_address = config_.modbus_address;
  const auto w = mbWrite1(target, address);
  if (!w.ok()) {
    return (w.error == Error::Rejected) ? w : failState(w.error);
  }
  config_.modbus_address = address;
  const auto back = mbRead(target, 1U);
  if (back.ok() && (back.value.words[0] == address)) {
    return Result<void>::Success();
  }
  config_.modbus_address = old_address;
  return failState(Error::Timeout);
}

template <UartPort Port>
Result<void> Basis2<Port>::SetModbusAddress(uint8_t address,
                                            ConfirmAddressChange /*confirm*/) noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if ((address < 1U) || (address > 247U)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (bus_.DeviceCount() > 1U) {
    // An unconfirmed change could not be traced on a shared line: the address tried could be a
    // neighbour's.
    return Result<void>::Failure(Error::GuardNotConfirmed);
  }
  alt_ = detail::Alternatives{.id = '\0', .address = address, .baud = 0U, .factory = false};
  if (config_.protocol == Protocol::ModbusRtu) {
    return setAddressOnBus(address, reg::kModbusAddress);
  }
  ascii::Args a{};
  a.a = address;
  if (const auto r = guardedAscii(Command::SetModbusAddress, a); !r.ok()) {
    return r;
  }
  const auto back = ReadModbusAddress();
  return (back.ok() && (back.value == address)) ? Result<void>::Success()
                                                : failState(Error::Timeout);
}

template <UartPort Port>
bool Basis2<Port>::baudVerified(bool sent, uint8_t code) noexcept {
  if (!sent) {
    return false;
  }
  if (config_.protocol == Protocol::Ascii) {
    return ReadFrame().ok();
  }
  // The register must hold the new code, not merely answer at the new speed.
  const auto back = mbRead(reg::kBaud, 1U);
  return back.ok() && (back.value.words[0] == code);
}

template <UartPort Port>
Result<void> Basis2<Port>::ChangeBaud(Baud baud, ConfirmBaudChange /*confirm*/) noexcept {
  if (!detail::IsEnumerator(baud)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if (!Bus<Port>::CanSetBaud()) {
    return Result<void>::Failure(Error::Unsupported);
  }
  if (bus_.DeviceCount() > 1U) {
    // One instrument cannot change the speed of a shared line: its neighbours stay at the old one.
    return Result<void>::Failure(Error::GuardNotConfirmed);
  }
  const uint8_t code = detail::ToUnderlying(baud);
  if ((code >= 4U) && (gate(reg::kFw222) != Error::Ok)) {
    return Result<void>::Failure(Error::FirmwareTooOld);
  }
  const uint32_t old_baud = config_.baud;
  alt_ = detail::Alternatives{
      .id = '\0', .address = 0U, .baud = detail::kBaudRates[code], .factory = false};
  Result<Request> made{};
  if (config_.protocol == Protocol::Ascii) {
    ascii::Args a{};
    a.a = static_cast<int64_t>(detail::kBaudRates[code]);
    made = ascii::Make(Command::ChangeBaud, config_.unit_id, a, true);
  } else {
    made = modbus::BuildWriteSingle(config_.modbus_address, reg::kBaud, code);
  }
  if (!made.ok()) {
    return Result<void>::Failure(made.error);
  }
  Timing t = timing();
  t.switch_to_baud = detail::kBaudRates[code];
  const Result<Reply> r = bus_.Execute(made.value, t);
  if ((r.error == Error::Rejected) || (r.error == Error::Busy)) {
    // The instrument kept its speed (or never heard the request): the port goes back to it and
    // nothing is in doubt.
    static_cast<void>(bus_.ApplyBaud(old_baud));
    alt_ = detail::Alternatives{};
    return Result<void>::Failure(r.error);
  }
  config_.baud = detail::kBaudRates[code];
  if (!baudVerified(r.ok(), code)) {
    config_.baud = old_baud;
    static_cast<void>(bus_.ApplyBaud(old_baud));
    return failState(Error::Timeout);
  }
  return Result<void>::Success();
}

template <UartPort Port>
Result<void> Basis2<Port>::SwitchToProtocol1(ConfirmAbandonProtocol2 /*confirm*/) noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  const FlowUnit u = identity_.flow_unit;
  const bool allowed = (u == FlowUnit::Sccm) || (u == FlowUnit::Slpm) || (u == FlowUnit::Nccm) ||
                       (u == FlowUnit::Nlpm);
  if (!allowed) {
    return Result<void>::Failure(Error::Unsupported);
  }
  alt_ = detail::Alternatives{};
  Result<void> r{};
  if (config_.protocol == Protocol::Ascii) {
    ascii::Args a{};
    a.a = 1;
    r = guardedAscii(Command::SwitchToProtocol1, a);
  } else {
    r = mbWrite1(reg::kProtocol, 1U);
    if (!r.ok() && (r.error != Error::Rejected)) {
      return failState(r.error);
    }
  }
  if (r.ok()) {
    abandoned_ = true;
    ready_ = false;
  }
  return r;
}

template <UartPort Port>
Result<void> Basis2<Port>::SwitchToProtocol2(ConfirmReturnToProtocol2 /*confirm*/) noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (!state_unknown_) {
    alt_ = detail::Alternatives{};
  }
  Result<void> r{};
  if (config_.protocol == Protocol::Ascii) {
    r = guardedAscii(Command::SwitchToProtocol2, ascii::Args{});
  } else {
    r = mbWrite1(reg::kProtocol, 0U);
  }
  if (r.ok()) {
    abandoned_ = false;
  }
  return r;
}

template <UartPort Port>
Result<void> Basis2<Port>::FactoryRestore(ConfirmFactoryRestore /*confirm*/) noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if (bus_.DeviceCount() > 1U) {
    // The factory unit ID A and address 1 collide with a neighbour on a shared line (manual p. 16).
    return Result<void>::Failure(Error::GuardNotConfirmed);
  }
  alt_ = detail::Alternatives{.id = 'A', .address = 1U, .baud = 38400U, .factory = true};
  Result<void> r{};
  if (config_.protocol == Protocol::Ascii) {
    r = guardedAscii(Command::FactoryRestore, ascii::Args{});
  } else {
    r = mbWrite1(reg::kFactoryRestore, reg::kMagicRestore, 0U, config_.restore_holdoff_ms);
  }
  if ((r.error == Error::Rejected) || (r.error == Error::Busy)) {
    return r; // refused, or never sent
  }
  state_unknown_ = true;
  ready_ = false;
  return r.ok() ? Result<void>::Success() : r;
}

// -------------------------------------------------------------------------------------------------------------
// Scans
// -------------------------------------------------------------------------------------------------------------

template <UartPort Port>
Result<UnitIdScan> Basis2<Port>::ScanUnitIds() noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<UnitIdScan>::Failure(e);
  }
  if (const Error e = needAscii(); e != Error::Ok) {
    return Result<UnitIdScan>::Failure(e);
  }
  const char original = config_.unit_id;
  UnitIdScan scan{};
  bool busy = false;
  for (uint32_t k = 0U; (k < 26U) && !busy; ++k) {
    config_.unit_id = detail::UpperChar(k);
    const auto made = ascii::Make(Command::Poll, config_.unit_id, ascii::Args{}, false);
    const Result<Reply> r = runOnce(made.value);
    Frame f{};
    FrameLayout seen = FrameLayout::Auto;
    if (r.ok() && (ascii::DecodeFrame(std::span<const char>{r.value.data.data(), r.value.length},
                                      rules(), f, seen) == Error::Ok)) {
      scan.found = scan.found | (1U << k);
    } else if (r.ok()) {
      bus_.EnterQuarantine(); // an answer that is no frame: drain it before the next poll
    } else {
      // No answer under this ID.
    }
    busy = r.error == Error::Busy;
  }
  config_.unit_id = original;
  // A poll that met a line that never went quiet was not sent: the scan cannot say who is there.
  return busy ? Result<UnitIdScan>::Failure(Error::Busy) : Result<UnitIdScan>::Success(scan);
}

template <UartPort Port>
Result<AddressScan> Basis2<Port>::ScanModbusAddresses(uint8_t first, uint8_t last) noexcept {
  if (const Error e = ready(false); e != Error::Ok) {
    return Result<AddressScan>::Failure(e);
  }
  if (const Error e = needModbus(); e != Error::Ok) {
    return Result<AddressScan>::Failure(e);
  }
  if ((first < 1U) || (last > 247U) || (first > last)) {
    return Result<AddressScan>::Failure(Error::InvalidArgument);
  }
  AddressScan scan{};
  bool busy = false;
  for (uint32_t a = first; (a <= last) && !busy; ++a) {
    const auto built = modbus::BuildRead(static_cast<uint8_t>(a), reg::kFirmware, 1U);
    const Result<Reply> r = runOnce(built.value);
    modbus::Response resp{};
    if (r.ok() && (modbus::ParseResponse(std::span<const char>{r.value.data.data(), r.value.length},
                                         static_cast<uint8_t>(a), modbus::kReadHolding,
                                         built.value.Bytes(), resp) == Error::Ok)) {
      scan.found[a / 8U] = static_cast<uint8_t>(scan.found[a / 8U] | (1U << (a % 8U)));
    } else if (r.ok()) {
      bus_.EnterQuarantine();
    } else {
      busy = r.error == Error::Busy; // else no answer at this address
    }
  }
  return busy ? Result<AddressScan>::Failure(Error::Busy) : Result<AddressScan>::Success(scan);
}

} // namespace alicat_basis2

#endif // ALICAT_BASIS2_IPP
