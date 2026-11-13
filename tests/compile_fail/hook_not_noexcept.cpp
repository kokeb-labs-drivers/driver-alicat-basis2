/**
 * @file hook_not_noexcept.cpp
 * @brief Must NOT compile: a Write hook that is not noexcept breaks the port contract, so the port
 * concept rejects it (handbook KB-CPP20-006).
 *
 * EXPECT: UartPort
 */
#include <cstddef>
#include <cstdint>

#include "alicat_basis2.hpp"

class ThrowingPort : public alicat_basis2::UartInterface<ThrowingPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Write(const uint8_t*, std::size_t) {
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
};

int main() {
  ThrowingPort port;
  alicat_basis2::Bus<ThrowingPort> bus(port);
  return static_cast<int>(bus.State());
}
