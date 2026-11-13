/**
 * @file alicat_basis2_request.hpp
 * @brief `Request` and `Reply`: the only things the driver puts on and takes off the wire, and the
 * timing formulas that size every wait.
 * @copyright Copyright (c) 2026 The driver-alicat-basis2 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * A `Request` can be made only by the factories of `alicat_basis2_ascii.hpp` and
 * `alicat_basis2_modbus.hpp`, which validate every argument; its constructor from data is private.
 * There is no way to send raw text.
 */
#ifndef ALICAT_BASIS2_REQUEST_HPP
#define ALICAT_BASIS2_REQUEST_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "alicat_basis2_types.hpp"

namespace alicat_basis2 {

/**
 * @defgroup alicat_basis2_request Requests and replies
 * @brief The command set and the timing formulas.
 * @{
 */

/** @brief The 46 ASCII commands of M pp.10 to 17 (rows C01 to C46 of the coverage map), plus the
 * Modbus kind. */
enum class Command : uint8_t {
  Poll = 0,                 ///< C01 `a`.
  ReadData,                 ///< C02 `aDV n`.
  SetSetpoint,              ///< C03 `aS f`.
  ReadSetpointSource,       ///< C04 `aLSS`.
  SetSetpointSource,        ///< C05 `aLSS x`.
  ReadRamp,                 ///< C06 `aSR`.
  SetRamp,                  ///< C07 `aSR r t`.
  DisableRamp,              ///< C08 `aSR 0`.
  ReadWatchdog,             ///< C09 `aWD`.
  SetWatchdog,              ///< C10 `aWD t`.
  HoldValve,                ///< C11 `aHPUR p`.
  ResumeControl,            ///< C12 `aC`.
  ReadGains,                ///< C13 `aLCG`.
  SetGains,                 ///< C14 `aLCG p i`.
  ReadReferenceTemperature, ///< C15 `aRT`.
  SetReferenceTemperature,  ///< C16 `aRT t`.
  ReadAveraging,            ///< C17 `aDCA`.
  SetAveraging,             ///< C18 `aDCA t`.
  ReadTrigger,              ///< C19 `aMT`.
  SetTrigger,               ///< C20 `aMT m`.
  StartMeasurement,         ///< C21 `aDVAS t`.
  ReadAverages,             ///< C22 `aDVAA`.
  ReadRanges,               ///< C23 `aDVAR`.
  Tare,                     ///< C24 `aV d`.
  ReadAutotare,             ///< C25 `aZCA`.
  SetAutotare,              ///< C26 `aZCA 1` or `aZCA 0`.
  ReadGas,                  ///< C27 `aGS`.
  ReadGasList,              ///< C28 `aGS *`.
  SetGas,                   ///< C29 `aGS n`.
  ResetTotal,               ///< C30 `aT`.
  SetBatch,                 ///< C31 `aTB v`.
  DisableBatch,             ///< C32 `aTB 0`.
  ReadTotalizerLimit,       ///< C33 `aTC`.
  SetTotalizerLimit,        ///< C34 `aTC m`.
  ReadFullScale,            ///< C35 `aFPF v`.
  ChangeUnitId,             ///< C36 `a@=b` (guarded).
  ReadSerialNumber,         ///< C37 `aSN`.
  ReadFirmware,             ///< C38 `aVE`.
  ReadModbusAddress,        ///< C39 `aMA`.
  SetModbusAddress,         ///< C40 `aMA n` (guarded).
  ReadBaud,                 ///< C41 `aNCB`.
  ChangeBaud,               ///< C42 `aNCB b` (guarded).
  ReadProtocol,             ///< C43 `aP`.
  SwitchToProtocol1,        ///< C44 `aP 1` (guarded).
  SwitchToProtocol2,        ///< C45 `aP2`.
  FactoryRestore,           ///< C46 `aFACTORY RESTORE` (guarded).
  Modbus,                   ///< A Modbus RTU frame (function 3, 6 or 16).
};

/// Longest request the driver builds, in bytes.
inline constexpr std::size_t kMaxRequest = 48;

/** @brief A reply as received: one ASCII line without its CR, or one Modbus frame (raw bytes as
 * chars). */
struct Reply {
  std::array<char, kMaxLine + 1U> data{}; ///< The characters or bytes.
  uint16_t length{0};                     ///< Their number.
};

namespace detail {
struct RequestBuilder;

/// The fields of a request; private to `Request`.
struct RequestData {
  std::array<uint8_t, kMaxRequest> bytes{}; ///< The wire bytes.
  uint8_t length{0};                        ///< Their number.
  Command command{Command::Poll};           ///< Which command.
  char id{'\0'};           ///< Addressed unit ID (ASCII) or the Modbus address (as a char).
  char alt_id{'\0'};       ///< A second unit ID the reply may carry (`@=`), else NUL.
  bool modbus{false};      ///< Binary frame.
  bool slow{false};        ///< May write non-volatile memory.
  bool guarded{false};     ///< One of the six guarded commands.
  bool idempotent{true};   ///< A read: may be repeated.
  uint16_t reply_chars{0}; ///< Most characters (or bytes) the reply holds.
  uint32_t extra_ms{0};    ///< Duration added to the reply wait (tare, measurement).
  uint32_t holdoff_ms{0};  ///< The bus stays closed this long after the send.
  uint8_t function{0};     ///< Modbus function code.
};
} // namespace detail

/** @brief One validated request. Only the factories of the grammar headers make one that can be
 * sent. */
class Request {
public:
  /** @brief An empty request: it has no bytes and `Submit()` refuses it. */
  constexpr Request() noexcept = default;

