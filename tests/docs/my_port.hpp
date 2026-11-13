/**
 * @file my_port.hpp
 * @brief The serial port shown on the Quickstart and Porting pages, compiled by the host build.
 *
 * Every platform call goes through the `platform_*` functions declared below; a real port replaces
 * them with its SDK (ESP-IDF, STM32 HAL, Zephyr, Linux termios).
 */
#ifndef ALICAT_BASIS2_MY_PORT_HPP
#define ALICAT_BASIS2_MY_PORT_HPP

#include <cstddef>
#include <cstdint>

#include "alicat_basis2_uart_interface.hpp"

// Your SDK's calls. Declared only: the docs build checks that the port compiles.
bool platform_uart_init() noexcept; // 38400 8N1, RS-232 or RS-485
bool platform_uart_write(const uint8_t* data, std::size_t len) noexcept; // all bytes or false
std::size_t platform_uart_read(uint8_t* data,
                               std::size_t cap) noexcept; // what has arrived, no waiting
bool platform_uart_flush_input() noexcept;
uint32_t platform_millis() noexcept; // a monotonic millisecond clock

// --8<-- [start:port]
class MyPort : public alicat_basis2::UartInterface<MyPort> {
public:
  // Bring the UART up once; later calls return true at once.
  bool EnsureInitialized() noexcept {
    if (!ready_) {
      ready_ = platform_uart_init();
    }
    return ready_;
  }

  bool Write(const uint8_t* data, std::size_t len) noexcept {
    return platform_uart_write(data, len);
  }

  // Whatever has arrived, up to `capacity` bytes. The driver polls, so do not wait for a full
  // frame.
  std::size_t Read(uint8_t* data, std::size_t capacity, uint32_t timeout_ms) noexcept {
    static_cast<void>(timeout_ms);
    return platform_uart_read(data, capacity);
  }

  // Drop stale bytes before each request.
  bool FlushInput() noexcept {
    return platform_uart_flush_input();
  }

  // The driver times every wait against this clock.
  uint32_t NowMs() noexcept {
    return platform_millis();
  }

private:
  bool ready_ = false;
};
// --8<-- [end:port]

#endif // ALICAT_BASIS2_MY_PORT_HPP
