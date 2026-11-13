/**
 * @file alicat_basis2_ascii.hpp
 * @brief The ASCII grammar of "BASIS Protocol 2" (M pp.10 to 17): the command table, the encoder,
 * the independent wire validator and the pure reply decoders.
 * @copyright Copyright (c) 2026 The driver-alicat-basis2 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Wire form of a request: `<ID><TOKEN>[ <ARG>]<CR>`. Numbers are formatted in integer arithmetic
 * (never `printf("%f")`, never an exponent). Everything here is pure: no I/O, no allocation.
 */
#ifndef ALICAT_BASIS2_ASCII_HPP
#define ALICAT_BASIS2_ASCII_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "alicat_basis2_registers.hpp"
#include "alicat_basis2_request.hpp"
#include "alicat_basis2_types.hpp"

/** @brief The ASCII grammar: table, encoder, validator, decoders. */
namespace alicat_basis2::ascii {

/**
 * @defgroup alicat_basis2_ascii ASCII grammar
 * @brief Table, encoder, wire validator and decoders.
 * @{
 */

/** @brief Carriage return, the terminator of every request and reply (M p.10). */
inline constexpr char kCr = '\r';

/** @brief What a command's argument looks like. */
enum class ArgKind : uint8_t {
  None = 0,    ///< No argument.
  Int = 1,     ///< One integer in `lo` to `hi`.
  Milli = 2,   ///< One decimal number, given in thousandths, in `lo` to `hi`.
  Letter = 3,  ///< One character out of `letters`.
  Star = 4,    ///< The literal `*`.
  Ramp = 5,    ///< A decimal rate then a time unit 3 to 5.
  IntPair = 6, ///< Two integers, each in `lo` to `hi`.
};

/** @brief One row of the command table (rows C01 to C46). */
struct CommandSpec {
  Command id;            ///< The command.
  const char* token;     ///< The text after the unit ID.
  ArgKind arg;           ///< The argument form.
  bool space;            ///< A space between the token and the argument.
  bool controller_only;  ///< The manual says controllers only.
  uint16_t min_firmware; ///< Firmware gate for the ASCII command, 0 for none.
  uint16_t reply_max;    ///< Most characters of the reply line.
  bool slow;             ///< May write non-volatile memory (longer reply wait).
  bool guarded;          ///< Needs a guard token.
  int64_t lo;            ///< Lowest argument value.
  int64_t hi;            ///< Highest argument value.
  const char* letters;   ///< Allowed characters of a `Letter` argument.
};

/** @brief Number of rows of the command table. */
inline constexpr std::size_t kCommandRows = 46;

/// Upper bound of a decimal argument in thousandths (10^12: below the mantissa limit of the
/// parser).
inline constexpr int64_t kMilliCap = 999'999'999'999LL;
/// Longest measurement duration in ms the driver sends (derived: 65535 samples of 2.5 ms, A7).
inline constexpr int64_t kMeasureMaxMs = 163'837;

/** @brief The command table, indexed by `Command` (data; one row per command of the coverage map).
 */
inline constexpr std::array<CommandSpec, kCommandRows> kCommands{
    {{.id = Command::Poll,
      .token = "",
      .arg = ArgKind::None,
      .space = false,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 96,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::ReadData,
      .token = "DV",
      .arg = ArgKind::Int,
      .space = true,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 96,
      .slow = false,
      .guarded = false,
      .lo = 1,
      .hi = 255,
      .letters = ""},
     {.id = Command::SetSetpoint,
      .token = "S",
      .arg = ArgKind::Milli,
      .space = true,
      .controller_only = true,
      .min_firmware = 0,
      .reply_max = 96,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = kMilliCap,
      .letters = ""},
     {.id = Command::ReadSetpointSource,
      .token = "LSS",
      .arg = ArgKind::None,
      .space = false,
      .controller_only = true,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::SetSetpointSource,
      .token = "LSS",
      .arg = ArgKind::Letter,
      .space = true,
      .controller_only = true,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = true,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = "ASU"},
     {.id = Command::ReadRamp,
      .token = "SR",
      .arg = ArgKind::None,
      .space = false,
      .controller_only = true,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::SetRamp,
      .token = "SR",
      .arg = ArgKind::Ramp,
      .space = true,
      .controller_only = true,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = true,
      .guarded = false,
      .lo = 1,
      .hi = kMilliCap,
      .letters = ""},
     {.id = Command::DisableRamp,
      .token = "SR",
      .arg = ArgKind::Int,
      .space = true,
      .controller_only = true,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = true,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::ReadWatchdog,
      .token = "WD",
      .arg = ArgKind::None,
      .space = false,
      .controller_only = true,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::SetWatchdog,
      .token = "WD",
      .arg = ArgKind::Int,
      .space = true,
      .controller_only = true,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = true,
      .guarded = false,
      .lo = 0,
      .hi = 5000,
      .letters = ""},
     {.id = Command::HoldValve,
      .token = "HPUR",
      .arg = ArgKind::Milli,
      .space = true,
      .controller_only = true,
      .min_firmware = 0,
      .reply_max = 96,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 100000,
      .letters = ""},
     {.id = Command::ResumeControl,
      .token = "C",
      .arg = ArgKind::None,
      .space = false,
      .controller_only = true,
      .min_firmware = 0,
      .reply_max = 96,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::ReadGains,
      .token = "LCG",
      .arg = ArgKind::None,
      .space = false,
      .controller_only = true,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::SetGains,
      .token = "LCG",
      .arg = ArgKind::IntPair,
      .space = true,
      .controller_only = true,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = true,
      .guarded = false,
      .lo = 0,
      .hi = 65535,
      .letters = ""},
     {.id = Command::ReadReferenceTemperature,
      .token = "RT",
      .arg = ArgKind::None,
      .space = false,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::SetReferenceTemperature,
      .token = "RT",
      .arg = ArgKind::Milli,
      .space = true,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = true,
      .guarded = false,
      .lo = 0,
      .hi = 30000,
      .letters = ""},
     {.id = Command::ReadAveraging,
      .token = "DCA",
      .arg = ArgKind::None,
      .space = false,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::SetAveraging,
      .token = "DCA",
      .arg = ArgKind::Int,
      .space = true,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 2500,
      .letters = ""},
     {.id = Command::ReadTrigger,
      .token = "MT",
      .arg = ArgKind::None,
      .space = false,
      .controller_only = true,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::SetTrigger,
      .token = "MT",
      .arg = ArgKind::Int,
      .space = true,
      .controller_only = true,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 7,
      .letters = ""},
     {.id = Command::StartMeasurement,
      .token = "DVAS",
      .arg = ArgKind::Int,
      .space = true,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 1,
      .hi = kMeasureMaxMs,
      .letters = ""},
     {.id = Command::ReadAverages,
      .token = "DVAA",
      .arg = ArgKind::None,
      .space = false,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::ReadRanges,
      .token = "DVAR",
      .arg = ArgKind::None,
      .space = false,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 96,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::Tare,
      .token = "V",
      .arg = ArgKind::Int,
      .space = true,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 1,
      .hi = 32767,
      .letters = ""},
     {.id = Command::ReadAutotare,
      .token = "ZCA",
      .arg = ArgKind::None,
      .space = false,
      .controller_only = true,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::SetAutotare,
      .token = "ZCA",
      .arg = ArgKind::Int,
      .space = true,
      .controller_only = true,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 1,
      .letters = ""},
     {.id = Command::ReadGas,
      .token = "GS",
      .arg = ArgKind::None,
      .space = false,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::ReadGasList,
      .token = "GS",
      .arg = ArgKind::Star,
      .space = true,
      .controller_only = false,
      .min_firmware = reg::kFw305,
      .reply_max = 256,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::SetGas,
      .token = "GS",
      .arg = ArgKind::Int,
      .space = true,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 255,
      .letters = ""},
     {.id = Command::ResetTotal,
      .token = "T",
      .arg = ArgKind::None,
      .space = false,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 96,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::SetBatch,
      .token = "TB",
      .arg = ArgKind::Milli,
      .space = true,
      .controller_only = true,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 1,
      .hi = kMilliCap,
      .letters = ""},
     {.id = Command::DisableBatch,
      .token = "TB",
      .arg = ArgKind::Int,
      .space = true,
      .controller_only = true,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::ReadTotalizerLimit,
      .token = "TC",
      .arg = ArgKind::None,
      .space = false,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::SetTotalizerLimit,
      .token = "TC",
      .arg = ArgKind::Int,
      .space = true,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = true,
      .guarded = false,
      .lo = 0,
      .hi = 3,
      .letters = ""},
     {.id = Command::ReadFullScale,
      .token = "FPF",
      .arg = ArgKind::Int,
      .space = true,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 2,
      .letters = ""},
     {.id = Command::ChangeUnitId,
      .token = "@=",
      .arg = ArgKind::Letter,
      .space = false,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 96,
      .slow = true,
      .guarded = true,
      .lo = 0,
      .hi = 0,
      .letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"},
     {.id = Command::ReadSerialNumber,
      .token = "SN",
      .arg = ArgKind::None,
      .space = false,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::ReadFirmware,
      .token = "VE",
      .arg = ArgKind::None,
      .space = false,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::ReadModbusAddress,
      .token = "MA",
      .arg = ArgKind::None,
      .space = false,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::SetModbusAddress,
      .token = "MA",
      .arg = ArgKind::Int,
      .space = true,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = true,
      .guarded = true,
      .lo = 1,
      .hi = 247,
      .letters = ""},
     {.id = Command::ReadBaud,
      .token = "NCB",
      .arg = ArgKind::None,
      .space = false,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::ChangeBaud,
      .token = "NCB",
      .arg = ArgKind::Int,
      .space = true,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = true,
      .guarded = true,
      .lo = 4800,
      .hi = 115200,
      .letters = ""},
     {.id = Command::ReadProtocol,
      .token = "P",
      .arg = ArgKind::None,
      .space = false,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = false,
      .guarded = false,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::SwitchToProtocol1,
      .token = "P",
      .arg = ArgKind::Int,
      .space = true,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = true,
      .guarded = true,
      .lo = 1,
      .hi = 1,
      .letters = ""},
     {.id = Command::SwitchToProtocol2,
      .token = "P2",
      .arg = ArgKind::None,
      .space = false,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = true,
      .guarded = true,
      .lo = 0,
      .hi = 0,
      .letters = ""},
     {.id = Command::FactoryRestore,
      .token = "FACTORY RESTORE",
      .arg = ArgKind::None,
      .space = false,
      .controller_only = false,
      .min_firmware = 0,
      .reply_max = 64,
      .slow = true,
      .guarded = true,
      .lo = 0,
      .hi = 0,
      .letters = ""}}};

/**
 * @brief The row of a command.
 * @param c An ASCII command (not `Command::Modbus`).
 * @return The row; the first row for `Command::Modbus`.
 */
[[nodiscard]] constexpr const CommandSpec& Spec(Command c) noexcept {
  const auto i = static_cast<std::size_t>(c);
  return kCommands[(i < kCommandRows) ? i : 0U];
}

/** @brief The arguments of a request before encoding. */
struct Args {
  int64_t a{0};    ///< First number: an integer, or thousandths for a decimal argument.
  int64_t b{0};    ///< Second number (`Ramp` time unit, `IntPair` second value).
  char c{'\0'};    ///< Character argument.
  uint8_t dec{3};  ///< Digits printed after the point of a decimal argument (0 to 6).
  bool trim{true}; ///< Drop trailing zeros of the fraction.
};

namespace detail {

/// Output cursor over a fixed buffer; `ok` turns false on overflow.
struct Writer {
  std::array<uint8_t, kMaxRequest>& buf; ///< Target.
  std::size_t pos{0};                    ///< Next index.
  bool ok{true};                         ///< No overflow so far.
  /// Append one character.
  void Put(char ch) noexcept {
    if (pos < buf.size()) {
      buf[pos] = ::alicat_basis2::detail::ByteOf(ch);
      ++pos;
    } else {
      ok = false;
    }
  }
  /// Append a NUL-terminated string.
  void PutText(const char* s) noexcept {
    for (std::size_t i = 0U; !::alicat_basis2::detail::SameChar(s[i], '\0'); ++i) {
      Put(s[i]);
    }
  }
};

/// 10 to the power `n` (n <= 18).
[[nodiscard]] constexpr int64_t Pow10(uint32_t n) noexcept {
  int64_t r = 1;
  for (uint32_t i = 0U; i < n; ++i) {
    r *= 10;
  }
  return r;
}

/// Print a non-negative integer.
inline void PutUnsigned(Writer& w, uint64_t v) noexcept {
  std::array<char, 20> tmp{};
  std::size_t n = 0U;
  do {
    tmp[n] = ::alicat_basis2::detail::DigitChar(v);
    v /= 10U;
    ++n;
  } while ((v != 0U) && (n < tmp.size()));
  while (n > 0U) {
    --n;
    w.Put(tmp[n]);
  }
}

/// Thousandths rescaled to `dec` digits, rounded half away from zero.
[[nodiscard]] constexpr int64_t Rescale(int64_t milli, uint32_t dec) noexcept {
  if (dec >= 3U) {
    return milli * Pow10(dec - 3U);
  }
  const int64_t div = Pow10(3U - dec);
  const int64_t half = div / 2;
  return (milli >= 0) ? ((milli + half) / div) : -((-milli + half) / div);
}

/// The thousandths that `dec` digits after the point put on the wire: `milli` rounded half away
/// from zero to a multiple of 10^(3 - dec) when `dec` is below 3, else unchanged.
[[nodiscard]] constexpr int64_t WireMilli(int64_t milli, uint32_t dec) noexcept {
  return (dec >= 3U) ? milli : (Rescale(milli, dec) * Pow10(3U - dec));
}

/// Print thousandths with `dec` digits after the point, optionally trimming trailing zeros.
inline void PutMilli(Writer& w, int64_t milli, uint32_t dec, bool trim) noexcept {
  const int64_t v = Rescale(milli, dec);
  const bool neg = v < 0;
  const auto mag = static_cast<uint64_t>(neg ? -v : v);
  const auto scale = static_cast<uint64_t>(Pow10(dec));
  if (neg && (mag != 0U)) {
    w.Put('-');
  }
  PutUnsigned(w, mag / scale);
  uint64_t frac = mag % scale;
  uint32_t digits = dec;
  if (trim) {
    while ((digits > 0U) && ((frac % 10U) == 0U)) {
      frac /= 10U;
      --digits;
    }
  }
  if (digits > 0U) {
    w.Put('.');
    for (uint32_t i = digits; i > 0U; --i) {
      w.Put(::alicat_basis2::detail::DigitChar(frac / static_cast<uint64_t>(Pow10(i - 1U))));
    }
  }
}

/// Whether a command only reads: such a request may be repeated after a failed transaction.
[[nodiscard]] constexpr bool IsRead(Command c) noexcept {
  constexpr std::array<Command, 21> kReads{Command::Poll,
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
  return std::ranges::any_of(kReads, [c](Command r) { return r == c; });
}

/// The six baud rates of the instrument.
[[nodiscard]] constexpr bool BaudValid(int64_t b) noexcept {
  return (b == 4800) || (b == 9600) || (b == 19200) || (b == 38400) || (b == 57600) ||
         (b == 115200);
}

/// Whether `ch` is in the NUL-terminated set.
[[nodiscard]] constexpr bool InSet(char ch, const char* set) noexcept {
  for (std::size_t i = 0U; !::alicat_basis2::detail::SameChar(set[i], '\0'); ++i) {
    if (::alicat_basis2::detail::SameChar(set[i], ch)) {
      return true;
    }
  }
  return false;
}

/// Argument check against the row; one place for every bound of section 1.6.
[[nodiscard]] constexpr bool ArgsValid(const CommandSpec& s, const Args& a) noexcept {
  const bool in_range = (a.a >= s.lo) && (a.a <= s.hi);
  switch (s.arg) {
  case ArgKind::None:
  case ArgKind::Star:
    return true;
  case ArgKind::Int:
    return in_range && ((s.id != Command::ChangeBaud) || BaudValid(a.a));
  case ArgKind::Milli:
    return in_range && (a.dec <= 6U);
  case ArgKind::Letter:
    return (!::alicat_basis2::detail::SameChar(a.c, '\0')) && InSet(a.c, s.letters);
  case ArgKind::Ramp:
    return in_range && (a.dec <= 6U) && (a.b >= 3) && (a.b <= 5);
  case ArgKind::IntPair:
    return in_range && (a.b >= s.lo) && (a.b <= s.hi);
  default:
    break;
  }
  return false;
}

/// Write the argument part of a row.
inline void PutArgs(Writer& w, const CommandSpec& s, const Args& a) noexcept {
  switch (s.arg) {
  case ArgKind::Int:
    PutUnsigned(w, static_cast<uint64_t>(a.a));
    break;
  case ArgKind::Milli:
    PutMilli(w, a.a, a.dec, a.trim);
    break;
  case ArgKind::Letter:
    w.Put(a.c);
    break;
  case ArgKind::Star:
    w.Put('*');
    break;
  case ArgKind::Ramp:
    PutMilli(w, a.a, a.dec, a.trim);
    w.Put(' ');
    PutUnsigned(w, static_cast<uint64_t>(a.b));
    break;
  case ArgKind::IntPair:
    PutUnsigned(w, static_cast<uint64_t>(a.a));
    w.Put(' ');
    PutUnsigned(w, static_cast<uint64_t>(a.b));
    break;
  case ArgKind::None:
    break;
  default:
    break;
  }
}

} // namespace detail

/**
 * @brief Build a request: validate every argument, write the wire bytes.
 *
 * Guarded rows are built only when `allow_guarded` is true; the public guarded calls of `Basis2`
 * pass it after checking their token. A rejected argument returns `InvalidArgument` and nothing is
 * formed.
 *
 * @param command An ASCII command.
 * @param id The unit ID, `A` to `Z`.
 * @param args The arguments.
 * @param allow_guarded Whether a guarded row may be built.
 * @return The request, or `InvalidArgument` (also for `Command::Modbus`).
 */
[[nodiscard]] inline Result<Request> Make(Command command, char id, const Args& args,
                                          bool allow_guarded) noexcept {
  if (static_cast<std::size_t>(command) >= kCommandRows) {
    return Result<Request>::Failure(Error::InvalidArgument);
  }
  const CommandSpec& s = Spec(command);
  if (!::alicat_basis2::detail::IsUpper(id) || (s.guarded && !allow_guarded) ||
      !detail::ArgsValid(s, args)) {
    return Result<Request>::Failure(Error::InvalidArgument);
  }
  ::alicat_basis2::detail::RequestData d{};
  detail::Writer w{.buf = d.bytes};
  w.Put(id);
  w.PutText(s.token);
  if (s.arg != ArgKind::None) {
    if (s.space) {
      w.Put(' ');
    }
    detail::PutArgs(w, s, args);
  }
  w.Put(kCr);
  if (!w.ok) {
    return Result<Request>::Failure(Error::InvalidArgument);
  }
  d.length = static_cast<uint8_t>(w.pos);
  d.command = command;
  d.id = id;
  d.alt_id = (command == Command::ChangeUnitId) ? args.c : '\0';
  d.slow = s.slow;
  d.guarded = s.guarded;
  d.idempotent = detail::IsRead(command);
  d.reply_chars = s.reply_max;
  return Result<Request>::Success(::alicat_basis2::detail::RequestBuilder::Build(d));
}

// ---------------------------------------------------------------------------------------------------------------
// Independent wire validator (hand-written, not generated from the table): accepts exactly the
// grammar of M pp.10-17
// ---------------------------------------------------------------------------------------------------------------

namespace detail::legal {

/// A read-only run of characters.
using View = std::span<const char>;

/// Whether `v` equals the NUL-terminated `s`.
[[nodiscard]] constexpr bool Eq(View v, const char* s) noexcept {
  std::size_t i = 0U;
  while ((!::alicat_basis2::detail::SameChar(s[i], '\0')) && (i < v.size()) &&
         (::alicat_basis2::detail::SameChar(v[i], s[i]))) {
    ++i;
  }
  return (::alicat_basis2::detail::SameChar(s[i], '\0')) && (i == v.size());
}

/// Whether `v` starts with `p`; `rest` receives the remainder.
[[nodiscard]] constexpr bool After(View v, const char* p, View& rest) noexcept {
  std::size_t i = 0U;
  while ((!::alicat_basis2::detail::SameChar(p[i], '\0')) && (i < v.size()) &&
         (::alicat_basis2::detail::SameChar(v[i], p[i]))) {
    ++i;
  }
  if (!::alicat_basis2::detail::SameChar(p[i], '\0')) {
    return false;
  }
  rest = v.subspan(i);
  return true;
}

/// An unsigned integer without sign or leading zeros (except "0") in `lo` to `hi`.
[[nodiscard]] constexpr bool IntIn(View v, int64_t lo, int64_t hi) noexcept {
  if (v.empty() || (v.size() > 12U) ||
      ((::alicat_basis2::detail::SameChar(v[0], '0')) && (v.size() > 1U))) {
    return false;
  }
  int64_t n = 0;
  for (const char ch : v) {
    const int32_t digit = ::alicat_basis2::detail::DigitValue(ch);
    if (digit < 0) {
      return false;
    }
    n = (n * 10) + digit;
  }
  return (n >= lo) && (n <= hi);
}

/// A decimal `digits[.digits]` (at most 6 fraction digits, no trailing zero in the fraction), value
/// in thousandths within `lo` to `hi`; a fraction digit beyond the third counts as a fraction of a
/// thousandth.
[[nodiscard]] constexpr bool DecIn(View v, int64_t lo_milli, int64_t hi_milli) noexcept {
  std::size_t dot = v.size();
  for (std::size_t i = 0U; i < v.size(); ++i) {
    if (::alicat_basis2::detail::SameChar(v[i], '.')) {
      dot = i;
      break;
    }
  }
  const View ip = v.first(dot);
  if (!IntIn(ip, 0, 999'999'999'999LL)) {
    return false;
  }
  int64_t milli = 0;
  for (const char ch : ip) {
    milli = (milli * 10) + ::alicat_basis2::detail::DigitValue(ch);
  }
  milli *= 1000;
  if (dot < v.size()) {
    const View fr = v.subspan(dot + 1U);
    if (fr.empty() || (fr.size() > 6U)) {
      return false;
    }
    int64_t scale = 100;
    for (const char ch : fr) {
      const int32_t digit = ::alicat_basis2::detail::DigitValue(ch);
      if (digit < 0) {
        return false;
      }
      milli += (scale >= 1) ? (digit * scale) : 0;
      scale /= 10;
    }
    // Rounding of the sub-thousandth digits to the nearest thousandth may add one.
    if ((fr.size() > 3U) && (::alicat_basis2::detail::DigitValue(fr[3]) >= 5)) {
      milli += 1;
    }
  }
  return (milli >= lo_milli) && (milli <= hi_milli);
}

/// Commands without an argument.
[[nodiscard]] constexpr bool NoArg(View r) noexcept {
  constexpr std::array<const char*, 22> kWords{
      "",   "C",   "T",  "SN",  "VE", "MA",   "NCB",  "P",   "P2", "LSS", "SR",
      "WD", "LCG", "RT", "DCA", "MT", "DVAA", "DVAR", "ZCA", "GS", "TC",  "FACTORY RESTORE"};
  return std::ranges::any_of(kWords, [r](const char* w) { return Eq(r, w); });
}

/// An integer argument with its prefix and bounds.
struct IntForm {
  const char* prefix; ///< Text before the number.
  int64_t lo;         ///< Lowest value.
  int64_t hi;         ///< Highest value.
};

/// Commands with one integer argument.
[[nodiscard]] constexpr bool OneInt(View r) noexcept {
  constexpr std::array<IntForm, 14> kForms{{{.prefix = "DV ", .lo = 1, .hi = 255},
                                            {.prefix = "DVAS ", .lo = 1, .hi = 163'837},
                                            {.prefix = "WD ", .lo = 0, .hi = 5000},
                                            {.prefix = "DCA ", .lo = 0, .hi = 2500},
                                            {.prefix = "MT ", .lo = 0, .hi = 7},
                                            {.prefix = "V ", .lo = 1, .hi = 32767},
                                            {.prefix = "ZCA ", .lo = 0, .hi = 1},
                                            {.prefix = "GS ", .lo = 0, .hi = 255},
                                            {.prefix = "TC ", .lo = 0, .hi = 3},
                                            {.prefix = "FPF ", .lo = 0, .hi = 2},
                                            {.prefix = "MA ", .lo = 1, .hi = 247},
                                            {.prefix = "P ", .lo = 1, .hi = 1},
                                            {.prefix = "SR ", .lo = 0, .hi = 0},
                                            {.prefix = "TB ", .lo = 0, .hi = 0}}};
  View rest{};
  for (const IntForm& f : kForms) {
    if (After(r, f.prefix, rest) && IntIn(rest, f.lo, f.hi)) {
      return true;
    }
  }
  return false;
}

/// `S f`, `HPUR p`, `RT t` and `TB v`: a decimal in its range.
[[nodiscard]] constexpr bool DecimalArg(View r) noexcept {
  View rest{};
  if (After(r, "S ", rest)) {
    return DecIn(rest, 0, 999'999'999'999LL);
  }
  if (After(r, "HPUR ", rest)) {
    return DecIn(rest, 0, 100'000);
  }
  if (After(r, "RT ", rest)) {
    return DecIn(rest, 0, 30'000);
  }
  return After(r, "TB ", rest) && DecIn(rest, 1, 999'999'999'999LL);
}

/// `NCB b`: one of the six baud rates.
[[nodiscard]] constexpr bool BaudArg(View r) noexcept {
  View rest{};
  if (!After(r, "NCB ", rest)) {
    return false;
  }
  return Eq(rest, "4800") || Eq(rest, "9600") || Eq(rest, "19200") || Eq(rest, "38400") ||
         Eq(rest, "57600") || Eq(rest, "115200");
}

/// Commands with a decimal, a letter, a star or a baud rate.
[[nodiscard]] constexpr bool OtherArg(View r) noexcept {
  View rest{};
  if (DecimalArg(r) || BaudArg(r) || Eq(r, "GS *")) {
    return true;
  }
  if (After(r, "LSS ", rest)) {
    return (rest.size() == 1U) && ((::alicat_basis2::detail::SameChar(rest[0], 'A')) ||
                                   (::alicat_basis2::detail::SameChar(rest[0], 'S')) ||
                                   (::alicat_basis2::detail::SameChar(rest[0], 'U')));
  }
  return After(r, "@=", rest) && (rest.size() == 1U) && ::alicat_basis2::detail::IsUpper(rest[0]);
}

/// The characters a request may contain besides the unit ID: `A-Z 0-9 space + - . * @ =`.
[[nodiscard]] constexpr bool AllowedChar(char ch) noexcept {
  const bool alnum = ::alicat_basis2::detail::IsUpper(ch) || ::alicat_basis2::detail::IsDigit(ch);
  return alnum || (::alicat_basis2::detail::SameChar(ch, ' ')) ||
         (::alicat_basis2::detail::SameChar(ch, '+')) ||
         (::alicat_basis2::detail::SameChar(ch, '-')) ||
         (::alicat_basis2::detail::SameChar(ch, '.')) ||
         (::alicat_basis2::detail::SameChar(ch, '*')) ||
         (::alicat_basis2::detail::SameChar(ch, '@')) ||
         (::alicat_basis2::detail::SameChar(ch, '='));
}

/// `SR r t` and `LCG p i`.
[[nodiscard]] constexpr bool TwoArgs(View r) noexcept {
  View rest{};
  std::size_t sp = 0U;
  if (After(r, "SR ", rest)) {
    while ((sp < rest.size()) && (!::alicat_basis2::detail::SameChar(rest[sp], ' '))) {
      ++sp;
    }
    return (sp < rest.size()) && DecIn(rest.first(sp), 1, 999'999'999'999LL) &&
           IntIn(rest.subspan(sp + 1U), 3, 5);
  }
  if (After(r, "LCG ", rest)) {
    while ((sp < rest.size()) && (!::alicat_basis2::detail::SameChar(rest[sp], ' '))) {
      ++sp;
    }
    return (sp < rest.size()) && IntIn(rest.first(sp), 0, 65535) &&
           IntIn(rest.subspan(sp + 1U), 0, 65535);
  }
  return false;
}

} // namespace detail::legal

/**
 * @brief Whether `bytes` is exactly one request of the grammar of M pp.10 to 17: unit ID `A` to
 * `Z`, a command with its argument, one CR at the end, only the characters `A-Z 0-9 space + - . * @
 * =`.
 *
 * Hand-written and independent of `kCommands`: a mismatch between the table and this function is a
 * build failure of `tests/test_grammar.cpp`. The driver runs it before every `Write()`.
 *
 * @param bytes The candidate wire bytes.
 * @return true when it is a legal request.
 */
[[nodiscard]] inline bool IsLegalRequest(std::span<const uint8_t> bytes) noexcept {
  if ((bytes.size() < 2U) || (bytes.size() > kMaxRequest) ||
      (bytes.back() != ::alicat_basis2::detail::ByteOf(kCr))) {
    return false;
  }
  std::array<char, kMaxRequest> text{};
  const std::size_t n = bytes.size() - 1U;
  for (std::size_t i = 0U; i < n; ++i) {
    const char ch = ::alicat_basis2::detail::CharOf(bytes[i]);
    if (!detail::legal::AllowedChar(ch)) {
      return false;
    }
    text[i] = ch;
  }
  if (!::alicat_basis2::detail::IsUpper(text[0])) {
    return false;
  }
  const detail::legal::View rest{text.data() + 1, n - 1U};
  return detail::legal::NoArg(rest) || detail::legal::OneInt(rest) ||
         detail::legal::OtherArg(rest) || detail::legal::TwoArgs(rest);
}

// ---------------------------------------------------------------------------------------------------------------
// Reply parsing
// ---------------------------------------------------------------------------------------------------------------

/// Tokens in a line, at most.
inline constexpr std::size_t kMaxTokens = 26;
/// Longest token, in characters.
inline constexpr std::size_t kMaxToken = 32;

/** @brief One token: where it starts and how long it is. */
struct Token {
  uint8_t offset{0}; ///< Index of the first character.
  uint8_t length{0}; ///< Number of characters.
};

/** @brief The tokens of a line. */
struct Tokens {
  std::array<Token, kMaxTokens> items{}; ///< The tokens.
  uint8_t count{0};                      ///< Their number.
};

/**
 * @brief Split a line on runs of spaces.
 * @param line The line without its CR.
 * @param out The tokens.
 * @return `Ok`, or `BadReply` for a character outside 0x21 to 0x7E (other than the space), a token
 * over `kMaxToken` characters, more than `kMaxTokens` tokens, or an empty line.
 */
[[nodiscard]] inline Error Tokenize(std::span<const char> line, Tokens& out) noexcept {
  out = Tokens{};
  std::size_t i = 0U;
  while (i < line.size()) {
    const uint8_t ch = ::alicat_basis2::detail::ByteOf(line[i]);
    if (::alicat_basis2::detail::SameChar(line[i], ' ')) {
      ++i;
    } else if ((ch > 0x20U) && (ch < 0x7FU)) {
      const std::size_t start = i;
      while ((i < line.size()) && (!::alicat_basis2::detail::SameChar(line[i], ' '))) {
        const uint8_t c2 = ::alicat_basis2::detail::ByteOf(line[i]);
        if ((c2 <= 0x20U) || (c2 >= 0x7FU)) {
          return Error::BadReply;
        }
        ++i;
      }
      if (((i - start) > kMaxToken) || (out.count >= kMaxTokens)) {
        return Error::BadReply;
      }
      out.items[out.count] =
          Token{.offset = static_cast<uint8_t>(start), .length = static_cast<uint8_t>(i - start)};
      ++out.count;
    } else {
      return Error::BadReply;
    }
  }
  return (out.count > 0U) ? Error::Ok : Error::BadReply;
}

/**
 * @brief The characters of a token.
 * @param line The line.
 * @param t The token.
 * @return A view of the token's characters.
 */
[[nodiscard]] inline std::span<const char> View(std::span<const char> line,
                                                const Token& t) noexcept {
  return line.subspan(t.offset, t.length);
}

namespace detail {
/// Read up to `cap` decimal digits of `tok` from `i` into `m` (appended); return how many were
/// read.
inline std::size_t ScanDigits(std::span<const char> tok, std::size_t& i, int64_t& m,
                              std::size_t cap) noexcept {
  std::size_t n = 0U;
  while ((i < tok.size()) && (n < cap) && ::alicat_basis2::detail::IsDigit(tok[i])) {
    m = (m * 10) + ::alicat_basis2::detail::DigitValue(tok[i]);
    ++n;
    ++i;
  }
  return n;
}
} // namespace detail

/**
 * @brief Parse `[+-]? digit{1,12} ( '.' digit{1,6} )?`: no exponent, no hex, no `nan`, no leading
 * or trailing point.
 * @param tok The token's characters.
 * @param out The number, with the decimal count of the text preserved.
 * @return true when the whole token is a number.
 */
[[nodiscard]] inline bool ParseDecimal(std::span<const char> tok, Decimal& out) noexcept {
  std::size_t i = 0U;
  bool neg = false;
  if (!tok.empty() && ((::alicat_basis2::detail::SameChar(tok[0], '+')) ||
                       (::alicat_basis2::detail::SameChar(tok[0], '-')))) {
    neg = ::alicat_basis2::detail::SameChar(tok[0], '-');
    i = 1U;
  }
  int64_t m = 0;
  // At most 12 integer digits and 6 decimals are read, so the mantissa stays below 10^18 whatever
  // the input; a digit left over after the cap makes the token end early and the final check fails.
  if (detail::ScanDigits(tok, i, m, 12U) == 0U) {
    return false;
  }
  uint8_t dec = 0U;
  if ((i < tok.size()) && (::alicat_basis2::detail::SameChar(tok[i], '.'))) {
    ++i;
    dec = static_cast<uint8_t>(detail::ScanDigits(tok, i, m, 6U));
    if (dec == 0U) {
      return false;
    }
  }
  if (i != tok.size()) {
    return false;
  }
  out = Decimal{.mantissa = neg ? -m : m, .decimals = dec};
  return true;
}

/** @brief Upper-case a letter. @param c The character. @return `c`, upper-cased when it is a
 * letter. */
[[nodiscard]] constexpr char Upper(char c) noexcept {
  const int32_t place = ::alicat_basis2::detail::PlaceOf(::alicat_basis2::detail::kLowerChars, c);
  return (place < 0) ? c : ::alicat_basis2::detail::kUpperChars[static_cast<std::size_t>(place)];
}

/**
 * @brief Case-insensitive equality of a token and a word.
 * @param tok The token.
 * @param word NUL-terminated word.
 * @return true when equal.
 */
[[nodiscard]] inline bool EqualsNoCase(std::span<const char> tok, const char* word) noexcept {
  std::size_t i = 0U;
  while ((!::alicat_basis2::detail::SameChar(word[i], '\0')) && (i < tok.size()) &&
         (::alicat_basis2::detail::SameChar(Upper(tok[i]), Upper(word[i])))) {
    ++i;
  }
  return (::alicat_basis2::detail::SameChar(word[i], '\0')) && (i == tok.size());
}

/**
 * @brief Whether the first token is exactly one letter equal (any case) to the unit ID.
 * @param line The line.
 * @param toks Its tokens.
 * @param id The addressed ID, upper case.
 * @return true when it is.
 */
[[nodiscard]] inline bool StartsWithId(std::span<const char> line, const Tokens& toks,
                                       char id) noexcept {
  return (toks.count > 0U) && (toks.items[0].length == 1U) &&
         (::alicat_basis2::detail::SameChar(Upper(line[toks.items[0].offset]), id));
}

/** @brief The rules a decoded frame is checked against. */
struct FrameRules {
  char unit_id{'A'};                                  ///< Addressed ID.
  FrameLayout layout{FrameLayout::Auto};              ///< Layout accepted (learned or configured).
  UnknownFlagPolicy flags{UnknownFlagPolicy::Report}; ///< Unknown flag policy.
  bool full_scale_known{false};                       ///< `full_scale_milli` is valid.
  int64_t full_scale_milli{0};                        ///< Full scale flow in thousandths.
  uint16_t flow_margin_permille{1100};                ///< Flow plausibility.
  int16_t temperature_min_c{-50};                     ///< Temperature plausibility.
  int16_t temperature_max_c{150};                     ///< Temperature plausibility.
};

namespace detail {

/// Copy a token into `dst` (NUL terminated); `cap` includes the NUL.
inline void CopyToken(std::span<const char> tok, char* dst, std::size_t cap) noexcept {
  std::size_t n = (tok.size() < (cap - 1U)) ? tok.size() : (cap - 1U);
  for (std::size_t i = 0U; i < n; ++i) {
    dst[i] = tok[i];
  }
  dst[n] = '\0';
}

/// A gas token: a table name, or up to 8 characters of `[A-Za-z0-9]`.
[[nodiscard]] inline bool ParseGasName(std::span<const char> tok, Frame& f) noexcept {
  if (tok.empty() || (tok.size() > 8U)) {
    return false;
  }
  for (const char ch : tok) {
    if (!::alicat_basis2::detail::IsAlnum(ch)) {
      return false;
    }
  }
  CopyToken(tok, f.gas_name.data(), f.gas_name.size());
  f.gas_known = false;
  for (uint8_t n = 0U; n < 9U; ++n) {
    if (EqualsNoCase(tok, GasName(n))) {
      f.gas = GasId{n};
      f.gas_known = true;
    }
  }
  return true;
}

/// Set one flag by name; returns false for a duplicate. Unknown names are counted.
[[nodiscard]] inline bool SetFlag(std::span<const char> name, StatusFlags& s) noexcept {
  bool* target = nullptr;
  if (EqualsNoCase(name, "TOV")) {
    target = &s.temperature_over;
  } else if (EqualsNoCase(name, "MOV")) {
    target = &s.mass_flow_over;
  } else if (EqualsNoCase(name, "OVR")) {
    target = &s.totalizer_over;
  } else if (EqualsNoCase(name, "HLD")) {
    target = &s.valve_hold;
  } else if (EqualsNoCase(name, "VTM")) {
    target = &s.valve_thermal;
  } else {
    if (s.unknown_count < 255U) {
      ++s.unknown_count;
    }
    return true;
  }
  if (*target) {
    return false;
  }
  *target = true;
  return true;
}

/// A flag token: `NAME`, `[NAME]` or several `[NAME]` glued together.
[[nodiscard]] inline bool ParseFlagToken(std::span<const char> tok, StatusFlags& s) noexcept {
  if (tok.empty()) {
    return false;
  }
  if (!::alicat_basis2::detail::SameChar(tok[0], '[')) {
    return SetFlag(tok, s);
  }
  std::size_t i = 0U;
  while (i < tok.size()) {
    if (!::alicat_basis2::detail::SameChar(tok[i], '[')) {
      return false;
    }
    std::size_t j = i + 1U;
    while ((j < tok.size()) && (!::alicat_basis2::detail::SameChar(tok[j], ']'))) {
      ++j;
    }
    if ((j >= tok.size()) || (j == (i + 1U)) || !SetFlag(tok.subspan(i + 1U, j - i - 1U), s)) {
      return false;
    }
    i = j + 1U;
  }
  return true;
}

/// Whether the frame carries `item`.
[[nodiscard]] inline bool Carries(const Frame& f, DataItem item) noexcept {
  return (static_cast<uint32_t>(f.present) & static_cast<uint32_t>(item)) != 0U;
}

/// Temperature, flow and setpoint against the instrument's own limits.
[[nodiscard]] inline Error CheckMeasured(const Frame& f, const FrameRules& r) noexcept {
  if (Carries(f, DataItem::Temperature)) {
    const int64_t t_milli = f.temperature.ToMilli();
    if ((t_milli < (static_cast<int64_t>(r.temperature_min_c) * 1000)) ||
        (t_milli > (static_cast<int64_t>(r.temperature_max_c) * 1000))) {
      return Error::OutOfRange;
    }
  }
  if (r.full_scale_known && Carries(f, DataItem::Flow)) {
    const int64_t limit = (r.full_scale_milli * r.flow_margin_permille) / 1000;
    const int64_t flow = f.flow.ToMilli();
    if ((flow > limit) || (flow < -limit)) {
      return Error::OutOfRange;
    }
  }
  if (r.full_scale_known && Carries(f, DataItem::Setpoint)) {
    const int64_t sp = f.setpoint.ToMilli();
    if ((sp < 0) || (sp > ((r.full_scale_milli * 1025) / 1000))) {
      return Error::OutOfRange;
    }
  }
  return Error::Ok;
}

/// Plausibility of one decoded frame (M p.11, p.10); `OutOfRange` when a value cannot be physical.
[[nodiscard]] inline Error CheckFrame(const Frame& f, const FrameRules& r) noexcept {
  if (const Error e = CheckMeasured(f, r); e != Error::Ok) {
    return e;
  }
  if (Carries(f, DataItem::ValveDrive) &&
      ((f.valve_drive.ToMilli() < -500) || (f.valve_drive.ToMilli() > 100'500))) {
    return Error::OutOfRange;
  }
  if (Carries(f, DataItem::TotalVolume) && (f.total.mantissa < 0)) {
    return Error::OutOfRange;
  }
  if (Carries(f, DataItem::BatchRemaining) && (f.batch_remaining.mantissa < 0)) {
    return Error::OutOfRange;
  }
  return Error::Ok;
}

/// Parse tokens `first` to `first + n - 1` as numbers into `out`.
[[nodiscard]] inline bool ParseNumbers(std::span<const char> line, const Tokens& t,
                                       std::size_t first, std::size_t n, Decimal* out) noexcept {
  for (std::size_t k = 0U; k < n; ++k) {
    if (!ParseDecimal(View(line, t.items[first + k]), out[k])) {
      return false;
    }
  }
  return true;
}

/// Parse the flag tokens from `first` to the end; `Reject` policy turns an unknown flag into false.
[[nodiscard]] inline bool ParseFlags(std::span<const char> line, const Tokens& t, std::size_t first,
                                     UnknownFlagPolicy policy, StatusFlags& s) noexcept {
  for (std::size_t k = first; k < t.count; ++k) {
    if (!ParseFlagToken(View(line, t.items[k]), s)) {
      return false;
    }
  }
  return (policy == UnknownFlagPolicy::Report) || (s.unknown_count == 0U);
}

} // namespace detail

namespace detail {
/// Parse the numbers that follow the unit ID, at most five; return how many there are.
[[nodiscard]] inline std::size_t LeadingNumbers(std::span<const char> line, const Tokens& t,
                                                std::array<Decimal, 5>& v) noexcept {
  std::size_t numbers = 0U;
  while ((numbers < 5U) && ((numbers + 1U) < t.count) &&
         ParseDecimal(View(line, t.items[numbers + 1U]), v[numbers])) {
    ++numbers;
  }
  return numbers;
}
} // namespace detail

/**
 * @brief Decode a data frame (C01): ID, three or five numbers, the gas, zero or more flags (M p.10
 * to 11).
 * @param line The reply line without its CR.
 * @param rules The addressed ID, the layout and the plausibility limits.
 * @param out The frame; untouched on failure.
 * @param layout_seen The layout of this frame: `Meter` or `Controller`.
 * @return `Ok`, `DeviceIdMismatch`, `BadReply` or `OutOfRange`.
 */
[[nodiscard]] inline Error DecodeFrame(std::span<const char> line, const FrameRules& rules,
                                       Frame& out, FrameLayout& layout_seen) noexcept {
  Tokens t{};
  if (Tokenize(line, t) != Error::Ok) {
    return Error::BadReply;
  }
  if (!StartsWithId(line, t, rules.unit_id)) {
    return Error::DeviceIdMismatch;
  }
  std::array<Decimal, 5> v{};
  const std::size_t numbers = detail::LeadingNumbers(line, t, v);
  if (((numbers != 3U) && (numbers != 5U)) || ((numbers + 1U) >= t.count)) {
    return Error::BadReply;
  }
  const FrameLayout seen = (numbers == 3U) ? FrameLayout::Meter : FrameLayout::Controller;
  if ((rules.layout != FrameLayout::Auto) && (rules.layout != seen)) {
    return Error::BadReply;
  }
  Frame f{};
  f.unit_id = rules.unit_id;
  if (!detail::ParseGasName(View(line, t.items[numbers + 1U]), f) ||
      !detail::ParseFlags(line, t, numbers + 2U, rules.flags, f.status)) {
    return Error::BadReply;
  }
  f.temperature = v[0];
  f.flow = v[1];
  f.total = v[2];
  f.present = DataItem::Temperature | DataItem::Flow | DataItem::TotalVolume | DataItem::Gas |
              DataItem::Status;
  if (numbers == 5U) {
    f.setpoint = v[3];
    f.valve_drive = v[4];
    f.present = f.present | DataItem::Setpoint | DataItem::ValveDrive;
  }
  if (const Error e = detail::CheckFrame(f, rules); e != Error::Ok) {
    return e;
  }
  out = f;
  layout_seen = seen;
  return Error::Ok;
}

namespace detail {

/// The field of a `DV` bit, as a pointer into the frame (nullptr for gas and status).
[[nodiscard]] inline Decimal* DataField(Frame& f, uint32_t bit) noexcept {
  switch (bit) {
  case 1U:
    return &f.flow;
  case 2U:
    return &f.setpoint;
  case 4U:
    return &f.temperature;
  case 8U:
    return &f.valve_drive;
  case 32U:
    return &f.total;
  case 64U:
    return &f.batch_remaining;
  default:
    break;
  }
  return nullptr;
}

} // namespace detail

namespace detail {
/// The status at the end of a `DV` reply: one integer (A4) or the flag tokens.
[[nodiscard]] inline bool ParseStatusTail(std::span<const char> line, const Tokens& t,
                                          std::size_t k, const FrameRules& rules,
                                          Frame& f) noexcept {
  Decimal bits{};
  if (((k + 1U) == t.count) && ParseDecimal(View(line, t.items[k]), bits) &&
      (bits.decimals == 0U) && (bits.mantissa >= 0) && (bits.mantissa < 256)) {
    f.status = StatusFromBits(static_cast<uint32_t>(bits.mantissa));
    return true;
  }
  return ParseFlags(line, t, k, rules.flags, f.status);
}
} // namespace detail

/**
 * @brief Decode a `DV n` reply: ID, then the selected fields in table order (M p.11).
 *
 * Each numeric bit takes one token, the gas one token, and the status the rest: flag tokens, or one
 * unsigned integer equal to the Modbus register 2101 bits (A4).
 *
 * @param line The reply line.
 * @param mask The requested bits, 1 to 255.
 * @param rules The addressed ID and limits.
 * @param out The frame with `present == mask`; untouched on failure.
 * @return `Ok`, `DeviceIdMismatch`, `BadReply` or `OutOfRange`.
 */
[[nodiscard]] inline Error DecodeData(std::span<const char> line, uint8_t mask,
                                      const FrameRules& rules, Frame& out) noexcept {
  Tokens t{};
  if (Tokenize(line, t) != Error::Ok) {
    return Error::BadReply;
  }
  if (!StartsWithId(line, t, rules.unit_id)) {
    return Error::DeviceIdMismatch;
  }
  Frame f{};
  f.unit_id = rules.unit_id;
  f.present = mask;
  std::size_t k = 1U;
  for (uint32_t bit = 1U; bit <= 64U; bit <<= 1U) {
    if ((static_cast<uint32_t>(mask) & bit) == 0U) {
      continue;
    }
    if (k >= t.count) {
      return Error::BadReply;
    }
    Decimal* field = detail::DataField(f, bit);
    const bool ok = (field != nullptr) ? ParseDecimal(View(line, t.items[k]), *field)
                                       : detail::ParseGasName(View(line, t.items[k]), f);
    if (!ok) {
      return Error::BadReply;
    }
    ++k;
  }
  if ((static_cast<uint32_t>(mask) & static_cast<uint32_t>(DataItem::Status)) != 0U) {
    if (!detail::ParseStatusTail(line, t, k, rules, f)) {
      return Error::BadReply;
    }
  } else if (k != t.count) {
    return Error::BadReply;
  } else {
    // Every number is accounted for.
  }
  if (const Error e = detail::CheckFrame(f, rules); e != Error::Ok) {
    return e;
  }
  out = f;
  return Error::Ok;
}

/** @brief The numbers of a short reply: `ID v1 [v2 ...]`. */
struct Values {
  std::array<Decimal, 5> v{}; ///< The numbers.
  uint8_t count{0};           ///< How many.
};

/**
 * @brief Decode `ID` followed by exactly `count` numbers.
 * @param line The reply line.
 * @param id The addressed ID.
 * @param count The number of values, 1 to 5.
 * @param out The values; untouched on failure.
 * @return `Ok`, `DeviceIdMismatch` or `BadReply`.
 */
[[nodiscard]] inline Error DecodeValues(std::span<const char> line, char id, uint8_t count,
                                        Values& out) noexcept {
  Tokens t{};
  if (Tokenize(line, t) != Error::Ok) {
    return Error::BadReply;
  }
  if (!StartsWithId(line, t, id)) {
    return Error::DeviceIdMismatch;
  }
  if ((count < 1U) || (count > 5U) || (t.count != (count + 1U))) {
    return Error::BadReply;
  }
  Values vals{};
  if (!detail::ParseNumbers(line, t, 1U, count, vals.v.data())) {
    return Error::BadReply;
  }
  vals.count = count;
  out = vals;
  return Error::Ok;
}

/**
 * @brief Decode `ID x` where x is one character out of `allowed` (case-insensitive).
 * @param line The reply line.
 * @param id The addressed ID.
 * @param allowed Upper-case allowed characters.
 * @param out The character, upper case.
 * @return `Ok`, `DeviceIdMismatch` or `BadReply`.
 */
[[nodiscard]] inline Error DecodeLetter(std::span<const char> line, char id, const char* allowed,
                                        char& out) noexcept {
  Tokens t{};
  if (Tokenize(line, t) != Error::Ok) {
    return Error::BadReply;
  }
  if (!StartsWithId(line, t, id)) {
    return Error::DeviceIdMismatch;
  }
  if ((t.count != 2U) || (t.items[1].length != 1U) ||
      !detail::InSet(Upper(line[t.items[1].offset]), allowed)) {
    return Error::BadReply;
  }
  out = Upper(line[t.items[1].offset]);
  return Error::Ok;
}

/**
 * @brief Decode `ID text...`: the rest of the line after the ID, truncated to `kTextMax`
 * characters.
 * @param line The reply line.
 * @param id The addressed ID.
 * @param out The text.
 * @return `Ok`, `DeviceIdMismatch` or `BadReply` (no text).
 */
[[nodiscard]] inline Error DecodeText(std::span<const char> line, char id, Text& out) noexcept {
  Tokens t{};
  if (Tokenize(line, t) != Error::Ok) {
    return Error::BadReply;
  }
  if (!StartsWithId(line, t, id)) {
    return Error::DeviceIdMismatch;
  }
  if (t.count < 2U) {
    return Error::BadReply;
  }
  const std::size_t start = t.items[1].offset;
  Text x{};
  std::size_t n = line.size() - start;
  n = (n < kTextMax) ? n : kTextMax;
  while ((n > 0U) && (::alicat_basis2::detail::SameChar(line[start + n - 1U], ' '))) {
    --n;
  }
  detail::CopyToken(line.subspan(start, n), x.chars.data(), x.chars.size());
  x.length = static_cast<uint8_t>(n);
  out = x;
  return Error::Ok;
}

/**
 * @brief Decode a gas reply `ID number name` (C27, C29).
 * @param line The reply line.
 * @param id The addressed ID.
 * @param out The gas; `gas_name` holds the name.
 * @return `Ok`, `DeviceIdMismatch` or `BadReply`.
 */
[[nodiscard]] inline Error DecodeGas(std::span<const char> line, char id, Frame& out) noexcept {
  Tokens t{};
  if (Tokenize(line, t) != Error::Ok) {
    return Error::BadReply;
  }
  if (!StartsWithId(line, t, id)) {
    return Error::DeviceIdMismatch;
  }
  Decimal n{};
  Frame f{};
  if ((t.count != 3U) || !ParseDecimal(View(line, t.items[1]), n) || (n.decimals != 0U) ||
      (n.mantissa < 0) || (n.mantissa > 255) || !detail::ParseGasName(View(line, t.items[2]), f)) {
    return Error::BadReply;
  }
  f.gas = GasId{static_cast<uint8_t>(n.mantissa)};
  f.gas_known = true;
  f.unit_id = id;
  f.present = static_cast<uint8_t>(DataItem::Gas);
  out = f;
  return Error::Ok;
}

/**
 * @brief Decode a full-scale reply `ID value units` (C35).
 * @param line The reply line.
 * @param id The addressed ID.
 * @param out The value and unit text.
 * @return `Ok`, `DeviceIdMismatch` or `BadReply`.
 */
[[nodiscard]] inline Error DecodeFullScale(std::span<const char> line, char id,
                                           FullScale& out) noexcept {
  Tokens t{};
  if (Tokenize(line, t) != Error::Ok) {
    return Error::BadReply;
  }
  if (!StartsWithId(line, t, id)) {
    return Error::DeviceIdMismatch;
  }
  FullScale fs{};
  if ((t.count != 3U) || !ParseDecimal(View(line, t.items[1]), fs.value)) {
    return Error::BadReply;
  }
  detail::CopyToken(View(line, t.items[2]), fs.unit.chars.data(), fs.unit.chars.size());
  fs.unit.length = t.items[2].length;
  out = fs;
  return Error::Ok;
}

/**
 * @brief The flow unit of an instrument's label (case-insensitive), `Other` when it is not one of
 * the twenty.
 * @param label The label text.
 * @return The unit.
 */
[[nodiscard]] inline FlowUnit FlowUnitFromLabel(std::span<const char> label) noexcept {
  for (uint16_t i = 0U; i < 20U; ++i) {
    if (EqualsNoCase(label, reg::kFlowUnitLabels[i])) {
      return static_cast<FlowUnit>(i);
    }
  }
  return FlowUnit::Other;
}

/**
 * @brief Decode a gas list `ID n name n name ...` (C28, A5); sets `raw` when it does not parse.
 * @param line The reply line.
 * @param id The addressed ID.
 * @param out The list, or the raw text.
 * @return `Ok` (also for raw), `DeviceIdMismatch` or `BadReply` (not tokenizable).
 */
[[nodiscard]] inline Error DecodeGasList(std::span<const char> line, char id,
                                         GasList& out) noexcept {
  Tokens t{};
  if (Tokenize(line, t) != Error::Ok) {
    return Error::BadReply;
  }
  if (!StartsWithId(line, t, id)) {
    return Error::DeviceIdMismatch;
  }
  GasList g{};
  bool parsed = (t.count > 1U) && (((t.count - 1U) % 2U) == 0U) && (((t.count - 1U) / 2U) <= 12U);
  for (std::size_t k = 1U; parsed && ((k + 1U) < t.count); k += 2U) {
    Decimal n{};
    Frame f{};
    parsed = ParseDecimal(View(line, t.items[k]), n) && (n.decimals == 0U) && (n.mantissa >= 0) &&
             (n.mantissa <= 255) && detail::ParseGasName(View(line, t.items[k + 1U]), f);
    if (parsed) {
      g.numbers[g.count] = static_cast<uint8_t>(n.mantissa);
      ++g.count;
    }
  }
  if (!parsed) {
    g = GasList{};
    g.raw = true;
    const std::size_t n = (line.size() < kTextMax) ? line.size() : kTextMax;
    detail::CopyToken(line.first(n), g.text.chars.data(), g.text.chars.size());
    g.text.length = static_cast<uint8_t>(n);
  }
  out = g;
  return Error::Ok;
}

/**
 * @brief Decode `ID time ...` replies of `DVAA` (two values after the time) and `DVAR` (four).
 * @param line The reply line.
 * @param id The addressed ID.
 * @param count Values after the time: 2 or 4.
 * @param time_ms The time in ms.
 * @param out The values.
 * @return `Ok`, `DeviceIdMismatch` or `BadReply`.
 */
[[nodiscard]] inline Error DecodeWindow(std::span<const char> line, char id, uint8_t count,
                                        uint32_t& time_ms, Values& out) noexcept {
  Values all{};
  if (const Error e = DecodeValues(line, id, static_cast<uint8_t>(count + 1U), all);
      e != Error::Ok) {
    return e;
  }
  const Decimal& tm = all.v[0];
  if ((tm.decimals != 0U) || (tm.mantissa < 0) || (tm.mantissa > 0x7FFF'FFFFLL)) {
    return Error::BadReply;
  }
  time_ms = static_cast<uint32_t>(tm.mantissa);
  out = Values{};
  for (uint8_t i = 0U; i < count; ++i) {
    out.v[i] = all.v[i + 1U];
  }
  out.count = count;
  return Error::Ok;
}

/**
 * @brief Whether a reply line is the error token: `?` alone, or `?` after the unit ID.
 * @param line The line.
 * @param id The addressed ID.
 * @return true when it is.
 */
[[nodiscard]] inline bool IsErrorToken(std::span<const char> line, char id) noexcept {
  Tokens t{};
  if (Tokenize(line, t) != Error::Ok) {
    return false;
  }
  const bool alone = (t.count == 1U) && (t.items[0].length == 1U) &&
                     (::alicat_basis2::detail::SameChar(line[t.items[0].offset], '?'));
  const bool after_id = (t.count == 2U) && StartsWithId(line, t, id) && (t.items[1].length == 1U) &&
                        (::alicat_basis2::detail::SameChar(line[t.items[1].offset], '?'));
  return alone || after_id;
}

/** @} */

} // namespace alicat_basis2::ascii

#endif // ALICAT_BASIS2_ASCII_HPP
