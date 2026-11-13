// clang-format off: the table below must keep one row per line.
/**
 * @file alicat_basis2_uart_interface.hpp
 * @brief The serial port of the ALICAT_BASIS2 driver: the CRTP base, the `UartPort` concept and `Bus<Port>`, the
 * transaction machine shared by every instrument on one serial line.
 * @copyright Copyright (c) 2026 The driver-alicat-basis2 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * The driver never calls an SDK. It calls hooks on a class you derive from `alicat_basis2::UartInterface<Self>`:
 *
 * | Hook | Contract |
 * | --- | --- |
 * | `bool EnsureInitialized() noexcept` | Bring the UART up (8N1 at the configured baud rate); `true` at once afterwards |
 * | `bool Write(const uint8_t* data, std::size_t len) noexcept` | Send exactly `len` bytes and return only after the last stop bit has left the shift register (RS-485: the bus is released); `false` on any error |
 * | `std::size_t Read(uint8_t* out, std::size_t cap, uint32_t timeout_ms) noexcept` | Copy up to `cap` received bytes into `out` and return the count (0 to `cap`); wait up to `timeout_ms` for the first byte only; `timeout_ms == 0` never blocks |
 * | `bool FlushInput() noexcept` | Discard every received byte not read yet, including the UART FIFO |
 * | `uint32_t NowMs() noexcept` | Monotonic millisecond counter; wrap-around is handled |
 *
 * Optional hooks (a wrong signature is a compile error): `void DelayMs(uint32_t)` (the blocking wrappers sleep
 * through it, else they spin on `NowMs()`), `bool SetBaud(uint32_t) noexcept` (needed by `ChangeBaud()`),
 * `void Log(LogLevel, const char*) noexcept`. RS-485 direction control belongs inside `Write()`. TTL levels are
 * the port's business: the instrument offers RS-232 or RS-485 levels (M p.7).
 */
// clang-format on
#ifndef ALICAT_BASIS2_UART_INTERFACE_HPP
#define ALICAT_BASIS2_UART_INTERFACE_HPP

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

#include "alicat_basis2_ascii.hpp"
#include "alicat_basis2_detail.hpp"
#include "alicat_basis2_modbus.hpp"
#include "alicat_basis2_request.hpp"
#include "alicat_basis2_types.hpp"

