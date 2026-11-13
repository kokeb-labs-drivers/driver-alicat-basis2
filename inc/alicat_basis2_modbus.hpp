/**
 * @file alicat_basis2_modbus.hpp
 * @brief The Modbus RTU codec (M pp.18 to 27): CRC-16, request builders, response parser and the
 * second wire validator.
 * @copyright Copyright (c) 2026 The driver-alicat-basis2 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Only functions 3 (read holding registers), 6 (write single) and 16 (write multiple) are ever
 * built, only on the registers of `reg::kRegisters`, only with the documented value ranges and
 * magic numbers, and never to address 0 (broadcast, A13) or above 247. Registers are big-endian;
 * the CRC is sent low byte first.
 */
#ifndef ALICAT_BASIS2_MODBUS_HPP
#define ALICAT_BASIS2_MODBUS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "alicat_basis2_registers.hpp"
#include "alicat_basis2_request.hpp"
#include "alicat_basis2_types.hpp"

/** @brief The Modbus RTU codec. */
namespace alicat_basis2::modbus {

/**
 * @defgroup alicat_basis2_modbus Modbus RTU codec
 * @brief CRC-16, builders, parser, validator.
 * @{
 */

/** @brief Function code: read holding registers. */
inline constexpr uint8_t kReadHolding = 3;
/** @brief Function code: write single register. */
inline constexpr uint8_t kWriteSingle = 6;
/** @brief Function code: write multiple registers. */
inline constexpr uint8_t kWriteMultiple = 16;

/**
 * @brief Modbus CRC-16 (polynomial 0xA001 reflected, initial value 0xFFFF).
 * @param data The bytes.
 * @return The CRC; it is sent low byte first.
 */
[[nodiscard]] constexpr uint16_t Crc16(std::span<const uint8_t> data) noexcept {
  uint32_t crc = 0xFFFFU;
  for (const uint8_t byte : data) {
    crc ^= static_cast<uint32_t>(byte);
    for (uint32_t bit = 0U; bit < 8U; ++bit) {
      crc = ((crc & 1U) != 0U) ? ((crc >> 1U) ^ 0xA001U) : (crc >> 1U);
    }
  }
  return static_cast<uint16_t>(crc);
}

// Known vectors (Modbus over Serial Line): the request 01 03 00 00 00 0A has CRC C5CD (bytes CD C5
// ... wire order low first is C5 CD).
static_assert(Crc16(std::array<uint8_t, 6>{0x01, 0x03, 0x00, 0x00, 0x00, 0x0A}) == 0xCDC5U);

namespace detail {

/// The row that contains register `r`, or nullptr.
[[nodiscard]] constexpr const reg::RegisterSpec* RowContaining(uint32_t r) noexcept {
  for (const reg::RegisterSpec& row : reg::kRegisters) {
    if ((r >= row.address) && (r < (static_cast<uint32_t>(row.address) + row.count))) {
      return &row;
    }
  }
  return nullptr;
}

/// The newest firmware (`256a + 16b + c`) that any register of a read of `n` registers from `first`
/// needs: the read is as old as its newest register.
[[nodiscard]] constexpr uint16_t MinFirmwareOver(uint32_t first, uint32_t n) noexcept {
  uint16_t newest = 0U;
  for (uint32_t r = first; r < (first + n); ++r) {
    const reg::RegisterSpec* row = RowContaining(r);
    if ((row != nullptr) && (row->min_fw > newest)) {
      newest = row->min_fw;
    }
  }
  return newest;
}

/// The row that starts exactly at `r`, or nullptr.
[[nodiscard]] constexpr const reg::RegisterSpec* RowAt(uint32_t r) noexcept {
  for (const reg::RegisterSpec& row : reg::kRegisters) {
    if (r == row.address) {
      return &row;
    }
  }
  return nullptr;
}

/// Whether a read of `n` registers from `first` lies on readable registers only.
[[nodiscard]] constexpr bool ReadLegal(uint32_t first, uint32_t n) noexcept {
  if ((n < 1U) || (n > reg::kMaxRead)) {
    return false;
  }
  for (uint32_t r = first; r < (first + n); ++r) {
    const reg::RegisterSpec* row = RowContaining(r);
    if ((row == nullptr) || ((static_cast<uint32_t>(row->access) & 1U) == 0U)) {
      return false;
    }
  }
  return true;
}

/// Whether a write of `count` registers starting at `first` with `value` is legal.
[[nodiscard]] constexpr bool WriteLegal(uint32_t first, uint32_t count, uint32_t value) noexcept {
  const reg::RegisterSpec* row = RowAt(first);
  if ((row == nullptr) || (row->count != count) ||
      ((static_cast<uint32_t>(row->access) & 2U) == 0U)) {
    return false;
  }
  if (row->magic != reg::kNoMagic) {
    return value == row->magic;
  }
  return (value >= row->min_value) && (value <= row->max_value);
}

/// Seal a request: append the CRC low byte first and fill in the common fields.
[[nodiscard]] inline Request Seal(alicat_basis2::detail::RequestData& d, std::size_t n,
                                  uint8_t address, uint8_t function, uint16_t reply_bytes,
                                  bool idempotent) noexcept {
  const uint16_t crc = Crc16(std::span<const uint8_t>{d.bytes.data(), n});
  d.bytes[n] = static_cast<uint8_t>(crc & 0xFFU);
  d.bytes[n + 1U] = static_cast<uint8_t>(crc >> 8U);
  d.length = static_cast<uint8_t>(n + 2U);
  d.command = Command::Modbus;
  d.id = ::alicat_basis2::detail::CharOf(address);
  d.modbus = true;
  d.function = function;
  d.idempotent = idempotent;
  d.reply_chars = reply_bytes;
  return alicat_basis2::detail::RequestBuilder::Build(d);
}

} // namespace detail

/**
 * @brief Build a function 3 request.
 * @param address Modbus address, 1 to 247.
 * @param first First register.
 * @param count Number of registers, 1 to `reg::kMaxRead`, all on readable registers of the table.
 * @return The request, or `InvalidArgument` (nothing is formed).
 */
[[nodiscard]] inline Result<Request> BuildRead(uint8_t address, uint16_t first,
                                               uint16_t count) noexcept {
  if ((address < 1U) || (address > 247U) || !detail::ReadLegal(first, count)) {
    return Result<Request>::Failure(Error::InvalidArgument);
  }
  alicat_basis2::detail::RequestData d{};
  d.bytes[0] = address;
  d.bytes[1] = kReadHolding;
  d.bytes[2] = static_cast<uint8_t>(first >> 8U);
  d.bytes[3] = static_cast<uint8_t>(first & 0xFFU);
  d.bytes[4] = static_cast<uint8_t>(count >> 8U);
  d.bytes[5] = static_cast<uint8_t>(count & 0xFFU);
  return Result<Request>::Success(
      detail::Seal(d, 6U, address, kReadHolding, static_cast<uint16_t>(5U + (2U * count)), true));
}

/**
 * @brief Build a function 6 request (one register).
 * @param address Modbus address, 1 to 247.
 * @param target The register, a writable one-register row of the table.
 * @param value The value, within the row's range, or exactly its magic number.
 * @return The request, or `InvalidArgument`.
 */
[[nodiscard]] inline Result<Request> BuildWriteSingle(uint8_t address, uint16_t target,
                                                      uint16_t value) noexcept {
  if ((address < 1U) || (address > 247U) || !detail::WriteLegal(target, 1U, value)) {
    return Result<Request>::Failure(Error::InvalidArgument);
  }
  alicat_basis2::detail::RequestData d{};
  d.bytes[0] = address;
  d.bytes[1] = kWriteSingle;
  d.bytes[2] = static_cast<uint8_t>(target >> 8U);
  d.bytes[3] = static_cast<uint8_t>(target & 0xFFU);
  d.bytes[4] = static_cast<uint8_t>(value >> 8U);
  d.bytes[5] = static_cast<uint8_t>(value & 0xFFU);
  return Result<Request>::Success(detail::Seal(d, 6U, address, kWriteSingle, 8U, false));
}

/**
 * @brief Build a function 16 request for a two-register item (32-bit value, high register first).
 * @param address Modbus address, 1 to 247.
 * @param target The first register of a writable two-register row.
 * @param value The combined 32-bit value, within the row's range.
 * @return The request, or `InvalidArgument`.
 */
[[nodiscard]] inline Result<Request> BuildWriteMultiple(uint8_t address, uint16_t target,
                                                        uint32_t value) noexcept {
  if ((address < 1U) || (address > 247U) || !detail::WriteLegal(target, 2U, value)) {
    return Result<Request>::Failure(Error::InvalidArgument);
  }
  alicat_basis2::detail::RequestData d{};
  d.bytes[0] = address;
  d.bytes[1] = kWriteMultiple;
  d.bytes[2] = static_cast<uint8_t>(target >> 8U);
  d.bytes[3] = static_cast<uint8_t>(target & 0xFFU);
  d.bytes[4] = 0U;
  d.bytes[5] = 2U;
  d.bytes[6] = 4U;
  d.bytes[7] = static_cast<uint8_t>(value >> 24U);
  d.bytes[8] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
  d.bytes[9] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
  d.bytes[10] = static_cast<uint8_t>(value & 0xFFU);
  return Result<Request>::Success(detail::Seal(d, 11U, address, kWriteMultiple, 8U, false));
}

/**
 * @brief Whether `bytes` is exactly one request the driver may send: a valid CRC, an address 1 to
 * 247, function 3 on readable registers, function 6 or 16 on writable registers with a legal value.
 *
 * Independent of the builders: it parses the bytes back and checks them against the register table.
 *
 * @param bytes The candidate wire bytes.
 * @return true when legal.
 */
[[nodiscard]] inline bool IsLegalModbusRequest(std::span<const uint8_t> bytes) noexcept {
  const std::size_t n = bytes.size();
  if ((n < 8U) || (n > 13U) || (bytes[0] < 1U) || (bytes[0] > 247U)) {
    return false;
  }
  const uint16_t crc = Crc16(bytes.first(n - 2U));
  if ((bytes[n - 2U] != (crc & 0xFFU)) || (bytes[n - 1U] != (crc >> 8U))) {
    return false;
  }
  const auto first = static_cast<uint32_t>((static_cast<uint32_t>(bytes[2]) << 8U) | bytes[3]);
  const auto word = static_cast<uint32_t>((static_cast<uint32_t>(bytes[4]) << 8U) | bytes[5]);
  if ((bytes[1] == kReadHolding) && (n == 8U)) {
    return detail::ReadLegal(first, word);
  }
  if ((bytes[1] == kWriteSingle) && (n == 8U)) {
    return detail::WriteLegal(first, 1U, word);
  }
  if ((bytes[1] == kWriteMultiple) && (n == 13U) && (word == 2U) && (bytes[6] == 4U)) {
    const uint32_t value = (static_cast<uint32_t>(bytes[7]) << 24U) |
                           (static_cast<uint32_t>(bytes[8]) << 16U) |
                           (static_cast<uint32_t>(bytes[9]) << 8U) | bytes[10];
    return detail::WriteLegal(first, 2U, value);
  }
  return false;
}

/**
 * @brief How long the reply frame is, as far as the bytes received so far tell.
 * @param bytes What has arrived.
 * @return The full length (5 for an exception reply, `5 + byte count` for function 3, 8 for 6 and
 * 16), or 0 while it is not known yet; `kMaxLine + 1` for a function code the driver never sends.
 */
[[nodiscard]] inline std::size_t ReplyLength(std::span<const char> bytes) noexcept {
  if (bytes.size() < 2U) {
    return 0U;
  }
  const auto fn = static_cast<uint32_t>(::alicat_basis2::detail::ByteOf(bytes[1]));
  if ((fn & 0x80U) != 0U) {
    return 5U;
  }
  if ((fn == kWriteSingle) || (fn == kWriteMultiple)) {
    return 8U;
  }
  if (fn != kReadHolding) {
    return kMaxLine + 1U;
  }
  return (bytes.size() < 3U)
             ? 0U
             : (5U + static_cast<std::size_t>(::alicat_basis2::detail::ByteOf(bytes[2])));
}

/** @brief A parsed Modbus response. */
struct Response {
  std::array<uint16_t, reg::kMaxRead> words{}; ///< Register values of a read reply.
  uint8_t count{0};                            ///< Number of register values.
  uint8_t exception{0};                        ///< The exception code of a `Rejected` reply.
};

namespace detail {

/// The register values of a read reply whose CRC, address and function are already checked.
[[nodiscard]] inline Error ParseReadBody(const std::array<uint8_t, kMaxLine + 1U>& raw,
                                         std::size_t n, Response& out) noexcept {
  const std::size_t bytes = raw[2];
  if ((n != (5U + bytes)) || ((bytes % 2U) != 0U) || (bytes == 0U) ||
      ((bytes / 2U) > reg::kMaxRead)) {
    return Error::BadReply;
  }
  Response r{};
  r.count = static_cast<uint8_t>(bytes / 2U);
  for (std::size_t i = 0U; i < r.count; ++i) {
    r.words[i] = static_cast<uint16_t>((static_cast<uint32_t>(raw[3U + (2U * i)]) << 8U) |
                                       raw[4U + (2U * i)]);
  }
  out = r;
  return Error::Ok;
}

/// Functions 6 and 16 echo the first six bytes of the request (for 6 the value written, not the
/// value kept).
[[nodiscard]] inline bool IsWriteEcho(const std::array<uint8_t, kMaxLine + 1U>& raw, std::size_t n,
                                      std::span<const uint8_t> request_bytes) noexcept {
  if ((n != 8U) || (request_bytes.size() < 6U)) {
    return false;
  }
  for (std::size_t i = 0U; i < 6U; ++i) {
    if (raw[i] != request_bytes[i]) {
      return false;
    }
  }
  return true;
}

} // namespace detail

/**
 * @brief Parse and validate a response frame.
 * @param frame The received bytes.
 * @param request_address The address that was sent to.
 * @param request_function The function code that was sent.
 * @param request_bytes The request that was sent: the echo of a write is checked against it.
 * @param out The response; untouched on failure except `exception` of a `Rejected` reply.
 * @return `Ok`, `BadReply` (too short, wrong length, wrong byte count, wrong function), `Crc`,
 *         `DeviceIdMismatch` (another address) or `Rejected` (an exception reply; the code is in
 * `out.exception`).
 */
[[nodiscard]] inline Error ParseResponse(std::span<const char> frame, uint8_t request_address,
                                         uint8_t request_function,
                                         std::span<const uint8_t> request_bytes,
                                         Response& out) noexcept {
  const std::size_t n = frame.size();
  if ((n < 5U) || (n > (kMaxLine + 1U))) {
    return Error::BadReply;
  }
  std::array<uint8_t, kMaxLine + 1U> raw{};
  for (std::size_t i = 0U; i < n; ++i) {
    raw[i] = ::alicat_basis2::detail::ByteOf(frame[i]);
  }
  const uint16_t crc = Crc16(std::span<const uint8_t>{raw.data(), n - 2U});
  if ((raw[n - 2U] != (crc & 0xFFU)) || (raw[n - 1U] != (crc >> 8U))) {
    return Error::Crc;
  }
  if (raw[0] != request_address) {
    return Error::DeviceIdMismatch;
  }
  if (raw[1] == (request_function | 0x80U)) {
    out.exception = raw[2];
    return (n == 5U) ? Error::Rejected : Error::BadReply;
  }
  if (raw[1] != request_function) {
    return Error::BadReply;
  }
  if (request_function == kReadHolding) {
    return detail::ParseReadBody(raw, n, out);
  }
  if (!detail::IsWriteEcho(raw, n, request_bytes)) {
    return Error::BadReply;
  }
  out = Response{};
  return Error::Ok;
}

/** @} */

} // namespace alicat_basis2::modbus

#endif // ALICAT_BASIS2_MODBUS_HPP
