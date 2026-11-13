/**
 * @file alicat_basis2_types.hpp
 * @brief Public types of the ALICAT_BASIS2 driver: errors, `Result<T>`, configuration, enumerations
 * and the values an instrument reports.
 * @copyright Copyright (c) 2026 The driver-alicat-basis2 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Everything here is plain data with no platform dependency. "M p.N" cites the printed page of the
 * operating manual DOC-MANUAL-BASIS2 Rev 4 (see `docs/datasheets.yml`). Quantities are `Decimal`:
 * the exact text the instrument printed, never rounded by the driver.
 */
#ifndef ALICAT_BASIS2_TYPES_HPP
#define ALICAT_BASIS2_TYPES_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "alicat_basis2_detail.hpp"
#include "alicat_basis2_version.h"

/** @brief The ALICAT_BASIS2 driver: everything it declares lives here; internals in `detail`. */
namespace alicat_basis2 {

/**
 * @defgroup alicat_basis2_types Driver types
 * @brief Errors, results, configuration and the values an instrument reports.
 * @{
 */

/**
 * @brief Why a call failed. Every call of the driver returns one of these in its `Result`.
 *
 * Values 0 to 9 are the shared base set; chip-specific values start at 16. `[[nodiscard]]` on the
 * type: a function that returns a bare `Error` cannot have it ignored either (KB-CPP20-037).
 */
enum class [[nodiscard]] Error : uint8_t {
  // Base set: the same names and values in every driver of the org (standards/driver.md).
  Ok = 0, ///< The call succeeded.
  NotInitialized =
      1, ///< `EnsureInitialized()` failed, or `Begin()` has not run where the call needs it.
  InvalidArgument = 2, ///< An argument or a `Config` field is out of range; nothing was sent.
  Bus = 3,             ///< `Write()` returned `false`; nothing is pending afterwards.
  Timeout = 4,         ///< No complete reply line (or frame) within the computed time.
  DeviceIdMismatch =
      5,   ///< The reply carried another unit ID or Modbus address than the one addressed.
  Crc = 6, ///< Modbus CRC-16 mismatch; the whole frame is discarded.
  Busy =
      7, ///< The bus was not free (a transaction, quarantine or hold-off), or the line never went
         ///< quiet: the transaction that met it was not sent (see `docs/api-guide.md`).
  DeviceFault = 8, ///< Reserved for the instrument's own fault reports.
  Unsupported =
      9, ///< The feature is absent in this mode, on this kind of instrument, or on this port.
  // Chip-specific values start at 16; each one is returned by some call (tests/test_errors.cpp).
  BadReply = 16, ///< The line does not match the command's reply grammar.
  Rejected = 17, ///< The instrument answered with the error token `?` or a Modbus exception reply.
  OutOfRange = 18,     ///< A decoded value is outside what the instrument can physically report.
  Overflow = 19,       ///< The reply exceeded the line buffer without a terminator.
  VerifyMismatch = 20, ///< The read-back after a setter differs from what was written.
  FirmwareTooOld = 21, ///< The call needs a newer firmware than the identity says.
  StateUnknown =
      22, ///< A guarded command may have changed the line settings and was not confirmed.
  RateLimited =
      23, ///< A saved-source setpoint change came sooner than the non-volatile memory allows.
  GuardNotConfirmed = 24, ///< A guarded call was made with a confirmation token of the wrong kind.
};

namespace detail {
/// The last enumerator of `Error` (values 0 to 9 and 16 to 24 are used; `IsEnumerator` is not
/// applicable).
inline constexpr uint8_t kErrorLast = 24;
} // namespace detail

/**
 * @brief The name of an error, for logs.
 * @param error The error.
 * @return A static string such as `"Timeout"`; `"Unknown"` for a value that is not an enumerator.
 */
constexpr const char* ToString(Error error) noexcept {
  switch (error) {
  case Error::Ok:
    return "Ok";
  case Error::NotInitialized:
    return "NotInitialized";
  case Error::InvalidArgument:
    return "InvalidArgument";
  case Error::Bus:
    return "Bus";
  case Error::Timeout:
    return "Timeout";
  case Error::DeviceIdMismatch:
    return "DeviceIdMismatch";
  case Error::Crc:
    return "Crc";
  case Error::Busy:
    return "Busy";
  case Error::DeviceFault:
    return "DeviceFault";
  case Error::Unsupported:
    return "Unsupported";
  case Error::BadReply:
    return "BadReply";
  case Error::Rejected:
    return "Rejected";
  case Error::OutOfRange:
    return "OutOfRange";
  case Error::Overflow:
    return "Overflow";
  case Error::VerifyMismatch:
    return "VerifyMismatch";
  case Error::FirmwareTooOld:
    return "FirmwareTooOld";
  case Error::StateUnknown:
    return "StateUnknown";
  case Error::RateLimited:
    return "RateLimited";
  case Error::GuardNotConfirmed:
    return "GuardNotConfirmed";
  default:
    break;
  }
  return "Unknown";
}

/** @brief Major version of the driver, from the generated `alicat_basis2_version.h`. @return The
 * number. */
[[nodiscard]] constexpr uint32_t GetDriverVersionMajor() noexcept {
  return static_cast<uint32_t>(ALICAT_BASIS2_VERSION_MAJOR);
}
/** @brief Minor version of the driver. @return The number. */
[[nodiscard]] constexpr uint32_t GetDriverVersionMinor() noexcept {
  return static_cast<uint32_t>(ALICAT_BASIS2_VERSION_MINOR);
}
/** @brief Patch version of the driver. @return The number. */
[[nodiscard]] constexpr uint32_t GetDriverVersionPatch() noexcept {
  return static_cast<uint32_t>(ALICAT_BASIS2_VERSION_PATCH);
}
/** @brief The version as text, such as `"0.1.0"`. @return A static string. */
[[nodiscard]] constexpr const char* GetDriverVersion() noexcept {
  return ALICAT_BASIS2_VERSION_STRING;
}

/**
 * @brief The value of a call, or why there is none.
 *
 * Check `ok()` (or the `bool` conversion) before reading `value`; on failure `value` is `T{}`.
 *
 * @tparam T The value type. `Result<void>` carries only the error.
 */
template <typename T>
struct [[nodiscard]] Result {
  T value{};              ///< The value; `T{}` when the call failed.
  Error error{Error::Ok}; ///< `Error::Ok` on success.