namespace alicat_basis2 {

/**
 * @name Bus requirements
 * What the instrument needs from the serial line and its supply (manual pp. 6, 10 and 17). A port
 * and a board configuration check their settings against these with `static_assert` (handbook
 * KB-CPP-123).
 * @{
 */
/// The baud rate after a factory restore, 8 data bits, no parity, 1 stop bit (M p.10).
inline constexpr uint32_t kPortDefaultBaud = 38'400;
/// The lowest baud rate the instrument offers (M p.17).
inline constexpr uint32_t kMinBaudRate = 4'800;
/// The highest baud rate the instrument offers, firmware 2.2.2 or later (M p.17).
inline constexpr uint32_t kMaxBaudRate = 115'200;
/// The idle time before the instrument acts on a command, in bit times: 3.5 characters of 10 bits
/// (M p.10).
inline constexpr uint32_t kSilentBitTimes = 35;
/// The instrument's turnaround that the reply wait adds to the line time, microseconds (an
/// assumption, K-PROC).
inline constexpr uint32_t kTurnaroundUs = 50'000;
/// Lowest supply, millivolts, on pin 4 only (M p.6).
inline constexpr uint32_t kSupplyMinMv = 12'000;
/// Highest supply, millivolts (M p.6).
inline constexpr uint32_t kSupplyMaxMv = 24'000;
/** @} */

/**
 * @brief CRTP base of the platform serial port.
 *
 * Derive `class MyPort : public alicat_basis2::UartInterface<MyPort>` and implement the hooks
 * above. The base declares no hook and no data; the driver checks your class with the `UartPort`
 * concept.
 *
 * @tparam Derived Your port class.
 */
template <typename Derived>
class UartInterface {
public:
  /** @brief Ports are not copied: the bus holds a reference. */
  UartInterface(const UartInterface&) = delete;
  /** @brief Ports are not copied: the bus holds a reference. */
  UartInterface& operator=(const UartInterface&) = delete;

protected:
  /** @brief Only the derived port is constructed. */
  UartInterface() noexcept = default;
  /** @brief Destroyed through the derived port only. */
  ~UartInterface() noexcept = default;
};

/**
 * @brief Satisfied by a class that derives from `UartInterface<Port>` and implements the five
 * required hooks with exactly the types above, `noexcept`.
 * @tparam Port The port class.
 */
template <typename Port>
concept UartPort = std::derived_from<Port, UartInterface<Port>> && requires {
  detail::ExactHook<Port, bool() noexcept>::Take(&Port::EnsureInitialized);
  detail::ExactHook<Port, bool(const uint8_t*, std::size_t) noexcept>::Take(&Port::Write);
  detail::ExactHook<Port, std::size_t(uint8_t*, std::size_t, uint32_t) noexcept>::Take(&Port::Read);
  detail::ExactHook<Port, bool() noexcept>::Take(&Port::FlushInput);
  detail::ExactHook<Port, uint32_t() noexcept>::Take(&Port::NowMs);
};

namespace detail {

/// The port declares something called `DelayMs`.
template <typename Port>
concept DeclaresDelayMs = requires(Port& port) { port.DelayMs(1_u32); };
/// The port sleeps: `void DelayMs(uint32_t ms) noexcept`.
template <typename Port>
concept HasDelayMs = requires { ExactHook<Port, void(uint32_t) noexcept>::Take(&Port::DelayMs); };
/// The port declares something called `SetBaud`.
template <typename Port>
concept DeclaresSetBaud = requires(Port& port) { port.SetBaud(1_u32); };
/// The port changes its speed: `bool SetBaud(uint32_t baud) noexcept`.
template <typename Port>
concept HasSetBaud = requires { ExactHook<Port, bool(uint32_t) noexcept>::Take(&Port::SetBaud); };
/// The port declares something called `Log`.
template <typename Port>
concept DeclaresLog = requires(Port& port) { port.Log(LogLevel::Info, ""); };
/// The port takes log lines: `void Log(LogLevel, const char* message) noexcept`.
template <typename Port>
concept HasLog =
    requires { ExactHook<Port, void(LogLevel, const char*) noexcept>::Take(&Port::Log); };

} // namespace detail

/** @brief What the bus needs to know about the instrument it talks to, per transaction. */
struct Timing {
  uint32_t baud{38400};          ///< The line speed, sizes every wait.
  uint16_t processing_ms{50};    ///< Turnaround added to the reply wait.
  uint16_t slow_ms{1000};        ///< Turnaround of a slow command.
  EchoMode echo{EchoMode::None}; ///< Drop a first line equal to the command sent.
  /// When not 0, `SetBaud()` is called right after the send and the reply is read at this speed (M
  /// p.17).
  uint32_t switch_to_baud{0};
};

/** @brief State of a transaction as seen by `Bus::Poll()`. */
enum class PollState : uint8_t {
  Idle = 0,    ///< Nothing pending and no result waiting.
  Pending = 1, ///< Waiting for the gap, or for the reply.
  Done = 2,    ///< The result is ready: `Take()` returns the reply.
  Failed = 3,  ///< The transaction failed: `Take()` returns the error.
};

/** @brief Internal phase of the bus. */
enum class BusState : uint8_t {
  Idle = 0,       ///< Nothing happening.
  Gap = 1,        ///< Waiting out the idle time before a send.
  AwaitReply = 2, ///< Accumulating the reply.
  Settling = 3,   ///< After a transaction: hold-off or quarantine drain.
};

/**
 * @brief The transaction machine of one serial line, shared by every instrument on it.
 *
 * One transaction at a time ("behavior undefined" otherwise, M p.10): `Submit()` on a busy bus
 * returns `Busy` and sends nothing. After a timeout, an overflow, a bad line or a wrong-ID reply
 * the bus waits out the failed request's reply wait and drains the line until it has been quiet
 * for t3.5 + 2 ms before it sends again, so a late reply is never taken for the next answer. A
 * request that never reached the wire (the line did not go quiet) only needs the quiet line. While
 * idle, bytes that arrive are counted as stray lines and never used.
 *
 * @tparam Port Your class derived from `UartInterface<Port>`.
 */
template <UartPort Port>
class Bus {
  static_assert(!detail::DeclaresDelayMs<Port> || detail::HasDelayMs<Port>,
                "alicat_basis2: the DelayMs hook must be `void DelayMs(uint32_t ms) noexcept`");
  static_assert(!detail::DeclaresSetBaud<Port> || detail::HasSetBaud<Port>,
                "alicat_basis2: the SetBaud hook must be `bool SetBaud(uint32_t baud) noexcept`");
  static_assert(
      !detail::DeclaresLog<Port> || detail::HasLog<Port>,
      "alicat_basis2: the Log hook must be `void Log(LogLevel, const char* message) noexcept`");

public:
  /** @brief Bind to a port; no I/O. @param port The port; it must outlive the bus. */
  explicit Bus(Port& port) noexcept : port_(port) {}
  /** @brief One bus per serial line. */
  Bus(const Bus&) = delete;
  /** @brief One bus per serial line. */
  Bus& operator=(const Bus&) = delete;

