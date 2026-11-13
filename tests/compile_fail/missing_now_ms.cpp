/**
 * @file missing_now_ms.cpp
 * @brief Must NOT compile: every wait of the driver is measured on the port's `NowMs()`, so a port
 * without it is not a port.
 *
 * EXPECT: UartPort
 */
#include <cstddef>
#include <cstdint>

#include "alicat_basis2.hpp"

class NoClockPort : public alicat_basis2::UartInterface<NoClockPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Write(const uint8_t*, std::size_t) noexcept {
    return true;
  }
  std::size_t Read(uint8_t*, std::size_t, uint32_t) noexcept {
    return 0U;
  }
  bool FlushInput() noexcept {
    return true;
  }
};

int main() {
  NoClockPort port;
  alicat_basis2::Bus<NoClockPort> bus(port);
  return 0;
}
