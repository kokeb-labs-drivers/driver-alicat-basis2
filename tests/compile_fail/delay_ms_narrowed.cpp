/**
 * @file delay_ms_narrowed.cpp
 * @brief Must NOT compile: a DelayMs hook with a narrower parameter would truncate long waits, so
 * the bus rejects it instead of ignoring it (handbook KB-CPP20-007).
 *
 * EXPECT: DelayMs hook must be
 */
#include "mock_basis2.hpp"

class ShortDelayPort : public alicat_basis2::UartInterface<ShortDelayPort> {
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
  uint32_t NowMs() noexcept {
    return 0U;
  }
  void DelayMs(uint16_t) noexcept {}
};

int main() {
  ShortDelayPort port;
  alicat_basis2::Bus<ShortDelayPort> bus(port);
  return 0;
}