  /** @return The port. */
  Port& GetPort() & noexcept {
    return port_;
  }

  /** @return true when the port has a `SetBaud()` hook. */
  [[nodiscard]] static constexpr bool CanSetBaud() noexcept {
    return detail::HasSetBaud<Port>;
  }

  /**
   * @brief Change the port's speed through the optional hook.
   * @param baud The new speed.
   * @return `Ok`, `Unsupported` (no hook) or `Bus` (the hook failed).
   */
  Result<void> ApplyBaud(uint32_t baud) noexcept;

  /**
   * @brief Start a transaction.
   * @param request A request from the factories; its wire bytes are checked once more against the
   * validators.
   * @param timing The line speed and turnaround times.
   * @return `Ok`; `Busy` (a transaction or its settling is not over: nothing sent);
   * `InvalidArgument` (an empty request or a wire violation: counted in `WireViolations()`).
   */
  Error Submit(const Request& request, const Timing& timing) noexcept;

  /** @brief Advance the machine without blocking. @return The state. */
  PollState Poll() noexcept;

  /** @brief Take the result of a finished transaction. @return The reply, or why there is none. */
  Result<Reply> Take() noexcept;

  /** @brief Drop the pending transaction and drain the line before the next send. */
  void Abort() noexcept;

  /** @brief Drain the line until quiet before the next send (a decoder found the reply unusable).
   */
  void EnterQuarantine() noexcept;

  /**
   * @brief Run one transaction to the end: wait for the bus, submit, poll, take. Blocks with
   * `DelayMs()` or spins. A non-blocking result that was never `Take()`n is dropped (its
   * transaction is over), and the wait for a busy bus is bounded by `WaitBoundMs()`. A request
   * whose line is not quiet within one line wait (its reply wait without its own duration) ends
   * `Busy`, unsent; the next request then waits only for the quiet line.
   * @param request The request.
   * @param timing The timing.
   * @return The reply or the error; always returns within the computed deadlines.
   */
  Result<Reply> Execute(const Request& request, const Timing& timing) noexcept;

  /**
   * @brief The longest `Execute()` waits for the transaction submitted last to end before it gives
   * up with `Busy`: the gap and one line wait (a reply wait without the request's own duration)
   * for a quiet line, the request's line time, one reply wait, a quarantine of two, its hold-off,
   * a quiet margin of 2 ms and a slack of 2 ms per phase (four) for a sleep that overshoots.
   * @return Milliseconds.
   */
  [[nodiscard]] uint32_t WaitBoundMs() const noexcept {
    return busyBoundMs();
  }

