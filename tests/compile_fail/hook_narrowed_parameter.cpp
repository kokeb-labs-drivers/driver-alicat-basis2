/**
 * @file hook_narrowed_parameter.cpp
 * @brief Must NOT compile: a Read hook with a narrower length would truncate replies, so the port
 * concept rejects it (handbook KB-CPP20-006).
 *
 * EXPECT: UartPort
 */
#include <cstddef>
#include <cstdint>

#include "alicat_basis2.hpp"

class NarrowPort : public alicat_basis2::UartInterface<NarrowPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Write(const uint8_t*, std::size_t) noexcept {
    return true;
  }
  std::size_t Read(uint8_t*, uint8_t, uint32_t) noexcept {
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
  NarrowPort port;
  alicat_basis2::Bus<NarrowPort> bus(port);
  return static_cast<int>(bus.State());
}