  /** @return true when the call succeeded. */
  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == Error::Ok;
  }
  /** @return true when the call succeeded. */
  constexpr explicit operator bool() const noexcept {
    return ok();
  }

  /**
   * @brief A successful result.
   * @param v The value.
   * @return `{v, Error::Ok}`.
   */
  static constexpr Result Success(T v) noexcept {
    return {v, Error::Ok};
  }
  /**
   * @brief A failed result.
   * @param e Why it failed; never `Error::Ok`.
   * @return `{T{}, e}`.
   */
  static constexpr Result Failure(Error e) noexcept {
    Result r{};
    r.error = e;
    return r;
  }
};

/** @brief The result of a call that returns no value. */
template <>
struct [[nodiscard]] Result<void> {
  Error error{Error::Ok}; ///< `Error::Ok` on success.

  /** @return true when the call succeeded. */
  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == Error::Ok;
  }
  /** @return true when the call succeeded. */
  constexpr explicit operator bool() const noexcept {
    return ok();
  }

  /** @return A successful result. */
  static constexpr Result Success() noexcept {
    return {Error::Ok};
  }
  /**
   * @brief A failed result.
   * @param e Why it failed; never `Error::Ok`.
   * @return `{e}`.
   */
  static constexpr Result Failure(Error e) noexcept {
    return {e};
  }
};

/** @brief Severity passed to the optional `Log()` hook. */
enum class LogLevel : uint8_t {
  Error = 0, ///< A transaction failed.
  Warning,   ///< Something unexpected that the driver recovered from.
  Info,      ///< Lifecycle events.
  Debug,     ///< Detail for bring-up.
};

/** @brief The command protocol one `Basis2` object speaks. */
enum class Protocol : uint8_t {
  Ascii = 0,     ///< "BASIS Protocol 2": ASCII lines, the default (M p.10 to 17).
  ModbusRtu = 1, ///< Modbus RTU, functions 3, 6 and 16 (M p.18 to 27).
};

