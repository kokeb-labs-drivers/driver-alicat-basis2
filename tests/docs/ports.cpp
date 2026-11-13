/**
 * @file ports.cpp
 * @brief The port with every optional hook, as shown on the Porting page. Compiled, never run.
 */
#include <cstddef>
#include <cstdint>

#include "alicat_basis2.hpp"

bool platform_uart_init() noexcept;
bool platform_uart_write(const uint8_t* data, std::size_t len) noexcept;
std::size_t platform_uart_read(uint8_t* data, std::size_t cap) noexcept;
bool platform_uart_flush_input() noexcept;
uint32_t platform_millis() noexcept;
void platform_sleep_ms(uint32_t ms) noexcept;
bool platform_uart_set_baud(uint32_t baud) noexcept;
void platform_log(const char* line) noexcept;

// --8<-- [start:full-port]
class FullPort : public alicat_basis2::UartInterface<FullPort> {
public:
  bool EnsureInitialized() noexcept {
    return platform_uart_init();
  }
  bool Write(const uint8_t* data, std::size_t len) noexcept {
    return platform_uart_write(data, len);
  }
  std::size_t Read(uint8_t* data, std::size_t capacity, uint32_t timeout_ms) noexcept {
    static_cast<void>(timeout_ms);
    return platform_uart_read(data, capacity);
  }
  bool FlushInput() noexcept {
    return platform_uart_flush_input();
  }
  uint32_t NowMs() noexcept {
    return platform_millis();
  }

  // Optional: lets blocking calls yield instead of spinning. Without it they poll the clock.
  void DelayMs(uint32_t ms) noexcept {
    platform_sleep_ms(ms);
  }

  // Optional: needed by ChangeBaud(), which changes the line speed after the instrument has
  // switched.
  bool SetBaud(uint32_t baud) noexcept {
    return platform_uart_set_baud(baud);
  }

  // Optional: one line per lifecycle event or failure. The driver never formats; the port does.
  void Log(alicat_basis2::LogLevel level, const char* message) noexcept {
    if (level > alicat_basis2::LogLevel::Warning) {
      return;
    }
    platform_log(message); // plain text: never use it as a format string
  }
};
// --8<-- [end:full-port]

// --8<-- [start:nonblocking]
// The non-blocking form: submit once, call Poll() from your loop, take the reply when it is no
// longer Pending. Basis2 uses Bus::Execute(), which does exactly this and waits with DelayMs() or
// the clock.
alicat_basis2::Error StartPoll(alicat_basis2::Bus<FullPort>& bus) {
  const auto request = alicat_basis2::ascii::Make(alicat_basis2::Command::Poll, 'A',
                                                  alicat_basis2::ascii::Args{}, false);
  if (!request) {
    return request.error;
  }
  return bus.Submit(request.value,
                    alicat_basis2::Timing{}); // Busy while another transaction is not over
}

bool Step(alicat_basis2::Bus<FullPort>& bus, alicat_basis2::Result<alicat_basis2::Reply>& out) {
  const alicat_basis2::PollState state = bus.Poll();
  if ((state == alicat_basis2::PollState::Done) || (state == alicat_basis2::PollState::Failed)) {
    out = bus.Take();
    return true;
  }
  return false;
}
// --8<-- [end:nonblocking]
