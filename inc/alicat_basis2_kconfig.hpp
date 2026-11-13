/**
 * @file alicat_basis2_kconfig.hpp
 * @brief Optional ESP-IDF layer: a `Config` built from the `CONFIG_ALICAT_BASIS2_*` Kconfig
 * options.
 * @copyright Copyright (c) 2026 The driver-alicat-basis2 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Include this header only where you want menuconfig to choose the defaults; the driver never
 * includes it and no driver signature depends on it. Under ESP-IDF the values come from
 * `sdkconfig.h`. Without Kconfig every option falls back to the datasheet default, so
 * `KconfigDefaults()` equals `Config{}`.
 *
 * Kconfig writes a bool option set to "n" as an absent macro, so an absent
 * `CONFIG_ALICAT_BASIS2_MODBUS` means ASCII in both cases.
 *
 * One configuration per program: the inline function below depends on the `CONFIG_` macros, so
 * every translation unit of one program must see the same values (ESP-IDF has one `sdkconfig.h`
 * per build; each host test is its own program). No driver or library header includes this one
 * (handbook KB-CPP20-018).
 */
#ifndef ALICAT_BASIS2_KCONFIG_HPP
#define ALICAT_BASIS2_KCONFIG_HPP

#ifdef __has_include
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif

#include "alicat_basis2_types.hpp"

#ifndef CONFIG_ALICAT_BASIS2_PROCESSING_MS
/** Instrument turnaround added to every reply wait when Kconfig does not set it: 50 ms. */
#define CONFIG_ALICAT_BASIS2_PROCESSING_MS 50
#endif
#ifndef CONFIG_ALICAT_BASIS2_SLOW_COMMAND_MS
/** Reply wait of commands that may write non-volatile memory when Kconfig does not set it: 1000 ms.
 */
#define CONFIG_ALICAT_BASIS2_SLOW_COMMAND_MS 1000
#endif
#ifndef CONFIG_ALICAT_BASIS2_READ_RETRIES
/** Retries of a failed read when Kconfig does not set it: 1. */
#define CONFIG_ALICAT_BASIS2_READ_RETRIES 1
#endif

namespace alicat_basis2 {

static_assert((CONFIG_ALICAT_BASIS2_PROCESSING_MS >= 1) &&
                  (CONFIG_ALICAT_BASIS2_PROCESSING_MS <= 1000),
              "ALICAT_BASIS2_PROCESSING_MS must be 1 to 1000");
static_assert(
    (CONFIG_ALICAT_BASIS2_SLOW_COMMAND_MS >= CONFIG_ALICAT_BASIS2_PROCESSING_MS) &&
        (CONFIG_ALICAT_BASIS2_SLOW_COMMAND_MS <= 10000),
    "ALICAT_BASIS2_SLOW_COMMAND_MS must be at least the processing time and at most 10000");
static_assert((CONFIG_ALICAT_BASIS2_READ_RETRIES >= 0) && (CONFIG_ALICAT_BASIS2_READ_RETRIES <= 3),
              "ALICAT_BASIS2_READ_RETRIES must be 0 to 3");

/**
 * @brief The configuration menuconfig selected, on top of the datasheet defaults.
 * @return `Config{}` with the protocol, baud rate, timing and retries taken from Kconfig.
 */
constexpr Config KconfigDefaults() noexcept {
  Config c{};
#ifdef CONFIG_ALICAT_BASIS2_MODBUS
  c.protocol = Protocol::ModbusRtu;
#endif
#ifdef CONFIG_ALICAT_BASIS2_BAUD_4800
  c.baud = 4800U;
#elif defined(CONFIG_ALICAT_BASIS2_BAUD_9600)
  c.baud = 9600U;
#elif defined(CONFIG_ALICAT_BASIS2_BAUD_19200)
  c.baud = 19200U;
#elif defined(CONFIG_ALICAT_BASIS2_BAUD_57600)
  c.baud = 57600U;
#elif defined(CONFIG_ALICAT_BASIS2_BAUD_115200)
  c.baud = 115200U;
#else
  c.baud = 38400U;
#endif
  c.processing_ms = static_cast<uint16_t>(CONFIG_ALICAT_BASIS2_PROCESSING_MS);
  c.slow_command_ms = static_cast<uint16_t>(CONFIG_ALICAT_BASIS2_SLOW_COMMAND_MS);
  c.read_retries = static_cast<uint8_t>(CONFIG_ALICAT_BASIS2_READ_RETRIES);
  return c;
}

} // namespace alicat_basis2

#endif // ALICAT_BASIS2_KCONFIG_HPP