/** @brief Whether the instrument is a meter (B series) or a controller (BC series). */
enum class DeviceKind : uint8_t {
  Auto = 0,       ///< Learn it at `Begin()` from the first data frame (five numbers: controller).
  Meter = 1,      ///< B series: no setpoint, no valve.
  Controller = 2, ///< BC series.
};

/** @brief Which data frame layout is accepted (assumption A2: a meter's frame is not shown in the
 * manual). */
enum class FrameLayout : uint8_t {
  Auto = 0,       ///< Learn from the first valid frame, reject a later frame of the other layout.
  Meter = 1,      ///< Three numbers, then the gas, then the flags.
  Controller = 2, ///< Five numbers, then the gas, then the flags.
};

/** @brief What happens with a flag token the manual does not list. */
enum class UnknownFlagPolicy : uint8_t {
  Report = 0, ///< Count it in `StatusFlags::unknown_count` and return the frame.
  Reject = 1, ///< `BadReply`; no value is exposed.
};

/** @brief Whether a half-duplex wiring that hears its own transmission is handled. */
enum class EchoMode : uint8_t {
  None = 0, ///< The reply is the first line.
  Skip = 1, ///< A first line equal to the command just sent is dropped once.
};

/** @brief Whether a setpoint is read back after it was set. */
enum class VerifyPolicy : uint8_t {
  Off = 0, ///< No read-back.
  WhenNoRamp =
      1, ///< Read it back unless a setpoint ramp is active (the instrument then lags on purpose).
};

/** @brief Whether the firmware gates of the register and command tables are enforced. */
enum class FirmwareGate : uint8_t {
  Enforce =
      0, ///< A call that needs a newer firmware than the identity says fails with `FirmwareTooOld`.
  Ignore = 1, ///< Never refuse on the firmware version.
};

/** @brief "Not set" marker of `Config::modbus_flow_decimals`: `Begin()` fails in Modbus mode while
 * it is. */
inline constexpr uint8_t kDecimalsUnset = 0xFF;

/// Largest line or frame the driver buffers: the gas list (`GS *`) reply can be this long.
inline constexpr std::size_t kMaxLine = 256;

/**
 * @brief The driver's configuration. Every default is the instrument's factory value or a
 * documented driver choice; fields that exist because the manual is silent are described in
 * `docs/configuration.md`.
 */
struct Config {
  /// ASCII unit ID, `A` to `Z` (M p.10).
  char unit_id{'A'};
  /// The protocol this object speaks.
  Protocol protocol{Protocol::Ascii};
  /// Modbus address 1 to 247.
  uint8_t modbus_address{1};
  /// What the port runs at; sizes every timeout.
  uint32_t baud{38400};
  /// Meter or controller; `Auto` learns it at `Begin()`.
  DeviceKind kind{DeviceKind::Auto};
  /// Instrument turnaround added to every reply wait.
  uint16_t processing_ms{50};
  /// Reply wait of commands that may write non-volatile memory.
  uint16_t slow_command_ms{1000};
  /// Quiet time after FACTORY RESTORE.
  uint16_t restore_holdoff_ms{3000};
  /// Retries of a read after a failed transaction; writes never repeat.
  uint8_t read_retries{1};
  /// Which data frame layout is accepted.
  FrameLayout frame_layout{FrameLayout::Auto};
  /// What happens with a status flag the manual does not list.
  UnknownFlagPolicy unknown_flags{UnknownFlagPolicy::Report};
  /// Whether a first line equal to the command is dropped (half-duplex wiring that hears itself).
  EchoMode echo{EchoMode::None};
  /// Plausibility: the flow must not exceed the full scale times this over 1000.
  uint16_t flow_margin_permille{1100};
  /// Plausibility lower limit of the temperature, C.
  int16_t temperature_min_c{-50};
  /// Plausibility upper limit of the temperature, C.
  int16_t temperature_max_c{150};
  /// Digits sent after the point in a setpoint.
  uint8_t setpoint_decimals{3};
  /// Whether a setpoint is read back.
  VerifyPolicy verify_setpoint{VerifyPolicy::WhenNoRamp};
  /// Read-back tolerance in permille of the full scale.
  uint16_t verify_tolerance_permille{5};
  /// Minimum time between setpoints while the source is saved.
  uint32_t saved_setpoint_min_interval_ms{180000};
  /// Whether the firmware gates of the register and command tables are enforced.
  FirmwareGate firmware_gate{FirmwareGate::Enforce};
  /// 0 unknown; else `256a + 16b + c`, used when the instrument gives no firmware.
  uint16_t assume_firmware{0};
  /// Required in Modbus mode: 0 to 6 (the decimals are not readable over Modbus).
  uint8_t modbus_flow_decimals{kDecimalsUnset};
  /// 0 learns the full scale at `Begin()`; else flow units times 1000.
  uint32_t full_scale_override_milli{0};
};

