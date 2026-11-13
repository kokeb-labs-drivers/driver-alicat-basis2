/**
 * @file host_alicat_basis2_port.hpp
 * @brief The host port: the driver's serial hooks over a simulated BASIS 2 instrument, so the
 * examples run on any PC.
 *
 * The simulated instrument and its wire (`model::Instrument`, tests/mock_basis2.hpp) supply bytes,
 * the clock and the 3.5-character idle time. This class adds what a real port adds: `DelayMs()`
 * (here it advances the simulated clock instead of sleeping), `SetBaud()` and `Log()` to the
 * console. A port for a real serial line on a PC (a termios device, a USB serial adapter)
 * implements the same hooks: see docs/porting.md.
 */
#ifndef ALICAT_BASIS2_HOST_ALICAT_BASIS2_PORT_HPP
#define ALICAT_BASIS2_HOST_ALICAT_BASIS2_PORT_HPP

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "alicat_basis2.hpp"
#include "mock_basis2.hpp"

static_assert(alicat_basis2::Config{}.baud == alicat_basis2::kPortDefaultBaud,
              "the default configuration runs at the instrument's factory baud rate");

/// The host port: simulated instrument, real console.
class HostPort : public alicat_basis2::UartInterface<HostPort> {
public:
  /// The simulated instrument this port is wired to.
  explicit HostPort(model::Instrument& instrument) noexcept : instrument_(instrument) {}

  /// Nothing to open on the host.
  bool EnsureInitialized() noexcept {
    return true;
  }
  /// Send bytes to the instrument.
  bool Write(const uint8_t* data, std::size_t length) noexcept {
    return instrument_.WriteBytes(data, length);
  }
  /// Whatever has arrived, without waiting (the driver polls).
  std::size_t Read(uint8_t* buffer, std::size_t capacity, uint32_t timeout_ms) noexcept {
    static_cast<void>(timeout_ms);
    return instrument_.ReadBytes(buffer, capacity);
  }
  /// Drop received bytes.
  bool FlushInput() noexcept {
    instrument_.Flush();
    return true;
  }
  /// Milliseconds on the simulated clock.
  uint32_t NowMs() noexcept {
    return instrument_.NowMsValue();
  }
  /// Time passes in the simulated instrument; nothing sleeps.
  void DelayMs(uint32_t ms) noexcept {
    instrument_.AdvanceMs(ms);
  }
  /// The line speed a real port would set.
  bool SetBaud(uint32_t baud) noexcept {
    instrument_.port_baud = baud;
    return baud != 0U;
  }
  /// Driver messages go to the console, one line each.
  void Log(alicat_basis2::LogLevel level, const char* message) noexcept {
    static_cast<void>(level);
    static_cast<void>(std::puts(message)); // plain text: never pass it as a format string
  }

private:
  model::Instrument& instrument_;
};

#endif // ALICAT_BASIS2_HOST_ALICAT_BASIS2_PORT_HPP