  /** @return The wire bytes. */
  [[nodiscard]] std::span<const uint8_t> Bytes() const noexcept {
    return std::span<const uint8_t>{d_.bytes.data(), d_.length};
  }
  /** @return The command. */
  [[nodiscard]] Command GetCommand() const noexcept {
    return d_.command;
  }
  /** @return The addressed unit ID, or the Modbus address as a char. */
  [[nodiscard]] char Id() const noexcept {
    return d_.id;
  }
  /** @return A second accepted reply ID (`@=` only), else NUL. */
  [[nodiscard]] char AltId() const noexcept {
    return d_.alt_id;
  }
  /** @return true for a Modbus frame. */
  [[nodiscard]] bool IsModbus() const noexcept {
    return d_.modbus;
  }
  /** @return true when the command may write non-volatile memory. */
  [[nodiscard]] bool IsSlow() const noexcept {
    return d_.slow;
  }
  /** @return true for the six guarded commands. */
  [[nodiscard]] bool IsGuarded() const noexcept {
    return d_.guarded;
  }
  /** @return true for a read that may be repeated. */
  [[nodiscard]] bool IsIdempotent() const noexcept {
    return d_.idempotent;
  }
  /** @return The most characters (bytes) the reply holds. */
  [[nodiscard]] uint16_t ReplyChars() const noexcept {
    return d_.reply_chars;
  }
  /** @return Duration added to the reply wait, in milliseconds. */
  [[nodiscard]] uint32_t ExtraMs() const noexcept {
    return d_.extra_ms;
  }
  /** @return Milliseconds the bus stays closed after the send. */
  [[nodiscard]] uint32_t HoldoffMs() const noexcept {
    return d_.holdoff_ms;
  }
  /** @return The Modbus function code, 0 for ASCII. */
  [[nodiscard]] uint8_t Function() const noexcept {
    return d_.function;
  }

  /**
   * @brief Set the durations that depend on the configuration.
   * @param extra_ms Added to the reply wait.
   * @param holdoff_ms The bus stays closed this long after the send. `Bus::Submit()` refuses a
   * request with either above 600000 ms (`InvalidArgument`).
   */
  void SetTiming(uint32_t extra_ms, uint32_t holdoff_ms) noexcept {
    d_.extra_ms = extra_ms;
    d_.holdoff_ms = holdoff_ms;
  }

private:
  friend struct detail::RequestBuilder;
  explicit Request(const detail::RequestData& d) noexcept : d_(d) {}
  detail::RequestData d_{};
};

namespace detail {
/// The only code that can construct a sendable `Request`; used by the two grammar headers.
struct RequestBuilder {
  /// @param d The validated fields. @return The request.
  static Request Build(const RequestData& d) noexcept {
    return Request{d};
  }
};

/// Ceiling of `a / b` for unsigned values.
[[nodiscard]] constexpr uint32_t CeilDiv(uint64_t a, uint64_t b) noexcept {
  return static_cast<uint32_t>((a + b - 1U) / b);
}
/// t3.5 of an ASCII line (10 bits per character), in milliseconds, at least 1 (M p.10).
[[nodiscard]] constexpr uint32_t T35Ms(uint32_t baud) noexcept {
  const uint32_t ms = CeilDiv(35ULL * 1000ULL, baud);
  return (ms < 1U) ? 1U : ms;
}
/// Transmission time of `chars` ASCII characters, rounded up, in milliseconds.
[[nodiscard]] constexpr uint32_t LineMs(uint32_t chars, uint32_t baud) noexcept {
  return CeilDiv(static_cast<uint64_t>(chars) * 10ULL * 1000ULL, baud);
}
/// Modbus inter-frame gap: 3.5 characters of 11 bits up to 19200 baud, 1.75 ms above (rounded up to
/// 2 ms).
[[nodiscard]] constexpr uint32_t ModbusGapMs(uint32_t baud) noexcept {
  return (baud <= 19200U) ? CeilDiv(385ULL * 1000ULL, 10ULL * baud) : 2U;
}
/// Transmission time of `bytes` Modbus bytes (11 bits each), rounded up, in milliseconds.
[[nodiscard]] constexpr uint32_t ModbusFrameMs(uint32_t bytes, uint32_t baud) noexcept {
  return CeilDiv(static_cast<uint64_t>(bytes) * 11ULL * 1000ULL, baud);
}
} // namespace detail

/** @} */

} // namespace alicat_basis2

#endif // ALICAT_BASIS2_REQUEST_HPP