namespace detail {
/// Line and addressing fields of a `Config` are in range.
[[nodiscard]] constexpr bool LineFieldsOk(const Config& c) noexcept {
  const bool id_ok = detail::IsUpper(c.unit_id);
  const bool addr_ok = (c.modbus_address >= 1U) && (c.modbus_address <= 247U);
  const bool baud_ok = (c.baud == 4800U) || (c.baud == 9600U) || (c.baud == 19200U) ||
                       (c.baud == 38400U) || (c.baud == 57600U) || (c.baud == 115200U);
  return id_ok && addr_ok && baud_ok;
}

/// Every enumeration field of a `Config` holds an enumerator.
[[nodiscard]] constexpr bool EnumFieldsOk(const Config& c) noexcept {
  return (static_cast<uint8_t>(c.protocol) <= 1U) && (static_cast<uint8_t>(c.kind) <= 2U) &&
         (static_cast<uint8_t>(c.frame_layout) <= 2U) &&
         (static_cast<uint8_t>(c.unknown_flags) <= 1U) && (static_cast<uint8_t>(c.echo) <= 1U) &&
         (static_cast<uint8_t>(c.verify_setpoint) <= 1U) &&
         (static_cast<uint8_t>(c.firmware_gate) <= 1U);
}

/// Decimals, limits and times of a `Config` are in range.
[[nodiscard]] constexpr bool ValueFieldsOk(const Config& c) noexcept {
  const bool dec_ok =
      ((c.modbus_flow_decimals <= 6U) || (c.modbus_flow_decimals == kDecimalsUnset)) &&
      (c.setpoint_decimals <= 6U);
  const bool temp_ok = c.temperature_min_c < c.temperature_max_c;
  const bool time_ok = (c.processing_ms >= 1U) && (c.slow_command_ms >= c.processing_ms) &&
                       (c.restore_holdoff_ms >= 1U) && (c.flow_margin_permille >= 1000U);
  return dec_ok && temp_ok && time_ok;
}
} // namespace detail

/**
 * @brief Check a configuration against the documented ranges.
 * @param c The configuration.
 * @return `Ok` or `InvalidArgument`.
 */
[[nodiscard]] constexpr Error Validate(const Config& c) noexcept {
  return (detail::LineFieldsOk(c) && detail::EnumFieldsOk(c) && detail::ValueFieldsOk(c))
             ? Error::Ok
             : Error::InvalidArgument;
}

/**
 * @brief An exact decimal number as the instrument printed it: `+0021513.0` is `{215130, 1}`.
 *
 * `mantissa` stays below 10^15 and `decimals` below 7 for everything the driver parses.
 */
struct Decimal {
  int64_t mantissa{0}; ///< The number without its decimal point.
  uint8_t decimals{0}; ///< Digits after the point.

  /** @return The value as a double (division by a power of ten; exact for the digits the instrument
   * prints). */
  [[nodiscard]] constexpr double ToDouble() const noexcept {
    double scale = 1.0;
    for (uint8_t i = 0U; i < decimals; ++i) {
      scale *= 10.0;
    }
    return static_cast<double>(mantissa) / scale;
  }

