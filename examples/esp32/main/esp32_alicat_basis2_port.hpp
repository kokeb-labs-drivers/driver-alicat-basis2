/**
 * @file esp32_alicat_basis2_port.hpp
 * @brief ESP-IDF port for the BASIS 2 driver: the UART driver for the line, `esp_timer` for the
 * clock, ESP_LOG for the log hook.
 *
 * `Write()` returns after the last byte has left the shift register (`uart_wait_tx_done`), which is
 * what the driver's silent-interval timing and an RS-485 transceiver both need. With
 * `CONFIG_ALICAT_BASIS2_EXAMPLE_RS485` the UART runs in the IDF's half-duplex RS-485 mode and
 * drives the transceiver's direction pin on RTS. The pins are Kconfig values
 * (main/Kconfig.projbuild); no board is assumed.
 */
#ifndef ALICAT_BASIS2_ESP32_ALICAT_BASIS2_PORT_HPP
#define ALICAT_BASIS2_ESP32_ALICAT_BASIS2_PORT_HPP

#include <cstddef>
#include <cstdint>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "alicat_basis2.hpp"

/// The line speed the port opens the UART at; it is the instrument's factory setting.
inline constexpr uint32_t kEsp32Baud = 38400U;
static_assert(kEsp32Baud == alicat_basis2::kPortDefaultBaud,
              "the port opens the UART at the instrument's factory baud rate");
static_assert((kEsp32Baud >= alicat_basis2::kMinBaudRate) &&
                  (kEsp32Baud <= alicat_basis2::kMaxBaudRate),
              "the baud rate is not one the instrument offers");

/// The ESP-IDF serial port, with the optional hooks `DelayMs`, `SetBaud` and `Log`.
class Esp32Port : public alicat_basis2::UartInterface<Esp32Port> {
public:
  /// UART 1 on the pins chosen in menuconfig.
  Esp32Port() noexcept = default;

  bool EnsureInitialized() noexcept {
    if (ready_) {
      return true;
    }
    uart_config_t cfg{};
    cfg.baud_rate = static_cast<int>(kEsp32Baud);
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    if ((uart_driver_install(kPort, 512, 0, 0, nullptr, 0) != ESP_OK) ||
        (uart_param_config(kPort, &cfg) != ESP_OK) ||
        (uart_set_pin(kPort, CONFIG_ALICAT_BASIS2_EXAMPLE_TX_PIN,
                      CONFIG_ALICAT_BASIS2_EXAMPLE_RX_PIN, kRtsPin,
                      UART_PIN_NO_CHANGE) != ESP_OK)) {
      return false;
    }
#if CONFIG_ALICAT_BASIS2_EXAMPLE_RS485
    // The IDF raises RTS while it transmits; wire RTS to the transceiver's DE and RE pins.
    if (uart_set_mode(kPort, UART_MODE_RS485_HALF_DUPLEX) != ESP_OK) {
      return false;
    }
#endif
    ready_ = true;
    return true;
  }

  bool Write(const uint8_t* data, std::size_t len) noexcept {
    if (uart_write_bytes(kPort, data, len) != static_cast<int>(len)) {
      return false;
    }
    return uart_wait_tx_done(kPort, pdMS_TO_TICKS(1000)) ==
           ESP_OK; // the last byte has left the pin
  }

  /// Whatever has arrived, without waiting: the driver polls and keeps its own time.
  std::size_t Read(uint8_t* data, std::size_t capacity, uint32_t timeout_ms) noexcept {
    static_cast<void>(timeout_ms);
    const int n = uart_read_bytes(kPort, data, static_cast<uint32_t>(capacity), 0);
    return n > 0 ? static_cast<std::size_t>(n) : 0U;
  }

  bool FlushInput() noexcept {
    return uart_flush_input(kPort) == ESP_OK;
  }

  /// Milliseconds since boot from the 64-bit microsecond timer; the driver uses wrap-safe
  /// differences.
  uint32_t NowMs() noexcept {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
  }

  void DelayMs(uint32_t ms) noexcept {
    vTaskDelay(pdMS_TO_TICKS(ms) + 1U);
  }

  /// Used only by `ChangeBaud()`, after the instrument has switched.
  bool SetBaud(uint32_t baud) noexcept {
    return uart_set_baudrate(kPort, baud) == ESP_OK;
  }

  void Log(alicat_basis2::LogLevel level, const char* message) noexcept {
    const esp_log_level_t esp_level = level == alicat_basis2::LogLevel::Error     ? ESP_LOG_ERROR
                                      : level == alicat_basis2::LogLevel::Warning ? ESP_LOG_WARN
                                      : level == alicat_basis2::LogLevel::Info    ? ESP_LOG_INFO
                                                                                  : ESP_LOG_DEBUG;
    esp_log_write(esp_level, kTag, "%s\n", message); // plain text, never a format
  }

private:
  static constexpr uart_port_t kPort = UART_NUM_1;
  static constexpr const char* kTag = "alicat_basis2";
#if CONFIG_ALICAT_BASIS2_EXAMPLE_RS485
  static constexpr int kRtsPin = CONFIG_ALICAT_BASIS2_EXAMPLE_DE_PIN;
#else
  static constexpr int kRtsPin = UART_PIN_NO_CHANGE;
#endif
  bool ready_ = false;
};

#endif // ALICAT_BASIS2_ESP32_ALICAT_BASIS2_PORT_HPP