  /** @return Complete lines that arrived while no transaction waited. */
  [[nodiscard]] uint32_t StrayLineCount() const noexcept {
    return stray_lines_;
  }
  /** @return true when a stray line began with `@`: a streaming peer (A19). */
  [[nodiscard]] bool StreamingSuspected() const noexcept {
    return streaming_;
  }
  /** @return Requests refused by the wire validator; must stay 0. */
  [[nodiscard]] uint32_t WireViolations() const noexcept {
    return wire_violations_;
  }
  /** @return The state of the machine. */
  [[nodiscard]] BusState State() const noexcept {
    return state_;
  }
  /** @return Number of instruments the application declared on this line. */
  [[nodiscard]] uint8_t DeviceCount() const noexcept {
    return device_count_;
  }
  /** @param count Number of instruments on the line; a change of unit ID is refused when it is
   * above one. */
  void SetDeviceCount(uint8_t count) noexcept {
    device_count_ = count;
  }

private:
  void log(LogLevel level, const char* message) noexcept;
  void sleep(uint32_t ms) noexcept;
  void send(uint32_t now) noexcept;
  void pollReply(uint32_t now) noexcept;
  void feedAscii(uint8_t byte, uint32_t now) noexcept;
  void feedModbus(uint8_t byte) noexcept;
  void finishLine() noexcept;
  void complete(Error error, bool quarantine, uint32_t now) noexcept;
  void startQuarantine(uint32_t now) noexcept;
  void settle(uint32_t now) noexcept;
  void drainStray(uint32_t now) noexcept;
  void strayByte(uint8_t byte) noexcept;
  std::size_t readChunk(uint32_t now) noexcept;
  [[nodiscard]] uint32_t gapMs() const noexcept;
  [[nodiscard]] uint32_t replyMs() const noexcept;
  [[nodiscard]] uint32_t lineWaitMs() const noexcept;
  [[nodiscard]] uint32_t busyBoundMs() const noexcept;
  void dropUntaken() noexcept;
  [[nodiscard]] PollState pollState() const noexcept;

  Port& port_;
  BusState state_{BusState::Idle};
  Timing timing_{};
  Request request_;
  std::array<uint8_t, 32> chunk_{};
  std::array<char, kMaxLine + 1U> line_{};
  std::size_t line_len_{0};
  Reply reply_{};
  Error result_{Error::Ok};
  bool result_ready_{false};
  bool echo_pending_{false};
  bool have_activity_{false};
  bool quarantine_{false};
  bool streaming_{false};
  uint32_t last_activity_ms_{0};
  // Each wait is a start time and a duration, compared as an unsigned elapsed time (wrap-safe).
  uint32_t gap_start_ms_{0}; ///< When the request entered the gap.
  uint32_t gap_limit_ms_{0}; ///< Not quiet within this long after it: the request is not sent.
  bool on_wire_{false};      ///< `Write()` was called for the request: a reply may come.
  uint32_t sent_ms_{0};      ///< When `Write()` returned; the reply wait and hold-off run from it.
  uint32_t holdoff_ms_{0};   ///< The request's hold-off, from the send.
  bool has_holdoff_{false};  ///< The request has a hold-off.
  uint32_t settle_start_ms_{0}; ///< When the quarantine or hold-off started.
  uint32_t settle_ms_{0};       ///< How long it lasts at most.
  uint32_t quiet_min_ms_{0};    ///< A quarantine ends on a quiet line only after this long.
  uint32_t stray_lines_{0};
  uint32_t wire_violations_{0};
  uint32_t reply_wait_ms_{0};
  std::size_t stray_len_{0};
  char stray_first_{'\0'};
  uint8_t device_count_{1};
};

} // namespace alicat_basis2

// codeql::misra_deviation_next_line(MD-CPP-009)
#include "../src/alicat_basis2_bus.ipp"

#endif // ALICAT_BASIS2_UART_INTERFACE_HPP