  /** @return The value in thousandths, rounded half away from zero. */
  [[nodiscard]] constexpr int64_t ToMilli() const noexcept {
    int64_t m = mantissa;
    uint8_t d = decimals;
    while (d < 3U) {
      m *= 10;
      ++d;
    }
    int64_t div = 1;
    for (uint8_t i = 3U; i < d; ++i) {
      div *= 10;
    }
    if (div == 1) {
      return m;
    }
    const int64_t half = div / 2;
    return (m >= 0) ? ((m + half) / div) : -((-m + half) / div);
  }

  /** @brief Equal mantissa and equal decimal count (hidden friend, MISRA C++:2023 Rule 16.6.1). */
  friend constexpr bool operator==(const Decimal&, const Decimal&) = default;
};

/** @brief A gas number. Not a closed enumeration: the V3.0.5 gas table allows numbers up to 255
 * (A5). */
struct GasId {
  uint8_t number{0}; ///< The gas number (M p.15).
  /** @brief Hidden-friend equality. */
  friend constexpr bool operator==(const GasId&, const GasId&) = default;
};

/** @brief The nine gases of M p.15. */
namespace gas {
inline constexpr GasId kAir{0};           ///< Clean dry air.
inline constexpr GasId kArgon{1};         ///< Ar.
inline constexpr GasId kCarbonDioxide{2}; ///< CO2.
inline constexpr GasId kNitrogen{3};      ///< N2.
inline constexpr GasId kOxygen{4};        ///< O2.
inline constexpr GasId kNitrousOxide{5};  ///< N2O.
inline constexpr GasId kHydrogen{6};      ///< H2.
inline constexpr GasId kHelium{7};        ///< He.
inline constexpr GasId kMethane{8};       ///< CH4.
} // namespace gas

/**
 * @brief The name of a gas of the nine of M p.15.
 * @param number The gas number.
 * @return A static short name (`"N2"`), or `nullptr` for a number outside 0 to 8.
 */
[[nodiscard]] constexpr const char* GasName(uint8_t number) noexcept {
  switch (number) {
  case 0U:
    return "Air";
  case 1U:
    return "Ar";
  case 2U:
    return "CO2";
  case 3U:
    return "N2";
  case 4U:
    return "O2";
  case 5U:
    return "N2O";
  case 6U:
    return "H2";
  case 7U:
    return "He";
  case 8U:
    return "CH4";
  default:
    break;
  }
  return nullptr;
}

/** @brief Setpoint source (`LSS`, Modbus 516; M p.11 to 12 and p.19). */
enum class SetpointSource : uint8_t {
  Analog = 0, ///< `a`: the analog input; digital setpoint commands are not accepted.
  SavedDigital =
      1, ///< `s`: the last digital setpoint is used at power-up; wears non-volatile memory.
  UnsavedDigital = 2, ///< `u`: recommended for frequent changes.
};

/** @brief Time unit of the ramp command (`SR r t`, M p.12). */
enum class TimeUnit : uint8_t {
  Millisecond = 3, ///< `3`.
  Second = 4,      ///< `4`.
  Minute = 5,      ///< `5`.
};

/** @brief Totalizer limit behaviour (`TC`, Modbus 54; M p.16). */
enum class TotalizerLimit : uint8_t {
  HoldNoError = 0,    ///< Keep at the maximum, no OVR.
  ResetNoError = 1,   ///< Reset to zero, no OVR.
  HoldWithError = 2,  ///< Keep at the maximum, OVR.
  ResetWithError = 3, ///< Reset to zero, OVR.
};

/** @brief The item read by `FPF` (M p.16). */
enum class FullScaleItem : uint8_t {
  Flow = 0,           ///< Full scale flow.
  TotalVolume = 1,    ///< Largest total volume or batch.
  MaxTemperature = 2, ///< Maximum temperature.
};

/** @brief Items of `DV` (a bit mask, M p.11). */
enum class DataItem : uint8_t {
  Flow = 1,            ///< +1.
  Setpoint = 2,        ///< +2.
  Temperature = 4,     ///< +4.
  ValveDrive = 8,      ///< +8.
  Gas = 16,            ///< +16.
  TotalVolume = 32,    ///< +32.
  BatchRemaining = 64, ///< +64.
  Status = 128,        ///< +128.
};

/** @brief Union of `DataItem` bits. @param a First. @param b Second. @return `a | b` as a mask
 * value. */
[[nodiscard]] constexpr uint8_t operator|(DataItem a, DataItem b) noexcept {
  return static_cast<uint8_t>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
/** @brief Union of a mask and an item. @param a Mask. @param b Item. @return `a | b`. */
[[nodiscard]] constexpr uint8_t operator|(uint8_t a, DataItem b) noexcept {
  return static_cast<uint8_t>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

/** @brief Measurement trigger bits (`MT`, Modbus 4200; M p.13 to 14). */
enum class TriggerMode : uint8_t {
  OnSetpointChange = 1, ///< +1: a digital setpoint change starts a measurement.
  OnHoldChange = 2,     ///< +2: a hold-percent change while held.
  OnAverageRead = 4,    ///< +4: reading the average flow starts the next measurement.
};

/** @brief Serial speeds the instrument offers (`NCB`, Modbus 21; M p.17, p.25). */
enum class Baud : uint8_t {
  B4800 = 0,   ///< Modbus code 0.
  B9600 = 1,   ///< Code 1.
  B19200 = 2,  ///< Code 2.
  B38400 = 3,  ///< Code 3, the default.
  B57600 = 4,  ///< Code 4 (firmware 2.2.2).
  B115200 = 5, ///< Code 5 (firmware 2.2.2).
};

/** @brief Command protocol selector (`P`, Modbus 56). */
enum class CommandProtocol : uint8_t {
  BasisProtocol2 = 0, ///< The ASCII protocol this driver speaks.
  BasisProtocol1 = 1, ///< The older protocol; the driver does not speak it (A14).
};

/** @brief Flow units (Modbus register 49, M p.26; M p.31). */
enum class FlowUnit : uint8_t {
  Sccm = 0,
  Nccm = 1,
  Slpm = 2,
  Nlpm = 3,
  SmLs = 4,
  NmLs = 5,
  SmLm = 6,
  NmLm = 7,
  SLh = 8,
  NLh = 9,
  Sccs = 10,
  Nccs = 11,
  Sm3h = 12,
  Nm3h = 13,
  Sm3d = 14,
  Nm3d = 15,
  Scim = 16,
  Scfm = 17,
  Scfh = 18,
  Scfd = 19,
  Other = 20, ///< A label not in the table of M p.31; the raw text is kept.
};

/** @brief Status flags of a data frame or of Modbus register 2101 (M p.10, p.21). */
struct StatusFlags {
  bool mass_flow_over{false};   ///< MOV, Modbus +1.
  bool temperature_over{false}; ///< TOV, Modbus +2.
  bool totalizer_over{false};   ///< OVR, Modbus +4.
  bool valve_hold{false};    ///< HLD, Modbus +8: valve held by the user (an override, not a fault).
  bool valve_thermal{false}; ///< VTM, Modbus +16: thermal management (an override, not a fault).
  uint8_t unknown_count{0};  ///< Flag tokens or bits the manual does not list.

  /** @return true for MOV, TOV or OVR, the three conditions the manual calls errors (M p.5). */
  [[nodiscard]] constexpr bool AnyFault() const noexcept {
    return mass_flow_over || temperature_over || totalizer_over;
  }
  /** @brief Hidden-friend equality. */
  friend constexpr bool operator==(const StatusFlags&, const StatusFlags&) = default;
};

/**
 * @brief Decode status bits (Modbus register 2101 or an integer `DV` status): +1 MOV, +2 TOV, +4
 * OVR, +8 HLD, +16 VTM.
 * @param bits The bits; anything above bit 4 is counted as unknown.
 * @return The flags.
 */
[[nodiscard]] constexpr StatusFlags StatusFromBits(uint32_t bits) noexcept {
  StatusFlags s{};
  s.mass_flow_over = (bits & 1U) != 0U;
  s.temperature_over = (bits & 2U) != 0U;
  s.totalizer_over = (bits & 4U) != 0U;
  s.valve_hold = (bits & 8U) != 0U;
  s.valve_thermal = (bits & 16U) != 0U;
  s.unknown_count = ((bits & 0xFFFF'FFE0U) != 0U) ? 1U : 0U;
  return s;
}

/** @brief One data frame (`A`) or a `DV` selection; `present` says which fields were read. */
struct Frame {
  uint8_t present{0};        ///< `DataItem` bits whose fields are set.
  char unit_id{'\0'};        ///< The unit ID of the reply, upper case.
  Decimal flow{};            ///< In the configured flow unit.
  Decimal setpoint{};        ///< Same unit as the flow; controllers only.
  Decimal temperature{};     ///< C.
  Decimal valve_drive{};     ///< Percent of full drive; controllers only.
  Decimal total{};           ///< In the instrument's volume unit.
  Decimal batch_remaining{}; ///< Same unit as the total.
  GasId gas{};               ///< Valid when `gas_known`.
  bool gas_known{false}; ///< The gas token was one of the nine names (or a number in Modbus mode).
  std::array<char, 9> gas_name{}; ///< The gas token as printed (ASCII only), NUL terminated.
  StatusFlags status{};           ///< Flags.
};

/** @brief Maximum characters kept of a text reply. */
inline constexpr std::size_t kTextMax = 24;

/** @brief A short text value (serial number, firmware, unit label), NUL terminated. */
struct Text {
  std::array<char, kTextMax + 1U> chars{}; ///< The characters.
  uint8_t length{0};                       ///< Their number.
};

/** @brief Result of `ReadRamp()`: rate and time unit as the instrument reported them. */
struct Ramp {
  Decimal rate{};       ///< 0 means ramping is disabled.
  uint8_t time_unit{0}; ///< 3, 4 or 5 (`TimeUnit`); 0 in Modbus mode (the unit is fixed there).
  /** @brief Hidden-friend equality. */
  friend constexpr bool operator==(const Ramp&, const Ramp&) = default;
};

/** @brief Closed-loop gains (`LCG`, Modbus 519 and 520). */
struct Gains {
  uint16_t proportional{0}; ///< P gain.
  uint16_t integral{0};     ///< I gain.
  /** @brief Hidden-friend equality. */
  friend constexpr bool operator==(const Gains&, const Gains&) = default;
};

/** @brief Result of `ReadAverages()`: the current or most recent measurement window. */
struct Averages {
  uint32_t time_ms{0};   ///< ASCII: elapsed time; Modbus: samples x 2.5 ms.
  Decimal temperature{}; ///< Average temperature, C.
  Decimal flow{};        ///< Average flow.
  Decimal valve_drive{}; ///< Modbus only (register 4209): instantaneous valve drive.
};

/** @brief Result of `ReadRanges()`. */
struct Ranges {
  uint32_t time_ms{0};       ///< As `Averages::time_ms`.
  Decimal min_temperature{}; ///< C.
  Decimal max_temperature{}; ///< C.
  Decimal min_flow{};        ///< Flow units.
  Decimal max_flow{};        ///< Flow units.
};

/** @brief Result of `ReadPreviousMeasurement()` (Modbus only, registers 4210 to 4215). */
struct PreviousMeasurement {
  Decimal min_flow{};     ///< Register 4210.
  Decimal max_flow{};     ///< Register 4211.
  uint16_t samples{0};    ///< Register 4212.
  Decimal temperature{};  ///< Register 4213, C.
  Decimal average_flow{}; ///< Registers 4214 to 4215, signed 32-bit.
};

/** @brief One reading of `FPF`: a value and its unit text. */
struct FullScale {
  Decimal value{}; ///< The value.
  Text unit{};     ///< The unit label as printed.
};

/** @brief One entry of the gas table of firmware 3.0.5 (Modbus 81 to 140, M p.26 to 27). */
struct GasEntry {
  uint8_t number{0};           ///< Gas number 0 to 255.
  uint16_t k_factor{0};        ///< 4096 = 1.0.
  std::array<char, 7> label{}; ///< Up to six characters, NUL terminated.
};

/** @brief The whole gas table: twelve entries. */
struct GasTable {
  std::array<GasEntry, 12> entries{}; ///< The entries.
};

/** @brief Result of `ReadGasList()`: the parsed pairs, or the raw text when the grammar did not
 * match (A5). */
struct GasList {
  uint8_t count{0};                  ///< Parsed entries.
  std::array<uint8_t, 12> numbers{}; ///< Gas numbers.
  bool raw{false};                   ///< The text did not parse; `text` holds it.
  Text text{};                       ///< Up to 24 characters of the reply.
};

/** @brief What `Begin()` learned: read-only knowledge of the instrument. */
struct Identity {
  DeviceKind kind{DeviceKind::Auto};   ///< Meter or controller.
  bool firmware_known{false};          ///< `firmware` is set.
  uint16_t firmware{0};                ///< 256a + 16b + c.
  Text firmware_text{};                ///< As printed.
  Text serial{};                       ///< Serial number text.
  Decimal full_scale{};                ///< Full scale flow.
  bool full_scale_known{false};        ///< `full_scale` is set.
  FlowUnit flow_unit{FlowUnit::Other}; ///< The unit learned from the `FPF` text or register 49.
  Text unit_label{};                   ///< The label as printed.
  GasId gas{};                         ///< The gas at `Begin()`.
  bool gas_known{false};               ///< `gas` is set.
  SetpointSource source{SetpointSource::UnsavedDigital}; ///< Last known source.
  bool source_known{false};                              ///< `source` is set.
  bool ramp_active{false}; ///< A ramp is configured, so a setpoint read-back lags.
};

namespace detail {
/**
 * @brief The line settings an unconfirmed guarded command may have left the instrument with: the
 * ID, Modbus address or baud rate it was asked to take, or the factory settings after a restore.
 * A zero field means "no such candidate". A switch-off in `StateUnknown` tries each of them.
 */
struct Alternatives {
  char id{'\0'};       ///< The unit ID the instrument may answer to, or `'\0'`.
  uint8_t address{0};  ///< The Modbus address it may answer to, or 0.
  uint32_t baud{0};    ///< The baud rate it may run at, or 0.
  bool factory{false}; ///< It may be back at the factory settings (ID `A`, address 1, 38400).
};

/**
 * @brief What `Recover()` puts back when it fails: the driver's state and line settings before it
 * started scanning.
 */
struct RecoverHeld {
  bool abandoned{false};    ///< The protocol had been abandoned.
  bool unknown{false};      ///< `StateUnknown` held.
  bool ready{false};        ///< `Begin()` had succeeded.
  bool layout_known{false}; ///< The frame layout was learnt.
  Identity identity{};      ///< What `Begin()` had learnt.
  char unit_id{'A'};        ///< The unit ID used.
  uint8_t address{1};       ///< The Modbus address used.
  uint32_t baud{38400};     ///< The line speed used.
};
} // namespace detail

/** @brief Position of the unit ID in a `StatusFlags` text: helper used by tests. */
inline constexpr uint8_t kFlagCount = 5;

/** @} */

namespace detail {
/// A `DataItem` mask value is valid when it is 1 to 255.
[[nodiscard]] constexpr bool MaskValid(uint32_t mask) noexcept {
  return (mask >= 1U) && (mask <= 255U);
}
template <>
struct EnumLast<FlowUnit> {
  static constexpr FlowUnit kValue = FlowUnit::Other; ///< The last flow unit.
};
template <>
struct EnumLast<Baud> {
  static constexpr Baud kValue = Baud::B115200; ///< The last baud code.
};
template <>
struct EnumLast<TotalizerLimit> {
  static constexpr TotalizerLimit kValue = TotalizerLimit::ResetWithError; ///< The last limit mode.
};
template <>
struct EnumLast<SetpointSource> {
  static constexpr SetpointSource kValue = SetpointSource::UnsavedDigital; ///< The last source.
};
template <>
struct EnumLast<FullScaleItem> {
  static constexpr FullScaleItem kValue = FullScaleItem::MaxTemperature; ///< The last item.
};
} // namespace detail

} // namespace alicat_basis2

#endif // ALICAT_BASIS2_TYPES_HPP
